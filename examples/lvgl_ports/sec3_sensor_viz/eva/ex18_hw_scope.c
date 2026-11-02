/*
 * ex18_hw_scope.c -- สโคปต่อฮาร์ดแวร์ (HW Scope, Eva Kit)
 *
 * เครื่องกำเนิดคลื่นสี่เหลี่ยมที่เส้นวิ่งตลอด ความถี่จากสไลเดอร์ (1-500 Hz)
 * และ duty จากลูกบิด POTEN บนบอร์ดจริง สวิตช์ OUTPUT ขับ LED น้ำเงินจริง
 * (P16.5) ด้วย PWM ให้หรี่ตาม duty และที่ความถี่ 5 Hz ลงมา LED
 * กะพริบตามจังหวะสัญญาณจริงที่ตามองเห็นได้ ทำงาน 3 นาทีแล้วจบ ดับ LED
 * และวาดแถบ "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * A square-wave generator whose trace always scrolls: the frequency
 * from the slider (1-500 Hz) and the duty from the board's real POTEN
 * knob. The OUTPUT switch drives the real blue LED (P16.5) with PWM,
 * dimmed to the duty; at 5 Hz and below the LED blinks with the signal
 * itself, slow enough to see. The program ends after three minutes,
 * turns the LED off, and then draws a "finished" band over the bottom
 * rows.
 *
 * C twin of ex18_hw_scope.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/eva/ex18_hw_scope.py), written new: the
 * library twin aiot-lvgl-sec3-eva-ex18-hw-scope follows an older
 * screen. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. What C changes: Python's int is int32_t
 * here; led_lit's None / True / False is two bools (blinking, lit);
 * Stop also turns the LED off on the way out, where the original's
 * KeyboardInterrupt skips its last two lines and leaves the LED as it
 * was; fmod() works in double and is rounded to the board's float
 * width.
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_gpio.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* Screen layout on the 792 x 398 canvas. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    TITLE_FONT_PX = 16,
    TITLE_Y = 8,
    CHART_X = 16,
    CHART_Y = 44,
    CHART_W = 594,
    CHART_H = 238,
    PANEL_X = 620,
    PANEL_W = 158,
    PANEL_EDGE_PX = 6,
    OUTPUT_X = 648,
    OUTPUT_Y = 56,
    SWITCH_X = 644,
    SWITCH_Y = 82,
    SWITCH_W = 80,
    SWITCH_H = 40,
    LED_X = 740,
    LED_Y = 88,
    LED_SIZE = 26,
    PANEL_TEXT_X = 636,
    DUTY_Y = 150,
    POT_NAME_Y = 176,
    RAW_Y = 196,
    FREQ_TEXT_X = 648,
    FREQ_TEXT_Y = 246,
    SLIDER_CAPTION_X = 16,
    SLIDER_CAPTION_Y = 300,
    SLIDER_X = 210,
    SLIDER_Y = 298,
    SLIDER_W = 330,
    SLIDER_H = 22,
    HINT_Y = 336,
    FOOTER_Y = 378
};

/* The "finished" band over the bottom rows. */
enum {
    END_Y = 330,
    END_W = 792,
    END_H = 36,
    END_TEXT_X = 232,
    END_TEXT_Y = 338,
    END_FONT_PX = 16
};

