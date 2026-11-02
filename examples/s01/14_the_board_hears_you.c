/*
 * 14_the_board_hears_you.c -- บอร์ดได้ยินเสียงคุณ (The board hears you)
 *
 * พูดใส่บอร์ด แล้วดูมันขยับตาม เสียงหนึ่งวินาทีมีตัวเลขหมื่นหกพันตัว
 * ไมค์ยุบมันเหลือตัวเดียวที่เอาขึ้นจอได้เลย จอแสดงเลขความดังกับแถบทางซ้าย
 * และมิเตอร์จุด 8x8 ทางขวาที่ไต่ขึ้นจากล่าง ตบมือแล้วตัวนับเดินขึ้น
 * กับดัก: ทุกครั้งที่อ่าน level หรือ stats คือการอ่านเสียงหนึ่งชุดจริง ๆ
 * ถ้าลูปอ่านช้ากว่าที่ไมค์ผลิต คิวจะค้าง ตัวเลข "คิวค้าง" มีไว้ให้เห็นด้วยตา
 *
 * Talk to the board and watch it move. One second of sound is sixteen
 * thousand numbers; the microphone code folds them into one number for
 * the screen. The loudness number and bar on the left and an 8 x 8 dot
 * meter on the right climb together while you talk, and a clap counts
 * up. Trap: every level or stats call really reads one block of sound;
 * read slower than the microphone produces and the queue backs up, so
 * the screen runs late. The "queue" number shows that.
 *
 * Twin of 14_the_board_hears_you.py
 * (embedded-systems-for-aiot-developer/examples/s01/14_the_board_hears_you.py).
 * Style: CLEAN_C.md.
 * Where C differs: the mic module is frozen Python with no C contract,
 * so its start(), level(), stats(), lag() and stop() are written here
 * on machine.PDM_PCM (bento_machine_pdm_*) exactly as freeze/mic.py
 * does them: MONO_RIGHT at 16 kHz, decimation 96, the sens-to-gain
 * table, two blocks thrown away at start, and the octave scale of
 * level(). last_clap = -CLAP_GAP_MS becomes "no clap yet". The
 * try/finally mic.stop() is the one cleanup main() runs.
 *
 * ตาคุณ: ตั้ง TICK_MS = 500 แล้วพูดสั้น ๆ หนึ่งคำ ดูว่าแถบขยับตอนพูดหรือ
 * หลังพูดจบ
 * Your turn: set TICK_MS to 500, say one short word, and watch whether
 * the bar moves while you talk or after you have finished.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_machine.h"
#include "bento_runtime.h"
#include "bento_time.h"

enum {
    SENS = 3,             /* 1 to 5: the higher, the more it hears */
    CLAP_PEAK = 12000,    /* a peak this high counts as a clap */
    QUIET_LEVEL = 20,     /* below: a quiet room */
    LOUD_LEVEL = 45,      /* the bar turns orange */
    VERY_LOUD_LEVEL = 85, /* the bar turns red */
    LAG_WARN_MS = 200,    /* a queue this long shows red */
    TEXT_BYTES = 128
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    RUN_MS = 40000,
    TICK_MS = 80, /* faster than the eye can follow the bar anyway */
    CLAP_GAP_MS = 400, /* an echo is not counted as a second clap */
    WARMUP_MS = 20     /* the pause after each block thrown away */
};

/* The board's microphone, as freeze/mic.py opens it. */
enum {
    MIC_RATE = 16000,
    MIC_DECIMATION = 96,
    MIC_SAMPLES = 256, /* samples per read */
    MIC_BUFFER_BYTES = MIC_SAMPLES * 2,
    WARMUP_BLOCKS = 2, /* the first blocks after start are not steady */
    SENS_MAX = 5
};

/* level(): an ear's scale, octaves of rms from 16 up to full scale.
 * Quiet room 7, normal speech 54, a shout or a clap 92-100. */
