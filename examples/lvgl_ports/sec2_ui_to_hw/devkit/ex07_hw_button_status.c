/*
 * ex07_hw_button_status.c -- สถานะปุ่มจริงสามปุ่ม (HW Button Status, Section 2, Dev Kit)
 *
 * จอแบ่งคอลัมน์ตามจำนวนปุ่มของบอร์ด: USER Button 1 บนบอร์ด AI Kit กับ SW4 และ SW5
 * บนฐาน QWA309 แต่ละคอลัมน์มีไฟ LED จำลอง 70x70 ชื่อปุ่ม และสถานะ "PRESSED"
 * (เขียว) หรือ "Released" (แดงอ่อน) อ่านทุก 50 ms และเขียนจอเฉพาะตอนสถานะเปลี่ยน
 * ทำงาน 2 นาทีแล้วจบ และวาดแถบ "ตัวอย่างจบแล้ว กดปุ่มไม่ได้" ทับแถวล่าง
 *
 * One column per button of the board: USER Button 1 on the AI Kit, and
 * SW4 and SW5 on the QWA309 base. Each column has a 70x70 virtual LED,
 * the button's name and a "PRESSED" (green) or "Released" (light red)
 * state. The buttons are read every 50 ms and the screen is written
 * only when a state changes. The example runs for two minutes, then
 * draws a bar over the bottom rows saying it has ended.
 *
 * Twin of ex07_hw_button_status.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/sec2_ui_to_hw/devkit/ex07_hw_button_status.py),
 * written new (the closest library twin,
 * aiot-lvgl-sec2-eva-ex07-hw-button-status, has similarity 0.32) in the
 * style of the reference twins and the part1 twin of this course.
 * Style: CLEAN_C.md. Where C differs:
 *  - the board's table of (name, reader, colour) keeps a reader number
 *    in place of the Python function; `import buttons` raising
 *    ImportError is BENTO_E_UNSUPPORTED when bento_buttons_count() is 0,
 *    and gpio.button(0) on a board without it returns
 *    BENTO_E_INVALID_ARGUMENT where MicroPython raises ValueError;
 *  - a read that fails counts as "not pressed", as the original's
 *    `except OSError`; only a ValueError (BENTO_E_INVALID_ARGUMENT)
 *    ends the program;
 *  - prev[i] = None is a "known" flag beside each state.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

#include "bento_buttons.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    STATE_FONT_PX = 16,
    TITLE_Y = 17,
    COLUMN_HALF_GAP = 90, /* columns 180 px apart, centred on the screen */
    LED_SIZE = 70,
    LED_Y = 139,
    NAME_Y = 225,
    STATE_DX = 28, /* "Released" starts this far left of its column */
    STATE_Y = 255,
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
    COLOR_GREEN = 0x4CAF50,
    COLOR_PRESSED = 0x00FF00,
    COLOR_RELEASED = 0xFF6666
};

/* The board's buttons. */
enum {
    BUTTON_COUNT = 3
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
    LOOP_MS = 50,    /* read the buttons this often */
    RUN_MS = 120000  /* the example ends after two minutes */
};

/* The lcd line: "ex07: watching ", any int32_t and " hardware
 * buttons", with its NUL. */
enum {
    LCD_CHARS = 48
};

/* How a button is read. */
typedef enum {
    READ_BOARD_BUTTON0, /* the board's own button 0, through gpio */
    READ_BASE_BUTTON0,  /* the QWA309 base board's button 0 */
    READ_BASE_BUTTON1   /* the QWA309 base board's button 1 */
} reader_t;

/* One button of the board's table. */
typedef struct {
    const char *name;
    reader_t reader;
    uint32_t color;
} button_t;

static const button_t BUTTONS[BUTTON_COUNT] = {
    {"USER Button 1", READ_BOARD_BUTTON0, COLOR_CYAN},
    {"SW4", READ_BASE_BUTTON0, COLOR_ORANGE},
    {"SW5", READ_BASE_BUTTON1, COLOR_GREEN}};

/* The widgets the loop changes and the last state shown in each
 * column. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t leds[BUTTON_COUNT];
    bento_ui_id_t states[BUTTON_COUNT];
    bool shown[BUTTON_COUNT];
    bool known[BUTTON_COUNT];
} buttons_screen_t;

/* A position or size on the screen: the widget arguments are 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

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
 * 4). */
static int16_t centered_at(int32_t center_x, const char *text)
{
    return px(center_x - (((utf8_length(text) * FONT_PX) / 2) / 2));
}

/* The middle of a column: the columns sit symmetrically around the
 * screen's centre. */
static int32_t column_center(int32_t column)
{
    return SCREEN_CENTER_X + (((column * 2) - (BUTTON_COUNT - 1)) * COLUMN_HALF_GAP);
}

