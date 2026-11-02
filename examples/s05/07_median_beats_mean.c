/*
 * 07_median_beats_mean.c -- median กับ ค่าเฉลี่ย ตอนเจอค่าหลุด
 *
 * สัญญาณฐาน 2.50 มีค่าหลุด (spike) 9.90 แบบที่เกิดจริงเวลาสาย I2C โดนรบกวน
 * เดี่ยวหนึ่งครั้ง สามตัวติด แล้วเดี่ยวอีกครั้ง กราฟวาดค่าดิบ (ฟ้า) ค่าเฉลี่ย
 * SMA (แดง) และ median (เขียว) เดินดูห้าขนาดหน้าต่าง N (3 5 7 9 15) Seg7 สองตัว
 * บอกว่าแต่ละตัวเบี่ยงจากฐานสูงสุดเท่าไร median กัน spike ติดกันได้ (N-1)/2 ตัว
 * ปุ่มย้อน เดินหน้า เล่นรวด เริ่มใหม่ ทำงานจนกดหยุด
 *
 * A base signal of 2.50 carries spikes of 9.90, the kind a disturbed
 * I2C line gives: one alone, three in a row, then one alone again. The
 * chart draws the raw values (blue), the SMA (red) and the median
 * (green). Five window sizes N (3 5 7 9 15) are walked; two Seg7s show
 * how far each strayed from the base at worst. A median rides out
 * (N-1)/2 spikes in a row. Back, next, play-through and restart. Runs
 * until Stop.
 *
 * Twin of 07_median_beats_mean.py
 * (embedded-systems-for-aiot-developer/examples/s05/07_median_beats_mean.py),
 * rewritten from the library twin aiot-s05-07-median-beats-mean. Style:
 * CLEAN_C.md. dsp.Median is bento_dsp_median_state_*; the SMA's list
 * is a ring of the last N samples, summed oldest first as sum() adds
 * them; round(x, 2) is nearbyint(x * 100) / 100 as MicroPython's.
 * The reTerminal's MicroPython has no dsp module (the original raises
 * ImportError there); this C twin runs there too.
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

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add, or a value differs in the last digit. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    N_SAMPLES = 50,  /* the chart's window, exactly */
    SPIKE_COUNT = 5,
    BURST = 3, /* spikes in a row in the middle group */
    WINDOW_COUNT = 5,
    WINDOW_MAX = 15,
    PLAY_MS = 1500,
    LOOP_MS = 30,
    CHART_MAX = 110, /* the chart's unit is the value times 10 */
    AUTO = -1,       /* no w= or h= given: the widget's own size */
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* Colours, 0xRRGGBB. A line has the very colour its label names. */
enum {
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_THEME = 0 /* no color= given */
};

/* The widgets, in the order they are made; the chart's two series come
 * after the chart. */
enum {
    W_TITLE,
    W_POSITION,
    W_CHART,
    W_RAW_NOTE,
    W_MEAN_NOTE,
    W_MEDIAN_NOTE,
    W_WINDOW,
    W_WORST_CAPTION,
    W_WORST_MEAN,
    W_WORST_MEDIAN,
    W_NOTE,
    W_PROGRESS,
    W_PREVIOUS,
    W_NEXT,
    W_PLAY,
    W_HOME,
    W_COUNT
};

/* The window sizes walked, one per step. */
static const int32_t WINDOWS[WINDOW_COUNT] = {3, 5, 7, 9, 15};

static const bento_real_t BASE = (bento_real_t)2.50;
static const bento_real_t SPIKE = (bento_real_t)9.90;

/* The 50 points of one step, worked out before the screen is touched. */
typedef struct {
    int16_t raw[N_SAMPLES];
    int16_t mean[N_SAMPLES];
    int16_t median[N_SAMPLES];
    bento_real_t worst_mean;
    bento_real_t worst_median;
} points_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
    uint8_t mean_series;
    uint8_t median_series;
    int32_t step; /* the current step: the program's truth */
    bool playing;
    uint32_t next_ms;
} median_t;

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

