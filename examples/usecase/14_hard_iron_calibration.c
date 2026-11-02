/*
 * 14_hard_iron_calibration.c -- การคาลิเบรตเข็มทิศ เป็นสิ่งที่วัดได้
 * (hard-iron calibration you can measure)
 *
 * ล้างค่าชดเชย hard-iron ของ BMM350 แล้วให้ผู้ใช้หมุนบอร์ดเป็นเลขแปดช้า ๆ
 * กราฟสองเส้นคือ offset_x (ฟ้า) กับ offset_y (แดง) ตอนเริ่มแกว่งแล้วค่อย ๆ แบน
 * Seg7 บอกว่ารอบนี้ค่าชดเชยขยับไปกี่ uT แถบขวานับรอบที่นิ่งติดกัน เมื่อนิ่งครบ
 * หกรอบและ valid เป็นจริง โปรแกรมหยุดและบอกรอบที่นิ่ง จอ LCD จดค่าก่อนและหลัง
 * และทิศที่อ่านได้ตอนจบ
 *
 * Clears the BMM350's hard-iron offsets, then asks you to turn the
 * board slowly in a figure eight. The chart's two lines are offset_x
 * (blue) and offset_y (red): they swing at first, then flatten. The
 * Seg7 shows how far the offsets moved this round, and the bar counts
 * the still rounds in a row. After six still rounds with valid set the
 * program stops and names the round. The LCD logs the offsets before
 * and after, and the heading at the end.
 *
 * Twin of 14_hard_iron_calibration.py
 * (embedded-systems-for-aiot-developer/examples/usecase/
 *  14_hard_iron_calibration.py), rewritten clean from the library twin
 * aiot-usecase-14-hard-iron-calibration.
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - cal_status() fills a struct whose offset_x, offset_y and valid are
 *    the dict's three keys; a board without a BMM350 has no
 *    sensors.bmm350 (AttributeError), here the call's status ends the
 *    program;
 *  - cal_reset() prints "BMM350: calibration reset. Rotate board 360
 *    deg." inside the MicroPython binding; the C call prints nothing,
 *    so the program prints that line itself;
 *  - prev_x and prev_y start as None and are always set together, so
 *    one flag says whether both hold a value;
 *  - a float is bento_real_t (float on the PSoC Edge kits, double on
 *    the reTerminal), and str(), "%.1f" and round(x, 1) go through
 *    bento_fmt so the text is the board's MicroPython text;
 *  - Stop during a sleep raises KeyboardInterrupt in MicroPython; here
 *    the sleep returns and the program ends at that point.
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* The original's own knobs. */
enum {
    ROUNDS = 60,        /* rounds watched, about half a second each */
    SETTLED_ROUNDS = 6, /* still rounds in a row that mean settled */
    SUB_READS = 5, /* chart points per round, so the line has shape */
    SUB_READ_MS = 100,
    LOG_EVERY = 4, /* the LCD logs every fourth round */
    TEXT_CHARS = 160
};

/* Colours, 0xRRGGBB; 0 leaves the widget's own colour. */
enum {
    COLOR_DEFAULT = 0,
    COLOR_BLUE = 0x4A9EFF,
    COLOR_RED = 0xE5484D,
    COLOR_DIM = 0x9AA3AF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623
};

/* Layout on the 792 x 398 canvas: the chart and the card on the left,
 * the legend, the movement and the still counter on the right. */
enum {
    LEFT_X = 24,
    RIGHT_X = 496,
    CARD_TEXT_X = 40,
    TITLE_Y = 8,
    CHART_Y = 48,
    CHART_W = 440,
    CHART_H = 192,
    CHART_MIN_UT = -50,
    CHART_MAX_UT = 50,
    LEGEND_X_Y = 48,
    LEGEND_Y_Y = 76,
    LEGEND_SAME_Y = 104,
    MOVE_LABEL_Y = 148,
    SEG_Y = 180,
    SEG_W = 180,
    SEG_H = 48,
    STABLE_Y = 244,
    BAR_Y = 280,
    BAR_W = 248,
    BAR_H = 20,
    CARD_Y = 268,
    CARD_H = 108,
    STATUS_Y = 288,
    DETAIL_Y = 328,
    BIG_PX = 24,
    SMALL_PX = 20,
    SEG_LIMIT_UT = 100 /* the Seg7 shows "--" from here up */
};

