/*
 * 01_imu_step_counter.c -- นับก้าวจากความเร่ง (คาบ 06)
 *
 * หนึ่งก้าวคือยอดคลื่นหนึ่งลูกของขนาดความเร่ง ต้องกรองก่อนนับเสมอ
 * อ่าน BMI270 ทุก 25 ms 2000 รอบ กราฟฟ้าคือขนาดความเร่งดิบ แดงคือค่าหลังกรอง EMA
 * เหลืองคือเส้นเกณฑ์ ขวามือคือจำนวนก้าวตัวใหญ่ กับป้ายบอกว่าพร้อมนับหรือยังห้ามนับซ้ำ
 * ถ้าไม่มีเวลาห้ามนับซ้ำ (refractory) การสั่นหนึ่งครั้งจะถูกนับหลายก้าว
 * รอบที่อ่านเซนเซอร์ยังไม่ได้ โปรแกรมบอกบนจอแล้วรอรอบหน้า บอร์ด: Dev Kit และ Eva Kit
 *
 * One step is one crest of the acceleration's magnitude, and it must
 * be filtered before it is counted. The BMI270 is read every 25 ms for
 * 2000 rounds: blue is the raw magnitude, red the EMA-filtered value,
 * yellow the threshold; on the right the step count in big digits and
 * a label that says whether the counter is ready or inside its
 * refractory time. A round whose sensor cannot be read yet says so and
 * waits for the next one. Boards: Dev Kit and Eva Kit.
 *
 * Twin of 01_imu_step_counter.py
 * (embedded-systems-for-aiot-developer/examples/s06/01_imu_step_counter.py),
 * rewritten from the library twin aiot-s06-01-imu-step-counter. Style:
 * CLEAN_C.md.
 * What C changes only because the language forces it: a float is
 * bento_real_t, the width each board's MicroPython uses; the
 * original's except OSError around the reading is any failed BMI270
 * read here, except BENTO_E_UNSUPPORTED (no sensors.bmi270 at all),
 * which ends the program as the AttributeError would; the original's
 * waiting flag holds False, True or -1, here an int32_t 0, 1 or -1;
 * round(v, 1) is nearbyint(v * 10) / 10, printed as str() prints it.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_dsp.h"
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

/* The step detector and the run. */
enum {
    REFRACT_MS =
        300, /* the fastest runner makes about 3 steps a second */
    ROUNDS = 2000,
    LOOP_MS = 25,
    LOG_EVERY = 5,    /* log every fifth step */
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
    CHART_MAX = 250, /* 0 to 25 m/s2, times ten */
    RIGHT_X = 496,
    BLUE_Y = 40,
    RED_Y = 68,
    YELLOW_Y = 96,
    COUNT_LABEL_Y = 124,
    COUNT_Y = 152,
    COUNT_W = 180,
    COUNT_H = 40,
    STATE_Y = 212,
    PANEL_X = 12,
    PANEL_Y = 288,
    PANEL_W = 664,
    PANEL_H = 76,
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
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_EMA = 0xFF5555,
    COLOR_THRESHOLD = 0xFFC107,
    COLOR_BUSY = 0xFF9800,
    COLOR_READY = 0x00E676
};

/* A magnitude above this (m/s2) is a crest. */
static const bento_real_t THRESHOLD = (bento_real_t)12.0;

static const char TEXT_READY[] = "พร้อมนับก้าว";

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
    uint8_t raw_series; /* series 0 comes with the chart, blue */
    uint8_t ema_series;
    uint8_t threshold_series;
    bento_ui_id_t count;
    bento_ui_id_t state;
} steps_screen_t;

/* What the detector remembers from one round to the next. */
typedef struct {
    bento_dsp_ema_state_t smooth;
    int32_t steps;
    bool above;
    uint32_t last_step_ms;
    int32_t
        waiting; /* 0 ready, 1 busy, -1 the label must be rewritten */
} counter_t;

/* int() of a real: truncates toward zero, as the cast does. A function,
 * so that the cast never applies to a composite expression (MISRA 10.8). */
static int32_t truncate_to_int(bento_real_t value)
{
    return (int32_t)value;
}

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

/* MicroPython's widget setters ignore a refusal (modui.c), so the twin
 * does too. */
static void set_text(bento_ui_t *ui, bento_ui_id_t id, const char *text)
{
    (void)bento_ui_set_text(ui, id, text, (uint32_t)strlen(text));
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

/* The chart with its two extra series, red and yellow. */
static bento_status_t add_chart(steps_screen_t *screen)
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
    screen->raw_series = 0U;
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            screen->ui, screen->chart, COLOR_EMA, &screen->ema_series);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_THRESHOLD,
                                           &screen->threshold_series);
    }
    return status;
}

