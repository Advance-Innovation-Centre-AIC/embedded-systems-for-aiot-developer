/*
 * ex11_capsense_hardware.c -- อ่าน CAPSENSE จริงแล้วขับไฟ LED จริง (Section 2, Eva Kit)
 *
 * หน้าจอแบบเดียวกับ ex10 พร้อมบรรทัดสถานะการเชื่อมต่อ I2C อ่าน CAPSENSE จริง
 * ทุก 50 ms: แตะ BTN0 ไฟแดงจริงติด แตะ BTN1 ไฟเขียวจริงติด เลื่อน slider
 * แล้วไฟน้ำเงินจริงหรี่ตาม ถ้า CAPSENSE ไม่ตอบ หน้าจอบอกไว้และไม่อ่านอีก
 * ทำงาน 2 นาทีแล้วจบ และวาดแถบ "ตัวอย่างจบแล้ว กดปุ่มไม่ได้" ทับแถวล่าง
 *
 * The same screen as ex10, with a line saying whether the I2C CAPSENSE
 * answers. The real CAPSENSE is read every 50 ms: BTN0 lights the Eva
 * Kit's real red LED, BTN1 the real green LED, and the slider dims the
 * real blue LED. When CAPSENSE does not answer, the screen says so and
 * it is not read. The example runs for two minutes, then draws a bar
 * over the bottom rows saying it has ended.
 *
 * Twin of ex11_capsense_hardware.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/sec2_ui_to_hw/eva/ex11_capsense_hardware.py),
 * written into one clean file from the library's C twin of the Example
 * Explorer program aiot-lvgl-part1-ex11-capsense-hardware (similarity
 * 0.83) and the part1 twin of this course. Style: CLEAN_C.md. Where C
 * differs: the probe sensors.capsense.read() that may raise is a status
 * other than BENTO_OK, and the dict it answers is three out values; prev
 * = (None, None) is a "known" flag beside each pad's state. Like the
 * original, the real LEDs are left as they are when the example ends.
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

#include "bento_sensors.h"

/* Screen layout on the 792 x 398 canvas. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    BIG_FONT_PX = 16,
    TITLE_Y = 7,
    CONNECTION_Y = 25,
    SLIDER_BOX_X = 186, /* a 420 x 66 panel: CX - 210 */
    SLIDER_BOX_Y = 46,
    SLIDER_BOX_W = 420,
    SLIDER_BOX_H = 66,
    SLIDER_NAME_DX = 10,
    SLIDER_NAME_DY = 6,
    SLIDER_VALUE_DX = 330,
    SLIDER_VALUE_DY = 4,
    SLIDER_DX = 10,
    SLIDER_DY = 34,
    SLIDER_W = 340,
    SLIDER_H = 21,
    OUT_LED_DX = 375,
    OUT_LED_DY = 30,
    OUT_LED_SIZE = 25,
    PAD_DX = 110, /* the pads, left and right of the centre */
    PAD_Y = 227,
    PAD_W = 140,
    PAD_H = 125,
    PAD_RADIUS_PX = 10,
    PAD_BORDER_PX = 3,
    PAD_NAME_HALF_W = 14,
    PAD_NAME_DY = 8,
    PAD_PIN_HALF_W = 18,
    PAD_PIN_DY = 28,
    PAD_LED_DX = 45,
    PAD_LED_DY = 48,
    PAD_LED_SIZE = 50,
    PAD_STATE_HALF_W = 17,
    PAD_STATE_DY = 104,
    FOOTER_Y = 380
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x1A1A2E,
    COLOR_SLIDER_BOX = 0x0F0F23,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_HINT = 0xAAAAAA,
    COLOR_FOOTER = 0x666666,
    COLOR_CONNECTED = 0x00FF00,
    COLOR_NOT_CONNECTED = 0xFF4444,
    COLOR_PAD = 0x333355,
    COLOR_PAD_EDGE = 0x666699,
    COLOR_IDLE = 0x888888,
    COLOR_TOUCHED = 0x00FF00,
    COLOR_LIGHT_BLUE = 0x00AAFF,
    COLOR_RED = 0xF44336, /* LVGL's palette */
    COLOR_GREEN = 0x4CAF50
};

/* The two pads and the slider's range. */
enum {
    PAD_COUNT = 2,
    PERCENT_MAX = 100,
    LEVEL_MAX = 255, /* PROP_LED_BRIGHTNESS range */
    NO_VALUE = -1    /* prev_s before the first read */
};

