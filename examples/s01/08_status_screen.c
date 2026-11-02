/*
 * 08_status_screen.c -- จอสถานะหนึ่งใบ ที่สามโมดูลแบ่งงานกันทำ (Status Screen)
 *
 * ค่าอ่านจำลองไต่ขึ้นลงเป็นรอบ ๆ ผ่านครบทั้งสามระดับ (ปกติ เริ่มสูง
 * ต้องรีบดู) จอแสดงค่า ระดับ แถบ และกราฟทุกรอบ ส่วนลิ้นชัก Console
 * ได้บรรทัดใหม่เฉพาะตอนที่ระดับเปลี่ยน ตัวอย่างเดิน 24 วินาทีแล้วจบ
 * โดยปล่อยค่าสุดท้ายค้างไว้บนจอ
 *
 * A simulated reading climbs and falls through all three levels
 * (normal, high, alarm). The screen shows the value, the level, a bar
 * and a chart on every round, while the Console drawer gets a new line
 * only when the level changes. The example runs for 24 seconds and
 * leaves the last values on the screen.
 *
 * Twin of 08_status_screen.py
 * (embedded-systems-for-aiot-developer/examples/s01/08_status_screen.py).
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

/* The reading and its levels. */
enum {
    READING_MAX = 100,
    WARN_AT = 60,
    ALARM_AT = 85,
    /* the simulated reading moves one step this often */
    READING_STEP_MS = 600,
    READING_STEPS = 20, /* one climb and one fall */
    READING_STEP_SIZE = 10,
    LEVEL_NORMAL = 0,
    LEVEL_HIGH = 1,
    LEVEL_ALARM = 2,
    /* so the first round always counts as a change */
    LEVEL_UNKNOWN = -1
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    TICK_MS = 200,   /* one round of the loop */
    RUN_MS = 24000
};

/* A level's name, colour and Console span class: screen and drawer
 * read the same row. */
typedef struct {
    const char *name;
    uint32_t color;
    const char *span_class;
} level_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t reading;
    bento_ui_id_t level;
    bento_ui_id_t bar;
    bento_ui_id_t chart;
    uint8_t series;
    bento_ui_id_t clock;
    bento_ui_id_t changes;
    bento_ui_id_t rounds;
} status_screen_t;

/* What the loop has counted so far. */
typedef struct {
    int32_t rounds;
    int32_t changes;
    int32_t last_level;
} tally_t;

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static int32_t level_of(int32_t value)
{
    int32_t level = LEVEL_NORMAL;

    if (value >= ALARM_AT) {
        level = LEVEL_ALARM;
    } else if (value >= WARN_AT) {
        level = LEVEL_HIGH;
    } else {
        /* below WARN_AT: normal */
    }
    return level;
}

/* The simulated reading: up in steps of 10 to 100, then down again,
 * over and over. */
static int32_t reading_at(int32_t elapsed_ms)
{
    int32_t step = (elapsed_ms / READING_STEP_MS) % READING_STEPS;

    if (step > (READING_STEPS / 2)) {
        step = READING_STEPS - step;
    }
    return step * READING_STEP_SIZE;
}