/* The legend of the three lines. */
static bento_status_t add_legend(bento_ui_t *ui)
{
    const place_t blue = {RIGHT_X, BLUE_Y, FONT_TEXT, COLOR_ACCENT};
    const place_t red = {RIGHT_X, RED_Y, FONT_TEXT, COLOR_BAD};
    const place_t yellow = {RIGHT_X, YELLOW_Y, FONT_TEXT, COLOR_WARN};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "ฟ้า = ขนาดความเร่งดิบ", &blue, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "แดง = หลังกรอง EMA", &red, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "เหลือง = เกณฑ์ 12 m/s2", &yellow, &unused);
    }
    return status;
}

/* The step count in big digits and the ready/busy label. */
static bento_status_t add_counter(steps_screen_t *screen)
{
    const place_t caption = {RIGHT_X, COUNT_LABEL_Y, FONT_TEXT,
                             COLOR_DEFAULT};
    const place_t state = {RIGHT_X, STATE_Y, FONT_TEXT, COLOR_OK};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "นับได้ (ก้าว)", &caption, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_t args;

        bento_ui_widget_args_init(&args);
        args.x = RIGHT_X;
        args.y = COUNT_Y;
        args.w = COUNT_W;
        args.h = COUNT_H;
        status =
            bento_ui_seg7_create(screen->ui, &args, &screen->count);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, TEXT_READY, &state, &screen->state);
    }
    return status;
}

/* Two labels instead of one block: a label is cut at 126 bytes, and a
 * Thai character takes 3. */
