/*
 * ex15_oscilloscope_ui.c -- ออสซิลโลสโคปดิจิทัล (Digital Oscilloscope)
 *
 * จอสโคปที่เส้นคลื่นวิ่งต่อเนื่อง ทีละแปดจุดทุก 100 ms แผงควบคุมขวาเลือก
 * รูปคลื่น (Square, Sine, Triangle) และความถี่ (100 + ค่า^2 Hz) ปุ่ม Run
 * และ Stop และแถบวัดด้านล่างแสดง Vpp, ความถี่ที่วัดได้ และ RMS ที่คำนวณจาก
 * คลื่น 200 ตัวอย่างหน่วย int16 ทุกหนึ่งวินาที ทำงาน 2 นาทีแล้วจบ
 * และวาดแถบ "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * A scope screen whose trace scrolls on, eight points every 100 ms. The
 * panel on the right picks the waveform (Square, Sine, Triangle) and the
 * frequency (100 + value^2 Hz), with Run and Stop; the strip below
 * shows Vpp, the measured frequency and the RMS, computed every second
 * from 200 int16 samples of the wave. The program ends after two
 * minutes and then draws a "finished" band over the bottom rows.
 *
 * C twin of ex15_oscilloscope_ui.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/devkit/ex15_oscilloscope_ui.py), from the library
 * twin aiot-lvgl-sec3-ex15-oscilloscope-ui. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. What C changes: Python's int is int32_t
 * here, and the sum of squares is int64_t (it passes 2^31); measure()
 * makes each sample as it needs it instead of building gen_wave()'s
 * list; sin(), fmod() and sqrt() work in double and are rounded to the
 * board's float width.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    BUTTON_FONT_PX = 16,
    TITLE_Y = 6,
    CHART_X = 16,
    CHART_Y = 42,
    CHART_W = 594,
    CHART_H = 240,
    PANEL_X = 620,
    PANEL_W = 158,
    BOX_EDGE_PX = 6,
    MENU_X = 630,
    MENU_Y = 52,
    MENU_W = 138,
    MENU_H = 44,
    SLIDER_X = 634,
    SLIDER_Y = 112,
    SLIDER_W = 130,
    SLIDER_H = 22,
    PANEL_TEXT_X = 650,
    FREQ_TEXT_Y = 140,
    TIME_DIV_Y = 176,
    VOLT_DIV_Y = 198,
    RUN_X = 630,
    STOP_X = 702,
    BUTTON_Y = 224,
    BUTTON_W = 66,
    BUTTON_H = 46,
    STRIP_X = 16,
    STRIP_Y = 288,
    STRIP_W = 762,
    STRIP_H = 52,
    VPP_X = 80,
    FREQ_MEAS_X = 330,
    RMS_X = 620,
    MEASURE_Y = 304,
    FOOTER_Y = 378
};

/* The "finished" band over the bottom rows. */
enum {
    END_Y = 330,
    END_W = 792,
    END_H = 36,
    END_TEXT_X = 232,
    END_TEXT_Y = 334,
    END_FONT_PX = 20
};

