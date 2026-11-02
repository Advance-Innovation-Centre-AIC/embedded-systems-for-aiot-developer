/*
 * s08_dashboard.c -- Mini-HMI แดชบอร์ด 4 การ์ด บน Eva Kit / Dev Kit
 * (A four-card mini HMI dashboard)
 *
 * สี่การ์ดคือค่าที่วัดได้: ความเร่ง BMI270 เป็นกราฟสามเส้น เข็มทิศ BMM350 แถบสัมผัส
 * CapSense และลูกบิด พร้อมเกณฑ์เตือนที่ตั้งเองด้วย Spinbox แถบหัวมีชื่อทีม ไฟ "ค่าค้าง"
 * ปุ่ม "เดินหน้า" กับ "หยุดภาพ" และเลขรอบที่บอกว่าลูปยังเดิน ค่าที่อ่านไม่ได้ไม่ถูกแทน
 * ด้วยศูนย์ แต่คงค่าล่าสุดไว้แล้วจุดไฟค่าค้าง แก้ชื่อทีมที่ team ใน add_head แล้วปล่อยรันยาว 10 นาที
 *
 * Four cards show what is measured: BMI270 acceleration as a
 * three-line chart, the BMM350 compass, the CapSense touch strip and
 * the potentiometer with an alarm threshold set on a spinbox. The head
 * bar holds the team name, a "stale" lamp, run and hold buttons and a
 * round counter that proves the loop is alive. A value that cannot be
 * read is never replaced by zero: the last one stays and the stale lamp
 * lights. Set the team name in team (add_head) and let it run for 10 minutes; Stop
 * clears the screen and prints the number of rounds.
 *
 * Twin of s08_dashboard.py
 * (embedded-systems-for-aiot-developer/solution_codes/s08_dashboard.py).
 * Style: CLEAN_C.md.
 * Differs from the original only where C must:
 *   - every failure of a sensor call is the original's OSError;
 *   - "{:+.1f}", "{:03.0f}", "{:.1f}" and "{:.3f}" are
 *     bento_fmt_real_precision() with the sign or the zero padding added;
 *   - heading % 360.0 is fmod() moved into 0..360 as Python's % does;
 *   - except KeyboardInterrupt is bento_runtime_acknowledge_stop() after
 *     the loop ends with BENTO_E_CANCELLED.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_time.h"

/* colours, 0xRRGGBB, the firmware dashboard's palette: one colour per
 * sensor, and warning colours kept for warnings only */
enum {
    COLOR_CARD = 0x171B22,
    COLOR_WHITE = 0xE8EAED,
    COLOR_GRAY = 0x9AA3AF,
    COLOR_IMU = 0x8E7BFF,
    COLOR_COMPASS = 0x2FB6A8,
    COLOR_TOUCH = 0xC77DFF,
    COLOR_POT = 0x4A9EFF,
    COLOR_NORMAL = 0x30A46C,
    COLOR_LINE_Z = 0x448AFF,
    COLOR_WARN = 0xF5A623,
    COLOR_ALERT = 0xE5484D,
    COLOR_HOLD = 0x3A4150
};

/* timing and limits */
enum {
    UI_TEXT_MS =
        1000, /* a number a person reads changes once a second at most */
    STALE_MS = 3000, /* unread longer than this: the numbers are old */
    LOOP_MS = 200, /* proven to run a heavy dashboard for a long time */
    SCREEN_SETTLE_MS = 200,
    CHART_SCALE =
        10, /* the chart takes whole numbers: tenths of m/s2 */
    CHART_LIMIT = 150,
    PERCENT_MAX = 100,
    THRESHOLD_START = 80,
    CARD_RADIUS = 12,
    CARD_EDGE_PX = 2,
    DIRECTION_COUNT = 8,
    AXIS_COUNT = 3,  /* X, Y and Z: one chart line each */
    VOLT_DIGITS = 3, /* "{:.3f} V" */
    HEADING_PAD = 3, /* "{:03.0f}" */
    TITLE_X = 24,
    TITLE_Y = 8,
    FONT_HEAD = 24,
    FONT_BIG = 28,
    FONT_BODY = 20,
    NUMBER_BYTES = 40,
    TEXT_BYTES = 96
};

/* Every widget, in the order the original creates it (32 of the 64 the
 * firmware allows). */
