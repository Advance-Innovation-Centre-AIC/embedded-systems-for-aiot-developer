/*
 * 01_state_machine.c -- สามสถานะ และเส้นแบ่งที่ต้องตัดสินใจไว้ล่วงหน้า
 *
 * แยก "ค่าที่วัดได้" ออกจาก "สถานะที่ตัดสินแล้ว" ด้วยฟังก์ชันตัดสินที่รับค่าแล้ว
 * คืนสถานะ (OK, WARN, ALERT) เกณฑ์จึงอยู่ที่เดียวและทดสอบได้ ค่าจำลองสิบห้าค่า
 * ไต่ขึ้นแล้วไต่ลง ตัดผ่านเส้นส้ม (WARN > 8.0) และเส้นแดง (ALERT > 15.0)
 * ป้ายสถานะเปลี่ยนสีตามเส้นที่เพิ่งตัดผ่าน คอนโซลบันทึกเฉพาะตอนที่สถานะเปลี่ยน
 * แล้วตรวจค่าที่ตกบนเส้นพอดี จบเองในราวหกวินาที
 *
 * Keep "the measured value" apart from "the decided state" with a function
 * that takes a value and returns a state (OK, WARN, ALERT), so the limits
 * live in one place and can be tested. Fifteen simulated values climb and
 * fall, crossing the orange line (WARN > 8.0) and the red one (ALERT > 15.0);
 * the state label takes the colour of the line just crossed. The console
 * logs only the changes, then checks values that sit exactly on a line.
 * Ends by itself after about six seconds.
 *
 * Twin of 01_state_machine.py
 * (embedded-systems-for-aiot-developer/examples/s12/01_state_machine.py),
 * merged from the library twin aiot-s12-01-state-machine. Style: CLEAN_C.md.
 * Real numbers are bento_real_t (the board MicroPython's float width) and are
 * printed through bento_fmt, which writes MicroPython's str() and "{:.1f}"
 * texts. The dicts STATE_COLOR and STATE_CLASS are one table, LEVELS.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 250,   /* one value per round */
    /* keep the screen up this many rounds of HOLD_MS after the data */
    HOLD_ROUNDS = 20,
    HOLD_MS = 100,
    /* the chart takes integers only: tenths, 0.0-22.0 -> 0-220 */
    CHART_MAX = 220,
    PERCENT_MAX = 100, /* the bar's range */
    SERIES_LENGTH = 15,
    TEXT_BYTES = 128 /* the longest text built here is 123 bytes */
};

/* A state: its name, its colour and its console class, written in one
 * place. A fourth state is added here and nowhere else. */
typedef enum {
    LEVEL_OK = 0,
    LEVEL_WARN = 1,
    LEVEL_ALERT = 2
} level_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t value_digits;
    bento_ui_id_t state_label;
    bento_ui_id_t changes_digits;
    bento_ui_id_t limit_bar;
    bento_ui_id_t chart;
    uint8_t value_series;
    uint8_t warn_series;
    uint8_t alert_series;
    bento_ui_id_t foot;
} state_screen_t;

/* The state machine's memory: the decided state and how often it
 * changed. */
typedef struct {
    level_t level;
    int32_t changes;
} decision_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* "%.1f" (digits 1) or str() (digits < 0) of value into text, which
 * holds BENTO_FMT_BUF_SIZE bytes: enough for every result, so the
 * format cannot fail. */
static void format_real(char *text, bento_real_t value, int32_t digits)
{
    uint32_t length = 0U;

    if (digits < 0) {
        (void)bento_fmt_real(text, BENTO_FMT_BUF_SIZE, value, &length);
    } else {
        (void)bento_fmt_real_precision(text, BENTO_FMT_BUF_SIZE, value,
                                       digits, &length);
    }
}

/* Layout on a grid of 8 with a 24 px margin. Below y=340 everything
 * ends before x=690, the firmware's Console button. */
