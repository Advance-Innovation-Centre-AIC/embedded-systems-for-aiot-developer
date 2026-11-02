/*
 * roller.c -- ui.Roller ตัวเดียว (one Roller)
 *
 * วงล้อห้าตัวเลือก .add_option() เติม .value(n) เลือก ตัวอย่างเลื่อนเองทุก 2 วินาที
 * ปัดเลือกเองแล้วคอนโซลบอกลำดับที่เลือก รันจบเองใน 24 วินาที
 *
 * A roller of five choices: add_option() adds one, value(n) selects
 * one. The example moves on by itself every 2 seconds; a choice the
 * user rolls to is printed by its number. The example ends by itself
 * after 24 seconds.
 *
 * Twin of roller.py
 * (embedded-systems-for-aiot-developer/examples/widgets/roller.py),
 * merged into one file from the library twin wref-roller.
 * Style: CLEAN_C.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    TITLE_PX = 28,
    ROLLER_Y = 72,
    ROLLER_W = 320,
    ROLLER_H = 248,
    SIDE_X = 384,
    NOTE_Y = 112,
    NOTE_PX = 24,
    HINT_Y = 160,
    HINT_PX = 20,
    FOOT_Y = 344,
    TEXT_CAP = 96
};

/* Timing: 60 rounds of 400 ms; the shown choice moves on every 5
 * rounds. */
enum {
    SETTLE_MS = 200,
    ROUNDS = 60,
    ROUNDS_PER_CHOICE = 5,
    ROUND_MS = 400,
    CHOICE_COUNT = 5
};

/* The widgets the loop changes. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t roller;
    bento_ui_id_t note;
} roller_screen_t;

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

/* True when the event is of this kind and came from this widget. */
static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF
};

static const char *const CHOICES[CHOICE_COUNT] = {
    "ปกติ", "เฝ้าระวัง", "หยุดฉุกเฉิน", "ล้างค่า", "ปิดเครื่อง"};

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

/* The roller and its choices; an option's refusal is ignored, as
 * modui.c does. */
static bento_status_t add_roller(roller_screen_t *screen)
{
    bento_ui_widget_args_t args;
    size_t i;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = ROLLER_Y;
    args.w = ROLLER_W;
    args.h = ROLLER_H;
    args.color = COLOR_TEXT;
    status = bento_ui_roller_create(screen->ui, &args, &screen->roller);
    for (i = 0U; (i < (size_t)CHOICE_COUNT) && (status == BENTO_OK);
         i++) {
        (void)bento_ui_add_option(screen->ui, screen->roller,
                                  CHOICES[i],
                                  (uint32_t)strlen(CHOICES[i]));
    }
    return status;
}

static void choice_text(char *text, size_t capacity, int32_t choice)
{
    (void)snprintf(text, capacity, "ตอนนี้เลือก %s", CHOICES[choice]);
}

static bento_status_t build_screen(roller_screen_t *screen)
{
    char text[TEXT_CAP];
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            screen->ui, "ui.Roller",
            (label_look_t){MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_roller(screen);
    }
    if (status == BENTO_OK) {
        choice_text(text, sizeof(text), 0);
        status = add_label(
            screen->ui, text,
            (label_look_t){SIDE_X, NOTE_Y, COLOR_TEXT, NOTE_PX},
            &screen->note);
    }
    if (status == BENTO_OK) {
        status = add_label(
            screen->ui, "ดูที่แถบสี กล่อง = ฟอนต์ไทยหาย",
            (label_look_t){SIDE_X, HINT_Y, COLOR_DIM, HINT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            screen->ui, "ปัดนิ้วก็ได้ สั่งจากโปรแกรมก็ได้",
            (label_look_t){MARGIN_X, FOOT_Y, COLOR_DIM, HINT_PX},
            &unused);
    }
    return status;
}

/* A choice the user rolled to is printed by its number; a failed
 * poll has no events. */
static void print_choices(const roller_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (event_is(&events[i], BENTO_UI_EVENT_VALUE_CHANGED,
                     screen->roller)) {
            (void)printf("บรรทัดที่เลือกคือ %ld\n", (long)events[i].value);
        }
    }
}

/* Every ROUNDS_PER_CHOICE rounds the next choice is selected and named
 * (setters ignore a refusal, as modui.c does). */
static bento_status_t run_rounds(const roller_screen_t *screen)
{
    char text[TEXT_CAP];
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        if ((round % ROUNDS_PER_CHOICE) == 0) {
            const int32_t choice =
                (round / ROUNDS_PER_CHOICE) % CHOICE_COUNT;

            (void)bento_ui_set_value(screen->ui, screen->roller,
                                     choice);
            choice_text(text, sizeof(text), choice);
            (void)bento_ui_set_text(screen->ui, screen->note, text,
                                    (uint32_t)strlen(text));
        }
        print_choices(screen);
        status = pause_ms(ROUND_MS);
    }
    return status;
}

int main(void)
{
    roller_screen_t screen = {bento_runtime_get_ui(), 0U, 0U};
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        status = run_rounds(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.Roller: .add_option() เติม, .value(n) เลือก, "
                     "event คือ value_changed "
                     "พร้อมลำดับ\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