enum {
    LEVEL_FLOOR = 16,     /* below: 0 */
    LEVEL_LOW_Q8 = 1024,  /* log2(16) x 256 */
    LEVEL_SPAN_Q8 = 2816, /* log2(32768) x 256, less LEVEL_LOW_Q8 */
    LEVEL_MAX = 100,
    Q8_SHIFT = 8,
    Q8_BITS = 9, /* the mantissa is read in 256-511 */
    Q8_ONE = 256
};

/* The dot meter: 8 x 8 dots, one byte per row, 0x80 the left dot. */
enum {
    METER_SIZE = 8,
    METER_ROW_ON = 0xFF
};

/* Layout on the 792 x 398 canvas. */
enum {
    FONT_PX = 20,
    TITLE_FONT_PX = 24,
    LEFT_X = 20,
    TITLE_Y = 12,
    STATUS_Y = 48,
    METER_X = 480,
    CARD_Y = 80,
    CARD_W = 432,
    CARD_H = 192,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    CAPTION_Y = 84,
    NOW_X = 36,
    MAX_X = 228,
    BIG_DIGITS_Y = 112,
    BIG_DIGITS_W = 172,
    BIG_DIGITS_H = 60,
    BAR_CAPTION_Y = 182,
    BAR_Y = 212,
    BAR_W = 392,
    BAR_H = 28,
    METER_PX = 180,
    METER_HINT_Y = 266,
    ROW_CAPTION_Y = 278,
    ROW_DIGITS_Y = 308,
    ROW_DIGITS_H = 48,
    LAG_X = 20,
    CLAP_X = 192,
    ROW_DIGITS_W = 140,
    PEAK_X = 380,
    PEAK_W = 160
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

/* The microphone's work area: 512 bytes, so it lives at file scope,
 * zeroed by the loader and again at the start of main() (CLEAN_C.md
 * C9, deviation BENTO-MISRA-E03/E04). */
extern uint8_t g_mic_buffer[MIC_BUFFER_BYTES];
uint8_t g_mic_buffer[MIC_BUFFER_BYTES];

typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} text_style_t;

typedef struct {
    int16_t x;
    int16_t w;
    int16_t h;
} digits_place_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t status;
    bento_ui_id_t now_digits;
    bento_ui_id_t max_digits;
    bento_ui_id_t bar;
    bento_ui_id_t dots;
    bento_ui_id_t lag_digits;
    bento_ui_id_t clap_digits;
    bento_ui_id_t peak_digits;
} mic_screen_t;

/* What the run has heard so far. */
typedef struct {
    uint32_t mic;
    int32_t loudest;
    int32_t claps;
    bool has_clapped;
    uint32_t last_clap_ms;
} hearing_t;

/* One block of sound folded into numbers. */
typedef struct {
    int32_t level;
    uint32_t peak;
} block_t;

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* ---- The microphone (freeze/mic.py) ---- */

/* The chip's gain scale runs backwards: 0 is +83 dB, 25 is -67 dB. */
static uint32_t gain_for(int32_t sens)
{
    static const uint32_t gains[SENS_MAX] = {17U, 15U, 13U, 11U, 9U};
    const int32_t index =
        ((sens >= 1) && (sens <= SENS_MAX)) ? sens : SENS;

    return gains[index - 1];
}

/* P8_5 is PDM_CLK and P8_6 PDM_DATA of channel 3, so the format is
 * MONO_RIGHT: MONO_LEFT reads channel 2, which is not wired, and stays
 * silent. The first two blocks are thrown away; their failures are
 * ignored, as start() ignores them. */
static bento_status_t mic_start(uint32_t *out_mic)
{
    bento_machine_pdm_cfg_t config;
    bento_status_t status;
    uint32_t got = 0U;
    uint32_t i;

    bento_machine_pdm_cfg_init(&config);
    config.sample_rate = MIC_RATE;
    config.decimation_rate = MIC_DECIMATION;
    config.bits = BENTO_MACHINE_PDM_BITS_16;
    config.format = BENTO_MACHINE_PDM_MONO_RIGHT;
    config.left_gain = gain_for(SENS);
    config.right_gain = gain_for(SENS);
    status = bento_machine_pdm_open(&config, out_mic);
    if (status == BENTO_OK) {
        status = bento_machine_pdm_start(*out_mic);
    }
    for (i = 0U; (i < (uint32_t)WARMUP_BLOCKS) && (status == BENTO_OK);
         i++) {
        (void)bento_machine_pdm_read(
            *out_mic, g_mic_buffer, MIC_BUFFER_BYTES,
            BENTO_MACHINE_PDM_STATS_TIMEOUT_MS,
            &got); /* start() ignores a failed read */
        status = sleep_checked(WARMUP_MS);
    }
    return status;
}

