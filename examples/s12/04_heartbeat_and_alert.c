/*
 * 04_heartbeat_and_alert.c -- heartbeat กับ alert (Heartbeat and Alert, period 12)
 *
 * ข้อความสองชนิด สองจังหวะ คนละหน้าที่ heartbeat บอกว่า "ยังอยู่"
 * ส่งตามนาฬิกาทุก 2 วินาที ไม่ว่าจะมีเรื่องหรือไม่ alert บอกว่า
 * "มีเรื่อง" ส่งเฉพาะตอนสถานะเปลี่ยนเข้าเขต ALERT และเว้น อย่างน้อย 4
 * วินาทีระหว่างใบ ใบที่มาเร็วเกินถูกกลั้นไว้ บนการ์ดมีสถานะ ค่าตอนนี้
 * ตัวนับ beat alert และใบที่กลั้นไว้ แถบนับถอยหลังถึง heartbeat ใบถัดไป
 * และกราฟค่ากับเส้นเกณฑ์ ทุกใบที่ "ส่ง" ไปลงในคอนโซล
 *
 * Two kinds of message on two clocks, for two jobs: a heartbeat says
 * "still here" every 2 seconds whatever happens; an alert says
 * "something happened", only when the state changes into ALERT, at most
 * once every 4 seconds; one that comes too early is held back. The card
 * shows the state, the current value, the beat, alert and held-back
 * counters, a bar counting down to the next heartbeat, and a chart of
 * the value with the limit line. Every message "sent" goes to the
 * console.
 *
 * Twin of 04_heartbeat_and_alert.py
 * (embedded-systems-for-aiot-developer/examples/s12/04_heartbeat_and_alert.py),
 * merged from the library twin aiot-s12-04-heartbeat-and-alert. Style: CLEAN_C.md. Stop is
 * not caught by the original: a Stop in any sleep ends the program here
 * too.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* The course palette, 0xRRGGBB: one value per role. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_BAD = 0xE5484D
};

enum {
    /* real devices use 30-60 s; short here so a lesson sees it */
    HEARTBEAT_MS = 2000,
    /* the soonest the same alert may be sent again */
    ALERT_GAP_MS = 4000,
    LOOP_MS = 300,
    /* the chart takes integers: tenths, 0.0-22.0 -> 0-220 */
    CHART_MAX = 220,
    CHART_SCALE = 10, /* value to chart units */
    PERCENT = 100,    /* the countdown bar runs 0-100 % */
    SETTLE_MS = 200,  /* let the cleared screen settle */
    HOLD_PASSES = 20, /* keep the final screen up for 20 x 100 ms */
    HOLD_MS = 100,
    COUNTER_COUNT = 3,
    /* the longest console line built here, with room for its numbers */
    TEXT_CHARS = 160
};

typedef enum {
    LEVEL_OK = 0,
    LEVEL_ALERT = 1
} level_t;

/* What a message says it is: its kind and the console class it is
 * shown with. */
typedef enum {
    SEND_ALERT = 0,
    SEND_CLEAR = 1,
    SEND_BEAT = 2
} send_kind_t;

/* The counters on the card, left to right. */
typedef enum {
    COUNTER_BEAT = 0,
    COUNTER_ALERT = 1,
    COUNTER_HELD = 2
} counter_t;

/* A point on the canvas. */
typedef struct {
    int16_t x;
    int16_t y;
} point_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t state;
    bento_ui_id_t value;
    bento_ui_id_t counters[COUNTER_COUNT];
    bento_ui_id_t countdown;
    bento_ui_id_t chart;
    uint8_t value_series;
    uint8_t limit_series;
    bento_ui_id_t footer;
} screen_t;

/* The two clocks and what they have sent so far. */
typedef struct {
    level_t state;
    uint32_t beat_ms; /* when the last heartbeat went out */
    /* false until the first alert, which no gap holds back */
    bool has_alerted;
    uint32_t alert_ms; /* when the last alert really went out */
    int32_t counts[COUNTER_COUNT];
} link_t;