enum {
    MARGIN_X = 24,
    INNER_X = 40,
    TITLE_Y = 8,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 160,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CAPTION_Y = 72,
    BIG_Y = 104,
    VALUE_W = 192,
    BIG_H = 56,
    STATE_X = 264,
    CHANGES_X = 528,
    CHANGES_W = 120,
    LIMIT_CAPTION_Y = 172,
    BAR_X = 288,
    BAR_Y = 168,
    BAR_W = 456,
    BAR_H = 32,
    CHART_Y = 232,
    CHART_W = 744,
    CHART_H = 96,
    LEGEND_Y = 336,
    LEGEND_ALERT_X = 256,
    LEGEND_NOTE_X = 488,
    FOOT_Y = 368,
    TITLE_PX = 28,
    TEXT_PX = 20
};

/* The course palette, 0xRRGGBB. The three state colours are used for
 * states only; the accent is for the measured line and for what is
 * changing. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

typedef struct {
    const char *name;
    uint32_t color;
    const char *css_class;
} level_look_t;

static const level_look_t LEVELS[] = {{"OK", COLOR_OK, "ok"},
                                      {"WARN", COLOR_WARN, "warn"},
                                      {"ALERT", COLOR_BAD, "error"}};

static const bento_real_t WARN_LIMIT = (bento_real_t)8.0;
static const bento_real_t ALERT_LIMIT = (bento_real_t)15.0;

/* ui.poll() here only keeps the queue empty; it answers no events
 * when it fails. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The decision. Checked from the strictest down: with WARN first, ALERT
 * could never be reached. > and not >=: a value exactly on a limit is
 * not over it. */
static level_t level_of(bento_real_t value)
{
    level_t level = LEVEL_OK;

    if (value > ALERT_LIMIT) {
        level = LEVEL_ALERT;
    } else if (value > WARN_LIMIT) {
        level = LEVEL_WARN;
    } else {
        /* within the limits */
    }
    return level;
}

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

/* A seven-segment number in the card. */
static bento_status_t add_digits(bento_ui_t *ui, const char *text,
                                 int16_t x, int16_t w, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = BIG_Y;
    args.w = w;
    args.h = BIG_H;
    args.color = color;
    return bento_ui_seg7_create(ui, &args, out_id);
}

/* The card: the measured value, the decided state, and the count of
 * changes. */
static bento_status_t add_card(state_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_CARD;
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    status = bento_ui_panel_create(ui, &args, &unused);
    if (status == BENTO_OK) {
        status = add_label(ui, "ค่าที่วัดได้", INNER_X, CAPTION_Y, COLOR_DIM,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_digits(ui, "0.0", INNER_X, VALUE_W, COLOR_OK,
                            &screen->value_digits);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "สถานะที่ตัดสิน", STATE_X, CAPTION_Y,
                           COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "OK";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = STATE_X;
        args.y = BIG_Y;
        args.color = COLOR_OK;
        args.font_px = TITLE_PX;
        status = bento_ui_label_create(ui, &args, &screen->state_label);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เปลี่ยนสถานะ (ครั้ง)", CHANGES_X, CAPTION_Y,
                           COLOR_DIM, &unused);
    }
    /* a count is not a state, so it is not coloured by level */
    if (status == BENTO_OK) {
        status = add_digits(ui, "0", CHANGES_X, CHANGES_W, COLOR_TEXT,
                            &screen->changes_digits);
    }
    return status;
}

/* A number alone cannot say whether it is high: the bar against the
 * ALERT limit sits in the same card. */
static bento_status_t add_limit_bar(state_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "เทียบเกณฑ์ ALERT 15.0", INNER_X,
                  LIMIT_CAPTION_Y, COLOR_DIM, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = BAR_X;
        args.y = BAR_Y;
        args.w = BAR_W;
        args.h = BAR_H;
        args.min_val = 0;
        args.max_val = PERCENT_MAX;
        args.font_px = 0; /* the bar starts empty */
        status =
            bento_ui_bar_create(screen->ui, &args, &screen->limit_bar);
    }
    return status;
}