/* The generator and the measurement. */
enum {
    WAVES = 3,
    WAVE_SQUARE = 0,
    WAVE_SINE = 1, /* 2 is the triangle */
    SAMPLE_RATE_HZ = 48000,
    CHART_POINTS = 200,
    MEASURE_SAMPLES = 200,
    POINTS_PER_ROUND = 8,
    CHART_MAX = 100,
    CHART_MIDDLE = 50,
    WAVE_AMPLITUDE = 20,
    SAMPLE_AMPLITUDE = 16000, /* int16 units, as the C demo */
    COUNTS_PER_VOLT = 327,    /* the C demo's divisor */
    TRIANGLE_SLOPE = 4,       /* up from -1 to 1 in half a period */
    TRIANGLE_TOP = 3,         /* and down again: 3 - 4 * phase */
    SLIDER_MAX = 100,
    SLIDER_START = 30,
    FREQ_BASE_HZ = 100, /* the frequency is 100 + slider^2 */
    FREQ_START_HZ = 1000,
    MEASURE_EVERY_MS = 1000,
    TEXT_BYTES = 24 /* the longest: "Freq: ", an int32_t, " Hz" */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,
    RUN_MS = 120000 /* the example ends after two minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x0A0A0A,
    COLOR_GREEN = 0x00FF00,
    COLOR_BOX = 0x1A1A1A,
    COLOR_BOX_EDGE = 0x333333,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_YELLOW = 0xFFFF00,
    COLOR_CYAN = 0x00FFFF,
    COLOR_RUN = 0x1B5E20,
    COLOR_STOP = 0x333333,
    COLOR_FOOTER = 0x666666
};

/* The "finished" band's colours, 0xRRGGBB. */
enum {
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

static const char FOOTER[] =
    "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";

/* The widgets the program changes, and the generator's state. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    bento_ui_id_t menu;
    bento_ui_id_t slider;
    bento_ui_id_t freq_text;
    bento_ui_id_t run;
    bento_ui_id_t stop;
    bento_ui_id_t vpp;
    bento_ui_id_t freq_measured;
    bento_ui_id_t rms;
    int32_t wave;
    int32_t freq_hz;
    bool running;
    int32_t phase; /* samples since the start */
} scope_t;

/* A label's place and look: the original's x=, y=, color= and value=. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} label_style_t;

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

/* A button with the original's 16 px text. */
static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 const bento_ui_widget_args_t *place,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args = *place;

    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.font_px = BUTTON_FONT_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* A dark box with a grey 6 px edge, behind a group of controls. */
static bento_status_t add_box(bento_ui_t *ui, int16_t x, int16_t y,
                              int16_t width, int16_t height)
{
    bento_ui_widget_args_t args =
        place_at(x, y, width, height, COLOR_BOX);
    bento_ui_id_t box;

    args.min_val =
        COLOR_BOX_EDGE; /* a panel's min= is its edge colour */
    args.max_val = BOX_EDGE_PX;
    args.font_px = 1;
    return bento_ui_panel_create(ui, &args, &box);
}

/* The scope: a 200-point chart; .prop() never raises in MicroPython. */
static bento_status_t add_chart(scope_t *scope)
{
    bento_ui_widget_args_t args =
        place_at(CHART_X, CHART_Y, CHART_W, CHART_H, COLOR_GREEN);
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

/* The wave menu with Sine chosen, and the frequency slider. An option
 * is sent and not answered, and the value is a setter, both ignored in
 * MicroPython too. */
static bento_status_t add_menu_and_slider(scope_t *scope)
{
    static const char *const wave_names[WAVES] = {"Square", "Sine",
                                                  "Triangle"};
    bento_ui_widget_args_t args =
        place_at(MENU_X, MENU_Y, MENU_W, MENU_H, 0U);
    int32_t wave;
    bento_status_t status =
        bento_ui_dropdown_create(scope->ui, &args, &scope->menu);

    for (wave = 0; (wave < WAVES) && (status == BENTO_OK); wave++) {
        (void)bento_ui_add_option(scope->ui, scope->menu,
                                  wave_names[wave],
                                  (uint32_t)strlen(wave_names[wave]));
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_value(scope->ui, scope->menu, WAVE_SINE);
        args = place_at(SLIDER_X, SLIDER_Y, SLIDER_W, SLIDER_H, 0U);
        args.min_val = 0;
        args.max_val = SLIDER_MAX;
        args.font_px = SLIDER_START;
        status =
            bento_ui_slider_create(scope->ui, &args, &scope->slider);
    }
    return status;
}

/* The control panel on the right: box, menu, slider, texts, buttons. */
static bento_status_t add_panel(scope_t *scope)
{
    const label_style_t freq = {PANEL_TEXT_X, FREQ_TEXT_Y, COLOR_TEXT,
                                FONT_PX};
    const label_style_t time_div = {PANEL_TEXT_X, TIME_DIV_Y,
                                    COLOR_YELLOW, FONT_PX};
    const label_style_t volt_div = {PANEL_TEXT_X, VOLT_DIV_Y,
                                    COLOR_CYAN, FONT_PX};
    const bento_ui_widget_args_t run =
        place_at(RUN_X, BUTTON_Y, BUTTON_W, BUTTON_H, COLOR_RUN);
    const bento_ui_widget_args_t stop =
        place_at(STOP_X, BUTTON_Y, BUTTON_W, BUTTON_H, COLOR_STOP);
    bento_ui_id_t unused;
    bento_status_t status =
        add_box(scope->ui, PANEL_X, CHART_Y, PANEL_W, CHART_H);

    if (status == BENTO_OK) {
        status = add_menu_and_slider(scope);
    }
    if (status == BENTO_OK) {
        status =
            add_label(scope->ui, "1000 Hz", &freq, &scope->freq_text);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "1 ms/div", &time_div, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "1 V/div", &volt_div, &unused);
    }
    if (status == BENTO_OK) {
        status = add_button(scope->ui, "Run", &run, &scope->run);
    }
    if (status == BENTO_OK) {
        status = add_button(scope->ui, "Stop", &stop, &scope->stop);
    }
    return status;
}

/* The measurement strip: Vpp, frequency and RMS. */
static bento_status_t add_strip(scope_t *scope)
{
    const label_style_t vpp = {VPP_X, MEASURE_Y, COLOR_GREEN,
                               BUTTON_FONT_PX};
    const label_style_t freq = {FREQ_MEAS_X, MEASURE_Y, COLOR_YELLOW,
                                BUTTON_FONT_PX};
    const label_style_t rms = {RMS_X, MEASURE_Y, COLOR_CYAN,
                               BUTTON_FONT_PX};
    bento_status_t status =
        add_box(scope->ui, STRIP_X, STRIP_Y, STRIP_W, STRIP_H);

    if (status == BENTO_OK) {
        status = add_label(scope->ui, "Vpp: --", &vpp, &scope->vpp);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "Freq: -- Hz", &freq,
                           &scope->freq_measured);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "RMS: --", &rms, &scope->rms);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(scope_t *scope)
{
    bento_ui_id_t unused;
    bento_status_t status = clear_screen(scope->ui);

    if (status == BENTO_OK) {
        status = add_background(scope->ui);
    }
    if (status == BENTO_OK) {
        status = add_centered(
            scope->ui, "Part 3 - Example 5: Digital Oscilloscope",
            TITLE_Y, COLOR_GREEN, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_chart(scope);
    }
    if (status == BENTO_OK) {
        status = add_panel(scope);
    }
    if (status == BENTO_OK) {
        status = add_strip(scope);
    }
    if (status == BENTO_OK) {
        status = add_centered(scope->ui, FOOTER, FOOTER_Y, COLOR_FOOTER,
                              FONT_PX, &unused);
    }
    return status;
}

/* The wave between -1 and 1 at phase (0 to 1 within one period):
 * square, sine, or a triangle for any other choice. */
static bento_real_t wave_at(int32_t wave, bento_real_t phase)
{
    const bento_real_t half = (bento_real_t)0.5;
    const bento_real_t full_turn =
        (bento_real_t)(2.0 * 3.141592653589793);
    bento_real_t value;

    if (wave == WAVE_SQUARE) {
        value = (phase < half) ? (bento_real_t)1 : (bento_real_t)-1;
    } else if (wave == WAVE_SINE) {
        value = (bento_real_t)sin((double)(full_turn * phase));
    } else {
        const bento_real_t slope = (bento_real_t)TRIANGLE_SLOPE * phase;

        value = (phase < half) ? (slope - (bento_real_t)1)
                               : ((bento_real_t)TRIANGLE_TOP - slope);
    }
    return value;
}

/* The phase (0 to 1) of sample number `sample` at freq_hz: Python's %
 * of a real, which fmod() gives for a positive sample. */
static bento_real_t phase_of(int32_t sample, int32_t freq_hz)
{
    const bento_real_t period =
        (bento_real_t)SAMPLE_RATE_HZ / (bento_real_t)freq_hz;
    const bento_real_t within =
        (bento_real_t)fmod((double)sample, (double)period);

    return within / period;
}

/* gen_wave()'s sample number `sample`, in int16 units. */
static int32_t wave_sample(const scope_t *scope, int32_t sample)
{
    return (int32_t)(wave_at(scope->wave,
                             phase_of(sample, scope->freq_hz)) *
                     (bento_real_t)SAMPLE_AMPLITUDE);
}

/* Sets a label's text; MicroPython's widget setters ignore a refusal
 * (modui.c), so the twin does too. */
static void show_number(bento_ui_t *ui, bento_ui_id_t label,
                        const char *prefix, int32_t value,
                        const char *suffix)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%s%d%s", prefix, (int)value,
                   suffix);
    (void)bento_ui_set_text(ui, label, text, (uint32_t)strlen(text));
}

