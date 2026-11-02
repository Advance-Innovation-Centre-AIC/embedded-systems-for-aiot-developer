/*
 * 02_fft64_two_tones.c -- FFT radix-2 เขียนเองทั้งตัว 64 จุด (คาบ 07)
 *
 * สัญญาณสร้างเองห้าแบบ (โทนเดียว 3, 10 หรือ 15 รอบ, ผสม 3 + 10 และผสม
 * พร้อม noise ที่คำนวณได้) เข้า FFT ที่เขียนเองในไฟล์นี้ กราฟซ้ายคือ
 * สัญญาณตามเวลา ตารางไฟ 16x8 ตรงกลางคือสเปกตรัม 16 bin แรก Seg7 สองตัว
 * บอก bin สูงสุดและ Hz กดย้อน / เดินหน้า / เริ่มใหม่ เพื่อเปลี่ยนสัญญาณ
 * หรือกดเล่นรวดให้เดินเองทุก 1.6 วินาที ทำงานจนกดหยุด
 *
 * Five made-up signals (one tone of 3, 10 or 15 cycles, 3 + 10 mixed,
 * and the mix with a computed noise) go through the FFT written in this
 * file: the chart on the left is the signal over time, the 16x8 dot
 * grid in the middle the first 16 bins of its spectrum, two Seg7s the
 * peak bin and its Hz. Back / next / restart change the signal, "play"
 * steps by itself every 1.6 seconds. Runs until Stop.
 *
 * Twin of 02_fft64_two_tones.py
 * (embedded-systems-for-aiot-developer/examples/s07/02_fft64_two_tones.py),
 * started from the library twin aiot-s07-02-fft64-two-tones. Style:
 * CLEAN_C.md.
 * What C changes only because the language forces it: the lists sig,
 * re, im and mags are arrays in one work area at file scope (too big
 * for the program stack); reals are bento_real_t, the board
 * MicroPython's float width, printed through bento_fmt, which writes
 * MicroPython's "%.<n>f" text; ev["handle"] is compared as the widget
 * id the create call gave; (step - 1) % 5 keeps Python's non-negative
 * result. The note "ยอดอยู่ที่ bin ... เกิน 16 คอลัมน์" needs a peak at
 * bin 16 or above, which none of the five signals has, in either language.
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"

/* The FFT, the spectrum picture and the run. */
enum {
    SAMPLE_COUNT = 64, /* a power of two: radix-2 */
    BIN_COUNT = 32,    /* the lower half; the upper half mirrors it */
    COLS = 16,         /* the dot grid is 16 wide: the first 16 bins */
    ROWS = 8,          /* bar height in 8 steps */
    ROW_BYTES = 2,     /* 16 dots a row, one bit each */
    CHART_POINTS = 50, /* exactly the Chart's window */
    CHART_SCALE = 100, /* the chart counts in hundredths */
    SIGNAL_COUNT = 5,
    MAX_TONES = 2,
    SETTLE_MS = 200,       /* let the cleared screen settle */
    PLAY_MS = 1600,        /* the pace of play-through */
    LOOP_MS = 30,          /* how often the screen is asked */
    RESOLUTION_DIGITS = 3, /* "%.3f" of the Hz per bin */
    TEXT_BYTES = 160,      /* the longest text built here, 120 bytes */
    /* the speed-up console line: 134 bytes of text, an int32_t (11),
     * N * N (4), a number (35) and the NUL */
    SPEEDUP_LINE_BYTES = 185
};

/* Layout on an 8 px grid, outer margin 24. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    POSITION_X = 696,
    SIGNAL_Y = 56,
    HINT_X = 424,
    CAPTION_Y = 96,
    SPECTRUM_X = 312,
    PEAK_X = 520,
    HZ_X = 648,
    VIEW_Y = 128,
    CHART_W = 272,
    CHART_H = 104,
    CHART_MAX = 200,
    DOTS_W = 192,
    SEG_W = 112,
    SEG_H = 48,
    RESOLUTION_Y = 184,
    BUTTON_Y = 248,
    BUTTON_H = 88,
    BUTTON_W = 168,
    HOME_W = 144,
    NEXT_X = 224,
    PLAY_X = 424,
    HOME_X = 624,
    NOTE_Y = 352,
    FONT_TITLE = 28,
    FONT_LARGE = 24,
    FONT_TEXT = 20
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_BUTTON_ALT = 0x3A4150 /* a secondary button */
};

