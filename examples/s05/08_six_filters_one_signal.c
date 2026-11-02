/*
 * 08_six_filters_one_signal.c -- dsp: ตัวกรองหกตัว ท่าเดียวกันหมด
 *
 * สัญญาณทดสอบเดียว (ฐาน 40 ขั้นบันไดขึ้น 60 คลื่นสั่นเล็ก ๆ และหนามแหลมสองจุด)
 * ป้อนเข้าตัวกรองหกตัวพร้อมกันทุก 200 ms: EMA SMA LPF HPF Median Kalman1D
 * กราฟวาดค่าดิบกับตัวกรองที่เลือก (กดตัวถัดไป) คอลัมน์ขวาแสดง value() ของทั้งหก
 * ปุ่ม reset ล้างความจำทุกตัว ปุ่มออกจบโปรแกรม HPF คนละเรื่องกับอีกห้าตัว
 *
 * One test signal (a base of 40, a step up to 60, a small wobble and
 * two sharp spikes) feeds six filters at once every 200 ms: EMA, SMA,
 * LPF, HPF, Median and Kalman1D. The chart draws the raw signal and
 * the filter chosen with "next"; the right column shows all six
 * value()s. "reset" clears every filter's memory, "exit" ends the
 * program. HPF is a different story from the other five.
 *
 * Twin of 08_six_filters_one_signal.py
 * (embedded-systems-for-aiot-developer/examples/s05/08_six_filters_one_signal.py),
 * rewritten from the library twin aiot-s05-08-six-filters-one-signal.
 * Style: CLEAN_C.md. The six dsp objects are bento_dsp_*_state_t held
 * in the program's memory; repr() of a filter is its state's window
 * and count in moddsp's own words. The reTerminal's MicroPython has
 * no dsp module (the original raises ImportError there); this C twin
 * runs there too.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_dsp.h"
#include "bento_fmt.h"
#include "bento_runtime.h"

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add, or a value differs in the last digit. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

enum {
    DT_MS = 200,   /* the loop period used in class: 5 Hz */
    N_POINTS = 60, /* points in one round before it starts again */
    STEP_AT = 20,
    WOBBLE_COUNT = 8,
    SPIKE_COUNT = 2,
    SMA_WINDOW = 8,
    MEDIAN_WINDOW = 5,
    ASKED_SMA = 200, /* asked for, to show the silent clamp */
    ASKED_MEDIAN = 4,
    CHART_MAX = 110,
    ROW_X = 488,
    ROW_Y = 100,
    ROW_PITCH = 24,
    FONT_ROW_PX = 16,
    AUTO = -1,       /* no w= or h= given: the widget's own size */
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The six filters, in the order they are shown. */
enum {
    FILTER_EMA,
    FILTER_SMA,
    FILTER_LPF,
    FILTER_HPF,
    FILTER_MEDIAN,
    FILTER_KALMAN,
    FILTER_COUNT
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_CHOSEN =
        0xE5484D,   /* the chosen filter's line, never changed */
    COLOR_THEME = 0 /* no color= given */
};

/* The widgets, in the order they are made; the chosen filter's series
 * comes after the chart, the six value rows after W_ROWS_CAPTION. */
enum {
    W_TITLE,
    W_CHART,
    W_RAW_NOTE,
    W_CHOSEN,
    W_ROWS_CAPTION,
    W_HPF_CARD,
    W_HPF_LINE_1,
    W_HPF_LINE_2,
    W_HPF_LINE_3,
    W_NEXT,
    W_RESET,
    W_EXIT,
    W_COUNT
};

/* The sample rate of the 200 ms loop, and the cut of LPF and HPF:
 * both must be told the same. */
static const float FS_HZ = 5.0F;
static const float CUTOFF_HZ = 0.4F;

/* The names shown and the colours of the value rows, filter order. */
static const char *const NAMES[FILTER_COUNT] = {
    "EMA a=0.2", "SMA w=8",    "LPF 0.4Hz",
    "HPF 0.4Hz", "Median w=5", "Kalman r=4"};
static const uint32_t COLORS[FILTER_COUNT] = {
    0xFF5555U, 0x50D890U, 0x00BFFFU, 0xFFD24AU, 0xB388FFU, 0xFF9800U};

/* Every filter keeps a memory inside, so all are made once, outside
 * the loop. */
typedef struct {
    bento_dsp_ema_state_t ema;
    bento_dsp_sma_state_t sma;
    bento_dsp_lpf_state_t lpf;
    bento_dsp_hpf_state_t hpf;
    bento_dsp_median_state_t median;
    bento_dsp_kalman1d_state_t kalman;
} filters_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
    bento_ui_id_t rows[FILTER_COUNT];
    uint8_t chosen_series;
    int32_t chosen;
    bool running;
    filters_t filters;
} bench_t;

