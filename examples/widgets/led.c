/*
 * led.c -- ui.Led ตัวเดียว (one Led)
 *
 * ไฟสี่ดวง ปกติ เฝ้าระวัง ผิดปกติ และสั่งดับ ดวงที่สี่ต้องหรี่ ไม่ใช่หาย
 * แผงจริงให้ติดทีละดวง ทุก 2 วินาทีย้ายไปดวงถัดไป รันจบเองใน 24 วินาที
 *
 * Four lamps: normal, watch, fault and switched off; the fourth must
 * look dimmed, not gone. Like a real panel, one lamp is lit at a time,
 * moving on every 2 seconds. The example ends by itself after 24
 * seconds.
 *
 * Twin of led.py
 * (embedded-systems-for-aiot-developer/examples/widgets/led.py),
 * merged into one file from the library twin wref-led.
 *
 * Style: CLEAN_C.md.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Timing: 60 rounds of 400 ms; the lit lamp moves on every 5 rounds. */
enum {
    SETTLE_MS = 200,
    ROUNDS = 60,
    ROUNDS_PER_LAMP = 5,
    ROUND_MS = 400,
    LIT_LAMPS = 3, /* the three that take turns; the fourth stays off */
    LAMP_COUNT = 4
};

/* The widgets the loop changes. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t lamps[LAMP_COUNT];
    bento_ui_id_t note;
} lamp_screen_t;

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
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* Layout on the 792 x 398 canvas: four lamps in a row, each with its
 * caption. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    TITLE_PX = 28,
    LAMP_Y = 96,
    LAMP_SIZE = 88,
    CAPTION_Y = 200,
    CAPTION_PX = 20,
    NOTE_Y = 256,
    NOTE_PX = 24,
    FOOT_Y = 344,
    TEXT_CAP = 96
};

/* A lamp: where it stands, its colour, whether it starts lit, and
 * its caption. */
typedef struct {
    int16_t x;
    uint32_t color;
    int32_t lit;
    const char *caption;
} lamp_spec_t;

static const lamp_spec_t LAMPS[LAMP_COUNT] = {
    {24, COLOR_OK, 1, "ปกติ"},
    {208, COLOR_WARN, 1, "เฝ้าระวัง"},
    {392, COLOR_BAD, 1, "ผิดปกติ"},
    {576, COLOR_OK, 0, "สั่งดับ"},
};

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

static bento_status_t add_lamp(lamp_screen_t *screen, size_t index)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = LAMPS[index].x;
    args.y = LAMP_Y;
    args.w = LAMP_SIZE;
    args.h = LAMP_SIZE;
    args.color = LAMPS[index].color;
    args.font_px = LAMPS[index].lit;
    return bento_ui_led_create(screen->ui, &args,
                               &screen->lamps[index]);
}

static bento_status_t build_screen(lamp_screen_t *screen)
{
    bento_ui_id_t unused = 0U;
    size_t i;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            screen->ui, "ui.Led",
            (label_look_t){MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX},
            &unused);
    }
    for (i = 0U; (i < (size_t)LAMP_COUNT) && (status == BENTO_OK);
         i++) {
        status = add_lamp(screen, i);
    }
    for (i = 0U; (i < (size_t)LAMP_COUNT) && (status == BENTO_OK);
         i++) {
        status = add_label(screen->ui, LAMPS[i].caption,
                           (label_look_t){LAMPS[i].x, CAPTION_Y,
                                          COLOR_DIM, CAPTION_PX},
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            screen->ui, "ดวงที่สี่ต้องหรี่ ไม่ใช่หาย",
            (label_look_t){MARGIN_X, NOTE_Y, COLOR_TEXT, NOTE_PX},
            &screen->note);
    }
    if (status == BENTO_OK) {
        status = add_label(
            screen->ui, "แผงจริงให้ติดทีละดวง",
            (label_look_t){MARGIN_X, FOOT_Y, COLOR_DIM, CAPTION_PX},
            &unused);
    }
    return status;
}

/* Lights lamp `lit` of the three and names it; setters ignore a
 * refusal, as modui.c does. */
static void light_one(const lamp_screen_t *screen, int32_t lit)
{
    const int32_t shown = lit + 1;
    char text[TEXT_CAP];
    int32_t i;

    for (i = 0; i < LIT_LAMPS; i++) {
        (void)bento_ui_set_value(screen->ui, screen->lamps[i],
                                 (i == lit) ? 1 : 0);
    }
    (void)snprintf(text, sizeof(text), "ตอนนี้ติดดวงที่ %ld จากสามดวง",
                   (long)shown);
    (void)bento_ui_set_text(screen->ui, screen->note, text,
                            (uint32_t)strlen(text));
}

/* The rounds: every ROUNDS_PER_LAMP the next lamp lights; the polls'
 * events are not used. */
static bento_status_t run_rounds(const lamp_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        if ((round % ROUNDS_PER_LAMP) == 0) {
            light_one(screen, (round / ROUNDS_PER_LAMP) % LIT_LAMPS);
        }
        (void)bento_ui_poll(screen->ui, events,
                            BENTO_UI_POLL_EVENTS_MAX, &count, NULL);
        status = pause_ms(ROUND_MS);
    }
    return status;
}

int main(void)
{
    lamp_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        status = run_rounds(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.Led: value(0) คือหรี่ ไม่ใช่หาย - lv_led_off "
                     "ตั้งความสว่างขั้นต่ำ 80 จาก 255\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