/* True when the event is of this kind; the kind passes through a
 * uint8_t, so both sides of the comparison have one type. */
static bool type_is(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

static bento_real_t real_fabs(bento_real_t value)
{
#if BENTO_FMT_REAL_DOUBLE
    return fabs(value);
#else
    return fabsf(value);
#endif
}

static bento_real_t real_nearbyint(bento_real_t value)
{
#if BENTO_FMT_REAL_DOUBLE
    return nearbyint(value);
#else
    return nearbyintf(value);
#endif
}

/* A spike as a disturbed I2C line gives one: one alone, three in a row,
 * then one alone again. */
static bento_real_t sample(int32_t index)
{
    static const int32_t spike_at[SPIKE_COUNT] = {6, 13, 14, 15, 30};
    bento_real_t value = BASE;
    uint32_t i;

    for (i = 0U; i < (uint32_t)SPIKE_COUNT; i++) {
        if (spike_at[i] == index) {
            value = SPIKE;
        }
    }
    return value;
}

/* A value in the chart's unit (times 10). */
static int16_t chart_value(bento_real_t value)
{
    const bento_real_t scaled = value * (bento_real_t)10.0;

    return (int16_t)(int32_t)scaled;
}

/* The mean of the last `window` samples, summed oldest first. */
static bento_real_t window_mean(const bento_real_t *ring,
                                int32_t window, int32_t newest)
{
    const int32_t held =
        ((newest + 1) < window) ? (newest + 1) : window;
    bento_real_t sum = (bento_real_t)0.0;
    int32_t k;

    for (k = held - 1; k >= 0; k--) {
        sum = sum + ring[(newest - k) % WINDOW_MAX];
    }
    return sum / (bento_real_t)held;
}

static void keep_worst(bento_real_t *worst, bento_real_t value)
{
    const bento_real_t away = real_fabs(value - BASE);

    if (away > *worst) {
        *worst = away;
    }
}

/* All the work first, without one command to the display core. */
static bento_status_t compute_points(int32_t window, points_t *points)
{
    bento_dsp_median_config_t config;
    bento_dsp_median_state_t median;
    bento_real_t ring[WINDOW_MAX];
    bento_status_t status = bento_dsp_median_config_init(&config);
    int32_t n;

    points->worst_mean = (bento_real_t)0.0;
    points->worst_median = (bento_real_t)0.0;
    if (status == BENTO_OK) {
        config.window = window;
        status = bento_dsp_median_state_init(&median, &config);
    }
    for (n = 0; (n < N_SAMPLES) && (status == BENTO_OK); n++) {
        const bento_real_t value = sample(n);
        float filtered = 0.0F;
        bento_real_t mean;

        ring[n % WINDOW_MAX] = value;
        mean = window_mean(ring, window, n);
        status = bento_dsp_median_state_update(&median, (float)value,
                                               &filtered);
        keep_worst(&points->worst_mean, mean);
        keep_worst(&points->worst_median, (bento_real_t)filtered);
        points->raw[n] = chart_value(value);
        points->mean[n] = chart_value(mean);
        points->median[n] = chart_value((bento_real_t)filtered);
    }
    return status;
}

/* The screen; the chart's unit is the value times 10. The buttons are
 * 88 tall, 32 apart, and end at 680, left of the Console's corner. */
/* The labels, numbers and buttons below and beside the chart. */
static bento_status_t add_lower(median_t *median)
{
    static const widget_spec_t lower_widgets[(uint32_t)W_COUNT -
                                             (uint32_t)W_RAW_NOTE] = {
        {UI_WIDGET_LABEL, "ฟ้า = ค่าดิบ (มี spike)", 472, 74, AUTO, AUTO,
         COLOR_ACCENT, 20, 0, 0},
        {UI_WIDGET_LABEL, "แดง = SMA", 472, 106, AUTO, AUTO, COLOR_BAD,
         20, 0, 0},
        {UI_WIDGET_LABEL, "เขียว = Median", 608, 106, AUTO, AUTO,
         COLOR_OK, 20, 0, 0},
        {UI_WIDGET_LABEL, "N = -", 472, 140, AUTO, AUTO, COLOR_WARN, 24,
         0, 0},
        {UI_WIDGET_LABEL, "เบี่ยงสูงสุด (x100)", 560, 142, AUTO, AUTO,
         COLOR_THEME, 20, 0, 0},
        {UI_WIDGET_SEG7, NULL, 472, 180, 136, 48, COLOR_BAD, 0, 0, 0},
        {UI_WIDGET_SEG7, NULL, 624, 180, 136, 48, COLOR_OK, 0, 0, 0},
        {UI_WIDGET_LABEL, "กำลังคำนวณ", 24, 246, AUTO, AUTO, COLOR_THEME,
         20, 0, 0},
        {UI_WIDGET_BAR, NULL, 520, 252, 168, 16, COLOR_THEME, 0, 0,
         WINDOW_COUNT - 1},
        {UI_WIDGET_BUTTON, "< ย้อน", 24, 284, 128, 88, COLOR_DIM, 20, 0,
         0},
        {UI_WIDGET_BUTTON, "เดินหน้า >", 184, 284, 152, 88, COLOR_ACCENT,
         20, 0, 0},
        {UI_WIDGET_BUTTON, ">> เล่นรวด", 368, 284, 152, 88, COLOR_OK, 20,
         0, 0},
        {UI_WIDGET_BUTTON, "เริ่มใหม่", 552, 284, 128, 88, COLOR_ACCENT,
         20, 0, 0}};

    return add_widgets(median->ui, lower_widgets,
                       (uint32_t)W_COUNT - (uint32_t)W_RAW_NOTE,
                       &median->ids[W_RAW_NOTE]);
}

static bento_status_t add_screen(median_t *median)
{
    static const widget_spec_t screen_widgets[W_RAW_NOTE] = {
        {UI_WIDGET_LABEL, "median กับ ค่าเฉลี่ย ตอนเจอค่าหลุด", 24, 24, AUTO,
         AUTO, COLOR_THEME, 24, 0, 0},
        {UI_WIDGET_LABEL, "ท่า - / -", 600, 32, AUTO, AUTO, COLOR_OK, 20,
         0, 0},
        {UI_WIDGET_CHART, NULL, 24, 76, 428, 164, COLOR_ACCENT, 0, 0,
         CHART_MAX}};
    bento_status_t status =
        add_widgets(median->ui, screen_widgets, (uint32_t)W_CHART + 1U,
                    median->ids);

    if (status == BENTO_OK) {
        status =
            bento_ui_chart_add_series(median->ui, median->ids[W_CHART],
                                      COLOR_BAD, &median->mean_series);
    }
    if (status == BENTO_OK) {
        status =
            bento_ui_chart_add_series(median->ui, median->ids[W_CHART],
                                      COLOR_OK, &median->median_series);
    }
    if (status == BENTO_OK) {
        status = add_lower(median);
    }
    return status;
}

/* The corner label tells the place and "playing" in one line: there
 * is no room for a separate hint, and both say the same thing. */
static void show_labels(const median_t *median, int32_t window,
                        const points_t *points)
{
    char text[TEXT_BYTES];
    const int32_t tolerated = (window - 1) / 2;
    const bento_real_t mean_x100 =
        points->worst_mean * (bento_real_t)100.0;
    const bento_real_t median_x100 =
        points->worst_median * (bento_real_t)100.0;

    (void)snprintf(text, sizeof(text), "%s %" PRId32 " / %d",
                   median->playing ? "เล่นรวด" : "ท่า", median->step + 1,
                   WINDOW_COUNT);
    show_text(median->ui, median->ids[W_POSITION], text);
    (void)snprintf(text, sizeof(text), "N = %" PRId32, window);
    show_text(median->ui, median->ids[W_WINDOW], text);
    (void)snprintf(text, sizeof(text), "%" PRId32, (int32_t)mean_x100);
    show_text(median->ui, median->ids[W_WORST_MEAN], text);
    (void)snprintf(text, sizeof(text), "%" PRId32,
                   (int32_t)median_x100);
    show_text(median->ui, median->ids[W_WORST_MEDIAN], text);
    (void)snprintf(text, sizeof(text),
                   (tolerated >= BURST) ? "กัน spike ติดกันได้ %" PRId32
                                          " ตัว -> ทนกลุ่ม %d ตัวไหว"
                                        : "กัน spike ติดกันได้ %" PRId32
                                          " ตัว -> กลุ่ม %d ตัว median แพ้",
                   tolerated, BURST);
    show_text(median->ui, median->ids[W_NOTE], text);
    (void)bento_ui_set_value(median->ui, median->ids[W_PROGRESS],
                             median->step);
}

/* str(round(x, 2)), as the board's MicroPython writes it. */
static void rounded_text(bento_real_t value, char *text,
                         uint32_t text_bytes)
{
    const bento_real_t hundred = (bento_real_t)100.0;
    const bento_real_t rounded =
        real_nearbyint(value * hundred) / hundred;
    uint32_t length = 0U;

    if (bento_fmt_real(text, text_bytes, rounded, &length) !=
        BENTO_OK) {
        text[0] = '\0';
    }
}

/* The numbers first, the chart last. 150 set_next() calls at once fill
 * the IPC queue and the commands behind them are dropped silently, so
 * labels sent after the chart could stay at the Seg7's 0000. */
static bento_status_t show(const median_t *median)
{
    points_t points;
    char mean_text[BENTO_FMT_BUF_SIZE];
    char median_text[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    const int32_t window = WINDOWS[median->step];
    const bento_ui_id_t chart = median->ids[W_CHART];
    bento_status_t status = compute_points(window, &points);
    int32_t n;

    if (status == BENTO_OK) {
        show_labels(median, window, &points);
        for (n = 0; n < N_SAMPLES; n++) {
            (void)bento_ui_chart_set_next(median->ui, chart, 0U,
                                          points.raw[n]);
            (void)bento_ui_chart_set_next(
                median->ui, chart, median->mean_series, points.mean[n]);
            (void)bento_ui_chart_set_next(median->ui, chart,
                                          median->median_series,
                                          points.median[n]);
        }
        rounded_text(points.worst_mean, mean_text, sizeof(mean_text));
        rounded_text(points.worst_median, median_text,
                     sizeof(median_text));
        (void)snprintf(text, sizeof(text),
                       "N=%" PRId32 " SMA เบี่ยง %s median %s", window,
                       mean_text, median_text);
        /* lcd.print() ignores a console that is not ready */
        (void)bento_lcd_print(text);
    }
    return status;
}

/* The full table cannot be read on a 4.3 inch screen; it goes to the
 * serial console to read later. */
static void print_table(void)
{
    const bento_real_t jump = SPIKE - BASE;
    const bento_real_t pushed = jump / (bento_real_t)WINDOWS[1];
    char jump_text[BENTO_FMT_BUF_SIZE];
    char pushed_text[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;
    uint32_t i;

    (void)printf("N    กัน spike ติดกันได้   ผลกับกลุ่ม %d ตัว\n", BURST);
    for (i = 0U; i < (uint32_t)WINDOW_COUNT; i++) {
        const int32_t tolerated = (WINDOWS[i] - 1) / 2;

        (void)printf("%-4" PRId32 " %-21" PRId32 " %s\n", WINDOWS[i],
                     tolerated,
                     (tolerated >= BURST) ? "median รอด" : "median แพ้");
    }
    (void)printf("\n");
    /* BENTO_FMT_BUF_SIZE holds every result */
    (void)bento_fmt_real_precision(jump_text, BENTO_FMT_BUF_SIZE, jump,
                                   2, &length);
    (void)bento_fmt_real_precision(pushed_text, BENTO_FMT_BUF_SIZE,
                                   pushed, 2, &length);
    (void)printf("spike เดี่ยวสูงกว่าฐาน %s -> ดัน SMA(5) ขึ้น %s\n",
                 jump_text, pushed_text);
    (void)printf("ส่วน median ขยับ 0.00 เพราะค่าหลุดถูกเรียงไปอยู่ริมแถว "
                 "ไม่ใช่ตัวกลาง\n");
    (void)printf("ท่ามาตรฐานในงานจริงคือ median ก่อนเพื่อฆ่า spike "
                 "แล้วค่อย EMA เพื่อความเรียบ\n");
}

static bento_status_t handle_click(median_t *median,
                                   bento_ui_id_t widget)
{
    if (widget == median->ids[W_PREVIOUS]) {
        median->playing = false;
        median->step = (median->step + WINDOW_COUNT - 1) % WINDOW_COUNT;
    } else if (widget == median->ids[W_NEXT]) {
        median->playing = false;
        median->step = (median->step + 1) % WINDOW_COUNT;
    } else if (widget == median->ids[W_HOME]) {
        median->playing = false;
        median->step = 0;
    } else if (widget == median->ids[W_PLAY]) {
        median->playing = !median->playing;
        median->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
    } else {
        /* any other button still redraws, as in the original */
    }
    return show(median);
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static bento_status_t handle_events(median_t *median)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    bento_status_t status = BENTO_OK;
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(median->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        if (type_is(&events[i], BENTO_UI_EVENT_CLICKED)) {
            status = handle_click(median, events[i].id);
        }
    }
    return status;
}

/* Playing through moves on by the clock, without sleeping, so the
 * buttons still answer; it stops at the last step. */
static bento_status_t play_on(median_t *median)
{
    bento_status_t status = BENTO_OK;

    if (median->playing &&
        (bento_time_ticks_diff(bento_time_ticks_ms(),
                               median->next_ms) >= 0)) {
        median->step++;
        if (median->step >= WINDOW_COUNT) {
            median->step = WINDOW_COUNT - 1;
            median->playing = false;
        }
        median->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
        status = show(median);
    }
    return status;
}

static bento_status_t run_loop(median_t *median)
{
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        status = handle_events(median);
        if (status == BENTO_OK) {
            status = play_on(median);
        }
        if (status == BENTO_OK) {
            status = pause_ms(LOOP_MS);
        }
    }
    return status;
}

static void print_start(void)
{
    char base_text[BENTO_FMT_BUF_SIZE];
    char spike_text[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint32_t length = 0U;

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>median เทียบ ค่าเฉลี่ย</h2>");
    (void)bento_fmt_real(base_text, BENTO_FMT_BUF_SIZE, BASE, &length);
    (void)bento_fmt_real(spike_text, BENTO_FMT_BUF_SIZE, SPIKE,
                         &length);
    (void)snprintf(text, sizeof(text),
                   "ฐาน %s | spike %s | spike ติดกัน %d ตัว", base_text,
                   spike_text, BURST);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print("<span class=muted>กดเดินหน้าเพื่อขยายหน้าต่าง N "
                          "- ระหว่างเล่นรวด กดปุ่มไหนก็หยุด</span>");
}

int main(void)
{
    median_t median;
    bento_status_t status;

    (void)memset(&median, 0, sizeof(median));
    median.ui = bento_runtime_get_ui();
    status = bento_ui_screen(median.ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                             BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_screen(&median);
    }
    if (status == BENTO_OK) {
        print_start();
        status = show(&median);
    }
    if (status == BENTO_OK) {
        print_table();
        (void)bento_lcd_print(
            "<span class=ok>ท่ามาตรฐาน: median ก่อน แล้วค่อย EMA</span>");
        status = run_loop(&median);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
