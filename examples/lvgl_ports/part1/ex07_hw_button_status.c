/*
 * ex07_hw_button_status.c -- สถานะปุ่มจริงสองปุ่ม (HW Button Status, Part 1)
 *
 * จอแบ่งสองคอลัมน์ แต่ละคอลัมน์มีไฟ LED จำลอง 70x70 ชื่อปุ่ม และสถานะ "PRESSED"
 * (เขียว) หรือ "Released" (แดงอ่อน) คอลัมน์ซ้ายคือปุ่ม 0 ของบอร์ด (gpio.button(0))
 * คอลัมน์ขวาคือปุ่มที่สอง: SW9 ของฐาน QWA309 บน Dev Kit, CapSense BTN0 บน Eva Kit
 * หรือไม่มีเลย อ่านทุก 50 ms และเขียนจอเฉพาะตอนสถานะเปลี่ยน ทำงาน 2 นาทีแล้วจบ
 *
 * Two columns, each with a 70x70 virtual LED, the button's names and a
 * "PRESSED" (green) or "Released" (light red) state. The left column is
 * the board's button 0 (gpio.button(0)); the right one is a second
 * button: SW9 of the QWA309 base on the Dev Kit, CapSense BTN0 on the
 * Eva Kit, or none. Both are read every 50 ms and the screen is written
 * only when a state changes. The example runs for two minutes.
 *
 * Twin of ex07_hw_button_status.py (embedded-systems-for-aiot-developer/
 * examples/lvgl_ports/part1/ex07_hw_button_status.py), written into one
 * clean file from the library's C twin of the Example Explorer program
 * aiot-lvgl-part1-ex07-hw-button-status (the same code). Style:
 * CLEAN_C.md. Where C differs:
 *  - `import buttons` succeeding is bento_buttons_count() > 0, and the
 *    CapSense probe that may raise is a status other than BENTO_OK;
 *  - read_btn2(), chosen at import time in MicroPython, is a source
 *    number that picks the same call;
 *  - p1 = p2 = None is a "known" flag beside each state, so the first
 *    pass always writes;
 *  - gpio.button(0) on a board without it returns
 *    BENTO_E_INVALID_ARGUMENT where MicroPython raises ValueError.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

#include "bento_buttons.h"
#include "bento_sensors.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    STATE_FONT_PX = 16,
    TITLE_Y = 17,
    COLUMN_DX = 90, /* C's CENTER(-90,-30) and (+90,-30) */
    LED_SIZE = 70,
    LED_Y = 139, /* 199 - 25 - 35 */
    CAPTION_Y = 219,
    NAME_Y = 237,
    STATE_Y = 261,
    HINT_Y = 336,
    FOOTER_Y = 374
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x16213E,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_HINT = 0xAAAAAA,
    COLOR_FOOTER = 0x666666,
    COLOR_CYAN = 0x00BCD4, /* LVGL's palette */
    COLOR_ORANGE = 0xFF9800,
    COLOR_PRESSED = 0x00FF00,
    COLOR_RELEASED = 0xFF6666
};

/* The two columns: the board's button 0, then the second button. */
enum {
    COLUMN_BUTTON1 = 0,
    COLUMN_BUTTON2 = 1,
    COLUMN_COUNT = 2
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 50,    /* read the buttons this often */
    RUN_MS = 120000  /* the example ends after two minutes */
};

/* Text buffers: "(" + a button name + ")", and the lcd line. */
enum {
    NAME_CHARS = 32,
    LCD_CHARS = 128
};

/* Where the second button is read from. */
typedef enum {
    BUTTON2_NONE,
    BUTTON2_BUTTONS, /* the QWA309 base board's buttons module */
    BUTTON2_CAPSENSE
} button2_source_t;

/* The widgets the loop changes, the button names and the last state
 * shown in each column. */