/* Screen layout on the 792 x 398 canvas: one card with five columns, a
 * chart and two lines below it. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 168,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    STATE_X = 40,
    VALUE_X = 184,
    COLUMN_TITLE_Y = 70,
    /* the small "beat" and "alert" titles sit 2 px lower */
    COUNTER_TITLE_Y = 72,
    FIGURE_Y = 104,
    VALUE_W = 128,
    FIGURE_H = 56,
    COUNTER_W = 112,
    BEAT_X = 328,
    ALERT_X = 472,
    HELD_X = 616,
    COUNTDOWN_Y = 170,
    BAR_X = 288,
    BAR_Y = 168,
    BAR_W = 456,
    BAR_H = 32,
    CHART_Y = 240,
    CHART_H = 88,
    NOTE_Y = 334,
    NOTE_RIGHT_X = 256,
    FOOTER_Y = 366
};

/* Font sizes, in pixels. */
enum {
    FONT_TITLE = 24,
    FONT_LABEL = 20,
    FONT_STATE = 28,
    FONT_SMALL = 16
};

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

static bento_status_t add_seg7(bento_ui_t *ui, const char *text,
                               point_t at, point_t size, uint32_t color,
                               bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = at.x;
    args.y = at.y;
    args.w = size.x;
    args.h = size.y;
    args.color = color;
    return bento_ui_seg7_create(ui, &args, out_id);
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
    /* min= repeats the colour, as the original does */
    args.min_val = COLOR_CARD;
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* The two left columns: the state and the current value. */
static bento_status_t add_state_columns(screen_t *screen)
{
    const point_t state_title_at = {STATE_X, COLUMN_TITLE_Y};
    const point_t state_at = {STATE_X, FIGURE_Y};
    const point_t value_title_at = {VALUE_X, COLUMN_TITLE_Y};
    const point_t value_at = {VALUE_X, FIGURE_Y};
    const point_t value_size = {VALUE_W, FIGURE_H};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "สถานะ", state_title_at, COLOR_DIM,
                  FONT_LABEL, &unused);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "OK", state_at, COLOR_OK,
                           FONT_STATE, &screen->state);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ค่าตอนนี้", value_title_at,
                           COLOR_DIM, FONT_LABEL, &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg7(screen->ui, "0.0", value_at, value_size,
                          COLOR_ACCENT, &screen->value);
    }
    return status;
}

/* The three counters side by side: what is compared is their ratio, so
 * all three use the plain text colour (status colours would announce
 * trouble while every count is still 0). */
static bento_status_t add_counters(screen_t *screen)
{
    typedef struct {
        const char *title;
        int16_t x;
        int16_t title_y;
        int16_t font_px;
    } counter_place_t;
    static const counter_place_t places[COUNTER_COUNT] = {
        {"beat", BEAT_X, COUNTER_TITLE_Y, FONT_SMALL},
        {"alert", ALERT_X, COUNTER_TITLE_Y, FONT_SMALL},
        {"กลั้นไว้", HELD_X, COLUMN_TITLE_Y, FONT_LABEL}};
    const point_t size = {COUNTER_W, FIGURE_H};
    bento_ui_id_t unused;
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < (uint32_t)COUNTER_COUNT) && (status == BENTO_OK);
         i++) {
        const point_t title_at = {places[i].x, places[i].title_y};
        const point_t figure_at = {places[i].x, FIGURE_Y};

        status = add_label(screen->ui, places[i].title, title_at,
                           COLOR_DIM, places[i].font_px, &unused);
        if (status == BENTO_OK) {
            status = add_seg7(screen->ui, "0", figure_at, size,
                              COLOR_TEXT, &screen->counters[i]);
        }
    }
    return status;
}

