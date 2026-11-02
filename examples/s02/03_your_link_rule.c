/*
 * 03_your_link_rule.c -- เขียนกฎว่าลิงก์แบบไหนใช้ได้จริง (Your Link Rule)
 *
 * แบบฝึกคู่กับ 02 ไม่ต้องต่อเน็ตจริง
 * โปรแกรมเล่นเทปผลตรวจลิงก์ที่บันทึกไว้ รันกี่รอบก็ได้ผลเดิม
 * ลิงก์ที่ใช้งานได้จริงต้องต่ออยู่และมีเลข IP ที่ใช้ได้ (ไม่ใช่
 * "0.0.0.0") พร้อมกัน is_usable() ตอนนี้ดูแค่ครึ่งเดียว จอจึงรายงาน 1
 * ครั้งจากที่ควรได้ 2 แก้ให้สองตัวเลขตรงกัน ต้องเสียติดกัน 3
 * รอบถึงจะรายงานหนึ่งบรรทัด
 *
 * A practice pair for 02 that needs no real network: it plays a
 * recorded tape of link checks, so every run gives the same result. A
 * usable link is connected AND has a real IP address (not "0.0.0.0").
 * is_usable() looks at only half of that now, so the screen reports 1
 * problem where it should report 2: fix it until both numbers match.
 * Three bad rounds in a row make one report.
 *
 * Twin of 03_your_link_rule.py
 * (embedded-systems-for-aiot-developer/examples/s02/03_your_link_rule.py).
 * Style: CLEAN_C.md.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 128,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CAPTION_Y = 70,
    GOT_X = 40,
    WANT_X = 280,
    ROUND_X = 520,
    COUNTER_Y = 104,
    COUNTER_W = 144,
    COUNTER_H = 64,
    LEGEND_Y = 194,
    CHART_Y = 232,
    CHART_H = 72,
    NOW_Y = 312,
    RESULT_Y = 352,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_BAD = 0xE5484D
};

/* The check and its tape. */
enum {
    NEED = 3,         /* bad rounds in a row before one report */
    WANT_REPORTS = 2, /* the tape holds two real problems */
    TAPE_LENGTH = 24,
    LINK_UP = 100, /* the chart's high line: usable */
    LINK_DOWN = 0
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    STEP_MS = 120    /* play the tape slowly enough to watch */
};

/* One round of the tape: what wifi.is_connected() and wifi.ip()
 * answered. */
typedef struct {
    bool online;
    const char *ip;
} link_check_t;

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t got;
    bento_ui_id_t want;
    bento_ui_id_t round;
    bento_ui_id_t chart;
    uint8_t series;
    bento_ui_id_t now;
    bento_ui_id_t result;
} link_screen_t;

/* What the check has counted so far. */
typedef struct {
    int32_t bad_streak;
    int32_t reports;
} link_tally_t;

/* ----- Your part: fill this in. ----- */
/* True when the link can really send data this round. online is what
 * wifi.is_connected() answered; ip is what wifi.ip() answered, always a
 * text. It looks at only half of the task now. Hint: "0.0.0.0" is a
 * text too; compare it with "0.0.0.0" itself. */
static bool is_usable(bool online, const char *ip)
{
    (void)ip; /* the starting rule does not look at the address yet */
    return online;
}

/* ----- End of your part. ----- */

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* ui.poll() here only keeps the screen drawn; its events are not read,
 * and a failed poll answers no events, so its status is not an error.
 * */
static void poll_screen(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t widget,
                      const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static void show_number(bento_ui_t *ui, bento_ui_id_t widget,
                        int32_t number)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, number);
    show_text(ui, widget, text);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                point_t at, uint32_t color,
                                int16_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = at.x;
    args.y = at.y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_CARD; /* the border colour: the card's own */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* A captioned counter: the caption above, the big digits below. */
