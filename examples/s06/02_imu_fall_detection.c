/*
 * 02_imu_fall_detection.c -- ตรวจการล้มด้วยลำดับสามขั้น (คาบ 06)
 *
 * การล้มจริงคือสามขั้นเรียงกัน: ตกอิสระ (ขนาดความเร่งต่ำกว่า 3 m/s2) แล้วกระแทก
 * (เกิน 25 m/s2) ภายใน 800 ms แล้วนิ่งต่ออีก 1500 ms จึงแจ้งเตือน อ่าน BMI270
 * ทุก 20 ms 2500 รอบ กราฟฟ้าคือขนาดความเร่ง เหลืองกับแดงคือสองเกณฑ์ ทางขวาสามขั้น
 * สว่างตามที่เครื่องเชื่อจริง กับตัวนับการแจ้งเตือน รอบที่อ่านเซนเซอร์ไม่ได้ใช้ค่านิ่ง 1 g
 * บอร์ด: Dev Kit และ Eva Kit
 *
 * A real fall is three stages in a row: free fall (the magnitude below
 * 3 m/s2), then an impact (above 25 m/s2) within 800 ms, then 1500 ms of
 * lying still; only then does it alert. The BMI270 is read every 20 ms
 * for 2500 rounds: blue is the magnitude, yellow and red the two
 * thresholds; on the right the three stages light as the machine
 * believes them, with the alert count. A round whose sensor cannot be
 * read takes a still 1 g instead. Boards: Dev Kit and Eva Kit.
 *
 * Twin of 02_imu_fall_detection.py
 * (embedded-systems-for-aiot-developer/examples/s06/02_imu_fall_detection.py),
 * rewritten from the library twin aiot-s06-02-imu-fall-detection.
 * Style: CLEAN_C.md.
 * What C changes only because the language forces it: a float is
 * bento_real_t, the width each board's MicroPython uses; the
 * original's except OSError around the reading is any failed BMI270
 * read here, except BENTO_E_UNSUPPORTED (no sensors.bmi270 at all),
 * which ends the program as the AttributeError would; round(m, 1) is
 * nearbyint(m * 10) / 10, printed as str() prints it.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* Every real-number step is rounded as MicroPython rounds it: the
 * compiler must not fuse a multiply and an add into one. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

/* The detector's stages, times and the run. */
enum {
    STAGE_IDLE = 0,
    STAGE_FELL = 1,
    STAGE_HIT = 2,
    STAGE_COUNT = 3, /* the three labels; 3 lit = the alert */
    WINDOW_MS =
        800, /* the impact must follow the free fall this soon */
    STILL_MS = 1500, /* then lie still this long */
    HOLD_MS = 2000,  /* an alert keeps all three stages lit this long */
    ROUNDS = 2500,
    LOOP_MS = 20,
    CHART_SCALE = 10, /* the chart takes integers: m/s2 times ten */
    TEXT_BYTES = 128  /* a console or label line, at most 126 bytes */
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 12,
    TITLE_Y = 8,
    CHART_X = 12,
    CHART_Y = 40,
    CHART_W = 472,
    CHART_H = 232,
    CHART_MAX = 400, /* 0 to 40 m/s2, times ten */
    RIGHT_X = 496,
    BLUE_Y = 42,
    YELLOW_Y = 78,
    RED_Y = 114,
    STAGE1_X = 692,
    STAGE1_Y = 124,
    STAGE_X = 504,
    STAGE2_Y = 152,
    STAGE3_Y = 184,
    ALERT_LABEL_Y = 238,
    ALERT_X = 600,
    ALERT_Y = 232,
    ALERT_W = 92,
    ALERT_H = 36,
    HINT_X = 24,
    HINT_Y = 296,
    HINT2_Y = 324
};

