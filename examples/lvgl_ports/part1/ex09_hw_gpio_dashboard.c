/*
 * ex09_hw_gpio_dashboard.c -- แผงคุม GPIO จริง (HW GPIO Dashboard, Part 1)
 *
 * คอลัมน์ไฟ LED สามแถว: แดงกับเขียวสลับด้วยสวิตช์และสั่งไฟจริง น้ำเงินคุมด้วย
 * ตัวต้านทานปรับค่าจริงแบบหรี่ไฟ แผงล่างซ้ายแสดงสถานะปุ่มที่สอง (SW9 บน Dev Kit,
 * CapSense BTN0 บน Eva Kit) แผงล่างขวาแสดง bar และเปอร์เซ็นต์ของตัวต้านทาน
 * ปุ่ม All ON / All OFF คุมทั้งชุด บน Dev Kit สถานะแดงเขียวขึ้น RGB Matrix แถวบนด้วย
 * ทำงาน 2 นาทีแล้วจบ
 *
 * A column of three LEDs: red and green follow their switches and drive
 * the real LEDs; blue is dimmed by the real potentiometer. The panel at
 * the bottom left shows the second button (SW9 on the Dev Kit, CapSense
 * BTN0 on the Eva Kit); the one at the bottom right shows the
 * potentiometer as a bar and a percentage. All ON and All OFF set the
 * whole set. On the Dev Kit red and green also show on the RGB matrix's
 * top row. The example runs for two minutes, then ends.
 *
 * Twin of ex09_hw_gpio_dashboard.py (embedded-systems-for-aiot-developer/
 * examples/lvgl_ports/part1/ex09_hw_gpio_dashboard.py), written into
 * one clean file from the library's C twin of the Example Explorer
 * program aiot-lvgl-part1-ex09-hw-gpio-dashboard (the same code).
 * Style: CLEAN_C.md. Where C differs:
 *  - `import pots` / `import buttons` / `import rgbmatrix` succeeding
 *    is bento_pots_count() > 0 / bento_buttons_count() > 0 /
 *    bento_rgbmatrix_available(); the CapSense probe that may raise is
 *    a status other than BENTO_OK;
 *  - pot_pct() and read_btn2(), chosen at import time in MicroPython,
 *    are flags that pick the same calls;
 *  - prev_b = None is a "known" flag beside the last state;
 *  - a fallback LED the board lacks returns BENTO_E_INVALID_ARGUMENT
 *    where gpio.led() raises ValueError.
 * Like the original, the real LEDs are left as they are when the
 * example ends. Left out on purpose: the "< Menu" button, which the
 * original shows only when the board's own menu starts it (MENU_MODE);
 * a C program is never started from that menu.
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
#include "bento_sensors.h"

/* Screen layout on the 792 x 398 canvas. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    STATE_FONT_PX = 20,
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
    BOX_LEFT_X = 5,
    BOX_RIGHT_X = 562,
    BOX_TITLE_Y = 279,
    BUTTON_TITLE_X = 117, /* centre of the left panel's title */
    BUTTON_LED_X = 20,
    BUTTON_LED_Y = 311,
    BUTTON_LED_SIZE = 50,
    BUTTON_STATE_X = 105,
    BUTTON_STATE_Y = 330,
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

/* The two switched channels, the blue LED's name search, and the
 * values the loop works with. */
enum {
    CHANNEL_RED = 0,
    CHANNEL_GREEN = 1,
    CHANNEL_COUNT = 2,
    FALLBACK_BLUE = 2,
    ADC_MAX = 4095,
    PCT_FULL = 100,
    LEVEL_MAX = 255, /* PROP_LED_BRIGHTNESS range */
    PCT_CHANGE_MIN = 1,
    NO_VALUE = -1, /* prev_p before the first read */
    MATRIX_PIXELS =
        3, /* each channel lights three columns of the top row */
    MATRIX_STEP = 4
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS =
        100, /* read the button and the potentiometer this often */
    RUN_MS = 120000 /* the example ends after two minutes */
};

/* Text buffers: the panel's title, and "n%" for any int32_t. */
enum {
    TITLE_CHARS = 40,
    PCT_CHARS = 16
};

typedef struct {
    const char *name;
    uint32_t color;
    int32_t matrix_color;
    const char *led_name_part;
    uint32_t fallback_led;
} channel_t;

