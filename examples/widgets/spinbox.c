/*
 * spinbox.c -- ui.Spinbox ตัวเดียว (one Spinbox)
 *
 * ช่องตัวเลขสองช่องเก็บ 2375 เท่ากัน ช่องขวาตั้ง .digits(4, 2) จึงวาดเป็น 23.75
 * แต่ค่ายังเป็น 2375 แตะปรับค่าแล้วบรรทัดล่างบอกค่าใหม่ รันจบเองใน 20 วินาที
 *
 * Two number boxes both hold 2375; the right one has digits(4, 2), so
 * it draws 23.75 while its value stays 2375. Change a value and the
 * note shows the new one. Ends after 20 s.
 *
 * Twin of spinbox.py
 * (embedded-systems-for-aiot-developer/examples/widgets/spinbox.py),
 * merged into one file from the library twin wref-spinbox.
 * Style: CLEAN_C.md.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF
};

/* Layout on the 792 x 398 canvas: two columns of a caption over a
 * spinbox. */
enum {
    LEFT_X = 24,
    RIGHT_X = 392,
    TITLE_Y = 16,
    TITLE_PX = 28,
    CAPTION_Y = 80,
    CAPTION_PX = 20,
    SPINBOX_Y = 112,
    SPINBOX_W = 336,
    SPINBOX_H = 96,
    SPINBOX_MAX = 9999,
    SPINBOX_START = 2375,
    DIGITS = 4,
    DECIMALS = 2,
    NOTE_Y = 240,
    NOTE_PX = 24,
    FOOT_Y = 344,
    TEXT_CAP = 64 /* "ค่าใหม่ " and an int32_t */
};

/* Timing: 100 rounds of 200 ms (20 s). */
enum {
    SETTLE_MS = 200,
    ROUND_MS = 200, /* read the taps, then sleep this long */
    ROUNDS = 100
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

/* ui.Spinbox: value= is carried in font_px. */
static bento_status_t add_spinbox(bento_ui_t *ui, int16_t x,
                                  bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = x;
    args.y = SPINBOX_Y;
    args.w = SPINBOX_W;
    args.h = SPINBOX_H;
    args.color = COLOR_TEXT;
    args.min_val = 0;
    args.max_val = SPINBOX_MAX;
    args.font_px = SPINBOX_START;
    return bento_ui_spinbox_create(ui, &args, out_id);
}

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

/* Draws the screen in the original's order; returns the note label
 * the events change. */
static bento_status_t build_screen(bento_ui_t *ui, bento_ui_id_t *note)
{
    bento_ui_id_t unused = 0U;
    bento_ui_id_t decimal_box = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            ui, "ui.Spinbox",
            (label_look_t){LEFT_X, TITLE_Y, COLOR_TEXT, TITLE_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "จำนวนเต็ม",
            (label_look_t){LEFT_X, CAPTION_Y, COLOR_DIM, CAPTION_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_spinbox(ui, LEFT_X, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "เลขเดียวกัน สองหลักหลังจุด",
            (label_look_t){RIGHT_X, CAPTION_Y, COLOR_DIM, CAPTION_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_spinbox(ui, RIGHT_X, &decimal_box);
    }
    /* .digits() is a setter: modui.c ignores its refusal */
    if (status == BENTO_OK) {
        (void)bento_ui_set_digits(ui, decimal_box, DIGITS, DECIMALS);
        status = add_label(
            ui, "ทั้งสองช่องเก็บ 2375 เท่ากัน",
            (label_look_t){LEFT_X, NOTE_Y, COLOR_TEXT, NOTE_PX}, note);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "จุดทศนิยมเป็นเรื่องของการวาด",
            (label_look_t){LEFT_X, FOOT_Y, COLOR_DIM, CAPTION_PX},
            &unused);
    }
    return status;
}

/* Any spinbox's new value goes to the note and the console; a failed
 * poll has no events. */
static void show_changes(bento_ui_t *ui, bento_ui_id_t note)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    char text[TEXT_CAP];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (events[i].type == (uint8_t)BENTO_UI_EVENT_VALUE_CHANGED) {
            (void)snprintf(text, sizeof(text), "ค่าใหม่ %ld",
                           (long)events[i].value);
            /* MicroPython's widget setters ignore a refusal
             * (modui.c), so the twin does too. */
            (void)bento_ui_set_text(ui, note, text,
                                    (uint32_t)strlen(text));
            (void)printf("spinbox handle %ld value %ld\n",
                         (long)bento_ui_id_to_handle(events[i].id),
                         (long)events[i].value);
        }
    }
}

/* ROUNDS rounds: read the taps, then sleep ROUND_MS (20 s in all). */
static bento_status_t run_rounds(bento_ui_t *ui, bento_ui_id_t note)
{
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        show_changes(ui, note);
        status = pause_ms(ROUND_MS);
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_ui_id_t note = 0U;
    bento_status_t status = build_screen(ui, &note);

    if (status == BENTO_OK) {
        status = run_rounds(ui, note);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.Spinbox: .digits(4, 2) ทำให้ 2375 วาดเป็น "
                     "23.75 "
                     "แต่ค่ายังเป็น 2375\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