/* Where a widget sits and how big it is (-1: the widget's default). */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} place_t;

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

/* The widgets the rounds change. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    uint8_t series_x;
    uint8_t series_y;
    bento_ui_id_t seg;
    bento_ui_id_t stable_label;
    bento_ui_id_t bar;
    bento_ui_id_t status_label;
    bento_ui_id_t detail_label;
} iron_screen_t;

/* What the rounds remember from one round to the next. */
typedef struct {
    bento_real_t prev_x;
    bento_real_t prev_y;
    bool has_prev;
    int32_t stable;
    bool done;
} progress_t;

#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

/* abs() of a float, as MicroPython's abs(). */
static bento_real_t real_abs(bento_real_t value)
{
    return (value < (bento_real_t)0) ? -value : value;
}

/* time.sleep_ms(): BENTO_E_CANCELLED when Stop arrives, as the
 * original's KeyboardInterrupt. */
static bento_status_t pause_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static void place_args(bento_ui_widget_args_t *args, place_t place)
{
    bento_ui_widget_args_init(args);
    args->x = place.x;
    args->y = place.y;
    args->w = place.w;
    args->h = place.h;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    place_args(&args, (place_t){look.x, look.y, -1, -1});
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

/* A ui.poll() whose events the original throws away. */
static void poll_drop(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL); /* the events are not read, as before */
}

/* The chart and its red second series; series 0 is the blue line. */
static bento_status_t add_chart(iron_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    place_args(&args, (place_t){LEFT_X, CHART_Y, CHART_W, CHART_H});
    args.min_val = CHART_MIN_UT;
    args.max_val = CHART_MAX_UT;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    screen->series_x = 0U;
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            screen->ui, screen->chart, COLOR_RED, &screen->series_y);
    }
    return status;
}

static bento_status_t add_legend(bento_ui_t *ui)
{
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        ui, "ฟ้า = offset_x (uT)",
        (label_look_t){RIGHT_X, LEGEND_X_Y, COLOR_BLUE, SMALL_PX},
        &unused);

    if (status == BENTO_OK) {
        status = add_label(
            ui, "แดง = offset_y (uT)",
            (label_look_t){RIGHT_X, LEGEND_Y_Y, COLOR_RED, SMALL_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "เส้นเดียว = ค่าเท่ากัน",
            (label_look_t){RIGHT_X, LEGEND_SAME_Y, COLOR_DIM, SMALL_PX},
            &unused);
    }
    return status;
}

/* The movement on a Seg7, then the still counter next to its bar: the
 * number says the value, the bar says how far there is to go. */
static bento_status_t add_progress(iron_screen_t *screen)
{
    bento_ui_widget_args_t args;
    char text[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        screen->ui, "ขยับรอบนี้ (uT)",
        (label_look_t){RIGHT_X, MOVE_LABEL_Y, COLOR_DEFAULT, SMALL_PX},
        &unused);

    if (status == BENTO_OK) {
        place_args(&args, (place_t){RIGHT_X, SEG_Y, SEG_W, SEG_H});
        status = bento_ui_seg7_create(screen->ui, &args, &screen->seg);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "นิ่งติดกัน 0/%d",
                       SETTLED_ROUNDS);
        status = add_label(
            screen->ui, text,
            (label_look_t){RIGHT_X, STABLE_Y, COLOR_DEFAULT, SMALL_PX},
            &screen->stable_label);
    }
    if (status == BENTO_OK) {
        place_args(&args, (place_t){RIGHT_X, BAR_Y, BAR_W, BAR_H});
        args.min_val = 0;
        args.max_val = SETTLED_ROUNDS;
        status = bento_ui_bar_create(screen->ui, &args, &screen->bar);
    }
    return status;
}