/* One widget as the original makes it: its factory, its text and its
 * keyword arguments. value= is a Label's or Button's font size and a
 * control's start value; min= and max= are a control's range (a
 * Panel's border colour and corner radius). */
typedef struct {
    uint8_t type;
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value;
    int32_t min;
    int32_t max;
} widget_spec_t;

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t widget,
                      const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_widget(bento_ui_t *ui,
                                 const widget_spec_t *spec,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    if (spec->text != NULL) {
        args.text = spec->text;
        args.text_len = (uint32_t)strlen(spec->text);
    }
    args.x = spec->x;
    args.y = spec->y;
    args.w = spec->w;
    args.h = spec->h;
    args.color = spec->color;
    args.font_px = spec->value;
    args.min_val = spec->min;
    args.max_val = spec->max;
    return bento_ui_create(ui, spec->type, &args, out_id);
}

/* Makes the widgets in table order (creation order is drawing order)
 * and keeps each id; the first refusal ends it, as a raise does in
 * MicroPython. */
static bento_status_t add_widgets(bento_ui_t *ui,
                                  const widget_spec_t *table,
                                  uint32_t count, bento_ui_id_t *ids)
{
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = add_widget(ui, &table[i], &ids[i]);
    }
    return status;
}

/* The test signal: a base of 40, a step up to 60 in the middle, a
 * small wobble all along and two lone spikes, so each filter meets
 * what it is good at. */
static bento_real_t signal_at(int32_t point)
{
    static const bento_real_t wobble[WOBBLE_COUNT] = {
        (bento_real_t)0.0,  (bento_real_t)1.2,  (bento_real_t)-0.9,
        (bento_real_t)0.6,  (bento_real_t)-1.4, (bento_real_t)1.0,
        (bento_real_t)-0.5, (bento_real_t)0.8};
    static const int32_t spikes[SPIKE_COUNT] = {34, 47};
    const bento_real_t low = (bento_real_t)40.0;
    const bento_real_t high = (bento_real_t)60.0;
    const bento_real_t spike = (bento_real_t)35.0;
    const bento_real_t base = (point < STEP_AT) ? low : high;
    bento_real_t value = base + wobble[point % WOBBLE_COUNT];
    uint32_t i;

    for (i = 0U; i < (uint32_t)SPIKE_COUNT; i++) {
        if (spikes[i] == point) {
            value = value + spike; /* not real: who eats it? */
        }
    }
    return value;
}

/* The smoothing three: EMA, SMA and the 0.4 Hz low-pass on a 5 Hz
 * loop. */
static bento_status_t start_smoothers(filters_t *filters)
{
    bento_dsp_ema_config_t ema;
    bento_dsp_sma_config_t sma;
    bento_dsp_lpf_config_t lpf;
    const float alpha = 0.2F;
    bento_status_t status = bento_dsp_ema_config_init(&ema);

    if (status == BENTO_OK) {
        ema.alpha = alpha;
        status = bento_dsp_ema_state_init(&filters->ema, &ema);
    }
    if (status == BENTO_OK) {
        status = bento_dsp_sma_config_init(&sma);
    }
    if (status == BENTO_OK) {
        sma.window = SMA_WINDOW;
        status = bento_dsp_sma_state_init(&filters->sma, &sma);
    }
    if (status == BENTO_OK) {
        status = bento_dsp_lpf_config_init(&lpf);
    }
    if (status == BENTO_OK) {
        lpf.cutoff_hz = CUTOFF_HZ;
        lpf.fs_hz = FS_HZ;
        status = bento_dsp_lpf_state_init(&filters->lpf, &lpf);
    }
    return status;
}

/* The other three: the same 0.4 Hz cut keeping the fast part (HPF), a
 * median of 5, and a Kalman that trusts the reading little (r high),
 * so it is very calm. */
