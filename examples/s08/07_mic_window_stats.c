/*
 * 07_mic_window_stats.c -- รูปคลื่นดิบ สามค่าจากหน้าต่างเดียว และคิวที่ค้างอยู่
 * (คาบ 08)
 *
 * อ่าน mic.stats() ครั้งเดียวได้ rms peak dc จากหน้าต่างเดียวกัน แล้วอ่าน
 * mic.lag() ว่าคิวเสียงค้างกี่ ms กับรูปคลื่นดิบ 50 จุดจาก read() ขึ้นกราฟ
 * 1500 รอบ แถบขวาคือคิวที่ค้าง 0-625 ms ปุ่มซ้ายสลับ fresh (True แถบเกือบว่าง
 * False แถบเต็ม) ปุ่มขวาถ่วงลูป 300 ms ให้เห็นคิวโตเร็วแค่ไหนตอน fresh ปิด
 * จบแล้วปิดไมค์เสมอ บอร์ด: Eva Kit
 *
 * Reads mic.stats() once for rms, peak and dc of the same window, then
 * mic.lag() (how many ms of audio wait in the queue) and a 50-point raw
 * waveform from read() for the chart, for 1500 rounds. The bar on the
 * right is the queue, 0-625 ms. The left button toggles fresh (True:
 * the bar stays nearly empty; False: it fills); the right button slows
 * the loop by 300 ms to show how fast the queue grows with fresh off.
 * The microphone is always closed at the end. Board: Eva Kit.
 *
 * Twin of 07_mic_window_stats.py
 * (embedded-systems-for-aiot-developer/examples/s08/07_mic_window_stats.py).
 * Style: CLEAN_C.md.
 * The C API has no mic module, so the few calls of the board's frozen
 * mic.py (ports/psoc-edge/freeze/mic.py) are reproduced here on
 * bento_machine_pdm: start(sens, samples) opens and starts PDM0 (16
 * kHz, decimation 96, MONO_RIGHT, the sens-to-gain table) and throws
 * away two reads 20 ms apart; stats(fresh) is PDM_PCM.stats; lag() is
 * the queue in ms; read(n) fills the whole window (a short read is the
 * binding's OSError, BENTO_E_TIMEOUT here), takes the floor-divided
 * mean off every sample and keeps the first n. bento_machine_pdm is a
 * PSoC Edge API: the reTerminal has no PDM microphone (its port answers
 * unsupported). Python's try/finally becomes close_microphone(), which
 * main() calls once. The loop sleeps only when slowed, so Stop is
 * checked every round.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_machine.h"
#include "bento_runtime.h"

/* The program's settings. */
enum {
    SENSITIVITY = 3, /* 1 to 5; anything else quietly becomes 3 */
    PLOT_N = 50,     /* the chart is 50 points wide */
    RING_MS = 625,   /* what the audio queue holds */
    SLOW_MS = 300,   /* the right button slows the loop this much */
    ROUNDS = 1500,
    WAVE_LIMIT = 8000, /* the chart's Y range, either side of zero */
    TEXT_BYTES = 128   /* Thai text and two int32_t numbers */
};

/* mic.py's numbers (ports/psoc-edge/freeze/mic.py). */
enum {
    MIC_RATE = 16000,
    MIC_DECIMATION = 96,
    MIC_BITS = 16,
    SAMPLES = 256, /* the window: 16 ms of real time at 16 kHz */
    SAMPLE_BYTES = 2,
    MIC_BUFFER_BYTES = SAMPLES * SAMPLE_BYTES,
    WARM_UP_READS = 2,
    WARM_UP_MS = 20,
    READ_LIMIT_MS = 2000, /* the binding's read time-out */
    SENSITIVITY_COUNT = 5,
    BYTE_BITS = 8
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 12,
    TITLE_Y = 8,
    CHART_X = 12,
    CHART_Y = 44,
    CHART_W = 472,
    CHART_H = 172,
    RIGHT_X = 500,
    NOTE_Y = 46,
    RMS_Y = 76,
    PEAK_Y = 108,
    DC_Y = 140,
    QUEUE_NOTE_X = 448,
    QUEUE_NOTE_Y = 222,
    BAR_Y = 196,
    BAR_W = 252,
    BAR_H = 20,
    LAG_Y = 260,
    FRESH_X = 12,
    FRESH_Y = 236,
    SLOW_X = 248,
    SLOW_Y = 260,
    BUTTON_W = 220,
    BUTTON_H = 88,
    STATE_X = 476,
    STATE_Y = 300,
    HINT_X = 12,
    HINT_Y = 354
};

