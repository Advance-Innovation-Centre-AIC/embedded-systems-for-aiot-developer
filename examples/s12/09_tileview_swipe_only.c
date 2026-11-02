/*
 * 09_tileview_swipe_only.c -- จอที่นิ้วพาไปได้ แต่โปรแกรมพาไปไม่ได้
 *
 * แถบบนสุดอยู่นอก Tileview บอกว่าโปรแกรม "ขอ" ช่องไหน ส่วน Tileview ใต้ลงมา
 * ยังโชว์ช่องที่ 1 อยู่ตลอด ทุกวินาทีโปรแกรมสั่งย้ายช่องด้วย .value() และ
 * PROP_ACTIVE_TAB แต่จอไม่ขยับ และ Tileview ไม่ส่ง event เลย บรรทัดล่างนับให้
 * อุณหภูมิมาจาก SHT40 ถ้าบอร์ดมี ไม่งั้นใช้ลูกบิด 0-100 % แทน 15-45 C
 *
 * The strip at the top sits outside the Tileview and says which tile the
 * program asks for; the Tileview below keeps showing tile 1. Every second
 * the program asks to move with .value() and PROP_ACTIVE_TAB, the tiles do
 * not move, and the Tileview sends no event; the bottom line counts both.
 * The temperature comes from the SHT40 when the board has one, else the
 * knob stands in (0-100 % = 15-45 C).
 *
 * Twin of 09_tileview_swipe_only.py
 * (embedded-systems-for-aiot-developer/examples/s12/09_tileview_swipe_only.py),
 * from the library twin aiot-s12-09-tileview-swipe-only, changed to this
 * file's loop: one round per 200 ms sleep, and a round without a
 * temperature throws one poll away and sleeps too. Style: CLEAN_C.md.
 * What C changes: hasattr(sensors, "sht40") plus try/except OSError is one
 * status test of bento_sensors_sht40_temperature(); "{:.1f}" is
 * bento_fmt_real_precision(); the global _TEMP_SRC lives in the tally
 * struct; a failing sensors.snapshot() ends the program with its status.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_time.h"

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add, or the knob temperature drifts. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

enum { /* timing and counts */
    SETTLE_MS = 200,
    ROUNDS = 90,
    PERIOD_MS = 200,
    SHOW_EVERY = 5, /* one change of the numbers a second (S7.7.4) */
    TILE_COUNT = 3,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};
enum { /* layout on the 792 x 398 canvas; inside a tile, from its corner */
    MARGIN_X = 24,
    TITLE_Y = 8,
    TITLE_PX = 28,
    STRIP_Y = 56,
    STRIP_W = 744,
    STRIP_H = 88,
    STRIP_RADIUS = 8,
    STRIP_TEXT_X = 48,
    STRIP_NOTE_Y = 72,
    STRIP_VALUE_Y = 104,
    TEMP_X = 560,
    TILES_Y = 160,
    TILES_H = 160,
    TILE_CAPTION_Y = 16,
    TILE_VALUE_Y = 64,
    LINE_Y = 336,
    SMALL_PX = 20,
    TEXT_PX = 24
};
enum { /* colours, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623
};

/* Where the temperature came from last; a new source is announced once. */
typedef enum {
    SOURCE_NONE = 0,
    SOURCE_SHT40,
    SOURCE_KNOB
} temp_source_t;

/* Where a label goes: x and y inside its parent (0 = the screen). */
typedef struct {
    int16_t x;
    int16_t y;
    bento_ui_id_t parent;
} spot_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ask;
    bento_ui_id_t temp;
    bento_ui_id_t tiles;
    bento_ui_id_t tile[TILE_COUNT];
    bento_ui_id_t now;
    bento_ui_id_t highest;
    bento_ui_id_t line;
} tile_screen_t;

/* What the loop counts and remembers. */
typedef struct {
    int32_t asked;
    int32_t events;
    int32_t want;
    bool has_hottest;
    bento_real_t hottest;
    temp_source_t source;
} tally_t;

static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The SHT40 when the board has one, else the knob (0-100 % = 15-45 C, so
 * a class can turn it across a threshold); false when neither answered. */
