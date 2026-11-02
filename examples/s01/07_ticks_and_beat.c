/*
 * 07_ticks_and_beat.c -- จังหวะของลูป (Ticks and the Loop's Beat)
 *
 * ลูปที่สั่ง sleep เท่าเดิมทุกรอบไม่ได้เดินตรงเวลา เพราะงานก่อนหลับกินเวลาของมันเอง
 * ตัวอย่างเดิน 40 รอบ ขอคาบละ 200 ms แล้ววัดคาบจริงด้วย ticks_ms() กับ
 * ticks_diff() แสดงรอบที่ คาบจริง และเวลา ช้าสะสมเป็นตัวเลข Seg7
 * พร้อมกราฟสองเส้น (เขียว = คาบจริง ส้ม = คาบที่ขอ) ครึ่งแรกสั่ง sleep เท่าเดิมทุกรอบ
 * ตั้งแต่รอบที่ 20 หักเวลางานออกก่อนหลับ เส้นเขียวจึงลงมาทาบเส้นส้มและเวลาช้าสะสม
 * หยุดโต ตอนจบบอกเวลาที่ใช้จริงเทียบกับที่ตั้งใจไว้ ไม่มีอะไรให้กด
 *
 * A loop that sleeps the same time every round does not keep time: the
 * work before the sleep takes time of its own. The example runs 40
 * rounds, asks for 200 ms each, and measures the real period with
 * ticks_ms() and ticks_diff(). Seg7 numbers show the round, the real
 * period and the drift so far, and a chart draws two lines (green =
 * real period, orange = the period asked for). The first half sleeps
 * the same time every round; from round 20 the work time is taken off
 * the sleep, so the green line drops onto the orange one and the drift
 * stops growing. At the end it shows the time really taken against the
 * time planned. Nothing to tap.
 *
 * Twin of 07_ticks_and_beat.py
 * (embedded-systems-for-aiot-developer/examples/s01/07_ticks_and_beat.py).
 * Style: CLEAN_C.md.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623
};

enum {
    TARGET_MS = 200, /* the period asked for */
    ROUNDS = 40,     /* rounds in all */
    SWITCH_AT = 20,  /* the round where the second way starts */
    /* the chart's top, in ms; a Chart takes integers, its range is
     * fixed */
    CHART_MAX = 400,
    /* a period this much over the target shows as slow */
    SLOW_MARGIN_MS = 10,
    SETTLE_MS = 200, /* let the cleared screen settle */
    /* a label takes at most 126 bytes, a drawer line 127 */
    TEXT_CHARS = 128,
    NUMBER_CHARS = 12 /* the longest int32_t */
};

/* The widgets the program writes over, and the chart's two series. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t round_digits;
    bento_ui_id_t period_digits;
    bento_ui_id_t drift_digits;
    bento_ui_id_t chart;
    uint8_t real_series;
    uint8_t target_series;
    bento_ui_id_t phase;
    bento_ui_id_t note;
} beat_screen_t;

/* Sleeps ms milliseconds (ms > 0); Stop ends the program. */
static bento_status_t pause_ms(int32_t ms)
{
    bento_status_t status = bento_time_sleep_ms((uint32_t)ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    FONT_PX = 20,
    TITLE_FONT_PX = 24,
    TITLE_X = 20,
    TITLE_Y = 12,
    CARD_X = 20,
    CARD_Y = 52,
    /* ends left of the board's Console button, bottom right */
    CARD_W = 652,
    CARD_H = 100,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    CAPTION_Y = 58,
    DIGITS_Y = 88,
    DIGITS_H = 56,
    ROUND_X = 40,
    ROUND_W = 112,
    PERIOD_X = 192,
    DRIFT_X = 400,
    READING_W = 152, /* the period and the drift */
    CHART_X = 20,
    CHART_Y = 164,
    CHART_W = 652,
    CHART_H = 140,
    LEGEND_Y = 306,
    LEGEND_X = 20,
    LEGEND2_X = 336,
    PHASE_X = 20,
    PHASE_Y = 336,
    NOTE_X = 20,
    NOTE_Y = 366
};

/* A widget's top-left corner. */
typedef struct {
    int16_t x;
    int16_t y;
} point_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                point_t at, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
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

/* A caption, and under it a Seg7 number that starts at "0". */
static bento_status_t add_reading(bento_ui_t *ui, const char *caption,
                                  point_t at, int16_t width,
                                  uint32_t color, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;
    bento_status_t status =
        add_label(ui, caption, at, COLOR_DIM, FONT_PX, &label);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "0";
        args.text_len = 1U;
        args.x = at.x;
        args.y = DIGITS_Y;
        args.w = width;
        args.h = DIGITS_H;
        args.color = color;
        status = bento_ui_seg7_create(ui, &args, out_id);
    }
    return status;
}