/* The card under the chart with the instruction and the detail line. */
static bento_status_t add_card(iron_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    place_args(&args, (place_t){LEFT_X, CARD_Y, CHART_W, CARD_H});
    status = bento_ui_panel_create(screen->ui, &args, &unused);
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "หมุนบอร์ดเป็นเลขแปดช้า ๆ",
                           (label_look_t){CARD_TEXT_X, STATUS_Y,
                                          COLOR_DEFAULT, BIG_PX},
                           &screen->status_label);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "valid = ?",
                           (label_look_t){CARD_TEXT_X, DETAIL_Y,
                                          COLOR_DEFAULT, SMALL_PX},
                           &screen->detail_label);
    }
    return status;
}

static bento_status_t build_screen(iron_screen_t *screen)
{
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = add_label(
        screen->ui, "คาลิเบรตเข็มทิศ - ดูตัวเลขลู่เข้า",
        (label_look_t){LEFT_X, TITLE_Y, COLOR_DEFAULT, BIG_PX},
        &unused);
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_legend(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_progress(screen);
    }
    if (status == BENTO_OK) {
        status = add_card(screen);
    }
    if (status == BENTO_OK) {
        poll_drop(screen->ui);
    }
    return status;
}

static bento_status_t
read_calibration(bento_sensors_bmm350_calibration_t *calibration)
{
    bento_sensors_bmm350_cal_init(calibration);
    return bento_sensors_bmm350_cal_status(calibration);
}

static const char *bool_text(uint8_t value)
{
    return (value != 0U) ? "True" : "False";
}

/* str(round(value, 1)): nearbyint(value * 10) / 10 in the board's float
 * width, as MicroPython's round() computes it. */
static void format_round1(char *text, uint32_t capacity,
                          bento_real_t value)
{
    const bento_real_t tenths = (bento_real_t)10.0;
    const bento_real_t scaled = value * tenths;
    const bento_real_t rounded =
        (bento_real_t)nearbyint((double)scaled) / tenths;
    uint32_t length = 0U;

    (void)bento_fmt_real(text, capacity, rounded, &length);
}

/* "%.1f" % value */
static void format_tenths(char *text, uint32_t capacity,
                          bento_real_t value)
{
    uint32_t length = 0U;

    (void)bento_fmt_real_precision(text, capacity, value, 1, &length);
}

/* lcd.print(label, round(ox, 1), "oy", round(oy, 1)): the arguments
 * joined by one space, as the binding joins them. lcd.print's refusal
 * is ignored in MicroPython (modlcd.c). */
static void log_offsets(const char *label,
                        const bento_sensors_bmm350_calibration_t *now)
{
    char x_text[BENTO_FMT_BUF_SIZE];
    char y_text[BENTO_FMT_BUF_SIZE];
    char line[TEXT_CHARS];

    format_round1(x_text, (uint32_t)sizeof(x_text),
                  (bento_real_t)now->offset_x);
    format_round1(y_text, (uint32_t)sizeof(y_text),
                  (bento_real_t)now->offset_y);
    (void)snprintf(line, sizeof(line), "%s ox %s oy %s", label, x_text,
                   y_text);
    (void)bento_lcd_print(line);
}

/* The offsets before the reset, then the reset itself. */
static bento_status_t reset_calibration(void)
{
    bento_sensors_bmm350_calibration_t before;
    bento_status_t status;

    (void)bento_lcd_clear(); /* lcd calls ignore a refusal (modlcd.c) */
    (void)bento_lcd_console("<h2>คาลิเบรต hard-iron</h2>");
    status = read_calibration(&before);
    if (status == BENTO_OK) {
        log_offsets("ก่อนล้าง:", &before);
        status = bento_sensors_bmm350_cal_reset();
    }
    if (status == BENTO_OK) {
        (void)printf(
            "BMM350: calibration reset. Rotate board 360 deg.\n");
        (void)bento_lcd_print("ล้างแล้ว - เริ่มหมุนเลขแปดได้เลย");
    }
    return status;
}

