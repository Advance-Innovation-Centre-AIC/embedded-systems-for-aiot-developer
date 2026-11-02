/*
 * ex12_waveform_gen.c -- เครื่องกำเนิดสัญญาณ (Waveform Generator)
 *
 * เลือกรูปคลื่นห้าแบบ (Square, Sine, Triangle, Sawtooth, Noise) จาก
 * Dropdown และความถี่จากสไลเดอร์ (100 + ค่า^2 Hz) แล้วดูคลื่นวิ่งบนกราฟ
 * 200 จุด ทีละแปดจุดทุก 100 ms ปุ่ม Run ให้คลื่นวิ่ง ปุ่ม Stop หยุดภาพนิ่ง
 * คลื่นคำนวณทีละจุดจากเฟสในหนึ่งคาบที่อัตราสุ่ม 48 kHz และสัญญาณรบกวน
 * มาจาก LFSR 16 บิต ทำงาน 2 นาทีแล้วจบ และวาดแถบ "ตัวอย่างจบแล้ว"
 * ทับแถวล่าง
 *
 * Pick one of five waveforms (Square, Sine, Triangle, Sawtooth, Noise)
 * from the dropdown and a frequency with the slider (100 + value^2 Hz),
 * and watch the wave scroll across a 200-point chart, eight points
 * every 100 ms. Run lets it scroll, Stop freezes it. Each point comes
 * from its phase within one period at a 48 kHz sample rate; the noise
 * comes from a 16-bit LFSR. The program ends after two minutes and then
 * draws a "finished" band over the bottom rows.
 *
 * C twin of ex12_waveform_gen.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/eva/ex12_waveform_gen.py). The library twin
 * aiot-lvgl-sec3-ex12-waveform-gen has frequency buttons instead of the
 * slider, so this file is written new. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. What C changes: Python's int is int32_t
 * here; the dropdown reports 0 to 4, and any other choice ends the
 * program with BENTO_E_INVALID_ARGUMENT where MicroPython raises
 * IndexError; sin() and fmod() work in double and are rounded to the
 * board's float width, so a sine point can differ from MicroPython's in
 * a rare case.
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
    TITLE_Y = 8,
    CHART_X = 66,
    CHART_Y = 44,
    CHART_W = 660,
    CHART_H = 240,
    MENU_X = 36,
    MENU_Y = 294,
    MENU_W = 170,
    MENU_H = 44,
    SLIDER_X = 280,
    SLIDER_Y = 316,
    SLIDER_W = 240,
    SLIDER_H = 22,
    FREQ_TEXT_X = 310,
    FREQ_TEXT_Y = 290,
    INFO_X = 560,
    INFO_Y = 352,
    RUN_X = 560,
    STOP_X = 676,
    BUTTON_Y = 294,
    BUTTON_W = 104,
    BUTTON_H = 46,
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

/* The generator. */
enum {
    WAVES = 5,
    WAVE_SQUARE = 0,
    WAVE_SINE = 1,
    WAVE_TRIANGLE = 2,
    WAVE_NOISE = 4, /* 3 is the sawtooth */
    SAMPLE_RATE_HZ = 48000,
    CHART_POINTS = 200,
    POINTS_PER_ROUND =
        8, /* about 80 points a second: a running scope */
    CHART_MAX = 100,
    CHART_MIDDLE = 50,
    WAVE_AMPLITUDE = 20,
    NOISE_SPAN = (2 * WAVE_AMPLITUDE) + 1, /* -20 to 20 */
    TRIANGLE_SLOPE = 4, /* up from -1 to 1 in half a period */
    TRIANGLE_TOP = 3,   /* and down again: 3 - 4 * phase */
    SLIDER_MAX = 100,
    SLIDER_START = 30,
    FREQ_BASE_HZ =
        100, /* the frequency is 100 + slider^2, as the C demo */
    FREQ_START_HZ = 1000,
    LFSR_START = 0xACE1,
    LFSR_TAPS = 0xB400,
    FREQ_TEXT_BYTES = 21, /* "Freq: ", an int32_t, " Hz" and the NUL */
    INFO_TEXT_BYTES = 20  /* "Waveform: " and the longest name */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,
    RUN_MS = 120000 /* the example ends after two minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x1A1A2E,
    COLOR_TITLE = 0x00FF88,
    COLOR_WAVE = 0x00FF00,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_INFO = 0x888888,
    COLOR_RUN = 0x1B5E20,
    COLOR_STOP = 0x333333,
    COLOR_FOOTER = 0x666666
};

/* The "finished" band's colours, 0xRRGGBB. */
enum {
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

static const char *const WAVE_NAMES[WAVES] = {
    "Square", "Sine", "Triangle", "Sawtooth", "Noise"};

/* The widgets the program changes, and the generator's state. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    bento_ui_id_t menu;
    bento_ui_id_t slider;
    bento_ui_id_t freq_text;
    bento_ui_id_t info_text;
    bento_ui_id_t run;
    bento_ui_id_t stop;
    int32_t wave;
    int32_t freq_hz;
    bool running;
    int32_t phase; /* samples since the start */
    uint32_t lfsr;
} generator_t;

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

/* The scope: a 200-point chart; .prop() never raises in MicroPython. */
static bento_status_t add_chart(generator_t *generator)
{
    const bento_ui_widget_args_t args =
        place_at(CHART_X, CHART_Y, CHART_W, CHART_H, COLOR_WAVE);
    bento_ui_widget_args_t chart = args;
    bento_status_t status;

    chart.min_val = 0;
    chart.max_val = CHART_MAX;
    status =
        bento_ui_chart_create(generator->ui, &chart, &generator->chart);
    if (status == BENTO_OK) {
        (void)bento_ui_set_prop(generator->ui, generator->chart,
                                BENTO_UI_PROP_CHART_POINTS,
                                CHART_POINTS);
    }
    return status;
}

/* The wave menu, Sine chosen, and the frequency slider. An option is
 * sent and not answered, and the value is a setter, both ignored in
 * MicroPython too. */
static bento_status_t add_controls(generator_t *generator)
{
    bento_ui_widget_args_t args =
        place_at(MENU_X, MENU_Y, MENU_W, MENU_H, 0U);
    int32_t wave;
    bento_status_t status = bento_ui_dropdown_create(
        generator->ui, &args, &generator->menu);

    for (wave = 0; (wave < WAVES) && (status == BENTO_OK); wave++) {
        (void)bento_ui_add_option(generator->ui, generator->menu,
                                  WAVE_NAMES[wave],
                                  (uint32_t)strlen(WAVE_NAMES[wave]));
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_value(generator->ui, generator->menu,
                                 WAVE_SINE);
        args = place_at(SLIDER_X, SLIDER_Y, SLIDER_W, SLIDER_H, 0U);
        args.min_val = 0;
        args.max_val = SLIDER_MAX;
        args.font_px = SLIDER_START;
        status = bento_ui_slider_create(generator->ui, &args,
                                        &generator->slider);
    }
    return status;
}

/* The two texts and the Run and Stop buttons. */
static bento_status_t add_texts_and_buttons(generator_t *generator)
{
    const label_style_t freq = {FREQ_TEXT_X, FREQ_TEXT_Y, COLOR_TEXT,
                                FONT_PX};
    const label_style_t info = {INFO_X, INFO_Y, COLOR_INFO, FONT_PX};
    const bento_ui_widget_args_t run =
        place_at(RUN_X, BUTTON_Y, BUTTON_W, BUTTON_H, COLOR_RUN);
    const bento_ui_widget_args_t stop =
        place_at(STOP_X, BUTTON_Y, BUTTON_W, BUTTON_H, COLOR_STOP);
    bento_status_t status = add_label(generator->ui, "Freq: 1000 Hz",
                                      &freq, &generator->freq_text);

    if (status == BENTO_OK) {
        status = add_label(generator->ui, "Waveform: Sine", &info,
                           &generator->info_text);
    }
    if (status == BENTO_OK) {
        status =
            add_button(generator->ui, "Run", &run, &generator->run);
    }
    if (status == BENTO_OK) {
        status =
            add_button(generator->ui, "Stop", &stop, &generator->stop);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(generator_t *generator)
{
    bento_ui_id_t unused;
    bento_status_t status = clear_screen(generator->ui);

    if (status == BENTO_OK) {
        status = add_background(generator->ui);
    }
    if (status == BENTO_OK) {
        status = add_centered(generator->ui,
                              "Part 3 - Example 1: Waveform Generator",
                              TITLE_Y, COLOR_TITLE, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_chart(generator);
    }
    if (status == BENTO_OK) {
        status = add_controls(generator);
    }
    if (status == BENTO_OK) {
        status = add_texts_and_buttons(generator);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(generator->ui,
                         "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                         "Burapha University",
                         FOOTER_Y, COLOR_FOOTER, FONT_PX, &unused);
    }
    return status;
}

/* One step of the 16-bit LFSR with taps 0xB400, the pseudo-random
 * source of aic_scope_generate_noise. */
static uint32_t lfsr_next(uint32_t *lfsr)
{
    const uint32_t low_bit = *lfsr & 1U;

    *lfsr >>= 1U;
    if (low_bit != 0U) {
        *lfsr ^= (uint32_t)LFSR_TAPS;
    }
    return *lfsr;
}

/* int(value): Python's int() of a real cuts toward zero, as the cast
 * does; the real expression is worked out first, at the board's float
 * width. */
static int32_t int_of(bento_real_t value)
{
    return (int32_t)value;
}

/* The wave between -1 and 1 at phase (0 to 1 within one period). */
static bento_real_t wave_at(int32_t wave, bento_real_t phase)
{
    const bento_real_t half = (bento_real_t)0.5;
    const bento_real_t full_turn =
        (bento_real_t)6.283185307179586; /* 2 pi */
    bento_real_t value;

    if (wave == WAVE_SQUARE) {
        value = (phase < half) ? (bento_real_t)1 : (bento_real_t)-1;
    } else if (wave == WAVE_SINE) {
        const bento_real_t angle = full_turn * phase;

        value = (bento_real_t)sin((double)angle);
    } else if (wave == WAVE_TRIANGLE) {
        const bento_real_t slope = (bento_real_t)TRIANGLE_SLOPE * phase;

        value = (phase < half) ? (slope - (bento_real_t)1)
                               : ((bento_real_t)TRIANGLE_TOP - slope);
    } else {
        value = ((bento_real_t)2 * phase) - (bento_real_t)1;
    }
    return value;
}

/* One chart point of the chosen wave, sample offset after phase. */
static int32_t next_point(generator_t *generator, int32_t offset)
{
    int32_t point;

    if (generator->wave == WAVE_NOISE) {
        const uint32_t draw =
            lfsr_next(&generator->lfsr) % (uint32_t)NOISE_SPAN;

        point = CHART_MIDDLE + ((int32_t)draw - WAVE_AMPLITUDE);
    } else {
        const bento_real_t period = (bento_real_t)SAMPLE_RATE_HZ /
                                    (bento_real_t)generator->freq_hz;
        const int32_t sample = generator->phase + offset;
        const bento_real_t within =
            (bento_real_t)fmod((double)sample, (double)period);

        point = CHART_MIDDLE +
                int_of(wave_at(generator->wave, within / period) *
                       (bento_real_t)WAVE_AMPLITUDE);
    }
    return point;
}

/* The running scope: eight new points a round, not the whole line. A
 * point is sent and not answered, as in MicroPython. */
static void push_chunk(generator_t *generator)
{
    int32_t i;

    for (i = 0; i < POINTS_PER_ROUND; i++) {
        (void)bento_ui_chart_set_next(generator->ui, generator->chart,
                                      0U, next_point(generator, i));
    }
    generator->phase += POINTS_PER_ROUND;
}

static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* A new wave from the menu; one the menu cannot offer ends the
 * program, where MicroPython raises IndexError. */
static bento_status_t choose_wave(generator_t *generator, int32_t wave)
{
    char text[INFO_TEXT_BYTES];
    bento_status_t status = BENTO_E_INVALID_ARGUMENT;

    if ((wave >= 0) && (wave < WAVES)) {
        generator->wave = wave;
        (void)snprintf(text, sizeof(text), "Waveform: %s",
                       WAVE_NAMES[wave]);
        /* a setter's refusal is ignored, as MicroPython ignores it */
        (void)bento_ui_set_text(generator->ui, generator->info_text,
                                text, (uint32_t)strlen(text));
        status = BENTO_OK;
    }
    return status;
}

/* A new frequency from the slider: 100 + value^2 Hz. */
static void choose_freq(generator_t *generator, int32_t value)
{
    char text[FREQ_TEXT_BYTES];

    generator->freq_hz = FREQ_BASE_HZ + (value * value);
    (void)snprintf(text, sizeof(text), "Freq: %d Hz",
                   (int)generator->freq_hz);
    (void)bento_ui_set_text(generator->ui, generator->freq_text, text,
                            (uint32_t)strlen(text));
}

static bento_status_t handle_event(generator_t *generator,
                                   const bento_ui_event_t *event)
{
    bento_status_t status = BENTO_OK;

    if (event_is(event, BENTO_UI_EVENT_CLICKED, generator->run)) {
        generator->running = true;
    } else if (event_is(event, BENTO_UI_EVENT_CLICKED,
                        generator->stop)) {
        generator->running = false;
    } else if (event_is(event, BENTO_UI_EVENT_VALUE_CHANGED,
                        generator->menu)) {
        status = choose_wave(generator, event->value);
    } else if (event_is(event, BENTO_UI_EVENT_VALUE_CHANGED,
                        generator->slider)) {
        choose_freq(generator, event->value);
    } else {
        /* any other event changes nothing */
    }
    return status;
}

/* One pass: the touch events, then new points while the wave runs.
 * ui.poll() answers no events when it fails, so a failed poll is not an
 * error here. */
static bento_status_t run_pass(generator_t *generator)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    if (bento_ui_poll(generator->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = handle_event(generator, &events[i]);
    }
    if ((status == BENTO_OK) && generator->running) {
        push_chunk(generator);
    }
    return status;
}

/* One pass every LOOP_MS until RUN_MS has passed or Stop is pressed. */
static bento_status_t run_loop(generator_t *generator)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = run_pass(generator);
        if (status == BENTO_OK) {
            status = bento_time_sleep_ms(LOOP_MS);
        }
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
    generator_t generator;
    bento_status_t status;

    (void)memset(&generator, 0, sizeof(generator));
    generator.ui = bento_runtime_get_ui();
    generator.wave = WAVE_SINE;
    generator.freq_hz = FREQ_START_HZ;
    generator.running = true;
    generator.lfsr = LFSR_START;
    status = build_screen(&generator);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print(
            "sec3 ex12: Run = คลื่นวิ่งต่อเนื่อง, Stop = หยุดนิ่ง");
        status = run_loop(&generator);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex12: done\n");
        status = show_finished(generator.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