typedef enum {
    W_TITLE,
    W_STALE_LAMP,
    W_STALE_NAME,
    W_RUN,
    W_HOLD,
    W_HEAD,
    W_IMU_CARD,
    W_IMU_TITLE,
    W_IMU_CHART,
    W_IMU_VALUE,
    W_COMPASS_CARD,
    W_COMPASS_TITLE,
    W_COMPASS,
    W_COMPASS_DEGREES,
    W_COMPASS_DIRECTION,
    W_TOUCH_CARD,
    W_TOUCH_TITLE,
    W_TOUCH_LAMP0,
    W_TOUCH_NAME0,
    W_TOUCH_LAMP1,
    W_TOUCH_NAME1,
    W_TOUCH_PERCENT,
    W_TOUCH_BAR,
    W_POT_CARD,
    W_POT_TITLE,
    W_POT_ARC,
    W_POT_SEG7,
    W_POT_VOLTS,
    W_THRESHOLD_NAME,
    W_THRESHOLD,
    W_ALARM_NAME,
    W_ALARM_LAMP,
    W_COUNT
} widget_index_t;

/* One widget as ui.<Factory>(...) makes it. */
typedef struct {
    uint8_t type;
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value;
    int32_t min;
    int32_t max;
} widget_spec_t;

/* The last values really read: never a zero that pretends to be one. */
typedef struct {
    bento_real_t accel[AXIS_COUNT];
    bento_real_t heading;
    bento_real_t percent;
    bento_real_t volts;
    uint8_t touch0;
    uint8_t touch1;
    uint8_t slider;
} readings_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t widgets[W_COUNT];
    uint8_t
        series[AXIS_COUNT]; /* series 0 comes with the chart itself */
    readings_t readings;
    int32_t rounds;
    int32_t threshold; /* the spinbox changes it, nothing else */
    uint32_t last_text_ms;
    uint32_t good_ms;
    uint32_t round_start_ms;
    bool running; /* only the buttons change it */
    bool stale_shown;
    bool alarm_shown;
} dashboard_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* A setter's refusal was ignored before too (modui.c). */
static void set_text(const dashboard_t *board, widget_index_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(board->ui, board->widgets[widget], text,
                            (uint32_t)strlen(text));
}

static void set_value(const dashboard_t *board, widget_index_t widget,
                      int32_t value)
{
    (void)bento_ui_set_value(board->ui, board->widgets[widget], value);
}

/* The chart's Y and Z lines, right after the chart is made, as in the
 * original. */
static bento_status_t add_chart_lines(dashboard_t *board,
                                      bento_ui_id_t chart)
{
    bento_status_t status = bento_ui_chart_add_series(
        board->ui, chart, COLOR_NORMAL, &board->series[1]);

    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            board->ui, chart, COLOR_LINE_Z, &board->series[2]);
    }
    return status;
}

/* One widget as ui.<Factory>(...) makes it. */
static bento_status_t add_widget(dashboard_t *board,
                                 const widget_spec_t *spec,
                                 bento_ui_id_t *widget)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = spec->text;
    args.text_len =
        (spec->text != NULL) ? (uint32_t)strlen(spec->text) : 0U;
    args.x = spec->x;
    args.y = spec->y;
    args.w = spec->w;
    args.h = spec->h;
    args.color = spec->color;
    args.font_px = spec->value;
    args.min_val = spec->min;
    args.max_val = spec->max;
    return bento_ui_create(board->ui, spec->type, &args, widget);
}

/* Creates specs[0..count) into widgets[first..]. */
static bento_status_t add_widgets(dashboard_t *board,
                                  const widget_spec_t *specs,
                                  int32_t first, int32_t count)
{
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < count) && (status == BENTO_OK); i++) {
        bento_ui_id_t *widget = &board->widgets[first + i];

        status = add_widget(board, &specs[i], widget);
        if ((status == BENTO_OK) &&
            (specs[i].type == (uint8_t)UI_WIDGET_CHART)) {
            status = add_chart_lines(board, *widget);
        }
    }
    return status;
}

/* The head bar: the team, the stale lamp, the two command buttons and
 * the round line, the dashboard's heartbeat. */
