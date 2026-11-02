/*
 * 07_spangroup_event_log.c -- บันทึกเหตุการณ์บนจอ ที่ยังอ่านออกตอนถ่ายเอกสารขาวดำ
 *
 * การ์ดกลางจอคือบันทึกเหตุการณ์ห้าบรรทัดล่าสุด เป็น SpanGroup ย่อหน้าเดียวที่
 * ประกอบจากหลายท่อน เวลาเป็นตัวเล็กสีจาง ข้อความเป็นตัวปกติ ส่วนคำที่บอก
 * ความรุนแรงมีเส้นใต้ ลองหรี่ตามองให้ภาพเบลอ บรรทัดที่มีเส้นใต้ยังโดดออกมา
 * pen() เป็นปากกา มีผลกับท่อนที่เติมหลังจากนั้นเท่านั้น และท่อนไม่มีแฮนเดิล
 * จึงต้อง clear_items() แล้วเขียนใหม่ทั้งย่อหน้าทุกครั้ง
 *
 * The card in the middle is the event log of the last five lines: one
 * SpanGroup paragraph made of spans. The time is a small dim span, the text
 * a normal one, and the word that tells the severity is underlined, so the
 * lines still stand out on a black-and-white copy. pen() sets the colour of
 * the spans added after it only, and a span has no handle, so the whole
 * paragraph is cleared and written again on every new event. 60 rounds of
 * one second, then a closing line.
 *
 * Twin of 07_spangroup_event_log.py
 * (embedded-systems-for-aiot-developer/examples/s12/07_spangroup_event_log.py),
 * from the library twin aiot-s12-07-spangroup-event-log. Style: CLEAN_C.md.
 * What C changes: FEED is a table of structs and LEVEL a table indexed by
 * the level, so the dict lookup cannot fail; the list `shown` is an array of
 * FEED rows and a length, which holds at most MAX_LINES + 1 rows before the
 * oldest is dropped, as append-then-pop(0) does. The span, pen and lcd
 * setters are ignored on refusal, as in MicroPython.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

enum { /* layout */
    MARGIN_X = 24,
    TITLE_Y = 16,
    CARD_Y = 64,
    CARD_W = 744,
    CARD_H = 272,
    CARD_RADIUS = 8,
    LOG_X = 48,
    LOG_Y = 88,
    LOG_W = 696,
    LOG_H = 224,
    FOOT_Y = 344
};
enum { /* fonts, counts, times, sizes */
    FONT_TITLE = 28,
    FONT_FOOT = 20,
    SPAN_TIME_PX = 20, /* the smallest size the rules allow */
    SPAN_TEXT_PX = 24,
    MAX_LINES = 5,
    ROUNDS = 60,
    SETTLE_MS = 200,
    ROUND_MS = 1000, /* one round is one second in this story */
    TEXT_BYTES = 128 /* the display carries at most 127 bytes */
};
enum { /* the course palette, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_OK = 0x30A46C
};

typedef enum {
    LEVEL_NORMAL = 0,
    LEVEL_WATCH = 1,
    LEVEL_FAULT = 2
} level_t;

/* A level: its word, its colour, and whether it is underlined. Colour alone
 * is not enough, so the second column is always there. */
typedef struct {
    const char *name;
    uint32_t color;
    uint8_t decoration;
} level_look_t;

static const level_look_t LEVELS[] = {
    {"ปกติ", COLOR_OK, 0U},
    {"เฝ้าระวัง", COLOR_WARN, BENTO_UI_SPAN_UNDERLINE},
    {"ผิดปกติ", COLOR_BAD, BENTO_UI_SPAN_UNDERLINE}};

/* One made-up event: the second it happens, its level and its text. In real
 * work it comes from step 2 (Decide) of the starter frame. */
typedef struct {
    int32_t at_s;
    level_t level;
    const char *text;
} event_row_t;

static const event_row_t FEED[] = {
    {3, LEVEL_NORMAL, "เริ่มทำงาน รอบส่ง 5 วินาที"},
    {9, LEVEL_NORMAL, "ต่อ broker ได้"},
    {15, LEVEL_WATCH, "อุณหภูมิ 62 องศา"},
    {21, LEVEL_FAULT, "อุณหภูมิ 81 องศา"},
    {27, LEVEL_WATCH, "อุณหภูมิ 64 องศา"},
    {33, LEVEL_NORMAL, "กลับเข้าพิสัย"},
    {39, LEVEL_FAULT, "เน็ตหลุด เข้าคิวไว้"},
    {45, LEVEL_NORMAL, "ต่อกลับได้ ส่งของค้าง 2 ชิ้น"}};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t log;
    bento_ui_id_t foot;
    uint32_t shown[MAX_LINES + 1]; /* rows of FEED, oldest first */
    uint32_t shown_count;
} event_log_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = MARGIN_X;
    args.y = y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = (uint32_t)COLOR_CARD;
    args.max_val = CARD_RADIUS;
    return bento_ui_panel_create(ui, &args, &unused);
}

/* The SpanGroup's color= is the starting colour of a span no pen was set
 * for. */
