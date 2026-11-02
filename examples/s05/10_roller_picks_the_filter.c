/*
 * 10_roller_picks_the_filter.c -- เลือกตัวกรองด้วยนิ้ว (Roller)
 *
 * วงล้อ (ui.Roller) หกบรรทัดให้ปัดเลือกตัวกรอง EMA SMA LPF HPF Median Kalman1D
 * สัญญาณทดสอบชุดเดียวกับไฟล์ก่อนหน้าป้อนเข้าทุกตัวทุกรอบ (รอบละ 200 ms 200 รอบ)
 * กราฟวาดค่าดิบ (เทา) กับตัวที่เลือก (ฟ้า) ป้ายบอกค่าล่าสุดวินาทีละครั้ง
 * Roller ส่ง value_changed พร้อมลำดับบรรทัด เริ่มที่ 0 เหมือน Dropdown
 *
 * A six-line roller (ui.Roller) lets a finger pick the filter: EMA,
 * SMA, LPF, HPF, Median or Kalman1D. The test signal of the previous
 * file feeds every filter every round (200 rounds of 200 ms). The
 * chart draws the raw signal (grey) and the chosen filter (blue); a
 * label shows the latest value once a second. The roller sends
 * value_changed with the line's index from 0, as a Dropdown does.
 *
 * Twin of 10_roller_picks_the_filter.py
 * (embedded-systems-for-aiot-developer/examples/s05/10_roller_picks_the_filter.py),
 * rewritten from the library twin aiot-s05-10-roller-picks-the-filter.
 * Style: CLEAN_C.md. The six dsp objects are bento_dsp_*_state_t held
 * in the program's memory; "%.2f" and "%.1f" are
 * bento_fmt_real_precision(). The reTerminal's MicroPython has no dsp
 * module (the original raises ImportError there); this C twin runs
 * there too.
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
    SETTLE_MS = 200, /* let the cleared screen settle */
    DT_MS = 200,
    N_POINTS = 60,
    STEP_AT = 20,
    WOBBLE_COUNT = 8,
    SPIKE_COUNT = 2,
    SMA_WINDOW = 8,
    MEDIAN_WINDOW = 5,
    ROUNDS = 200,
    SHOW_EVERY = 5, /* a value people read changes once a second */
    CHART_MAX = 110,
    AUTO = -1,       /* no w= or h= given: the widget's own size */
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The six filters, in the order of the roller's lines. */
enum {
    FILTER_EMA,
    FILTER_SMA,
    FILTER_LPF,
    FILTER_HPF,
    FILTER_MEDIAN,
    FILTER_KALMAN,
    FILTER_COUNT
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF
};

/* The widgets, in the order they are made. */
enum {
    W_TITLE,
    W_ROLLER,
    W_CHART,
    W_PICK,
    W_VALUE,
    W_COUNT
};

/* The sample rate of the 200 ms loop, and the cut of LPF and HPF:
 * both must be told the same. */
static const float FS_HZ = 5.0F;
static const float CUTOFF_HZ = 0.4F;

/* Short names: the options share a 320-byte ceiling, and a Thai letter
 * takes 3 bytes. */
static const char *const NAMES[FILTER_COUNT] = {
    "EMA a=0.2", "SMA w=8",    "LPF 0.4Hz",
    "HPF 0.4Hz", "Median w=5", "Kalman r=4"};

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
    uint8_t chosen_series;
    int32_t chosen;
    filters_t filters;
} roller_t;

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

/* No value= on the roller: its 232 x 248 touch target is far over the
 * 88 px minimum, since a finger drags rather than taps. Its visible
 * rows are not set either: that call would overwrite the h= given. */
static bento_status_t add_screen(roller_t *roller)
{
    static const widget_spec_t screen_widgets[W_COUNT] = {
        {UI_WIDGET_LABEL, "เลือกตัวกรองด้วยนิ้ว", 24, 16, AUTO, AUTO,
         COLOR_TEXT, 28, 0, 0},
        {UI_WIDGET_ROLLER, NULL, 24, 72, 232, 248, COLOR_TEXT, 0, 0, 0},
        {UI_WIDGET_CHART, NULL, 288, 72, 480, 176, COLOR_DIM, 0, 0,
         CHART_MAX},
        {UI_WIDGET_LABEL, "กำลังวาด: EMA a=0.2", 288, 264, AUTO, AUTO,
         COLOR_ACCENT, 24, 0, 0},
        {UI_WIDGET_LABEL, "รอค่าแรก", 288, 304, AUTO, AUTO, COLOR_DIM,
         24, 0, 0}};
    bento_status_t status =
        add_widgets(roller->ui, screen_widgets, 2U, roller->ids);
    int32_t i;

    for (i = 0; (i < FILTER_COUNT) && (status == BENTO_OK); i++) {
        /* add_option never raises in MicroPython */
        (void)bento_ui_add_option(roller->ui, roller->ids[W_ROLLER],
                                  NAMES[i], (uint32_t)strlen(NAMES[i]));
    }
    if (status == BENTO_OK) {
        status = add_widget(roller->ui, &screen_widgets[W_CHART],
                            &roller->ids[W_CHART]);
    }
    if (status == BENTO_OK) {
        /* series 0 comes with the chart and takes its color= */
        status = bento_ui_chart_add_series(
            roller->ui, roller->ids[W_CHART], COLOR_ACCENT,
            &roller->chosen_series);
    }
    if (status == BENTO_OK) {
        status = add_widgets(roller->ui, &screen_widgets[W_PICK], 2U,
                             &roller->ids[W_PICK]);
    }
    return status;
}