/* The Eva Kit's three separate LEDs, by pin: red P16.7, green P16.6,
 * blue P16.5 (the firmware's name table calls LED 2 "RGB_RED" although
 * it is blue; fixed numbers avoid that trap). */
enum {
    LED_RED = 0,
    LED_GREEN = 1,
    LED_BLUE = 2,
    LED_HIGHEST = 2 /* gpio.led() of each is made, so this one must exist */
};

/* The "example has ended" bar over the bottom rows. */
enum {
    BANNER_Y = 330,
    BANNER_H = 36,
    BANNER_BORDER_PX = 1,
    BANNER_TEXT_X = 232,
    BANNER_TEXT_Y = 338,
    BANNER_FONT_PX = 16,
    COLOR_BANNER = 0x1A1A2E,
    COLOR_ENDED = 0xFF6600 /* the banner's border and text */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 50,    /* read CAPSENSE this often */
    RUN_MS = 120000  /* the example ends after two minutes */
};

/* "n%" for any int32_t, with its NUL. */
enum {
    PCT_CHARS = 16
};

/* One pad's widgets, the real LED it drives and the last state
 * shown. */
typedef struct {
    bento_ui_id_t led;
    bento_ui_id_t state;
    uint32_t hw_led;
    bool touched;
    bool known;
} pad_t;

/* The widgets the loop changes, the real blue LED and the last
 * slider value shown. */
typedef struct {
    bento_ui_t *ui;
    bool has_capsense;
    pad_t pads[PAD_COUNT];
    uint32_t hw_blue;
    bento_ui_id_t slider_value;
    bento_ui_id_t slider;
    bento_ui_id_t out_led;
    int32_t prev_slider;
} capsense_screen_t;

/* Where a widget goes and how it looks; for an LED, font_px is its
 * value= (on 1, off 0). */
typedef struct {
    int32_t x;
    int32_t y;
    uint32_t color;
    int32_t font_px;
} place_t;

/* How one pad looks. */
typedef struct {
    int32_t center_dx;
    const char *name;
    const char *pin;
    uint32_t led_color;
} pad_look_t;

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
static int32_t centered_x(const char *text)
{
    return SCREEN_CENTER_X - (((utf8_length(text) * FONT_PX) / 2) / 2);
}

/* gpio.led(i) raises ValueError for an LED the board does not have:
 * every LED the original makes must exist. */
static bento_status_t check_leds(void)
{
    const uint32_t highest_led = LED_HIGHEST;

    return (highest_led < bento_gpio_num_leds()) ? BENTO_OK : BENTO_E_INVALID_ARGUMENT;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text, const place_t *place,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = px(place->x);
    args.y = px(place->y);
    args.color = place->color;
    args.font_px = place->font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_led(bento_ui_t *ui, const place_t *place, int32_t size,
                              bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = px(place->x);
    args.y = px(place->y);
    args.w = px(size);
    args.h = px(size);
    args.color = place->color;
    args.font_px = place->font_px;
    return bento_ui_led_create(ui, &args, out_id);
}

/* A panel: min= is its border colour (the plain panels repeat their
 * own colour, as the original does), max= its radius. */
static bento_status_t add_panel(bento_ui_t *ui, const int16_t place[4], uint32_t color,
                                bool is_pad)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;

    bento_ui_widget_args_init(&args);
    args.x = place[0];
    args.y = place[1];
    args.w = place[2];
    args.h = place[3];
    args.color = color;
    args.min_val = is_pad ? (int32_t)COLOR_PAD_EDGE : (int32_t)color;
    args.max_val = is_pad ? PAD_RADIUS_PX : 0;
    args.font_px = is_pad ? PAD_BORDER_PX : 0;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* The background, the title and the connection line. */
static bento_status_t add_header(const capsense_screen_t *screen)
{
    const int16_t screen_place[4] = {0, 0, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                                     BENTO_RUNTIME_SCREEN_HEIGHT_PX};
    const char *title = "Part 1 Ex11: CAPSENSE Hardware";
    const char *connection =
        screen->has_capsense ? "I2C: Connected (addr 0x08)" : "I2C: CAPSENSE not responding";
    const place_t title_place = {centered_x(title), TITLE_Y, COLOR_TEXT, FONT_PX};
    const place_t connection_place = {
        centered_x(connection), CONNECTION_Y,
        screen->has_capsense ? (uint32_t)COLOR_CONNECTED : (uint32_t)COLOR_NOT_CONNECTED,
        FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status = add_panel(screen->ui, screen_place, COLOR_BACKGROUND, false);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, title, &title_place, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, connection, &connection_place, &unused);
    }
    return status;
}