typedef struct {
    bento_ui_t *ui;
    button2_source_t source;
    const char *button2_name;
    char button1_name[NAME_CHARS];
    bento_ui_id_t leds[COLUMN_COUNT];
    bento_ui_id_t states[COLUMN_COUNT];
    bool shown[COLUMN_COUNT];
    bool known[COLUMN_COUNT];
} buttons_screen_t;

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

/* x that centres a text around center_x, taking a character as half
 * the 14 px font wide (the original's len(text) * 14, floor-divided by
 * 4); a column's 16 px state is placed the same way. */
static int16_t centered_at(int32_t center_x, const char *text)
{
    return (int16_t)(center_x - (((utf8_length(text) * FONT_PX) / 2) / 2));
}

static int32_t column_center(int32_t column)
{
    return (column == COLUMN_BUTTON1) ? (SCREEN_CENTER_X - COLUMN_DX)
                                      : (SCREEN_CENTER_X + COLUMN_DX);
}

/* The second button's source, probed in the original's order. */
static void find_button2(buttons_screen_t *screen)
{
    uint8_t button0 = 0U;
    uint8_t button1 = 0U;

    if (bento_buttons_count() > 0U) {
        screen->source = BUTTON2_BUTTONS;
        screen->button2_name = "(SW9)";
    } else if (bento_sensors_capsense_buttons(&button0, &button1) == BENTO_OK) {
        screen->source = BUTTON2_CAPSENSE;
        screen->button2_name = "(CapSense BTN0)";
    } else {
        screen->source = BUTTON2_NONE;
        screen->button2_name = "(none)";
    }
}

static bento_status_t read_button2(const buttons_screen_t *screen, bool *pressed)
{
    uint8_t button0 = 0U;
    uint8_t button1 = 0U;
    bento_status_t status = BENTO_OK;

    *pressed = false;
    if (screen->source == BUTTON2_BUTTONS) {
        status = bento_buttons_pressed(0U, pressed);
    } else if (screen->source == BUTTON2_CAPSENSE) {
        status = bento_sensors_capsense_buttons(&button0, &button1);
        *pressed = (button0 != 0U);
    } else {
        /* no second button: never pressed */
    }
    return status;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text, int16_t x, int16_t y,
                                uint32_t color, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.color = color;
    args.font_px = FONT_PX;
    return bento_ui_label_create(ui, &args, out_id);
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

static bento_status_t add_led(buttons_screen_t *screen, int32_t column, uint32_t color)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = (int16_t)(column_center(column) - (LED_SIZE / 2));
    args.y = LED_Y;
    args.w = LED_SIZE;
    args.h = LED_SIZE;
    args.color = color;
    args.font_px = 0;
    return bento_ui_led_create(screen->ui, &args, &screen->leds[column]);
}

/* A centred caption line of one column. */
static bento_status_t add_column_label(const buttons_screen_t *screen, int32_t column,
                                       const char *text, int16_t y)
{
    bento_ui_id_t unused;

    return add_label(screen->ui, text, centered_at(column_center(column), text), y, COLOR_TEXT,
                     &unused);
}

static bento_status_t add_state(buttons_screen_t *screen, int32_t column)
{
    const char *text = "Released";
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = centered_at(column_center(column), text);
    args.y = STATE_Y;
    args.color = COLOR_RELEASED;
    args.font_px = STATE_FONT_PX;
    return bento_ui_label_create(screen->ui, &args, &screen->states[column]);
}

/* gpio.button(0) must exist, and its name goes in brackets. */
static bento_status_t name_button1(buttons_screen_t *screen)
{
    const char *name = "";
    bento_status_t status =
        (bento_gpio_num_buttons() > 0U) ? BENTO_OK : BENTO_E_INVALID_ARGUMENT;

    if (status == BENTO_OK) {
        status = bento_gpio_button_name(0U, &name);
    }
    if (status == BENTO_OK) {
        (void)snprintf(screen->button1_name, sizeof(screen->button1_name), "(%s)", name);
    }
    return status;
}

/* The two LEDs, then the captions, names and states of both columns,
 * in the original's order. */