/* The countdown to the next heartbeat: it fills, then starts again. */
static bento_status_t add_countdown(screen_t *screen)
{
    const point_t title_at = {STATE_X, COUNTDOWN_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "ถึง heartbeat ใบถัดไป", title_at,
                  COLOR_DIM, FONT_LABEL, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = BAR_X;
        args.y = BAR_Y;
        args.w = BAR_W;
        args.h = BAR_H;
        args.min_val = 0;
        args.max_val = PERCENT;
        args.font_px = 0;
        status =
            bento_ui_bar_create(screen->ui, &args, &screen->countdown);
    }
    return status;
}

/* The chart with two series: the value (blue) and the ALERT limit
 * (red). */
static bento_status_t add_chart(screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CHART_Y;
    args.w = CARD_W;
    args.h = CHART_H;
    args.color = COLOR_CARD;
    args.min_val = 0;
    args.max_val = CHART_MAX;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_ACCENT,
                                           &screen->value_series);
    }
    if (status == BENTO_OK) {
        status =
            bento_ui_chart_add_series(screen->ui, screen->chart,
                                      COLOR_BAD, &screen->limit_series);
    }
    return status;
}

/* Two standing notes, then the footer: it starts as the chart's legend
 * and the summary overwrites it at the end; both are about as wide, so
 * neither runs over its neighbour. */
static bento_status_t add_notes(screen_t *screen)
{
    const point_t beat_at = {MARGIN_X, NOTE_Y};
    const point_t alert_at = {NOTE_RIGHT_X, NOTE_Y};
    const point_t footer_at = {MARGIN_X, FOOTER_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "beat เดินตามนาฬิกา", beat_at, COLOR_DIM,
                  FONT_LABEL, &unused);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "alert เดินตามเหตุการณ์", alert_at,
                           COLOR_DIM, FONT_LABEL, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "เส้นแดงคือเกณฑ์ ALERT", footer_at,
                           COLOR_DIM, FONT_LABEL, &screen->footer);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(screen_t *screen)
{
    const point_t title_at = {MARGIN_X, TITLE_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "คาบ 12 - heartbeat กับ alert",
                           title_at, COLOR_TEXT, FONT_TITLE, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_state_columns(screen);
    }
    if (status == BENTO_OK) {
        status = add_counters(screen);
    }
    if (status == BENTO_OK) {
        status = add_countdown(screen);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_notes(screen);
    }
    return status;
}

/* ui.poll() here only keeps the display's queue moving: its events are
 * not used, and a failed poll answers none in MicroPython, so its
 * status is not a failure. */
