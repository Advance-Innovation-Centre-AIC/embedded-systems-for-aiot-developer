/*
 * ex02_button_counter.c -- ปุ่มนับครั้ง (Button Counter, Section 2, Dev Kit)
 *
 * แตะปุ่ม "Click Me!" กลางจอ แล้วข้อความบนปุ่มเปลี่ยนเป็น "Clicked: n" ทันที
 * พร้อมพิมพ์จำนวนครั้งออกทาง console ตัวอย่างทำงาน 2 นาทีแล้วจบ บอกจำนวนครั้ง
 * ทั้งหมด และวาดแถบสีส้ม "ตัวอย่างจบแล้ว กดปุ่มไม่ได้" ทับแถวล่าง
 *
 * Tap the "Click Me!" button in the middle of the screen: its own text
 * becomes "Clicked: n" at once, and the count is printed. The example
 * runs for two minutes, prints the total number of clicks, and draws an
 * orange bar over the bottom rows saying the example has ended.
 *
 * Twin of ex02_button_counter.py (embedded-systems-for-aiot-developer/
 * examples/lvgl_ports/sec2_ui_to_hw/devkit/ex02_button_counter.py),
 * written into one file from the library's C twin of the Example
 * Explorer program aiot-lvgl-part1-ex02-button-counter (similarity
 * 0.88: this file adds the end banner). Style: CLEAN_C.md. Left out on
 * purpose: the "< Menu" button, which the original shows only when the
 * board's own menu starts it (MENU_MODE); a C program is never started
 * from that menu.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    TITLE_Y = 17,
    BUTTON_Y = 175,
    /* the original's LVGL padding around a 14 px text */
    BUTTON_W = 150,
    BUTTON_H = 48,
    HINT_Y = 322,
    HINT2_Y = 340,
    FOOTER_Y = 374
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x1A1A2E,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_HINT = 0xAAAAAA,
    COLOR_FOOTER = 0x666666
};

/* The "example has ended" bar over the bottom rows. */
enum {
    BANNER_Y = 330,
    BANNER_H = 36,
    BANNER_BORDER_PX = 1,
    BANNER_TEXT_X = 232,
    BANNER_TEXT_Y = 334,
    BANNER_FONT_PX = 20,
    COLOR_BANNER = 0x1A1A2E,
    COLOR_ENDED = 0xFF6600 /* the banner's border and text */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 50,    /* read the touch events this often */
    RUN_MS = 120000  /* the example ends after two minutes */
};

/* The button's text: "Clicked: " and any int32_t, with its NUL. */
enum {
    BUTTON_TEXT_CHARS = 24
};

/* The button the program changes, and how often it was clicked. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t button;
    int32_t clicks;
} counter_screen_t;

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
static int16_t centered_x(const char *text)
{
    return (int16_t)(SCREEN_CENTER_X - (((utf8_length(text) * FONT_PX) / 2) / 2));
}

/* True when the event is of this kind and came from this widget. */
static bool event_is(const bento_ui_event_t *event, uint8_t kind, bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text, int16_t y, uint32_t color)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = centered_x(text);
    args.y = y;
    args.color = color;
    args.font_px = FONT_PX;
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

static bento_status_t add_button(counter_screen_t *screen)
{
    const char *text = "Click Me!";
    bento_ui_widget_args_t args;

    /* no colour: the theme's button, as the original */
    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = SCREEN_CENTER_X - (BUTTON_W / 2);
    args.y = BUTTON_Y;
    args.w = BUTTON_W;
    args.h = BUTTON_H;
    args.font_px = FONT_PX;
    return bento_ui_button_create(screen->ui, &args, &screen->button);
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(counter_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX, BENTO_RUNTIME_SCREEN_HEIGHT_PX);

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
        status = add_label(ui, "Part 1 - Example 2: Button Counter", TITLE_Y, COLOR_TEXT);
    }
    if (status == BENTO_OK) {
        status = add_button(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "Learning: lv_button_create, lv_obj_add_event_cb", HINT_Y,
                           COLOR_HINT);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "Pattern: Event callback with LV_EVENT_CLICKED", HINT2_Y,
                           COLOR_HINT);
    }
    if (status == BENTO_OK) {
        status = add_label(ui,
                           "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                           "Burapha University",
                           FOOTER_Y, COLOR_FOOTER);
    }
    return status;
}

/* One more click: the count goes onto the button itself and to the
 * console. */
static void count_click(counter_screen_t *screen)
{
    char text[BUTTON_TEXT_CHARS];

    screen->clicks++;
    (void)snprintf(text, sizeof(text), "Clicked: %" PRId32, screen->clicks);
    /* MicroPython's widget setters ignore a refusal (modui.c), so
     * the twin does too. */
    (void)bento_ui_set_text(screen->ui, screen->button, text, (uint32_t)strlen(text));
    (void)printf("Button clicked %" PRId32 " times\n", screen->clicks);
}

static void handle_events(counter_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    /* ui.poll() answers no events when it fails, so a failed poll is
     * not an error here. */
    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (event_is(&events[i], BENTO_UI_EVENT_CLICKED, screen->button)) {
            count_click(screen);
        }
    }
}

/* Reads the touch events every LOOP_MS until RUN_MS has passed or
 * Stop is pressed. */
static bento_status_t run_event_loop(counter_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) < RUN_MS)) {
        handle_events(screen);
        status = bento_time_sleep_ms(LOOP_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

/* The picture stays after the end, so a bar over the bottom rows says
 * that taps no longer do anything. */
static bento_status_t show_end_banner(bento_ui_t *ui)
{
    const char *text = "ตัวอย่างจบแล้ว กดปุ่มไม่ได้";
    bento_ui_widget_args_t args;
    bento_ui_id_t widget;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = 0;
    args.y = BANNER_Y;
    args.w = BENTO_RUNTIME_SCREEN_WIDTH_PX;
    args.h = BANNER_H;
    args.color = COLOR_BANNER;
    /* a panel's min= is its border colour */
    args.min_val = COLOR_ENDED;
    args.max_val = 0;
    args.font_px = BANNER_BORDER_PX;
    status = bento_ui_panel_create(ui, &args, &widget);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = text;
        args.text_len = (uint32_t)strlen(text);
        args.x = BANNER_TEXT_X;
        args.y = BANNER_TEXT_Y;
        args.color = COLOR_ENDED;
        args.font_px = BANNER_FONT_PX;
        status = bento_ui_label_create(ui, &args, &widget);
    }
    return status;
}

int main(void)
{
    counter_screen_t screen = {bento_runtime_get_ui(), 0U, 0};
    bento_status_t status = build_screen(&screen);

    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print("ex02: tap the button - counter updates on the button itself");
    }
    if (status == BENTO_OK) {
        status = run_event_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("ex02_button_counter: done - %" PRId32 " clicks\n", screen.clicks);
        status = show_end_banner(screen.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