/* stats(fresh=True): one fresh block, older sound dropped. */
static bento_status_t mic_stats(uint32_t mic,
                                bento_machine_pdm_stats_t *out_stats)
{
    bento_machine_pdm_stats_init(out_stats);
    return bento_machine_pdm_stats(mic, g_mic_buffer, MIC_BUFFER_BYTES,
                                   true, out_stats);
}

/* log2(value) x 256 in integers: the octave plus a linear read of the
 * fraction, within 3 %, enough for a meter. */
static int32_t log2_q8(uint32_t value)
{
    uint32_t bits = 0U;
    uint32_t rest = value;
    uint32_t mantissa;
    uint32_t q8;

    while (rest != 0U) {
        rest >>= 1U;
        bits++;
    }
    mantissa = (bits <= (uint32_t)Q8_BITS)
                   ? (value << ((uint32_t)Q8_BITS - bits))
                   : (value >> (bits - (uint32_t)Q8_BITS));
    q8 = ((bits - 1U) << (uint32_t)Q8_SHIFT) +
         (mantissa - (uint32_t)Q8_ONE);
    return (int32_t)q8;
}

/* level(): loudness 0 to 100 on the ear's scale. */
static bento_status_t mic_level(uint32_t mic, int32_t *out_level)
{
    bento_machine_pdm_stats_t stats;
    bento_status_t status = mic_stats(mic, &stats);
    int32_t level = 0;

    if ((status == BENTO_OK) && (stats.rms >= (uint32_t)LEVEL_FLOOR)) {
        level = ((log2_q8(stats.rms) - LEVEL_LOW_Q8) * LEVEL_MAX) /
                LEVEL_SPAN_Q8;
        level = (level < 0) ? 0 : level;
        level = (level > LEVEL_MAX) ? LEVEL_MAX : level;
    }
    *out_level = level;
    return status;
}

/* ---- The screen ---- */

/* A setter's refusal is ignored, as in MicroPython (modui.c). */
static void set_text(const mic_screen_t *screen, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(screen->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void set_number(const mic_screen_t *screen, bento_ui_id_t widget,
                       int32_t number)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, number);
    set_text(screen, widget, text);
}

static void set_status(const mic_screen_t *screen, uint32_t color,
                       const char *text)
{
    (void)bento_ui_set_color(screen->ui, screen->status, color);
    set_text(screen, screen->status, text);
}

