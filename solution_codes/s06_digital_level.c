/*
 * s06_digital_level.c -- เครื่องวัดระดับดิจิทัลสองแกน (Two-axis digital level)
 *
 * วางบอร์ดราบกับโต๊ะ รอค่านิ่ง แล้วแตะ "ตั้งศูนย์" หนึ่งครั้ง จากนั้นเอียงบอร์ด
 * ซ้าย-ขวา (roll) และหน้า-หลัง (pitch) สองแถบวิ่งแยกแกนบนไม้บรรทัด -90 ถึง 90
 * Seg7 บอกค่าทศนิยมวินาทีละครั้ง ไฟสองดวงบอกว่าผ่านเกณฑ์หรือเกิน ปุ่มลด/เพิ่ม
 * ตั้งเกณฑ์ยอมรับ 1-30 องศา และ "จบการวัด" ปิดโปรแกรม
 *
 * Lay the board flat, wait for the values to settle and tap the zero
 * button once. Then tilt the board left-right (roll) and front-back
 * (pitch): two bars move on their own -90..90 rulers, the Seg7
 * displays show one decimal once a second, two lamps say inside or
 * outside the tolerance, two buttons set the tolerance (1-30 degrees)
 * and the exit button ends the program.
 *
 * Twin of s06_digital_level.py
 * (embedded-systems-for-aiot-developer/solution_codes/s06_digital_level.py).
 * Style: CLEAN_C.md.
 * Differs from the original only where C must:
 *   - every failure of sensors.bmi270.motion() or dsp.tilt() is the
 *     original's OSError (the reading keeps its old value);
 *   - Stop ends the program at the next sleep, without ui.clear() and
 *     the last print, as MicroPython's uncaught KeyboardInterrupt does;
 *   - "{:+.1f}" is bento_fmt_real_precision() with the '+' put in front.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_dsp.h"
#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_time.h"

/* colours, 0xRRGGBB */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_BAR = 0x4A9EFF,
    COLOR_KEY = 0x3A4150
};

/* the tolerance in degrees: the user sets it, it is not fixed */
enum {
    TOL_MIN = 1,
    TOL_MAX = 30,
    TOL_STEP = 1,
    TOL_START = 5,
    TOL_DIGITS = 2
};

/* rulers, timing and text sizes */
enum {
    ANGLE_MAX = 90,
    SCALE_TICKS = 9,
    SCALE_MAJOR_EVERY = 4,
    CARD_RADIUS = 12,
    SCREEN_SETTLE_MS = 200,
    LOOP_MS = 200,
    MS_PER_SECOND = 1000,
    FONT_TITLE = 24,
    FONT_BODY = 20,
    FONT_SMALL = 16,
    NUMBER_BYTES = 40,
    TEXT_BYTES = 96
};

/* Every widget, in the order the original creates it. */
typedef enum {
    W_TITLE,
    W_HEALTH,
    W_TOP_CARD,
    W_ROLL_NAME,
    W_ROLL_BAR,
    W_ROLL_SCALE,
    W_ROLL_SEG,
    W_PITCH_NAME,
    W_PITCH_BAR,
    W_PITCH_SCALE,
    W_PITCH_SEG,
    W_LAMP_CARD,
    W_LAMP_IN,
    W_LAMP_OUT,
    W_IN_NAME,
    W_OUT_NAME,
    W_CONTROL_CARD,
    W_TOL_NAME,
    W_TOL_BOX,
    W_DOWN,
    W_UP,
    W_COMMAND_NAME,
    W_ZERO,
    W_EXIT,
    W_REFERENCE,
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

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t widgets[W_COUNT];
    bento_dsp_ema_state_t
        roll_filter; /* a filter remembers: one per axis */
    bento_dsp_ema_state_t pitch_filter;
    bento_real_t roll;
    bento_real_t pitch;
    bento_real_t roll_filtered;
    bento_real_t pitch_filtered;
    bento_real_t roll_zero;
    bento_real_t pitch_zero;
    bento_real_t roll_show;
    bento_real_t pitch_show;
    int32_t tolerance;
    int32_t last_second; /* -1: no number written yet */
    bool ok_read;
    bool running;
    char health
        [TEXT_BYTES]; /* the quality line, decided once a second */
} level_t;

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
static void set_text(const level_t *level, widget_index_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(level->ui, level->widgets[widget], text,
                            (uint32_t)strlen(text));
}

