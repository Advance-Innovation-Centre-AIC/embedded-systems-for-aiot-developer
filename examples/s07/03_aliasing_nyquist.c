/*
 * 03_aliasing_nyquist.c -- สุ่มช้าเกินไป
 * แล้วได้ความถี่ที่ไม่เคยมีอยู่จริง (aliasing) (คาบ 07)
 *
 * กราฟสามเส้นที่อัตราสุ่มตรึงไว้ 40 Hz: คลื่นจริง ค่าที่ ADC เก็บได้
 * (บันได zero-order hold) และ คลื่นที่เครื่องคิดว่าเห็น กดเดินหน้า /
 * ย้อน / เริ่มใหม่ เพื่อเปลี่ยนความถี่จริงทีละขั้น (5, 18, 20, 30, 39,
 * 41 Hz) หรือกดเล่นรวดให้เดินเองทุก 1.6 วินาที ใต้ Nyquist
 * สองเส้นทับกัน เกินแล้ว แยกกัน ตารางเทียบ 30 Hz กับ 10 Hz
 * ที่จุดสุ่มทั้งเก้าจุดออกทาง serial ทำงานจนกดหยุด
 *
 * Three lines at a fixed 40 Hz sampling rate: the real wave, what the
 * ADC holds (a zero-order hold staircase), and the wave the machine
 * thinks it sees. Step the real frequency with next / back / restart
 * (5, 18, 20, 30, 39, 41 Hz), or let "play" step it every 1.6 seconds.
 * Below Nyquist the two lines agree; above it they part. A table of 30
 * Hz against 10 Hz at all nine sample points goes to the serial
 * console. Runs until Stop.
 *
 * Twin of 03_aliasing_nyquist.py
 * (embedded-systems-for-aiot-developer/examples/s07/03_aliasing_nyquist.py),
 * merged from the library twin aiot-s07-03-aliasing-nyquist (the same
 * program). Style: CLEAN_C.md.
 * Real numbers are bento_real_t (the board MicroPython's float width),
 * math.sin / floor / fabs are the C functions of that width, and reals
 * are printed through bento_fmt, which writes MicroPython's "%.<n>f"
 * text. Every % here has non-negative operands, so C's % and fmod give
 * what Python's modulo gives.
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"

enum {
    SETTLE_MS = 200,      /* let the cleared screen settle */
    LOOP_MS = 30,         /* read the taps this often */
    PLAY_MS = 1600,       /* "play" steps once this often */
    STEP_COUNT = 6,       /* the real frequencies walked through */
    POINTS_PER_STEP = 50, /* exactly the Chart's window */
    TABLE_ROWS = 9,  /* the nine sample points of the serial table */
    SINE_DIGITS = 4, /* the table's decimals for one sine */
    SUM_DIGITS = 6,  /* and for the sum of two */
    TRUE_SERIES = 0, /* the Chart is made with series 0 */
    /* 120 bytes at most here; 160 also holds any text bento_fmt can
     * write */
    TEXT_BYTES = 160
};

/* the one sampling rate, fixed */
static const bento_real_t SAMPLE_HZ = (bento_real_t)40.0;

/* The screen, the widgets it changes, and where the walk is. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t position;
    bento_ui_id_t chart;
    uint8_t hold_series;
    uint8_t alias_series;
    bento_ui_id_t frequency;
    bento_ui_id_t seen;
    bento_ui_id_t note;
    bento_ui_id_t back;
    bento_ui_id_t next;
    bento_ui_id_t play;
    bento_ui_id_t restart;
    int32_t step; /* the program's truth: which frequency is shown */
    bool playing; /* "play" is stepping by itself */
    uint32_t next_ms; /* when "play" steps next */
} alias_screen_t;

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add, or a sample lands on the other side of a
 * step. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

static bento_real_t real_sin(bento_real_t radians)
{
#if BENTO_FMT_REAL_DOUBLE
    return sin(radians);
#else
    return sinf(radians);
#endif
}