static bento_status_t add_head(dashboard_t *board)
{
    static const char team[] =
        "BentoBuilders"; /* change it to your team before you run */
    static const widget_spec_t specs[W_IMU_CARD - W_STALE_LAMP] = {
        {UI_WIDGET_LED, NULL, 400, 12, 48, 48, COLOR_WARN, 0, 0, 0},
        {UI_WIDGET_LABEL, "ค่าค้าง", 400, 64, -1, -1, COLOR_GRAY,
         FONT_BODY, 0, 0},
        {UI_WIDGET_BUTTON, "เดินหน้า", 480, 4, 120, 88, COLOR_NORMAL,
         FONT_BODY, 0, 0},
        {UI_WIDGET_BUTTON, "หยุดภาพ", 632, 4, 120, 88, COLOR_HOLD,
         FONT_BODY, 0, 0},
        {UI_WIDGET_LABEL, "รอบที่ 0 | loop 0 ms", 24, 52, -1, -1,
         COLOR_GRAY, FONT_BODY, 0, 0}};
    bento_ui_widget_args_t args;
    char text[TEXT_BYTES];
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "แดชบอร์ดทีม %s", team);
    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = TITLE_X;
    args.y = TITLE_Y;
    args.color = COLOR_WHITE;
    args.font_px = FONT_HEAD;
    status = bento_ui_label_create(board->ui, &args,
                                   &board->widgets[W_TITLE]);
    if (status == BENTO_OK) {
        status = add_widgets(board, specs, W_STALE_LAMP,
                             W_IMU_CARD - W_STALE_LAMP);
    }
    return status;
}

/* The top row: IMU (left) and compass (right). A Panel is a card:
 * color = fill, min = edge colour, max = corner radius, value = edge
 * width. The Panel comes first, so the rest is drawn on top of it. */
static bento_status_t add_top_cards(dashboard_t *board)
{
    static const widget_spec_t specs[W_TOUCH_CARD - W_IMU_CARD] = {
        {UI_WIDGET_PANEL, NULL, 24, 100, 368, 136, COLOR_CARD,
         CARD_EDGE_PX, COLOR_IMU, CARD_RADIUS},
        {UI_WIDGET_LABEL, "ความเร่ง BMI270 (m/s2)", 40, 108, -1, -1,
         COLOR_IMU, FONT_BODY, 0, 0},
        {UI_WIDGET_CHART, NULL, 40, 136, 336, 56, COLOR_IMU, 0,
         -CHART_LIMIT, CHART_LIMIT},
        {UI_WIDGET_LABEL, "X+0.0 Y+0.0 Z+9.8", 40, 200, -1, -1,
         COLOR_WHITE, FONT_BODY, 0, 0},
        {UI_WIDGET_PANEL, NULL, 408, 100, 360, 136, COLOR_CARD,
         CARD_EDGE_PX, COLOR_COMPASS, CARD_RADIUS},
        {UI_WIDGET_LABEL, "เข็มทิศ BMM350", 424, 108, -1, -1,
         COLOR_COMPASS, FONT_BODY, 0, 0},
        {UI_WIDGET_COMPASS, NULL, 424, 132, 96, 96, COLOR_COMPASS, 0, 0,
         0},
        {UI_WIDGET_LABEL, "000 deg", 544, 140, -1, -1, COLOR_WHITE,
         FONT_BIG, 0, 0},
        {UI_WIDGET_LABEL, "N", 544, 188, -1, -1, COLOR_COMPASS,
         FONT_HEAD, 0, 0}};

    return add_widgets(board, specs, W_IMU_CARD,
                       W_TOUCH_CARD - W_IMU_CARD);
}

/* The bottom row: touch strip (left) and the knob with its alarm
 * threshold (right). The threshold is a spinbox: the person on site
 * knows the right value, not the programmer. */