/* Screen layout on the 792 x 398 canvas: an 8 px grid with a 24 px
 * margin. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 144,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CAPTION_Y = 70,
    READING_X = 40,
    READING_Y = 104,
    READING_W = 192,
    READING_H = 80,
    LEVEL_X = 264,
    LEVEL_Y = 104,
    BAR_Y = 152,
    BAR_W = 488,
    BAR_H = 32,
    HISTORY_Y = 208,
    CHART_Y = 240,
    CHART_H = 96,
    FOOTER_Y = 350,
    CHANGES_X = 256,
    ROUNDS_X = 576,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    FONT_LEVEL_PX = 28,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB: one role per colour; the last three
 * mean a level only. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

static const level_t LEVELS[] = {
    {"ปกติ", COLOR_OK, "ok"},
    {"เริ่มสูง", COLOR_WARN, "warn"},
    {"ต้องรีบดู", COLOR_BAD, "error"},
};

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

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

static bento_status_t add_reading(status_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0";
    args.text_len = 1U;
    args.x = READING_X;
    args.y = READING_Y;
    args.w = READING_W;
    args.h = READING_H;
    args.color = COLOR_OK;
    return bento_ui_seg7_create(screen->ui, &args, &screen->reading);
}

static bento_status_t add_bar(status_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = LEVEL_X;
    args.y = BAR_Y;
    args.w = BAR_W;
    args.h = BAR_H;
    args.min_val = 0;
    args.max_val = READING_MAX;
    args.font_px = 0; /* the starting value */
    return bento_ui_bar_create(screen->ui, &args, &screen->bar);
}

/* The chart line uses the accent colour: green is kept for "normal"
 * only. */
static bento_status_t add_chart(status_screen_t *screen)
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
    args.max_val = READING_MAX;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            screen->ui, screen->chart, COLOR_ACCENT, &screen->series);
    }
    return status;
}

/* The title and the card with the latest value, its level and the
 * bar. */
static bento_status_t build_card(status_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t title = {MARGIN_X, TITLE_Y};
    const point_t reading_caption = {READING_X, CAPTION_Y};
    const point_t level_caption = {LEVEL_X, CAPTION_Y};
    const point_t level = {LEVEL_X, LEVEL_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "จอสถานะ - จอกับลิ้นชักคนละงาน", title, COLOR_TEXT,
                  FONT_TITLE_PX, &unused);

    if (status == BENTO_OK) {
        status = add_card(ui);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ค่าล่าสุด", reading_caption, COLOR_DIM,
                           FONT_TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_reading(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ระดับตอนนี้", level_caption, COLOR_DIM,
                           FONT_TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, LEVELS[LEVEL_NORMAL].name, level,
                           COLOR_OK, FONT_LEVEL_PX, &screen->level);
    }
    if (status == BENTO_OK) {
        status = add_bar(screen);
    }
    return status;
}

/* The chart of the history and the three footer lines. */
static bento_status_t build_history(status_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t caption = {MARGIN_X, HISTORY_Y};
    const point_t clock = {MARGIN_X, FOOTER_Y};
    const point_t changes = {CHANGES_X, FOOTER_Y};
    const point_t rounds = {ROUNDS_X, FOOTER_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "ประวัติอยู่ในลิ้นชัก Console", caption, COLOR_DIM,
                  FONT_TEXT_PX, &unused);

    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เวลาเดินไป 0 ms", clock, COLOR_DIM,
                           FONT_TEXT_PX, &screen->clock);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ยังไม่เปลี่ยนระดับ", changes, COLOR_DIM,
                           FONT_TEXT_PX, &screen->changes);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "รอบที่ 0", rounds, COLOR_DIM,
                           FONT_TEXT_PX, &screen->rounds);
    }
    return status;
}

static bento_status_t build_screen(status_screen_t *screen)
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
        status = build_history(screen);
    }
    return status;
}

/* The screen's job: show how things are now, on every round. */
static void show_round(const status_screen_t *screen,
                       const level_t *level, int32_t value,
                       int32_t elapsed_ms, int32_t rounds)
{
    bento_ui_t *ui = screen->ui;
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, value);
    show_text(ui, screen->reading, text);
    (void)bento_ui_set_color(ui, screen->reading, level->color);
    show_text(ui, screen->level, level->name);
    (void)bento_ui_set_color(ui, screen->level, level->color);
    (void)bento_ui_set_value(ui, screen->bar, value);
    /* fire and forget */
    (void)bento_ui_chart_set_next(ui, screen->chart, screen->series,
                                  value);
    (void)snprintf(text, sizeof(text), "เวลาเดินไป %" PRId32 " ms",
                   elapsed_ms);
    show_text(ui, screen->clock, text);
    (void)snprintf(text, sizeof(text), "รอบที่ %" PRId32, rounds);
    show_text(ui, screen->rounds, text);
}