/* Font sizes, in pixels, and colours, 0xRRGGBB. */
enum {
    FONT_TITLE = 24,
    FONT_TEXT = 20,
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_OK = 0x30A46C,
    COLOR_QUEUE_LOW = 0x55DD55,
    COLOR_QUEUE_HIGH = 0xFF5555,
    COLOR_FRESH_ON = 0x2E7D32,
    COLOR_FRESH_OFF = 0xC62828
};

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * and set again at the start of main() (CLEAN_C.md C9). */
extern uint8_t g_mic_buffer[MIC_BUFFER_BYTES];
uint8_t g_mic_buffer[MIC_BUFFER_BYTES]; /* one window of samples */

/* A place on the screen; a label uses only x and y. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

/* The PDM microphone and whether it needs closing. */
typedef struct {
    uint32_t handle;
    bool open;
} microphone_t;

/* rms, peak and dc of one window, and the queue read after it. */
typedef struct {
    int32_t rms;
    int32_t peak;
    int32_t dc;
    int32_t queued_ms;
} window_t;

/* The widgets and what the loop remembers. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    bento_ui_id_t rms_label;
    bento_ui_id_t peak_label;
    bento_ui_id_t dc_label;
    bento_ui_id_t lag_bar;
    bento_ui_id_t lag_label;
    bento_ui_id_t fresh_button;
    bento_ui_id_t slow_button;
    bento_ui_id_t state;
    microphone_t microphone;
    bool fresh;
    bool slow;
    int32_t loudest;
} scope_t;

/* mic.py's sens-to-gain table; the chip's gain runs backwards, and a
 * sens outside 1 to 5 is sens 3. */
static uint32_t gain_for(int32_t sensitivity)
{
    static const uint32_t gains[SENSITIVITY_COUNT] = {17U, 15U, 13U,
                                                      11U, 9U};
    int32_t place = sensitivity - 1;

    if ((place < 0) || (place >= SENSITIVITY_COUNT)) {
        place = SENSITIVITY - 1;
    }
    return gains[place];
}

/* start() throws away two windows 20 ms apart: the first audio after
 * opening is not settled. A failed read is ignored there too. */
