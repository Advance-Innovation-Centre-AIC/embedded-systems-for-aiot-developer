/*
 * ex09_hw_gpio_dashboard.c -- แผงคุม GPIO จริง (HW GPIO Dashboard, Section 2, Dev Kit)
 *
 * คอลัมน์ไฟ LED สามแถว: แดงกับเขียวสลับด้วยสวิตช์และสั่ง RGB LED บน SoM
 * น้ำเงินหรี่ตาม VR1 แผงล่างแสดงปุ่มจริงทั้งสามของบอร์ด (USR BTN1, SW4, SW5)
 * แผงล่างขวาแสดง bar และเปอร์เซ็นต์ของ VR1 ปุ่ม All ON / All OFF คุมทั้งชุด
 * สถานะแดงเขียวขึ้น RGB Matrix แถวบน ทำงาน 2 นาทีแล้วจบ
 * และวาดแถบ "ตัวอย่างจบแล้ว กดปุ่มไม่ได้" ทับแถวล่าง
 *
 * A column of three LEDs: red and green follow their switches and drive
 * the Dev Kit's RGB LED on the SoM; blue is dimmed by VR1. The bottom
 * panel shows the board's three real buttons (USR BTN1, SW4, SW5); the
 * one at the bottom right shows VR1 as a bar and a percentage. All ON
 * and All OFF set the whole set, and red and green also show on the RGB
 * matrix's top row. The example runs for two minutes, then draws a bar
 * over the bottom rows saying it has ended.
 *
 * Twin of ex09_hw_gpio_dashboard.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/sec2_ui_to_hw/devkit/ex09_hw_gpio_dashboard.py),
 * written into one clean file from the library's C twin of the Example
 * Explorer program aiot-lvgl-part1-ex09-hw-gpio-dashboard (similarity
 * 0.58) and the part1 twin of this course. Style: CLEAN_C.md. Where C
 * differs: `import pots` / `import buttons` / `import rgbmatrix` raising
 * ImportError is BENTO_E_UNSUPPORTED when the board lacks them, and
 * gpio.button(0) or gpio.led(i) of a part the board lacks returns
 * BENTO_E_INVALID_ARGUMENT where MicroPython raises ValueError; the
 * board's table of (name, reader) keeps a reader number in place of the
 * Python function; a button read that fails counts as "not pressed", as
 * the original's `except OSError`; btn_prev[i] = None is a "known" flag
 * beside each state. Like the original, the real LEDs are left as they
 * are when the example ends. Left out on purpose: the "< Menu" button,
 * which the original shows only when the board's own menu starts it
 * (MENU_MODE); a C program is never started from that menu.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_buttons.h"
#include "bento_pots.h"
#include "bento_rgbmatrix.h"
#include "bento_runtime.h"

/* Screen layout on the 792 x 398 canvas. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    PCT_FONT_PX = 24,
    TITLE_Y = 7,
    ROW_Y = 54, /* C's 65 + 50 i, scaled: 54 + 41 i */
    ROW_H = 41,
    LED_X = 274, /* CX - 122 */
    LED_SIZE = 45,
    NAME_X = 361, /* CX - 35, less 3 px per character */
    NAME_CHAR_PX = 3,
    NAME_DY = 12,
    BLUE_NAME_DY = 4,
    SWITCH_X = 406, /* CX + 45 - 35 */
    SWITCH_DY = 2,
    SWITCH_W = 70,
    SWITCH_H = 38,
    BOX_Y = 271, /* the two panels at the bottom, 225 x 106 */
    BOX_W = 225,
    BOX_H = 106,
    BOX_BORDER_PX = 2,
    BOX_LEFT_X = 137,
    BOX_RIGHT_X = 562,
    BOX_TITLE_Y = 279,
    BUTTONS_TITLE_X = 228, /* 249 - 21 */
    BUTTONS_TITLE_Y = 277,
    BUTTON_LED_Y = 299,
    BUTTON_LED_SIZE = 32,
    BUTTON_NAME_Y = 338,
    POT_TITLE_X = 629, /* 674 - 45 */
    POT_BAR_X = 584,
    POT_BAR_Y = 305,
    POT_BAR_W = 180,
    POT_BAR_H = 21,
    POT_PCT_X = 664,
    POT_PCT_Y = 336,
    ALL_BUTTON_DX =
        60, /* All ON / All OFF, left and right of the centre */
    ALL_BUTTON_Y = 225,
    ALL_BUTTON_W = 100,
    ALL_BUTTON_H = 40,
    FOOTER_Y = 380
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x1A1A2E,
    COLOR_BOX = 0x0F0F23,
    COLOR_BOX_EDGE = 0x444444,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_FOOTER = 0x666666,
    COLOR_RED = 0xF44336, /* LVGL's palette */
    COLOR_GREEN = 0x4CAF50,
    COLOR_BLUE = 0x2196F3,
    COLOR_ORANGE = 0xFF9800,
    COLOR_CYAN = 0x00AAFF,
    COLOR_POT_BAR = 0x0088FF,
    COLOR_PRESSED = 0x00FF00,
    COLOR_RELEASED = 0xFF6666
};