/* Five quicker reads between rounds: the same value, but the chart's
 * lines get a shape to look at. */
static bento_status_t draw_sub_reads(const iron_screen_t *screen)
{
    bento_sensors_bmm350_calibration_t now;
    bento_status_t status = BENTO_OK;
    int32_t read;

    for (read = 0; (read < SUB_READS) && (status == BENTO_OK); read++) {
        status = read_calibration(&now);
        if (status == BENTO_OK) {
            (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                          screen->series_x,
                                          (int32_t)now.offset_x);
            (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                          screen->series_y,
                                          (int32_t)now.offset_y);
            poll_drop(screen->ui);
            status = pause_ms(SUB_READ_MS);
        }
    }
    return status;
}

/* How far both offsets moved since the last round (999 on the first),
 * and the still counter: it starts again at a move. */
static bento_real_t count_stillness(progress_t *progress,
                                    bento_real_t ox, bento_real_t oy)
{
    const bento_real_t first_move = (bento_real_t)999.0;
    const bento_real_t settled_ut = (bento_real_t)1.5;
    bento_real_t move = first_move;

    if (progress->has_prev) {
        const bento_real_t move_x = real_abs(ox - progress->prev_x);
        const bento_real_t move_y = real_abs(oy - progress->prev_y);

        move = move_x + move_y;
    }
    if (move < settled_ut) {
        progress->stable++;
    } else {
        progress->stable = 0;
    }
    return move;
}

static void show_round(const iron_screen_t *screen, int32_t round,
                       bento_real_t move, int32_t stable, uint8_t valid)
{
    char text[TEXT_CHARS];

    if (move < (bento_real_t)SEG_LIMIT_UT) {
        format_tenths(text, (uint32_t)sizeof(text), move);
        set_text(screen->ui, screen->seg, text);
    } else {
        set_text(screen->ui, screen->seg, "--");
    }
    (void)bento_ui_set_value(screen->ui, screen->bar, stable);
    (void)snprintf(text, sizeof(text), "นิ่งติดกัน %" PRId32 "/%d", stable,
                   SETTLED_ROUNDS);
    set_text(screen->ui, screen->stable_label, text);
    (void)snprintf(text, sizeof(text), "รอบ %" PRId32 " | valid = %s",
                   round, bool_text(valid));
    set_text(screen->ui, screen->detail_label, text);
    poll_drop(screen->ui);
}

static void log_round(int32_t round, bento_real_t ox, bento_real_t oy,
                      bento_real_t move)
{
    char move_text[BENTO_FMT_BUF_SIZE];
    char line[TEXT_CHARS];

    format_round1(move_text, (uint32_t)sizeof(move_text), move);
    (void)snprintf(line, sizeof(line),
                   "%" PRId32 ": ox %" PRId32 " oy %" PRId32 " ขยับ %s",
                   round, (int32_t)ox, (int32_t)oy, move_text);
    (void)bento_lcd_print(line);
}

static void show_settled(const iron_screen_t *screen, int32_t round)
{
    char text[TEXT_CHARS];

    (void)snprintf(text, sizeof(text), "นิ่งแล้วที่รอบ %" PRId32, round);
    set_text(screen->ui, screen->status_label, text);
    (void)bento_ui_set_color(screen->ui, screen->status_label,
                             COLOR_OK);
    poll_drop(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ค่าชดเชยนิ่งแล้วที่รอบ %" PRId32 "</span>",
                   round);
    (void)bento_lcd_print(text);
}