/* The slider panel: its name, the percentage, the read-only slider
 * and the small blue LED. */
static bento_status_t add_slider_box(capsense_screen_t *screen)
{
    const int16_t box_place[4] = {SLIDER_BOX_X, SLIDER_BOX_Y, SLIDER_BOX_W, SLIDER_BOX_H};
    const place_t name_place = {SLIDER_BOX_X + SLIDER_NAME_DX, SLIDER_BOX_Y + SLIDER_NAME_DY,
                                COLOR_TEXT, FONT_PX};
    const place_t value_place = {SLIDER_BOX_X + SLIDER_VALUE_DX, SLIDER_BOX_Y + SLIDER_VALUE_DY,
                                 COLOR_TEXT, BIG_FONT_PX};
    const place_t led_place = {SLIDER_BOX_X + OUT_LED_DX, SLIDER_BOX_Y + OUT_LED_DY,
                               COLOR_LIGHT_BLUE, 0};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_panel(screen->ui, box_place, COLOR_SLIDER_BOX, false);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "SLIDER (CSS1)", &name_place, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "0%", &value_place, &screen->slider_value);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = SLIDER_BOX_X + SLIDER_DX;
        args.y = SLIDER_BOX_Y + SLIDER_DY;
        args.w = SLIDER_W;
        args.h = SLIDER_H;
        args.min_val = 0;
        args.max_val = PERCENT_MAX;
        args.font_px = 0;
        args.color = COLOR_LIGHT_BLUE;
        status = bento_ui_slider_create(screen->ui, &args, &screen->slider);
    }
    if (status == BENTO_OK) {
        status = add_led(screen->ui, &led_place, OUT_LED_SIZE, &screen->out_led);
    }
    return status;
}

/* One pad: the panel, its name, its pin, its LED and its state. */
static bento_status_t add_pad(capsense_screen_t *screen, int32_t pad)
{
    static const pad_look_t looks[PAD_COUNT] = {{-PAD_DX, "BTN0", "(CSB1)", COLOR_RED},
                                                {PAD_DX, "BTN1", "(CSB2)", COLOR_GREEN}};
    const pad_look_t *look = &looks[pad];
    const int32_t x = (SCREEN_CENTER_X + look->center_dx) - (PAD_W / 2);
    const int32_t middle_x = x + (PAD_W / 2);
    const int16_t pad_place[4] = {px(x), PAD_Y, PAD_W, PAD_H};
    const place_t name_place = {middle_x - PAD_NAME_HALF_W, PAD_Y + PAD_NAME_DY, COLOR_TEXT,
                                BIG_FONT_PX};
    const place_t pin_place = {middle_x - PAD_PIN_HALF_W, PAD_Y + PAD_PIN_DY, COLOR_HINT,
                               FONT_PX};
    const place_t led_place = {x + PAD_LED_DX, PAD_Y + PAD_LED_DY, look->led_color, 0};
    const place_t state_place = {middle_x - PAD_STATE_HALF_W, PAD_Y + PAD_STATE_DY, COLOR_IDLE,
                                 FONT_PX};
    pad_t *widgets = &screen->pads[pad];
    bento_ui_id_t unused;
    bento_status_t status = add_panel(screen->ui, pad_place, COLOR_PAD, true);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, look->name, &name_place, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, look->pin, &pin_place, &unused);
    }
    if (status == BENTO_OK) {
        status = add_led(screen->ui, &led_place, PAD_LED_SIZE, &widgets->led);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "Ready", &state_place, &widgets->state);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(capsense_screen_t *screen)
{
    const char *footer = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";
    const place_t footer_place = {centered_x(footer), FOOTER_Y, COLOR_FOOTER, FONT_PX};
    bento_ui_id_t unused;
    int32_t pad;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX, BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_header(screen);
    }
    if (status == BENTO_OK) {
        status = add_slider_box(screen);
    }
    for (pad = 0; (pad < PAD_COUNT) && (status == BENTO_OK); pad++) {
        status = add_pad(screen, pad);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, footer, &footer_place, &unused);
    }
    return status;
}