/* `import buttons` and gpio.button(0), in the original's order. */
static bento_status_t check_buttons(void)
{
    bento_status_t status = (bento_buttons_count() > 0U) ? BENTO_OK : BENTO_E_UNSUPPORTED;

    if ((status == BENTO_OK) && (bento_gpio_num_buttons() == 0U)) {
        status = BENTO_E_INVALID_ARGUMENT;
    }
    return status;
}

/* Reads one button; a failed read counts as not pressed, as the
 * original's `except OSError`, but a ValueError still ends the
 * program. */
static bento_status_t read_button(int32_t button, bool *pressed)
{
    bento_status_t status;

    *pressed = false;
    if (BUTTONS[button].reader == READ_BOARD_BUTTON0) {
        status = bento_gpio_button_is_pressed(0U, pressed);
    } else if (BUTTONS[button].reader == READ_BASE_BUTTON0) {
        status = bento_buttons_pressed(0U, pressed);
    } else {
        status = bento_buttons_pressed(1U, pressed);
    }
    if ((status != BENTO_OK) && (status != BENTO_E_INVALID_ARGUMENT)) {
        *pressed = false;
        status = BENTO_OK;
    }
    return status;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text, int16_t x, int16_t y,
                                uint32_t color)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
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

/* One column: the button's LED, its name and its state. */
static bento_status_t add_column(buttons_screen_t *screen, int32_t button)
{
    const int32_t center_x = column_center(button);
    const char *state = "Released";
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = px(center_x - (LED_SIZE / 2));
    args.y = LED_Y;
    args.w = LED_SIZE;
    args.h = LED_SIZE;
    args.color = BUTTONS[button].color;
    args.font_px = 0;
    status = bento_ui_led_create(screen->ui, &args, &screen->leds[button]);
    if (status == BENTO_OK) {
        status = add_label(screen->ui, BUTTONS[button].name,
                           centered_at(center_x, BUTTONS[button].name), NAME_Y, COLOR_TEXT);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = state;
        args.text_len = (uint32_t)strlen(state);
        args.x = px(center_x - STATE_DX);
        args.y = STATE_Y;
        args.color = COLOR_RELEASED;
        args.font_px = STATE_FONT_PX;
        status = bento_ui_label_create(screen->ui, &args, &screen->states[button]);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(buttons_screen_t *screen)
{
    const char *title = "Part 1 Ex7: Hardware Button Status";
    const char *hint = "[Part II] Press the buttons on the board";
    const char *footer = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";
    int32_t button;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX, BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_background(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, title, centered_at(SCREEN_CENTER_X, title), TITLE_Y,
                           COLOR_TEXT);
    }
    for (button = 0; (button < BUTTON_COUNT) && (status == BENTO_OK); button++) {
        status = add_column(screen, button);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, hint, centered_at(SCREEN_CENTER_X, hint), HINT_Y,
                           COLOR_HINT);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, footer, centered_at(SCREEN_CENTER_X, footer), FOOTER_Y,
                           COLOR_FOOTER);
    }
    return status;
}

/* Writes a column only when its state changed (or was never shown). */
static void show_button(buttons_screen_t *screen, int32_t button, bool pressed)
{
    const char *text = pressed ? "PRESSED" : "Released";

    if (!screen->known[button] || (screen->shown[button] != pressed)) {
        screen->shown[button] = pressed;
        screen->known[button] = true;
        /* MicroPython's widget setters ignore a refusal (modui.c), so
         * the twin does too. */
        (void)bento_ui_set_value(screen->ui, screen->leds[button], pressed ? 1 : 0);
        (void)bento_ui_set_text(screen->ui, screen->states[button], text, (uint32_t)strlen(text));
        (void)bento_ui_set_color(screen->ui, screen->states[button],
                                 pressed ? COLOR_PRESSED : COLOR_RELEASED);
    }
}

static bento_status_t read_buttons(buttons_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    int32_t button;
    bento_status_t status = BENTO_OK;

    /* No widget here answers a tap (the "< Menu" button is left out),
     * so the events are not read, and ui.poll() answers no events
     * when it fails. */
    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count, NULL);
    for (button = 0; (button < BUTTON_COUNT) && (status == BENTO_OK); button++) {
        bool pressed = false;

        status = read_button(button, &pressed);
        if (status == BENTO_OK) {
            show_button(screen, button, pressed);
        }
    }
    return status;
}

/* Reads every button every LOOP_MS until RUN_MS has passed or Stop is
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
    buttons_screen_t screen;
    char line[LCD_CHARS];
    const int32_t button_count = BUTTON_COUNT;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = check_buttons();
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)snprintf(line, sizeof(line), "ex07: watching %" PRId32 " hardware buttons",
                       button_count);
        (void)bento_lcd_print(line);
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("ex07_hw_button_status: done\n");
        status = show_end_banner(screen.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
