/*
 * 06_compass_readout.c -- เข็มทิศที่ใช้งานได้จริง พร้อมตัวเลของศา (คาบ 08)
 *
 * อ่านทิศจาก BMM350 ทุก 100 ms 900 รอบ เข็มทิศทางซ้าย ตัวเลของศาตัวใหญ่ ชื่อทิศ
 * และค่าดิบสามแกน ทางขวา กับกราฟทิศตามเวลา 0-360 องศา
 * จออัปเดตเฉพาะตอนทิศขยับเกิน 1 องศา ส่วนกราฟกินทุกรอบ รอบแรก ๆ
 * หลังรีเซ็ตที่อ่านทิศยังไม่ได้ โปรแกรมบอกแล้วลองใหม่รอบหน้า จบแล้วสรุปลง console
 * หมุนบอร์ดช้า ๆ บนโต๊ะ วางนิ่งแล้วกราฟต้องเป็นเส้นตรง บอร์ด: Dev Kit และ Eva Kit
 *
 * Reads the BMM350's heading every 100 ms for 900 rounds: a compass on
 * the left, the degrees in big digits, the direction's name and the
 * three raw axes on the right, and a chart of the heading over time,
 * 0-360 degrees. The screen changes only when the heading moves by 1
 * degree or more; the chart takes every round. A round whose heading
 * cannot be read yet (the first ones after a reset) says so and tries
 * again. At the end it sums up on the console. Turn the board slowly on
 * the table; lying still, the chart must be a straight line. Boards:
 * Dev Kit and Eva Kit.
 *
 * Twin of 06_compass_readout.py
 * (embedded-systems-for-aiot-developer/examples/s08/06_compass_readout.py),
 * started from the library twin aiot-s08-06-compass-readout. Style:
 * CLEAN_C.md.
 * What C changes only because the language forces it: Python's int is
 * unbounded, here it is int32_t; a float is bento_real_t, the width
 * each board's MicroPython uses; the original's except OSError around
 * the heading is any failed heading read here; Python's % on a float is
 * fmod() with the sign of the divisor.
 */
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* The directions, the run and text sizes. */
enum {
    NAME_COUNT = 8,
    AXES = 3,        /* x, y and z */
    SECTOR_DEG = 45, /* one name per 45 degrees */
    FULL_TURN = 360,
    ROUNDS = 900,  /* rounds to read */
    LOOP_MS = 100, /* one round */
    /* the longest line built here, with room for its numbers */
    STATUS_BYTES = 80
};

/* Screen layout on the 792 x 398 canvas: the compass and the chart on
 * the left, the numbers on the right, the status line at the bottom. */
enum {
    TITLE_X = 12,
    TITLE_Y = 8,
    COMPASS_X = 12,
    COMPASS_Y = 44,
    COMPASS_W = 200,
    CHART_X = 232,
    CHART_Y = 44,
    CHART_W = 252,
    CHART_H = 200,
    CHART_MAX = 360,
    CHART_NOTE_Y = 250,
    RIGHT_X = 500,
    DEGREES_LABEL_Y = 42,
    DEGREES_Y = 68,
    DEGREES_W = 180,
    DEGREES_H = 44,
    DIRECTION_Y = 120,
    RAW_Y = 164,
    TURN_Y = 190,
    STATUS_X = 24,
    STATUS_Y = 296
};

/* Font sizes, in pixels, and colours, 0xRRGGBB. */
enum {
    FONT_TITLE = 24,
    FONT_TEXT = 20,
    FONT_DIRECTION = 28,
    FONT_RAW = 16,
    COLOR_ACCENT = 0x4A9EFF
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t compass;
    bento_ui_id_t chart;
    bento_ui_id_t degrees;
    bento_ui_id_t direction;
    bento_ui_id_t raw;
    bento_ui_id_t status;
} compass_screen_t;

/* What the loop remembers from one round to the next. */
typedef struct {
    bento_real_t last_deg; /* the heading the screen last showed */
    int32_t moved;         /* how often the screen followed */
} readout_t;