/* One chart, three lines: the measured value, and the two limits drawn
 * flat to compare by eye. A limit is not data: it is the team's
 * decision laid over the data. */
static bento_status_t add_chart(state_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CHART_Y;
    args.w = CHART_W;
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
                                      COLOR_WARN, &screen->warn_series);
    }
    if (status == BENTO_OK) {
        status =
            bento_ui_chart_add_series(screen->ui, screen->chart,
                                      COLOR_BAD, &screen->alert_series);
    }
    return status;
}

/* The line labels carry the limits' numbers, so they still point at
 * their lines in black and white; then the empty footer. */
static bento_status_t add_legend(state_screen_t *screen)
{
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "เส้นส้ม = WARN 8.0", MARGIN_X, LEGEND_Y,
                  COLOR_WARN, &unused);

    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "เส้นแดง = ALERT 15.0", LEGEND_ALERT_X,
                      LEGEND_Y, COLOR_BAD, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "lcd เก็บเฉพาะขอบ", LEGEND_NOTE_X,
                           LEGEND_Y, COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "", MARGIN_X, FOOT_Y, COLOR_DIM,
                           &screen->foot);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(state_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "คาบ 12 - สามสถานะกับเส้นแบ่ง";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = MARGIN_X;
        args.y = TITLE_Y;
        args.color = COLOR_TEXT;
        args.font_px = TITLE_PX;
        status = bento_ui_label_create(screen->ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_limit_bar(screen);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_legend(screen);
    }
    return status;
}

/* The screen tells the latest value every round: people look at it to
 * see "now". MicroPython's widget setters ignore a refusal (modui.c),
 * so the twin does too. */
static void show_value(const state_screen_t *screen, bento_real_t value,
                       level_t level)
{
    const bento_real_t tenths = (bento_real_t)10;
    const bento_real_t value_tenths = value * tenths;
    const bento_real_t warn_tenths = WARN_LIMIT * tenths;
    const bento_real_t alert_tenths = ALERT_LIMIT * tenths;
    char text[BENTO_FMT_BUF_SIZE];
    int32_t percent =
        (int32_t)((value * (bento_real_t)PERCENT_MAX) / ALERT_LIMIT);

    format_real(text, value, 1);
    (void)bento_ui_set_text(screen->ui, screen->value_digits, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_color(screen->ui, screen->value_digits,
                             LEVELS[level].color);
    if (percent > PERCENT_MAX) {
        percent = PERCENT_MAX;
    }
    (void)bento_ui_set_value(screen->ui, screen->limit_bar, percent);
    /* Chart.set_next() sends without an answer and ignores a refusal
     * (modui.c) */
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  screen->value_series,
                                  (int32_t)value_tenths);
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  screen->warn_series,
                                  (int32_t)warn_tenths);
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  screen->alert_series,
                                  (int32_t)alert_tenths);
}

/* A change of state is logged with both ends, because "OK -> ALERT" and
 * "WARN -> ALERT" are different news for whoever goes to look. */
static void change_state(const state_screen_t *screen,
                         decision_t *decision, int32_t round,
                         bento_real_t value, level_t level)
{
    char text[TEXT_BYTES];
    char value_text[BENTO_FMT_BUF_SIZE];
    const char *name = LEVELS[level].name;

    format_real(value_text, value, 1);
    (void)snprintf(text, sizeof(text),
                   "<span class=%s>รอบ %" PRId32
                   " - %s - %s -> %s</span>",
                   LEVELS[level].css_class, round, value_text,
                   LEVELS[decision->level].name, name);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
    decision->level = level;
    decision->changes++;
    (void)bento_ui_set_text(screen->ui, screen->state_label, name,
                            (uint32_t)strlen(name));
    (void)bento_ui_set_color(screen->ui, screen->state_label,
                             LEVELS[level].color);
    (void)snprintf(text, sizeof(text), "%" PRId32, decision->changes);
    (void)bento_ui_set_text(screen->ui, screen->changes_digits, text,
                            (uint32_t)strlen(text));
}

