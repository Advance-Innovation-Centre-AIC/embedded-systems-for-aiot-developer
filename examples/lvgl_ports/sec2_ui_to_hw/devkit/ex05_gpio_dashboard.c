/*
 * ex05_gpio_dashboard.c -- แผงคุม GPIO สี่ช่อง (GPIO Dashboard, Section 2, Dev Kit)
 *
 * กล่องกลางจอมีสี่ช่อง แต่ละช่องมีไฟ LED จำลอง ชื่อ และสวิตช์ แตะสวิตช์แล้วไฟช่องนั้น
 * ติดหรือดับ ปุ่ม All ON / All OFF คุมทั้งชุด RGB LED บน SoM ติดตามด้วย
 * โดยช่องสีเหลืองใช้แดงกับเขียวพร้อมกัน และสถานะสี่ช่องขึ้น RGB Matrix แถวบน
 * ทำงาน 2 นาทีแล้วจบ และวาดแถบ "ตัวอย่างจบแล้ว กดปุ่มไม่ได้" ทับแถวล่าง
 *
 * A box in the middle holds four channels, each with a virtual LED, a
 * name and a switch. A switch lights or darkens its channel; All ON and
 * All OFF set the whole set. The Dev Kit's RGB LED on the SoM follows,
 * the yellow channel lighting red and green together, and the four
 * states show on the RGB matrix's top row. The example runs for two
 * minutes, then draws a bar over the bottom rows saying it has ended.
 *
 * Twin of ex05_gpio_dashboard.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/sec2_ui_to_hw/devkit/ex05_gpio_dashboard.py),
 * written into one clean file from the library's C twin of the Example
 * Explorer program aiot-lvgl-part1-ex05-gpio-dashboard (similarity 0.75)
 * and the part1 twin of this course. Style: CLEAN_C.md. Where C differs:
 * `import rgbmatrix` raising ImportError is BENTO_E_UNSUPPORTED when
 * bento_rgbmatrix_available() is false. Like the original, the switches
 * keep the user's position after All ON / All OFF (a MicroPython switch
 * has no setter), and the real LEDs are left as they are when the
 * example ends. Left out on purpose: the "< Menu" button, which the
 * original shows only when the board's own menu starts it (MENU_MODE); a
 * C program is never started from that menu.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_rgbmatrix.h"
#include "bento_runtime.h"

/* Screen layout on the 792 x 398 canvas. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    TITLE_Y = 8,
    BOX_X =
        186, /* the original's 420 x 200 box, 170 high here: CX - 210 */
    BOX_Y = 122, /* 199 + 8 - 85 */
    BOX_W = 420,
    BOX_H = 170,
    BOX_BORDER_PX = 2,
    GRID_DX = 20, /* the 2 x 2 grid of channels inside the box */
    GRID_DY = 14,
    CELL_W = 200,
    CELL_H = 66,
    LED_SIZE = 40,
    NAME_DX = 50, /* name and switch, right of the channel's LED */
    NAME_DY = 12,
    SWITCH_DX = 110,
    SWITCH_DY = 2,
    SWITCH_W = 70,
    SWITCH_H = 36,
    BUTTON_Y = 294,
    BUTTON_W = 110,
    BUTTON_H = 48,
    BUTTON_MARGIN_X = 40,
    FOOTER_Y = 374
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
    COLOR_YELLOW = 0xFFEB3B
};

/* The four channels. */
enum {
    CHANNEL_RED = 0,
    CHANNEL_GREEN = 1,
    CHANNEL_BLUE = 2,
    CHANNEL_YELLOW = 3,
    CHANNEL_COUNT = 4
};

/* Each channel lights MATRIX_PIXELS columns of the matrix's top row,
 * MATRIX_STEP columns apart. */
enum {
    MATRIX_PIXELS = 3,
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
    LOOP_MS = 50,    /* read the touch events this often */
    RUN_MS = 120000  /* the example ends after two minutes */
};

typedef struct {
    const char *name;
    uint32_t color;
    int32_t matrix_color;
} channel_t;