static bento_real_t real_floor(bento_real_t value)
{
#if BENTO_FMT_REAL_DOUBLE
    return floor(value);
#else
    return floorf(value);
#endif
}

static bento_real_t real_fmod(bento_real_t dividend,
                              bento_real_t divisor)
{
#if BENTO_FMT_REAL_DOUBLE
    return fmod(dividend, divisor);
#else
    return fmodf(dividend, divisor);
#endif
}

static bento_real_t real_fabs(bento_real_t value)
{
#if BENTO_FMT_REAL_DOUBLE
    return fabs(value);
#else
    return fabsf(value);
#endif
}

/* sin(2 * pi * hz * time_s), multiplied in the original's order. */
static bento_real_t wave(bento_real_t hz, bento_real_t time_s)
{
    const bento_real_t math_pi = (bento_real_t)3.141592653589793;
    const bento_real_t two_pi = (bento_real_t)2 * math_pi;

    return real_sin((two_pi * hz) * time_s);
}

/* "%.<digits>f" of value into text, which holds BENTO_FMT_BUF_SIZE
 * bytes: enough for every result, so the format cannot fail. */
static void format_real(char *text, bento_real_t value, int32_t digits)
{
    uint32_t length = 0U;

    (void)bento_fmt_real_precision(text, BENTO_FMT_BUF_SIZE, value,
                                   digits, &length);
}

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* True when the event is of this kind (a uint8_t, so both sides of
 * == have one type). */
static bool kind_is(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

/* Layout in four bands, 24 px margin: title / chart and legend /
 * verdict / buttons. The button row ends at x=680, clear of the Console
 * button's corner (x>=690). */
enum {
    MARGIN_X = 24,
    TOP_Y = 8,
    SIDE_X = 448,
    CHART_Y = 44,
    CHART_W = 408,
    CHART_H = 192,
    /* the chart counts in hundredths: a sine of 1.0 is 100 */
    CHART_MAX = 120,
    LEGEND_TRUE_Y = 44,
    LEGEND_HOLD_Y = 72,
    LEGEND_ALIAS_Y = 100,
    FREQUENCY_Y = 132,
    SEEN_CAPTION_Y = 164,
    SEEN_Y = 196,
    SEEN_W = 152,
    SEEN_H = 40,
    NOTE_Y = 244,
    BUTTON_Y = 284,
    BUTTON_W = 140,
    BUTTON_H = 88,
    BUTTON_STRIDE = 172, /* 140 wide, 32 apart */
    COLUMN_BACK = 0,     /* the buttons, left to right */
    COLUMN_NEXT = 1,
    COLUMN_PLAY = 2,
    COLUMN_RESTART = 3,
    TEXT_PX = 20
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TRUE = 0x4A9EFF,
    COLOR_HOLD = 0xE5484D,
    COLOR_ALIAS = 0x30A46C,
    COLOR_FREQUENCY = 0xF5A623,
    COLOR_BACK = 0x9AA3AF
};

/* A label in the theme's colour when color is 0. */
static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t x, int16_t y, uint32_t color,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.color = color;
    args.font_px = TEXT_PX;
    return bento_ui_label_create(ui, &args, out_id);
}

/* The n-th button of the bottom row. */
static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 int32_t index, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = (int16_t)(MARGIN_X + (index * BUTTON_STRIDE));
    args.y = BUTTON_Y;
    args.w = BUTTON_W;
    args.h = BUTTON_H;
    args.color = color;
    args.font_px = TEXT_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* The chart with its two added series; series 0, the real wave,
 * comes with it. */
static bento_status_t add_chart(alias_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.min_val = -CHART_MAX;
    args.max_val = CHART_MAX;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status =
            bento_ui_chart_add_series(screen->ui, screen->chart,
                                      COLOR_HOLD, &screen->hold_series);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_ALIAS,
                                           &screen->alias_series);
    }
    return status;
}

/* The right column: the colour legend, the frequencies, and the
 * frequency "seen". */