static bento_status_t add_bottom_cards(dashboard_t *board)
{
    static const widget_spec_t specs[W_COUNT - W_TOUCH_CARD] = {
        {UI_WIDGET_PANEL, NULL, 24, 252, 320, 136, COLOR_CARD,
         CARD_EDGE_PX, COLOR_TOUCH, CARD_RADIUS},
        {UI_WIDGET_LABEL, "แถบสัมผัส CapSense", 40, 260, -1, -1,
         COLOR_TOUCH, FONT_BODY, 0, 0},
        {UI_WIDGET_LED, NULL, 40, 288, 48, 48, COLOR_TOUCH, 0, 0, 0},
        {UI_WIDGET_LABEL, "ปุ่ม 0", 96, 300, -1, -1, COLOR_GRAY,
         FONT_BODY, 0, 0},
        {UI_WIDGET_LED, NULL, 160, 288, 48, 48, COLOR_TOUCH, 0, 0, 0},
        {UI_WIDGET_LABEL, "ปุ่ม 1", 216, 300, -1, -1, COLOR_GRAY,
         FONT_BODY, 0, 0},
        {UI_WIDGET_LABEL, "0 %", 272, 256, -1, -1, COLOR_WHITE,
         FONT_BODY, 0, 0},
        {UI_WIDGET_BAR, NULL, 40, 348, 288, 16, COLOR_TOUCH, 0, 0,
         PERCENT_MAX},
        {UI_WIDGET_PANEL, NULL, 360, 252, 408, 136, COLOR_CARD,
         CARD_EDGE_PX, COLOR_POT, CARD_RADIUS},
        {UI_WIDGET_LABEL, "ลูกบิด Potentiometer", 376, 260, -1, -1,
         COLOR_POT, FONT_BODY, 0, 0},
        {UI_WIDGET_ARC, NULL, 376, 288, 88, 88, 0U, 0, 0, PERCENT_MAX},
        {UI_WIDGET_SEG7, NULL, 480, 288, 112, 44, COLOR_POT, 0, 0,
         PERCENT_MAX},
        {UI_WIDGET_LABEL, "0.000 V", 480, 340, -1, -1, COLOR_WHITE,
         FONT_BODY, 0, 0},
        {UI_WIDGET_LABEL, "เกณฑ์ (%)", 600, 260, -1, -1, COLOR_GRAY,
         FONT_BODY, 0, 0},
        {UI_WIDGET_SPINBOX, NULL, 600, 288, 88, 88, COLOR_WHITE,
         THRESHOLD_START, 0, PERCENT_MAX},
        {UI_WIDGET_LABEL, "เกิน", 704, 260, -1, -1, COLOR_GRAY,
         FONT_BODY, 0, 0},
        {UI_WIDGET_LED, NULL, 704, 288, 48, 48, COLOR_ALERT, 0, 0, 0}};

    return add_widgets(board, specs, W_TOUCH_CARD,
                       W_COUNT - W_TOUCH_CARD);
}

/* '%.<digits>f' as the board writes it; "" when it cannot be written. */
static void real_text(bento_real_t value, int32_t digits, char *text,
                      size_t cap)
{
    uint32_t length = 0U;

    if (bento_fmt_real_precision(text, (uint32_t)cap, value, digits,
                                 &length) != BENTO_OK) {
        text[0] = '\0';
    }
}

/* "{:+.1f}": a '+' in front of a positive number. */
static void signed_text(bento_real_t value, char *text, size_t cap)
{
    char digits[BENTO_FMT_BUF_SIZE];

    real_text(value, 1, digits, sizeof(digits));
    (void)snprintf(text, cap, "%s%s", (digits[0] == '-') ? "" : "+",
                   digits);
}

/* Python's x % 360.0: the remainder has the divisor's sign. */
static bento_real_t wrap_degrees(bento_real_t degrees)
{
    const double full_turn = 360.0;
    double wrapped = fmod((double)degrees, full_turn);

    if (wrapped < 0.0) {
        wrapped += full_turn;
    }
    return (bento_real_t)wrapped;
}

/* Six axes from one question to the display core, ten times larger on
 * the chart so it is not a staircase of 1 m/s2 steps. */
static bool read_motion(dashboard_t *board)
{
    readings_t *readings = &board->readings;
    bento_sensors_imu6_t imu;
    bool ok;
    int32_t axis;

    bento_sensors_imu6_init(&imu);
    ok = bento_sensors_bmi270_motion(&imu) == BENTO_OK;
    if (ok) {
        readings->accel[0] = (bento_real_t)imu.ax;
        readings->accel[1] = (bento_real_t)imu.ay;
        readings->accel[2] = (bento_real_t)imu.az;
    }
    for (axis = 0; axis < AXIS_COUNT; axis++) {
        const bento_real_t scaled =
            readings->accel[axis] * (bento_real_t)CHART_SCALE;

        (void)bento_ui_chart_set_next(
            board->ui, board->widgets[W_IMU_CHART], board->series[axis],
            (int32_t)scaled);
    }
    return ok;
}

/* The BMM350 sits on its own I3C bus. % 360 keeps the needle from
 * jumping back at 361. */