static const channel_t CHANNELS[CHANNEL_COUNT] = {
    {"LED1", COLOR_RED, BENTO_RGBMATRIX_RED},
    {"LED2", COLOR_GREEN, BENTO_RGBMATRIX_GREEN},
    {"LED3", COLOR_BLUE, BENTO_RGBMATRIX_BLUE},
    {"LED4", COLOR_YELLOW, BENTO_RGBMATRIX_YELLOW}};

/* The real LEDs, the widgets the program changes after it has drawn
 * the screen, and the state of each channel. */
typedef struct {
    bento_ui_t *ui;
    uint32_t hw_red;
    uint32_t hw_green;
    uint32_t hw_blue;
    bento_ui_id_t leds[CHANNEL_COUNT];
    bento_ui_id_t switches[CHANNEL_COUNT];
    bento_ui_id_t all_on;
    bento_ui_id_t all_off;
    bool on[CHANNEL_COUNT];
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

/* gpio.led(i) raises ValueError for an LED the board does not have:
 * every LED the original makes must exist. */
static bento_status_t check_leds(void)
{
    const uint32_t highest_led = LED_HIGHEST;

    return (highest_led < bento_gpio_num_leds())
               ? BENTO_OK
               : BENTO_E_INVALID_ARGUMENT;
}

static bento_status_t set_led(uint32_t led, bool on)
{
    return on ? bento_gpio_led_on(led) : bento_gpio_led_off(led);
}

/* A single RGB LED: the yellow channel needs red and green together,
 * so every channel's state is combined before the pins are driven. */
static bento_status_t drive_board_leds(const dashboard_t *board)
{
    const bool yellow = board->on[CHANNEL_YELLOW];
    bento_status_t status =
        set_led(board->hw_red, board->on[CHANNEL_RED] || yellow);

    if (status == BENTO_OK) {
        status = set_led(board->hw_green,
                         board->on[CHANNEL_GREEN] || yellow);
    }
    if (status == BENTO_OK) {
        status = set_led(board->hw_blue, board->on[CHANNEL_BLUE]);
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

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t x, int16_t y, uint32_t color)
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

static bento_status_t add_background_and_box(bento_ui_t *ui)
{
    const char *title = "Part 1 - Example 5: GPIO Dashboard";
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;
    bento_status_t status;

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
    status = bento_ui_panel_create(ui, &args, &panel);
    if (status == BENTO_OK) {
        status = add_label(ui, title, centered_x(title), TITLE_Y,
                           COLOR_TEXT);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = BOX_X;
        args.y = BOX_Y;
        args.w = BOX_W;
        args.h = BOX_H;
        args.color = COLOR_BOX;
        /* a panel's min= is its border colour */
        args.min_val = COLOR_BOX_EDGE;
        args.max_val = 0;
        args.font_px = BOX_BORDER_PX;
        status = bento_ui_panel_create(ui, &args, &panel);
    }
    return status;
}

/* One cell of the grid: the channel's LED, its name and its switch. */
static bento_status_t add_channel(dashboard_t *board, int32_t channel)
{
    const int32_t x = BOX_X + GRID_DX + ((channel % 2) * CELL_W);
    const int32_t y = BOX_Y + GRID_DY + ((channel / 2) * CELL_H);
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = px(x);
    args.y = px(y);
    args.w = LED_SIZE;
    args.h = LED_SIZE;
    args.color = CHANNELS[channel].color;
    args.font_px = 0;
    status =
        bento_ui_led_create(board->ui, &args, &board->leds[channel]);
    if (status == BENTO_OK) {
        status =
            add_label(board->ui, CHANNELS[channel].name,
                      px(x + NAME_DX), px(y + NAME_DY), COLOR_TEXT);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = px(x + SWITCH_DX);
        args.y = px(y + SWITCH_DY);
        args.w = SWITCH_W;
        args.h = SWITCH_H;
        status = bento_ui_switch_create(board->ui, &args,
                                        &board->switches[channel]);
    }
    return status;
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 int16_t x, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = BUTTON_Y;
    args.w = BUTTON_W;
    args.h = BUTTON_H;
    args.color = color;
    args.font_px = FONT_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(dashboard_t *board)
{
    const char *footer =
        "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";
    int32_t channel;
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
        status = add_background_and_box(board->ui);
    }
    for (channel = 0; (channel < CHANNEL_COUNT) && (status == BENTO_OK);
         channel++) {
        status = add_channel(board, channel);
    }
    if (status == BENTO_OK) {
        status = add_button(board->ui, "All ON", BUTTON_MARGIN_X,
                            COLOR_GREEN, &board->all_on);
    }
    if (status == BENTO_OK) {
        status = add_button(board->ui, "All OFF",
                            BENTO_RUNTIME_SCREEN_WIDTH_PX -
                                BUTTON_MARGIN_X - BUTTON_W,
                            COLOR_RED, &board->all_off);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, footer, centered_x(footer),
                           FOOTER_Y, COLOR_FOOTER);
    }
    return status;
}

/* The channel's columns on the matrix's top row, so its state shows
 * on the board too. */
static bento_status_t show_on_matrix(int32_t channel, bool on)
{
    const int32_t color =
        on ? CHANNELS[channel].matrix_color : BENTO_RGBMATRIX_OFF;
    int32_t column;
    bento_status_t status = BENTO_OK;

    for (column = 0; (column < MATRIX_PIXELS) && (status == BENTO_OK);
         column++) {
        status = bento_rgbmatrix_pixel((channel * MATRIX_STEP) + column,
                                       0, color);
    }
    return status;
}

/* One channel: the widget, the matrix, the real LEDs and a line. */
static bento_status_t set_channel(dashboard_t *board, int32_t channel,
                                  bool on)
{
    bento_status_t status;

    board->on[channel] = on;
    /* MicroPython's widget setters ignore a refusal (modui.c), so
     * the twin does too. */
    (void)bento_ui_set_value(board->ui, board->leds[channel],
                             on ? 1 : 0);
    status = show_on_matrix(channel, on);
    if (status == BENTO_OK) {
        status = drive_board_leds(board);
    }
    if (status == BENTO_OK) {
        (void)printf("%s: %s\n", CHANNELS[channel].name,
                     on ? "ON" : "OFF");
    }
    return status;
}

/* All ON / All OFF: every channel, then one line for the whole set. */
static bento_status_t set_all(dashboard_t *board, bool on)
{
    int32_t channel;
    bento_status_t status = BENTO_OK;

    for (channel = 0; (channel < CHANNEL_COUNT) && (status == BENTO_OK);
         channel++) {
        status = set_channel(board, channel, on);
    }
    if (status == BENTO_OK) {
        (void)printf("All LEDs: %s\n", on ? "ON" : "OFF");
    }
    return status;
}

/* True when the event is of this kind: the comparison has one type
 * on both sides. */
static bool is_kind(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

static bento_status_t handle_event(dashboard_t *board,
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

/* Reads the touch events every LOOP_MS until RUN_MS has passed or
 * Stop is pressed. */
static bento_status_t run_event_loop(dashboard_t *board)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        /* ui.poll() answers no events when it fails, so a failed
         * poll is not an error here. */
        if (bento_ui_poll(board->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                          &count, NULL) != BENTO_OK) {
            count = 0U;
        }
        for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
            status = handle_event(board, &events[i]);
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
    board.hw_red = LED_RED;
    board.hw_green = LED_GREEN;
    board.hw_blue = LED_BLUE;
    /* `import rgbmatrix` raises ImportError on a board without it */
    status =
        bento_rgbmatrix_available() ? BENTO_OK : BENTO_E_UNSUPPORTED;
    if (status == BENTO_OK) {
        status = check_leds();
    }
    if (status == BENTO_OK) {
        status = build_screen(&board);
    }
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print("ex05: four virtual GPIO channels - "
                              "switches + All ON / All OFF");
    }
    if (status == BENTO_OK) {
        status = run_event_loop(&board);
    }
    if (status == BENTO_OK) {
        (void)printf("ex05_gpio_dashboard: done\n");
        status = show_end_banner(board.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