static void poll_once(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* ui.poll() answers no events when it fails; none are read */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The DotMatrix takes packed bytes, one bit per dot, rows top to
 * bottom; row 0 is the top, so the meter climbs from the last row. */
static void show_meter(const mic_screen_t *screen, int32_t level)
{
    uint8_t rows[METER_SIZE];
    int32_t lit = (level * (int32_t)METER_SIZE) / (int32_t)LEVEL_MAX;
    int32_t row;

    lit = (lit > METER_SIZE) ? METER_SIZE : lit;
    for (row = 0; row < METER_SIZE; row++) {
        rows[row] =
            (row >= (METER_SIZE - lit)) ? (uint8_t)METER_ROW_ON : 0U;
    }
    (void)bento_ui_dotmatrix_set(screen->ui, screen->dots, rows,
                                 METER_SIZE);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const text_style_t *style,
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

/* A caption, and under it a Seg7 number that starts at "0". */
static bento_status_t add_reading(bento_ui_t *ui, const char *caption,
                                  const text_style_t *style,
                                  const digits_place_t *place,
                                  int16_t digits_y,
                                  bento_ui_id_t *out_id)
{
    const text_style_t dim = {place->x, style->y, COLOR_DIM, FONT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(ui, caption, &dim, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "0";
        args.text_len = 1U;
        args.x = place->x;
        args.y = digits_y;
        args.w = place->w;
        args.h = place->h;
        args.color = style->color;
        status = bento_ui_seg7_create(ui, &args, out_id);
    }
    return status;
}

static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = LEFT_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border colour */
    args.max_val = CARD_RADIUS; /* and max= its corner radius */
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &card);
}

/* A Bar takes no color= when it is made; the colour is set after. */
static bento_status_t add_bar(mic_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = NOW_X;
    args.y = BAR_Y;
    args.w = BAR_W;
    args.h = BAR_H;
    args.min_val = 0;
    args.max_val = LEVEL_MAX;
    args.font_px = 0;
    status = bento_ui_bar_create(screen->ui, &args, &screen->bar);
    if (status == BENTO_OK) {
        (void)bento_ui_set_color(screen->ui, screen->bar, COLOR_OK);
    }
    return status;
}

/* The number of dots is cols= and rows= (min_val, max_val), not w and
 * h, which only space the dots; the most is 16 x 16. */
static bento_status_t add_dots(mic_screen_t *screen)
{
    const text_style_t hint = {METER_X, METER_HINT_Y, COLOR_DIM,
                               FONT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = METER_X;
    args.y = CARD_Y;
    args.w = METER_PX;
    args.h = METER_PX;
    args.min_val = METER_SIZE;
    args.max_val = METER_SIZE;
    status =
        bento_ui_dotmatrix_create(screen->ui, &args, &screen->dots);
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "จ่อปากใกล้ ๆ แล้วพูด", &hint, &unused);
    }
    return status;
}

/* The card: loudness now, the loudest so far and the bar. */
static bento_status_t build_card(mic_screen_t *screen)
{
    const text_style_t now = {NOW_X, CAPTION_Y, COLOR_OK, FONT_PX};
    const text_style_t loudest = {MAX_X, CAPTION_Y, COLOR_WARN,
                                  FONT_PX};
    const text_style_t bar_caption = {NOW_X, BAR_CAPTION_Y, COLOR_DIM,
                                      FONT_PX};
    const digits_place_t now_place = {NOW_X, BIG_DIGITS_W,
                                      BIG_DIGITS_H};
    const digits_place_t max_place = {MAX_X, BIG_DIGITS_W,
                                      BIG_DIGITS_H};
    bento_ui_id_t unused;
    bento_status_t status = add_card(screen->ui);

    if (status == BENTO_OK) {
        status = add_reading(screen->ui, "ความดังตอนนี้", &now, &now_place,
                             BIG_DIGITS_Y, &screen->now_digits);
    }
    if (status == BENTO_OK) {
        status =
            add_reading(screen->ui, "ดังสุดที่เจอ", &loudest, &max_place,
                        BIG_DIGITS_Y, &screen->max_digits);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "แถบเดียวกัน", &bar_caption, &unused);
    }
    if (status == BENTO_OK) {
        status = add_bar(screen);
    }
    return status;
}

/* The bottom row: the queue, the claps and the last peak. */
static bento_status_t build_row(mic_screen_t *screen)
{
    const text_style_t lag = {LAG_X, ROW_CAPTION_Y, COLOR_INFO,
                              FONT_PX};
    const text_style_t clap = {CLAP_X, ROW_CAPTION_Y, COLOR_OK,
                               FONT_PX};
    const text_style_t peak = {PEAK_X, ROW_CAPTION_Y, COLOR_WARN,
                               FONT_PX};
    const digits_place_t lag_place = {LAG_X, ROW_DIGITS_W,
                                      ROW_DIGITS_H};
    const digits_place_t clap_place = {CLAP_X, ROW_DIGITS_W,
                                       ROW_DIGITS_H};
    const digits_place_t peak_place = {PEAK_X, PEAK_W, ROW_DIGITS_H};
    bento_status_t status =
        add_reading(screen->ui, "คิวค้าง (ms)", &lag, &lag_place,
                    ROW_DIGITS_Y, &screen->lag_digits);

    if (status == BENTO_OK) {
        status =
            add_reading(screen->ui, "ตบมือ (ครั้ง)", &clap, &clap_place,
                        ROW_DIGITS_Y, &screen->clap_digits);
    }
    if (status == BENTO_OK) {
        status = add_reading(screen->ui, "ยอด peak", &peak, &peak_place,
                             ROW_DIGITS_Y, &screen->peak_digits);
    }
    return status;
}