static void set_value(const level_t *level, widget_index_t widget,
                      int32_t value)
{
    (void)bento_ui_set_value(level->ui, level->widgets[widget], value);
}

static void set_color(const level_t *level, widget_index_t widget,
                      uint32_t color)
{
    (void)bento_ui_set_color(level->ui, level->widgets[widget], color);
}

/* Creates specs[0..count) into widgets[first..]; a scale gets its ticks
 * and the spinbox its digits right after it is made, as in the original. */
static bento_status_t add_widgets(level_t *level,
                                  const widget_spec_t *specs,
                                  int32_t first, int32_t count)
{
    bento_ui_widget_args_t args;
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < count) && (status == BENTO_OK); i++) {
        const widget_spec_t *spec = &specs[i];
        bento_ui_id_t *widget = &level->widgets[first + i];

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
        status = bento_ui_create(level->ui, spec->type, &args, widget);
        if ((status == BENTO_OK) &&
            (spec->type == (uint8_t)UI_WIDGET_SCALE)) {
            (void)bento_ui_set_ticks(level->ui, *widget, SCALE_TICKS,
                                     SCALE_MAJOR_EVERY);
        }
        if ((status == BENTO_OK) &&
            (spec->type == (uint8_t)UI_WIDGET_SPINBOX)) {
            (void)bento_ui_set_digits(level->ui, *widget, TOL_DIGITS,
                                      0);
        }
    }
    return status;
}

/* The title, the quality line, the two axes side by side and the lamps. */
static bento_status_t add_measure_widgets(level_t *level)
{
    static const widget_spec_t specs[W_CONTROL_CARD] = {
        {UI_WIDGET_LABEL, "เครื่องวัดระดับดิจิทัลสองแกน", 24, 8, -1, -1,
         COLOR_TEXT, FONT_TITLE, 0, 0},
        {UI_WIDGET_LABEL, "กำลังวัดอยู่", 416, 12, -1, -1, COLOR_DIM,
         FONT_BODY, 0, 0},
        {UI_WIDGET_PANEL, NULL, 24, 48, 576, 128, COLOR_CARD, 1,
         COLOR_DIM, CARD_RADIUS},
        {UI_WIDGET_LABEL, "ROLL เอียงซ้าย-ขวา (องศา)", 40, 60, -1, -1,
         COLOR_DIM, FONT_SMALL, 0, 0},
        {UI_WIDGET_BAR, NULL, 40, 96, 160, 16, COLOR_BAR, 0, -ANGLE_MAX,
         ANGLE_MAX},
        {UI_WIDGET_SCALE, NULL, 40, 112, 160, 44, COLOR_TEXT, 0,
         -ANGLE_MAX, ANGLE_MAX},
        {UI_WIDGET_SEG7, "+00.0", 216, 96, 96, 56, COLOR_TEXT, 0, 0, 0},
        {UI_WIDGET_LABEL, "PITCH เอียงหน้า-หลัง (องศา)", 328, 60, -1, -1,
         COLOR_DIM, FONT_SMALL, 0, 0},
        {UI_WIDGET_BAR, NULL, 328, 96, 160, 16, COLOR_BAR, 0,
         -ANGLE_MAX, ANGLE_MAX},
        {UI_WIDGET_SCALE, NULL, 328, 112, 160, 44, COLOR_TEXT, 0,
         -ANGLE_MAX, ANGLE_MAX},
        {UI_WIDGET_SEG7, "+00.0", 504, 96, 80, 56, COLOR_TEXT, 0, 0, 0},
        {UI_WIDGET_PANEL, NULL, 616, 48, 152, 128, COLOR_CARD, 1,
         COLOR_DIM, CARD_RADIUS},
        {UI_WIDGET_LED, NULL, 632, 72, 48, 48, COLOR_OK, 1, 0, 0},
        {UI_WIDGET_LED, NULL, 704, 72, 48, 48, COLOR_BAD, 0, 0, 0},
        {UI_WIDGET_LABEL, "ผ่าน", 632, 128, -1, -1, COLOR_DIM,
         FONT_SMALL, 0, 0},
        {UI_WIDGET_LABEL, "เกิน", 696, 128, -1, -1, COLOR_DIM,
         FONT_SMALL, 0, 0}};

    return add_widgets(level, specs, W_TITLE, W_CONTROL_CARD);
}