static bool read_temp(const bento_sensors_snapshot_t *snap,
                      temp_source_t *source, bento_real_t *temp_c)
{
    const bento_real_t knob_base_c = (bento_real_t)15.0;
    const bento_real_t c_per_percent = (bento_real_t)0.3;
    float sht40_c = 0.0F;
    bool found = true;

    if (bento_sensors_sht40_temperature(&sht40_c) == BENTO_OK) {
        if (*source != SOURCE_SHT40) {
            *source = SOURCE_SHT40;
            (void)bento_lcd_print("อุณหภูมิจาก SHT40 (เซนเซอร์จริงบนบอร์ด)");
        }
        *temp_c = (bento_real_t)sht40_c;
    } else if (snap->has_pot != 0U) {
        const bento_real_t scaled =
            (bento_real_t)snap->pot_percent * c_per_percent;

        if (*source != SOURCE_KNOB) {
            *source = SOURCE_KNOB;
            (void)bento_lcd_print(
                "<span class=warn>ไม่มีเซนเซอร์อุณหภูมิ</span>");
            (void)bento_lcd_print("ใช้ลูกบิดแทน: 0-100 % = 15-45 C");
        }
        *temp_c = knob_base_c + scaled;
    } else {
        found = false;
    }
    return found;
}

/* Writes "<prefix><value> องศา" with the value as "{:.1f}" prints it. */
static void format_degrees(char *text, size_t cap, const char *prefix,
                           bento_real_t value)
{
    char digits[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;

    if (bento_fmt_real_precision(digits, sizeof(digits), value, 1,
                                 &length) != BENTO_OK) {
        digits[0] = '\0'; /* the buffer is always large enough */
    }
    (void)snprintf(text, cap, "%s%s องศา", prefix, digits);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                spot_t spot, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = spot.x;
    args.y = spot.y;
    args.color = color;
    args.font_px = font_px;
    args.parent = spot.parent;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A label the program never changes again. */
static bento_status_t add_fixed_label(bento_ui_t *ui, const char *text,
                                      spot_t spot, uint32_t color,
                                      int32_t font_px)
{
    bento_ui_id_t unused;

    return add_label(ui, text, spot, color, font_px, &unused);
}

/* The strip outside the Tileview: it stays on screen whatever tile a
 * person swiped to and left there. */
static bento_status_t add_strip(tile_screen_t *screen)
{
    const spot_t note = {STRIP_TEXT_X, STRIP_NOTE_Y, 0U};
    const spot_t ask = {STRIP_TEXT_X, STRIP_VALUE_Y, 0U};
    const spot_t temp = {TEMP_X, STRIP_VALUE_Y, 0U};
    bento_ui_widget_args_t args;
    bento_ui_id_t card;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = STRIP_Y;
    args.w = STRIP_W;
    args.h = STRIP_H;
    args.color = COLOR_CARD;
    args.max_val = STRIP_RADIUS;
    status = bento_ui_panel_create(screen->ui, &args, &card);
    if (status == BENTO_OK) {
        status = add_fixed_label(screen->ui, "แถบนี้อยู่นอก Tileview", note,
                                 COLOR_DIM, SMALL_PX);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่ได้ขอช่องไหน", ask,
                           COLOR_WARN, TEXT_PX, &screen->ask);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "-- องศา", temp, COLOR_TEXT,
                           TEXT_PX, &screen->temp);
    }
    return status;
}

/* Three tiles for a person to walk through, not for the program to go to. */
static bento_status_t add_tiles(tile_screen_t *screen)
{
    static const uint8_t directions[TILE_COUNT] = {
        BENTO_UI_DIR_RIGHT, BENTO_UI_DIR_LEFT | BENTO_UI_DIR_RIGHT,
        BENTO_UI_DIR_LEFT};
    bento_ui_widget_args_t args;
    uint8_t column;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = TILES_Y;
    args.w = STRIP_W;
    args.h = TILES_H;
    args.color = COLOR_CARD;
    status =
        bento_ui_tileview_create(screen->ui, &args, &screen->tiles);
    for (column = 0U;
         (column < (uint8_t)TILE_COUNT) && (status == BENTO_OK);
         column++) {
        status = bento_ui_add_tile(screen->ui, screen->tiles, column,
                                   0U, directions[column],
                                   &screen->tile[column]);
    }
    return status;
}