static bento_status_t start_others(filters_t *filters)
{
    bento_dsp_hpf_config_t hpf;
    bento_dsp_median_config_t median;
    bento_dsp_kalman1d_config_t kalman;
    const float process_noise = 0.05F;
    const float reading_noise = 4.0F; /* trusts the reading little */
    bento_status_t status = bento_dsp_hpf_config_init(&hpf);

    if (status == BENTO_OK) {
        hpf.cutoff_hz = CUTOFF_HZ;
        hpf.fs_hz = FS_HZ;
        status = bento_dsp_hpf_state_init(&filters->hpf, &hpf);
    }
    if (status == BENTO_OK) {
        status = bento_dsp_median_config_init(&median);
    }
    if (status == BENTO_OK) {
        median.window = MEDIAN_WINDOW;
        status = bento_dsp_median_state_init(&filters->median, &median);
    }
    if (status == BENTO_OK) {
        status = bento_dsp_kalman1d_config_init(&kalman);
    }
    if (status == BENTO_OK) {
        kalman.q = process_noise;
        kalman.r = reading_noise;
        status =
            bento_dsp_kalman1d_state_init(&filters->kalman, &kalman);
    }
    return status;
}

/* One sample into one filter; it answers the filtered value at once. */
static float update_filter(filters_t *filters, int32_t which,
                           float sample)
{
    float output = 0.0F;

    if (which == FILTER_EMA) {
        (void)bento_dsp_ema_state_update(&filters->ema, sample,
                                         &output);
    } else if (which == FILTER_SMA) {
        (void)bento_dsp_sma_state_update(&filters->sma, sample,
                                         &output);
    } else if (which == FILTER_LPF) {
        (void)bento_dsp_lpf_state_update(&filters->lpf, sample,
                                         &output);
    } else if (which == FILTER_HPF) {
        (void)bento_dsp_hpf_state_update(&filters->hpf, sample,
                                         &output);
    } else if (which == FILTER_MEDIAN) {
        (void)bento_dsp_median_state_update(&filters->median, sample,
                                            &output);
    } else {
        (void)bento_dsp_kalman1d_state_update(&filters->kalman, sample,
                                              &output);
    }
    return output;
}

/* .value() feeds nothing in: it asks for the latest value again, so it
 * is safe to call as often as wanted. */
static float filter_value(const filters_t *filters, int32_t which)
{
    float output = 0.0F;

    if (which == FILTER_EMA) {
        (void)bento_dsp_ema_state_value(&filters->ema, &output);
    } else if (which == FILTER_SMA) {
        (void)bento_dsp_sma_state_value(&filters->sma, &output);
    } else if (which == FILTER_LPF) {
        (void)bento_dsp_lpf_state_value(&filters->lpf, &output);
    } else if (which == FILTER_HPF) {
        (void)bento_dsp_hpf_state_value(&filters->hpf, &output);
    } else if (which == FILTER_MEDIAN) {
        (void)bento_dsp_median_state_value(&filters->median, &output);
    } else {
        (void)bento_dsp_kalman1d_state_value(&filters->kalman, &output);
    }
    return output;
}

/* .reset() clears every memory at once; next round each starts again,
 * so its value jumps straight to the raw one. */
static void reset_filters(filters_t *filters)
{
    (void)bento_dsp_ema_state_reset(&filters->ema);
    (void)bento_dsp_sma_state_reset(&filters->sma);
    (void)bento_dsp_lpf_state_reset(&filters->lpf);
    (void)bento_dsp_hpf_state_reset(&filters->hpf);
    (void)bento_dsp_median_state_reset(&filters->median);
    (void)bento_dsp_kalman1d_state_reset(&filters->kalman);
}

/* The silent clamp proved by the filter itself: its repr tells the
 * window it really got. */
static void print_clamps(void)
{
    bento_dsp_sma_config_t sma_config;
    bento_dsp_sma_state_t sma;
    bento_dsp_median_config_t median_config;
    bento_dsp_median_state_t median;
    char text[TEXT_BYTES];

    (void)bento_dsp_sma_config_init(&sma_config);
    sma_config.window = ASKED_SMA;
    if (bento_dsp_sma_state_init(&sma, &sma_config) == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "ขอ SMA window=200 ได้จริง: SMA(window=%" PRIu32
                       ", count=%" PRIu32 ")",
                       sma.window, sma.count);
        (void)bento_lcd_print(text);
    }
    (void)bento_dsp_median_config_init(&median_config);
    median_config.window = ASKED_MEDIAN;
    if (bento_dsp_median_state_init(&median, &median_config) ==
        BENTO_OK) {
        (void)snprintf(
            text, sizeof(text),
            "ขอ Median window=4 ได้จริง: Median(window=%" PRIu32
            ", count=%" PRIu32 ")",
            median.window, median.count);
        (void)bento_lcd_print(text);
    }
}

/* The labels, the HPF card and the buttons. The Panel comes before the
 * three lines on it, or it would paint over them; the card ends at
 * y = 340, above the Console's corner. */
