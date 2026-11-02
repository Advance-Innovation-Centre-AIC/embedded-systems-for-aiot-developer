/*
 * ex16_spectrum_analyzer.c -- วิเคราะห์สเปกตรัมด้วย FFT
 * (FFT Spectrum Analyzer)
 *
 * สร้างคลื่นห้าแบบ (Square, Sine, Triangle, Sawtooth, Noise) ที่ 1000 Hz
 * อัตราสุ่ม 48 kHz ขนาด 256 ตัวอย่าง แล้วหาสเปกตรัมด้วย FFT จริงของ
 * dsp.fft_mag() กราฟบนเป็นรูปคลื่นตามเวลา (วาดใหม่เมื่อเปลี่ยนชนิดคลื่น)
 * กราฟล่างเป็นสเปกตรัม 64 แท่ง และบอกความถี่ของยอดที่วัดได้ (bin กว้าง
 * 187.5 Hz ยอดจึงตกที่ 937 ไม่ใช่ 1000 พอดี) ปุ่ม Run วัดซ้ำทุก 250 ms
 * ปุ่ม Stop หยุด ทำงาน 2 นาทีแล้วจบ และวาดแถบ "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * Generates one of five waves (Square, Sine, Triangle, Sawtooth, Noise)
 * at 1000 Hz, 48 kHz sample rate, 256 samples, and finds its spectrum
 * with dsp.fft_mag()'s real FFT. The upper chart shows the wave in time
 * (redrawn when the wave changes), the lower one the spectrum in 64
 * bins, with the frequency of the measured peak (the bins are 187.5 Hz
 * wide, so the peak lands on 937, not exactly 1000). Run measures again
 * every 250 ms, Stop pauses. The program ends after two minutes and then
 * draws a "finished" band over the bottom rows.
 *
 * C twin of ex16_spectrum_analyzer.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/devkit/ex16_spectrum_analyzer.py), written new: the
 * library twin aiot-lvgl-sec3-ex16-spectrum-analyzer follows an older
 * screen. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. What C changes: Python's int is int32_t
 * here; the signal is a float array (dsp.fft_mag() turns the list into
 * floats too) and the FFT's work area is the caller's; the dropdown
 * reports 0 to 4, and any other choice ends the program with
 * BENTO_E_INVALID_ARGUMENT where MicroPython raises IndexError; sin()
 * and fmod() work in double and are rounded to the board's float width.
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_dsp.h"
#include "bento_fmt.h"
#include "bento_runtime.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    BUTTON_FONT_PX = 16,
    TITLE_Y = 8,
    CONTROL_Y = 40,
    CONTROL_H = 44,
    MENU_X = 16,
    MENU_W = 170,
    RUN_X = 196,
    STOP_X = 288,
    BUTTON_W = 84,
    SIGNAL_TEXT_X = 360,
    PEAK_TEXT_X = 560,
    TOP_TEXT_Y = 52,
    CHART_X = 66,
    CHART_W = 660,
    WAVE_CHART_Y = 92,
    WAVE_CHART_H = 86,
    SPECTRUM_Y = 196,
    SPECTRUM_H = 130,
    AXIS_NAME_X = 16,
    TIME_NAME_Y = 124,
    FREQ_NAME_Y = 250,
    SCALE_LOW_X = 66,
    SCALE_HIGH_X = 650,
    SCALE_Y = 334,
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

/* The generator and the FFT. */
enum {
    WAVES = 5,
    WAVE_SQUARE = 0,
    WAVE_SINE = 1,
    WAVE_TRIANGLE = 2,
    WAVE_NOISE = 4, /* 3 is the sawtooth */
    SAMPLE_RATE_HZ = 48000,
    FFT_N = 256,
    MAGNITUDES = FFT_N / 2,
    BINS = 64, /* 64 of the 128 bins, every second one */
    BIN_STEP = MAGNITUDES / BINS,
    WAVE_POINTS = 64,
    SAMPLE_STEP = FFT_N / WAVE_POINTS,
    AMPLITUDE = 16000,
    NOISE_SPAN = (2 * AMPLITUDE) + 1, /* -16000 to 16000 */
    WAVE_CHART_FULL = 100, /* the time chart shows -100 to 100 */
    PERCENT = 100,
    TRIANGLE_SLOPE = 4, /* up from -1 to 1 in half a period */
    TRIANGLE_TOP = 3,   /* and down again: 3 - 4 * phase */
    FREQ_HZ = 1000,
    YIELD_EVERY = 16, /* points between two short pauses */
    YIELD_MS = 6,
    LFSR_START = 0xACE1,
    LFSR_TAPS = 0xB400,
    SIGNAL_TEXT_BYTES =
        32,              /* "gen: ", a name, " ", an int32_t, " Hz" */
    PEAK_TEXT_BYTES = 48 /* "FFT: ", two int32_t and the rest */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 250,
    RUN_MS = 120000 /* the example ends after two minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x0A0A1E,
    COLOR_TITLE = 0xFF6600,
    COLOR_RUN = 0x1B5E20,
    COLOR_STOP = 0x333333,
    COLOR_WAVE = 0x00FF88,
    COLOR_PEAK = 0xFFFF00,
    COLOR_SPECTRUM = 0x00FFFF,
    COLOR_AXIS = 0x888888,
    COLOR_FOOTER = 0x666666
};

/* The "finished" band's colours, 0xRRGGBB. */
enum {
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

static const char *const WAVE_NAMES[WAVES] = {
    "Square", "Sine", "Triangle", "Sawtooth", "Noise"};

/* The signal, the FFT's work area and its magnitudes: 3584 bytes, too
 * big for the 8 KB program stack, so at file scope, zeroed by the
 * loader and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
typedef struct {
    float samples[FFT_N];
    float scratch[BENTO_DSP_FFT_SCRATCH_PER_N * FFT_N];
    float magnitudes[MAGNITUDES];
} spectrum_buffers_t;

extern spectrum_buffers_t g_spectrum;
spectrum_buffers_t g_spectrum;

/* The widgets the program changes, and the generator's state. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t menu;
    bento_ui_id_t run;
    bento_ui_id_t stop;
    bento_ui_id_t signal_text;
    bento_ui_id_t peak_text;
    bento_ui_id_t wave_chart;
    bento_ui_id_t spectrum;
    int32_t wave;
    bool running;
    uint32_t lfsr;
} analyzer_t;

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

/* time.sleep_ms(): Stop during the pause becomes BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The wave menu with Sine chosen, then Run and Stop. An option is sent
 * and not answered, and the value is a setter, both ignored in
 * MicroPython too. */
static bento_status_t add_controls(analyzer_t *analyzer)
{
    const bento_ui_widget_args_t menu =
        place_at(MENU_X, CONTROL_Y, MENU_W, CONTROL_H, 0U);
    const bento_ui_widget_args_t run =
        place_at(RUN_X, CONTROL_Y, BUTTON_W, CONTROL_H, COLOR_RUN);
    const bento_ui_widget_args_t stop =
        place_at(STOP_X, CONTROL_Y, BUTTON_W, CONTROL_H, COLOR_STOP);
    int32_t wave;
    bento_status_t status =
        bento_ui_dropdown_create(analyzer->ui, &menu, &analyzer->menu);

    for (wave = 0; (wave < WAVES) && (status == BENTO_OK); wave++) {
        (void)bento_ui_add_option(analyzer->ui, analyzer->menu,
                                  WAVE_NAMES[wave],
                                  (uint32_t)strlen(WAVE_NAMES[wave]));
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_value(analyzer->ui, analyzer->menu,
                                 WAVE_SINE);
        status = add_button(analyzer->ui, "Run", &run, &analyzer->run);
    }
    if (status == BENTO_OK) {
        status =
            add_button(analyzer->ui, "Stop", &stop, &analyzer->stop);
    }
    return status;
}

/* A chart of `points` points; .prop() never raises in MicroPython. */
static bento_status_t add_chart(bento_ui_t *ui,
                                const bento_ui_widget_args_t *place,
                                int32_t points, bento_ui_id_t *chart)
{
    const bento_status_t status =
        bento_ui_chart_create(ui, place, chart);

    if (status == BENTO_OK) {
        (void)bento_ui_set_prop(ui, *chart, BENTO_UI_PROP_CHART_POINTS,
                                points);
    }
    return status;
}

/* The two texts at the top and the two charts, each with its name. */
static bento_status_t add_charts(analyzer_t *analyzer)
{
    const label_style_t signal = {SIGNAL_TEXT_X, TOP_TEXT_Y, COLOR_WAVE,
                                  FONT_PX};
    const label_style_t peak = {PEAK_TEXT_X, TOP_TEXT_Y, COLOR_PEAK,
                                FONT_PX};
    const label_style_t time_name = {AXIS_NAME_X, TIME_NAME_Y,
                                     COLOR_AXIS, FONT_PX};
    const label_style_t freq_name = {AXIS_NAME_X, FREQ_NAME_Y,
                                     COLOR_AXIS, FONT_PX};
    bento_ui_widget_args_t wave = place_at(
        CHART_X, WAVE_CHART_Y, CHART_W, WAVE_CHART_H, COLOR_WAVE);
    bento_ui_widget_args_t spectrum = place_at(
        CHART_X, SPECTRUM_Y, CHART_W, SPECTRUM_H, COLOR_SPECTRUM);
    bento_ui_id_t unused;
    bento_status_t status = add_label(analyzer->ui, "gen: Sine 1000 Hz",
                                      &signal, &analyzer->signal_text);

    wave.min_val = -WAVE_CHART_FULL;
    wave.max_val = WAVE_CHART_FULL;
    spectrum.min_val = 0;
    spectrum.max_val = PERCENT;
    if (status == BENTO_OK) {
        status = add_label(analyzer->ui, "FFT: -- Hz", &peak,
                           &analyzer->peak_text);
    }
    if (status == BENTO_OK) {
        status = add_chart(analyzer->ui, &wave, WAVE_POINTS,
                           &analyzer->wave_chart);
    }
    if (status == BENTO_OK) {
        status = add_label(analyzer->ui, "time", &time_name, &unused);
    }
    if (status == BENTO_OK) {
        status = add_chart(analyzer->ui, &spectrum, BINS,
                           &analyzer->spectrum);
    }
    if (status == BENTO_OK) {
        status = add_label(analyzer->ui, "freq", &freq_name, &unused);
    }
    return status;
}

/* The frequency scale under the spectrum, then the footer. */
static bento_status_t add_scale_and_footer(bento_ui_t *ui)
{
    const label_style_t low = {SCALE_LOW_X, SCALE_Y, COLOR_AXIS,
                               FONT_PX};
    const label_style_t high = {SCALE_HIGH_X, SCALE_Y, COLOR_AXIS,
                                FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status = add_label(ui, "0 Hz", &low, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "24000 Hz", &high, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(ui,
                         "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                         "Burapha University",
                         FOOTER_Y, COLOR_FOOTER, FONT_PX, &unused);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(analyzer_t *analyzer)
{
    bento_ui_id_t unused;
    bento_status_t status = clear_screen(analyzer->ui);

    if (status == BENTO_OK) {
        status = add_background(analyzer->ui);
    }
    if (status == BENTO_OK) {
        status = add_centered(
            analyzer->ui, "Part 3 - Example 6: FFT Spectrum Analyzer",
            TITLE_Y, COLOR_TITLE, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_controls(analyzer);
    }
    if (status == BENTO_OK) {
        status = add_charts(analyzer);
    }
    if (status == BENTO_OK) {
        status = add_scale_and_footer(analyzer->ui);
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
    const bento_real_t slope = (bento_real_t)TRIANGLE_SLOPE * phase;
    bento_real_t value;

    if (wave == WAVE_SQUARE) {
        value = (phase < half) ? (bento_real_t)1 : (bento_real_t)-1;
    } else if (wave == WAVE_SINE) {
        const bento_real_t angle = full_turn * phase;

        value = (bento_real_t)sin((double)angle);
    } else if (wave == WAVE_TRIANGLE) {
        value = (phase < half) ? (slope - (bento_real_t)1)
                               : ((bento_real_t)TRIANGLE_TOP - slope);
    } else {
        value = ((bento_real_t)2 * phase) - (bento_real_t)1;
    }
    return value;
}

/* gen_wave(): 256 samples of the wave in int16 units, or LFSR noise
 * between -16000 and 16000. */
static void make_signal(analyzer_t *analyzer)
{
    const bento_real_t period =
        (bento_real_t)SAMPLE_RATE_HZ / (bento_real_t)FREQ_HZ;
    int32_t i;

    for (i = 0; i < FFT_N; i++) {
        int32_t value;

        if (analyzer->wave == WAVE_NOISE) {
            const uint32_t draw =
                lfsr_next(&analyzer->lfsr) % (uint32_t)NOISE_SPAN;

            value = (int32_t)draw - AMPLITUDE;
        } else {
            const bento_real_t within =
                (bento_real_t)fmod((double)i, (double)period);

            value = int_of(wave_at(analyzer->wave, within / period) *
                           (bento_real_t)AMPLITUDE);
        }
        g_spectrum.samples[i] = (float)value;
    }
}

/* The wave in time, 64 points; only when the wave changes, because the
 * picture stays the same. A point is sent and not answered. */
static bento_status_t draw_wave_time(const analyzer_t *analyzer)
{
    int32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0; (i < WAVE_POINTS) && (status == BENTO_OK); i++) {
        const int32_t index = i * SAMPLE_STEP;
        const int32_t sample = (int32_t)g_spectrum.samples[index];
        const int32_t sample_x100 = sample * WAVE_CHART_FULL;
        const bento_real_t scaled =
            (bento_real_t)sample_x100 / (bento_real_t)AMPLITUDE;

        (void)bento_ui_chart_set_next(
            analyzer->ui, analyzer->wave_chart, 0U, (int32_t)scaled);
        if ((i % YIELD_EVERY) == 0) {
            status = pause_ms(YIELD_MS);
        }
    }
    return status;
}

/* The highest bin after bin 0 (at least 1.0), and its number. */
static int32_t find_peak(bento_real_t *top)
{
    int32_t peak = 0;
    int32_t bin;

    *top = (bento_real_t)1;
    for (bin = 1; bin < MAGNITUDES; bin++) {
        if ((bento_real_t)g_spectrum.magnitudes[bin] > *top) {
            *top = (bento_real_t)g_spectrum.magnitudes[bin];
            peak = bin;
        }
    }
    return peak;
}

/* The spectrum as 64 points scaled to the peak, then the peak's
 * frequency; the bins are 187.5 Hz wide. */
static bento_status_t draw_spectrum(const analyzer_t *analyzer)
{
    const int32_t bin_width_hz =
        SAMPLE_RATE_HZ / FFT_N; /* 187.5, floored */
    char text[PEAK_TEXT_BYTES];
    bento_real_t top = (bento_real_t)1;
    const int32_t peak = find_peak(&top);
    int32_t bin;
    bento_status_t status = BENTO_OK;

    for (bin = 0; (bin < BINS) && (status == BENTO_OK); bin++) {
        const int32_t index = bin * BIN_STEP;
        const int32_t value =
            int_of(((bento_real_t)g_spectrum.magnitudes[index] *
                    (bento_real_t)PERCENT) /
                   top);

        (void)bento_ui_chart_set_next(
            analyzer->ui, analyzer->spectrum, 0U,
            (value <= PERCENT) ? value : PERCENT);
        if (((bin + 1) % YIELD_EVERY) == 0) {
            status = pause_ms(YIELD_MS);
        }
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "FFT: %" PRId32 " Hz (bin %" PRId32 " Hz)",
                       (peak * SAMPLE_RATE_HZ) / FFT_N, bin_width_hz);
        /* a setter's refusal is ignored, as MicroPython ignores it */
        (void)bento_ui_set_text(analyzer->ui, analyzer->peak_text, text,
                                (uint32_t)strlen(text));
    }
    return status;
}

/* redraw(): a new signal, its time picture when asked, its FFT and the
 * spectrum. */
static bento_status_t redraw(analyzer_t *analyzer, bool with_time)
{
    uint32_t bins = 0U;
    bento_status_t status = BENTO_OK;

    make_signal(analyzer);
    if (with_time) {
        status = draw_wave_time(analyzer);
    }
    if (status == BENTO_OK) {
        status = bento_dsp_fft_mag_f32(
            g_spectrum.samples, FFT_N, FFT_N, BENTO_DSP_FFT_DEFAULT,
            g_spectrum.scratch, BENTO_DSP_FFT_SCRATCH_PER_N * FFT_N,
            g_spectrum.magnitudes, MAGNITUDES, &bins);
    }
    if (status == BENTO_OK) {
        status = draw_spectrum(analyzer);
    }
    return status;
}

/* "gen: <wave> 1000 Hz"; one the menu cannot offer ends the program,
 * where MicroPython raises IndexError. */
static bento_status_t show_wave_name(const analyzer_t *analyzer)
{
    const int32_t freq_hz = FREQ_HZ;
    char text[SIGNAL_TEXT_BYTES];
    bento_status_t status = BENTO_E_INVALID_ARGUMENT;

    if ((analyzer->wave >= 0) && (analyzer->wave < WAVES)) {
        (void)snprintf(text, sizeof(text), "gen: %s %" PRId32 " Hz",
                       WAVE_NAMES[analyzer->wave], freq_hz);
        (void)bento_ui_set_text(analyzer->ui, analyzer->signal_text,
                                text, (uint32_t)strlen(text));
        status = BENTO_OK;
    }
    return status;
}

static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* One event: Run, Stop, or a new wave (which also needs a new time
 * picture, *changed). */
static bento_status_t handle_event(analyzer_t *analyzer,
                                   const bento_ui_event_t *event,
                                   bool *changed)
{
    bento_status_t status = BENTO_OK;

    if (event_is(event, BENTO_UI_EVENT_CLICKED, analyzer->run)) {
        analyzer->running = true;
    } else if (event_is(event, BENTO_UI_EVENT_CLICKED,
                        analyzer->stop)) {
        analyzer->running = false;
    } else if (event_is(event, BENTO_UI_EVENT_VALUE_CHANGED,
                        analyzer->menu)) {
        analyzer->wave = event->value;
        *changed = true;
        status = show_wave_name(analyzer);
    } else {
        /* any other event changes nothing */
    }
    return status;
}

/* One pass: the events, then a new spectrum while running or after a
 * new wave (redrawn even while Stop holds the picture). ui.poll()
 * answers no events when it fails, so a failed poll is not an error. */
static bento_status_t run_pass(analyzer_t *analyzer)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bool changed = false;
    bento_status_t status = BENTO_OK;

    if (bento_ui_poll(analyzer->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = handle_event(analyzer, &events[i], &changed);
    }
    if ((status == BENTO_OK) && (analyzer->running || changed)) {
        status = redraw(analyzer, changed);
    }
    return status;
}

/* One pass every LOOP_MS until RUN_MS has passed or Stop is pressed. */
static bento_status_t run_loop(analyzer_t *analyzer)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = run_pass(analyzer);
        if (status == BENTO_OK) {
            status = pause_ms(LOOP_MS);
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
    analyzer_t analyzer;
    bento_status_t status;

    (void)memset(&g_spectrum, 0, sizeof(g_spectrum));
    (void)memset(&analyzer, 0, sizeof(analyzer));
    analyzer.ui = bento_runtime_get_ui();
    analyzer.wave = WAVE_SINE;
    analyzer.running = true;
    analyzer.lfsr = LFSR_START;
    status = build_screen(&analyzer);
    if (status == BENTO_OK) {
        status = show_wave_name(&analyzer);
    }
    if (status == BENTO_OK) {
        status = redraw(&analyzer, true);
    }
    if (status == BENTO_OK) {
        /* lcd.print() ignores a console that is not ready (modlcd.c) */
        (void)bento_lcd_print("ex16: gen = คลื่นที่สร้าง, FFT = ที่วัดได้");
        (void)bento_lcd_print(
            "bin กว้าง 187.5 Hz ยอดจึงตกที่ 937 ไม่ใช่ 1000 พอดี");
        status = run_loop(&analyzer);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex16: done\n");
        status = show_finished(analyzer.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