static bento_status_t add_legend(alias_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(
        ui, "ฟ้า = คลื่นจริง", SIDE_X, LEGEND_TRUE_Y, COLOR_TRUE, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "แดง = ที่ ADC เก็บได้", SIDE_X,
                           LEGEND_HOLD_Y, COLOR_HOLD, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เขียว = ที่เครื่องคิดว่าเห็น", SIDE_X,
                           LEGEND_ALIAS_Y, COLOR_ALIAS, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "f จริง 5 | Nyquist 20 Hz", SIDE_X,
                      FREQUENCY_Y, COLOR_FREQUENCY, &screen->frequency);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ความถี่ที่ 'เห็น' (Hz)", SIDE_X,
                           SEEN_CAPTION_Y, 0U, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = SIDE_X;
        args.y = SEEN_Y;
        args.w = SEEN_W;
        args.h = SEEN_H;
        status = bento_ui_seg7_create(ui, &args, &screen->seen);
    }
    return status;
}

static bento_status_t add_buttons(alias_screen_t *screen)
{
    bento_status_t status = add_button(screen->ui, "< ย้อน", COLUMN_BACK,
                                       COLOR_BACK, &screen->back);

    if (status == BENTO_OK) {
        status = add_button(screen->ui, "เดินหน้า >", COLUMN_NEXT,
                            COLOR_TRUE, &screen->next);
    }
    if (status == BENTO_OK) {
        status = add_button(screen->ui, ">> เล่นรวด", COLUMN_PLAY,
                            COLOR_ALIAS, &screen->play);
    }
    if (status == BENTO_OK) {
        status = add_button(screen->ui, "เริ่มใหม่", COLUMN_RESTART,
                            COLOR_TRUE, &screen->restart);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(alias_screen_t *screen)
{
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "aliasing: "
                           "ความถี่ที่ไม่เคยมีอยู่จริง",
                           MARGIN_X, TOP_Y, 0U, &unused);
    }
    /* says the step, and whether "play" is on */
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "1 / 6", SIDE_X, TOP_Y,
                           COLOR_ALIAS, &screen->position);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_legend(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กดเดินหน้าเพื่อเพิ่มความถี่", MARGIN_X,
                           NOTE_Y, 0U, &screen->note);
    }
    if (status == BENTO_OK) {
        status = add_buttons(screen);
    }
    return status;
}

static void set_text(const alias_screen_t *screen, bento_ui_id_t label,
                     const char *text)
{
    /* MicroPython's widget setters ignore a refusal (modui.c), so
     * the twin does too. */
    (void)bento_ui_set_text(screen->ui, label, text,
                            (uint32_t)strlen(text));
}

static bento_real_t nyquist_hz(void)
{
    return SAMPLE_HZ / (bento_real_t)2;
}

/* Folds a frequency into 0 .. fs/2, the only band this sampling rate
 * can see. */
static bento_real_t alias_of(bento_real_t signal_hz)
{
    bento_real_t folded = real_fmod(signal_hz, SAMPLE_HZ);

    if (folded > nyquist_hz()) {
        folded = SAMPLE_HZ - folded;
    }
    return folded;
}

/* All POINTS_PER_STEP points of one step, so the chart's whole window
 * is replaced. The staircase takes a sample when its time has come and
 * holds it to the next one, as an ADC does (a zero-order hold). Folding
 * past fs/2 turns the phase over, so the wave "seen" is flipped. */