/* The drawer's job: one line, only when the level has changed. */
static void log_change(const status_screen_t *screen,
                       const level_t *level, int32_t value,
                       int32_t elapsed_ms, int32_t changes)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "<span class=%s>%" PRId32 " ms  %s  ค่า %" PRId32
                   "</span>",
                   level->span_class, elapsed_ms, level->name, value);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text), "เปลี่ยนระดับไปแล้ว %" PRId32 " ครั้ง",
                   changes);
    show_text(screen->ui, screen->changes, text);
}

/* It lives here because it shares the LEVELS table with
 * build_card(); most of its work is writing the screen. */
/* One round: show the reading, log a level change, and keep to
 * TICK_MS. */
static bento_status_t run_round(const status_screen_t *screen,
                                tally_t *tally, uint32_t round_start_ms,
                                int32_t elapsed_ms)
{
    const int32_t value = reading_at(elapsed_ms);
    const int32_t level = level_of(value);
    int32_t left_ms;
    bento_status_t status = BENTO_OK;

    tally->rounds++;
    show_round(screen, &LEVELS[level], value, elapsed_ms,
               tally->rounds);
    if (level != tally->last_level) {
        tally->changes++;
        tally->last_level = level;
        log_change(screen, &LEVELS[level], value, elapsed_ms,
                   tally->changes);
    }
    poll_screen(screen->ui);
    left_ms = TICK_MS - bento_time_ticks_diff(bento_time_ticks_ms(),
                                              round_start_ms);
    if (left_ms > 0) {
        status = pause_ms((uint32_t)left_ms);
    }
    return status;
}

/* The last values stay on the screen; the end texts are no longer
 * than the ones they replace. */
static void show_end(const status_screen_t *screen,
                     const tally_t *tally)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "จบแล้ว - จอวาด %" PRId32 " รอบ",
                   tally->rounds);
    show_text(screen->ui, screen->clock, text);
    (void)snprintf(text, sizeof(text), "เปลี่ยน %" PRId32 " ครั้ง",
                   tally->changes);
    show_text(screen->ui, screen->changes, text);
    (void)bento_ui_set_color(screen->ui, screen->changes, COLOR_DIM);
    poll_screen(screen->ui);
    (void)bento_lcd_console(
        "<span class=muted>------------------------</span>");
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>จอวาด %" PRId32
                   " รอบ | ลิ้นชักได้ %" PRId32 " บรรทัด</span>",
                   tally->rounds, tally->changes);
    (void)bento_lcd_print(text);
}

static bento_status_t run_loop(const status_screen_t *screen,
                               tally_t *tally)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bool running = true;
    bento_status_t status = BENTO_OK;

    while (running && (status == BENTO_OK)) {
        const uint32_t round_start_ms = bento_time_ticks_ms();
        const int32_t elapsed_ms =
            bento_time_ticks_diff(round_start_ms, start_ms);

        if (elapsed_ms >= RUN_MS) {
            running = false;
        } else {
            status =
                run_round(screen, tally, round_start_ms, elapsed_ms);
        }
    }
    return status;
}

int main(void)
{
    status_screen_t screen = {
        bento_runtime_get_ui(), 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    tally_t tally = {0, 0, LEVEL_UNKNOWN};
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        poll_screen(screen.ui);
        /* lcd calls ignore a console that is not ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ประวัติการเปลี่ยนระดับ</h2>");
        (void)bento_lcd_console(
            "<span "
            "class=muted>บรรทัดจะเพิ่มเฉพาะตอนระดับเปลี่ยน</span>");
        status = run_loop(&screen, &tally);
    }
    if (status == BENTO_OK) {
        show_end(&screen, &tally);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