/* Vpp, the frequency from the upward zero crossings, and the RMS of
 * 200 samples, each in the C demo's units. */
static void measure(const scope_t *scope)
{
    int32_t low = wave_sample(scope, 0);
    int32_t high = low;
    int32_t previous = low;
    int64_t squares = 0;
    int32_t crossings = 0;
    int32_t i;
    bento_real_t mean_square;
    int32_t rms;

    for (i = 0; i < MEASURE_SAMPLES; i++) {
        const int32_t value = wave_sample(scope, i);

        low = (value < low) ? value : low;
        high = (value > high) ? value : high;
        squares += (int64_t)value * (int64_t)value;
        if ((previous < 0) && (value >= 0)) {
            crossings++;
        }
        previous = value;
    }
    mean_square = (bento_real_t)squares / (bento_real_t)MEASURE_SAMPLES;
    rms = (int32_t)(bento_real_t)sqrt((double)mean_square);
    show_number(scope->ui, scope->vpp,
                "Vpp: ", (high - low) / COUNTS_PER_VOLT, "");
    show_number(scope->ui, scope->freq_measured, "Freq: ",
                (crossings * SAMPLE_RATE_HZ) / MEASURE_SAMPLES, " Hz");
    show_number(scope->ui, scope->rms, "RMS: ", rms / COUNTS_PER_VOLT,
                "");
}