/* Hz: 64 samples a second, say */
static const bento_real_t SAMPLE_RATE_HZ = (bento_real_t)64.0;

static const char TEXT_HINT[] = "กดเดินหน้าเพื่อเปลี่ยนสัญญาณ";

typedef struct {
    int32_t cycles; /* cycles per window */
    bento_real_t amplitude;
} tone_t;

/* One step: its name, its tones, and noise or not. */
typedef struct {
    const char *name;
    int32_t tone_count;
    tone_t tones[MAX_TONES];
    bool noisy;
} signal_t;

static const signal_t SIGNALS[SIGNAL_COUNT] = {
    {"โทนเดียว 3 รอบ",
     1,
     {{3, (bento_real_t)1.0}, {0, (bento_real_t)0.0}},
     false},
    {"โทนเดียว 10 รอบ",
     1,
     {{10, (bento_real_t)1.0}, {0, (bento_real_t)0.0}},
     false},
    {"ผสมสองโทน 3 + 10",
     2,
     {{3, (bento_real_t)1.0}, {10, (bento_real_t)0.5}},
     false},
    {"ผสมสองโทน + noise",
     2,
     {{3, (bento_real_t)1.0}, {10, (bento_real_t)0.5}},
     true},
    {"โทนเดียว 15 รอบ",
     1,
     {{15, (bento_real_t)1.0}, {0, (bento_real_t)0.0}},
     false}};

/* The FFT's work area: the signal, its real and imaginary parts, and
 * the magnitudes of the lower half. */
typedef struct {
    bento_real_t signal[SAMPLE_COUNT];
    bento_real_t real[SAMPLE_COUNT];
    bento_real_t imag[SAMPLE_COUNT];
    bento_real_t magnitude[BIN_COUNT];
} fft_work_t;

/* The widgets the program changes, and where the walk is. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t position;
    bento_ui_id_t signal;
    bento_ui_id_t hint;
    bento_ui_id_t chart;
    uint8_t time_series;
    bento_ui_id_t dots;
    bento_ui_id_t peak_bin;
    bento_ui_id_t peak_hz;
    bento_ui_id_t resolution;
    bento_ui_id_t back;
    bento_ui_id_t next;
    bento_ui_id_t play;
    bento_ui_id_t home;
    bento_ui_id_t note;
    int32_t step;     /* the program's truth: which signal is shown */
    bool playing;     /* "play" is stepping by itself */
    uint32_t next_ms; /* when "play" steps next */
    int32_t stages;   /* log2 of SAMPLE_COUNT, counted by shifting */
    bento_real_t speedup;
} fft_screen_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load, and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern fft_work_t g_work;
fft_work_t g_work;

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

static bento_real_t real_cos(bento_real_t radians)
{
#if BENTO_FMT_REAL_DOUBLE
    return cos(radians);
#else
    return cosf(radians);
#endif
}

static bento_real_t real_sin(bento_real_t radians)
{
#if BENTO_FMT_REAL_DOUBLE
    return sin(radians);
#else
    return sinf(radians);
#endif
}

static bento_real_t real_sqrt(bento_real_t value)
{
#if BENTO_FMT_REAL_DOUBLE
    return sqrt(value);
#else
    return sqrtf(value);
#endif
}

/* int() of a real: truncates toward zero, as the cast does. A function,
 * so that the cast never applies to a composite expression (MISRA 10.8). */
static int32_t truncate_to_int(bento_real_t value)
{
    return (int32_t)value;
}