static bento_status_t add_log(event_log_t *board)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = LOG_X;
    args.y = LOG_Y;
    args.w = LOG_W;
    args.h = LOG_H;
    args.color = (uint32_t)COLOR_TEXT;
    return bento_ui_spangroup_create(board->ui, &args, &board->log);
}

static bento_status_t build_screen(event_log_t *board)
{
    bento_ui_id_t unused;
    bento_status_t status;

    /* MicroPython ignores a refused ui.screen(): the runtime already set it up */
    (void)bento_ui_screen(board->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = bento_time_sleep_ms((uint32_t)SETTLE_MS);
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, "บันทึกเหตุการณ์บนจอ", TITLE_Y,
                           (uint32_t)COLOR_TEXT, FONT_TITLE, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(board->ui);
    }
    if (status == BENTO_OK) {
        status = add_log(board);
    }
    if (status == BENTO_OK) {
        status =
            add_label(board->ui, "เส้นใต้รอดจากภาพขาวดำ", FOOT_Y,
                      (uint32_t)COLOR_DIM, FONT_FOOT, &board->foot);
    }
    return status;
}

/* A span's refusal is ignored, as MicroPython's add_span ignores it. */
static void add_span(const event_log_t *board, const char *text,
                     int32_t size_px, uint8_t flags)
{
    (void)bento_ui_add_span(board->ui, board->log, text,
                            (uint32_t)strlen(text), (uint8_t)size_px,
                            flags);
}

/* Spans have no handle, so the program owns the truth in `shown` and writes
 * the whole paragraph again: the one place allowed to change the log. */
static void redraw(const event_log_t *board)
{
    char text[TEXT_BYTES];
    uint32_t i;

    (void)bento_ui_items_clear(board->ui, board->log);
    for (i = 0U; i < board->shown_count; i++) {
        const event_row_t *event = &FEED[board->shown[i]];
        const level_look_t *look = &LEVELS[event->level];

        (void)bento_ui_set_pen(board->ui, board->log,
                               (uint32_t)COLOR_DIM);
        (void)snprintf(text, sizeof(text), "%3d s  ", (int)event->at_s);
        add_span(board, text, SPAN_TIME_PX, 0U);
        /* the underline tells the level a second way: that survives black
         * and white */
        (void)bento_ui_set_pen(board->ui, board->log, look->color);
        add_span(board, look->name, SPAN_TEXT_PX, look->decoration);
        /* the pen must go back, or the text takes the level's colour too */
        (void)bento_ui_set_pen(board->ui, board->log,
                               (uint32_t)COLOR_TEXT);
        (void)snprintf(text, sizeof(text), "  %s\n", event->text);
        add_span(board, text, SPAN_TEXT_PX, 0U);
    }
}

/* Keeps only the last five lines: a screen longer than a page cannot be
 * read in time. */
static void remember(event_log_t *board, uint32_t row)
{
    uint32_t i;

    board->shown[board->shown_count] = row;
    board->shown_count++;
    if (board->shown_count > (uint32_t)MAX_LINES) {
        for (i = 1U; i < board->shown_count; i++) {
            board->shown[i - 1U] = board->shown[i];
        }
        board->shown_count--;
    }
}

static void add_events_at(event_log_t *board, int32_t now_s)
{
    char text[TEXT_BYTES];
    uint32_t row;

    for (row = 0U; row < (uint32_t)(sizeof(FEED) / sizeof(FEED[0]));
         row++) {
        if (FEED[row].at_s == now_s) {
            remember(board, row);
            redraw(board);
            (void)snprintf(text, sizeof(text), "[%d s] %s - %s",
                           (int)FEED[row].at_s,
                           LEVELS[FEED[row].level].name,
                           FEED[row].text);
            (void)bento_lcd_print(text);
        }
    }
}

static bento_status_t run_rounds(event_log_t *board)
{
    bento_status_t status = BENTO_OK;
    int32_t round;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        add_events_at(board, round);
        status = bento_time_sleep_ms((uint32_t)ROUND_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    static const char foot_done[] = "จบ - หรี่ตามองแล้วยังแยกสามระดับออก";
    event_log_t board;
    bento_status_t status;

    (void)memset(&board, 0, sizeof(board));
    board.ui = bento_runtime_get_ui();
    status = build_screen(&board);
    if (status == BENTO_OK) {
        /* lcd.print() ignores a console that is not ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console(
            "<h2>SpanGroup - ย่อหน้าเดียว หลายท่อน</h2>");
        (void)bento_lcd_print("pen() มีผลกับท่อนที่เติมหลังจากนั้น ไม่ใช่ท่อนเดิม");
        status = run_rounds(&board);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_text(board.ui, board.foot, foot_done,
                                (uint32_t)strlen(foot_done));
        (void)bento_lcd_print(
            "<span class=ok>สีบอกซ้ำ ไม่ใช่บอกอย่างเดียว</span>");
        (void)printf(
            "SpanGroup: pen() ทาท่อนถัดไป, add_span() เติม, clear_items() "
            "ล้างทั้งย่อหน้า\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