static bento_status_t add_counter(bento_ui_t *ui, const char *caption,
                                  const char *start, int16_t x,
                                  uint32_t color, bento_ui_id_t *out_id)
{
    const point_t at = {x, CAPTION_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, caption, at, COLOR_DIM, FONT_TEXT_PX, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = start;
        args.text_len = (uint32_t)strlen(start);
        args.x = x;
        args.y = COUNTER_Y;
        args.w = COUNTER_W;
        args.h = COUNTER_H;
        args.color = color;
        status = bento_ui_seg7_create(ui, &args, out_id);
    }
    return status;
}

/* The chart line uses the accent colour: green is kept for "normal"
 * only. */
static bento_status_t add_chart(link_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CHART_Y;
    args.w = CARD_W;
    args.h = CHART_H;
    args.color = COLOR_CARD;
    args.min_val = LINK_DOWN;
    args.max_val = LINK_UP;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            screen->ui, screen->chart, COLOR_ACCENT, &screen->series);
    }
    return status;
}

/* The title and the card with the three counters. The target is
 * text-coloured, not green: it is not a state. */
static bento_status_t build_card(link_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t title = {MARGIN_X, TITLE_Y};
    char want[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "เขียนกฎว่าลิงก์แบบไหนใช้ได้จริง", title, COLOR_TEXT,
                  FONT_TITLE_PX, &unused);

    (void)snprintf(want, sizeof(want), "%d", WANT_REPORTS);
    if (status == BENTO_OK) {
        status = add_card(ui);
    }
    if (status == BENTO_OK) {
        status = add_counter(ui, "กฎคุณรายงาน", "0", GOT_X, COLOR_BAD,
                             &screen->got);
    }
    if (status == BENTO_OK) {
        status = add_counter(ui, "ควรรายงาน", want, WANT_X, COLOR_TEXT,
                             &screen->want);
    }
    if (status == BENTO_OK) {
        status = add_counter(ui, "รอบที่", "0", ROUND_X, COLOR_TEXT,
                             &screen->round);
    }
    return status;
}

/* The legend says high and low, not colour names, so a monochrome
 * screen reads it too. */
static bento_status_t build_chart(link_screen_t *screen)
{
    const point_t legend = {MARGIN_X, LEGEND_Y};
    const point_t now = {MARGIN_X, NOW_Y};
    const point_t result = {MARGIN_X, RESULT_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "เส้นสูง = ใช้ได้  เส้นต่ำ = ใช้ไม่ได้", legend,
                  COLOR_DIM, FONT_TEXT_PX, &unused);

    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่เริ่มเล่นเทป", now, COLOR_DIM,
                           FONT_TEXT_PX, &screen->now);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่ได้ตรวจ", result, COLOR_DIM,
                           FONT_TEXT_PX, &screen->result);
    }
    return status;
}

static bento_status_t build_screen(link_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = build_card(screen);
    }
    if (status == BENTO_OK) {
        status = build_chart(screen);
    }
    return status;
}

/* One line exactly when the evidence is complete (== NEED), not a
 * new line every bad round. */
static void show_streak(const link_screen_t *screen,
                        link_tally_t *tally, int32_t round_number,
                        const link_check_t *check)
{
    char text[TEXT_BYTES];

    if (tally->bad_streak == NEED) {
        tally->reports++;
        (void)snprintf(text, sizeof(text),
                       "รอบ %" PRId32 " รายงาน - เสียติดกัน %" PRId32,
                       round_number, tally->bad_streak);
        show_text(screen->ui, screen->now, text);
        (void)bento_ui_set_color(screen->ui, screen->now, COLOR_BAD);
        (void)snprintf(text, sizeof(text),
                       "<span class=error>รอบ %" PRId32
                       " รายงาน | ต่ออยู่ %s | ip %s </span>",
                       round_number, check->online ? "True" : "False",
                       check->ip);
        /* lcd.print() ignores a console that is not ready */
        (void)bento_lcd_print(text);
    } else {
        (void)snprintf(text, sizeof(text),
                       "รอบ %" PRId32 " เงียบ - เสียติดกัน %" PRId32,
                       round_number, tally->bad_streak);
        show_text(screen->ui, screen->now, text);
        (void)bento_ui_set_color(screen->ui, screen->now, COLOR_DIM);
    }
}