static bento_status_t add_hint(bento_ui_t *ui)
{
    const place_t first = {HINT_X, HINT_Y, FONT_TEXT, COLOR_WARN};
    const place_t second = {HINT_X, HINT2_Y, FONT_TEXT, COLOR_WARN};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = PANEL_X;
    args.y = PANEL_Y;
    args.w = PANEL_W;
    args.h = PANEL_H;
    status = bento_ui_panel_create(ui, &args, &unused);
    if (status == BENTO_OK) {
        status =
            add_label(ui, "วางนิ่ง ๆ สองเส้นทับกันสนิท", &first, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "พอเขย่า เส้นแดงเรียบกว่าเส้นฟ้า", &second,
                           &unused);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(steps_screen_t *screen)
{
    const place_t title = {TITLE_X, TITLE_Y, FONT_TITLE, COLOR_DEFAULT};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ตัวนับก้าว - ทำไมต้องกรองก่อนนับ",
                           &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_legend(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_counter(screen);
    }
    if (status == BENTO_OK) {
        status = add_hint(screen->ui);
    }
    return status;
}

/* The console's heading and the two settings; lcd calls are sent
 * without an answer (modlcd.c). */
static bento_status_t open_console(void)
{
    char threshold[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint32_t length = 0U;
    const bento_status_t status = bento_fmt_real(
        threshold, (uint32_t)sizeof(threshold), THRESHOLD, &length);

    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ตัวนับก้าว</h2>");
        (void)snprintf(text, sizeof(text),
                       "เกณฑ์ %s m/s2 | ห้ามนับซ้ำใน %d ms", threshold,
                       REFRACT_MS);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* Every fifth step goes to the console with its crest, round(v, 1). */
static bento_status_t log_step(int32_t steps, bento_real_t filtered)
{
    char crest[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint32_t length = 0U;
    const bento_real_t tenths = (bento_real_t)nearbyint(
        (double)(filtered * (bento_real_t)CHART_SCALE));
    const bento_status_t status =
        bento_fmt_real(crest, (uint32_t)sizeof(crest),
                       tenths / (bento_real_t)CHART_SCALE, &length);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "ก้าวที่ %d | ค่ายอด %s",
                       (int)steps, crest);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* A crest past the refractory time: one more step on the screen, and
 * every fifth on the console. */
static bento_status_t count_step(const steps_screen_t *screen,
                                 counter_t *counter,
                                 bento_real_t filtered, uint32_t now_ms)
{
    char digits[TEXT_BYTES];
    bento_status_t status = BENTO_OK;

    counter->steps++;
    counter->last_step_ms = now_ms;
    (void)snprintf(digits, sizeof(digits), "%d", (int)counter->steps);
    set_text(screen->ui, screen->count, digits);
    if ((counter->steps % LOG_EVERY) == 0) {
        status = log_step(counter->steps, filtered);
    }
    return status;
}

/* A rising crossing of the threshold counts a step, but only after the
 * refractory time; the value must fall a whole 1 m/s2 below the
 * threshold before the next crest (hysteresis). */
static bento_status_t detect_step(const steps_screen_t *screen,
                                  counter_t *counter,
                                  bento_real_t filtered,
                                  uint32_t now_ms)
{
    bento_status_t status = BENTO_OK;
    const bento_real_t hysteresis = (bento_real_t)1.0;

    if ((filtered > THRESHOLD) && !counter->above) {
        counter->above = true;
        if (bento_time_ticks_diff(now_ms, counter->last_step_ms) >=
            REFRACT_MS) {
            status = count_step(screen, counter, filtered, now_ms);
        }
    } else if (filtered < (THRESHOLD - hysteresis)) {
        counter->above = false;
    } else {
        /* between the two levels the detector keeps its state */
    }
    return status;
}

/* The label answers why some crests are not counted. */
static void show_state(const steps_screen_t *screen, counter_t *counter,
                       uint32_t now_ms)
{
    const int32_t busy =
        (bento_time_ticks_diff(now_ms, counter->last_step_ms) <
         REFRACT_MS)
            ? 1
            : 0;

    if (busy != counter->waiting) {
        counter->waiting = busy;
        set_text(screen->ui, screen->state,
                 (busy != 0) ? "รอพ้นเวลาห้ามนับซ้ำ" : TEXT_READY);
        (void)bento_ui_set_color(screen->ui, screen->state,
                                 (busy != 0) ? COLOR_BUSY
                                             : COLOR_READY);
    }
}

/* One good reading: filter, chart, detect, label. */
static bento_status_t take_reading(const steps_screen_t *screen,
                                   counter_t *counter,
                                   const bento_sensors_imu6_t *motion)
{
    const bento_real_t ax = (bento_real_t)motion->ax;
    const bento_real_t ay = (bento_real_t)motion->ay;
    const bento_real_t az = (bento_real_t)motion->az;
    const bento_real_t squares = (ax * ax) + (ay * ay) + (az * az);
    const bento_real_t magnitude = (bento_real_t)sqrt((double)squares);
    float smoothed = 0.0F;
    bento_status_t status = bento_dsp_ema_state_update(
        &counter->smooth, (float)magnitude, &smoothed);

    if (status == BENTO_OK) {
        const bento_real_t filtered = (bento_real_t)smoothed;
        const uint32_t now_ms = bento_time_ticks_ms();
        const bento_real_t scale = (bento_real_t)CHART_SCALE;

        (void)bento_ui_chart_set_next(
            screen->ui, screen->chart, screen->raw_series,
            truncate_to_int(magnitude * scale));
        (void)bento_ui_chart_set_next(
            screen->ui, screen->chart, screen->ema_series,
            truncate_to_int(filtered * scale));
        (void)bento_ui_chart_set_next(
            screen->ui, screen->chart, screen->threshold_series,
            truncate_to_int(THRESHOLD * scale));
        status = detect_step(screen, counter, filtered, now_ms);
        if (status == BENTO_OK) {
            show_state(screen, counter, now_ms);
        }
    }
    return status;
}

/* One round; a reading that fails (the display core or the bus is not
 * ready yet) waits for the next round instead of ending the program. */
static bento_status_t run_round(const steps_screen_t *screen,
                                counter_t *counter)
{
    bento_sensors_imu6_t motion;
    bento_status_t status;

    bento_sensors_imu6_init(&motion);
    status = bento_sensors_bmi270_motion(&motion);
    if (status == BENTO_OK) {
        status = take_reading(screen, counter, &motion);
    } else if (status != BENTO_E_UNSUPPORTED) {
        set_text(screen->ui, screen->state, "รอเซนเซอร์พร้อม");
        counter->waiting = -1; /* the next round rewrites the label */
        status = BENTO_OK;
    } else {
        /* no BMI270 path at all: the original ends here too */
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

/* The EMA filter, alpha 0.3. */
static bento_status_t start_filter(counter_t *counter)
{
    const float alpha = 0.3F;
    bento_dsp_ema_config_t config;
    bento_status_t status = bento_dsp_ema_config_init(&config);

    if (status == BENTO_OK) {
        config.alpha = alpha;
        status = bento_dsp_ema_state_init(&counter->smooth, &config);
    }
    return status;
}

static bento_status_t run_rounds(const steps_screen_t *screen,
                                 counter_t *counter)
{
    bento_status_t status = BENTO_OK;
    int32_t round;

    counter->last_step_ms = bento_time_ticks_ms();
    set_text(screen->ui, screen->count, "0");
    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = run_round(screen, counter);
    }
    return status;
}

int main(void)
{
    steps_screen_t screen = {
        bento_runtime_get_ui(), 0U, 0U, 0U, 0U, 0U, 0U};
    counter_t counter;
    bento_status_t status;

    (void)memset(&counter, 0, sizeof(counter));
    status = start_filter(&counter);
    if (status == BENTO_OK) {
        status = open_console();
    }
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    if (status == BENTO_OK) {
        status = run_rounds(&screen, &counter);
    }
    if (status == BENTO_OK) {
        char text[TEXT_BYTES];

        (void)snprintf(text, sizeof(text),
                       "<span class=ok>รวม %d ก้าว</span>",
                       (int)counter.steps);
        (void)bento_lcd_print(text);
        (void)printf("ลองตั้ง REFRACT_MS = 0 แล้วเดินเท่าเดิม "
                     "จะเห็นตัวเลขพุ่งเกินจริง\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
