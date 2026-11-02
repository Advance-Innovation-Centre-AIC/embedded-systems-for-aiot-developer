/*
 * ex03_led_control.c -- ไฟ LED บนจอ ปุ่ม ON/OFF และ slider ความสว่าง (Section 2, Dev Kit)
 *
 * LED เขียวบนจอ ปุ่ม ON/OFF และ slider 0-255 ปรับความสว่าง ไฟเขียวจริงของ
 * RGB LED บน SoM (LED 4) หรี่ตามด้วย brightness() ซ้ำทุกรอบ เพราะไม่มี PWM
 * ฮาร์ดแวร์ ทุก event พิมพ์ลง console ทำงาน 2 นาทีแล้วจบ และวาดแถบ
 * "ตัวอย่างจบแล้ว กดปุ่มไม่ได้" ทับแถวล่าง
 *
 * A green LED widget, ON / OFF buttons and a 0-255 slider set its
 * brightness. The green of the Dev Kit's RGB LED on the SoM (LED 4)
 * follows through brightness(), repeated every loop while dimmed because
 * there is no hardware PWM. Every event is printed. The example runs for
 * two minutes, then draws a bar over the bottom rows saying it has
 * ended.
 *
 * Twin of ex03_led_control.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/sec2_ui_to_hw/devkit/ex03_led_control.py),
 * written into one clean file from the library's C twin of the Example
 * Explorer program aiot-lvgl-sec2-devkit-ex03-led-control (similarity
 * 1.0) and the part1 twin of this course. Style: CLEAN_C.md. Where C
 * differs: C has no dict, so print("EV:", ev) is written out in the key
 * order the MicroPython event dict prints (type, value, handle). Like
 * the original, the real LEDs are left as they are when the example
 * ends. Left out on purpose: the "< Menu" button, which the original
 * shows only when the board's own menu starts it (MENU_MODE); a C
 * program is never started from that menu.
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
    LED_SIZE = 80,
    LED_Y = 101, /* C's CENTER(0,-70): 199 - 58 - 40 */
    BRIGHT_Y = 191,
    BUTTON_W = 90,
    BUTTON_H = 48,
    BUTTON_Y = 217,
    BUTTON_DX = 60, /* C's CENTER(-60,+50) and (+60,+50) */
    SLIDER_W = 200,
    SLIDER_H = 22,
    SLIDER_Y = 282, /* C's CENTER(0,+110) */
    PCT_Y = 306,
    HINT_Y = 348,
    FOOTER_Y = 374
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x0F0F23,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_HINT = 0xAAAAAA,
    COLOR_FOOTER = 0x666666,
    COLOR_GREEN = 0x4CAF50, /* LVGL's palette green */
    COLOR_RED = 0xF44336    /* LVGL's palette red */
};

/* Brightness: the slider and the LED widget take 0..255, the real LED
 * a percentage. */