/* Python's % on floats: the remainder takes the sign of the divisor. */
static bento_real_t float_modulo(bento_real_t dividend,
                                 bento_real_t divisor)
{
    bento_real_t rest = (bento_real_t)fmod(dividend, divisor);

    if ((rest != (bento_real_t)0) &&
        ((rest < (bento_real_t)0) != (divisor < (bento_real_t)0))) {
        rest += divisor;
    }
    return rest;
}

/* The direction's name: 45 degrees per name, shifted by 22.5 so that
 * each boundary lies halfway between two names. A heading that gives no
 * name (not a number) is refused, as int() and the index refuse it in
 * the original. */
static bento_status_t cardinal(bento_real_t deg, const char **name)
{
    static const char *const names[NAME_COUNT] = {"N", "NE", "E", "SE",
                                                  "S", "SW", "W", "NW"};
    const bento_real_t half_sector = (bento_real_t)22.5;
    const bento_real_t shifted = deg + half_sector;
    const bento_real_t wrapped =
        float_modulo(shifted, (bento_real_t)FULL_TURN);
    const bento_real_t sector = wrapped / (bento_real_t)SECTOR_DEG;
    bento_status_t status = BENTO_E_INVALID_ARGUMENT;

    if ((sector >= (bento_real_t)0) &&
        (sector < (bento_real_t)NAME_COUNT)) {
        *name = names[(int32_t)sector];
        status = BENTO_OK;
    }
    return status;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t x, int16_t y, int32_t font_px,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* The compass, the heading chart and the chart's note in the accent
 * colour. */
static bento_status_t add_dial_and_chart(compass_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t note;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = COMPASS_X;
    args.y = COMPASS_Y;
    args.w = COMPASS_W;
    args.font_px = 0;
    status =
        bento_ui_compass_create(screen->ui, &args, &screen->compass);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = CHART_X;
        args.y = CHART_Y;
        args.w = CHART_W;
        args.h = CHART_H;
        args.min_val = 0;
        args.max_val = CHART_MAX;
        status =
            bento_ui_chart_create(screen->ui, &args, &screen->chart);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "ฟ้า = ทิศตามเวลา 0-360 องศา";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = CHART_X;
        args.y = CHART_NOTE_Y;
        args.font_px = FONT_TEXT;
        args.color = COLOR_ACCENT;
        status = bento_ui_label_create(screen->ui, &args, &note);
    }
    return status;
}

/* The right column: the degrees, the direction, the raw axes and the
 * hint. */
static bento_status_t add_numbers(compass_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    bento_ui_id_t unused;
    bento_status_t status = add_label(
        ui, "องศา", RIGHT_X, DEGREES_LABEL_Y, FONT_TEXT, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_t args;

        bento_ui_widget_args_init(&args);
        args.x = RIGHT_X;
        args.y = DEGREES_Y;
        args.w = DEGREES_W;
        args.h = DEGREES_H;
        status = bento_ui_seg7_create(ui, &args, &screen->degrees);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "--", RIGHT_X, DIRECTION_Y,
                           FONT_DIRECTION, &screen->direction);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "mx my mz", RIGHT_X, RAW_Y, FONT_RAW,
                           &screen->raw);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "หมุนบอร์ดช้า ๆ", RIGHT_X, TURN_Y,
                           FONT_TEXT, &unused);
    }
    return status;
}

/* ui.poll() is called every round, as the original does; the events are
 * not used, and it answers no events when it fails, so its status is
 * not an error here. */
static void skip_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(compass_screen_t *screen)
{
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "เข็มทิศ BMM350", TITLE_X, TITLE_Y,
                           FONT_TITLE, &unused);
    }
    if (status == BENTO_OK) {
        status = add_dial_and_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_numbers(screen);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "อัปเดตจอเฉพาะตอนขยับเกิน 1 องศา",
                      STATUS_X, STATUS_Y, FONT_TEXT, &screen->status);
    }
    if (status == BENTO_OK) {
        skip_events(screen->ui);
    }
    return status;
}

/* The heading moved by a degree or more: every number on the screen
 * follows. MicroPython's widget setters ignore a refusal (modui.c), so
 * the twin does too. */