/* The two switched channels, the board's buttons, and the values the
 * loop works with. */
enum {
    CHANNEL_RED = 0,
    CHANNEL_GREEN = 1,
    CHANNEL_COUNT = 2,
    BUTTON_COUNT = 3,
    ADC_MAX = 4095,
    PCT_FULL = 100,
    LEVEL_MAX = 255, /* PROP_LED_BRIGHTNESS range */
    PCT_CHANGE_MIN = 1,
    NO_VALUE = -1, /* prev_p before the first read */
    MATRIX_PIXELS =
        3, /* each channel lights three columns of the top row */
    MATRIX_STEP = 4
};

/* The Dev Kit's single RGB LED on the SoM, by pin: red P20.6, blue
 * P20.5, green P20.4 (LED1 and LED2 of the firmware's table switch,
 * but are not visible on the assembled board). */
enum {
    LED_RED = 2,
    LED_BLUE = 3,
    LED_GREEN = 4,
    LED_HIGHEST =
        4 /* gpio.led() of each is made, so this one must exist */
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
    LOOP_MS =
        100, /* read the button and the potentiometer this often */
    RUN_MS = 120000 /* the example ends after two minutes */
};

/* "n%" for any int32_t, with its NUL. */
enum {
    PCT_CHARS = 16
};

typedef struct {
    const char *name;
    uint32_t color;
    uint32_t hw_led;
    int32_t matrix_color;
} channel_t;

static const channel_t CHANNELS[CHANNEL_COUNT] = {
    {"Red", COLOR_RED, LED_RED, BENTO_RGBMATRIX_RED},
    {"Green", COLOR_GREEN, LED_GREEN, BENTO_RGBMATRIX_GREEN}};

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
} button_t;

static const button_t BUTTONS[BUTTON_COUNT] = {
    {"USR BTN1", READ_BOARD_BUTTON0},
    {"SW4", READ_BASE_BUTTON0},
    {"SW5", READ_BASE_BUTTON1}};

/* The widgets the loop changes and the last values shown. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t leds[CHANNEL_COUNT];
    bento_ui_id_t switches[CHANNEL_COUNT];
    bento_ui_id_t led_blue;
    bento_ui_id_t button_leds[BUTTON_COUNT];
    bento_ui_id_t button_names[BUTTON_COUNT];
    bento_ui_id_t pot_bar;
    bento_ui_id_t pot_label;
    bento_ui_id_t all_on;
    bento_ui_id_t all_off;
    bool pressed[BUTTON_COUNT];
    bool pressed_known[BUTTON_COUNT];
    int32_t prev_pct;
} dashboard_t;

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

/* x that centres a text: half its width left of the centre, taking a
 * character as half the font size wide (the original's len(text) *
 * size, floor-divided by 4). */
