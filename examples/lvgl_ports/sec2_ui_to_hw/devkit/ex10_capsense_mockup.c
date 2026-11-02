/*
 * ex10_capsense_mockup.c -- แผง CAPSENSE จำลองบนจอ (CapSense Mockup, Section 2, Dev Kit)
 *
 * หน้าจอจำลองแผง CAPSENSE: แผง slider ด้านบนกับแผงปุ่มสองใบด้านล่าง เริ่มที่โหมด
 * Auto Demo ซึ่งเดินแปดจังหวะทุก 500 ms เมื่อผู้ใช้แตะแผงหรือลาก slider เองก็เปลี่ยน
 * เป็นโหมด Manual ปุ่มคุมสีแดงและเขียวของ RGB LED บน SoM slider หรี่สีน้ำเงิน
 * ทำงาน 2 นาทีแล้วจบ และวาดแถบ "ตัวอย่างจบแล้ว กดปุ่มไม่ได้" ทับแถวล่าง
 *
 * A mockup of the CAPSENSE panel: a slider panel at the top and two
 * button pads below. It starts in Auto Demo, which walks eight steps
 * every 500 ms; touching a pad or moving the slider switches to Manual.
 * The pads drive the red and green of the Dev Kit's RGB LED on the SoM,
 * the slider dims its blue. The example runs for two minutes, then draws
 * a bar over the bottom rows saying it has ended.
 *
 * Twin of ex10_capsense_mockup.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/sec2_ui_to_hw/devkit/ex10_capsense_mockup.py),
 * written into one clean file from the library's C twin of the Example
 * Explorer program aiot-lvgl-part1-ex10-capsense-mockup (similarity
 * 0.84) and the part1 twin of this course. Style: CLEAN_C.md. Like the
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

/* Screen layout on the 792 x 398 canvas. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    BIG_FONT_PX = 16,
    TITLE_Y = 7,
    MODE_Y = 25,
    SLIDER_BOX_X = 186, /* a 420 x 66 panel: CX - 210 */
    SLIDER_BOX_Y = 71,
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
    PAD_Y = 202,
    PAD_W = 140,
    PAD_H = 125,
    PAD_RADIUS_PX = 10,
    PAD_BORDER_PX = 3,
    PAD_NAME_HALF_W = 14,
    PAD_NAME_Y = 210,
    PAD_PIN_HALF_W = 18,
    PAD_PIN_Y = 230,
    PAD_LED_DX = 45,
    PAD_LED_Y = 250,
    PAD_LED_SIZE = 50,
    PAD_STATE_HALF_W = 17,
    PAD_STATE_Y = 306,
    FOOTER_Y = 380
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x1A1A2E,
    COLOR_SLIDER_BOX = 0x0F0F23,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_MODE = 0x00FFFF,
    COLOR_HINT = 0xAAAAAA,
    COLOR_FOOTER = 0x666666,
    COLOR_PAD = 0x333355,
    COLOR_PAD_TOUCHED = 0x00AA00,
    COLOR_PAD_EDGE = 0x666699,
    COLOR_IDLE = 0x888888,
    COLOR_TOUCHED = 0x00FF00,
    COLOR_LIGHT_BLUE = 0x00AAFF,
    COLOR_RED = 0xF44336, /* LVGL's palette */
    COLOR_GREEN = 0x4CAF50
};

/* The two pads, the slider's range and the demo's eight steps. */
enum {
    PAD_COUNT = 2,
    PERCENT_MAX = 100,
    LEVEL_MAX = 255, /* PROP_LED_BRIGHTNESS range */
    SLIDER_START = 50,
    DEMO_STEPS = 8,
    DEMO_PRESS_STEPS = 4, /* steps 0..3 press and release the pads */
    DEMO_LEVEL_STEP = 33  /* steps 4..7 move the slider by this much */
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
    DEMO_MS = 500,   /* one demo step this often */
    RUN_MS = 120000  /* the example ends after two minutes */
};

/* "n%" for any int32_t, with its NUL. */
enum {
    PCT_CHARS = 16
};

/* One pad's widgets and the real LED it drives. */
typedef struct {
    bento_ui_id_t panel;
    bento_ui_id_t led;
    bento_ui_id_t state;
    uint32_t hw_led;
} pad_t;

/* The widgets the program changes, the real blue LED and the demo's
 * progress. */
typedef struct {
    bento_ui_t *ui;
    pad_t pads[PAD_COUNT];
    uint32_t hw_blue;
    bento_ui_id_t mode;
    bento_ui_id_t slider_value;
    bento_ui_id_t slider;
    bento_ui_id_t out_led;
    int32_t level;
    bool in_demo;
    int32_t step;
    uint32_t last_step_ms;
} mockup_t;

