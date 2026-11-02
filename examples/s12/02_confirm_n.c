/*
 * 02_confirm_n.c -- ยืนยัน N รอบก่อนเปลี่ยนสถานะ (Confirm N, Capstone period
 * 12)
 *
 * ระดับใหม่ต้องยืนพื้นครบ 3 รอบติดกันก่อนจึงเปลี่ยนสถานะจริง
 * ค่าที่กระโดดวูบเดียวคือสัญญาณ รบกวน ไม่ใช่เหตุการณ์ บนการ์ด ป้ายซ้าย
 * "เชื่อทันที" เปลี่ยนทุกครั้งที่ค่าข้ามเกณฑ์ ป้ายกลาง "ยืนยัน 3 รอบ"
 * เปลี่ยนเมื่อระดับใหม่ยืนได้ครบ ตัวเลขใต้ป้ายนับว่าเปลี่ยนกี่ครั้ง แถบ
 * streak คือจำนวนรอบที่กำลังสะสม กราฟวาดค่ากับเส้นเกณฑ์
 * ตอนจบสรุปส่วนต่างบนจอและบนคอนโซล
 *
 * A new level must hold for 3 passes in a row before the state really
 * changes: a one-pass spike is noise, not an event. On the card the
 * left label ("believe at once") changes every time the value crosses
 * the limit, the middle one ("confirm 3 passes") only once a new level
 * has held, and the counters below them count the changes. The streak
 * bar shows the passes collected so far, the chart draws the value and
 * the limit line, and at the end the difference is summed up on the
 * screen and on the console.
 *
 * Twin of 02_confirm_n.py
 * (embedded-systems-for-aiot-developer/examples/s12/02_confirm_n.py),
 * merged from the library twin aiot-s12-02-confirm-n. Style: CLEAN_C.md.
 * Stop is not caught by the original: a Stop in any sleep ends the
 * program here too.
 */
#include <inttypes.h>
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
    CONFIRM_N = 3, /* passes in a row before a new level is believed */
    /* 3 passes x 250 ms = 0.75 s late, which is acceptable */
    LOOP_MS = 250,
    /* the chart takes integers: tenths, 0.0-32.0 -> 0-320 */
    CHART_MAX = 320,
    CHART_SCALE = 10, /* value to chart units */
    SETTLE_MS = 200,  /* let the cleared screen settle */
    HOLD_PASSES = 20, /* keep the final screen up for 20 x 100 ms */
    HOLD_MS = 100,
    /* the longest console line built here, with room for its numbers */
    TEXT_CHARS = 160,
    SERIES_COUNT = 17 /* the simulated readings in run_pass() */
};

typedef enum {
    LEVEL_OK = 0,
    LEVEL_ALERT = 1
} level_t;

/* A point on the canvas. */
typedef struct {
    int16_t x;
    int16_t y;
} point_t;

/* One rule's column: its state label and its change counter. */
typedef struct {
    bento_ui_id_t state;
    bento_ui_id_t counter;
} rule_view_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    rule_view_t naive;
    rule_view_t confirmed;
    bento_ui_id_t value;
    bento_ui_id_t streak_bar;
    bento_ui_id_t chart;
    uint8_t value_series;
    uint8_t limit_series;
    bento_ui_id_t footer;
} screen_t;

/* What both rules have decided so far. */
typedef struct {
    level_t naive_state;
    int32_t naive_changes;
    level_t state;
    /* the level that will be believed once it holds CONFIRM_N passes */
    level_t pending;
    int32_t streak;
    int32_t changes;
} rules_t;