/* "%.<digits>f" of value into text, which holds BENTO_FMT_BUF_SIZE
 * bytes: enough for every result, so the format cannot fail. */
static void format_real(char *text, bento_real_t value, int32_t digits)
{
    uint32_t length = 0U;

    (void)bento_fmt_real_precision(text, BENTO_FMT_BUF_SIZE, value,
                                   digits, &length);
}

/* The repeated even-odd split leaves the samples in bit-reversed order,
 * so they are put in that order first. */
static int32_t bit_reverse(int32_t index, int32_t bits)
{
    uint32_t rest = (uint32_t)index;
    uint32_t reversed = 0U;
    int32_t bit;

    for (bit = 0; bit < bits; bit++) {
        reversed = (reversed << 1U) | (rest & 1U);
        rest >>= 1U;
    }
    return (int32_t)reversed;
}

static void swap_reals(bento_real_t *first, bento_real_t *second)
{
    const bento_real_t kept = *first;

    *first = *second;
    *second = kept;
}

static void reorder(fft_work_t *work, int32_t bits)
{
    int32_t i;

    for (i = 0; i < SAMPLE_COUNT; i++) {
        const int32_t j = bit_reverse(i, bits);

        if (j > i) {
            swap_reals(&work->real[i], &work->real[j]);
            swap_reals(&work->imag[i], &work->imag[j]);
        }
    }
}

/* One butterfly: samples a and b, twiddle factor (wr, wi). */
static void butterfly(fft_work_t *work, int32_t a, int32_t b,
                      bento_real_t wr, bento_real_t wi)
{
    const bento_real_t tr = (wr * work->real[b]) - (wi * work->imag[b]);
    const bento_real_t ti = (wr * work->imag[b]) + (wi * work->real[b]);
    const bento_real_t real_a = work->real[a];
    const bento_real_t imag_a = work->imag[a];

    work->real[b] = real_a - tr;
    work->imag[b] = imag_a - ti;
    work->real[a] = real_a + tr;
    work->imag[a] = imag_a + ti;
}

/* One stage: every block of this size merged by its butterflies. */
static void run_stage(fft_work_t *work, int32_t size)
{
    const bento_real_t math_pi = (bento_real_t)3.141592653589793;
    const bento_real_t step =
        ((bento_real_t)-2 * math_pi) / (bento_real_t)size;
    const int32_t half = size / 2;
    int32_t start;

    for (start = 0; start < SAMPLE_COUNT; start += size) {
        int32_t k;

        for (k = 0; k < half; k++) {
            const bento_real_t angle = step * (bento_real_t)k;

            butterfly(work, start + k, start + k + half,
                      real_cos(angle), real_sin(angle));
        }
    }
}

/* Radix-2 in place: merged back one stage at a time, 2, 4, ..., 64. */
static void fft(fft_work_t *work, int32_t bits)
{
    int32_t size = 2;

    reorder(work, bits);
    while (size <= SAMPLE_COUNT) {
        run_stage(work, size);
        size <<= 1;
    }
}

/* A made-up signal, so the right answer is known in advance and the
 * FFT can be judged. */
static void make_signal(const signal_t *source, bento_real_t *out)
{
    const bento_real_t two_pi =
        (bento_real_t)2 * (bento_real_t)3.141592653589793;
    int32_t i;

    for (i = 0; i < SAMPLE_COUNT; i++) {
        bento_real_t value = (bento_real_t)0.0;
        int32_t t;

        for (t = 0; t < source->tone_count; t++) {
            const tone_t *tone = &source->tones[t];
            const bento_real_t phase =
                ((two_pi * (bento_real_t)tone->cycles) *
                 (bento_real_t)i) /
                (bento_real_t)SAMPLE_COUNT;

            value += tone->amplitude * real_cos(phase);
        }
        if (source->noisy) {
            /* computed noise, not random: the same numbers every run */
            const bento_real_t noise_size = (bento_real_t)0.25;
            const bento_real_t sine_rate = (bento_real_t)12.9898;
            const bento_real_t cosine_rate = (bento_real_t)4.1414;
            const bento_real_t noise =
                noise_size * real_sin((bento_real_t)i * sine_rate);

            value += noise * real_cos((bento_real_t)i * cosine_rate);
        }
        out[i] = value;
    }
}