static void show_result(const link_screen_t *screen, int32_t reports)
{
    char text[TEXT_BYTES];

    if (reports == WANT_REPORTS) {
        show_text(screen->ui, screen->result,
                  "ถูกแล้ว - กฎเห็นปัญหาครบทั้งสองแบบ");
        (void)bento_ui_set_color(screen->ui, screen->result, COLOR_OK);
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>ผ่าน - รายงานครบ %" PRId32
                       " ครั้ง</span>",
                       reports);
    } else {
        (void)snprintf(text, sizeof(text),
                       "รายงาน %" PRId32 " ครั้ง แต่ควรได้ %d ครั้ง", reports,
                       WANT_REPORTS);
        show_text(screen->ui, screen->result, text);
        (void)bento_ui_set_color(screen->ui, screen->result, COLOR_BAD);
        (void)snprintf(text, sizeof(text),
                       "<span class=warn>รายงาน %" PRId32
                       " ครั้ง ควรได้ %d </span>",
                       reports, WANT_REPORTS);
    }
    (void)bento_lcd_print(text);
    poll_screen(screen->ui);
}

/* One round of the tape. */
static void check_round(const link_screen_t *screen,
                        link_tally_t *tally, int32_t i)
{
    /* Recorded on a real board carried out of the room and back: one
     * dropped round, a real outage, then a connection without an
     * address. */
    static const char ok_ip[] = "192.168.1.42";
    static const char no_ip[] = "0.0.0.0";
    static const link_check_t tape[TAPE_LENGTH] = {
        {true, ok_ip},  {true, ok_ip},  {true, ok_ip},  {true, ok_ip},
        {true, ok_ip},  {false, no_ip}, {true, ok_ip},  {true, ok_ip},
        {true, ok_ip},  {false, no_ip}, {false, no_ip}, {false, no_ip},
        {false, no_ip}, {true, ok_ip},  {true, ok_ip},  {true, ok_ip},
        {true, no_ip},  {true, no_ip},  {true, no_ip},  {true, no_ip},
        {true, no_ip},  {true, ok_ip},  {true, ok_ip},  {true, ok_ip},
    };
    const link_check_t *check = &tape[i];
    const int32_t round_number = i + 1;
    const bool usable = is_usable(check->online, check->ip);

    if (usable) {
        tally->bad_streak = 0;
    } else {
        tally->bad_streak++;
    }
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  screen->series,
                                  /* fire and forget */
                                  usable ? LINK_UP : LINK_DOWN);
    show_number(screen->ui, screen->round, round_number);
    show_streak(screen, tally, round_number, check);
    show_number(screen->ui, screen->got, tally->reports);
    (void)bento_ui_set_color(
        screen->ui, screen->got,
        (tally->reports == WANT_REPORTS) ? COLOR_OK : COLOR_BAD);
}

static bento_status_t play_tape(const link_screen_t *screen,
                                link_tally_t *tally)
{
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < TAPE_LENGTH) && (status == BENTO_OK); i++) {
        check_round(screen, tally, i);
        poll_screen(screen->ui);
        status = pause_ms(STEP_MS);
    }
    return status;
}

int main(void)
{
    link_screen_t screen;
    link_tally_t tally = {0, 0};
    char text[TEXT_BYTES];
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        poll_screen(screen.ui);
        /* lcd calls ignore a console that is not ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ตรวจกฎว่าลิงก์ใช้ได้จริงไหม</h2>");
        (void)snprintf(
            text, sizeof(text),
            "<span class=muted>ต้องเสียติดกัน %d รอบถึงจะรายงาน</span>",
            NEED);
        (void)bento_lcd_print(text);
        status = play_tape(&screen, &tally);
    }
    if (status == BENTO_OK) {
        show_result(&screen, tally.reports);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