static bento_status_t show_heading(const compass_screen_t *screen,
                                   readout_t *readout, bento_real_t deg)
{
    char text[STATUS_BYTES];
    float magnetic[AXES] = {0.0F, 0.0F, 0.0F};
    const char *name = "";
    bento_status_t status;

    (void)bento_ui_set_value(screen->ui, screen->compass, (int32_t)deg);
    (void)snprintf(text, sizeof(text), "%" PRId32, (int32_t)deg);
    (void)bento_ui_set_text(screen->ui, screen->degrees, text,
                            (uint32_t)strlen(text));
    status = cardinal(deg, &name);
    if (status == BENTO_OK) {
        (void)bento_ui_set_text(screen->ui, screen->direction, name,
                                (uint32_t)strlen(name));
        status = bento_sensors_bmm350_magnetic(
            &magnetic[0], &magnetic[1], &magnetic[2]);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "%" PRId32 " %" PRId32 " %" PRId32,
                       (int32_t)magnetic[0], (int32_t)magnetic[1],
                       (int32_t)magnetic[2]);
        (void)bento_ui_set_text(screen->ui, screen->raw, text,
                                (uint32_t)strlen(text));
        readout->last_deg = deg;
        readout->moved++;
        (void)snprintf(text, sizeof(text), "อัปเดตไปแล้ว %" PRId32 " ครั้ง",
                       readout->moved);
        (void)bento_ui_set_text(screen->ui, screen->status, text,
                                (uint32_t)strlen(text));
    }
    return status;
}

/* One round: the heading into the chart, and onto the screen when it
 * moved by a degree; a heading that cannot be read yet is said on the
 * status line and tried again next round. */
static bento_status_t run_round(const compass_screen_t *screen,
                                readout_t *readout)
{
    float heading = 0.0F;
    bento_status_t status = BENTO_OK;

    if (bento_sensors_bmm350_heading(&heading) != BENTO_OK) {
        static const char not_yet[] = "อ่านเข็มทิศยังไม่ได้ - ลองใหม่";

        (void)bento_ui_set_text(screen->ui, screen->status, not_yet,
                                (uint32_t)strlen(not_yet));
    } else {
        const bento_real_t deg = (bento_real_t)heading;
        const bento_real_t change = deg - readout->last_deg;
        const bento_real_t distance =
            (change < (bento_real_t)0) ? -change : change;

        (void)bento_ui_chart_set_next(screen->ui, screen->chart, 0U,
                                      (int32_t)deg);
        if (distance >= (bento_real_t)1) {
            status = show_heading(screen, readout, deg);
        }
    }
    if (status == BENTO_OK) {
        skip_events(screen->ui);
        status = bento_time_sleep_ms(LOOP_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* ROUNDS rounds, or until Stop is pressed or the magnetometer fails. */
static bento_status_t run_rounds(const compass_screen_t *screen,
                                 readout_t *readout)
{
    bento_status_t status = BENTO_OK;
    int32_t round;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = run_round(screen, readout);
    }
    return status;
}

/* lcd.clear(), lcd.console() and lcd.print() are sent without an
 * answer (modlcd.c). */
static void open_console(void)
{
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>เข็มทิศ BMM350</h2>");
    (void)bento_lcd_print("เข็มทิศทำงาน หมุนบอร์ดช้า ๆ");
}

int main(void)
{
    compass_screen_t screen = {
        bento_runtime_get_ui(), 0U, 0U, 0U, 0U, 0U, 0U};
    readout_t readout = {(bento_real_t)-1, 0};
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        open_console();
        status = run_rounds(&screen, &readout);
    }
    if (status == BENTO_OK) {
        char text[STATUS_BYTES];

        (void)snprintf(text, sizeof(text),
                       "อัปเดตจอทั้งหมด %" PRId32 " ครั้ง", readout.moved);
        /* sent without an answer (modlcd.c) */
        (void)bento_lcd_print(text);
        (void)bento_lcd_print(
            "<span class=muted>เส้นสั่นมาก = ต้องคาลิเบรตก่อน</span>");
        (void)printf("ถ้าเข็มสั่นหรือนิ่งผิดปกติ ให้คาลิเบรต "
                     "hard-iron ก่อน\n");
        (void)printf("ไฟล์คาลิเบรตอยู่ที่ "
                     "examples/usecase/14_hard_iron_calibration.py\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