/* The summary, then a check by hand of values that sit exactly on a
 * limit. */
static void show_summary(const state_screen_t *screen,
                         const decision_t *decision)
{
    static const bento_real_t on_the_line[] = {
        (bento_real_t)8.0, (bento_real_t)8.1, (bento_real_t)15.0,
        (bento_real_t)15.1};
    char text[TEXT_BYTES];
    char value_text[BENTO_FMT_BUF_SIZE];
    uint32_t i;

    (void)snprintf(text, sizeof(text),
                   "จบชุดข้อมูล - เปลี่ยนสถานะ %" PRId32 " ครั้ง จาก "
                   "%d รอบ",
                   decision->changes, SERIES_LENGTH);
    (void)bento_ui_set_text(screen->ui, screen->foot, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_color(screen->ui, screen->foot, COLOR_TEXT);
    (void)snprintf(text, sizeof(text),
                   "<b>สรุป</b> เปลี่ยน %" PRId32 " ครั้ง - จบที่ %s",
                   decision->changes, LEVELS[decision->level].name);
    (void)bento_lcd_print(text);
    for (i = 0U;
         i < (uint32_t)(sizeof(on_the_line) / sizeof(on_the_line[0]));
         i++) {
        format_real(value_text, on_the_line[i], -1);
        (void)snprintf(
            text, sizeof(text), "<span class=muted>ค่า %s -> %s</span>",
            value_text, LEVELS[level_of(on_the_line[i])].name);
        (void)bento_lcd_print(text);
    }
}

/* One value per LOOP_MS. Today the values are simulated; a team swaps
 * in its own source, and nothing below this table has to change. */
static bento_status_t run_series(const state_screen_t *screen,
                                 decision_t *decision)
{
    static const bento_real_t series[SERIES_LENGTH] = {
        (bento_real_t)0.0,  (bento_real_t)2.0,  (bento_real_t)5.0,
        (bento_real_t)9.0,  (bento_real_t)11.0, (bento_real_t)14.0,
        (bento_real_t)16.0, (bento_real_t)19.0, (bento_real_t)17.0,
        (bento_real_t)12.0, (bento_real_t)9.5,  (bento_real_t)7.0,
        (bento_real_t)3.0,  (bento_real_t)1.0,  (bento_real_t)0.0};
    bento_status_t status = BENTO_OK;
    int32_t round;

    for (round = 0; (round < SERIES_LENGTH) && (status == BENTO_OK);
         round++) {
        const bento_real_t value = series[round];
        const level_t level = level_of(value);

        show_value(screen, value, level);
        if (level != decision->level) {
            change_state(screen, decision, round, value, level);
        }
        drain_events(screen->ui);
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* Keeps the final screen up long enough to read. */
static bento_status_t hold_screen(const state_screen_t *screen)
{
    bento_status_t status = BENTO_OK;
    int32_t round;

    for (round = 0; (round < HOLD_ROUNDS) && (status == BENTO_OK);
         round++) {
        drain_events(screen->ui);
        status = wait_ms(HOLD_MS);
    }
    return status;
}

int main(void)
{
    state_screen_t screen;
    decision_t decision = {LEVEL_OK, 0};
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        drain_events(screen.ui);
        (void)bento_lcd_clear();
        (void)bento_lcd_console(
            "<h2>คาบ 12 - บันทึกเฉพาะตอนที่สถานะเปลี่ยน</h2>");
        (void)bento_lcd_print(
            "<span class=muted>WARN > 8.0 - ALERT > 15.0</span>");
        status = run_series(&screen, &decision);
    }
    if (status == BENTO_OK) {
        show_summary(&screen, &decision);
        status = hold_screen(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