static int16_t centered_x(const char *text)
{
    return px(SCREEN_CENTER_X -
              (((utf8_length(text) * FONT_PX) / 2) / 2));
}

/* The imports and the parts the original makes, in its order:
 * `import pots`, `import buttons`, gpio.button(0), `import rgbmatrix`,
 * then gpio.led() of each LED. */
static bento_status_t check_hardware(void)
{
    const uint32_t highest_led = LED_HIGHEST;
    bento_status_t status =
        (bento_pots_count() > 0U) ? BENTO_OK : BENTO_E_UNSUPPORTED;

    if ((status == BENTO_OK) && (bento_buttons_count() == 0U)) {
        status = BENTO_E_UNSUPPORTED;
    }
    if ((status == BENTO_OK) && (bento_gpio_num_buttons() == 0U)) {
        status = BENTO_E_INVALID_ARGUMENT;
    }
    if ((status == BENTO_OK) && !bento_rgbmatrix_available()) {
        status = BENTO_E_UNSUPPORTED;
    }
    if ((status == BENTO_OK) &&
        (highest_led >= bento_gpio_num_leds())) {
        status = BENTO_E_INVALID_ARGUMENT;
    }
    return status;
}

/* VR1 in percent. The raw value is never negative, so C's division is
 * the floor division. */