/* Screen layout on the 792 x 398 canvas: one card with three columns, a
 * chart, two legend labels and a footer line. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 168,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    NAIVE_X = 40,
    CONFIRMED_X = 288,
    COLUMN_TITLE_Y = 70,
    STATE_Y = 104,
    CHANGED_Y = 158,
    COUNTER_DX = 136, /* the counter sits right of "เปลี่ยนแล้ว" */
    COUNTER_Y = 152,
    COUNTER_W = 88,
    COUNTER_H = 56,
    VALUE_X = 536,
    VALUE_W = 192,
    VALUE_H = 48,
    STREAK_LABEL_Y = 160,
    STREAK_BAR_X = 616,
    STREAK_BAR_Y = 156,
    STREAK_BAR_W = 128,
    STREAK_BAR_H = 32,
    CHART_Y = 240,
    CHART_H = 88,
    LEGEND_Y = 334,
    LEGEND_RIGHT_X = 288,
    FOOTER_Y = 366
};

/* Font sizes, in pixels. */
enum {
    FONT_TITLE = 24,
    FONT_LABEL = 20,
    FONT_STATE = 28,
    FONT_STREAK = 16
};

/* The one narrowing into the API's int16_t positions. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
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

/* One rule's column: the rule's name, its state, "เปลี่ยนแล้ว" and the
 * change counter. Both rules use the same spacing and colours; only the
 * rule differs. */
static bento_status_t add_rule_column(bento_ui_t *ui, const char *title,
                                      int16_t x, rule_view_t *view)
{
    const point_t counter_at = {px(x + COUNTER_DX), COUNTER_Y};
    const point_t counter_size = {COUNTER_W, COUNTER_H};
    const point_t title_at = {x, COLUMN_TITLE_Y};
    const point_t state_at = {x, STATE_Y};
    const point_t changed_at = {x, CHANGED_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, title, title_at, COLOR_DIM, FONT_LABEL, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "OK", state_at, COLOR_OK, FONT_STATE,
                           &view->state);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เปลี่ยนแล้ว", changed_at, COLOR_DIM,
                           FONT_LABEL, &unused);
    }
    if (status == BENTO_OK) {
        /* a count, not a state: plain text colour, so neither side
         * looks "wrong" */
        status = add_seg7(ui, "0", counter_at, counter_size, COLOR_TEXT,
                          &view->counter);
    }
    return status;
}