/* Font sizes, in pixels, and colours, 0xRRGGBB. */
enum {
    FONT_TITLE = 24,
    FONT_TEXT = 20,
    COLOR_DEFAULT = 0,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_FREE_FALL = 0xFFC107,
    COLOR_IMPACT = 0xFF5555,
    COLOR_ON = 0x30A46C, /* a stage the machine believes */
    COLOR_OFF = 0x9AA3AF /* a stage not reached yet */
};

/* The two thresholds (m/s2) and a still board's magnitude, 1 g. */
static const bento_real_t FREE_FALL = (bento_real_t)3.0;
static const bento_real_t IMPACT = (bento_real_t)25.0;
static const bento_real_t GRAVITY = (bento_real_t)9.8;

/* Where a label goes and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    int32_t font_px;
    uint32_t color;
} place_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    uint8_t magnitude_series; /* series 0 comes with the chart, blue */
    uint8_t free_fall_series;
    uint8_t impact_series;
    bento_ui_id_t stages[STAGE_COUNT];
    bento_ui_id_t alerts;
} fall_screen_t;

/* What the detector remembers from one round to the next. */
typedef struct {
    int32_t stage;
    uint32_t mark_ms; /* when the current stage began */
    int32_t alerts;
    int32_t lit; /* stages lit on the screen, -1 before the first */
    uint32_t alert_ms;
    bool alerted;
} detector_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const place_t *place,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.color = place->color;
    args.font_px = place->font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* ui.poll() answers no events when it fails, and the events are not
 * used, so its status is not an error here. */
static void skip_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The chart with its two threshold series, yellow and red. */
static bento_status_t add_chart(fall_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CHART_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.min_val = 0;
    args.max_val = CHART_MAX;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    screen->magnitude_series = 0U;
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_FREE_FALL,
                                           &screen->free_fall_series);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_IMPACT,
                                           &screen->impact_series);
    }
    return status;
}

/* The legend of the three lines. */
static bento_status_t add_legend(bento_ui_t *ui)
{
    const place_t blue = {RIGHT_X, BLUE_Y, FONT_TEXT, COLOR_ACCENT};
    const place_t yellow = {RIGHT_X, YELLOW_Y, FONT_TEXT, COLOR_WARN};
    const place_t red = {RIGHT_X, RED_Y, FONT_TEXT, COLOR_BAD};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "ฟ้า = ขนาดความเร่ง", &blue, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "เหลือง = เกณฑ์ตกอิสระ", &yellow, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "แดง = เกณฑ์กระแทก", &red, &unused);
    }
    return status;
}

/* The three stage labels, all off, and the alert counter. */
static bento_status_t add_stages(fall_screen_t *screen)
{
    static const char *const names[STAGE_COUNT] = {"1 ตกอิสระ",
                                                   "2 กระแทก", "3 นิ่ง"};
    static const place_t places[STAGE_COUNT] = {
        {STAGE1_X, STAGE1_Y, FONT_TEXT, COLOR_OFF},
        {STAGE_X, STAGE2_Y, FONT_TEXT, COLOR_OFF},
        {STAGE_X, STAGE3_Y, FONT_TEXT, COLOR_OFF}};
    const place_t caption = {RIGHT_X, ALERT_LABEL_Y, FONT_TEXT,
                             COLOR_DEFAULT};
    bento_ui_id_t unused;
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < STAGE_COUNT) && (status == BENTO_OK); i++) {
        status = add_label(screen->ui, names[i], &places[i],
                           &screen->stages[i]);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "แจ้งเตือน", &caption, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_t args;

        bento_ui_widget_args_init(&args);
        args.x = ALERT_X;
        args.y = ALERT_Y;
        args.w = ALERT_W;
        args.h = ALERT_H;
        status =
            bento_ui_seg7_create(screen->ui, &args, &screen->alerts);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). Two hint labels instead of one block: a label is
 * cut at 126 bytes, and a Thai character takes 3. */