static bool read_compass(dashboard_t *board)
{
    float heading = 0.0F;
    const bool ok = bento_sensors_bmm350_heading(&heading) == BENTO_OK;

    if (ok) {
        board->readings.heading = wrap_degrees((bento_real_t)heading);
    }
    set_value(board, W_COMPASS, (int32_t)board->readings.heading);
    return ok;
}

/* One read gives both buttons and the slider. A dark lamp dims, it
 * does not vanish. */
static bool read_touch(dashboard_t *board)
{
    readings_t *readings = &board->readings;
    const bool ok = bento_sensors_capsense_read(
                        &readings->touch0, &readings->touch1,
                        &readings->slider) == BENTO_OK;

    if (!ok) {
        readings->touch0 = 0U;
        readings->touch1 = 0U;
        readings->slider = 0U;
    }
    set_value(board, W_TOUCH_LAMP0, (readings->touch0 != 0U) ? 1 : 0);
    set_value(board, W_TOUCH_LAMP1, (readings->touch1 != 0U) ? 1 : 0);
    set_value(board, W_TOUCH_BAR, (int32_t)readings->slider);
    return ok;
}

static bool read_knob(dashboard_t *board)
{
    float percent = 0.0F;
    float volts = 0.0F;
    bool ok = bento_sensors_pot_percent(&percent) == BENTO_OK;

    if (ok) {
        board->readings.percent = (bento_real_t)percent;
        ok = bento_sensors_pot_voltage(&volts) == BENTO_OK;
    }
    if (ok) {
        board->readings.volts = (bento_real_t)volts;
    }
    set_value(board, W_POT_ARC, (int32_t)board->readings.percent);
    return ok;
}

/* Every sensor in turn; one failed read keeps the last value and raises
 * the flag instead of blanking the dashboard. */
static void read_sensors(dashboard_t *board)
{
    const bool motion_ok = read_motion(board);
    const bool compass_ok = read_compass(board);
    const bool touch_ok = read_touch(board);
    const bool knob_ok = read_knob(board);

    if (motion_ok && compass_ok && touch_ok && knob_ok) {
        board->good_ms = bento_time_ticks_ms();
    }
}

/* The lamps are written only when they change: the display queue has a
 * bottom, and a full queue drops commands first. */
static void show_lamps(dashboard_t *board)
{
    const bool stale =
        bento_time_ticks_diff(bento_time_ticks_ms(), board->good_ms) >=
        (int32_t)STALE_MS;
    const bool alarm =
        board->readings.percent >= (bento_real_t)board->threshold;

    if (stale != board->stale_shown) {
        board->stale_shown = stale;
        set_value(board, W_STALE_LAMP, stale ? 1 : 0);
    }
    if (alarm != board->alarm_shown) {
        board->alarm_shown = alarm;
        set_value(board, W_ALARM_LAMP, alarm ? 1 : 0);
    }
}