static bento_status_t read_pot_pct(int32_t *pct)
{
    uint16_t raw = 0U;
    const bento_status_t status = bento_pots_read(0U, &raw);

    *pct = ((int32_t)raw * PCT_FULL) / ADC_MAX;
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

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int32_t x, int32_t y, uint32_t color,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = px(x);
    args.y = px(y);
    args.color = color;
    args.font_px = FONT_PX;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_big_label(bento_ui_t *ui, const char *text,
                                    const int16_t place[2],
                                    uint32_t color, int32_t font_px,
                                    bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place[0];
    args.y = place[1];
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_led(bento_ui_t *ui, int32_t x, int32_t y,
                              int32_t size, uint32_t color,
                              bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = px(x);
    args.y = px(y);
    args.w = px(size);
    args.h = px(size);
    args.color = color;
    args.font_px = 0;
    return bento_ui_led_create(ui, &args, out_id);
}

/* A panel; border_px 0 with border = colour is the background. */
static bento_status_t add_panel(bento_ui_t *ui, int32_t x, int32_t y,
                                int32_t w, int32_t h, bool boxed)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;

    bento_ui_widget_args_init(&args);
    args.x = px(x);
    args.y = px(y);
    args.w = px(w);
    args.h = px(h);
    args.color = boxed ? COLOR_BOX : COLOR_BACKGROUND;
    /* a panel's min= is its border colour; the background repeats its
     * own colour, as the original does */
    args.min_val = boxed ? COLOR_BOX_EDGE : COLOR_BACKGROUND;
    args.max_val = 0;
    args.font_px = boxed ? BOX_BORDER_PX : 0;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* One switched row: its LED, its name and its switch. */
static bento_status_t add_channel(dashboard_t *board, int32_t channel)
{
    const char *name = CHANNELS[channel].name;
    const int32_t y = ROW_Y + (channel * ROW_H);
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_led(board->ui, LED_X, y, LED_SIZE, CHANNELS[channel].color,
                &board->leds[channel]);

    if (status == BENTO_OK) {
        status = add_label(board->ui, name,
                           NAME_X - (utf8_length(name) * NAME_CHAR_PX),
                           y + NAME_DY, COLOR_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = SWITCH_X;
        args.y = px(y + SWITCH_DY);
        args.w = SWITCH_W;
        args.h = SWITCH_H;
        status = bento_ui_switch_create(board->ui, &args,
                                        &board->switches[channel]);
    }
    return status;
}

/* The third row: the blue LED the potentiometer dims. */
static bento_status_t add_blue_row(dashboard_t *board)
{
    const int32_t rows_above = CHANNEL_COUNT;
    const int32_t y = ROW_Y + (rows_above * ROW_H);
    bento_ui_id_t unused;
    bento_status_t status = add_led(board->ui, LED_X, y, LED_SIZE,
                                    COLOR_BLUE, &board->led_blue);

    if (status == BENTO_OK) {
        status =
            add_label(board->ui, "Blue",
                      NAME_X - (utf8_length("Blue") * NAME_CHAR_PX),
                      y + BLUE_NAME_DY, COLOR_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, "(POT ctrl)", SWITCH_X,
                           y + NAME_DY, COLOR_CYAN, &unused);
    }
    return status;
}

/* One button of the bottom panel: its small LED and its name, which
 * turns green while pressed. */
static bento_status_t add_button_cell(dashboard_t *board,
                                      int32_t button)
{
    const char *name = BUTTONS[button].name;
    const int32_t button_count = BUTTON_COUNT;
    const int32_t center_x =
        BOX_LEFT_X +
        ((BOX_W / (button_count * 2)) * ((button * 2) + 1));
    bento_status_t status = add_led(
        board->ui, center_x - (BUTTON_LED_SIZE / 2), BUTTON_LED_Y,
        BUTTON_LED_SIZE, COLOR_ORANGE, &board->button_leds[button]);

    if (status == BENTO_OK) {
        status = add_label(
            board->ui, name,
            center_x - (((utf8_length(name) * FONT_PX) / 2) / 2),
            BUTTON_NAME_Y, COLOR_RELEASED,
            &board->button_names[button]);
    }
    return status;
}

/* Bottom left: every real button of the board. */
static bento_status_t add_button_panel(dashboard_t *board)
{
    int32_t button;
    bento_ui_id_t unused;
    bento_status_t status =
        add_panel(board->ui, BOX_LEFT_X, BOX_Y, BOX_W, BOX_H, true);

    if (status == BENTO_OK) {
        status = add_label(board->ui, "Buttons", BUTTONS_TITLE_X,
                           BUTTONS_TITLE_Y, COLOR_TEXT, &unused);
    }
    for (button = 0; (button < BUTTON_COUNT) && (status == BENTO_OK);
         button++) {
        status = add_button_cell(board, button);
    }
    return status;
}

/* Bottom right: the potentiometer as a bar and a percentage. */
static bento_status_t add_pot_panel(dashboard_t *board)
{
    const int16_t pct_place[2] = {POT_PCT_X, POT_PCT_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_panel(board->ui, BOX_RIGHT_X, BOX_Y, BOX_W, BOX_H, true);

    if (status == BENTO_OK) {
        status = add_label(board->ui, "POT -> Blue LED", POT_TITLE_X,
                           BOX_TITLE_Y, COLOR_CYAN, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = POT_BAR_X;
        args.y = POT_BAR_Y;
        args.w = POT_BAR_W;
        args.h = POT_BAR_H;
        args.min_val = 0;
        args.max_val = PCT_FULL;
        args.font_px = 0;
        args.color = COLOR_POT_BAR;
        status = bento_ui_bar_create(board->ui, &args, &board->pot_bar);
    }
    if (status == BENTO_OK) {
        status = add_big_label(board->ui, "0%", pct_place, COLOR_CYAN,
                               PCT_FONT_PX, &board->pot_label);
    }
    return status;
}

static bento_status_t add_all_button(bento_ui_t *ui, const char *text,
                                     int32_t center_x, uint32_t color,
                                     bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = px(center_x - (ALL_BUTTON_W / 2));
    args.y = ALL_BUTTON_Y;
    args.w = ALL_BUTTON_W;
    args.h = ALL_BUTTON_H;
    args.color = color;
    args.font_px = FONT_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* The title and the three LED rows. */
static bento_status_t build_top(dashboard_t *board)
{
    const char *title = "Part 1 Ex9: HW GPIO Dashboard";
    int32_t channel;
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(board->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status =
            add_panel(board->ui, 0, 0, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                      BENTO_RUNTIME_SCREEN_HEIGHT_PX, false);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, title, centered_x(title), TITLE_Y,
                           COLOR_TEXT, &unused);
    }
    for (channel = 0; (channel < CHANNEL_COUNT) && (status == BENTO_OK);
         channel++) {
        status = add_channel(board, channel);
    }
    if (status == BENTO_OK) {
        status = add_blue_row(board);
    }
    return status;
}

/* The two panels, the All ON / All OFF buttons and the footer. */
static bento_status_t build_bottom(dashboard_t *board)
{
    const char *footer =
        "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";
    bento_ui_id_t unused;
    bento_status_t status = add_button_panel(board);

    if (status == BENTO_OK) {
        status = add_pot_panel(board);
    }
    if (status == BENTO_OK) {
        status = add_all_button(board->ui, "All ON",
                                SCREEN_CENTER_X - ALL_BUTTON_DX,
                                COLOR_GREEN, &board->all_on);
    }
    if (status == BENTO_OK) {
        status = add_all_button(board->ui, "All OFF",
                                SCREEN_CENTER_X + ALL_BUTTON_DX,
                                COLOR_RED, &board->all_off);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, footer, centered_x(footer),
                           FOOTER_Y, COLOR_FOOTER, &unused);
    }
    return status;
}

/* One switched channel: the widget, the real LED, the matrix and a
 * line. */
static bento_status_t set_channel(const dashboard_t *board,
                                  int32_t channel, bool on)
{
    const int32_t color =
        on ? CHANNELS[channel].matrix_color : BENTO_RGBMATRIX_OFF;
    const uint32_t led = CHANNELS[channel].hw_led;
    int32_t column;
    bento_status_t status;

    /* MicroPython's widget setters ignore a refusal (modui.c), so the
     * twin does too. */
    (void)bento_ui_set_value(board->ui, board->leds[channel],
                             on ? 1 : 0);
    status = on ? bento_gpio_led_on(led) : bento_gpio_led_off(led);
    for (column = 0; (column < MATRIX_PIXELS) && (status == BENTO_OK);
         column++) {
        status = bento_rgbmatrix_pixel((channel * MATRIX_STEP) + column,
                                       0, color);
    }
    if (status == BENTO_OK) {
        (void)printf("[HW] %s: %s\n", CHANNELS[channel].name,
                     on ? "ON" : "OFF");
    }
    return status;
}

/* All ON / All OFF: both channels, then the blue LED full or dark. */
static bento_status_t set_all(const dashboard_t *board, bool on)
{
    bento_status_t status = set_channel(board, CHANNEL_RED, on);

    if (status == BENTO_OK) {
        status = set_channel(board, CHANNEL_GREEN, on);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_value(board->ui, board->led_blue,
                                 on ? 1 : 0);
        status = bento_gpio_led_brightness(LED_BLUE, on ? PCT_FULL : 0);
    }
    return status;
}

/* The channel whose switch this is, or CHANNEL_COUNT when it is no
 * switch. */
static int32_t channel_of_switch(const dashboard_t *board,
                                 bento_ui_id_t widget)
{
    int32_t found = CHANNEL_COUNT;
    int32_t channel;

    for (channel = CHANNEL_COUNT - 1; channel >= 0; channel--) {
        if (board->switches[channel] == widget) {
            found = channel;
        }
    }
    return found;
}

/* True when the event is of this kind: the comparison has one type
 * on both sides. */
static bool is_kind(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

static bento_status_t handle_event(const dashboard_t *board,
                                   const bento_ui_event_t *event)
{
    const int32_t channel = channel_of_switch(board, event->id);
    bento_status_t status = BENTO_OK;

    if (is_kind(event, BENTO_UI_EVENT_TOGGLED) &&
        (channel < CHANNEL_COUNT)) {
        status = set_channel(board, channel, event->value != 0);
    } else if (is_kind(event, BENTO_UI_EVENT_CLICKED) &&
               (event->id == board->all_on)) {
        status = set_all(board, true);
    } else if (is_kind(event, BENTO_UI_EVENT_CLICKED) &&
               (event->id == board->all_off)) {
        status = set_all(board, false);
    } else {
        /* any other event changes nothing */
    }
    return status;
}

static bento_status_t handle_events(const dashboard_t *board)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    /* ui.poll() answers no events when it fails, so a failed poll is
     * not an error here. */
    if (bento_ui_poll(board->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = handle_event(board, &events[i]);
    }
    return status;
}

/* Every button: its LED and the colour of its name follow, written
 * only when its state changed. */
static bento_status_t watch_buttons(dashboard_t *board)
{
    int32_t button;
    bento_status_t status = BENTO_OK;

    for (button = 0; (button < BUTTON_COUNT) && (status == BENTO_OK);
         button++) {
        bool pressed = false;

        status = read_button(button, &pressed);
        if ((status == BENTO_OK) &&
            (!board->pressed_known[button] ||
             (pressed != board->pressed[button]))) {
            board->pressed[button] = pressed;
            board->pressed_known[button] = true;
            (void)bento_ui_set_value(
                board->ui, board->button_leds[button], pressed ? 1 : 0);
            (void)bento_ui_set_color(
                board->ui, board->button_names[button],
                pressed ? COLOR_PRESSED : COLOR_RELEASED);
        }
    }
    return status;
}

/* The potentiometer: the bar, the percentage and the blue LED follow
 * when it moved by more than one percent. */
static bento_status_t watch_pot(dashboard_t *board)
{
    char text[PCT_CHARS];
    int32_t pct = 0;
    int32_t change;
    bento_status_t status = read_pot_pct(&pct);

    change = pct - board->prev_pct;
    if ((status == BENTO_OK) &&
        (((change < 0) ? -change : change) > PCT_CHANGE_MIN)) {
        board->prev_pct = pct;
        (void)bento_ui_set_value(board->ui, board->pot_bar, pct);
        (void)snprintf(text, sizeof(text), "%" PRId32 "%%", pct);
        (void)bento_ui_set_text(board->ui, board->pot_label, text,
                                (uint32_t)strlen(text));
        (void)bento_ui_set_value(board->ui, board->led_blue, 1);
        (void)bento_ui_set_prop(board->ui, board->led_blue,
                                BENTO_UI_PROP_LED_BRIGHTNESS,
                                (pct * LEVEL_MAX) / PCT_FULL);
        status = bento_gpio_led_brightness(LED_BLUE, pct);
    }
    return status;
}

/* Reads the touch events, the buttons and the potentiometer every
 * LOOP_MS until RUN_MS has passed or Stop is pressed. */
static bento_status_t run_loop(dashboard_t *board)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = handle_events(board);
        if (status == BENTO_OK) {
            status = watch_buttons(board);
        }
        if (status == BENTO_OK) {
            status = watch_pot(board);
        }
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
    dashboard_t board;
    bento_status_t status;

    (void)memset(&board, 0, sizeof(board));
    board.ui = bento_runtime_get_ui();
    board.prev_pct = NO_VALUE;
    status = check_hardware();
    if (status == BENTO_OK) {
        status = build_top(&board);
    }
    if (status == BENTO_OK) {
        status = build_bottom(&board);
    }
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print("ex09: switches -> real Red/Green, pot "
                              "-> real Blue PWM, watch BTN2");
    }
    if (status == BENTO_OK) {
        status = run_loop(&board);
    }
    if (status == BENTO_OK) {
        (void)printf("ex09_hw_gpio_dashboard: done\n");
        status = show_end_banner(board.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