static bento_status_t warm_up(uint32_t handle)
{
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < WARM_UP_READS) && (status == BENTO_OK); i++) {
        uint32_t got = 0U;

        (void)bento_machine_pdm_read(
            handle, g_mic_buffer, MIC_BUFFER_BYTES, READ_LIMIT_MS,
            &got); /* start() ignores OSError */
        status = bento_time_sleep_ms(WARM_UP_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

/* mic.start(sens, samples): PDM0 configured, capturing and warmed
 * up; the window is SAMPLES long (g_mic_buffer). */
static bento_status_t open_microphone(microphone_t *microphone,
                                      int32_t sensitivity)
{
    bento_machine_pdm_cfg_t config;
    const uint32_t gain = gain_for(sensitivity);
    bento_status_t status;

    bento_machine_pdm_cfg_init(&config);
    config.sample_rate = MIC_RATE;
    config.decimation_rate = MIC_DECIMATION;
    config.bits = MIC_BITS;
    config.format = BENTO_MACHINE_PDM_MONO_RIGHT;
    config.left_gain = gain;
    config.right_gain = gain;
    status = bento_machine_pdm_open(&config, &microphone->handle);
    if (status == BENTO_OK) {
        microphone->open = true;
        status = bento_machine_pdm_start(microphone->handle);
    }
    if (status == BENTO_OK) {
        status = warm_up(microphone->handle);
    }
    return status;
}

/* mic.stop(): the cleanup, whatever ended the program. */
static void close_microphone(microphone_t *microphone)
{
    if (microphone->open) {
        (void)bento_machine_pdm_close(
            microphone->handle); /* stop() swallows every error */
        microphone->open = false;
    }
}

/* mic.stats(fresh) and then mic.lag(): lag is read after stats, since
 * fresh has just dropped the old audio. */
static bento_status_t read_window(const microphone_t *microphone,
                                  bool fresh, window_t *window)
{
    bento_machine_pdm_stats_t stats;
    uint32_t pending_ms = 0U;
    bento_status_t status;

    bento_machine_pdm_stats_init(&stats);
    status = bento_machine_pdm_stats(microphone->handle, g_mic_buffer,
                                     MIC_BUFFER_BYTES, fresh, &stats);
    if (status == BENTO_OK) {
        status = bento_machine_pdm_pending_ms(microphone->handle,
                                              &pending_ms);
    }
    window->rms = (int32_t)stats.rms;
    window->peak = (int32_t)stats.peak;
    window->dc = stats.dc;
    window->queued_ms = (int32_t)pending_ms;
    return status;
}

/* The sample at this place in the window: 16-bit little-endian. */
static int32_t sample_at(int32_t place)
{
    const uint32_t first = (uint32_t)place * (uint32_t)SAMPLE_BYTES;
    const uint16_t bits = (uint16_t)((uint32_t)g_mic_buffer[first] |
                                     ((uint32_t)g_mic_buffer[first + 1U]
                                      << (uint32_t)BYTE_BITS));

    return (int32_t)(int16_t)bits;
}

/* Python's floor division of the samples' sum by their count. */
static int32_t floor_mean(int32_t total, int32_t count)
{
    int32_t mean = total / count;

    if (((total % count) != 0) && (total < 0)) {
        mean--;
    }
    return mean;
}

/* mic.read(n): the whole window, without dropping old audio, its mean
 * taken off every sample; the first n samples go to wave[]. */
static bento_status_t read_wave(const microphone_t *microphone,
                                int32_t wave[PLOT_N])
{
    uint32_t got = 0U;
    int32_t total = 0;
    int32_t mean;
    int32_t i;
    bento_status_t status =
        bento_machine_pdm_read(microphone->handle, g_mic_buffer,
                               MIC_BUFFER_BYTES, READ_LIMIT_MS, &got);

    if ((status == BENTO_OK) && (got < (uint32_t)MIC_BUFFER_BYTES)) {
        status = BENTO_E_TIMEOUT;
    }
    if (status == BENTO_OK) {
        for (i = 0; i < SAMPLES; i++) {
            total += sample_at(i);
        }
        mean = floor_mean(total, SAMPLES);
        for (i = 0; i < PLOT_N; i++) {
            wave[i] = sample_at(i) - mean;
        }
    }
    return status;
}

static void set_text(const scope_t *scope, bento_ui_id_t id,
                     const char *text)
{
    /* a setter's refusal was ignored before too (modui.c) */
    (void)bento_ui_set_text(scope->ui, id, text,
                            (uint32_t)strlen(text));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const rect_t *place, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.font_px = font_px;
    args.color = color;
    return bento_ui_label_create(ui, &args, out_id);
}

static void box_args(bento_ui_widget_args_t *args, const rect_t *box)
{
    bento_ui_widget_args_init(args);
    args->x = box->x;
    args->y = box->y;
    args->w = box->w;
    args->h = box->h;
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 const rect_t *box, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    box_args(&args, box);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.color = color;
    args.font_px = FONT_TEXT;
    return bento_ui_button_create(ui, &args, out_id);
}

/* The title, the waveform chart and the three numbers. */
static bento_status_t add_numbers(scope_t *scope)
{
    static const rect_t title = {TITLE_X, TITLE_Y, 0, 0};
    static const rect_t chart = {CHART_X, CHART_Y, CHART_W, CHART_H};
    static const rect_t note = {RIGHT_X, NOTE_Y, 0, 0};
    static const rect_t rms = {RIGHT_X, RMS_Y, 0, 0};
    static const rect_t peak = {RIGHT_X, PEAK_Y, 0, 0};
    static const rect_t dc = {RIGHT_X, DC_Y, 0, 0};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(scope->ui, "รูปคลื่นดิบ สามค่า และคิวเสียง", &title, 0U,
                  FONT_TITLE, &unused);

    if (status == BENTO_OK) {
        box_args(&args, &chart);
        args.min_val = -WAVE_LIMIT;
        args.max_val = WAVE_LIMIT;
        status = bento_ui_chart_create(scope->ui, &args, &scope->chart);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "rms peak dc จากครั้งเดียว", &note,
                           COLOR_DIM, FONT_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "rms 0", &rms, COLOR_ACCENT,
                           FONT_TEXT, &scope->rms_label);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "peak 0", &peak, COLOR_WARN,
                           FONT_TEXT, &scope->peak_label);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "dc 0", &dc, COLOR_DIM, FONT_TEXT,
                           &scope->dc_label);
    }
    return status;
}