/* The compass card's two texts: "{:03.0f} deg" and the direction. */
static void show_heading(const dashboard_t *board)
{
    static const char *const directions[DIRECTION_COUNT] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    const bento_real_t half_sector = (bento_real_t)22.5;
    const bento_real_t sector = (bento_real_t)45.0;
    const bento_real_t heading = board->readings.heading;
    const bento_real_t shifted = heading + half_sector;
    const bento_real_t sectors = shifted / sector;
    static const char zeros[] = "000";
    char digits[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    size_t length;

    real_text(heading, 0, digits, sizeof(digits));
    length = strlen(digits);
    (void)snprintf(text, sizeof(text), "%s%s deg",
                   (length < (size_t)HEADING_PAD) ? &zeros[length] : "",
                   digits);
    set_text(board, W_COMPASS_DEGREES, text);
    set_text(board, W_COMPASS_DIRECTION,
             directions[(int32_t)sectors % (int32_t)DIRECTION_COUNT]);
}

/* The numbers a person reads, once a second. A Seg7 takes text, not a
 * value: its value() is never drawn. */
static void show_numbers(dashboard_t *board)
{
    const readings_t *readings = &board->readings;
    char x_text[NUMBER_BYTES];
    char y_text[NUMBER_BYTES];
    char z_text[NUMBER_BYTES];
    char text[TEXT_BYTES];

    board->last_text_ms = bento_time_ticks_ms();
    signed_text(readings->accel[0], x_text, sizeof(x_text));
    signed_text(readings->accel[1], y_text, sizeof(y_text));
    signed_text(readings->accel[2], z_text, sizeof(z_text));
    (void)snprintf(text, sizeof(text), "X%s Y%s Z%s", x_text, y_text,
                   z_text);
    set_text(board, W_IMU_VALUE, text);
    show_heading(board);
    (void)snprintf(text, sizeof(text), "%d %%",
                   board->running ? (int)readings->slider : 0);
    set_text(board, W_TOUCH_PERCENT, text);
    real_text(readings->percent, 1, text, sizeof(text));
    set_text(board, W_POT_SEG7, text);
    real_text(readings->volts, VOLT_DIGITS, x_text, sizeof(x_text));
    (void)snprintf(text, sizeof(text), "%s V", x_text);
    set_text(board, W_POT_VOLTS, text);
    (void)snprintf(text, sizeof(text), "รอบที่ %d | loop %d ms",
                   (int)board->rounds,
                   (int)bento_time_ticks_diff(bento_time_ticks_ms(),
                                              board->round_start_ms));
    set_text(board, W_HEAD, text);
}

static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* Polled every round: the buttons are real now. Hold freezes the
 * picture, not the program; the round counter keeps moving. */
static void handle_events(dashboard_t *board)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(board->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U; /* ui.poll() answers no events when it fails */
    }
    for (i = 0U; i < count; i++) {
        if (event_is(&events[i], BENTO_UI_EVENT_CLICKED,
                     board->widgets[W_RUN])) {
            board->running = true;
        } else if (event_is(&events[i], BENTO_UI_EVENT_CLICKED,
                            board->widgets[W_HOLD])) {
            board->running = false;
        } else if (event_is(&events[i], BENTO_UI_EVENT_VALUE_CHANGED,
                            board->widgets[W_THRESHOLD])) {
            board->threshold = events[i].value;
        } else {
            /* any other event changes nothing */
        }
    }
}

/* One 200 ms round of the main loop. */
static bento_status_t run_round(dashboard_t *board)
{
    board->round_start_ms = bento_time_ticks_ms();
    if (board->running) {
        read_sensors(board);
    }
    show_lamps(board);
    if (bento_time_ticks_diff(bento_time_ticks_ms(),
                              board->last_text_ms) >=
        (int32_t)UI_TEXT_MS) {
        show_numbers(board);
    }
    board->rounds++;
    handle_events(board);
    return wait_ms(LOOP_MS);
}

/* The first read on the Eva Kit can wait up to 16 s after a reset: let
 * that wait happen before the four cards are built. */
static void warm_up_sensor(void)
{
    bento_sensors_imu6_t imu;

    bento_sensors_imu6_init(&imu);
    if (bento_sensors_bmi270_motion(&imu) != BENTO_OK) {
        (void)printf("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ในลูป\n");
    }
}

static bento_status_t build_screen(dashboard_t *board)
{
    bento_status_t status = bento_ui_screen_default(board->ui);

    if (status == BENTO_OK) {
        status = wait_ms(SCREEN_SETTLE_MS);
    }
    if (status == BENTO_OK) {
        warm_up_sensor();
        status = add_head(board);
    }
    if (status == BENTO_OK) {
        status = add_top_cards(board);
    }
    if (status == BENTO_OK) {
        status = add_bottom_cards(board);
    }
    return status;
}

int main(void)
{
    const bento_real_t gravity =
        (bento_real_t)9.81; /* until the first read answers */
    dashboard_t board;
    bento_status_t status;

    (void)memset(&board, 0, sizeof(board));
    board.ui = bento_runtime_get_ui();
    board.running = true;
    board.threshold = THRESHOLD_START;
    board.readings.accel[2] = gravity;
    status = build_screen(&board);
    if (status == BENTO_OK) {
        board.last_text_ms = bento_time_ticks_ms();
        board.good_ms = bento_time_ticks_ms();
        (void)printf(
            "s08 dashboard started - 64 widgets - cadence 200 ms\n");
    }
    while (status == BENTO_OK) {
        status = run_round(&board);
    }
    if ((status == BENTO_E_CANCELLED) &&
        bento_runtime_acknowledge_stop()) {
        (void)bento_ui_clear(
            board
                .ui); /* a refused CLEAR_ALL is ignored, as modui.c does */
        (void)printf("dashboard stopped after %d rounds\n",
                     (int)board.rounds);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