enum {
    LEVEL_MAX = 255,
    LEVEL_START = 150,
    PCT_FULL = 100
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

/* "Brightness: " and any int32_t, with its NUL. */
enum {
    TEXT_CHARS = 24
};

/* The LEDs and widgets the event loop changes, and the real LED's
 * percentage. */
typedef struct {
    bento_ui_t *ui;
    uint32_t hw_green;
    bento_ui_id_t led;
    bento_ui_id_t bright;
    bento_ui_id_t pct;
    bento_ui_id_t on_button;
    bento_ui_id_t off_button;
    bento_ui_id_t slider;
    int32_t cur_pct;
} led_screen_t;

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
    return (int16_t)(SCREEN_CENTER_X -
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

/* True when the event is of this kind and came from this widget. */
static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

static void set_text(const led_screen_t *screen, bento_ui_id_t widget,
                     const char *text)
{
    /* MicroPython's widget setters ignore a refusal (modui.c), so the
     * twin does too. */
    (void)bento_ui_set_text(screen->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void set_brightness(const led_screen_t *screen, int32_t level)
{
    /* a setter's refusal was ignored before too */
    (void)bento_ui_set_prop(screen->ui, screen->led,
                            BENTO_UI_PROP_LED_BRIGHTNESS, level);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = centered_x(text);
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

static bento_status_t add_led(led_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = SCREEN_CENTER_X - (LED_SIZE / 2);
    args.y = LED_Y;
    args.w = LED_SIZE;
    args.h = LED_SIZE;
    args.color = COLOR_GREEN;
    args.font_px = 1; /* value=1: the LED starts on */
    status = bento_ui_led_create(screen->ui, &args, &screen->led);
    if (status == BENTO_OK) {
        set_brightness(screen, LEVEL_START);
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

static bento_status_t add_slider(led_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = SCREEN_CENTER_X - (SLIDER_W / 2);
    args.y = SLIDER_Y;
    args.w = SLIDER_W;
    args.h = SLIDER_H;
    args.min_val = 0;
    args.max_val = LEVEL_MAX;
    args.font_px = LEVEL_START;
    return bento_ui_slider_create(screen->ui, &args, &screen->slider);
}

/* Background, title, the LED widget and its brightness line. */
static bento_status_t build_top(led_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    bento_ui_id_t unused;
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
        status = add_label(ui, "Part 1 - Example 3: LED Widget",
                           TITLE_Y, COLOR_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_led(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "Brightness: 150", BRIGHT_Y, COLOR_TEXT,
                           &screen->bright);
    }
    return status;
}

/* ON and OFF, the slider, its percentage, the hint and the footer. */
static bento_status_t build_controls(led_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    bento_ui_id_t unused;
    bento_status_t status = add_button(
        ui, "ON", SCREEN_CENTER_X - BUTTON_DX - (BUTTON_W / 2),
        COLOR_GREEN, &screen->on_button);

    if (status == BENTO_OK) {
        status = add_button(
            ui, "OFF", SCREEN_CENTER_X + BUTTON_DX - (BUTTON_W / 2),
            COLOR_RED, &screen->off_button);
    }
    if (status == BENTO_OK) {
        status = add_slider(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "59%", PCT_Y, COLOR_TEXT, &screen->pct);
    }
    if (status == BENTO_OK) {
        status = add_label(ui,
                           "Learning: lv_led_create, lv_led_on/off, "
                           "lv_led_set_brightness",
                           HINT_Y, COLOR_HINT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui,
                           "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                           "Burapha University",
                           FOOTER_Y, COLOR_FOOTER, &unused);
    }
    return status;
}

/* The event as MicroPython prints its dict. */
static void print_event(const bento_ui_event_t *event)
{
    /* modui.c's type names, indexed by UI_EVENT_* */
    static const char *const names[] = {
        "unknown",    "clicked",      "value_changed",
        "toggled",    "pressed",      "released",
        "press_lost", "long_pressed", "long_pressed_repeat",
        "ready",      "cancel",       "focused",
        "defocused",  "scroll_begin", "scroll_end",
        "gesture"};
    const char *name = names[0];

    if (event->type <= BENTO_UI_EVENT_GESTURE) {
        name = names[event->type];
    }
    (void)printf("EV: {'type': '%s', 'value': %" PRId32
                 ", 'handle': %" PRIu32 "}\n",
                 name, event->value, bento_ui_id_to_handle(event->id));
}

/* ON or OFF: the widget and the real LED go to full or dark. */
static bento_status_t switch_led(led_screen_t *screen, bool on)
{
    const int32_t pct = on ? PCT_FULL : 0;

    set_brightness(screen, on ? LEVEL_MAX : 0);
    set_text(screen, screen->bright,
             on ? "Brightness: 255 (ON)" : "Brightness: 0 (OFF)");
    screen->cur_pct = pct;
    return bento_gpio_led_brightness(screen->hw_green, pct);
}

/* The slider moved: the widget, both labels and the percentage the
 * loop keeps the real LED at. Levels are never negative, so C's
 * division is the floor division. */
static void move_slider(led_screen_t *screen, int32_t level)
{
    char text[TEXT_CHARS];
    const int32_t pct = (level * PCT_FULL) / LEVEL_MAX;

    set_brightness(screen, level);
    (void)snprintf(text, sizeof(text), "Brightness: %" PRId32, level);
    set_text(screen, screen->bright, text);
    (void)snprintf(text, sizeof(text), "%" PRId32 "%%", pct);
    set_text(screen, screen->pct, text);
    screen->cur_pct = pct;
}

static bento_status_t handle_event(led_screen_t *screen,
                                   const bento_ui_event_t *event)
{
    bento_status_t status = BENTO_OK;

    print_event(event);
    if (event_is(event, BENTO_UI_EVENT_CLICKED, screen->on_button)) {
        status = switch_led(screen, true);
    } else if (event_is(event, BENTO_UI_EVENT_CLICKED,
                        screen->off_button)) {
        status = switch_led(screen, false);
    } else if (event_is(event, BENTO_UI_EVENT_VALUE_CHANGED,
                        screen->slider)) {
        move_slider(screen, event->value);
    } else {
        /* another widget or another kind of event: printed only */
    }
    return status;
}

static bento_status_t handle_events(led_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    /* ui.poll() answers no events when it fails, so a failed poll is
     * not an error here. */
    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = handle_event(screen, &events[i]);
    }
    /* no hardware PWM: a dimmed LED needs a fresh burst every loop */
    if ((status == BENTO_OK) && (screen->cur_pct > 0) &&
        (screen->cur_pct < PCT_FULL)) {
        status = bento_gpio_led_brightness(screen->hw_green,
                                           screen->cur_pct);
    }
    return status;
}

/* Reads the touch events every LOOP_MS until RUN_MS has passed or
 * Stop is pressed. */
static bento_status_t run_event_loop(led_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = handle_events(screen);
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
    led_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    screen.cur_pct = (LEVEL_START * PCT_FULL) / LEVEL_MAX;
    screen.hw_green = LED_GREEN;
    status = check_leds();
    if (status == BENTO_OK) {
        status = build_top(&screen);
    }
    if (status == BENTO_OK) {
        status = build_controls(&screen);
    }
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print("ex03: ON/OFF buttons + slider drive the "
                              "LED widget brightness");
    }
    if (status == BENTO_OK) {
        status = run_event_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("ex03_led_control: done\n");
        status = show_end_banner(screen.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