/* The bars as DotMatrix bytes: one bit per dot, row first, MSB first;
 * bars grow from the bottom and row 0 is the top. */
static void spectrum_bytes(const int32_t *heights, uint8_t *out)
{
    const uint32_t byte_mask = 0xFFU;
    const uint32_t byte_bits = 8U;
    int32_t row;

    for (row = 0; row < ROWS; row++) {
        uint32_t word = 0U;
        int32_t column;

        for (column = 0; column < COLS; column++) {
            const bool lit = heights[column] >= (ROWS - row);

            word = (word << 1) | (lit ? 1U : 0U);
        }
        const size_t first = (size_t)row * (size_t)ROW_BYTES;

        out[first] = (uint8_t)((word >> byte_bits) & byte_mask);
        out[first + 1U] = (uint8_t)(word & byte_mask);
    }
}

/* Each bin times 2/N reads as the amplitude of the wave put in; the
 * answer is the bin with the largest one. */
static int32_t find_peak(fft_work_t *work)
{
    int32_t peak = 0;
    int32_t k;

    for (k = 0; k < BIN_COUNT; k++) {
        const bento_real_t power = (work->real[k] * work->real[k]) +
                                   (work->imag[k] * work->imag[k]);

        work->magnitude[k] = (real_sqrt(power) * (bento_real_t)2) /
                             (bento_real_t)SAMPLE_COUNT;
    }
    for (k = 0; k < BIN_COUNT; k++) {
        if (work->magnitude[k] > work->magnitude[peak]) {
            peak = k;
        }
    }
    return peak;
}

/* The 16 bars; a bar with any value is at least one step high, so it
 * never vanishes by rounding down. */
static void show_spectrum(const fft_screen_t *screen,
                          const fft_work_t *work)
{
    const bento_real_t smallest = (bento_real_t)0.02;
    const bento_real_t half = (bento_real_t)0.5;
    int32_t heights[COLS];
    uint8_t bits[ROWS * ROW_BYTES];
    int32_t column;

    for (column = 0; column < COLS; column++) {
        const bento_real_t magnitude = work->magnitude[column];
        const int32_t height =
            (int32_t)((magnitude * (bento_real_t)ROWS) + half);

        heights[column] =
            (height != 0) ? height : ((magnitude > smallest) ? 1 : 0);
    }
    spectrum_bytes(heights, bits);
    /* DotMatrix.set_pixels() ignores a refusal (modui.c) */
    (void)bento_ui_dotmatrix_set(screen->ui, screen->dots, bits,
                                 (uint32_t)sizeof(bits));
}

static void set_text(const fft_screen_t *screen, bento_ui_id_t label,
                     const char *text)
{
    /* MicroPython's widget setters ignore a refusal (modui.c), so
     * the twin does too. */
    (void)bento_ui_set_text(screen->ui, label, text,
                            (uint32_t)strlen(text));
}

/* The note under the buttons: the peak's size and the FFT's speed-up,
 * or that the peak lies past the grid. */
static void show_note(const fft_screen_t *screen,
                      bento_real_t peak_size, int32_t peak)
{
    char text[TEXT_BYTES];
    char number[BENTO_FMT_BUF_SIZE];

    if (peak >= COLS) {
        (void)snprintf(text, sizeof(text),
                       "ยอดอยู่ที่ bin %" PRId32 " ซึ่งเกิน %d "
                       "คอลัมน์ที่ตารางวาดได้",
                       peak, COLS);
    } else {
        format_real(number, peak_size, 2);
        (void)snprintf(text, sizeof(text),
                       "ยอดสูงสุด %s | FFT เร็วกว่า DFT %" PRId32 " เท่า",
                       number, (int32_t)screen->speedup);
    }
    set_text(screen, screen->note, text);
}