static bento_status_t add_lower(bench_t *bench)
{
    static const widget_spec_t
        lower_widgets[(uint32_t)W_COUNT - (uint32_t)W_HPF_CARD] = {
            {UI_WIDGET_PANEL, NULL, 488, 252, 280, 88, COLOR_THEME, 0,
             0, 0},
            {UI_WIDGET_LABEL, "HPF คนละเรื่องกับอีกห้าตัว", 504, 254, AUTO,
             AUTO, COLOR_WARN, 20, 0, 0},
            {UI_WIDGET_LABEL, "เก็บส่วนที่เปลี่ยนเร็ว", 504, 282, AUTO, AUTO,
             COLOR_WARN, 20, 0, 0},
            {UI_WIDGET_LABEL, "ไม่ใช่ระดับของสัญญาณ", 504, 310, AUTO, AUTO,
             COLOR_WARN, 20, 0, 0},
            {UI_WIDGET_BUTTON, "ตัวถัดไป >", 24, 280, 128, 88,
             COLOR_ACCENT, 20, 0, 0},
            {UI_WIDGET_BUTTON, "reset ทั้งหมด", 184, 280, 160, 88,
             COLOR_ACCENT, 20, 0, 0},
            {UI_WIDGET_BUTTON, "ออก", 376, 280, 88, 88, COLOR_DIM, 20,
             0, 0}};

    return add_widgets(bench->ui, lower_widgets,
                       (uint32_t)W_COUNT - (uint32_t)W_HPF_CARD,
                       &bench->ids[W_HPF_CARD]);
}

/* The right column: all six value()s at once, which is why value()
 * exists. */
static bento_status_t add_rows(bench_t *bench)
{
    bento_ui_widget_args_t args;
    bento_status_t status = BENTO_OK;
    char text[TEXT_BYTES];
    int32_t i;

    for (i = 0; (i < FILTER_COUNT) && (status == BENTO_OK); i++) {
        (void)snprintf(text, sizeof(text), "%s  ----", NAMES[i]);
        bento_ui_widget_args_init(&args);
        args.text = text;
        args.text_len = (uint32_t)strlen(text);
        args.x = ROW_X;
        args.y = (int16_t)(ROW_Y + (i * ROW_PITCH));
        args.color = COLORS[i];
        args.font_px = FONT_ROW_PX;
        status =
            bento_ui_label_create(bench->ui, &args, &bench->rows[i]);
    }
    return status;
}

/* The chosen filter's line keeps one colour; the label says whose it
 * is, since a line cannot change colour midway. */
static bento_status_t add_screen(bench_t *bench)
{
    static const widget_spec_t upper_widgets[W_HPF_CARD] = {
        {UI_WIDGET_LABEL, "dsp: ตัวกรองหกตัว ท่าเดียวกันหมด", 24, 24, AUTO,
         AUTO, COLOR_THEME, 24, 0, 0},
        {UI_WIDGET_CHART, NULL, 24, 68, 448, 160, COLOR_DIM, 0, 0,
         CHART_MAX},
        {UI_WIDGET_LABEL, "เทา = ค่าดิบ", 24, 238, AUTO, AUTO, COLOR_DIM,
         20, 0, 0},
        {UI_WIDGET_LABEL, "กำลังวาด: EMA a=0.2", 136, 238, AUTO, AUTO,
         COLOR_CHOSEN, 20, 0, 0},
        {UI_WIDGET_LABEL, "value() ของทั้งหกตัว", 488, 66, AUTO, AUTO,
         COLOR_THEME, 20, 0, 0}};
    bento_status_t status =
        bento_ui_screen(bench->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_widgets(bench->ui, upper_widgets, 2U, bench->ids);
    }
    if (status == BENTO_OK) {
        /* series 0 comes with the chart and takes its color= */
        status = bento_ui_chart_add_series(
            bench->ui, bench->ids[W_CHART], COLOR_CHOSEN,
            &bench->chosen_series);
    }
    if (status == BENTO_OK) {
        status =
            add_widgets(bench->ui, &upper_widgets[W_RAW_NOTE],
                        (uint32_t)W_HPF_CARD - (uint32_t)W_RAW_NOTE,
                        &bench->ids[W_RAW_NOTE]);
    }
    if (status == BENTO_OK) {
        status = add_rows(bench);
    }
    if (status == BENTO_OK) {
        status = add_lower(bench);
    }
    return status;
}

/* HPF swings around zero and would sink to the chart's floor, so it is
 * lifted by 50 for drawing only, then kept inside 0-110. */