/* The queue: its note, its bar and its line. */
static bento_status_t add_queue(scope_t *scope)
{
    static const rect_t note = {QUEUE_NOTE_X, QUEUE_NOTE_Y, 0, 0};
    static const rect_t bar = {RIGHT_X, BAR_Y, BAR_W, BAR_H};
    static const rect_t lag = {RIGHT_X, LAG_Y, 0, 0};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(scope->ui, "คิวค้างอยู่กี่ ms (เต็มที่ 625)", &note, COLOR_DIM,
                  FONT_TEXT, &unused);

    if (status == BENTO_OK) {
        box_args(&args, &bar);
        args.min_val = 0;
        args.max_val = RING_MS;
        status = bento_ui_bar_create(scope->ui, &args, &scope->lag_bar);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "lag 0 ms", &lag, COLOR_OK,
                           FONT_TEXT, &scope->lag_label);
    }
    return status;
}

/* The two buttons, the state line and the hint. */
static bento_status_t add_controls(scope_t *scope)
{
    static const rect_t fresh = {FRESH_X, FRESH_Y, BUTTON_W, BUTTON_H};
    static const rect_t slow = {SLOW_X, SLOW_Y, BUTTON_W, BUTTON_H};
    static const rect_t state = {STATE_X, STATE_Y, 0, 0};
    static const rect_t hint = {HINT_X, HINT_Y, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status =
        add_button(scope->ui, "fresh = True", &fresh, COLOR_OK,
                   &scope->fresh_button);

    if (status == BENTO_OK) {
        status = add_button(scope->ui, "ลูปปกติ", &slow, COLOR_DIM,
                            &scope->slow_button);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "กำลังเปิดไมค์", &state, 0U,
                           FONT_TEXT, &scope->state);
    }
    if (status == BENTO_OK) {
        status = add_label(scope->ui, "fresh ปิดเมื่อไร คิวจะไต่ขึ้นจนเต็ม 625",
                           &hint, COLOR_DIM, FONT_TEXT, &unused);
    }
    return status;
}