static bento_status_t build_screen(fall_screen_t *screen)
{
    const place_t title = {TITLE_X, TITLE_Y, FONT_TITLE, COLOR_DEFAULT};
    const place_t hint = {HINT_X, HINT_Y, FONT_TEXT, COLOR_WARN};
    const place_t hint2 = {HINT_X, HINT2_Y, FONT_TEXT, COLOR_WARN};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ตรวจการล้ม - ลำดับสามขั้น", &title,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_legend(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_stages(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ตรวจแค่แรงกระแทกอย่างเดียว", &hint,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "เตือนผิดทุกครั้งที่วางของลงโต๊ะ",
                           &hint2, &unused);
    }
    return status;
}

/* The console's heading and the two thresholds; lcd calls are sent
 * without an answer (modlcd.c). */
static bento_status_t open_console(void)
{
    char free_fall[BENTO_FMT_BUF_SIZE];
    char impact[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint32_t length = 0U;
    bento_status_t status = bento_fmt_real(
        free_fall, (uint32_t)sizeof(free_fall), FREE_FALL, &length);

    if (status == BENTO_OK) {
        status = bento_fmt_real(impact, (uint32_t)sizeof(impact),
                                IMPACT, &length);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ตรวจการล้ม</h2>");
        (void)snprintf(text, sizeof(text),
                       "ตกอิสระ < %s | กระแทก > %s m/s2", free_fall,
                       impact);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* The magnitude of the acceleration; a reading that fails (the sensor
 * is not ready yet) gives a still 1 g instead of ending the program. */
static bento_status_t read_magnitude(bento_real_t *magnitude)
{
    bento_sensors_imu6_t motion;
    bento_status_t status;

    bento_sensors_imu6_init(&motion);
    status = bento_sensors_bmi270_motion(&motion);
    if (status == BENTO_OK) {
        const bento_real_t ax = (bento_real_t)motion.ax;
        const bento_real_t ay = (bento_real_t)motion.ay;
        const bento_real_t az = (bento_real_t)motion.az;
        const bento_real_t squares = (ax * ax) + (ay * ay) + (az * az);

        *magnitude = (bento_real_t)sqrt((double)squares);
    } else if (status != BENTO_E_UNSUPPORTED) {
        *magnitude = GRAVITY;
        status = BENTO_OK;
    } else {
        /* no BMI270 path at all: the original ends here too */
    }
    return status;
}

/* A stage line on the console with the magnitude, round(m, 1). */
static bento_status_t log_stage(const char *stage,
                                bento_real_t magnitude)
{
    char value[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint32_t length = 0U;
    const bento_real_t tenths = (bento_real_t)nearbyint(
        (double)(magnitude * (bento_real_t)CHART_SCALE));
    const bento_status_t status =
        bento_fmt_real(value, (uint32_t)sizeof(value),
                       tenths / (bento_real_t)CHART_SCALE, &length);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%s %s", stage, value);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* The board lay still long enough after the impact: an alert. */
static void raise_alert(const fall_screen_t *screen,
                        detector_t *detector, uint32_t now_ms)
{
    char digits[TEXT_BYTES];

    detector->stage = STAGE_IDLE;
    detector->alerts++;
    detector->alert_ms = now_ms;
    detector->alerted = true;
    (void)snprintf(digits, sizeof(digits), "%d", (int)detector->alerts);
    (void)bento_ui_set_text(screen->ui, screen->alerts, digits,
                            (uint32_t)strlen(digits));
    (void)bento_lcd_print(
        "<span class=ok>3) นิ่งหลังกระแทก - แจ้งเตือน</span>");
}

/* After the impact the board must lie still: a person who gets up at
 * once needs no help. */
static void check_still(const fall_screen_t *screen,
                        detector_t *detector, bento_real_t magnitude,
                        uint32_t now_ms)
{
    const bento_real_t still_band = (bento_real_t)4.0;
    const bento_real_t change = magnitude - GRAVITY;
    const bento_real_t distance =
        (change < (bento_real_t)0) ? -change : change;

    if (distance > still_band) {
        detector->stage = STAGE_IDLE;
        (void)bento_lcd_print("<span class=muted>ขยับต่อ - ยกเลิก</span>");
    } else if (bento_time_ticks_diff(now_ms, detector->mark_ms) >=
               STILL_MS) {
        raise_alert(screen, detector, now_ms);
    } else {
        /* still lying there: wait */
    }
}

/* One step of the three-stage machine. */
static bento_status_t advance(const fall_screen_t *screen,
                              detector_t *detector,
                              bento_real_t magnitude, uint32_t now_ms)
{
    bento_status_t status = BENTO_OK;

    if (detector->stage == STAGE_IDLE) {
        if (magnitude < FREE_FALL) {
            detector->stage = STAGE_FELL;
            detector->mark_ms = now_ms;
            status = log_stage("1) ตกอิสระ - ขนาด", magnitude);
        }
    } else if (detector->stage == STAGE_FELL) {
        if (magnitude > IMPACT) {
            detector->stage = STAGE_HIT;
            detector->mark_ms = now_ms;
            status = log_stage("2) กระแทก - ขนาด", magnitude);
        } else if (bento_time_ticks_diff(now_ms, detector->mark_ms) >
                   WINDOW_MS) {
            detector->stage =
                STAGE_IDLE; /* maybe only lifted quickly */
        } else {
            /* still inside the window */
        }
    } else {
        check_still(screen, detector, magnitude, now_ms);
    }
    return status;
}

/* The three stages show the machine's state, not a story, so they
 * light as it really is; an alert holds all three for HOLD_MS. */
static void show_stages(const fall_screen_t *screen,
                        detector_t *detector, uint32_t now_ms)
{
    int32_t upto;

    if (detector->alerted &&
        (bento_time_ticks_diff(now_ms, detector->alert_ms) >=
         HOLD_MS)) {
        detector->alerted = false;
    }
    upto = detector->alerted ? STAGE_COUNT : detector->stage;
    if (upto != detector->lit) {
        int32_t i;

        detector->lit = upto;
        for (i = 0; i < STAGE_COUNT; i++) {
            (void)bento_ui_set_color(screen->ui, screen->stages[i],
                                     (i < upto) ? COLOR_ON : COLOR_OFF);
        }
    }
}

static bento_status_t run_round(const fall_screen_t *screen,
                                detector_t *detector)
{
    bento_real_t magnitude = GRAVITY;
    bento_status_t status = read_magnitude(&magnitude);

    if (status == BENTO_OK) {
        const uint32_t now_ms = bento_time_ticks_ms();
        const bento_real_t scale = (bento_real_t)CHART_SCALE;

        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->magnitude_series,
                                      (int32_t)(magnitude * scale));
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->free_fall_series,
                                      (int32_t)(FREE_FALL * scale));
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->impact_series,
                                      (int32_t)(IMPACT * scale));
        status = advance(screen, detector, magnitude, now_ms);
        show_stages(screen, detector, now_ms);
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

static bento_status_t run_rounds(const fall_screen_t *screen,
                                 detector_t *detector)
{
    bento_status_t status = BENTO_OK;
    int32_t round;

    (void)bento_ui_set_text(screen->ui, screen->alerts, "0", 1U);
    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = run_round(screen, detector);
    }
    return status;
}

int main(void)
{
    fall_screen_t screen;
    detector_t detector = {STAGE_IDLE, 0U, 0, -1, 0U, false};
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = open_console();
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    if (status == BENTO_OK) {
        status = run_rounds(&screen, &detector);
    }
    if (status == BENTO_OK) {
        (void)printf("แจ้งเตือนไป %d ครั้ง | "
                     "ลองวางบอร์ดลงโต๊ะแรง ๆ ดูว่าเตือนหรือไม่\n",
                     (int)detector.alerts);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
