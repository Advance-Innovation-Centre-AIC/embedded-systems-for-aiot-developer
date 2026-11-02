/*
 * picture.c -- ui.Picture ตัวเดียว (one Picture)
 *
 * ไอคอนสามรูปจากชื่อ (text=) รูปแรกอยู่นิ่ง รูปที่สองหมุน (PROP_ROTATION หน่วย 0.1
 * องศา) รูปที่สามย่อขยาย (PROP_SCALE 256 = เท่าเดิม) ชื่อไอคอนมีห้าชื่อเท่านั้น
 * รันจบเองใน 24 วินาที
 *
 * Three icons chosen by name (text=): the first stays still, the second
 * rotates (PROP_ROTATION in tenths of a degree), the third grows
 * (PROP_SCALE, 256 = original size). There are only five icon names.
 * The example ends by itself after 24 seconds.
 *
 * Twin of picture.py
 * (embedded-systems-for-aiot-developer/examples/widgets/picture.py),
 * merged into one file from the library twin wref-picture.
 * Style: CLEAN_C.md.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Layout on the 792 x 398 canvas: three icons in a row, each with
 * its caption. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    TITLE_PX = 28,
    ICON_Y = 96,
    ICON_SIZE = 128,
    CAPTION_Y = 296,
    CAPTION_PX = 20,
    FOOT_Y = 344,
    ICON_COUNT = 3,
    ICON_SPIN = 1,
    ICON_GROW = 2
};

/* Timing and the two animations. */
enum {
    SETTLE_MS = 200,
    ROUNDS = 60,
    ROUND_MS = 400,
    LAST_ROUND = 59,
    TURN_STEP = 60, /* tenths of a degree per round */
    FULL_TURN = 3600,
    SCALE_START = 128, /* half size */
    /* added over the rounds: from half to one and a half */
    SCALE_GROWTH = 256
};

/* time.sleep_ms(): BENTO_E_CANCELLED when Stop arrives, as the
 * original's KeyboardInterrupt. */
static bento_status_t pause_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623
};

/* An icon: its name, where it stands, its colour and its caption. */
typedef struct {
    const char *name;
    int16_t x;
    uint32_t color;
    const char *caption;
} icon_spec_t;

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_icon(bento_ui_t *ui, const icon_spec_t *icon,
                               bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = icon->name;
    args.text_len = (uint32_t)strlen(icon->name);
    args.x = icon->x;
    args.y = ICON_Y;
    args.w = ICON_SIZE;
    args.h = ICON_SIZE;
    args.color = icon->color;
    return bento_ui_picture_create(ui, &args, out_id);
}

static bento_status_t build_screen(bento_ui_t *ui,
                                   bento_ui_id_t icons[ICON_COUNT])
{
    static const icon_spec_t icon_specs[ICON_COUNT] = {
        {"heart", 48, COLOR_ACCENT, "อยู่นิ่ง"},
        {"star", 304, COLOR_WARN, "หมุน"},
        {"trophy", 560, COLOR_ACCENT, "ย่อขยาย"},
    };
    bento_ui_id_t unused = 0U;
    size_t i;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            ui, "ui.Picture",
            (label_look_t){MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX},
            &unused);
    }
    for (i = 0U; (i < (size_t)ICON_COUNT) && (status == BENTO_OK);
         i++) {
        status = add_icon(ui, &icon_specs[i], &icons[i]);
    }
    for (i = 0U; (i < (size_t)ICON_COUNT) && (status == BENTO_OK);
         i++) {
        status = add_label(ui, icon_specs[i].caption,
                           (label_look_t){icon_specs[i].x, CAPTION_Y,
                                          COLOR_DIM, CAPTION_PX},
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "ชื่อไอคอนมีห้าชื่อเท่านั้น",
            (label_look_t){MARGIN_X, FOOT_Y, COLOR_DIM, CAPTION_PX},
            &unused);
    }
    return status;
}

/* Turns one icon and grows another each round; prop setters ignore a
 * refusal (modui.c). */
static bento_status_t run_rounds(bento_ui_t *ui,
                                 const bento_ui_id_t icons[ICON_COUNT])
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        (void)bento_ui_set_prop(ui, icons[ICON_SPIN],
                                (uint8_t)BENTO_UI_PROP_ROTATION,
                                (round * TURN_STEP) % FULL_TURN);
        (void)bento_ui_set_prop(
            ui, icons[ICON_GROW], (uint8_t)BENTO_UI_PROP_SCALE,
            SCALE_START + ((round * SCALE_GROWTH) / LAST_ROUND));
        (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX,
                            &count, NULL);
        status = pause_ms(ROUND_MS);
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_ui_id_t icons[ICON_COUNT] = {0U, 0U, 0U};
    bento_status_t status = build_screen(ui, icons);

    if (status == BENTO_OK) {
        status = run_rounds(ui, icons);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.Picture: text=ชื่อไอคอน, PROP_ROTATION หน่วย "
                     "0.1 "
                     "องศา, PROP_SCALE "
                     "256=เท่าเดิม\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