static int32_t chart_point(int32_t which, bento_real_t output)
{
    const bento_real_t lift = (bento_real_t)50.0;
    bento_real_t draw =
        (which == FILTER_HPF) ? (output + lift) : output;

    if (!(draw < (bento_real_t)CHART_MAX)) {
        draw = (bento_real_t)CHART_MAX;
    }
    if (!(draw > (bento_real_t)0.0)) {
        draw = (bento_real_t)0.0;
    }
    return (int32_t)draw;
}

/* The same sample into all six in one round, so they compare directly;
 * then the value rows, "{:6.2f}" each. */
static void run_filters(bench_t *bench, bento_real_t sample)
{
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    bento_real_t chosen = (bento_real_t)0.0;
    uint32_t length = 0U;
    int32_t i;

    for (i = 0; i < FILTER_COUNT; i++) {
        const float output =
            update_filter(&bench->filters, i, (float)sample);

        if (i == bench->chosen) {
            chosen = (bento_real_t)output;
        }
    }
    (void)bento_ui_chart_set_next(bench->ui, bench->ids[W_CHART], 0U,
                                  (int32_t)sample);
    (void)bento_ui_chart_set_next(bench->ui, bench->ids[W_CHART],
                                  bench->chosen_series,
                                  chart_point(bench->chosen, chosen));
    for (i = 0; i < FILTER_COUNT; i++) {
        if (bento_fmt_real_precision(
                number, BENTO_FMT_BUF_SIZE,
                (bento_real_t)filter_value(&bench->filters, i), 2,
                &length) != BENTO_OK) {
            number[0] = '\0';
        }
        (void)snprintf(text, sizeof(text), "%s  %6s", NAMES[i], number);
        show_text(bench->ui, bench->rows[i], text);
    }
}

/* The original reads only ev['handle'], so any kind of event on a
 * button counts. */
static void handle_event(bench_t *bench, bento_ui_id_t widget)
{
    char text[TEXT_BYTES];

    if (widget == bench->ids[W_NEXT]) {
        bench->chosen = (bench->chosen + 1) % FILTER_COUNT;
        (void)snprintf(text, sizeof(text), "กำลังวาด: %s",
                       NAMES[bench->chosen]);
        show_text(bench->ui, bench->ids[W_CHOSEN], text);
        (void)bento_ui_set_color(bench->ui, bench->ids[W_CHOSEN],
                                 COLORS[bench->chosen]);
    } else if (widget == bench->ids[W_RESET]) {
        reset_filters(&bench->filters);
        (void)bento_lcd_print(
            "reset ทั้งหกตัวแล้ว - ดูรอบถัดไปว่าทุกตัวเริ่มใหม่พร้อมกัน");
    } else if (widget == bench->ids[W_EXIT]) {
        bench->running = false;
    } else {
        /* another widget: nothing to do */
    }
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static void handle_events(bench_t *bench)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(bench->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        handle_event(bench, events[i].id);
    }
}

static bento_status_t run_loop(bench_t *bench)
{
    bento_status_t status = BENTO_OK;
    int32_t point = 0;

    bench->running = true;
    while (bench->running && (status == BENTO_OK)) {
        run_filters(bench, signal_at(point % N_POINTS));
        handle_events(bench);
        point++;
        status = pause_ms(DT_MS);
    }
    return status;
}

static void print_start(void)
{
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint32_t length = 0U;

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>ตัวกรองหกตัวบนสัญญาณเดียวกัน</h2>");
    if (bento_fmt_real(number, BENTO_FMT_BUF_SIZE, (bento_real_t)FS_HZ,
                       &length) != BENTO_OK) {
        number[0] = '\0';
    }
    (void)snprintf(text, sizeof(text),
                   "ลูป %d ms = fs %s Hz | ขั้นบันไดที่จุด %d", DT_MS, number,
                   STEP_AT);
    (void)bento_lcd_print(text);
    print_clamps();
}

int main(void)
{
    bench_t bench;
    bento_status_t status;

    (void)memset(&bench, 0, sizeof(bench));
    bench.ui = bento_runtime_get_ui();
    status = start_smoothers(&bench.filters);
    if (status == BENTO_OK) {
        status = start_others(&bench.filters);
    }
    if (status == BENTO_OK) {
        print_start();
        status = add_screen(&bench);
    }
    if (status == BENTO_OK) {
        status = run_loop(&bench);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_clear(bench.ui);
        (void)printf("เทียบกันแล้วจะเห็นว่า Median กลืนหนามได้ ส่วน "
                     "EMA/SMA/Kalman โดนลากตาม\n");
        (void)printf("และ HPF ไม่สนใจระดับของสัญญาณเลย "
                     "มันสนแค่ว่าเปลี่ยนเร็วแค่ไหน\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