/* The tolerance box with its two buttons, the two commands and the
 * reference line. Five touch targets in one row, 32 px apart, clear of
 * the corner the Console button keeps (x > 690 and y > 340). */
static bento_status_t add_control_widgets(level_t *level)
{
    static const widget_spec_t specs[W_COUNT - W_CONTROL_CARD] = {
        {UI_WIDGET_PANEL, NULL, 24, 192, 744, 188, COLOR_CARD, 1,
         COLOR_DIM, CARD_RADIUS},
        {UI_WIDGET_LABEL, "เกณฑ์ยอมรับ (องศา)", 40, 204, -1, -1,
         COLOR_DIM, FONT_SMALL, 0, 0},
        {UI_WIDGET_SPINBOX, NULL, 40, 240, 96, 88, COLOR_TEXT,
         TOL_START, TOL_MIN, TOL_MAX},
        {UI_WIDGET_BUTTON, "ลด", 168, 240, 88, 88, COLOR_KEY, FONT_BODY,
         0, 0},
        {UI_WIDGET_BUTTON, "เพิ่ม", 288, 240, 88, 88, COLOR_KEY,
         FONT_BODY, 0, 0},
        {UI_WIDGET_LABEL, "คำสั่ง", 472, 204, -1, -1, COLOR_DIM,
         FONT_SMALL, 0, 0},
        {UI_WIDGET_BUTTON, "ตั้งศูนย์", 472, 240, 144, 88, COLOR_OK,
         FONT_BODY, 0, 0},
        {UI_WIDGET_BUTTON, "จบการวัด", 648, 240, 104, 88, COLOR_KEY,
         FONT_BODY, 0, 0},
        {UI_WIDGET_LABEL, "อ้างอิง R +0.0  P +0.0", 40, 336, -1, -1,
         COLOR_DIM, FONT_SMALL, 0, 0}};

    return add_widgets(level, specs, W_CONTROL_CARD,
                       W_COUNT - W_CONTROL_CARD);
}

/* Two filters, one per signal, alpha 0.2. */
static bento_status_t start_filters(level_t *level)
{
    const float alpha = 0.2F;
    bento_dsp_ema_config_t config;
    bento_status_t status = bento_dsp_ema_config_init(&config);

    config.alpha = alpha;
    if (status == BENTO_OK) {
        status = bento_dsp_ema_state_init(&level->roll_filter, &config);
    }
    if (status == BENTO_OK) {
        status =
            bento_dsp_ema_state_init(&level->pitch_filter, &config);
    }
    return status;
}