/* "%.<digits>f" of a real. */
static void fixed_text(bento_real_t value, int32_t digits, char *text,
                       uint32_t text_bytes)
{
    uint32_t length = 0U;

    if (bento_fmt_real_precision(text, text_bytes, value, digits,
                                 &length) != BENTO_OK) {
        text[0] = '\0';
    }
}

/* Every filter is fed every round, not only the chosen one: a filter
 * has a memory, and one left unfed would answer from a broken past.
 * HPF swings around zero, so it is lifted by 50 for drawing only. */
static void run_filters(roller_t *roller, bento_real_t sample,
                        int32_t round)
{
    const bento_real_t lift = (bento_real_t)50.0;
    char output_text[BENTO_FMT_BUF_SIZE];
    char sample_text[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    bento_real_t chosen = (bento_real_t)0.0;
    bento_real_t drawn;
    int32_t i;

    for (i = 0; i < FILTER_COUNT; i++) {
        const float output =
            update_filter(&roller->filters, i, (float)sample);

        if (i == roller->chosen) {
            chosen = (bento_real_t)output;
        }
    }
    drawn = (roller->chosen == FILTER_HPF) ? (chosen + lift) : chosen;
    (void)bento_ui_chart_set_next(roller->ui, roller->ids[W_CHART], 0U,
                                  (int32_t)sample);
    (void)bento_ui_chart_set_next(roller->ui, roller->ids[W_CHART],
                                  roller->chosen_series,
                                  (int32_t)drawn);
    if ((round % SHOW_EVERY) == 0) {
        fixed_text(chosen, 2, output_text, sizeof(output_text));
        fixed_text(sample, 1, sample_text, sizeof(sample_text));
        (void)snprintf(text, sizeof(text), "ค่าล่าสุด %s จากค่าดิบ %s",
                       output_text, sample_text);
        show_text(roller->ui, roller->ids[W_VALUE], text);
    }
}

/* The value is the line's index, not its text: the program looks the
 * name up itself, exactly as for a Dropdown. */
static void pick(roller_t *roller, int32_t line)
{
    char text[TEXT_BYTES];

    roller->chosen = ((line < 0) || (line >= FILTER_COUNT)) ? 0 : line;
    (void)snprintf(text, sizeof(text), "กำลังวาด: %s",
                   NAMES[roller->chosen]);
    show_text(roller->ui, roller->ids[W_PICK], text);
    (void)snprintf(text, sizeof(text), "เลือกบรรทัดที่ %" PRId32 " %s",
                   roller->chosen, NAMES[roller->chosen]);
    /* lcd.print() ignores a console that is not ready */
    (void)bento_lcd_print(text);
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static void handle_events(roller_t *roller)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(roller->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if ((events[i].id == roller->ids[W_ROLLER]) &&
            type_is(&events[i], BENTO_UI_EVENT_VALUE_CHANGED)) {
            pick(roller, events[i].value);
        }
    }
}

int main(void)
{
    roller_t roller;
    bento_status_t status;
    int32_t round;

    (void)memset(&roller, 0, sizeof(roller));
    roller.ui = bento_runtime_get_ui();
    status = start_smoothers(&roller.filters);
    if (status == BENTO_OK) {
        status = start_others(&roller.filters);
    }
    if (status == BENTO_OK) {
        status =
            bento_ui_screen(roller.ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                            BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    }
    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_screen(&roller);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>Roller - เลือกด้วยการปัดนิ้ว</h2>");
        (void)bento_lcd_print("เทาคือค่าดิบ ฟ้าคือตัวกรองที่เลือกอยู่");
        (void)bento_lcd_print(
            "Roller ส่ง value_changed พร้อมลำดับบรรทัด เริ่มที่ 0");
    }
    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        run_filters(&roller, signal_at(round % N_POINTS), round);
        handle_events(&roller);
        status = pause_ms(DT_MS);
    }
    if (status == BENTO_OK) {
        show_text(roller.ui, roller.ids[W_VALUE],
                  "จบรอบ - ปัดวงล้อได้อีกในการรันครั้งหน้า");
        (void)bento_lcd_print("<span class=ok>วงล้อบอกทั้งตัวที่เลือกและตัวที่"
                              "เหลือพร้อมกัน</span>");
        (void)printf("Roller: .add_option() เติมทีละบรรทัด .value() "
                     "อ่านลำดับที่เลือก\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