/* ui.poll() with its answer: a failed poll answers no events. */
static uint32_t
poll_events(bento_ui_t *ui,
            bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX])
{
    uint32_t count = 0U;

    if (bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    return count;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(scope_t *scope)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    bento_status_t status =
        bento_ui_screen(scope->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_numbers(scope);
    }
    if (status == BENTO_OK) {
        status = add_queue(scope);
    }
    if (status == BENTO_OK) {
        status = add_controls(scope);
    }
    if (status == BENTO_OK) {
        (void)poll_events(scope->ui, events); /* events not used yet */
    }
    return status;
}

/* The waveform onto the chart, each point held inside the Y range. */
static void plot_wave(const scope_t *scope, const int32_t wave[PLOT_N])
{
    int32_t i;

    for (i = 0; i < PLOT_N; i++) {
        int32_t point = wave[i];

        if ((point <= -WAVE_LIMIT) || (point >= WAVE_LIMIT)) {
            point = (point > 0) ? WAVE_LIMIT : -WAVE_LIMIT;
        }
        (void)bento_ui_chart_set_next(scope->ui, scope->chart, 0U,
                                      point);
    }
}

/* The three numbers, the queue bar and its line, coloured red once
 * the queue is past half: the screen then shows the past. */
static void show_window(const scope_t *scope, const window_t *window)
{
    char text[TEXT_BYTES];
    const int32_t queued = window->queued_ms;

    (void)snprintf(text, sizeof(text), "rms %" PRId32, window->rms);
    set_text(scope, scope->rms_label, text);
    (void)snprintf(text, sizeof(text), "peak %" PRId32, window->peak);
    set_text(scope, scope->peak_label, text);
    (void)snprintf(text, sizeof(text), "dc %" PRId32, window->dc);
    set_text(scope, scope->dc_label, text);
    (void)bento_ui_set_value(scope->ui, scope->lag_bar,
                             (queued < RING_MS) ? queued : RING_MS);
    (void)snprintf(text, sizeof(text), "lag %" PRId32 " ms", queued);
    set_text(scope, scope->lag_label, text);
    (void)bento_ui_set_color(
        scope->ui, scope->lag_label,
        (queued < (RING_MS / 2)) ? COLOR_QUEUE_LOW : COLOR_QUEUE_HIGH);
}

/* The left button: fresh flips, and the console says the queue now. */
static void toggle_fresh(scope_t *scope, int32_t queued_ms)
{
    char line[TEXT_BYTES];
    const char *name;

    scope->fresh = !scope->fresh;
    name = scope->fresh ? "True" : "False";
    (void)snprintf(line, sizeof(line), "fresh = %s", name);
    set_text(scope, scope->fresh_button, line);
    (void)bento_ui_set_color(scope->ui, scope->fresh_button,
                             scope->fresh ? COLOR_FRESH_ON
                                          : COLOR_FRESH_OFF);
    (void)snprintf(line, sizeof(line),
                   "fresh = %s | คิวตอนนี้ %" PRId32 " ms", name,
                   queued_ms);
    (void)bento_lcd_print(line); /* sent without an answer */
}

/* The right button: the loop is slowed by SLOW_MS, or not. */
static void toggle_slow(scope_t *scope)
{
    scope->slow = !scope->slow;
    set_text(scope, scope->slow_button,
             scope->slow ? "ถ่วงลูป 300 ms" : "ลูปปกติ");
    (void)bento_lcd_print(scope->slow ? "ลูปถ่วง" : "ลูปปกติ");
}

/* True when the event is a click on this widget. */
static bool clicked(const bento_ui_event_t *event, bento_ui_id_t widget)
{
    const uint8_t kind = BENTO_UI_EVENT_CLICKED;

    return (event->type == kind) && (event->id == widget);
}

static void handle_events(scope_t *scope, int32_t queued_ms)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    const uint32_t count = poll_events(scope->ui, events);
    uint32_t i;

    for (i = 0U; i < count; i++) {
        if (clicked(&events[i], scope->fresh_button)) {
            toggle_fresh(scope, queued_ms);
        } else if (clicked(&events[i], scope->slow_button)) {
            toggle_slow(scope);
        } else {
            /* any other event is skipped, as the original skips it */
        }
    }
}