/* Every label and Seg7 for this step, and the console line. */
static void show_texts(const fft_screen_t *screen, int32_t peak,
                       bento_real_t peak_size)
{
    const char *name = SIGNALS[screen->step].name;
    const int32_t peak_hz =
        truncate_to_int(((bento_real_t)peak * SAMPLE_RATE_HZ) /
                        (bento_real_t)SAMPLE_COUNT);
    char text[TEXT_BYTES];
    char number[BENTO_FMT_BUF_SIZE];

    (void)snprintf(text, sizeof(text), "%" PRId32 " / %d",
                   screen->step + 1, SIGNAL_COUNT);
    set_text(screen, screen->position, text);
    (void)snprintf(text, sizeof(text), "ใส่เข้าไป: %s", name);
    set_text(screen, screen->signal, text);
    (void)snprintf(text, sizeof(text), "%" PRId32, peak);
    set_text(screen, screen->peak_bin, text);
    (void)snprintf(text, sizeof(text), "%" PRId32, peak_hz);
    set_text(screen, screen->peak_hz, text);
    format_real(number, SAMPLE_RATE_HZ / (bento_real_t)SAMPLE_COUNT,
                RESOLUTION_DIGITS);
    (void)snprintf(text, sizeof(text), "1 bin = %s Hz", number);
    set_text(screen, screen->resolution, text);
    show_note(screen, peak_size, peak);
    set_text(screen, screen->hint,
             screen->playing ? "กำลังเล่นรวด - กดปุ่มไหนก็หยุด" : TEXT_HINT);
    (void)snprintf(text, sizeof(text),
                   "%s -> ยอดที่ bin %" PRId32 " = %" PRId32 " Hz", name,
                   peak, peak_hz);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
}

/* The FFT of the current step, both views on the screen. All 50 chart
 * points replace the old picture; the FFT still uses all 64 samples. */
static void show_step(const fft_screen_t *screen, fft_work_t *work)
{
    const bento_real_t scale = (bento_real_t)CHART_SCALE;
    int32_t peak;
    int32_t i;

    make_signal(&SIGNALS[screen->step], work->signal);
    for (i = 0; i < CHART_POINTS; i++) {
        /* Chart.set_next() ignores a refusal (modui.c) */
        (void)bento_ui_chart_set_next(
            screen->ui, screen->chart, screen->time_series,
            truncate_to_int(work->signal[i] * scale));
    }
    for (i = 0; i < SAMPLE_COUNT; i++) {
        work->real[i] = work->signal[i];
        work->imag[i] = (bento_real_t)0.0;
    }
    fft(work, screen->stages);
    peak = find_peak(work);
    show_spectrum(screen, work);
    show_texts(screen, peak, work->magnitude[peak]);
}

/* A label in this font and colour. */
static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const int16_t place[2], uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place[0];
    args.y = place[1];
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A button of the bottom row. */
static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 const int16_t span[2], uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = span[0];
    args.y = BUTTON_Y;
    args.w = span[1];
    args.h = BUTTON_H;
    args.color = color;
    args.font_px = FONT_LARGE;
    return bento_ui_button_create(ui, &args, out_id);
}

/* A Seg7 of the right column; its font is fixed, h sets the digits. */
static bento_status_t add_seg7(bento_ui_t *ui, int16_t x,
                               bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = x;
    args.y = VIEW_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = COLOR_ACCENT;
    return bento_ui_seg7_create(ui, &args, out_id);
}

/* The title row and the signal row. */
static bento_status_t add_title_rows(fft_screen_t *screen)
{
    static const int16_t title_place[2] = {MARGIN_X, TITLE_Y};
    static const int16_t position_place[2] = {POSITION_X, TITLE_Y};
    static const int16_t signal_place[2] = {MARGIN_X, SIGNAL_Y};
    static const int16_t hint_place[2] = {HINT_X, SIGNAL_Y};
    bento_ui_t *ui = screen->ui;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "FFT 64 จุด เขียนเอง", title_place, COLOR_TEXT,
                  FONT_TITLE, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "1 / 5", position_place, COLOR_DIM,
                           FONT_LARGE, &screen->position);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "กำลังเริ่ม", signal_place, COLOR_TEXT,
                           FONT_LARGE, &screen->signal);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, TEXT_HINT, hint_place, COLOR_DIM,
                           FONT_TEXT, &screen->hint);
    }
    return status;
}