/* The right column: the raw value and the streak bar. */
static bento_status_t add_value_column(screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t title_at = {VALUE_X, COLUMN_TITLE_Y};
    const point_t value_at = {VALUE_X, STATE_Y};
    const point_t value_size = {VALUE_W, VALUE_H};
    const point_t streak_at = {VALUE_X, STREAK_LABEL_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(ui, "ค่าตอนนี้", title_at, COLOR_DIM,
                                      FONT_LABEL, &unused);

    if (status == BENTO_OK) {
        status = add_seg7(ui, "0.0", value_at, value_size, COLOR_ACCENT,
                          &screen->value);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "streak", streak_at, COLOR_DIM,
                           FONT_STREAK, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = STREAK_BAR_X;
        args.y = STREAK_BAR_Y;
        args.w = STREAK_BAR_W;
        args.h = STREAK_BAR_H;
        args.min_val = 0;
        args.max_val = CONFIRM_N;
        args.font_px = 0;
        status = bento_ui_bar_create(ui, &args, &screen->streak_bar);
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

/* The legend under the chart and the footer, which the summary
 * overwrites at the end. The limit line is flat, so its label points at
 * it even on a monochrome picture. */
static bento_status_t add_legend(screen_t *screen)
{
    const point_t value_at = {MARGIN_X, LEGEND_Y};
    const point_t limit_at = {LEGEND_RIGHT_X, LEGEND_Y};
    const point_t footer_at = {MARGIN_X, FOOTER_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "เส้นฟ้า = ค่าที่วัดได้", value_at, COLOR_ACCENT,
                  FONT_LABEL, &unused);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "เส้นแดง = เกณฑ์ ALERT", limit_at,
                           COLOR_BAD, FONT_LABEL, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "ดูจังหวะที่ป้ายสองใบไม่ตรงกัน", footer_at,
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
        status = add_label(screen->ui,
                           "คาบ 12 - "
                           "ยืนยันกี่รอบถึงจะเชื่อ",
                           title_at, COLOR_TEXT, FONT_TITLE, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_rule_column(screen->ui, "เชื่อทันที", NAIVE_X,
                                 &screen->naive);
    }
    if (status == BENTO_OK) {
        status = add_rule_column(screen->ui, "ยืนยัน 3 รอบ", CONFIRMED_X,
                                 &screen->confirmed);
    }
    if (status == BENTO_OK) {
        status = add_value_column(screen);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_legend(screen);
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

/* The footer is overwritten with a text about as wide as before: the
 * difference between the two counts is the answer of the whole
 * lesson. */
static void show_summary(const screen_t *screen, const rules_t *rules)
{
    char line[TEXT_CHARS];

    (void)snprintf(line, sizeof(line),
                   "ทันที %" PRId32 " ครั้ง - ยืนยัน %" PRId32
                   " ครั้ง - ต่างกัน %" PRId32 " สาย",
                   rules->naive_changes, rules->changes,
                   rules->naive_changes - rules->changes);
    (void)bento_ui_set_text(screen->ui, screen->footer, line,
                            (uint32_t)strlen(line));
    (void)bento_ui_set_color(screen->ui, screen->footer, COLOR_TEXT);

    (void)snprintf(line, sizeof(line),
                   "<span class=error>เชื่อทันที เปลี่ยน %" PRId32
                   " ครั้ง</span>",
                   rules->naive_changes);
    (void)bento_lcd_print(line);
    (void)snprintf(line, sizeof(line),
                   "<span class=ok>ยืนยัน %d รอบ เปลี่ยน %" PRId32
                   " ครั้ง</span>",
                   CONFIRM_N, rules->changes);
    (void)bento_lcd_print(line);
    (void)bento_lcd_print("ส่วนต่างคือสายที่ไม่ต้องโทรกลางดึก");
    (void)snprintf(line, sizeof(line),
                   "<span class=muted>ราคาที่จ่าย: ช้าลง %d ms</span>",
                   CONFIRM_N * LOOP_MS);
    (void)bento_lcd_print(line);
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

/* A rule changed its state: its label, its colour and its counter
 * follow. MicroPython's widget setters ignore a refusal (modui.c), so
 * the twin does too. */
static void show_rule(bento_ui_t *ui, const rule_view_t *view,
                      level_t state, int32_t changes)
{
    static const uint32_t level_colors[] = {COLOR_OK, COLOR_BAD};
    char count[TEXT_CHARS];
    const char *name = LEVEL_NAMES[state];

    (void)bento_ui_set_text(ui, view->state, name,
                            (uint32_t)strlen(name));
    (void)bento_ui_set_color(ui, view->state, level_colors[state]);
    (void)snprintf(count, sizeof(count), "%" PRId32, changes);
    (void)bento_ui_set_text(ui, view->counter, count,
                            (uint32_t)strlen(count));
}

/* The left rule believes every level at once. */
static void apply_naive(const screen_t *screen, rules_t *rules,
                        level_t level)
{
    if (level != rules->naive_state) {
        rules->naive_state = level;
        rules->naive_changes++;
        show_rule(screen->ui, &screen->naive, rules->naive_state,
                  rules->naive_changes);
    }
}

/* The middle rule counts how many passes in a row a level has held (a
 * new level starts the count at one) and changes only once the level
 * has held CONFIRM_N passes and really differs from the state. */
static void apply_confirmed(const screen_t *screen, rules_t *rules,
                            level_t level, int32_t pass)
{
    char line[TEXT_CHARS];

    if (level == rules->pending) {
        rules->streak++;
    } else {
        rules->pending = level;
        rules->streak = 1;
    }
    if ((rules->streak >= CONFIRM_N) &&
        (rules->pending != rules->state)) {
        rules->state = rules->pending;
        rules->changes++;
        show_rule(screen->ui, &screen->confirmed, rules->state,
                  rules->changes);
        (void)snprintf(line, sizeof(line),
                       "<b>ยืนยันแล้ว</b> รอบ %" PRId32 " - เปลี่ยนเป็น %s",
                       pass, LEVEL_NAMES[rules->state]);
        /* lcd.print() ignores a console that is not ready */
        (void)bento_lcd_print(line);
    }
}

/* The value, the streak bar and the chart for this pass. */
static bento_status_t show_value(const screen_t *screen,
                                 const rules_t *rules,
                                 bento_real_t value)
{
    char text[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;
    const int32_t limit_units =
        (int32_t)(ALERT_LIMIT * (bento_real_t)CHART_SCALE);
    bento_status_t status =
        bento_fmt_real_precision(text, sizeof(text), value, 1, &length);

    if (status == BENTO_OK) {
        (void)bento_ui_set_text(screen->ui, screen->value, text,
                                length);
        (void)bento_ui_set_value(
            screen->ui, screen->streak_bar,
            (rules->streak < CONFIRM_N) ? rules->streak : CONFIRM_N);
        (void)bento_ui_chart_set_next(
            screen->ui, screen->chart, screen->value_series,
            (int32_t)(value * (bento_real_t)CHART_SCALE));
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->limit_series,
                                      limit_units);
    }
    return status;
}

/* One pass over a reading. Only the passes where the two rules disagree
 * are logged: those are the passes where confirming is at work. */
static bento_status_t run_pass(const screen_t *screen, rules_t *rules,
                               int32_t pass)
{
    /* Simulated readings, real and false mixed: passes 3 and 8 are
     * one-pass spikes, the run near the end is a real level that
     * stays. */
    static const bento_real_t series[SERIES_COUNT] = {
        (bento_real_t)1.0,  (bento_real_t)2.0,  (bento_real_t)3.0,
        (bento_real_t)22.0, (bento_real_t)2.0,  (bento_real_t)3.0,
        (bento_real_t)1.0,  (bento_real_t)2.0,  (bento_real_t)30.0,
        (bento_real_t)2.0,  (bento_real_t)18.0, (bento_real_t)19.0,
        (bento_real_t)20.0, (bento_real_t)21.0, (bento_real_t)18.0,
        (bento_real_t)3.0,  (bento_real_t)2.0};
    const bento_real_t value = series[pass];
    const level_t level = level_of(value);
    char line[TEXT_CHARS];
    bento_status_t status;

    apply_naive(screen, rules, level);
    apply_confirmed(screen, rules, level, pass);
    status = show_value(screen, rules, value);
    if ((status == BENTO_OK) && (rules->naive_state != rules->state)) {
        (void)snprintf(line, sizeof(line),
                       "<span class=warn>รอบ %" PRId32
                       " - ทันที %s - ยืนยัน %s</span>",
                       pass, LEVEL_NAMES[rules->naive_state],
                       LEVEL_NAMES[rules->state]);
        (void)bento_lcd_print(line);
    }
    if (status == BENTO_OK) {
        poll_display(screen->ui);
        status = nap(LOOP_MS);
    }
    return status;
}

/* Runs every reading, then keeps the final screen up: the two
 * counters are the answer. */
static bento_status_t run_series(const screen_t *screen)
{
    rules_t rules = {LEVEL_OK, 0, LEVEL_OK, LEVEL_OK, 0, 0};
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < SERIES_COUNT) && (status == BENTO_OK); i++) {
        status = run_pass(screen, &rules, i);
    }
    if (status == BENTO_OK) {
        show_summary(screen, &rules);
    }
    for (i = 0; (i < HOLD_PASSES) && (status == BENTO_OK); i++) {
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
        (void)bento_lcd_console(
            "<h2>คาบ 12 - เชื่อทันที เทียบกับ ยืนยัน 3 รอบ</h2>");
        status = run_series(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