/* The running scope: eight new points a round, the phase kept across
 * rounds; a point is sent and not answered, as in MicroPython. */
static void push_chunk(scope_t *scope)
{
    int32_t i;

    for (i = 0; i < POINTS_PER_ROUND; i++) {
        const bento_real_t value = wave_at(
            scope->wave, phase_of(scope->phase + i, scope->freq_hz));

        (void)bento_ui_chart_set_next(
            scope->ui, scope->chart, 0U,
            CHART_MIDDLE +
                (int32_t)(value * (bento_real_t)WAVE_AMPLITUDE));
    }
    scope->phase += POINTS_PER_ROUND;
}

static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

static void handle_event(scope_t *scope, const bento_ui_event_t *event)
{
    if (event_is(event, BENTO_UI_EVENT_CLICKED, scope->run)) {
        scope->running = true;
    } else if (event_is(event, BENTO_UI_EVENT_CLICKED, scope->stop)) {
        scope->running = false;
    } else if (event_is(event, BENTO_UI_EVENT_VALUE_CHANGED,
                        scope->menu)) {
        scope->wave = event->value;
    } else if (event_is(event, BENTO_UI_EVENT_VALUE_CHANGED,
                        scope->slider)) {
        scope->freq_hz = FREQ_BASE_HZ + (event->value * event->value);
        show_number(scope->ui, scope->freq_text, "", scope->freq_hz,
                    " Hz");
    } else {
        /* any other event changes nothing */
    }
}

/* The touch events; ui.poll() answers no events when it fails, so a
 * failed poll is not an error here. */
static void read_events(scope_t *scope)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(scope->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        handle_event(scope, &events[i]);
    }
}

/* One pass every LOOP_MS until RUN_MS has passed or Stop is pressed;
 * while running, a new measurement after each second. */
static bento_status_t run_loop(scope_t *scope)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    uint32_t measured_ms = start_ms;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        read_events(scope);
        if (scope->running) {
            push_chunk(scope);
        }
        if (scope->running &&
            (bento_time_ticks_diff(bento_time_ticks_ms(), measured_ms) >
             MEASURE_EVERY_MS)) {
            measured_ms = bento_time_ticks_ms();
            measure(scope);
        }
        status = bento_time_sleep_ms(LOOP_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
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
    scope_t scope;
    bento_status_t status;

    (void)memset(&scope, 0, sizeof(scope));
    scope.ui = bento_runtime_get_ui();
    scope.wave = WAVE_SINE;
    scope.freq_hz = FREQ_START_HZ;
    scope.running = true;
    status = build_screen(&scope);
    if (status == BENTO_OK) {
        measure(&scope);
        /* lcd.print() ignores a console that is not ready (modlcd.c) */
        (void)bento_lcd_print(
            "sec3 ex15: Run = เส้นวิ่ง + วัด Vpp/Freq/RMS สด");
        status = run_loop(&scope);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex15: done\n");
        status = show_finished(scope.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