static void draw_waves(const alias_screen_t *screen,
                       bento_real_t true_hz, bento_real_t seen_hz)
{
    /* wide enough for nine samples */
    const bento_real_t span_s = (bento_real_t)0.225;
    const bento_real_t one = (bento_real_t)1.0;
    const bento_real_t hundred = (bento_real_t)100.0;
    const bento_real_t flip = (true_hz > nyquist_hz()) ? -one : one;
    /* the latest sample's time: before the first one */
    bento_real_t sample_s = -one;
    bento_real_t held = (bento_real_t)0.0;
    int32_t point;

    for (point = 0; point < POINTS_PER_STEP; point++) {
        const bento_real_t time_s = (span_s * (bento_real_t)point) /
                                    (bento_real_t)(POINTS_PER_STEP - 1);
        const bento_real_t true_value = hundred * wave(true_hz, time_s);
        const bento_real_t alias_value =
            (flip * hundred) * wave(seen_hz, time_s);
        bento_real_t hold_value;

        if (time_s >= (sample_s + (one / SAMPLE_HZ))) {
            sample_s = real_floor(time_s * SAMPLE_HZ) / SAMPLE_HZ;
            held = wave(true_hz, sample_s);
        }
        hold_value = hundred * held;
        /* Chart.set_next() sends without an answer and ignores a
         * refusal (modui.c) */
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      TRUE_SERIES, (int32_t)true_value);
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->hold_series,
                                      (int32_t)hold_value);
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->alias_series,
                                      (int32_t)alias_value);
    }
}

/* The step label, the frequencies, the frequency seen, the verdict,
 * and the console line. */
static void show_labels(const alias_screen_t *screen,
                        bento_real_t true_hz, bento_real_t seen_hz)
{
    char text[TEXT_BYTES];
    char true_text[BENTO_FMT_BUF_SIZE];
    char nyquist_text[BENTO_FMT_BUF_SIZE];

    if (screen->playing) {
        (void)snprintf(text, sizeof(text), "%" PRId32 " / %d  เล่นรวด",
                       screen->step + 1, STEP_COUNT);
    } else {
        (void)snprintf(text, sizeof(text), "%" PRId32 " / %d",
                       screen->step + 1, STEP_COUNT);
    }
    set_text(screen, screen->position, text);
    format_real(true_text, true_hz, 0);
    format_real(nyquist_text, nyquist_hz(), 0);
    (void)snprintf(text, sizeof(text), "f จริง %s | Nyquist %s Hz",
                   true_text, nyquist_text);
    set_text(screen, screen->frequency, text);
    (void)snprintf(text, sizeof(text), "%" PRId32, (int32_t)seen_hz);
    set_text(screen, screen->seen, text);
    if (true_hz < nyquist_hz()) {
        set_text(screen, screen->note,
                 "อยู่ใต้ Nyquist - สองเส้นทับกัน ตัวเลขเชื่อได้");
    } else if (true_hz == nyquist_hz()) {
        set_text(screen, screen->note,
                 "อยู่ที่ Nyquist พอดี - ไม่ปลอดภัย อาจได้เส้นแบน");
    } else {
        format_real(true_text, seen_hz, 0);
        (void)snprintf(text, sizeof(text),
                       "เกิน Nyquist - เครื่องรายงาน %s Hz "
                       "ซึ่งไม่มีอยู่จริง",
                       true_text);
        set_text(screen, screen->note, text);
    }
    (void)snprintf(
        text, sizeof(text),
        "%" PRId32 " Hz สุ่มที่ %" PRId32 " Hz -> เห็น %" PRId32 " Hz",
        (int32_t)true_hz, (int32_t)SAMPLE_HZ, (int32_t)seen_hz);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
}

/* The screen for the current step. */
static void show_step(const alias_screen_t *screen)
{
    static const bento_real_t frequencies_hz[STEP_COUNT] = {
        (bento_real_t)5.0,  (bento_real_t)18.0, (bento_real_t)20.0,
        (bento_real_t)30.0, (bento_real_t)39.0, (bento_real_t)41.0};
    const bento_real_t true_hz = frequencies_hz[screen->step];
    const bento_real_t seen_hz = alias_of(true_hz);

    draw_waves(screen, true_hz, seen_hz);
    show_labels(screen, true_hz, seen_hz);
}

/* Proof that 30 Hz and 10 Hz cannot be told apart at 40 Hz: at all nine
 * sample points they give the same numbers but for the sign, so no
 * algorithm can say which wave was real. Too long for the 4.3 inch
 * screen, so it goes to the serial console. */