static bento_status_t build_screen(mic_screen_t *screen)
{
    const text_style_t title = {LEFT_X, TITLE_Y, COLOR_TEXT,
                                TITLE_FONT_PX};
    const text_style_t status_style = {LEFT_X, STATUS_Y, COLOR_WARN,
                                       FONT_PX};
    const text_style_t meter = {METER_X, STATUS_Y, COLOR_DIM, FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "บอร์ดได้ยินเสียงคุณ", &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังเปิดไมค์", &status_style,
                           &screen->status);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "มิเตอร์จุด 8x8", &meter, &unused);
    }
    if (status == BENTO_OK) {
        status = build_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_dots(screen);
    }
    if (status == BENTO_OK) {
        status = build_row(screen);
    }
    return status;
}

/* ---- The run ---- */

/* Two reads: level() folds one block into 0-100; stats() reads
 * another and gives rms, peak and dc at once (0 to 32768, the full
 * 16-bit scale). The peak catches short sounds. */
static bento_status_t read_block(hearing_t *hearing, block_t *out_block)
{
    bento_machine_pdm_stats_t stats;
    bento_status_t status = mic_level(hearing->mic, &out_block->level);

    if (status == BENTO_OK) {
        status = mic_stats(hearing->mic, &stats);
        out_block->peak = stats.peak;
    }
    return status;
}

static void show_block(const mic_screen_t *screen, hearing_t *hearing,
                       const block_t *block)
{
    uint32_t bar_color = COLOR_OK;

    if (block->level > hearing->loudest) {
        hearing->loudest = block->level;
        set_number(screen, screen->max_digits, hearing->loudest);
    }
    set_number(screen, screen->now_digits, block->level);
    set_number(screen, screen->peak_digits, (int32_t)block->peak);
    (void)bento_ui_set_value(screen->ui, screen->bar, block->level);
    if (block->level >= VERY_LOUD_LEVEL) {
        bar_color = COLOR_BAD;
    } else if (block->level >= LOUD_LEVEL) {
        bar_color = COLOR_WARN;
    } else {
        bar_color = COLOR_OK;
    }
    (void)bento_ui_set_color(screen->ui, screen->bar, bar_color);
    show_meter(screen, block->level);
}

/* lag() reads no sound: it asks how many ms are waiting. A number that
 * keeps climbing means the loop reads slower than the microphone
 * produces, and the screen runs that late for ever. */
static bento_status_t show_lag(const mic_screen_t *screen,
                               const hearing_t *hearing)
{
    uint32_t lag_ms = 0U;
    bento_status_t status =
        bento_machine_pdm_pending_ms(hearing->mic, &lag_ms);

    if (status == BENTO_OK) {
        set_number(screen, screen->lag_digits, (int32_t)lag_ms);
        (void)bento_ui_set_color(
            screen->ui, screen->lag_digits,
            (lag_ms > (uint32_t)LAG_WARN_MS) ? COLOR_BAD : COLOR_INFO);
    }
    return status;
}

/* A clap is counted from the peak, not the average: a clap is short
 * and hard, and the block's average would all but hide it. */
