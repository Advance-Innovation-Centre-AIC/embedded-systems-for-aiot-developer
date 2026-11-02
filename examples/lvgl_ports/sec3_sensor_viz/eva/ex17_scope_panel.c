/*
 * ex17_scope_panel.c -- สโคปแผงกำหนดเอง (Custom Panel Scope)
 *
 * สโคปสามหน้า สลับด้วยปุ่มนำทางซ้าย: Scope แสดงคลื่นไซน์ 1000 Hz วิ่ง
 * ต่อเนื่อง Gen เป็นเครื่องกำเนิดคลื่นสี่เหลี่ยมที่ปรับความถี่ (10-500 Hz)
 * และ duty (0-100 %) ด้วยสไลเดอร์ และ FFT แสดงสเปกตรัมของคลื่นไซน์จาก
 * dsp.fft_mag() พร้อมความถี่ยอด สวิตช์ Run หยุดหรือให้ทุกหน้าวิ่ง
 * (ไฟ Run สว่างหรือหรี่ตาม) ทำงาน 3 นาทีแล้วจบ และวาดแถบ
 * "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * A three-page scope, switched with the navigation buttons on the left:
 * Scope shows a 1000 Hz sine scrolling on, Gen a square-wave generator
 * whose frequency (10-500 Hz) and duty (0-100 %) the sliders set, and
 * FFT the sine's spectrum from dsp.fft_mag() with its peak frequency.
 * The Run switch pauses or runs every page (the Run light dims or
 * brightens with it). The program ends after three minutes and then
 * draws a "finished" band over the bottom rows.
 *
 * C twin of ex17_scope_panel.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/eva/ex17_scope_panel.py), from the library twin
 * aiot-lvgl-sec3-ex17-scope-panel. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. What C changes: Python's int is int32_t
 * here; the page names "scp", "gen", "fft" are an enum; the FFT signal
 * is a float array and its work area the caller's; sin() and fmod()
 * work in double and are rounded to the board's float width.
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

/* Screen layout on the 792 x 398 canvas. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    TITLE_FONT_PX = 16,
    TITLE_Y = 8,
    LED_X = 700,
    LED_Y = 8,
    LED_SIZE = 24,
    RUN_TEXT_X = 734,
    RUN_TEXT_Y = 12,
    NAV_X = 8,
    NAV_Y = 48,
    NAV_STEP = 64,
    NAV_W = 112,
    NAV_H = 56,
    SWITCH_X = 24,
    SWITCH_Y = 248,
    SWITCH_W = 80,
    SWITCH_H = 40,
    PAGE_X = 136,
    PAGE_Y = 48,
    PAGE_W = 644,
    CHART_H = 250,
    INFO_X = 340,
    INFO_Y = 312,
    GENERATOR_PANEL_H = 290,
    GENERATOR_EDGE_PX = 8,
    GENERATOR_CHART_X = 20,
    GENERATOR_CHART_Y = 14,
    GENERATOR_CHART_W = 600,
    GENERATOR_CHART_H = 150,
    GENERATOR_LABEL_X = 20,
    GENERATOR_SLIDER_X = 210,
    GENERATOR_SLIDER_W = 280,
    GENERATOR_SLIDER_H = 22,
    GENERATOR_VALUE_X = 520,
    FREQ_ROW_Y = 180,
    DUTY_ROW_Y = 230,
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

/* The pages, the waves and the FFT. */
enum {
    PAGE_SCOPE = 0,
    PAGE_GENERATOR = 1,
    PAGE_FFT = 2,
    PAGES = 3,
    WAVE_SQUARE = 0,
    WAVE_SINE = 1,
    SCOPE_RATE_HZ = 48000,
    SCOPE_FREQ_HZ = 1000,
    GENERATOR_RATE_HZ =
        10000, /* a low rate makes the duty easy to see */
    SCOPE_POINTS = 200,
    GENERATOR_POINTS = 100,
    SCOPE_CHUNK = 8, /* points a round */
    GENERATOR_CHUNK = 6,
    CHART_MAX = 100,
    CHART_MIDDLE = 50,
    WAVE_AMPLITUDE = 20,
    SAMPLE_AMPLITUDE = 16000,
    TRIANGLE_SLOPE = 4, /* up from -1 to 1 in half a period */
    TRIANGLE_TOP = 3,   /* and down again: 3 - 4 * phase */
    HALF_DUTY = 50,
    PERCENT = 100,
    FREQ_MIN_HZ = 10,
    FREQ_MAX_HZ = 500,
    GENERATOR_START_HZ = 100,
    FFT_N = 256,
    MAGNITUDES = FFT_N / 2,
    BINS = 64,
    BIN_STEP = MAGNITUDES / BINS,
    LED_FULL = 255,
    LED_DIM = 80,
    TEXT_BYTES = 30 /* "Dominant: ", an int32_t, " Hz" and the NUL */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,
    FFT_LOOP_MS = 150,
    RUN_MS = 180000 /* the example ends after three minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x101018,
    COLOR_TITLE = 0x00FF88,
    COLOR_LED = 0x00FF00,
    COLOR_TEXT = 0xCCCCCC,
    COLOR_VALUE = 0xFFFFFF,
    COLOR_ACTIVE = 0x2196F3,
    COLOR_IDLE = 0x333333,
    COLOR_SCOPE = 0x00FF00,
    COLOR_GENERATOR_PANEL = 0x181820,
    COLOR_GENERATOR_EDGE = 0x333333,
    COLOR_GENERATOR = 0xFFAA00,
    COLOR_FFT = 0x00FFFF,
    COLOR_PEAK = 0xFFFF00,
    COLOR_FOOTER = 0x666666
};

/* The "finished" band's colours, 0xRRGGBB. */
enum {
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

/* The FFT's signal, work area and magnitudes: 3584 bytes, too big for
 * the 8 KB program stack, so at file scope, zeroed by the loader and
 * set again at the start of main() (CLEAN_C.md C9, deviation
 * BENTO-MISRA-E03/E04). */
typedef struct {
    float samples[FFT_N];
    float scratch[BENTO_DSP_FFT_SCRATCH_PER_N * FFT_N];
    float magnitudes[MAGNITUDES];
} fft_buffers_t;

extern fft_buffers_t g_fft;
fft_buffers_t g_fft;

/* The widgets the program changes, and the scope's state. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t run_led;
    bento_ui_id_t nav[PAGES];
    bento_ui_id_t run_switch;
    bento_ui_id_t scope_chart;
    bento_ui_id_t scope_info;
    bento_ui_id_t generator_panel;
    bento_ui_id_t generator_chart;
    bento_ui_id_t freq_slider;
    bento_ui_id_t freq_text;
    bento_ui_id_t duty_slider;
    bento_ui_id_t duty_text;
    bento_ui_id_t fft_chart;
    bento_ui_id_t fft_info;
    int32_t page;
    bool running;
    int32_t generator_freq_hz;
    int32_t generator_duty;
    int32_t scope_phase;
    int32_t generator_phase;
} scope_panel_t;

/* A label's place and look: the original's x=, y=, color=, value= and
 * parent= (0 is the screen itself). */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
    bento_ui_id_t parent;
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
    args.parent = style->parent;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A label centred on the screen, as the original's cx() places it. */
static bento_status_t add_centered(bento_ui_t *ui, const char *text,
                                   int16_t y, uint32_t color,
                                   int32_t font_px,
                                   bento_ui_id_t *out_id)
{
    const label_style_t style = {centered_x(text, font_px), y, color,
                                 font_px, 0U};

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

/* A chart of `points` points; .prop() never raises in MicroPython. */
static bento_status_t add_chart(bento_ui_t *ui,
                                const bento_ui_widget_args_t *place,
                                int32_t points, bento_ui_id_t *chart)
{
    bento_ui_widget_args_t args = *place;
    bento_status_t status;

    args.min_val = 0;
    args.max_val = CHART_MAX;
    status = bento_ui_chart_create(ui, &args, chart);
    if (status == BENTO_OK) {
        (void)bento_ui_set_prop(ui, *chart, BENTO_UI_PROP_CHART_POINTS,
                                points);
    }
    return status;
}

/* The title, the Run light, the three page buttons and the Run
 * switch. */
static bento_status_t add_header_and_nav(scope_panel_t *panel)
{
    static const char *const nav_names[PAGES] = {"Scope", "Gen", "FFT"};
    const label_style_t run = {RUN_TEXT_X, RUN_TEXT_Y, COLOR_TEXT,
                               FONT_PX, 0U};
    bento_ui_widget_args_t args =
        place_at(LED_X, LED_Y, LED_SIZE, LED_SIZE, COLOR_LED);
    bento_ui_id_t unused;
    int32_t page;
    bento_status_t status =
        add_centered(panel->ui, "AIC-EEC Scope v1.0", TITLE_Y,
                     COLOR_TITLE, TITLE_FONT_PX, &unused);

    args.font_px = LED_FULL;
    if (status == BENTO_OK) {
        status = bento_ui_led_create(panel->ui, &args, &panel->run_led);
    }
    if (status == BENTO_OK) {
        status = add_label(panel->ui, "Run", &run, &unused);
    }
    for (page = 0; (page < PAGES) && (status == BENTO_OK); page++) {
        args = place_at(
            NAV_X, (int16_t)(NAV_Y + (page * NAV_STEP)), NAV_W, NAV_H,
            (page == PAGE_SCOPE) ? COLOR_ACTIVE : COLOR_IDLE);
        args.text = nav_names[page];
        args.text_len = (uint32_t)strlen(nav_names[page]);
        args.font_px = TITLE_FONT_PX;
        status =
            bento_ui_button_create(panel->ui, &args, &panel->nav[page]);
    }
    if (status == BENTO_OK) {
        args = place_at(SWITCH_X, SWITCH_Y, SWITCH_W, SWITCH_H, 0U);
        args.font_px = 1;
        status = bento_ui_switch_create(panel->ui, &args,
                                        &panel->run_switch);
    }
    return status;
}

/* The Scope page: the chart and its info line. */
static bento_status_t add_scope_page(scope_panel_t *panel)
{
    const bento_ui_widget_args_t chart =
        place_at(PAGE_X, PAGE_Y, PAGE_W, CHART_H, COLOR_SCOPE);
    const label_style_t info = {INFO_X, INFO_Y, COLOR_TEXT, FONT_PX,
                                0U};
    bento_status_t status =
        add_chart(panel->ui, &chart, SCOPE_POINTS, &panel->scope_chart);

    if (status == BENTO_OK) {
        status = add_label(panel->ui, "Wave: Sine    1000 Hz", &info,
                           &panel->scope_info);
    }
    return status;
}

/* One slider row of the Gen page: caption, slider and value text. */
static bento_status_t add_generator_row(scope_panel_t *panel,
                                        const char *caption, int16_t y,
                                        const int32_t *range,
                                        bento_ui_id_t *slider)
{
    const label_style_t style = {GENERATOR_LABEL_X, y, COLOR_TEXT,
                                 FONT_PX, panel->generator_panel};
    bento_ui_widget_args_t args =
        place_at(GENERATOR_SLIDER_X, y, GENERATOR_SLIDER_W,
                 GENERATOR_SLIDER_H, 0U);
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(panel->ui, caption, &style, &unused);

    args.min_val = range[0];
    args.max_val = range[1];
    args.font_px = range[2];
    args.parent = panel->generator_panel;
    if (status == BENTO_OK) {
        status = bento_ui_slider_create(panel->ui, &args, slider);
    }
    return status;
}

/* A value text at the right of a Gen row. */
static bento_status_t add_generator_value(scope_panel_t *panel,
                                          const char *text, int16_t y,
                                          bento_ui_id_t *label)
{
    const label_style_t style = {GENERATOR_VALUE_X, y, COLOR_VALUE,
                                 FONT_PX, panel->generator_panel};

    return add_label(panel->ui, text, &style, label);
}

/* The Gen page: a panel holding its chart and two slider rows. */
static bento_status_t add_generator_page(scope_panel_t *panel)
{
    const int32_t freq_range[3] = {FREQ_MIN_HZ, FREQ_MAX_HZ,
                                   GENERATOR_START_HZ};
    const int32_t duty_range[3] = {0, PERCENT, HALF_DUTY};
    bento_ui_widget_args_t args =
        place_at(PAGE_X, PAGE_Y, PAGE_W, GENERATOR_PANEL_H,
                 COLOR_GENERATOR_PANEL);
    bento_status_t status;

    args.min_val =
        COLOR_GENERATOR_EDGE; /* a panel's min= is its edge colour */
    args.max_val = GENERATOR_EDGE_PX;
    args.font_px = 1;
    status = bento_ui_panel_create(panel->ui, &args,
                                   &panel->generator_panel);
    if (status == BENTO_OK) {
        args = place_at(GENERATOR_CHART_X, GENERATOR_CHART_Y,
                        GENERATOR_CHART_W, GENERATOR_CHART_H,
                        COLOR_GENERATOR);
        args.parent = panel->generator_panel;
        status = add_chart(panel->ui, &args, GENERATOR_POINTS,
                           &panel->generator_chart);
    }
    if (status == BENTO_OK) {
        status =
            add_generator_row(panel, "Freq (10-500 Hz)", FREQ_ROW_Y,
                              freq_range, &panel->freq_slider);
    }
    if (status == BENTO_OK) {
        status = add_generator_value(panel, "100 Hz", FREQ_ROW_Y,
                                     &panel->freq_text);
    }
    if (status == BENTO_OK) {
        status = add_generator_row(panel, "Duty (%)", DUTY_ROW_Y,
                                   duty_range, &panel->duty_slider);
    }
    if (status == BENTO_OK) {
        status = add_generator_value(panel, "50 %", DUTY_ROW_Y,
                                     &panel->duty_text);
    }
    return status;
}

/* The FFT page: the 64-bin chart and the peak text. */
static bento_status_t add_fft_page(scope_panel_t *panel)
{
    const bento_ui_widget_args_t chart =
        place_at(PAGE_X, PAGE_Y, PAGE_W, CHART_H, COLOR_FFT);
    const label_style_t info = {INFO_X, INFO_Y, COLOR_PEAK, FONT_PX,
                                0U};
    bento_status_t status =
        add_chart(panel->ui, &chart, BINS, &panel->fft_chart);

    if (status == BENTO_OK) {
        status = add_label(panel->ui, "Dominant: -- Hz", &info,
                           &panel->fft_info);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(scope_panel_t *panel)
{
    bento_ui_id_t unused;
    bento_status_t status = clear_screen(panel->ui);

    if (status == BENTO_OK) {
        status = add_background(panel->ui);
    }
    if (status == BENTO_OK) {
        status = add_header_and_nav(panel);
    }
    if (status == BENTO_OK) {
        status = add_scope_page(panel);
    }
    if (status == BENTO_OK) {
        status = add_generator_page(panel);
    }
    if (status == BENTO_OK) {
        status = add_fft_page(panel);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(panel->ui,
                         "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                         "Burapha University",
                         FOOTER_Y, COLOR_FOOTER, FONT_PX, &unused);
    }
    return status;
}

/* Shows one page and hides the others, and lights its button. .hide(),
 * .show() and .color() are setters whose refusal MicroPython ignores
 * (modui.c). */
static void show_page(const scope_panel_t *panel, int32_t page)
{
    const uint8_t on_scope = (page == PAGE_SCOPE) ? 1U : 0U;
    const uint8_t on_fft = (page == PAGE_FFT) ? 1U : 0U;
    int32_t nav;

    (void)bento_ui_set_visible(panel->ui, panel->scope_chart, on_scope);
    (void)bento_ui_set_visible(panel->ui, panel->scope_info, on_scope);
    (void)bento_ui_set_visible(panel->ui, panel->generator_panel,
                               (page == PAGE_GENERATOR) ? 1U : 0U);
    (void)bento_ui_set_visible(panel->ui, panel->fft_chart, on_fft);
    (void)bento_ui_set_visible(panel->ui, panel->fft_info, on_fft);
    for (nav = 0; nav < PAGES; nav++) {
        (void)bento_ui_set_color(panel->ui, panel->nav[nav],
                                 (nav == page) ? COLOR_ACTIVE
                                               : COLOR_IDLE);
    }
}

/* The wave between -1 and 1 at phase (0 to 1 within one period); the
 * square is high for duty percent of the period. */
static bento_real_t wave_at(int32_t wave, bento_real_t phase,
                            int32_t duty)
{
    const bento_real_t half = (bento_real_t)0.5;
    const bento_real_t full_turn =
        (bento_real_t)(2.0 * 3.141592653589793);
    const bento_real_t high_part =
        (bento_real_t)duty / (bento_real_t)PERCENT;
    bento_real_t value;

    if (wave == WAVE_SQUARE) {
        value =
            (phase < high_part) ? (bento_real_t)1 : (bento_real_t)-1;
    } else if (wave == WAVE_SINE) {
        value = (bento_real_t)sin((double)(full_turn * phase));
    } else {
        const bento_real_t slope = (bento_real_t)TRIANGLE_SLOPE * phase;

        value = (phase < half) ? (slope - (bento_real_t)1)
                               : ((bento_real_t)TRIANGLE_TOP - slope);
    }
    return value;
}

/* The phase (0 to 1) of sample `sample` at freq_hz and rate_hz. */
static bento_real_t phase_of(int32_t sample, int32_t freq_hz,
                             int32_t rate_hz)
{
    const bento_real_t period =
        (bento_real_t)rate_hz / (bento_real_t)freq_hz;
    const bento_real_t within =
        (bento_real_t)fmod((double)sample, (double)period);

    return within / period;
}

/* What push_chunk() draws: a wave at a rate and a duty. */
typedef struct {
    int32_t wave;
    int32_t freq_hz;
    int32_t rate_hz;
    int32_t duty;
    int32_t points;
} chunk_t;

/* A few new points on a chart, the phase kept across rounds; a point
 * is sent and not answered, as in MicroPython. Answers the new phase. */
static int32_t push_chunk(bento_ui_t *ui, bento_ui_id_t chart,
                          const chunk_t *chunk, int32_t phase)
{
    int32_t i;

    for (i = 0; i < chunk->points; i++) {
        const bento_real_t value =
            wave_at(chunk->wave,
                    phase_of(phase + i, chunk->freq_hz, chunk->rate_hz),
                    chunk->duty);

        (void)bento_ui_chart_set_next(
            ui, chart, 0U,
            CHART_MIDDLE +
                (int32_t)(value * (bento_real_t)WAVE_AMPLITUDE));
    }
    return phase + chunk->points;
}

/* The highest bin after bin 0 (at least 1.0), and its number. */
static int32_t find_peak(bento_real_t *top)
{
    int32_t peak = 0;
    int32_t bin;

    *top = (bento_real_t)1;
    for (bin = 1; bin < MAGNITUDES; bin++) {
        if ((bento_real_t)g_fft.magnitudes[bin] > *top) {
            *top = (bento_real_t)g_fft.magnitudes[bin];
            peak = bin;
        }
    }
    return peak;
}

/* The scope's sine, its FFT, 64 bins scaled to the peak, and the peak's
 * frequency. A failed FFT ends the program, as it raises there. */
static bento_status_t fft_redraw(const scope_panel_t *panel)
{
    char text[TEXT_BYTES];
    bento_real_t top = (bento_real_t)1;
    uint32_t bins = 0U;
    int32_t i;
    bento_status_t status;

    for (i = 0; i < FFT_N; i++) {
        const bento_real_t value = wave_at(
            WAVE_SINE, phase_of(i, SCOPE_FREQ_HZ, SCOPE_RATE_HZ),
            HALF_DUTY);

        g_fft.samples[i] =
            (float)(int32_t)(value * (bento_real_t)SAMPLE_AMPLITUDE);
    }
    status = bento_dsp_fft_mag_f32(g_fft.samples, FFT_N, FFT_N,
                                   BENTO_DSP_FFT_DEFAULT, g_fft.scratch,
                                   BENTO_DSP_FFT_SCRATCH_PER_N * FFT_N,
                                   g_fft.magnitudes, MAGNITUDES, &bins);
    if (status == BENTO_OK) {
        const int32_t peak = find_peak(&top);

        for (i = 0; i < BINS; i++) {
            const int32_t index = i * BIN_STEP;
            const int32_t value =
                (int32_t)(((bento_real_t)g_fft.magnitudes[index] *
                           (bento_real_t)PERCENT) /
                          top);

            (void)bento_ui_chart_set_next(
                panel->ui, panel->fft_chart, 0U,
                (value <= PERCENT) ? value : PERCENT);
        }
        (void)snprintf(text, sizeof(text), "Dominant: %" PRId32 " Hz",
                       (peak * SCOPE_RATE_HZ) / FFT_N);
        (void)bento_ui_set_text(panel->ui, panel->fft_info, text,
                                (uint32_t)strlen(text));
    }
    return status;
}

/* A new value text: "<value><unit>"; a setter's refusal is ignored. */
static void show_value(bento_ui_t *ui, bento_ui_id_t label,
                       int32_t value, const char *unit)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%d%s", (int)value, unit);
    (void)bento_ui_set_text(ui, label, text, (uint32_t)strlen(text));
}

/* A page button: that page is shown. */
static void handle_click(scope_panel_t *panel, bento_ui_id_t widget)
{
    int32_t page;

    for (page = 0; page < PAGES; page++) {
        if (widget == panel->nav[page]) {
            panel->page = page;
            show_page(panel, page);
        }
    }
}

static void handle_event(scope_panel_t *panel,
                         const bento_ui_event_t *event)
{
    if (event->type == BENTO_UI_EVENT_CLICKED) {
        handle_click(panel, event->id);
    } else if ((event->type == BENTO_UI_EVENT_TOGGLED) &&
               (event->id == panel->run_switch)) {
        panel->running = (event->value == 1);
        (void)bento_ui_set_prop(panel->ui, panel->run_led,
                                BENTO_UI_PROP_LED_BRIGHTNESS,
                                panel->running ? LED_FULL : LED_DIM);
    } else if ((event->type == BENTO_UI_EVENT_VALUE_CHANGED) &&
               (event->id == panel->freq_slider)) {
        panel->generator_freq_hz = event->value;
        show_value(panel->ui, panel->freq_text, event->value, " Hz");
    } else if ((event->type == BENTO_UI_EVENT_VALUE_CHANGED) &&
               (event->id == panel->duty_slider)) {
        panel->generator_duty = event->value;
        show_value(panel->ui, panel->duty_text, event->value, " %");
    } else {
        /* any other event changes nothing */
    }
}

/* The touch events; ui.poll() answers no events when it fails, so a
 * failed poll is not an error here. */
static void read_events(scope_panel_t *panel)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(panel->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        handle_event(panel, &events[i]);
    }
}

/* While Run is on, the page that is shown moves on. */
static bento_status_t advance_page(scope_panel_t *panel)
{
    bento_status_t status = BENTO_OK;

    if (panel->page == PAGE_SCOPE) {
        const chunk_t scope = {WAVE_SINE, SCOPE_FREQ_HZ, SCOPE_RATE_HZ,
                               HALF_DUTY, SCOPE_CHUNK};

        panel->scope_phase = push_chunk(panel->ui, panel->scope_chart,
                                        &scope, panel->scope_phase);
    } else if (panel->page == PAGE_GENERATOR) {
        const chunk_t generator = {
            WAVE_SQUARE, panel->generator_freq_hz, GENERATOR_RATE_HZ,
            panel->generator_duty, GENERATOR_CHUNK};

        panel->generator_phase =
            push_chunk(panel->ui, panel->generator_chart, &generator,
                       panel->generator_phase);
    } else {
        status = fft_redraw(panel); /* the FFT takes about 1 ms in C */
    }
    return status;
}

/* One pass every 100 ms (150 ms on the FFT page) until RUN_MS has
 * passed or Stop is pressed. */
static bento_status_t run_loop(scope_panel_t *panel)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        read_events(panel);
        if (panel->running) {
            status = advance_page(panel);
        }
        if (status == BENTO_OK) {
            status = bento_time_sleep_ms(
                (panel->page == PAGE_FFT) ? FFT_LOOP_MS : LOOP_MS);
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
                                 END_FONT_PX, 0U};
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
    scope_panel_t panel;
    bento_status_t status;

    (void)memset(&g_fft, 0, sizeof(g_fft));
    (void)memset(&panel, 0, sizeof(panel));
    panel.ui = bento_runtime_get_ui();
    panel.page = PAGE_SCOPE;
    panel.running = true;
    panel.generator_freq_hz = GENERATOR_START_HZ;
    panel.generator_duty = HALF_DUTY;
    status = build_screen(&panel);
    if (status == BENTO_OK) {
        show_page(&panel, PAGE_SCOPE);
        /* lcd.print() ignores a console that is not ready (modlcd.c) */
        (void)bento_lcd_print("sec3 ex17: สวิตช์ Run = ทุกหน้าวิ่งต่อเนื่อง");
        status = run_loop(&panel);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex17: done\n");
        status = show_finished(panel.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
