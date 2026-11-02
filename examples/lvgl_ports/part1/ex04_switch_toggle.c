/*
 * ex04_switch_toggle.c -- สวิตช์บนจอคุมไฟ (Switch Toggle, Part 1)
 *
 * แตะสวิตช์บนจอ แล้วไฟ LED จำลองบนจอติดหรือดับตาม พร้อมกันนั้นไฟแดงกับไฟเขียวจริง
 * บนบอร์ด (หาจากชื่อใน gpio.board_info()) ก็ติดคู่กัน เป็นสีเหลืองบน RGB ดวงเดียว
 * ตัวอย่างทำงาน 2 นาทีแล้วจบ
 *
 * Tap the switch on the screen: the virtual LED follows it, and the
 * board's real red and green LEDs (found by name in gpio.board_info())
 * light together, yellow on a single RGB LED. The example runs for two
 * minutes, then ends.
 *
 * Twin of ex04_switch_toggle.py (embedded-systems-for-aiot-developer/
 * examples/lvgl_ports/part1/ex04_switch_toggle.py), written into one
 * file from the library's C twin of the Example Explorer program
 * aiot-lvgl-part1-ex04-switch-toggle (the same code), in the style of
 * the reference aiot-lvgl-sec2-eva-ex04-switch-toggle. Style:
 * CLEAN_C.md. Where C differs: a fallback LED the board lacks returns
 * BENTO_E_INVALID_ARGUMENT where gpio.led() raises ValueError. Like the
 * original, the real LEDs are left as they are when the example ends.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu.
 */
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
    LED_Y = 119, /* C's CENTER(0,-60): 199 - 50 - 30 */
    LED_SIZE = 60,
    LED_CAPTION_Y = 189,
    SWITCH_Y = 212,
    SWITCH_W = 80,
    SWITCH_H = 40,
    STATUS_Y = 274,
    HINT_Y = 322,
    HINT2_Y = 340,
    FOOTER_Y = 374
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x16213E,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_STATUS = 0x00FF00,
    COLOR_HINT = 0xAAAAAA,
    COLOR_FOOTER = 0x666666,
    COLOR_LED = 0xFFEB3B /* LVGL's palette yellow */
};

/* The LED each name search falls back to. */
enum {
    FALLBACK_RED = 0,
    FALLBACK_GREEN = 1
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 50,    /* read the touch events this often */
    RUN_MS = 120000  /* the example ends after two minutes */
};

static const char TEXT_STATUS_OFF[] = "GPIO State: LOW (OFF)";

/* The real LEDs and the widgets the program changes after it has
 * drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    uint32_t hw_red;
    uint32_t hw_green;
    bento_ui_id_t led;
    bento_ui_id_t status;
    bento_ui_id_t toggle;
} switch_screen_t;

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

/* True when the event is of this kind and came from this widget. */
static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
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

static bento_status_t add_led(switch_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = SCREEN_CENTER_X - (LED_SIZE / 2);
    args.y = LED_Y;
    args.w = LED_SIZE;
    args.h = LED_SIZE;
    args.color = COLOR_LED;
    args.font_px = 0;
    return bento_ui_led_create(screen->ui, &args, &screen->led);
}

static bento_status_t add_switch(switch_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = SCREEN_CENTER_X - (SWITCH_W / 2);
    args.y = SWITCH_Y;
    args.w = SWITCH_W;
    args.h = SWITCH_H;
    return bento_ui_switch_create(screen->ui, &args, &screen->toggle);
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(switch_screen_t *screen)
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
        status = add_label(ui, "Part 1 - Example 4: Switch Control",
                           TITLE_Y, COLOR_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_led(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "Virtual LED", LED_CAPTION_Y, COLOR_TEXT,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_switch(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, TEXT_STATUS_OFF, STATUS_Y, COLOR_STATUS,
                           &screen->status);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "Learning: lv_switch_create, LV_STATE_CHECKED", HINT_Y,
            COLOR_HINT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui,
            "This switch would control a real GPIO in actual hardware",
            HINT2_Y, COLOR_HINT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui,
                           "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                           "Burapha University",
                           FOOTER_Y, COLOR_FOOTER, &unused);
    }
    return status;
}

/* Red and green together: yellow on a single RGB LED. */
static bento_status_t set_leds(const switch_screen_t *screen, bool on)
{
    bento_status_t status = on ? bento_gpio_led_on(screen->hw_red)
                               : bento_gpio_led_off(screen->hw_red);

    if (status == BENTO_OK) {
        status = on ? bento_gpio_led_on(screen->hw_green)
                    : bento_gpio_led_off(screen->hw_green);
    }
    return status;
}

/* The switch moved: the virtual LED, the real red and green LEDs and
 * the status line follow. */
static bento_status_t show_switch(const switch_screen_t *screen,
                                  bool on)
{
    const char *text = on ? "GPIO State: HIGH (ON)" : TEXT_STATUS_OFF;
    bento_status_t status;

    /* MicroPython's widget setters ignore a refusal (modui.c), so
     * the twin does too. */
    (void)bento_ui_set_value(screen->ui, screen->led, on ? 1 : 0);
    status = set_leds(screen, on);
    if (status == BENTO_OK) {
        (void)bento_ui_set_text(screen->ui, screen->status, text,
                                (uint32_t)strlen(text));
        (void)printf("Switch %s - GPIO would be %s\n",
                     on ? "ON" : "OFF", on ? "HIGH" : "LOW");
    }
    return status;
}

static bento_status_t handle_events(const switch_screen_t *screen)
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
        if (event_is(&events[i], BENTO_UI_EVENT_TOGGLED,
                     screen->toggle)) {
            status = show_switch(screen, events[i].value != 0);
        }
    }
    return status;
}

/* Reads the touch events every LOOP_MS until RUN_MS has passed or
 * Stop is pressed. */
static bento_status_t run_event_loop(const switch_screen_t *screen)
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

int main(void)
{
    switch_screen_t screen = {
        bento_runtime_get_ui(), 0U, 0U, 0U, 0U, 0U};
    bento_status_t status =
        find_led("RED", FALLBACK_RED, &screen.hw_red);

    if (status == BENTO_OK) {
        status = find_led("GREEN", FALLBACK_GREEN, &screen.hw_green);
    }
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print(
            "ex04: toggle the switch - virtual LED follows");
    }
    if (status == BENTO_OK) {
        status = run_event_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("ex04_switch_toggle: done\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
