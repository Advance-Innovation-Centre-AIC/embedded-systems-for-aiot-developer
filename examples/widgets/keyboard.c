/*
 * keyboard.c -- ui.Keyboard ตัวเดียว (one Keyboard)
 *
 * แป้นพิมพ์บนจอผูกกับช่องข้อความ (.bind) แล้วฟัง ready และ cancel (.listen)
 * กดตกลงหรือปิดแล้วคอนโซลบอกข้อความในช่อง ตอนจบบอกจำนวนครั้งและข้อความสุดท้าย
 * รันจบเองใน 24 วินาที
 *
 * An on-screen keyboard bound to a text area (bind) that listens for
 * ready and cancel (listen): each prints what the text area holds; at
 * the end the count and the final text are printed. The example ends by
 * itself after 24 seconds.
 *
 * Twin of keyboard.py
 * (embedded-systems-for-aiot-developer/examples/widgets/keyboard.py),
 * merged into one file from the library twin wref-keyboard.
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
    PANEL_W = 664,
    TEXTAREA_Y = 64,
    TEXTAREA_H = 88,
    TEXT_PX = 0, /* no value= in the original: the default font */
    KEYBOARD_Y = 168,
    KEYBOARD_H = 176,
    KEYBOARD_MODE = 1,
    /* the text area's text: the display sends at most 127 bytes */
    TEXT_CAP = 160
};

/* Timing: 60 rounds of 400 ms (24 s). */
enum {
    SETTLE_MS = 200,
    ROUNDS = 60,
    ROUND_MS = 400
};

/* The widgets the program reads. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t text_area;
    bento_ui_id_t keyboard;
} keyboard_screen_t;

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
    COLOR_TEXT = 0xE8EAED
};

static bento_status_t add_widget(bento_ui_t *ui, const char *text,
                                 int16_t y, int16_t h, int32_t size_px,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    if (text != NULL) {
        args.text = text;
        args.text_len = (uint32_t)strlen(text);
    }
    args.x = MARGIN_X;
    args.y = y;
    args.w = PANEL_W;
    args.h = h;
    args.color = COLOR_TEXT;
    args.font_px = size_px;
    return (text != NULL) ? bento_ui_textarea_create(ui, &args, out_id)
                          : bento_ui_keyboard_create(ui, &args, out_id);
}

static bento_status_t build_screen(keyboard_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "ui.Keyboard";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = MARGIN_X;
        args.y = TITLE_Y;
        args.color = COLOR_TEXT;
        args.font_px = TITLE_PX;
        status = bento_ui_label_create(screen->ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        status = add_widget(screen->ui, "พิมพ์ต่อท้ายได้เลย", TEXTAREA_Y,
                            TEXTAREA_H, TEXT_PX, &screen->text_area);
    }
    if (status == BENTO_OK) {
        status = add_widget(screen->ui, NULL, KEYBOARD_Y, KEYBOARD_H,
                            KEYBOARD_MODE, &screen->keyboard);
    }
    /* .bind() and .listen() are setters: modui.c ignores a refusal */
    if (status == BENTO_OK) {
        (void)bento_ui_bind_textarea(screen->ui, screen->keyboard,
                                     screen->text_area);
        (void)bento_ui_listen_compat(
            screen->ui, screen->keyboard,
            BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_READY) |
                BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_CANCEL));
    }
    return status;
}

/* ta.text(): the text area's text, or "" when the display does not
 * answer. */
static void read_text(const keyboard_screen_t *screen, char *text,
                      uint32_t capacity)
{
    if (bento_ui_get_text(screen->ui, screen->text_area, text, capacity,
                          NULL) != BENTO_OK) {
        text[0] = '\0';
    }
}

/* Any ready or cancel event (from whichever widget) is counted and
 * printed with the text. */
static int32_t print_endings(const keyboard_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    char text[TEXT_CAP];
    uint32_t count = 0U;
    uint32_t i;
    int32_t endings = 0;

    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        const bool ready =
            events[i].type == (uint8_t)BENTO_UI_EVENT_READY;

        if (ready ||
            (events[i].type == (uint8_t)BENTO_UI_EVENT_CANCEL)) {
            endings++;
            read_text(screen, text, (uint32_t)sizeof(text));
            (void)printf("ui.Keyboard: %s - ในช่องตอนนี้: %s\n",
                         ready ? "ready" : "cancel", text);
        }
    }
    return endings;
}

static bento_status_t run_rounds(const keyboard_screen_t *screen,
                                 int32_t *endings)
{
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        *endings += print_endings(screen);
        status = pause_ms(ROUND_MS);
    }
    return status;
}

int main(void)
{
    keyboard_screen_t screen = {bento_runtime_get_ui(), 0U, 0U};
    char text[TEXT_CAP];
    int32_t endings = 0;
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        status = run_rounds(&screen, &endings);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.Keyboard: ready/cancel ที่ได้รับ = %ld\n",
                     (long)endings);
        read_text(&screen, text, (uint32_t)sizeof(text));
        (void)printf("ข้อความที่อ่านกลับได้ตอนจบ: %s\n", text);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