/* A pad whose state changed: its LED, state, real LED and a line. */
static bento_status_t show_pad(capsense_screen_t *screen, int32_t pad, bool touched)
{
    pad_t *widgets = &screen->pads[pad];
    const char *text = touched ? "TOUCHED" : "Ready";
    bento_status_t status = BENTO_OK;

    if (!widgets->known || (widgets->touched != touched)) {
        /* MicroPython's widget setters ignore a refusal (modui.c), so
         * the twin does too. */
        (void)bento_ui_set_value(screen->ui, widgets->led, touched ? 1 : 0);
        (void)bento_ui_set_text(screen->ui, widgets->state, text, (uint32_t)strlen(text));
        (void)bento_ui_set_color(screen->ui, widgets->state, touched ? COLOR_TOUCHED : COLOR_IDLE);
        status =
            touched ? bento_gpio_led_on(widgets->hw_led) : bento_gpio_led_off(widgets->hw_led);
        if (status == BENTO_OK) {
            (void)printf("[HW] BTN%" PRId32 ": %s\n", pad, touched ? "TOUCHED" : "Released");
        }
    }
    widgets->touched = touched;
    widgets->known = true;
    return status;
}

/* A slider value that changed: the slider, its percentage, the small
 * LED and the real blue LED. Values are never negative, so C's
 * division is the floor division. */
static bento_status_t show_slider(capsense_screen_t *screen, int32_t level)
{
    char text[PCT_CHARS];
    bento_status_t status = BENTO_OK;

    if (level != screen->prev_slider) {
        screen->prev_slider = level;
        (void)bento_ui_set_value(screen->ui, screen->slider, level);
        (void)snprintf(text, sizeof(text), "%" PRId32 "%%", level);
        (void)bento_ui_set_text(screen->ui, screen->slider_value, text, (uint32_t)strlen(text));
        (void)bento_ui_set_value(screen->ui, screen->out_led, (level != 0) ? 1 : 0);
        (void)bento_ui_set_prop(screen->ui, screen->out_led, BENTO_UI_PROP_LED_BRIGHTNESS,
                                (level * LEVEL_MAX) / PERCENT_MAX);
        status = bento_gpio_led_brightness(screen->hw_blue, level);
    }
    return status;
}

/* One reading of CAPSENSE, when it answered at the start. */
static bento_status_t read_capsense(capsense_screen_t *screen)
{
    uint8_t button0 = 0U;
    uint8_t button1 = 0U;
    uint8_t slider = 0U;
    bento_status_t status = BENTO_OK;

    if (screen->has_capsense) {
        status = bento_sensors_capsense_read(&button0, &button1, &slider);
        if (status == BENTO_OK) {
            status = show_pad(screen, 0, button0 != 0U);
        }
        if (status == BENTO_OK) {
            status = show_pad(screen, 1, button1 != 0U);
        }
        if (status == BENTO_OK) {
            status = show_slider(screen, (int32_t)slider);
        }
    }
    return status;
}

/* Reads CAPSENSE every LOOP_MS until RUN_MS has passed or Stop is
 * pressed. */
static bento_status_t run_loop(capsense_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) < RUN_MS)) {
        /* No widget here answers a tap (the "< Menu" button is left
         * out), so the events are not read, and ui.poll() answers no
         * events when it fails. */
        (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count, NULL);
        status = read_capsense(screen);
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
    capsense_screen_t screen;
    uint8_t button0 = 0U;
    uint8_t button1 = 0U;
    uint8_t slider = 0U;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    screen.prev_slider = NO_VALUE;
    screen.pads[0].hw_led = LED_RED;
    screen.pads[1].hw_led = LED_GREEN;
    screen.hw_blue = LED_BLUE;
    status = check_leds();
    if (status == BENTO_OK) {
        /* the probe: any failure means "not responding", as the
         * original's except Exception */
        screen.has_capsense =
            (bento_sensors_capsense_read(&button0, &button1, &slider) == BENTO_OK);
        status = build_screen(&screen);
    }
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print("ex11: touch the CAPSENSE pads on the board - watch LEDs follow");
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("ex11_capsense_hardware: done\n");
        status = show_end_banner(screen.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