/* The knob, the LED and the generator. */
enum {
    POT_SHIFT = 4, /* the Eva Kit's pot is 0-65535: keep 12 bits */
    LED_BLUE =
        2, /* blue, P16.5 (the firmware's name table says RGB_RED) */
    ADC_MAX = 4095, /* the ADC's full scale */
    PERCENT = 100,
    DUTY_STEP = 2,          /* a smaller change of the knob is noise */
    SAMPLE_RATE_HZ = 10000, /* a low rate makes the duty easy to see */
    CHART_POINTS = 200,
    POINTS_PER_ROUND = 8,
    CHART_MAX = 100,
    SIGNAL_HIGH = 70,
    SIGNAL_LOW = 30,
    FREQ_MIN_HZ = 1,
    FREQ_MAX_HZ = 500,
    FREQ_START_HZ = 100,
    DUTY_START = 50,
    BLINK_MAX_HZ = 5, /* above about 5 Hz the eye sees a steady light */
    MS_PER_SECOND = 1000,
    LED_WIDGET_FULL = 255,
    LED_WIDGET_DIM = 80,
    TEXT_BYTES = 24 /* the longest: "Duty: ", an int32_t, "%" */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,
    RUN_MS = 180000 /* the example ends after three minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x0A0A0A,
    COLOR_TITLE = 0x00FF88,
    COLOR_TRACE = 0x00FF00,
    COLOR_PANEL = 0x1A1A1A,
    COLOR_PANEL_EDGE = 0x333333,
    COLOR_CAPTION = 0xCCCCCC,
    COLOR_LED = 0x2196F3,
    COLOR_DUTY = 0xFFFF00,
    COLOR_DIM_TEXT = 0x888888,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_FOOTER = 0x666666
};

/* The "finished" band's colours, 0xRRGGBB. */
enum {
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

/* The widgets the program changes, and the generator's state. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    bento_ui_id_t output_switch;
    bento_ui_id_t output_led;
    bento_ui_id_t duty_text;
    bento_ui_id_t raw_text;
    bento_ui_id_t freq_text;
    bento_ui_id_t slider;
    int32_t freq_hz;
    int32_t duty;
    bool output_on;
    bool blinking; /* false: PWM brightness follows the duty */
    bool lit;      /* while blinking: the LED is on now */
    int32_t phase; /* samples since the start */
} hw_scope_t;

/* A label's place and look: the original's x=, y=, color= and value=. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} label_style_t;

/* The knob as a duty percentage and its raw 0-4095: the Eva Kit's
 * single pot (P15.1) reads 0-65535, its top 12 bits give 0-4095. */
static bento_status_t read_pot(int32_t *duty, int32_t *raw)
{
    uint16_t value = 0U;
    const bento_status_t status = bento_sensors_pot_read(&value);

    if (status == BENTO_OK) {
        *raw = (int32_t)((uint32_t)value >> POT_SHIFT);
        *duty = (*raw * PERCENT) / ADC_MAX;
    }
    return status;
}

/* Python's len(): characters, not bytes, so a text is centred like
 * the original. */
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

/* x that centres a text: the original's len(text) * size, floor-divided
 * by 4, left of the centre. */
static int16_t centered_x(const char *text, int32_t font_px)
{
    return (int16_t)(SCREEN_CENTER_X -
                     (((utf8_length(text) * font_px) / 2) / 2));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const label_style_t *style,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = style->x;
    args.y = style->y;
    args.color = style->color;
    args.font_px = style->font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A label centred on the screen, as the original's cx() places it. */
static bento_status_t add_centered(bento_ui_t *ui, const char *text,
                                   int16_t y, uint32_t color,
                                   int32_t font_px,
                                   bento_ui_id_t *out_id)
{
    const label_style_t style = {centered_x(text, font_px), y, color,
                                 font_px};

    return add_label(ui, text, &style, out_id);
}

/* A fresh screen, then a pause to let it settle; Stop during the pause
 * ends the program. */
static bento_status_t clear_screen(bento_ui_t *ui)
{
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
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
    args.min_val = COLOR_BACKGROUND; /* min= repeats the colour */
    args.max_val = 0;
    args.font_px = 0;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* Widget arguments with a place, a size and a colour. */
static bento_ui_widget_args_t place_at(int16_t x, int16_t y,
                                       int16_t width, int16_t height,
                                       uint32_t color)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = x;
    args.y = y;
    args.w = width;
    args.h = height;
    args.color = color;
    return args;
}

/* The scope: a 200-point chart; .prop() never raises in MicroPython. */
static bento_status_t add_chart(hw_scope_t *scope)
{
    bento_ui_widget_args_t args =
        place_at(CHART_X, CHART_Y, CHART_W, CHART_H, COLOR_TRACE);
    bento_status_t status;

    args.min_val = 0;
    args.max_val = CHART_MAX;
    status = bento_ui_chart_create(scope->ui, &args, &scope->chart);
    if (status == BENTO_OK) {
        (void)bento_ui_set_prop(scope->ui, scope->chart,
                                BENTO_UI_PROP_CHART_POINTS,
                                CHART_POINTS);
    }
    return status;
}

/* The OUTPUT box: its panel, caption, switch and the LED widget. */
static bento_status_t add_output(hw_scope_t *scope)
{
    const label_style_t caption = {OUTPUT_X, OUTPUT_Y, COLOR_CAPTION,
                                   FONT_PX};
    bento_ui_widget_args_t args =
        place_at(PANEL_X, CHART_Y, PANEL_W, CHART_H, COLOR_PANEL);
    bento_ui_id_t unused;
    bento_status_t status;

    args.min_val =
        COLOR_PANEL_EDGE; /* a panel's min= is its edge colour */
    args.max_val = PANEL_EDGE_PX;
    args.font_px = 1;
    status = bento_ui_panel_create(scope->ui, &args, &unused);
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "OUTPUT", &caption, &unused);
    }
    if (status == BENTO_OK) {
        args = place_at(SWITCH_X, SWITCH_Y, SWITCH_W, SWITCH_H, 0U);
        status = bento_ui_switch_create(scope->ui, &args,
                                        &scope->output_switch);
    }
    if (status == BENTO_OK) {
        args = place_at(LED_X, LED_Y, LED_SIZE, LED_SIZE, COLOR_LED);
        args.font_px = LED_WIDGET_DIM;
        status =
            bento_ui_led_create(scope->ui, &args, &scope->output_led);
    }
    return status;
}