/* One round: the sub-reads, one more read, the movement, the screen and
 * the log; it settles after SETTLED_ROUNDS still rounds with valid. */
static bento_status_t run_round(const iron_screen_t *screen,
                                progress_t *progress, int32_t round)
{
    bento_sensors_bmm350_calibration_t now;
    bento_status_t status = draw_sub_reads(screen);

    if (status == BENTO_OK) {
        status = read_calibration(&now);
    }
    if (status == BENTO_OK) {
        const bento_real_t ox = (bento_real_t)now.offset_x;
        const bento_real_t oy = (bento_real_t)now.offset_y;
        const bento_real_t move = count_stillness(progress, ox, oy);

        show_round(screen, round, move, progress->stable, now.valid);
        if ((round % LOG_EVERY) == 0) {
            log_round(round, ox, oy, move);
        }
        progress->done =
            (progress->stable >= SETTLED_ROUNDS) && (now.valid != 0U);
        if (progress->done) {
            show_settled(screen, round);
        }
        progress->prev_x = ox;
        progress->prev_y = oy;
        progress->has_prev = true;
    }
    return status;
}

static bento_status_t run_rounds(const iron_screen_t *screen,
                                 progress_t *progress)
{
    bento_status_t status = BENTO_OK;
    int32_t round;

    for (round = 0;
         (round < ROUNDS) && (status == BENTO_OK) && !progress->done;
         round++) {
        status = run_round(screen, progress, round);
    }
    return status;
}

/* The result on the card and the LCD, then the heading. */
static bento_status_t show_result(const iron_screen_t *screen,
                                  bool done)
{
    bento_sensors_bmm350_calibration_t after;
    char x_text[BENTO_FMT_BUF_SIZE];
    char y_text[BENTO_FMT_BUF_SIZE];
    char text[TEXT_CHARS];
    float heading_deg = 0.0F;
    bento_status_t status = read_calibration(&after);

    if ((status == BENTO_OK) && !done) {
        (void)snprintf(text, sizeof(text), "ยังไม่นิ่งภายใน %d รอบ",
                       ROUNDS);
        set_text(screen->ui, screen->status_label, text);
        (void)bento_ui_set_color(screen->ui, screen->status_label,
                                 COLOR_WARN);
    }
    if (status == BENTO_OK) {
        format_tenths(x_text, (uint32_t)sizeof(x_text),
                      (bento_real_t)after.offset_x);
        format_tenths(y_text, (uint32_t)sizeof(y_text),
                      (bento_real_t)after.offset_y);
        (void)snprintf(text, sizeof(text), "ox %s oy %s | valid = %s",
                       x_text, y_text, bool_text(after.valid));
        set_text(screen->ui, screen->detail_label, text);
        poll_drop(screen->ui);
        log_offsets("หลังคาลิเบรต:", &after);
        status = bento_sensors_bmm350_heading(&heading_deg);
    }
    if (status == BENTO_OK) {
        (void)snprintf(
            text, sizeof(text), "valid = %s | heading = %" PRId32,
            bool_text(after.valid), (int32_t)(bento_real_t)heading_deg);
        (void)bento_lcd_print(text);
    }
    return status;
}

int main(void)
{
    iron_screen_t screen;
    progress_t progress = {(bento_real_t)0, (bento_real_t)0, false, 0,
                           false};
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        status = reset_calibration();
    }
    if (status == BENTO_OK) {
        status = run_rounds(&screen, &progress);
    }
    if (status == BENTO_OK) {
        status = show_result(&screen, progress.done);
    }
    if (status == BENTO_OK) {
        (void)printf("จดค่า offset ไว้ ถ้าย้ายบอร์ดไปติดบนโครงเหล็ก "
                     "ต้องคาลิเบรตใหม่ทั้งหมด\n");
        (void)printf("ขั้นถัดไป เอาค่าไปใช้ที่ "
                     "examples/s08/06_compass_readout.py\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
