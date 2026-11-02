/*
 * 02_mic_sound_level_meter.c -- เครื่องวัดระดับเสียงในห้อง (คาบ 08)
 *
 * เปิดไมค์ที่ความไว 3 แล้วอ่านระดับเสียง 0-100 ทุก 60 ms 900 รอบ
 * ระดับเดียวกันขึ้นทั้งกราฟตามเวลา ตัวเลข 7 ส่วน และแถบ พร้อมค่าดังที่สุดที่เจอ
 * ป้ายสถานะเปลี่ยนเป็น "มีเสียง" เมื่อระดับถึง 35 และกลับเป็น "ห้องเงียบ"
 * เมื่อต่ำกว่านั้น คอนโซลรายงานเฉพาะตอนสถานะเปลี่ยน จบแล้วปิดไมค์เสมอ
 * พูดใส่บอร์ดแล้วทั้งสามอย่างต้องขยับพร้อมกัน บอร์ด: Eva Kit
 *
 * Opens the microphone at sensitivity 3 and reads the sound level,
 * 0-100, every 60 ms for 900 rounds. The same level drives a chart over
 * time, the seven-segment digits and a bar, beside the loudest level so
 * far. The status label says "sound" once the level reaches 35 and
 * "quiet room" below it; the console reports only the changes. The
 * microphone is always closed at the end. Talk at the board: all three
 * must move together. Board: Eva Kit.
 *
 * Twin of 02_mic_sound_level_meter.py
 * (embedded-systems-for-aiot-developer/examples/s08/02_mic_sound_level_meter.py).
 * Style: CLEAN_C.md.
 * The C API has no mic module, so the few calls of the board's frozen
 * mic.py (ports/psoc-edge/freeze/mic.py) are reproduced here on
 * bento_machine_pdm: start(sens) opens and starts PDM0 (16 kHz,
 * decimation 96, MONO_RIGHT, the sens-to-gain table) and throws away
 * two reads 20 ms apart; level() is rms() of a fresh window on the
 * module's integer octave scale. bento_machine_pdm is a PSoC Edge API:
 * the reTerminal has no PDM microphone (its port answers unsupported).
 * Python's try/finally around the loop becomes close_microphone(),
 * which main() calls once.
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
    SENSITIVITY = 3, /* 1 to 5, higher hears quieter sounds */
    QUIET = 35,      /* below this the room counts as quiet */
    PERIOD_MS = 60,  /* the eye cannot follow the bar faster */
    ROUNDS = 900,
    TEXT_BYTES = 128 /* Thai text and two int32_t numbers */
};

/* mic.py's numbers (ports/psoc-edge/freeze/mic.py). */
enum {
    MIC_RATE = 16000,
    MIC_DECIMATION = 96,
    MIC_BITS = 16,
    MIC_SAMPLES = 256, /* start()'s default samples= */
    SAMPLE_BYTES = 2,
    MIC_BUFFER_BYTES = MIC_SAMPLES * SAMPLE_BYTES,
    WARM_UP_READS = 2,
    WARM_UP_MS = 20,
    READ_LIMIT_MS = 2000, /* the binding's read time-out */
    SENSITIVITY_COUNT = 5,
    LEVEL_FLOOR = 16,     /* an rms below this is level 0 */
    LEVEL_LOW_Q8 = 1024,  /* log2(16) times 256 */
    LEVEL_SPAN_Q8 = 2816, /* log2(32768) times 256, less LEVEL_LOW_Q8 */
    LEVEL_MAX = 100,
    Q8_TOP_BITS = 9, /* keep 9 bits: a mantissa of 256 to 511 */
    Q8_ONE = 256
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 12,
    TITLE_Y = 8,
    CHART_X = 12,
    CHART_Y = 40,
    CHART_W = 472,
    CHART_H = 200,
    RIGHT_X = 496,
    LEGEND_Y = 42,
    NOW_Y = 74,
    DIGITS_Y = 100,
    DIGITS_W = 180,
    DIGITS_H = 44,
    BAR_NOTE_Y = 150,
    BAR_Y = 176,
    BAR_W = 180,
    BAR_H = 16,
    LOUDEST_NOTE_Y = 202,
    LOUDEST_Y = 228,
    PANEL_X = 12,
    PANEL_Y = 256,
    PANEL_W = 472,
    PANEL_H = 76,
    STATE_X = 24,
    STATE_Y = 268,
    SUB_Y = 304
};