static void print_table(void)
{
    const bento_real_t high_hz = (bento_real_t)30.0;
    const bento_real_t low_hz = (bento_real_t)10.0;
    const bento_real_t ms_per_s = (bento_real_t)1000.0;
    char ms_text[BENTO_FMT_BUF_SIZE];
    char high_text[BENTO_FMT_BUF_SIZE];
    char low_text[BENTO_FMT_BUF_SIZE];
    char sum_text[BENTO_FMT_BUF_SIZE];
    int32_t row;

    (void)printf("n   t (ms)   sin 30 Hz   sin 10 Hz   |ผลบวก|\n");
    for (row = 0; row < TABLE_ROWS; row++) {
        const bento_real_t time_s = (bento_real_t)row / SAMPLE_HZ;
        const bento_real_t high = wave(high_hz, time_s);
        const bento_real_t low = wave(low_hz, time_s);

        format_real(ms_text, time_s * ms_per_s, 1);
        format_real(high_text, high, SINE_DIGITS);
        format_real(low_text, low, SINE_DIGITS);
        format_real(sum_text, real_fabs(high + low), SUM_DIGITS);
        (void)printf("%-3" PRId32 " %-8s %-11s %-11s %s\n", row,
                     ms_text, high_text, low_text, sum_text);
    }
}

/* A tap on a button: back and next wrap around, restart goes to the
 * first step, play turns "play" on or off. Any click redraws the
 * step. */
static void handle_click(alias_screen_t *screen, bento_ui_id_t widget)
{
    if (widget == screen->back) {
        screen->playing = false;
        screen->step = (screen->step + (STEP_COUNT - 1)) % STEP_COUNT;
    } else if (widget == screen->next) {
        screen->playing = false;
        screen->step = (screen->step + 1) % STEP_COUNT;
    } else if (widget == screen->restart) {
        screen->playing = false;
        screen->step = 0;
    } else if (widget == screen->play) {
        screen->playing = !screen->playing;
        screen->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
    } else {
        /* a click elsewhere only redraws */
    }
    show_step(screen);
}

/* "play" steps by itself on time but never sleeps for it, so the
 * buttons still answer. */
static void play_on(alias_screen_t *screen)
{
    if (screen->playing &&
        (bento_time_ticks_diff(bento_time_ticks_ms(),
                               screen->next_ms) >= 0)) {
        screen->step++;
        if (screen->step >= STEP_COUNT) {
            screen->step = STEP_COUNT - 1;
            screen->playing = false;
        }
        screen->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
        show_step(screen);
    }
}

/* Every click since the last poll, in the order it came. */
static void handle_events(alias_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    /* ui.poll() answers no events when it fails, so a failed poll is
     * not an error here. */
    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (kind_is(&events[i], BENTO_UI_EVENT_CLICKED)) {
            handle_click(screen, events[i].id);
        }
    }
}

/* Reads the taps every LOOP_MS until Stop is pressed. */
static bento_status_t run_event_loop(alias_screen_t *screen)
{
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        handle_events(screen);
        play_on(screen);
        status = wait_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    alias_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        char text[TEXT_BYTES];

        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>aliasing และ Nyquist</h2>");
        (void)snprintf(text, sizeof(text),
                       "อัตราสุ่มตรึงที่ %" PRId32 " Hz | Nyquist %" PRId32
                       " Hz",
                       (int32_t)SAMPLE_HZ, (int32_t)nyquist_hz());
        (void)bento_lcd_print(text);
        show_step(&screen);
        print_table();
        /* the only cure is an anti-aliasing low-pass filter in
         * hardware, before the ADC */
        (void)bento_lcd_print(
            "แก้ได้ทางเดียว: anti-aliasing filter ก่อนถึง ADC");
        (void)bento_lcd_print("<span class=muted>บนบอร์ดนี้คือ C204 "
                              "ที่ "
                              "wiper ของ pot fc ~637 Hz</span>");
        status = run_event_loop(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
