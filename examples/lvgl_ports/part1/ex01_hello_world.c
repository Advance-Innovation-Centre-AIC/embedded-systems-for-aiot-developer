/*
 * ex01_hello_world.c -- สวัสดี BUU (Hello World, Part 1)
 *
 * หน้าจอนิ่ง: พื้นสีน้ำเงินเข้ม ชื่อตัวอย่างด้านบน คำว่า "Hello BUU!" ตัวใหญ่กลางจอ
 * คำอธิบายสองบรรทัดสีเทา และบรรทัดลิขสิทธิ์ด้านล่าง ไม่มีปุ่มให้กด จอค้างไว้ 2 วินาที
 * แล้วตัวอย่างจบ โดยภาพยังค้างอยู่บนจอ
 *
 * A still screen: a dark blue background, the example's title at the
 * top, a large "Hello BUU!" in the middle, two grey lines of
 * description and the copyright line at the bottom. Nothing to tap: the
 * screen stays for two seconds, then the example ends and the widgets
 * stay on the screen.
 *
 * Twin of ex01_hello_world.py (embedded-systems-for-aiot-developer/
 * examples/lvgl_ports/part1/ex01_hello_world.py), merged into one file
 * from the library's C twin of the Example Explorer program
 * aiot-lvgl-part1-ex01-hello-world (py/aiot-lvgl-part1-ex01-hello-world.py,
 * the same code). Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    HELLO_FONT_PX = 24,
    TITLE_Y = 17,
    HELLO_Y = 187,
    DESCRIPTION_Y = 314,
    DESCRIPTION2_Y = 332,
    FOOTER_Y = 374
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x003A57,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_HINT = 0xAAAAAA,
    COLOR_FOOTER = 0x666666
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,   /* read the touch events this often */
    RUN_MS =
        2000 /* the screen stays this long, then the example ends */
};

/* Python's len(): characters, not bytes, so a Thai text is centred
 * like the original. */
static int32_t utf8_length(const char *text)
{
    /* the two bits that mark a UTF-8 byte */
    const uint8_t top_bits = 0xC0U;
    /* 10xxxxxx: not the first byte of a character */
    const uint8_t continuation = 0x80U;
    int32_t count = 0;
    size_t i;

    for (i = 0U; text[i] != '\0'; i++) {
        if (((uint8_t)text[i] & top_bits) != continuation) {
            count++;
        }
    }
    return count;
}

/* x that centres a text: half its width left of the centre, taking a
 * character as half the font size wide (the original's len(text) *
 * size, floor-divided by 4). */
static int16_t centered_x(const char *text, int32_t font_px)
{
    return (int16_t)(SCREEN_CENTER_X -
                     (((utf8_length(text) * font_px) / 2) / 2));
}

/* A label centred on the screen. */
static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                int32_t font_px)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = centered_x(text, font_px);
    args.y = y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, &label);
}

static bento_status_t add_background(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;

    bento_ui_widget_args_init(&args);
    args.x = 0;
    args.y = 0;
    args.w = BENTO_RUNTIME_SCREEN_WIDTH_PX;
    args.h = BENTO_RUNTIME_SCREEN_HEIGHT_PX;
    args.color = COLOR_BACKGROUND;
    /* min= repeats the colour, as the original does */
    args.min_val = COLOR_BACKGROUND;
    args.max_val = 0;
    args.font_px = 0;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(bento_ui_t *ui)
{
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_background(ui);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "Part 1 - Example 1", TITLE_Y,
                           COLOR_TEXT, FONT_PX);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "Hello BUU!", HELLO_Y, COLOR_TEXT,
                           HELLO_FONT_PX);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "Basic Label Example", DESCRIPTION_Y,
                           COLOR_HINT, FONT_PX);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "Learning: lv_label_create, lv_obj_align",
                      DESCRIPTION2_Y, COLOR_HINT, FONT_PX);
    }
    if (status == BENTO_OK) {
        status = add_label(ui,
                           "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                           "Burapha University",
                           FOOTER_Y, COLOR_FOOTER, FONT_PX);
    }
    return status;
}

/* Keeps the screen up, reading the touch events every LOOP_MS, until
 * RUN_MS has passed or Stop is pressed. */
static bento_status_t hold_screen(bento_ui_t *ui)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        /* No widget here answers a tap (the "< Menu" button is left
         * out), so the events are not read, and ui.poll() answers no
         * events when it fails. */
        (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX,
                            &count, NULL);
        status = bento_time_sleep_ms(LOOP_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_status_t status = build_screen(ui);

    if (status == BENTO_OK) {
        status = hold_screen(ui);
    }
    if (status == BENTO_OK) {
        (void)printf(
            "ex01_hello_world: done - widgets stay on screen\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