/* Font sizes, in pixels, and colours, 0xRRGGBB. */
enum {
    FONT_TITLE = 24,
    FONT_TEXT = 20,
    FONT_BIG = 28,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_MUTED = 0x9AA0A6,
    COLOR_LOUD = 0x55DD55
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

/* The widgets the program changes, and what the loop remembers. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    bento_ui_id_t digits;
    bento_ui_id_t bar;
    bento_ui_id_t loudest_label;
    bento_ui_id_t state;
    bento_ui_id_t sub;
    uint32_t microphone;
    bool microphone_open;
    int32_t loudest;
    bool loud;
} meter_t;

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

/* mic.start(sens): PDM0 configured, capturing and warmed up. */
static bento_status_t open_microphone(meter_t *meter,
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
    status = bento_machine_pdm_open(&config, &meter->microphone);
    if (status == BENTO_OK) {
        meter->microphone_open = true;
        status = bento_machine_pdm_start(meter->microphone);
    }
    if (status == BENTO_OK) {
        status = warm_up(meter->microphone);
    }
    return status;
}

/* mic.stop(): the cleanup, whatever ended the program. */
static void close_microphone(meter_t *meter)
{
    if (meter->microphone_open) {
        (void)bento_machine_pdm_close(
            meter->microphone); /* stop() swallows every error */
        meter->microphone_open = false;
    }
}

/* log2(value) times 256 in integers: the octave from the bit count,
 * its fraction read linearly from the top 9 bits. */
static int32_t log2_q8(uint32_t value)
{
    uint32_t rest = value;
    int32_t bits = 0;
    uint32_t mantissa;

    while (rest != 0U) {
        rest >>= 1U;
        bits++;
    }
    if (bits <= Q8_TOP_BITS) {
        mantissa = value << (uint32_t)(Q8_TOP_BITS - bits);
    } else {
        mantissa = value >> (uint32_t)(bits - Q8_TOP_BITS);
    }
    return ((bits - 1) * Q8_ONE) + ((int32_t)mantissa - Q8_ONE);
}

/* mic.level(): the rms of a fresh window on the ear's scale, 0 to 100,
 * one point per equal step in octaves from rms 16 to full scale. */
static bento_status_t read_level(const meter_t *meter, int32_t *level)
{
    bento_machine_pdm_stats_t stats;
    bento_status_t status;

    bento_machine_pdm_stats_init(&stats);
    status = bento_machine_pdm_stats(meter->microphone, g_mic_buffer,
                                     MIC_BUFFER_BYTES, true, &stats);
    *level = 0;
    if ((status == BENTO_OK) && (stats.rms >= (uint32_t)LEVEL_FLOOR)) {
        const int32_t above = log2_q8(stats.rms) - LEVEL_LOW_Q8;
        const int32_t scaled = (above * LEVEL_MAX) / LEVEL_SPAN_Q8;

        *level = (scaled > LEVEL_MAX) ? LEVEL_MAX : scaled;
    }
    return status;
}

static void set_text(const meter_t *meter, bento_ui_id_t id,
                     const char *text)
{
    /* a setter's refusal was ignored before too (modui.c) */
    (void)bento_ui_set_text(meter->ui, id, text,
                            (uint32_t)strlen(text));
}

/* ui.poll() is called every round, as the original does; the events are
 * not used, and it answers no events when it fails. */
static void skip_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const rect_t *place, int32_t font_px,
                                uint32_t color, bento_ui_id_t *out_id)
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

/* The arguments of a widget that is only a box on a 0-100 scale: the
 * chart, the digits, the bar and the panel. */