/* The four captions above the views. */
static bento_status_t add_captions(bento_ui_t *ui)
{
    static const int16_t time_place[2] = {MARGIN_X, CAPTION_Y};
    static const int16_t spectrum_place[2] = {SPECTRUM_X, CAPTION_Y};
    static const int16_t peak_place[2] = {PEAK_X, CAPTION_Y};
    static const int16_t hz_place[2] = {HZ_X, CAPTION_Y};
    bento_ui_id_t unused;
    bento_status_t status = add_label(ui, "สัญญาณตามเวลา", time_place,
                                      COLOR_DIM, FONT_TEXT, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "สเปกตรัม 16 bin", spectrum_place,
                           COLOR_DIM, FONT_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "bin สูงสุด", peak_place, COLOR_DIM,
                           FONT_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "คิดเป็น Hz", hz_place, COLOR_DIM,
                           FONT_TEXT, &unused);
    }
    return status;
}

/* The time chart with its one series, and the 16x8 dot grid; w and h
 * are the pixel box, cols and rows the dots. */
static bento_status_t add_views(fft_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = VIEW_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.color = COLOR_CARD;
    args.min_val = -CHART_MAX;
    args.max_val = CHART_MAX;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_ACCENT,
                                           &screen->time_series);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = SPECTRUM_X;
        args.y = VIEW_Y;
        args.w = DOTS_W;
        args.h = CHART_H;
        args.min_val = COLS;
        args.max_val = ROWS;
        status =
            bento_ui_dotmatrix_create(screen->ui, &args, &screen->dots);
    }
    return status;
}

/* The two Seg7s and the resolution label of the right column. */
static bento_status_t add_readouts(fft_screen_t *screen)
{
    static const int16_t place[2] = {PEAK_X, RESOLUTION_Y};
    bento_status_t status =
        add_seg7(screen->ui, PEAK_X, &screen->peak_bin);

    if (status == BENTO_OK) {
        status = add_seg7(screen->ui, HZ_X, &screen->peak_hz);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "1 bin = fs/N Hz", place,
                           COLOR_DIM, FONT_TEXT, &screen->resolution);
    }
    return status;
}

/* The four buttons, h=88 and 32 apart, then the note row. */
static bento_status_t add_buttons(fft_screen_t *screen)
{
    static const int16_t back_span[2] = {MARGIN_X, BUTTON_W};
    static const int16_t next_span[2] = {NEXT_X, BUTTON_W};
    static const int16_t play_span[2] = {PLAY_X, BUTTON_W};
    static const int16_t home_span[2] = {HOME_X, HOME_W};
    static const int16_t note_place[2] = {MARGIN_X, NOTE_Y};
    bento_ui_t *ui = screen->ui;
    bento_status_t status = add_button(ui, "< ย้อน", back_span,
                                       COLOR_BUTTON_ALT, &screen->back);

    if (status == BENTO_OK) {
        status = add_button(ui, "เดินหน้า >", next_span, COLOR_ACCENT,
                            &screen->next);
    }
    if (status == BENTO_OK) {
        status = add_button(ui, ">> เล่นรวด", play_span, COLOR_ACCENT,
                            &screen->play);
    }
    if (status == BENTO_OK) {
        status = add_button(ui, "เริ่มใหม่", home_span, COLOR_BUTTON_ALT,
                            &screen->home);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, TEXT_HINT, note_place, COLOR_DIM,
                           FONT_TEXT, &screen->note);
    }
    return status;
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

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(fft_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_title_rows(screen);
    }
    if (status == BENTO_OK) {
        status = add_captions(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_views(screen);
    }
    if (status == BENTO_OK) {
        status = add_readouts(screen);
    }
    if (status == BENTO_OK) {
        status = add_buttons(screen);
    }
    return status;
}