/* One tile: its blue caption, and a value line under it. */
static bento_status_t fill_tile(bento_ui_t *ui, bento_ui_id_t tile,
                                const char *caption, const char *value,
                                bento_ui_id_t *value_id)
{
    const spot_t top = {MARGIN_X, TILE_CAPTION_Y, tile};
    const spot_t below = {MARGIN_X, TILE_VALUE_Y, tile};
    bento_status_t status =
        add_fixed_label(ui, caption, top, COLOR_ACCENT, TEXT_PX);

    if (status == BENTO_OK) {
        status =
            add_label(ui, value, below, COLOR_TEXT, TEXT_PX, value_id);
    }
    return status;
}

static bento_status_t fill_tiles(tile_screen_t *screen)
{
    bento_ui_id_t unused;
    bento_status_t status =
        fill_tile(screen->ui, screen->tile[0], "ช่อง 1 - ค่าปัจจุบัน",
                  "รออ่านค่าแรก", &screen->now);

    if (status == BENTO_OK) {
        status =
            fill_tile(screen->ui, screen->tile[1], "ช่อง 2 - ค่าสูงสุดที่เจอ",
                      "ยังไม่มีข้อมูล", &screen->highest);
    }
    if (status == BENTO_OK) {
        status =
            fill_tile(screen->ui, screen->tile[2], "ช่อง 3 - เกณฑ์ที่ตั้งไว้",
                      "รอบส่ง 5 วินาที", &unused);
    }
    return status;
}

/* Draws the whole screen in the original's order. */
static bento_status_t build_screen(tile_screen_t *screen)
{
    const spot_t title = {MARGIN_X, TITLE_Y, 0U};
    const spot_t line = {MARGIN_X, LINE_Y, 0U};
    bento_status_t status;

    /* MicroPython ignores a refused ui.screen(): the runtime already set it up */
    (void)bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = wait_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_fixed_label(screen->ui, "ปัดได้ แต่สั่งไม่ได้", title,
                                 COLOR_TEXT, TITLE_PX);
    }
    if (status == BENTO_OK) {
        status = add_strip(screen);
    }
    if (status == BENTO_OK) {
        status = add_tiles(screen);
    }
    if (status == BENTO_OK) {
        status = fill_tiles(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่ได้สั่งย้ายช่อง", line,
                           COLOR_DIM, SMALL_PX, &screen->line);
    }
    return status;
}

static void set_text(const tile_screen_t *screen, bento_ui_id_t label,
                     const char *text)
{
    /* MicroPython's widget setters ignore a refusal (modui.c) */
    (void)bento_ui_set_text(screen->ui, label, text,
                            (uint32_t)strlen(text));
}

/* The console's opening lines, with the answer to "which tile?". */
static void print_intro(const tile_screen_t *screen)
{
    char text[TEXT_BYTES];
    int32_t answer = 0;

    /* .value() answers 0 when the display core does not (modui.c) */
    if (bento_ui_get_value(screen->ui, screen->tiles, &answer) !=
        BENTO_OK) {
        answer = 0;
    }
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>Tileview - ทางเดียว นิ้วเท่านั้น</h2>");
    (void)bento_lcd_print("สามช่อง กิน 4 แฮนเดิล ตั้งแต่ยังไม่มีของอยู่ในนั้น");
    (void)snprintf(text, sizeof(text),
                   "ถามว่าอยู่ช่องไหน ได้ %" PRId32 " - ไม่ใช่หมายเลขช่อง",
                   answer);
    (void)bento_lcd_print(text);
}