/* The round's time and the loudest peak on the state line. */
static void show_round(const scope_t *scope, uint32_t start_ms)
{
    char text[TEXT_BYTES];
    const int32_t took_ms =
        bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);

    (void)snprintf(text, sizeof(text),
                   "รอบนี้ %" PRId32 " ms | ยอดสูงสุด %" PRId32, took_ms,
                   scope->loudest);
    set_text(scope, scope->state, text);
}

/* One round: the three numbers, the queue and the waveform, then the
 * buttons, then the optional slow-down. */
static bento_status_t run_round(scope_t *scope)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    int32_t wave[PLOT_N];
    window_t window = {0, 0, 0, 0};
    bento_status_t status =
        read_window(&scope->microphone, scope->fresh, &window);

    if (status == BENTO_OK) {
        status = read_wave(&scope->microphone, wave);
    }
    if (status == BENTO_OK) {
        plot_wave(scope, wave);
        scope->loudest = (window.peak > scope->loudest)
                             ? window.peak
                             : scope->loudest;
        show_window(scope, &window);
        show_round(scope, start_ms);
        handle_events(scope, window.queued_ms);
        if (scope->slow) {
            status = bento_time_sleep_ms(SLOW_MS);
        }
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* ROUNDS rounds, or until Stop or a failed read. */
static bento_status_t run_scope(scope_t *scope)
{
    bento_status_t status = BENTO_OK;
    int32_t i;

    set_text(scope, scope->state, "พูดใส่บอร์ดได้เลย");
    (void)bento_lcd_print("ไมค์พร้อม กดปุ่มซ้ายสลับ fresh แล้วเทียบแถบคิว");
    scope->fresh = true;
    for (i = 0; (i < ROUNDS) && (status == BENTO_OK); i++) {
        status = run_round(scope);
    }
    return status;
}

/* The console's first lines; lcd calls are sent without an answer
 * (modlcd.c). */
static void open_console(void)
{
    char line[TEXT_BYTES];

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>รูปคลื่นดิบ สามค่า และคิวเสียง</h2>");
    (void)snprintf(line, sizeof(line),
                   "หน้าต่าง %d ตัวอย่างที่ 16 kHz = 16 ms ของเวลาจริง",
                   SAMPLES);
    (void)bento_lcd_print(line);
}

/* After the loop: the microphone is off, and the summary. */
static void report_end(const scope_t *scope)
{
    char line[TEXT_BYTES];

    set_text(scope, scope->state, "ปิดไมค์แล้ว");
    (void)snprintf(line, sizeof(line),
                   "ยอด peak สูงสุดที่เจอ %" PRId32 " จาก 32768",
                   scope->loudest);
    (void)bento_lcd_print(line);
    (void)bento_lcd_print("<span class=muted>ปิด fresh แล้วถ่วงลูป "
                          "คือวิธีเห็นคิว 625 ms เต็ม ๆ</span>");
    (void)printf("stats() ครั้งเดียวได้สามค่าจากหน้าต่างเดียวกัน "
                 "แยกเรียกได้คนละหน้าต่าง\n");
}

int main(void)
{
    scope_t scope;
    bento_status_t status;

    (void)memset(&scope, 0, sizeof(scope));
    (void)memset(g_mic_buffer, 0, sizeof(g_mic_buffer));
    scope.ui = bento_runtime_get_ui();
    status = build_screen(&scope);
    if (status == BENTO_OK) {
        open_console();
        status = open_microphone(&scope.microphone, SENSITIVITY);
    }
    if (status == BENTO_OK) {
        status = run_scope(&scope);
    }
    close_microphone(&scope.microphone);
    if (status == BENTO_OK) {
        report_end(&scope);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