static bento_status_t add_columns(buttons_screen_t *screen)
{
    bento_status_t status = add_led(screen, COLUMN_BUTTON1, COLOR_CYAN);

    if (status == BENTO_OK) {
        status = add_led(screen, COLUMN_BUTTON2, COLOR_ORANGE);
    }
    if (status == BENTO_OK) {
        status = add_column_label(screen, COLUMN_BUTTON1, "USER Button 1", CAPTION_Y);
    }
    if (status == BENTO_OK) {
        status = add_column_label(screen, COLUMN_BUTTON1, screen->button1_name, NAME_Y);
    }
    if (status == BENTO_OK) {
        status = add_column_label(screen, COLUMN_BUTTON2, "USER Button 2", CAPTION_Y);
    }
    if (status == BENTO_OK) {
        status = add_column_label(screen, COLUMN_BUTTON2, screen->button2_name, NAME_Y);
    }
    if (status == BENTO_OK) {
        status = add_state(screen, COLUMN_BUTTON1);
    }
    if (status == BENTO_OK) {
        status = add_state(screen, COLUMN_BUTTON2);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(buttons_screen_t *screen)
{
    const char *title = "Part 1 Ex7: Hardware Button Status";
    const char *hint = "[Part II] Press the two buttons on the board";
    const char *footer = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";
    bento_ui_t *ui = screen->ui;
    bento_ui_id_t unused;
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
        status = add_label(ui, title, centered_at(SCREEN_CENTER_X, title), TITLE_Y, COLOR_TEXT,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_columns(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, hint, centered_at(SCREEN_CENTER_X, hint), HINT_Y, COLOR_HINT,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, footer, centered_at(SCREEN_CENTER_X, footer), FOOTER_Y,
                           COLOR_FOOTER, &unused);
    }
    return status;
}

/* Writes a column only when its state changed (or was never shown). */
static void show_button(buttons_screen_t *screen, int32_t column, bool pressed)
{
    const char *text = pressed ? "PRESSED" : "Released";

    if (!screen->known[column] || (screen->shown[column] != pressed)) {
        screen->shown[column] = pressed;
        screen->known[column] = true;
        /* MicroPython's widget setters ignore a refusal (modui.c), so
         * the twin does too. */
        (void)bento_ui_set_value(screen->ui, screen->leds[column], pressed ? 1 : 0);
        (void)bento_ui_set_text(screen->ui, screen->states[column], text, (uint32_t)strlen(text));
        (void)bento_ui_set_color(screen->ui, screen->states[column],
                                 pressed ? COLOR_PRESSED : COLOR_RELEASED);
    }
}

static bento_status_t read_buttons(buttons_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    bool pressed1 = false;
    bool pressed2 = false;
    bento_status_t status;

    /* No widget here answers a tap (the "< Menu" button is left out),
     * so the events are not read, and ui.poll() answers no events
     * when it fails. */
    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count, NULL);
    status = bento_gpio_button_is_pressed(0U, &pressed1);
    if (status == BENTO_OK) {
        status = read_button2(screen, &pressed2);
    }
    if (status == BENTO_OK) {
        show_button(screen, COLUMN_BUTTON1, pressed1);
        show_button(screen, COLUMN_BUTTON2, pressed2);
    }
    return status;
}

/* Reads both buttons every LOOP_MS until RUN_MS has passed or Stop is
 * pressed. */
static bento_status_t run_loop(buttons_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) < RUN_MS)) {
        status = read_buttons(screen);
        if (status == BENTO_OK) {
            status = bento_time_sleep_ms(LOOP_MS);
        }
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    buttons_screen_t screen;
    char line[LCD_CHARS];
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    find_button2(&screen);
    status = name_button1(&screen);
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)snprintf(line, sizeof(line), "ex07: watching button 1 %s and button 2 %s",
                       screen.button1_name, screen.button2_name);
        (void)bento_lcd_print(line);
    }
    if (status == BENTO_OK) {
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("ex07_hw_button_status: done\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