static void show_verdict(const mic_screen_t *screen, hearing_t *hearing,
                         const block_t *block)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    char text[TEXT_BYTES];

    if ((block->peak >= (uint32_t)CLAP_PEAK) &&
        (!hearing->has_clapped ||
         (bento_time_ticks_diff(now_ms, hearing->last_clap_ms) >=
          CLAP_GAP_MS))) {
        hearing->has_clapped = true;
        hearing->last_clap_ms = now_ms;
        hearing->claps++;
        set_number(screen, screen->clap_digits, hearing->claps);
        (void)snprintf(text, sizeof(text), "ได้ยินเสียงตบมือ ครั้งที่ %" PRId32,
                       hearing->claps);
        set_status(screen, COLOR_BAD, text);
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>ตบมือครั้งที่ %" PRId32
                       " - peak %" PRIu32 " </span>",
                       hearing->claps, block->peak);
        (void)bento_lcd_print(text);
    } else if (block->level < QUIET_LEVEL) {
        (void)snprintf(text, sizeof(text), "ห้องเงียบ - ความดัง %" PRId32,
                       block->level);
        set_status(screen, COLOR_DIM, text);
    } else {
        (void)snprintf(text, sizeof(text), "มีเสียง - ความดัง %" PRId32,
                       block->level);
        set_status(screen, COLOR_OK, text);
    }
}

static bento_status_t listen_once(const mic_screen_t *screen,
                                  hearing_t *hearing,
                                  uint32_t work_start_ms)
{
    block_t block = {0, 0U};
    bento_status_t status = read_block(hearing, &block);

    if (status == BENTO_OK) {
        show_block(screen, hearing, &block);
        status = show_lag(screen, hearing);
    }
    if (status == BENTO_OK) {
        const int32_t left_ms =
            TICK_MS -
            bento_time_ticks_diff(bento_time_ticks_ms(), work_start_ms);

        show_verdict(screen, hearing, &block);
        poll_once(screen->ui);
        status =
            (left_ms > 0) ? sleep_checked((uint32_t)left_ms) : BENTO_OK;
    }
    return status;
}

static bento_status_t listen(const mic_screen_t *screen,
                             hearing_t *hearing)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    uint32_t work_start_ms = start_ms;
    bento_status_t status = BENTO_OK;

    set_status(screen, COLOR_OK, "พูดหรือตบมือใส่บอร์ดได้เลย");
    poll_once(screen->ui);
    (void)bento_lcd_print("<span class=ok>ไมค์พร้อมแล้ว</span>");
    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(work_start_ms, start_ms) < RUN_MS)) {
        status = listen_once(screen, hearing, work_start_ms);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
        work_start_ms = bento_time_ticks_ms();
    }
    return status;
}

/* The last values stay on the screen to be read. */
static void show_end(const mic_screen_t *screen,
                     const hearing_t *hearing)
{
    char text[TEXT_BYTES];

    show_meter(screen, 0);
    (void)bento_ui_set_value(screen->ui, screen->bar, 0);
    (void)snprintf(text, sizeof(text),
                   "ปิดไมค์แล้ว - ตบมือ %" PRId32 " ครั้ง", hearing->claps);
    set_status(screen, COLOR_DIM, text);
    poll_once(screen->ui);
    (void)bento_lcd_console(
        "<span class=muted>------------------------</span>");
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ดังสุด %" PRId32
                   " จาก 100 | ตบมือ %" PRId32 " ครั้ง</span>",
                   hearing->loudest, hearing->claps);
    (void)bento_lcd_print(text);
    (void)printf("ระดับสูงสุด %" PRId32 " | ตบมือ %" PRId32 " ครั้ง\n",
                 hearing->loudest, hearing->claps);
}

int main(void)
{
    char text[TEXT_BYTES];
    mic_screen_t screen;
    hearing_t hearing;
    bento_status_t status;

    (void)memset(g_mic_buffer, 0, sizeof(g_mic_buffer));
    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(&hearing, 0, sizeof(hearing));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        poll_once(screen.ui);
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>บอร์ดได้ยินเสียงคุณ</h2>");
        (void)snprintf(text, sizeof(text), "เปิดไมค์ที่ความไว %d จาก 5",
                       SENS);
        (void)bento_lcd_print(text);
        status = mic_start(&hearing.mic);
        if (status == BENTO_OK) {
            status = listen(&screen, &hearing);
            /* the one cleanup, run whatever ended the loop; stop()
             * ignores a failed deinit, so this does too */
            (void)bento_machine_pdm_close(hearing.mic);
        }
    }
    if (status == BENTO_OK) {
        show_end(&screen, &hearing);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