static const channel_t CHANNELS[CHANNEL_COUNT] = {
    {"Red", COLOR_RED, BENTO_RGBMATRIX_RED, "RED", 0U},
    {"Green", COLOR_GREEN, BENTO_RGBMATRIX_GREEN, "GREEN", 1U}};

/* Where the second button is read from. */
typedef enum {
    BUTTON2_NONE,
    BUTTON2_BUTTONS, /* the QWA309 base board's buttons module */
    BUTTON2_CAPSENSE
} button2_source_t;

/* The hardware the imports found, the real LEDs, the widgets the loop
 * changes and the last values shown. */
typedef struct {
    bento_ui_t *ui;
    bool has_pots;
    bool has_matrix;
    button2_source_t source;
    const char *button2_name;
    uint32_t hw[CHANNEL_COUNT];
    uint32_t hw_blue;
    bento_ui_id_t leds[CHANNEL_COUNT];
    bento_ui_id_t switches[CHANNEL_COUNT];
    bento_ui_id_t led_blue;
    bento_ui_id_t button_led;
    bento_ui_id_t button_state;
    bento_ui_id_t pot_bar;
    bento_ui_id_t pot_label;
    bento_ui_id_t all_on;
    bento_ui_id_t all_off;
    bool pressed;
    bool pressed_known;
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

/* Python's `part in text`. strstr is not among the C library
 * functions a PSoC Edge native module may call; strncmp is. */
static bool text_contains(const char *text, const char *part)
{
    const size_t part_len = strlen(part);
    bool found = (part_len == 0U);
    size_t i;

    for (i = 0U; (text[i] != '\0') && !found; i++) {
        found = (strncmp(&text[i], part, part_len) == 0);
    }
    return found;
}

/* The first LED whose name holds name_part, else the fallback index,
 * which the board must have (gpio.led() raises ValueError). */
static bento_status_t find_led(const char *name_part, uint32_t fallback,
                               uint32_t *led)
{
    bento_gpio_board_info_t info;
    bento_status_t status;
    bool found = false;
    uint32_t i;

    bento_gpio_board_info_init(&info);
    status = bento_gpio_board_info(&info);
    for (i = 0U; (i < info.leds) && (status == BENTO_OK) && !found;
         i++) {
        const char *name = NULL;

        status = bento_gpio_led_name(i, &name);
        if ((status == BENTO_OK) && text_contains(name, name_part)) {
            *led = i;
            found = true;
        }
    }
    if ((status == BENTO_OK) && !found) {
        if (fallback < bento_gpio_num_leds()) {
            *led = fallback;
        } else {
            status = BENTO_E_INVALID_ARGUMENT;
        }
    }
    return status;
}

/* The imports' choices, in the original's order: pots, the second
 * button's source, the RGB matrix. */
static void find_hardware(dashboard_t *board)
{
    uint8_t button0 = 0U;
    uint8_t button1 = 0U;

    board->has_pots = (bento_pots_count() > 0U);
    if (bento_buttons_count() > 0U) {
        board->source = BUTTON2_BUTTONS;
        board->button2_name = "SW9";
    } else if (bento_sensors_capsense_buttons(&button0, &button1) ==
               BENTO_OK) {
        board->source = BUTTON2_CAPSENSE;
        board->button2_name = "CapSense BTN0";
    } else {
        board->source = BUTTON2_NONE;
        board->button2_name = "none";
    }
    board->has_matrix = bento_rgbmatrix_available();
}

static bento_status_t find_leds(dashboard_t *board)
{
    int32_t channel;
    bento_status_t status = BENTO_OK;

    for (channel = 0; (channel < CHANNEL_COUNT) && (status == BENTO_OK);
         channel++) {
        status = find_led(CHANNELS[channel].led_name_part,
                          CHANNELS[channel].fallback_led,
                          &board->hw[channel]);
    }
    if (status == BENTO_OK) {
        status = find_led("BLUE", FALLBACK_BLUE, &board->hw_blue);
    }
    return status;
}

/* The potentiometer in percent: pots.read(0) scaled on the Dev Kit,
 * else the sensors module's percentage, truncated as int() does. */
static bento_status_t read_pot_pct(const dashboard_t *board,
                                   int32_t *pct)
{
    uint16_t raw = 0U;
    float percent = 0.0F;
    bento_status_t status;

    if (board->has_pots) {
        status = bento_pots_read(0U, &raw);
        *pct = ((int32_t)raw * PCT_FULL) / ADC_MAX;
    } else {
        status = bento_sensors_pot_percent(&percent);
        *pct = (int32_t)percent;
    }
    return status;
}

static bento_status_t read_button2(const dashboard_t *board,
                                   bool *pressed)
{
    uint8_t button0 = 0U;
    uint8_t button1 = 0U;
    bento_status_t status = BENTO_OK;

    *pressed = false;
    if (board->source == BUTTON2_BUTTONS) {
        status = bento_buttons_pressed(0U, pressed);
    } else if (board->source == BUTTON2_CAPSENSE) {
        status = bento_sensors_capsense_buttons(&button0, &button1);
        *pressed = (button0 != 0U);
    } else {
        /* no second button: never pressed */
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
    const int32_t y = ROW_Y + (CHANNEL_COUNT * ROW_H);
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

/* Bottom left: the second button's LED and state. */
static bento_status_t add_button_panel(dashboard_t *board)
{
    const int16_t state_place[2] = {BUTTON_STATE_X, BUTTON_STATE_Y};
    char title[TITLE_CHARS];
    bento_ui_id_t unused;
    bento_status_t status =
        add_panel(board->ui, BOX_LEFT_X, BOX_Y, BOX_W, BOX_H, true);

    (void)snprintf(title, sizeof(title), "USER BTN2 (%s)",
                   board->button2_name);
    if (status == BENTO_OK) {
        status = add_label(board->ui, title,
                           BUTTON_TITLE_X -
                               (utf8_length(title) * NAME_CHAR_PX),
                           BOX_TITLE_Y, COLOR_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_led(board->ui, BUTTON_LED_X, BUTTON_LED_Y,
                    BUTTON_LED_SIZE, COLOR_ORANGE, &board->button_led);
    }
    if (status == BENTO_OK) {
        status = add_big_label(board->ui, "Released", state_place,
                               COLOR_RELEASED, STATE_FONT_PX,
                               &board->button_state);
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
    int32_t column;
    bento_status_t status;

    /* MicroPython's widget setters ignore a refusal (modui.c), so the
     * twin does too. */
    (void)bento_ui_set_value(board->ui, board->leds[channel],
                             on ? 1 : 0);
    status = on ? bento_gpio_led_on(board->hw[channel])
                : bento_gpio_led_off(board->hw[channel]);
    for (column = 0; board->has_matrix && (column < MATRIX_PIXELS) &&
                     (status == BENTO_OK);
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
        status = bento_gpio_led_brightness(board->hw_blue,
                                           on ? PCT_FULL : 0);
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

static bento_status_t handle_event(const dashboard_t *board,
                                   const bento_ui_event_t *event)
{
    const int32_t channel = channel_of_switch(board, event->id);
    bento_status_t status = BENTO_OK;

    if ((event->type == BENTO_UI_EVENT_TOGGLED) &&
        (channel < CHANNEL_COUNT)) {
        status = set_channel(board, channel, event->value != 0);
    } else if ((event->type == BENTO_UI_EVENT_CLICKED) &&
               (event->id == board->all_on)) {
        status = set_all(board, true);
    } else if ((event->type == BENTO_UI_EVENT_CLICKED) &&
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

/* The second button: written only when its state changed. */
static bento_status_t watch_button2(dashboard_t *board)
{
    bool pressed = false;
    bento_status_t status = read_button2(board, &pressed);

    if ((status == BENTO_OK) &&
        (!board->pressed_known || (pressed != board->pressed))) {
        const char *text = pressed ? "PRESSED" : "Released";

        board->pressed = pressed;
        board->pressed_known = true;
        (void)bento_ui_set_value(board->ui, board->button_led,
                                 pressed ? 1 : 0);
        (void)bento_ui_set_text(board->ui, board->button_state, text,
                                (uint32_t)strlen(text));
        (void)bento_ui_set_color(board->ui, board->button_state,
                                 pressed ? COLOR_PRESSED
                                         : COLOR_RELEASED);
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
    bento_status_t status = read_pot_pct(board, &pct);

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
        status = bento_gpio_led_brightness(board->hw_blue, pct);
    }
    return status;
}

/* Reads the touch events, the button and the potentiometer every
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
            status = watch_button2(board);
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

int main(void)
{
    dashboard_t board;
    bento_status_t status;

    (void)memset(&board, 0, sizeof(board));
    board.ui = bento_runtime_get_ui();
    board.prev_pct = NO_VALUE;
    find_hardware(&board);
    status = find_leds(&board);
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
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