/* "{:+.1f}": one decimal, with a '+' in front of a positive number. */
static void signed_text(bento_real_t value, char *text, size_t cap)
{
    char digits[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;

    if (bento_fmt_real_precision(digits, sizeof(digits), value, 1,
                                 &length) != BENTO_OK) {
        digits[0] = '\0';
    }
    (void)snprintf(text, cap, "%s%s", (digits[0] == '-') ? "" : "+",
                   digits);
}

/* The bars are set to -90..90; a value past an end would stick there. */
static int32_t clamp90(bento_real_t value)
{
    bento_real_t clamped = value;

    if (value > (bento_real_t)ANGLE_MAX) {
        clamped = (bento_real_t)ANGLE_MAX;
    } else if (value < (bento_real_t)-ANGLE_MAX) {
        clamped = (bento_real_t)-ANGLE_MAX;
    } else {
        /* inside the ruler */
    }
    return (int32_t)clamped; /* int() truncates toward zero */
}

static bento_real_t magnitude(bento_real_t value)
{
    return (value < (bento_real_t)0) ? -value : value;
}

/* Six axes from one snapshot, then degrees. dsp.tilt() answers
 * (roll, pitch) in that order: swap them and the bars move on the wrong
 * axis. A failed read keeps the old values (the original's OSError). */
static void read_tilt(level_t *level)
{
    bento_sensors_imu6_t imu;
    float roll = 0.0F;
    float pitch = 0.0F;

    bento_sensors_imu6_init(&imu);
    level->ok_read = (bento_sensors_bmi270_motion(&imu) == BENTO_OK) &&
                     (bento_dsp_tilt(imu.ax, imu.ay, imu.az, &roll,
                                     &pitch) == BENTO_OK);
    if (level->ok_read) {
        level->roll = (bento_real_t)roll;
        level->pitch = (bento_real_t)pitch;
    }
}

/* Filter first, then subtract the zero: the zero button must keep the
 * filtered value, not a raw one that is shaking right now. */
static bento_status_t filter_tilt(level_t *level)
{
    float roll = 0.0F;
    float pitch = 0.0F;
    bento_status_t status = BENTO_OK;

    if (level->ok_read) {
        status = bento_dsp_ema_state_update(&level->roll_filter,
                                            (float)level->roll, &roll);
        if (status == BENTO_OK) {
            level->roll_filtered = (bento_real_t)roll;
            status = bento_dsp_ema_state_update(
                &level->pitch_filter, (float)level->pitch, &pitch);
        }
        if (status == BENTO_OK) {
            level->pitch_filtered = (bento_real_t)pitch;
        }
    }
    level->roll_show = level->roll_filtered - level->roll_zero;
    level->pitch_show = level->pitch_filtered - level->pitch_zero;
    return status;
}

/* The zero button moves the reference to the pose of now; it does not
 * change what is measured. */
static void set_zero(level_t *level)
{
    char roll[NUMBER_BYTES];
    char pitch[NUMBER_BYTES];
    char text[TEXT_BYTES];

    level->roll_zero = level->roll_filtered;
    level->pitch_zero = level->pitch_filtered;
    signed_text(level->roll_zero, roll, sizeof(roll));
    signed_text(level->pitch_zero, pitch, sizeof(pitch));
    (void)snprintf(text, sizeof(text), "อ้างอิง R %s  P %s", roll, pitch);
    set_text(level, W_REFERENCE, text);
}

/* The original reads only ev['handle'], so any kind of event on a
 * widget counts: the id alone is compared. */
static void handle_events(level_t *level)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(level->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U; /* ui.poll() answers no events when it fails */
    }
    for (i = 0U; i < count; i++) {
        const bento_ui_id_t widget = events[i].id;

        if (widget == level->widgets[W_ZERO]) {
            set_zero(level);
        } else if (widget == level->widgets[W_EXIT]) {
            level->running = false;
        } else if (widget == level->widgets[W_UP]) {
            level->tolerance = (level->tolerance + TOL_STEP < TOL_MAX)
                                   ? (level->tolerance + TOL_STEP)
                                   : TOL_MAX;
            set_value(level, W_TOL_BOX, level->tolerance);
        } else if (widget == level->widgets[W_DOWN]) {
            level->tolerance = (level->tolerance - TOL_STEP > TOL_MIN)
                                   ? (level->tolerance - TOL_STEP)
                                   : TOL_MIN;
            set_value(level, W_TOL_BOX, level->tolerance);
        } else {
            /* any other widget changes nothing */
        }
    }
}

/* Bars and lamps move every pass: the eye reads a position without
 * stopping to read a number. One lamp only is lit. */