/* The widget setters (text, value, colour) ignore a refusal in
 * MicroPython (modui.c), so they do here too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t widget,
                      const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static void show_number(bento_ui_t *ui, bento_ui_id_t widget,
                        int32_t number)
{
    char digits[NUMBER_CHARS];

    (void)snprintf(digits, sizeof(digits), "%" PRId32, number);
    show_text(ui, widget, digits);
}

/* ui.poll() hands the display's touches over; nothing here reads them,
 * and a failed poll answers no events, so its result is not needed. */
static void poll_display(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The card: its panel and the three readings. */
static bento_status_t add_card(beat_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CARD_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border colour */
    args.max_val = CARD_RADIUS; /* and max= its corner radius */
    args.font_px = CARD_BORDER_PX;
    status = bento_ui_panel_create(screen->ui, &args, &panel);
    if (status == BENTO_OK) {
        status = add_reading(screen->ui, "รอบที่",
                             (point_t){ROUND_X, CAPTION_Y}, ROUND_W,
                             COLOR_OK, &screen->round_digits);
    }
    if (status == BENTO_OK) {
        status = add_reading(screen->ui, "คาบจริงรอบนี้ (ms)",
                             (point_t){PERIOD_X, CAPTION_Y}, READING_W,
                             COLOR_OK, &screen->period_digits);
    }
    if (status == BENTO_OK) {
        status = add_reading(screen->ui, "ช้าสะสม (ms)",
                             (point_t){DRIFT_X, CAPTION_Y}, READING_W,
                             COLOR_WARN, &screen->drift_digits);
    }
    return status;
}

/* The chart and its legend. One number alone cannot say whether the
 * period is steady: the whole row of them can. */
static bento_status_t add_chart(beat_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t legend;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CHART_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.color = COLOR_CARD;
    args.min_val = 0;
    args.max_val = CHART_MAX;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    /* a Chart starts with series 0 of its own: keep the numbers
     * add_series() answers */
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            screen->ui, screen->chart, COLOR_OK, &screen->real_series);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_WARN,
                                           &screen->target_series);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "เส้นเขียว = คาบจริงที่วัดได้",
                           (point_t){LEGEND_X, LEGEND_Y}, COLOR_DIM,
                           FONT_PX, &legend);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "เส้นส้ม = คาบที่ขอไว้",
                           (point_t){LEGEND2_X, LEGEND_Y}, COLOR_DIM,
                           FONT_PX, &legend);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(beat_screen_t *screen)
{
    bento_ui_id_t title;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "จังหวะของลูป "
                           "กับนาฬิกาที่ไม่รอใคร",
                           (point_t){TITLE_X, TITLE_Y}, COLOR_TEXT,
                           TITLE_FONT_PX, &title);
    }
    if (status == BENTO_OK) {
        status = add_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "ท่าที่ 1 - สั่ง sleep "
                           "เท่าเดิมทุกรอบ",
                           (point_t){PHASE_X, PHASE_Y}, COLOR_WARN,
                           FONT_PX, &screen->phase);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่เริ่มจับเวลา",
                           (point_t){NOTE_X, NOTE_Y}, COLOR_DIM,
                           FONT_PX, &screen->note);
    }
    return status;
}

/* The drawer's calls ignore a console that is not ready (modlcd.c),
 * here as in MicroPython. */
static void start_drawer(void)
{
    char line[TEXT_CHARS];
    const int32_t target_ms = TARGET_MS;

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>จังหวะของลูป</h2>");
    (void)snprintf(line, sizeof(line),
                   "<span class=muted>ขอคาบละ %" PRId32 " ms</span>",
                   target_ms);
    (void)bento_lcd_print(line);
}

/* One round's readings. The first round has no round before it to
 * measure against, so its period is dropped, as the first difference of
 * any measurement is. */