/* Once a second: the numbers, and one more request to move. */
static void show_and_ask(const tile_screen_t *screen, tally_t *tally,
                         bento_real_t temp_c)
{
    char text[TEXT_BYTES];

    format_degrees(text, sizeof(text), "", temp_c);
    set_text(screen, screen->temp, text);
    set_text(screen, screen->now, text);
    format_degrees(text, sizeof(text), "สูงสุด ", tally->hottest);
    set_text(screen, screen->highest, text);

    /* like a kiosk that has to turn its own pages */
    tally->want = (tally->want + 1) % TILE_COUNT;
    (void)snprintf(text, sizeof(text), "โปรแกรมขอช่องที่ %" PRId32,
                   tally->want + 1);
    set_text(screen, screen->ask, text);

    /* the two calls everyone tries first; neither moves the tiles */
    (void)bento_ui_set_value(screen->ui, screen->tiles, tally->want);
    (void)bento_ui_set_prop(screen->ui, screen->tiles,
                            BENTO_UI_PROP_ACTIVE_TAB, tally->want);
    tally->asked++;
    (void)snprintf(text, sizeof(text),
                   "สั่งย้ายช่องไปแล้ว %" PRId32 " ครั้ง จอไม่ขยับ",
                   tally->asked);
    set_text(screen, screen->line, text);
}

/* Counts every event of the Tileview, of any kind: the original reads only
 * ev["handle"]. No callback is bound to a Tileview, so it stays zero. */
static void count_events(const tile_screen_t *screen, tally_t *tally)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U; /* ui.poll() answers no events when it fails */
    }
    for (i = 0U; i < count; i++) {
        if (bento_ui_id_to_handle(events[i].id) ==
            bento_ui_id_to_handle(screen->tiles)) {
            tally->events++;
        }
    }
}

/* A round with a temperature: keep the highest, show every fifth, count
 * the events. */
static void use_temp(const tile_screen_t *screen, tally_t *tally,
                     int32_t round, bento_real_t temp_c)
{
    if (!tally->has_hottest || (temp_c > tally->hottest)) {
        tally->hottest = temp_c;
        tally->has_hottest = true;
    }
    if ((round % SHOW_EVERY) == 0) {
        show_and_ask(screen, tally, temp_c);
    }
    count_events(screen, tally);
}

/* One round: read, then use the temperature. A round without one throws
 * one poll away instead. */
static bento_status_t run_round(const tile_screen_t *screen,
                                tally_t *tally, int32_t round)
{
    bento_sensors_snapshot_t snap;
    bento_real_t temp_c = (bento_real_t)0.0;
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    bento_status_t status;

    bento_sensors_snapshot_init(&snap);
    status = bento_sensors_snapshot(BENTO_SENSORS_SNAPSHOT_DEFAULT_MS,
                                    &snap);
    if ((status == BENTO_OK) &&
        read_temp(&snap, &tally->source, &temp_c)) {
        use_temp(screen, tally, round, temp_c);
    } else if (status == BENTO_OK) {
        (void)bento_lcd_print("<span class=warn>ไม่มีค่าอุณหภูมิรอบนี้</span>");
        /* the poll's events are thrown away, as in the original */
        (void)bento_ui_poll(screen->ui, events,
                            BENTO_UI_POLL_EVENTS_MAX, &count, NULL);
    } else {
        /* the snapshot's status ends the program */
    }
    if (status == BENTO_OK) {
        status = wait_ms(PERIOD_MS);
    }
    return status;
}

static bento_status_t run_loop(const tile_screen_t *screen,
                               tally_t *tally)
{
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = run_round(screen, tally, round);
    }
    return status;
}

static void print_summary(const tile_screen_t *screen,
                          const tally_t *tally)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "สั่งย้าย %" PRId32 " ครั้ง ได้ event %" PRId32 " ครั้ง",
                   tally->asked, tally->events);
    set_text(screen, screen->line, text);
    (void)snprintf(text, sizeof(text),
                   "<span class=warn>สั่งย้ายช่อง %" PRId32
                   " ครั้ง จอไม่ขยับ</span>",
                   tally->asked);
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text), "event จาก Tileview = %" PRId32,
                   tally->events);
    (void)bento_lcd_print(text);
    (void)printf(
        "Tileview: นิ้วสั่งได้ โปรแกรมสั่งไม่ได้ และถามไม่ได้ว่าอยู่ช่องไหน\n");
}

int main(void)
{
    tile_screen_t screen;
    tally_t tally;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(&tally, 0, sizeof(tally));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        print_intro(&screen);
        status = run_loop(&screen, &tally);
    }
    if (status == BENTO_OK) {
        print_summary(&screen, &tally);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