static void poll_display(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The countdown bar: the share of HEARTBEAT_MS since the last
 * heartbeat, read on the clock now (after this pass's messages), true
 * division truncated as int() does, capped at 100. */
static void show_countdown(const screen_t *screen, const link_t *link)
{
    const int32_t since_ms =
        bento_time_ticks_diff(bento_time_ticks_ms(), link->beat_ms);
    const int32_t scaled_ms = since_ms * PERCENT;
    const bento_real_t share =
        (bento_real_t)scaled_ms / (bento_real_t)HEARTBEAT_MS;
    const int32_t percent = (int32_t)share;

    (void)bento_ui_set_value(screen->ui, screen->countdown,
                             (percent > PERCENT) ? PERCENT : percent);
}

static void show_summary(const screen_t *screen, const link_t *link)
{
    char line[TEXT_CHARS];

    (void)snprintf(line, sizeof(line),
                   "beat %" PRId32 " ใบ - alert %" PRId32
                   " ใบ - กลั้นไว้ %" PRId32 " ใบ",
                   link->counts[COUNTER_BEAT],
                   link->counts[COUNTER_ALERT],
                   link->counts[COUNTER_HELD]);
    (void)bento_ui_set_text(screen->ui, screen->footer, line,
                            (uint32_t)strlen(line));
    (void)bento_ui_set_color(screen->ui, screen->footer, COLOR_TEXT);
    (void)snprintf(line, sizeof(line),
                   "<b>สรุป</b> beat %" PRId32 " - alert %" PRId32
                   " - กลั้นไว้ %" PRId32,
                   link->counts[COUNTER_BEAT],
                   link->counts[COUNTER_ALERT],
                   link->counts[COUNTER_HELD]);
    (void)bento_lcd_print(line);
    (void)bento_lcd_print(
        "<span class=muted>ไม่เห็น beat สองคาบ = อุปกรณ์หาย</span>");
}

static const bento_real_t ALERT_LIMIT = (bento_real_t)15.0;

static const char *const LEVEL_NAMES[] = {"OK", "ALERT"};

static level_t level_of(bento_real_t value)
{
    return (value > ALERT_LIMIT) ? LEVEL_ALERT : LEVEL_OK;
}

/* A sleep, with Stop turned into BENTO_E_CANCELLED. */
static bento_status_t nap(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* "Sends" one message: here it only goes to the console drawer; a real
 * device would call mqtt.publish or tesaiot.publish, so the channel
 * changes in this one place. */
static bento_status_t send(send_kind_t kind, bento_real_t value,
                           level_t state)
{
    static const char *const kind_names[] = {"alert", "clear", "beat"};
    static const char *const kind_classes[] = {"error", "info", "ok"};
    char number[BENTO_FMT_BUF_SIZE];
    char line[TEXT_CHARS];
    uint32_t length = 0U;
    const bento_status_t status =
        bento_fmt_real(number, sizeof(number), value, &length);

    if (status == BENTO_OK) {
        (void)snprintf(line, sizeof(line),
                       "<span class=%s>[ส่ง %s] v=%s state=%s</span>",
                       kind_classes[kind], kind_names[kind], number,
                       LEVEL_NAMES[state]);
        /* lcd.print() ignores a console that is not ready */
        (void)bento_lcd_print(line);
    }
    return status;
}

/* A counter on the card went up. MicroPython's widget setters ignore
 * a refusal (modui.c). */
static void count_up(const screen_t *screen, link_t *link,
                     counter_t counter)
{
    char text[TEXT_CHARS];

    link->counts[counter]++;
    (void)snprintf(text, sizeof(text), "%" PRId32,
                   link->counts[counter]);
    (void)bento_ui_set_text(screen->ui, screen->counters[counter], text,
                            (uint32_t)strlen(text));
}

/* Entering ALERT: an alert goes out unless the last one that really
 * went out is less than ALERT_GAP_MS ago; then it is held back (the gap
 * counts from the last sent one, not from the last try). */
static bento_status_t raise_alert(const screen_t *screen, link_t *link,
                                  bento_real_t value, uint32_t now_ms)
{
    char line[TEXT_CHARS];
    bento_status_t status = BENTO_OK;

    if (!link->has_alerted ||
        (bento_time_ticks_diff(now_ms, link->alert_ms) >=
         ALERT_GAP_MS)) {
        status = send(SEND_ALERT, value, link->state);
        if (status == BENTO_OK) {
            link->alert_ms = now_ms;
            link->has_alerted = true;
            count_up(screen, link, COUNTER_ALERT);
        }
    } else {
        count_up(screen, link, COUNTER_HELD);
        (void)snprintf(line, sizeof(line),
                       "<span class=warn>[กลั้นไว้] ยังเหลืออีก "
                       "%" PRId32 " ms</span>",
                       ALERT_GAP_MS - bento_time_ticks_diff(
                                          now_ms, link->alert_ms));
        (void)bento_lcd_print(line);
    }
    return status;
}

/* The first clock: did the state change? Into ALERT an alert may go
 * out; back to OK a "clear" always goes out, or the receiver never
 * learns that the trouble is over. */
static bento_status_t follow_state(const screen_t *screen, link_t *link,
                                   bento_real_t value, uint32_t now_ms)
{
    static const uint32_t level_colors[] = {COLOR_OK, COLOR_BAD};
    const level_t level = level_of(value);
    bento_status_t status = BENTO_OK;

    if (level != link->state) {
        link->state = level;
        (void)bento_ui_set_text(screen->ui, screen->state,
                                LEVEL_NAMES[level],
                                (uint32_t)strlen(LEVEL_NAMES[level]));
        (void)bento_ui_set_color(screen->ui, screen->state,
                                 level_colors[level]);
        if (level == LEVEL_ALERT) {
            status = raise_alert(screen, link, value, now_ms);
        } else {
            status = send(SEND_CLEAR, value, link->state);
        }
    }
    return status;
}

/* The second clock: a heartbeat every HEARTBEAT_MS, whatever the
 * state. */
static bento_status_t follow_heartbeat(const screen_t *screen,
                                       link_t *link, bento_real_t value,
                                       uint32_t now_ms)
{
    bento_status_t status = BENTO_OK;

    if (bento_time_ticks_diff(now_ms, link->beat_ms) >= HEARTBEAT_MS) {
        link->beat_ms = now_ms;
        status = send(SEND_BEAT, value, link->state);
        if (status == BENTO_OK) {
            count_up(screen, link, COUNTER_BEAT);
        }
    }
    return status;
}

/* The value and the chart for this pass. */
static bento_status_t show_value(const screen_t *screen,
                                 bento_real_t value)
{
    char text[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;
    const int32_t limit_units =
        (int32_t)(ALERT_LIMIT * (bento_real_t)CHART_SCALE);
    const bento_status_t status =
        bento_fmt_real_precision(text, sizeof(text), value, 1, &length);

    if (status == BENTO_OK) {
        (void)bento_ui_set_text(screen->ui, screen->value, text,
                                length);
        (void)bento_ui_chart_set_next(
            screen->ui, screen->chart, screen->value_series,
            (int32_t)(value * (bento_real_t)CHART_SCALE));
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->limit_series,
                                      limit_units);
    }
    return status;
}

static bento_status_t run_pass(const screen_t *screen, link_t *link,
                               bento_real_t value)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    bento_status_t status = show_value(screen, value);

    if (status == BENTO_OK) {
        status = follow_state(screen, link, value, now_ms);
    }
    if (status == BENTO_OK) {
        status = follow_heartbeat(screen, link, value, now_ms);
    }
    if (status == BENTO_OK) {
        show_countdown(screen, link);
        poll_display(screen->ui);
        status = nap(LOOP_MS);
    }
    return status;
}

/* Runs every reading, then keeps the final screen up long enough to
 * read the counters. */
static bento_status_t run_series(const screen_t *screen)
{
    static const bento_real_t series[] = {
        (bento_real_t)2.0,  (bento_real_t)3.0,  (bento_real_t)16.0,
        (bento_real_t)17.0, (bento_real_t)16.5, (bento_real_t)18.0,
        (bento_real_t)4.0,  (bento_real_t)3.0,  (bento_real_t)2.0,
        (bento_real_t)19.0, (bento_real_t)20.0, (bento_real_t)3.0,
        (bento_real_t)2.0,  (bento_real_t)1.0,  (bento_real_t)2.0,
        (bento_real_t)3.0,  (bento_real_t)2.0,  (bento_real_t)1.0};
    const uint32_t passes =
        (uint32_t)(sizeof(series) / sizeof(series[0]));
    link_t link;
    bento_status_t status = BENTO_OK;
    uint32_t i;

    (void)memset(&link, 0, sizeof(link));
    link.state = LEVEL_OK;
    link.beat_ms = bento_time_ticks_ms();
    for (i = 0U; (i < passes) && (status == BENTO_OK); i++) {
        status = run_pass(screen, &link, series[i]);
    }
    if (status == BENTO_OK) {
        show_summary(screen, &link);
    }
    for (i = 0U; (i < (uint32_t)HOLD_PASSES) && (status == BENTO_OK);
         i++) {
        poll_display(screen->ui);
        status = nap(HOLD_MS);
    }
    return status;
}

int main(void)
{
    screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        poll_display(screen.ui);
        /* the lcd calls ignore a console that is not ready
         * (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>คาบ 12 - "
                                "ทุกใบที่ส่งออกไปจริง</h2>");
        status = run_series(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