static void show_round(const beat_screen_t *screen,
                       int32_t round_number, int32_t period_ms,
                       int32_t drift_ms)
{
    uint32_t period_color = COLOR_OK;

    show_number(screen->ui, screen->round_digits, round_number);
    if (round_number > 1) {
        show_number(screen->ui, screen->period_digits, period_ms);
        show_number(screen->ui, screen->drift_digits, drift_ms);
        if (period_ms > (TARGET_MS + SLOW_MARGIN_MS)) {
            period_color = COLOR_WARN;
        }
        (void)bento_ui_set_color(screen->ui, screen->period_digits,
                                 period_color);
        /* set_next() is sent without a reply (modui.c): there is no
         * answer to check */
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->real_series, period_ms);
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->target_series, TARGET_MS);
    }
}

static void show_result(const beat_screen_t *screen, uint32_t start_ms)
{
    const int32_t total_ms =
        bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);
    const int32_t rounds = ROUNDS;
    const int32_t planned_ms = ROUNDS * TARGET_MS;
    char text[TEXT_CHARS];

    (void)snprintf(text, sizeof(text),
                   "เดิน %" PRId32 " รอบใช้จริง %" PRId32
                   " ms - ตั้งใจไว้ %" PRId32 " ms",
                   rounds, total_ms, planned_ms);
    show_text(screen->ui, screen->note, text);
    (void)bento_ui_set_color(screen->ui, screen->note, COLOR_DIM);
    poll_display(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "ใช้จริง %" PRId32 " ms | ตั้งใจไว้ %" PRId32 " ms",
                   total_ms, planned_ms);
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ส่วนต่าง %" PRId32 " ms</span>",
                   total_ms - planned_ms);
    (void)bento_lcd_print(text);
}

/* The second way: measure this round's work and sleep only what is
 * left of the period. */
static bento_status_t pace_round(const beat_screen_t *screen,
                                 int32_t round_number, uint32_t work_ms)
{
    char line[TEXT_CHARS];
    int32_t left_ms;
    bento_status_t status = BENTO_OK;

    if (round_number == SWITCH_AT) {
        show_text(screen->ui, screen->phase,
                  "ท่าที่ 2 - หักเวลางานออกก่อนหลับ");
        (void)bento_ui_set_color(screen->ui, screen->phase, COLOR_OK);
        (void)snprintf(line, sizeof(line),
                       "<span class=info>เปลี่ยนเป็นท่าที่ 2 ที่รอบ "
                       "%" PRId32 " </span>",
                       round_number);
        (void)bento_lcd_print(line);
    }
    left_ms = TARGET_MS -
              bento_time_ticks_diff(bento_time_ticks_ms(), work_ms);
    if (left_ms > 0) {
        status = pause_ms(left_ms);
        /* the work took longer than the period: no time left to sleep,
     * say so */
    } else {
        show_text(screen->ui, screen->note,
                  "รอบนี้งานยาวกว่าคาบที่ขอ ไม่ได้หลับเลย");
        (void)bento_ui_set_color(screen->ui, screen->note, COLOR_WARN);
    }
    return status;
}

/* ROUNDS rounds of the same work. Times are always taken apart with
 * ticks_diff(): the clock wraps, and a plain subtraction would then
 * give a huge negative number. */
static bento_status_t run_rounds(const beat_screen_t *screen,
                                 uint32_t start_ms)
{
    uint32_t previous_ms = start_ms;
    int32_t round_number;
    bento_status_t status = BENTO_OK;

    for (round_number = 1;
         (round_number <= ROUNDS) && (status == BENTO_OK);
         round_number++) {
        const uint32_t work_ms = bento_time_ticks_ms();
        const int32_t period_ms =
            bento_time_ticks_diff(work_ms, previous_ms);
        const int32_t drift_ms =
            bento_time_ticks_diff(work_ms, start_ms) -
            (round_number * TARGET_MS);

        previous_ms = work_ms;
        show_round(screen, round_number, period_ms, drift_ms);
        poll_display(screen->ui);
        if (round_number < SWITCH_AT) {
            /* the first way: the same sleep every round, so the
             * work's time adds up */
            status = pause_ms(TARGET_MS);
        } else {
            status = pace_round(screen, round_number, work_ms);
        }
    }
    return status;
}

int main(void)
{
    beat_screen_t screen = {
        bento_runtime_get_ui(), 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        poll_display(screen.ui);
        start_drawer();
    }
    if (status == BENTO_OK) {
        /* ticks, not the calendar: milliseconds since the board
         * started */
        const uint32_t start_ms = bento_time_ticks_ms();

        status = run_rounds(&screen, start_ms);
        if (status == BENTO_OK) {
            show_result(&screen, start_ms);
        }
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