/* A tap: back and next wrap around, restart goes to the first signal,
 * play turns "play" on or off. Any click redraws the step. */
static void handle_click(fft_screen_t *screen, bento_ui_id_t widget)
{
    if (widget == screen->back) {
        screen->playing = false;
        screen->step =
            (screen->step + (SIGNAL_COUNT - 1)) % SIGNAL_COUNT;
    } else if (widget == screen->next) {
        screen->playing = false;
        screen->step = (screen->step + 1) % SIGNAL_COUNT;
    } else if (widget == screen->home) {
        screen->playing = false;
        screen->step = 0;
    } else if (widget == screen->play) {
        screen->playing = !screen->playing;
        screen->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
    } else {
        /* a click elsewhere only redraws */
    }
    show_step(screen, &g_work);
}

/* "play" steps by itself on time but never sleeps for it, so the
 * buttons still answer. */
static void play_on(fft_screen_t *screen)
{
    if (screen->playing &&
        (bento_time_ticks_diff(bento_time_ticks_ms(),
                               screen->next_ms) >= 0)) {
        screen->step++;
        if (screen->step >= SIGNAL_COUNT) {
            screen->step = SIGNAL_COUNT - 1;
            screen->playing = false;
        }
        screen->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
        show_step(screen, &g_work);
    }
}

/* True when the event is of this kind (a uint8_t, so both sides of
 * == have one type). */
static bool kind_is(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

static void handle_events(fft_screen_t *screen)
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
static bento_status_t run_event_loop(fft_screen_t *screen)
{
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        handle_events(screen);
        play_on(screen);
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* Complex multiplications of the FFT: N log2(N) / 2, floor division
 * as in the original. */
static int32_t fft_products(const fft_screen_t *screen)
{
    return (SAMPLE_COUNT * screen->stages) / 2;
}

/* The console header, and the two numbers that are why the FFT exists
 * on the serial line. */
static void print_numbers(const fft_screen_t *screen)
{
    const int32_t products = fft_products(screen);
    char text[SPEEDUP_LINE_BYTES];
    char number[BENTO_FMT_BUF_SIZE];

    format_real(number, SAMPLE_RATE_HZ / (bento_real_t)SAMPLE_COUNT,
                RESOLUTION_DIGITS);
    (void)printf("ความละเอียดต่อ bin = fs/N = %s Hz\n", number);
    format_real(number, screen->speedup, 0);
    (void)snprintf(text, sizeof(text),
                   "คูณเชิงซ้อน FFT ~ %" PRId32
                   "  เทียบ DFT ตรง ๆ %d ครั้ง  (เร็วขึ้น %s เท่า)",
                   products, SAMPLE_COUNT * SAMPLE_COUNT, number);
    (void)printf("%s\n", text);
}

static void open_console(void)
{
    char text[TEXT_BYTES];

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>FFT 64 จุด เขียนเอง</h2>");
    (void)snprintf(text, sizeof(text),
                   "N = %d | fs = %" PRId32 " Hz | %d สัญญาณ",
                   SAMPLE_COUNT, (int32_t)SAMPLE_RATE_HZ, SIGNAL_COUNT);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
}

int main(void)
{
    fft_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(&g_work, 0, sizeof(g_work));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        /* stages counted by shifting, no float log */
        while ((1U << (uint32_t)screen.stages) <
               (uint32_t)SAMPLE_COUNT) {
            screen.stages++;
        }
        screen.speedup =
            ((bento_real_t)SAMPLE_COUNT * (bento_real_t)SAMPLE_COUNT) /
            (bento_real_t)fft_products(&screen);
        open_console();
        show_step(&screen, &g_work);
        print_numbers(&screen);
        status = run_event_loop(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