/* Where a label goes and how it looks. */
typedef struct {
    int32_t x;
    int32_t y;
    uint32_t color;
    int32_t font_px;
} label_place_t;

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

    return (highest_led < bento_gpio_num_leds())
               ? BENTO_OK
               : BENTO_E_INVALID_ARGUMENT;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const label_place_t *place,
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

static bento_status_t add_led(bento_ui_t *ui,
                              const label_place_t *place, int32_t size,
                              bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = px(place->x);
    args.y = px(place->y);
    args.w = px(size);
    args.h = px(size);
    args.color = place->color;
    args.font_px = place->font_px; /* value=: on (1) or off (0) */
    return bento_ui_led_create(ui, &args, out_id);
}

/* A plain panel: min= repeats the colour, as the original does. */
static bento_status_t
add_plain_panel(bento_ui_t *ui, const int16_t place[4], uint32_t color)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;

    bento_ui_widget_args_init(&args);
    args.x = place[0];
    args.y = place[1];
    args.w = place[2];
    args.h = place[3];
    args.color = color;
    args.min_val = (int32_t)color;
    args.max_val = 0;
    args.font_px = 0;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* The background, the title and the mode line. */
static bento_status_t add_header(mockup_t *mockup)
{
    const int16_t screen_place[4] = {0, 0,
                                     BENTO_RUNTIME_SCREEN_WIDTH_PX,
                                     BENTO_RUNTIME_SCREEN_HEIGHT_PX};
    const char *title = "Part 1 Ex10: CAPSENSE UI Mockup";
    const char *mode = "Mode: Auto Demo";
    const label_place_t title_place = {centered_x(title), TITLE_Y,
                                       COLOR_TEXT, FONT_PX};
    const label_place_t mode_place = {centered_x(mode), MODE_Y,
                                      COLOR_MODE, FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        add_plain_panel(mockup->ui, screen_place, COLOR_BACKGROUND);

    if (status == BENTO_OK) {
        status = add_label(mockup->ui, title, &title_place, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(mockup->ui, mode, &mode_place, &mockup->mode);
    }
    return status;
}

/* The slider panel: its name, the percentage, the slider and the
 * small blue LED. */
static bento_status_t add_slider_box(mockup_t *mockup)
{
    const int16_t box_place[4] = {SLIDER_BOX_X, SLIDER_BOX_Y,
                                  SLIDER_BOX_W, SLIDER_BOX_H};
    const label_place_t name_place = {SLIDER_BOX_X + SLIDER_NAME_DX,
                                      SLIDER_BOX_Y + SLIDER_NAME_DY,
                                      COLOR_TEXT, FONT_PX};
    const label_place_t value_place = {SLIDER_BOX_X + SLIDER_VALUE_DX,
                                       SLIDER_BOX_Y + SLIDER_VALUE_DY,
                                       COLOR_TEXT, BIG_FONT_PX};
    const label_place_t led_place = {SLIDER_BOX_X + OUT_LED_DX,
                                     SLIDER_BOX_Y + OUT_LED_DY,
                                     COLOR_LIGHT_BLUE, 1};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_plain_panel(mockup->ui, box_place, COLOR_SLIDER_BOX);

    if (status == BENTO_OK) {
        status = add_label(mockup->ui, "SLIDER (CSS1)", &name_place,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(mockup->ui, "50%", &value_place,
                           &mockup->slider_value);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = SLIDER_BOX_X + SLIDER_DX;
        args.y = SLIDER_BOX_Y + SLIDER_DY;
        args.w = SLIDER_W;
        args.h = SLIDER_H;
        args.min_val = 0;
        args.max_val = PERCENT_MAX;
        args.font_px = SLIDER_START;
        args.color = COLOR_LIGHT_BLUE;
        status =
            bento_ui_slider_create(mockup->ui, &args, &mockup->slider);
    }
    if (status == BENTO_OK) {
        status = add_led(mockup->ui, &led_place, OUT_LED_SIZE,
                         &mockup->out_led);
    }
    return status;
}

/* A pad's panel, which reports touches. */
static bento_status_t add_pad_panel(bento_ui_t *ui, int32_t x,
                                    bento_ui_id_t *out_id)
{
    const uint32_t touch_events =
        BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_PRESSED) |
        BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_RELEASED);
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = px(x);
    args.y = PAD_Y;
    args.w = PAD_W;
    args.h = PAD_H;
    args.color = COLOR_PAD;
    /* a panel's min= is its border colour, max= its radius */
    args.min_val = COLOR_PAD_EDGE;
    args.max_val = PAD_RADIUS_PX;
    args.font_px = PAD_BORDER_PX;
    status = bento_ui_panel_create(ui, &args, out_id);
    if (status == BENTO_OK) {
        /* MicroPython sends the mask and ignores the answer (modui.c) */
        (void)bento_ui_listen_compat(ui, *out_id, touch_events);
    }
    return status;
}

/* How one pad looks. */
typedef struct {
    int32_t center_dx;
    const char *name;
    const char *pin;
    uint32_t led_color;
} pad_look_t;

/* One pad: the panel, its name, its pin, its LED and its state. */
static bento_status_t add_pad(mockup_t *mockup, int32_t pad)
{
    static const pad_look_t looks[PAD_COUNT] = {
        {-PAD_DX, "BTN0", "(CSB1)", COLOR_RED},
        {PAD_DX, "BTN1", "(CSB2)", COLOR_GREEN}};
    const pad_look_t *look = &looks[pad];
    const int32_t x = (SCREEN_CENTER_X + look->center_dx) - (PAD_W / 2);
    const int32_t middle_x = x + (PAD_W / 2);
    const label_place_t name_place = {middle_x - PAD_NAME_HALF_W,
                                      PAD_NAME_Y, COLOR_TEXT,
                                      BIG_FONT_PX};
    const label_place_t pin_place = {middle_x - PAD_PIN_HALF_W,
                                     PAD_PIN_Y, COLOR_HINT, FONT_PX};
    const label_place_t led_place = {x + PAD_LED_DX, PAD_LED_Y,
                                     look->led_color, 0};
    const label_place_t state_place = {
        middle_x - PAD_STATE_HALF_W, PAD_STATE_Y, COLOR_IDLE, FONT_PX};
    pad_t *widgets = &mockup->pads[pad];
    bento_ui_id_t unused;
    bento_status_t status =
        add_pad_panel(mockup->ui, x, &widgets->panel);

    if (status == BENTO_OK) {
        status =
            add_label(mockup->ui, look->name, &name_place, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(mockup->ui, look->pin, &pin_place, &unused);
    }
    if (status == BENTO_OK) {
        status = add_led(mockup->ui, &led_place, PAD_LED_SIZE,
                         &widgets->led);
    }
    if (status == BENTO_OK) {
        status = add_label(mockup->ui, "Ready", &state_place,
                           &widgets->state);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(mockup_t *mockup)
{
    const char *footer =
        "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";
    const label_place_t footer_place = {centered_x(footer), FOOTER_Y,
                                        COLOR_FOOTER, FONT_PX};
    bento_ui_id_t unused;
    int32_t pad;
    bento_status_t status =
        bento_ui_screen(mockup->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_header(mockup);
    }
    if (status == BENTO_OK) {
        status = add_slider_box(mockup);
    }
    for (pad = 0; (pad < PAD_COUNT) && (status == BENTO_OK); pad++) {
        status = add_pad(mockup, pad);
    }
    if (status == BENTO_OK) {
        status = add_label(mockup->ui, footer, &footer_place, &unused);
    }
    return status;
}

/* A pad touched or let go: its real LED, panel, LED and state. */
static bento_status_t press(const mockup_t *mockup, int32_t pad,
                            bool touched)
{
    const pad_t *widgets = &mockup->pads[pad];
    const char *text = touched ? "TOUCHED" : "Ready";
    const bento_status_t status =
        touched ? bento_gpio_led_on(widgets->hw_led)
                : bento_gpio_led_off(widgets->hw_led);

    if (status == BENTO_OK) {
        /* MicroPython's widget setters ignore a refusal (modui.c), so
         * the twin does too. */
        (void)bento_ui_set_color(mockup->ui, widgets->panel,
                                 touched ? COLOR_PAD_TOUCHED
                                         : COLOR_PAD);
        (void)bento_ui_set_value(mockup->ui, widgets->led,
                                 touched ? 1 : 0);
        (void)bento_ui_set_text(mockup->ui, widgets->state, text,
                                (uint32_t)strlen(text));
        (void)bento_ui_set_color(mockup->ui, widgets->state,
                                 touched ? COLOR_TOUCHED : COLOR_IDLE);
    }
    return status;
}

/* A new slider level: the real blue LED, the percentage and the small
 * LED's brightness. Levels are never negative, so C's division is the
 * floor division. */
static bento_status_t slide(mockup_t *mockup, int32_t level)
{
    char text[PCT_CHARS];
    const bento_status_t status = bento_gpio_led_brightness(
        mockup->hw_blue, (level >= PERCENT_MAX) ? PERCENT_MAX : level);

    mockup->level = level;
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%" PRId32 "%%", level);
        (void)bento_ui_set_text(mockup->ui, mockup->slider_value, text,
                                (uint32_t)strlen(text));
        (void)bento_ui_set_value(mockup->ui, mockup->out_led, 1);
        (void)bento_ui_set_prop(mockup->ui, mockup->out_led,
                                BENTO_UI_PROP_LED_BRIGHTNESS,
                                (level * LEVEL_MAX) / PERCENT_MAX);
    }
    return status;
}

/* The first touch ends the demo for good. */
static void go_manual(mockup_t *mockup)
{
    const char *text = "Mode: Manual";

    if (mockup->in_demo) {
        mockup->in_demo = false;
        (void)bento_ui_set_text(mockup->ui, mockup->mode, text,
                                (uint32_t)strlen(text));
    }
}

/* One demo step: steps 0..3 press and release BTN0 then BTN1, steps
 * 4..7 move the slider to 0, 33, 66 and 99. */
static bento_status_t demo_tick(mockup_t *mockup)
{
    const int32_t step = mockup->step % DEMO_STEPS;
    bento_status_t status;

    if (step < DEMO_PRESS_STEPS) {
        status = press(mockup, step / 2, (step % 2) == 0);
    } else {
        int32_t level = (step - DEMO_PRESS_STEPS) * DEMO_LEVEL_STEP;

        if (level > PERCENT_MAX) {
            level = PERCENT_MAX;
        }
        (void)bento_ui_set_value(mockup->ui, mockup->slider, level);
        status = slide(mockup, level);
    }
    mockup->step++;
    return status;
}

/* The pad whose panel this is, or PAD_COUNT when it is no pad's. */
static int32_t pad_of_panel(const mockup_t *mockup,
                            bento_ui_id_t widget)
{
    int32_t found = PAD_COUNT;
    int32_t pad;

    for (pad = PAD_COUNT - 1; pad >= 0; pad--) {
        if (mockup->pads[pad].panel == widget) {
            found = pad;
        }
    }
    return found;
}

static bento_status_t handle_event(mockup_t *mockup,
                                   const bento_ui_event_t *event)
{
    const int32_t pad = pad_of_panel(mockup, event->id);
    const bool touched = (event->type == BENTO_UI_EVENT_PRESSED);
    bento_status_t status = BENTO_OK;

    if ((pad < PAD_COUNT) &&
        (touched || (event->type == BENTO_UI_EVENT_RELEASED))) {
        go_manual(mockup);
        status = press(mockup, pad, touched);
    } else if ((event->id == mockup->slider) &&
               (event->type == BENTO_UI_EVENT_VALUE_CHANGED)) {
        go_manual(mockup);
        status = slide(mockup, event->value);
    } else {
        /* any other event changes nothing */
    }
    return status;
}

/* One pass of the loop: the touch events, a demo step when one is
 * due, and a fresh burst for a dimmed blue LED (no hardware PWM). */
static bento_status_t run_pass(mockup_t *mockup)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    /* ui.poll() answers no events when it fails, so a failed poll is
     * not an error here. */
    if (bento_ui_poll(mockup->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = handle_event(mockup, &events[i]);
    }
    if ((status == BENTO_OK) && mockup->in_demo &&
        (bento_time_ticks_diff(bento_time_ticks_ms(),
                               mockup->last_step_ms) >= DEMO_MS)) {
        mockup->last_step_ms = bento_time_ticks_ms();
        status = demo_tick(mockup);
    }
    if ((status == BENTO_OK) && (mockup->level > 0) &&
        (mockup->level < PERCENT_MAX)) {
        status =
            bento_gpio_led_brightness(mockup->hw_blue, mockup->level);
    }
    return status;
}

/* Runs a pass every LOOP_MS until RUN_MS has passed or Stop is
 * pressed. */
static bento_status_t run_event_loop(mockup_t *mockup)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = run_pass(mockup);
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
    mockup_t mockup;
    bento_status_t status;

    (void)memset(&mockup, 0, sizeof(mockup));
    mockup.ui = bento_runtime_get_ui();
    mockup.level = SLIDER_START;
    mockup.pads[0].hw_led = LED_RED;
    mockup.pads[1].hw_led = LED_GREEN;
    mockup.hw_blue = LED_BLUE;
    status = check_leds();
    if (status == BENTO_OK) {
        status = build_screen(&mockup);
    }
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print("ex10: auto demo walks the panel - touch "
                              "anything to go Manual");
        mockup.in_demo = true;
        mockup.step = 0;
        mockup.last_step_ms = bento_time_ticks_ms();
        status = run_event_loop(&mockup);
    }
    if (status == BENTO_OK) {
        (void)printf("ex10_capsense_mockup: done\n");
        status = show_end_banner(mockup.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