/* The duty, raw and frequency texts in the box. */
static bento_status_t add_readouts(hw_scope_t *scope)
{
    const label_style_t duty = {PANEL_TEXT_X, DUTY_Y, COLOR_DUTY,
                                FONT_PX};
    const label_style_t raw = {PANEL_TEXT_X, RAW_Y, COLOR_DIM_TEXT,
                               FONT_PX};
    const label_style_t pot = {PANEL_TEXT_X, POT_NAME_Y, COLOR_DIM_TEXT,
                               FONT_PX};
    const label_style_t freq = {FREQ_TEXT_X, FREQ_TEXT_Y, COLOR_TEXT,
                                FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(scope->ui, "Duty: --%", &duty, &scope->duty_text);

    if (status == BENTO_OK) {
        status =
            add_label(scope->ui, "raw: ----", &raw, &scope->raw_text);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "POTEN = duty", &pot, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(scope->ui, "100 Hz", &freq, &scope->freq_text);
    }
    return status;
}

/* The frequency slider and its caption. */
static bento_status_t add_slider(hw_scope_t *scope)
{
    const label_style_t caption = {SLIDER_CAPTION_X, SLIDER_CAPTION_Y,
                                   COLOR_CAPTION, FONT_PX};
    bento_ui_widget_args_t args =
        place_at(SLIDER_X, SLIDER_Y, SLIDER_W, SLIDER_H, 0U);
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(scope->ui, "Freq (1-500 Hz)", &caption, &unused);

    args.min_val = FREQ_MIN_HZ;
    args.max_val = FREQ_MAX_HZ;
    args.font_px = FREQ_START_HZ;
    if (status == BENTO_OK) {
        status =
            bento_ui_slider_create(scope->ui, &args, &scope->slider);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(hw_scope_t *scope)
{
    bento_ui_id_t unused;
    bento_status_t status = clear_screen(scope->ui);

    if (status == BENTO_OK) {
        status = add_background(scope->ui);
    }
    if (status == BENTO_OK) {
        status = add_centered(
            scope->ui, "Part 3 - HW Scope: Function Generator", TITLE_Y,
            COLOR_TITLE, TITLE_FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_chart(scope);
    }
    if (status == BENTO_OK) {
        status = add_output(scope);
    }
    if (status == BENTO_OK) {
        status = add_readouts(scope);
    }
    if (status == BENTO_OK) {
        status = add_slider(scope);
    }
    if (status == BENTO_OK) {
        status = add_centered(
            scope->ui, "Turn POTEN - the duty and the real LED follow",
            HINT_Y, COLOR_DIM_TEXT, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(scope->ui,
                         "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                         "Burapha University",
                         FOOTER_Y, COLOR_FOOTER, FONT_PX, &unused);
    }
    return status;
}

/* Sets a label to "<prefix><value><suffix>"; a setter's refusal is
 * ignored, as MicroPython ignores it (modui.c). */
static void show_number(bento_ui_t *ui, bento_ui_id_t label,
                        const char *prefix, int32_t value,
                        const char *suffix)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%s%" PRId32 "%s", prefix, value,
                   suffix);
    (void)bento_ui_set_text(ui, label, text, (uint32_t)strlen(text));
}

/* The OUTPUT switch: PWM at the duty, or dark; the LED widget shows
 * the state. */
static bento_status_t set_output(hw_scope_t *scope, bool on)
{
    bento_status_t status;

    scope->output_on = on;
    scope->blinking = false;
    if (on) {
        status = bento_gpio_led_brightness(LED_BLUE, scope->duty);
    } else {
        status = bento_gpio_led_brightness(LED_BLUE, 0);
        if (status == BENTO_OK) {
            status = bento_gpio_led_off(LED_BLUE);
        }
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_prop(scope->ui, scope->output_led,
                                BENTO_UI_PROP_LED_BRIGHTNESS,
                                on ? LED_WIDGET_FULL : LED_WIDGET_DIM);
    }
    return status;
}

static bento_status_t handle_event(hw_scope_t *scope,
                                   const bento_ui_event_t *event)
{
    bento_status_t status = BENTO_OK;

    if ((event->type == BENTO_UI_EVENT_TOGGLED) &&
        (event->id == scope->output_switch)) {
        status = set_output(scope, event->value == 1);
    } else if ((event->type == BENTO_UI_EVENT_VALUE_CHANGED) &&
               (event->id == scope->slider)) {
        scope->freq_hz = event->value;
        show_number(scope->ui, scope->freq_text, "", scope->freq_hz,
                    " Hz");
    } else {
        /* any other event changes nothing */
    }
    return status;
}

/* The touch events; ui.poll() answers no events when it fails, so a
 * failed poll is not an error here. */
static bento_status_t read_events(hw_scope_t *scope)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    if (bento_ui_poll(scope->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = handle_event(scope, &events[i]);
    }
    return status;
}

/* The knob: its raw value always, a new duty when it moved by more
 * than DUTY_STEP (the real LED follows while OUTPUT is on). */
static bento_status_t follow_knob(hw_scope_t *scope)
{
    int32_t duty = 0;
    int32_t raw = 0;
    bento_status_t status = read_pot(&duty, &raw);

    if (status == BENTO_OK) {
        show_number(scope->ui, scope->raw_text, "raw: ", raw, "");
    }
    if ((status == BENTO_OK) && (abs(duty - scope->duty) > DUTY_STEP)) {
        scope->duty = duty;
        show_number(scope->ui, scope->duty_text, "Duty: ", duty, "%");
        if (scope->output_on) {
            status = bento_gpio_led_brightness(LED_BLUE, duty);
        }
    }
    return status;
}

/* At 5 Hz and below the LED blinks with the signal (on for duty percent
 * of each period); faster, the eye would see a steady light, so the
 * brightness follows the duty again. */
static bento_status_t drive_led(hw_scope_t *scope)
{
    bento_status_t status = BENTO_OK;

    if (scope->freq_hz <= BLINK_MAX_HZ) {
        const int32_t period_ms = MS_PER_SECOND / scope->freq_hz;
        const bool on_now =
            (int32_t)(bento_time_ticks_ms() % (uint32_t)period_ms) <
            ((period_ms * scope->duty) / PERCENT);

        if (!scope->blinking || (on_now != scope->lit)) {
            scope->blinking = true;
            scope->lit = on_now;
            status = bento_gpio_led_brightness(LED_BLUE,
                                               on_now ? PERCENT : 0);
        }
    } else if (scope->blinking) {
        scope->blinking = false;
        status = bento_gpio_led_brightness(LED_BLUE, scope->duty);
    } else {
        /* the PWM brightness already follows the duty */
    }
    return status;
}

/* Eight new points of the square wave; the duty applies to the next
 * point at once. A point is sent and not answered, as in MicroPython. */
static void push_square(hw_scope_t *scope)
{
    const bento_real_t period =
        (bento_real_t)SAMPLE_RATE_HZ / (bento_real_t)scope->freq_hz;
    const bento_real_t high_part =
        (bento_real_t)scope->duty / (bento_real_t)PERCENT;
    int32_t i;

    for (i = 0; i < POINTS_PER_ROUND; i++) {
        const bento_real_t within = (bento_real_t)fmod(
            (double)(scope->phase + i), (double)period);

        (void)bento_ui_chart_set_next(
            scope->ui, scope->chart, 0U,
            ((within / period) < high_part) ? SIGNAL_HIGH : SIGNAL_LOW);
    }
    scope->phase += POINTS_PER_ROUND;
}

/* One pass: events, the knob, the LED, then the trace. */
static bento_status_t run_pass(hw_scope_t *scope)
{
    bento_status_t status = read_events(scope);

    if (status == BENTO_OK) {
        status = follow_knob(scope);
    }
    if ((status == BENTO_OK) && scope->output_on) {
        status = drive_led(scope);
    }
    if (status == BENTO_OK) {
        push_square(scope);
    }
    return status;
}

/* One pass every LOOP_MS until RUN_MS has passed, Stop is pressed or
 * the knob or the LED fails. */
static bento_status_t run_loop(hw_scope_t *scope)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = run_pass(scope);
        if (status == BENTO_OK) {
            status = bento_time_sleep_ms(LOOP_MS);
        }
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

/* The cleanup: the real LED dark, whatever ended the loop. */
static bento_status_t led_off(void)
{
    bento_status_t status = bento_gpio_led_brightness(LED_BLUE, 0);

    if (status == BENTO_OK) {
        status = bento_gpio_led_off(LED_BLUE);
    }
    return status;
}

/* The run is over but the picture stays: a band over the bottom rows
 * says so. */
static bento_status_t show_finished(bento_ui_t *ui)
{
    const label_style_t style = {END_TEXT_X, END_TEXT_Y, COLOR_END_TEXT,
                                 END_FONT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t band;
    bento_ui_id_t label;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = 0;
    args.y = END_Y;
    args.w = END_W;
    args.h = END_H;
    args.color = COLOR_END;
    args.min_val = COLOR_END_TEXT; /* the original's min= */
    args.max_val = 0;
    args.font_px = 1;
    status = bento_ui_panel_create(ui, &args, &band);
    if (status == BENTO_OK) {
        status = add_label(ui, "ตัวอย่างจบแล้ว กดปุ่มไม่ได้", &style, &label);
    }
    return status;
}

int main(void)
{
    hw_scope_t scope;
    /* gpio.led(n) refuses an LED the board does not have */
    bento_status_t status = (LED_BLUE < bento_gpio_num_leds())
                                ? BENTO_OK
                                : BENTO_E_INVALID_ARGUMENT;

    (void)memset(&scope, 0, sizeof(scope));
    scope.ui = bento_runtime_get_ui();
    scope.freq_hz = FREQ_START_HZ;
    scope.duty = DUTY_START;
    if (status == BENTO_OK) {
        status = build_screen(&scope);
    }
    if (status == BENTO_OK) {
        /* lcd.print() ignores a console that is not ready (modlcd.c) */
        (void)bento_lcd_print(
            "sec3 ex18: เส้นวิ่งตลอด - pot คุม duty, OUTPUT ขับ LED จริง");
    }
    if (status == BENTO_OK) {
        const bento_status_t loop_status = run_loop(&scope);
        const bento_status_t off_status = led_off();

        status = (off_status != BENTO_OK) ? off_status : loop_status;
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex18: done\n");
        status = show_finished(scope.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