static bool show_bars_and_lamps(const level_t *level)
{
    const bento_real_t tolerance = (bento_real_t)level->tolerance;
    const bool in_tolerance =
        (magnitude(level->roll_show) <= tolerance) &&
        (magnitude(level->pitch_show) <= tolerance);

    set_value(level, W_ROLL_BAR, clamp90(level->roll_show));
    set_value(level, W_PITCH_BAR, clamp90(level->pitch_show));
    set_value(level, W_LAMP_IN, in_tolerance ? 1 : 0);
    set_value(level, W_LAMP_OUT, in_tolerance ? 0 : 1);
    return in_tolerance;
}

/* The numbers a person reads change at most once a second. The colour
 * goes first: text and colour are two messages across the cores. */
static void show_numbers(level_t *level, bool in_tolerance)
{
    const int32_t second =
        (int32_t)(bento_time_ticks_ms() / (uint32_t)MS_PER_SECOND);
    char text[NUMBER_BYTES];

    if (second != level->last_second) {
        level->last_second = second;
        signed_text(level->roll_show, text, sizeof(text));
        set_text(level, W_ROLL_SEG, text);
        signed_text(level->pitch_show, text, sizeof(text));
        set_text(level, W_PITCH_SEG, text);
        if (!level->ok_read) {
            set_color(level, W_HEALTH, COLOR_WARN);
            (void)snprintf(level->health, sizeof(level->health), "%s",
                           "ค่าค้าง นี่คือค่าล่าสุด");
        } else if (in_tolerance) {
            set_color(level, W_HEALTH, COLOR_DIM);
            (void)snprintf(level->health, sizeof(level->health),
                           "อยู่ในเกณฑ์ %d องศา", (int)level->tolerance);
        } else {
            set_color(level, W_HEALTH, COLOR_BAD);
            (void)snprintf(level->health, sizeof(level->health),
                           "เอียงเกินเกณฑ์ %d องศา",
                           (int)level->tolerance);
        }
    }
}

/* One pass: read, filter, touches, dials, then the 200 ms rest. The
 * quality line is sent again every pass on purpose: a text keeps the
 * display core in its fast 5 ms mode, a bar's value does not, and the
 * firmware does not redraw a text that did not change. */
static bento_status_t measure_once(level_t *level)
{
    bento_status_t status;

    read_tilt(level);
    status = filter_tilt(level);
    if (status == BENTO_OK) {
        handle_events(level);
        show_numbers(level, show_bars_and_lamps(level));
        set_text(level, W_HEALTH, level->health);
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* The first read on the Eva Kit can wait up to 16 s after a reset: let
 * that wait happen before the dials are built. */
static void warm_up_sensor(void)
{
    bento_sensors_imu6_t imu;

    bento_sensors_imu6_init(&imu);
    if (bento_sensors_bmi270_motion(&imu) != BENTO_OK) {
        (void)printf("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ในลูป\n");
    }
}

static bento_status_t start_level(level_t *level)
{
    bento_status_t status = bento_ui_screen_default(level->ui);

    if (status == BENTO_OK) {
        (void)bento_ui_clear(
            level
                ->ui); /* a refused CLEAR_ALL is ignored, as modui.c does */
        status = wait_ms(SCREEN_SETTLE_MS);
    }
    if (status == BENTO_OK) {
        warm_up_sensor();
        status = add_measure_widgets(level);
    }
    if (status == BENTO_OK) {
        status = add_control_widgets(level);
    }
    if (status == BENTO_OK) {
        status = start_filters(level);
    }
    return status;
}

int main(void)
{
    level_t level;
    bento_status_t status;

    (void)memset(&level, 0, sizeof(level));
    level.ui = bento_runtime_get_ui();
    level.tolerance = TOL_START;
    level.last_second = -1;
    level.running = true;
    (void)snprintf(level.health, sizeof(level.health), "%s",
                   "กำลังวัดอยู่");
    status = start_level(&level);
    while ((status == BENTO_OK) && level.running) {
        status = measure_once(&level);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_clear(
            level
                .ui); /* a refused CLEAR_ALL is ignored, as modui.c does */
        (void)printf("ปิดเครื่องวัดระดับแล้ว\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