static void box_args(bento_ui_widget_args_t *args, const rect_t *box)
{
    bento_ui_widget_args_init(args);
    args->x = box->x;
    args->y = box->y;
    args->w = box->w;
    args->h = box->h;
    args->min_val = 0;
    args->max_val = LEVEL_MAX;
}

/* The title, the chart and the right column down to the digits. */
static bento_status_t add_top(meter_t *meter)
{
    static const rect_t title = {TITLE_X, TITLE_Y, 0, 0};
    static const rect_t chart = {CHART_X, CHART_Y, CHART_W, CHART_H};
    static const rect_t legend = {RIGHT_X, LEGEND_Y, 0, 0};
    static const rect_t now = {RIGHT_X, NOW_Y, 0, 0};
    static const rect_t digits = {RIGHT_X, DIGITS_Y, DIGITS_W,
                                  DIGITS_H};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(meter->ui, "เครื่องวัดระดับเสียง",
                                      &title, FONT_TITLE, 0U, &unused);

    if (status == BENTO_OK) {
        box_args(&args, &chart);
        status = bento_ui_chart_create(meter->ui, &args, &meter->chart);
    }
    if (status == BENTO_OK) {
        status = add_label(meter->ui, "ฟ้า = ความดัง 0-100", &legend,
                           FONT_TEXT, COLOR_ACCENT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(meter->ui, "ความดังตอนนี้", &now, FONT_TEXT, 0U,
                           &unused);
    }
    if (status == BENTO_OK) {
        box_args(&args, &digits);
        args.max_val = 0;
        status = bento_ui_seg7_create(meter->ui, &args, &meter->digits);
    }
    return status;
}

/* The bar, the loudest level and the status panel. */
static bento_status_t add_bottom(meter_t *meter)
{
    static const rect_t bar_note = {RIGHT_X, BAR_NOTE_Y, 0, 0};
    static const rect_t bar = {RIGHT_X, BAR_Y, BAR_W, BAR_H};
    static const rect_t loudest_note = {RIGHT_X, LOUDEST_NOTE_Y, 0, 0};
    static const rect_t loudest = {RIGHT_X, LOUDEST_Y, 0, 0};
    static const rect_t panel = {PANEL_X, PANEL_Y, PANEL_W, PANEL_H};
    static const rect_t state = {STATE_X, STATE_Y, 0, 0};
    static const rect_t sub = {STATE_X, SUB_Y, 0, 0};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(meter->ui, "แถบ = ตัวเลขเดียวกัน", &bar_note, FONT_TEXT,
                  0U, &unused);

    if (status == BENTO_OK) {
        box_args(&args, &bar);
        status = bento_ui_bar_create(meter->ui, &args, &meter->bar);
    }
    if (status == BENTO_OK) {
        status = add_label(meter->ui, "ดังที่สุดที่เจอ", &loudest_note,
                           FONT_TEXT, 0U, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(meter->ui, "0", &loudest, FONT_BIG,
                           COLOR_WARN, &meter->loudest_label);
    }
    if (status == BENTO_OK) {
        box_args(&args, &panel);
        args.max_val = 0;
        status = bento_ui_panel_create(meter->ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(meter->ui, "กำลังเปิดไมค์", &state, FONT_BIG,
                           0U, &meter->state);
    }
    if (status == BENTO_OK) {
        status = add_label(meter->ui, "ยังไม่ได้อ่านค่าแรก", &sub, FONT_TEXT,
                           0U, &meter->sub);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(meter_t *meter)
{
    bento_status_t status =
        bento_ui_screen(meter->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_top(meter);
    }
    if (status == BENTO_OK) {
        status = add_bottom(meter);
    }
    if (status == BENTO_OK) {
        skip_events(meter->ui);
    }
    return status;
}

/* The status label and the console follow only a change between quiet
 * and loud, so the console is not flooded. */
static void show_state(meter_t *meter, int32_t level)
{
    const bool now_loud = level >= QUIET;

    if (now_loud != meter->loud) {
        char line[TEXT_BYTES];

        meter->loud = now_loud;
        if (now_loud) {
            set_text(meter, meter->state, "มีเสียง");
            (void)bento_ui_set_color(meter->ui, meter->state,
                                     COLOR_LOUD);
            (void)snprintf(line, sizeof(line),
                           "<span class=ok>มีเสียง - ระดับ %" PRId32
                           "</span>",
                           level);
            (void)bento_lcd_print(line); /* sent without an answer */
        } else {
            set_text(meter, meter->state, "ห้องเงียบ");
            (void)bento_ui_set_color(meter->ui, meter->state,
                                     COLOR_MUTED);
            (void)bento_lcd_print(
                "<span class=muted>กลับมาเงียบแล้ว</span>");
        }
    }
}

/* One level onto the chart, the digits, the bar and the two lines. */
static void show_level(meter_t *meter, int32_t level)
{
    char text[TEXT_BYTES];

    if (level > meter->loudest) {
        meter->loudest = level;
        (void)snprintf(text, sizeof(text), "%" PRId32, level);
        set_text(meter, meter->loudest_label, text);
    }
    (void)bento_ui_chart_set_next(meter->ui, meter->chart, 0U, level);
    (void)snprintf(text, sizeof(text), "%" PRId32, level);
    set_text(meter, meter->digits, text);
    (void)bento_ui_set_value(meter->ui, meter->bar, level);
    (void)snprintf(text, sizeof(text),
                   "ระดับ %" PRId32 " จาก 100 | ดังสุด %" PRId32, level,
                   meter->loudest);
    set_text(meter, meter->sub, text);
    show_state(meter, level);
}

/* ROUNDS rounds of PERIOD_MS, or until Stop or a failed read. */
static bento_status_t run_meter(meter_t *meter)
{
    bento_status_t status = BENTO_OK;
    int32_t round;

    set_text(meter, meter->state, "พูดใส่บอร์ดได้เลย");
    (void)bento_ui_set_color(meter->ui, meter->state, COLOR_MUTED);
    (void)bento_lcd_print("ไมค์พร้อมแล้ว พูดหรือตบมือใส่บอร์ดดู");
    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        int32_t level = 0;

        status = read_level(meter, &level);
        if (status == BENTO_OK) {
            show_level(meter, level);
            skip_events(meter->ui);
            status = bento_time_sleep_ms(PERIOD_MS);
        }
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

/* The console's first lines; lcd calls are sent without an answer
 * (modlcd.c). */
static void open_console(void)
{
    char line[TEXT_BYTES];

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>เครื่องวัดระดับเสียง</h2>");
    (void)snprintf(line, sizeof(line), "เปิดไมค์ที่ความไว %d จาก 5",
                   SENSITIVITY);
    (void)bento_lcd_print(line);
}

/* After the loop: the microphone is off, and the summary. */
static void report_end(const meter_t *meter)
{
    char line[TEXT_BYTES];

    set_text(meter, meter->state, "ปิดไมค์แล้ว");
    (void)bento_ui_set_color(meter->ui, meter->state, COLOR_MUTED);
    (void)snprintf(line, sizeof(line),
                   "ระดับสูงสุดที่เจอ %" PRId32 " จาก 100", meter->loudest);
    (void)bento_lcd_print(line);
    (void)bento_lcd_print(
        "<span class=muted>ลองตั้ง SENS = 5 แล้ววัดห้องเดิมซ้ำ</span>");
    (void)printf("ตัวเลข 0-100 มาจาก rms ที่ยกกำลังสองก่อนเฉลี่ย "
                 "ไม่ใช่ค่าเฉลี่ยดิบ\n");
}

int main(void)
{
    meter_t meter;
    bento_status_t status;

    (void)memset(&meter, 0, sizeof(meter));
    (void)memset(g_mic_buffer, 0, sizeof(g_mic_buffer));
    meter.ui = bento_runtime_get_ui();
    status = build_screen(&meter);
    if (status == BENTO_OK) {
        open_console();
        status = open_microphone(&meter, SENSITIVITY);
    }
    if (status == BENTO_OK) {
        status = run_meter(&meter);
    }
    close_microphone(&meter);
    if (status == BENTO_OK) {
        report_end(&meter);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
