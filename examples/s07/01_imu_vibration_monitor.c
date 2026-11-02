/*
 * 01_imu_vibration_monitor.c -- เฝ้าการสั่นของเครื่องจักร (คาบ 07)
 *
 * เก็บเส้นฐาน RMS สามหน้าต่างตอนเครื่องเดินปกติ (หน้าต่างละ 25 ตัวอย่าง
 * จาก BMI270 ห่างกัน 2 ms) แล้ววัดต่อ 200 หน้าต่าง กราฟ RMS
 * พร้อมเส้นเฝ้าดู (2 เท่า) กับเส้นเข้าตรวจ (4 เท่า) Seg7 บอกกี่เท่าของ
 * เส้นฐาน ไฟสามสีบนบอร์ดบอก ปกติ / เฝ้าดู / ต้องเข้าตรวจ ปุ่มซ้ายล่าง
 * หยุดกราฟไว้ที่ภาพเดิม (หยุดวาด แต่ยังอ่านปุ่ม)
 * บอร์ด: Dev Kit และ Eva Kit
 *
 * Takes a baseline RMS over three windows while the machine runs
 * normally (25 BMI270 samples, 2 ms apart, per window), then measures
 * 200 more windows: a chart of the RMS with a watch line (2 times) and
 * an inspect line (4 times), a Seg7 with the ratio to the baseline, and
 * the board's three status LEDs for normal / watch / inspect. The
 * button at the bottom left freezes the chart (it stops drawing, the
 * button is still read). Boards: Dev Kit and Eva Kit.
 *
 * Twin of 01_imu_vibration_monitor.py
 * (embedded-systems-for-aiot-developer/examples/s07/01_imu_vibration_monitor.py),
 * written new, in the style of the library twin
 * aiot-s07-01-imu-vibration-monitor. Style: CLEAN_C.md.
 * What C changes only because the language forces it: the LEDs are
 * found by name as led_named() does, through bento_gpio_led_name() over
 * every LED the board has, falling back to LED 0 (led.on() and .off()
 * raise nothing in MicroPython, so their status is ignored); the
 * original's except OSError around the motion read is any failed read
 * here (the sample is skipped, as there); reals are bento_real_t, the
 * board MicroPython's float width; round(x, n) is
 * nearbyint(x * 10^n) / 10^n printed through bento_fmt (MicroPython's
 * str of a float). As in the original, the
 * LEDs are switched off only when the 200 windows end.
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_gpio.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* The measurement and the run. */
enum {
    WIN = 25,            /* samples per window */
    GAP_MS = 2,          /* between samples, squeezed short here */
    BASE_WINDOWS = 3,    /* windows for the baseline */
    WINDOWS = 200,       /* windows to watch */
    LOG_EVERY = 5,       /* a console line every 5 windows */
    PAUSE_MS = 30,       /* how often a paused chart reads the button */
    MILLI = 1000,        /* the chart counts in thousandths */
    CHART_HEADROOM = 60, /* room above the inspect line */
    PERCENT = 100,
    LAMP_COUNT = 3,
    RMS_DIGITS = 3,    /* round(x, 3) for an RMS value */
    DECIMAL_BASE = 10, /* round() scales by powers of ten */
    TEXT_BYTES = 128 /* the longest text built here, with its numbers */
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    LEFT_X = 24,
    TITLE_Y = 8,
    BUSY_Y = 288,
    CHART_Y = 56,
    CHART_W = 456,
    CHART_H = 216,
    RIGHT_X = 496,
    LEGEND_RMS_Y = 56,
    LEGEND_WARN_Y = 88,
    LEGEND_ALARM_Y = 120,
    RATIO_CAPTION_Y = 160,
    SEG_Y = 192,
    SEG_W = 192,
    SEG_H = 48,
    BAR_Y = 248,
    BAR_W = 272,
    BAR_H = 24,
    BUTTON_Y = 288,
    BUTTON_W = 192,
    BUTTON_H = 88,
    MIDDLE_X = 248,
    RUN_Y = 296,
    STATUS_Y = 336,
    BASE_Y = 280,
    BASE_NOTE_Y = 312,
    FONT_LARGE = 28,
    FONT_MEDIUM = 24,
    FONT_TEXT = 20
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* The lamps, in the original's LAMPS order. */
enum {
    LAMP_ALARM = 0,
    LAMP_WARN = 1,
    LAMP_NORMAL = 2
};

static const bento_real_t WARN_K = (bento_real_t)2.0;  /* watch */
static const bento_real_t ALARM_K = (bento_real_t)4.0; /* inspect */

/* The widgets the program changes, the LEDs and what it measured. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t busy;
    bento_ui_id_t chart;
    uint8_t rms_series;
    uint8_t warn_series;
    uint8_t alarm_series;
    bento_ui_id_t seg;
    bento_ui_id_t ratio_bar;
    bento_ui_id_t pause;
    bento_ui_id_t run;
    bento_ui_id_t status;
    uint32_t lamps[LAMP_COUNT]; /* alarm, warn, normal */
    bento_real_t base;
    bento_real_t warn_at;
    bento_real_t alarm_at;
    const char *shown_tag; /* the state the status label shows */
    bool paused;
} monitor_t;

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

static bento_real_t real_sqrt(bento_real_t value)
{
#if BENTO_FMT_REAL_DOUBLE
    return sqrt(value);
#else
    return sqrtf(value);
#endif
}

/* str(round(value, digits)) into text, which holds BENTO_FMT_BUF_SIZE
 * bytes: enough for every real, so the format cannot fail. */
static void format_round(char *text, bento_real_t value, int32_t digits)
{
    bento_real_t scale = (bento_real_t)1.0;
    uint32_t length = 0U;
    int32_t i;

    for (i = 0; i < digits; i++) {
        scale *= (bento_real_t)DECIMAL_BASE;
    }
    (void)bento_fmt_real(text, BENTO_FMT_BUF_SIZE,
                         (bento_real_t)nearbyint(value * scale) / scale,
                         &length);
}

/* True, with its index, when the board has an LED of this name. */
static bool find_led(const char *name, uint32_t *led)
{
    const uint32_t count = bento_gpio_num_leds();
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < count) && !found; i++) {
        const char *led_name = NULL;

        if ((bento_gpio_led_name(i, &led_name) == BENTO_OK) &&
            (led_name != NULL) && (strcmp(led_name, name) == 0)) {
            *led = i;
            found = true;
        }
    }
    return found;
}

/* The LED of the first name the board has (the index differs between
 * boards, the name does not), else LED 0; gpio.led() refuses an LED
 * the board lacks. */
static bento_status_t led_named(const char *first, const char *second,
                                uint32_t *led)
{
    bento_status_t status = BENTO_OK;

    if (!find_led(first, led) &&
        ((second == NULL) || !find_led(second, led))) {
        *led = 0U;
        if (bento_gpio_num_leds() == 0U) {
            status = BENTO_E_INVALID_ARGUMENT;
        }
    }
    return status;
}

/* Red, blue and green: RGB_RED / RGB_BLUE / RGB_GREEN on the Dev Kit,
 * LED1 / RGB_RED / LED2 on the Eva Kit. */
static bento_status_t find_lamps(monitor_t *monitor)
{
    uint32_t green = 0U;
    const bool has_rgb = find_led("RGB_GREEN", &green);
    bento_status_t status =
        led_named(has_rgb ? "RGB_RED" : "LED1", NULL,
                  &monitor->lamps[LAMP_ALARM]);

    if (status == BENTO_OK) {
        status = led_named("RGB_BLUE", "RGB_RED",
                           &monitor->lamps[LAMP_WARN]);
    }
    if (status == BENTO_OK) {
        status = led_named("RGB_GREEN", "LED2",
                           &monitor->lamps[LAMP_NORMAL]);
    }
    return status;
}

/* led.on() and led.off() raise nothing (modgpio.c), so their status
 * is ignored, as there. */
static void lamps_off(const monitor_t *monitor)
{
    int32_t i;

    for (i = 0; i < LAMP_COUNT; i++) {
        (void)bento_gpio_led_off(monitor->lamps[i]);
    }
}

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* One window gives one RMS value: of the part that differs from
 * gravity. A sample that cannot be read yet is skipped. */
static bento_status_t rms_window(bento_real_t *rms)
{
    const bento_real_t gravity = (bento_real_t)9.81;
    bento_real_t sum = (bento_real_t)0.0;
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < WIN) && (status == BENTO_OK); i++) {
        bento_sensors_imu6_t motion;

        bento_sensors_imu6_init(&motion);
        if (bento_sensors_bmi270_motion(&motion) == BENTO_OK) {
            const bento_real_t ax = (bento_real_t)motion.ax;
            const bento_real_t ay = (bento_real_t)motion.ay;
            const bento_real_t az = (bento_real_t)motion.az;
            const bento_real_t away =
                real_sqrt(((ax * ax) + (ay * ay)) + (az * az)) -
                gravity;

            sum += away * away;
        }
        status = wait_ms(GAP_MS);
    }
    *rms = real_sqrt(sum / (bento_real_t)WIN);
    return status;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const int16_t place[2], uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place[0];
    args.y = place[1];
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the twin
 * does too. */
static void set_text(bento_ui_t *ui, bento_ui_id_t label,
                     const char *text)
{
    (void)bento_ui_set_text(ui, label, text, (uint32_t)strlen(text));
}

/* ui.poll() whose events are not used; it answers no events when it
 * fails, so its status is not an error here. */
static void skip_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The first screen: the title and "taking the baseline". */
static bento_status_t build_busy_screen(monitor_t *monitor)
{
    static const int16_t title_place[2] = {LEFT_X, TITLE_Y};
    static const int16_t busy_place[2] = {LEFT_X, BUSY_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(monitor->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status =
            add_label(monitor->ui, "เฝ้าการสั่นของเครื่องจักร", title_place,
                      COLOR_TEXT, FONT_LARGE, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(monitor->ui, "กำลังเก็บเส้นฐาน 3 หน้าต่าง", busy_place,
                      COLOR_DIM, FONT_MEDIUM, &monitor->busy);
    }
    if (status == BENTO_OK) {
        skip_events(monitor->ui);
    }
    return status;
}

/* The baseline from three windows taken while the machine runs
 * normally, never less than 0.05, and the two lines from it. */
static bento_status_t take_baseline(monitor_t *monitor)
{
    const bento_real_t base_min = (bento_real_t)0.05;
    bento_real_t sum = (bento_real_t)0.0;
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < BASE_WINDOWS) && (status == BENTO_OK); i++) {
        bento_real_t rms = (bento_real_t)0.0;

        status = rms_window(&rms);
        sum += rms;
    }
    if (status == BENTO_OK) {
        const bento_real_t mean = sum / (bento_real_t)BASE_WINDOWS;
        char text[TEXT_BYTES];
        char first[BENTO_FMT_BUF_SIZE];
        char second[BENTO_FMT_BUF_SIZE];

        monitor->base = (mean > base_min) ? mean : base_min;
        monitor->warn_at = monitor->base * WARN_K;
        monitor->alarm_at = monitor->base * ALARM_K;
        format_round(first, monitor->base, RMS_DIGITS);
        (void)snprintf(text, sizeof(text), "เส้นฐาน RMS %s m/s2", first);
        /* lcd.print() ignores a console that is not ready (modlcd.c) */
        (void)bento_lcd_print(text);
        format_round(first, monitor->warn_at, 2);
        format_round(second, monitor->alarm_at, 2);
        (void)snprintf(text, sizeof(text), "เฝ้าดูที่ %s | เตือนที่ %s", first,
                       second);
        (void)bento_lcd_print(text);
        /* hide() ignores a refusal (modui.c) */
        (void)bento_ui_set_visible(monitor->ui, monitor->busy, 0U);
    }
    return status;
}

/* The chart: the RMS, the watch line and the inspect line. */
static bento_status_t add_chart(monitor_t *monitor)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = LEFT_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.color = COLOR_CARD;
    args.min_val = 0;
    args.max_val = (int32_t)(monitor->alarm_at * (bento_real_t)MILLI) +
                   CHART_HEADROOM;
    status = bento_ui_chart_create(monitor->ui, &args, &monitor->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(monitor->ui, monitor->chart,
                                           COLOR_ACCENT,
                                           &monitor->rms_series);
    }
    if (status == BENTO_OK) {
        status =
            bento_ui_chart_add_series(monitor->ui, monitor->chart,
                                      COLOR_DIM, &monitor->warn_series);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(monitor->ui, monitor->chart,
                                           COLOR_TEXT,
                                           &monitor->alarm_series);
    }
    return status;
}

/* The legend, the ratio Seg7 and the ratio bar on the right. */
static bento_status_t add_ratio(monitor_t *monitor)
{
    static const int16_t rms_place[2] = {RIGHT_X, LEGEND_RMS_Y};
    static const int16_t warn_place[2] = {RIGHT_X, LEGEND_WARN_Y};
    static const int16_t alarm_place[2] = {RIGHT_X, LEGEND_ALARM_Y};
    static const int16_t ratio_place[2] = {RIGHT_X, RATIO_CAPTION_Y};
    bento_ui_t *ui = monitor->ui;
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "เส้นที่ขยับ = RMS ตอนนี้", rms_place, COLOR_ACCENT,
                  FONT_TEXT, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "เส้นล่าง = เฝ้าดู 2 เท่า", warn_place,
                           COLOR_DIM, FONT_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เส้นบน = เข้าตรวจ 4 เท่า", alarm_place,
                           COLOR_TEXT, FONT_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "กี่เท่าของเส้นฐาน (0-4)", ratio_place,
                           COLOR_DIM, FONT_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = RIGHT_X;
        args.y = SEG_Y;
        args.w = SEG_W;
        args.h = SEG_H;
        args.color = COLOR_ACCENT;
        status = bento_ui_seg7_create(ui, &args, &monitor->seg);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = RIGHT_X;
        args.y = BAR_Y;
        args.w = BAR_W;
        args.h = BAR_H;
        args.color = COLOR_CARD;
        args.min_val = 0;
        args.max_val = (int32_t)(ALARM_K * (bento_real_t)PERCENT);
        status = bento_ui_bar_create(ui, &args, &monitor->ratio_bar);
    }
    return status;
}

/* The pause button, the run and status labels, and the baseline note. */
static bento_status_t add_controls(monitor_t *monitor)
{
    static const int16_t run_place[2] = {MIDDLE_X, RUN_Y};
    static const int16_t status_place[2] = {MIDDLE_X, STATUS_Y};
    static const int16_t base_place[2] = {RIGHT_X, BASE_Y};
    static const int16_t note_place[2] = {RIGHT_X, BASE_NOTE_Y};
    bento_ui_t *ui = monitor->ui;
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "หยุดกราฟ";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = LEFT_X;
    args.y = BUTTON_Y;
    args.w = BUTTON_W;
    args.h = BUTTON_H;
    args.color = COLOR_ACCENT;
    args.font_px = FONT_MEDIUM;
    status = bento_ui_button_create(ui, &args, &monitor->pause);
    if (status == BENTO_OK) {
        status = add_label(ui, "กำลังวัด", run_place, COLOR_DIM,
                           FONT_TEXT, &monitor->run);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ปกติ", status_place, COLOR_OK,
                           FONT_LARGE, &monitor->status);
    }
    if (status == BENTO_OK) {
        format_round(number, monitor->base, RMS_DIGITS);
        (void)snprintf(text, sizeof(text), "เส้นฐาน RMS %s", number);
        status = add_label(ui, text, base_place, COLOR_DIM, FONT_TEXT,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เก็บตอนเครื่องเดินปกติ", note_place,
                           COLOR_DIM, FONT_TEXT, &unused);
    }
    return status;
}

/* One tap on the pause button freezes or resumes the chart. */
static void toggle_pause(monitor_t *monitor)
{
    const bool paused = !monitor->paused;
    const char *button = paused ? "เดินกราฟต่อ" : "หยุดกราฟ";

    monitor->paused = paused;
    set_text(monitor->ui, monitor->pause, button);
    set_text(monitor->ui, monitor->run,
             paused ? "หยุดกราฟไว้" : "กำลังวัด");
    (void)bento_ui_set_color(monitor->ui, monitor->run,
                             paused ? (uint32_t)COLOR_WARN
                                    : (uint32_t)COLOR_DIM);
    (void)bento_lcd_print(paused ? "หยุดกราฟ" : "เดินกราฟต่อ");
}

/* True when the event is of this kind and came from this widget. */
static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* Reads the screen's events once; it must run while measuring and while
 * paused, or the button stops answering and the firmware takes the
 * program for dead. */
static void service_ui(monitor_t *monitor)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    /* ui.poll() answers no events when it fails */
    if (bento_ui_poll(monitor->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (event_is(&events[i], BENTO_UI_EVENT_CLICKED,
                     monitor->pause)) {
            toggle_pause(monitor);
        }
    }
}

/* Only one lamp lights: red to inspect, blue to watch, else green. */
static void show_state(monitor_t *monitor, bento_real_t ratio,
                       const char **tag)
{
    int32_t lamp = LAMP_NORMAL;
    uint32_t color = COLOR_OK;

    lamps_off(monitor);
    *tag = "ปกติ";
    if (ratio >= ALARM_K) {
        lamp = LAMP_ALARM;
        *tag = "ต้องเข้าตรวจ";
        color = COLOR_BAD;
    } else if (ratio >= WARN_K) {
        lamp = LAMP_WARN;
        *tag = "เฝ้าดู";
        color = COLOR_WARN;
    } else {
        /* normal: green, as set above */
    }
    (void)bento_gpio_led_on(monitor->lamps[lamp]);
    if (strcmp(*tag, monitor->shown_tag) != 0) {
        monitor->shown_tag = *tag;
        set_text(monitor->ui, monitor->status, *tag);
        (void)bento_ui_set_color(monitor->ui, monitor->status, color);
    }
}

/* The chart, the bar and the Seg7 for one window. */
static void show_window(const monitor_t *monitor, bento_real_t rms,
                        bento_real_t ratio)
{
    const bento_real_t milli = (bento_real_t)MILLI;
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];

    /* Chart.set_next() and the setters ignore a refusal (modui.c) */
    (void)bento_ui_chart_set_next(monitor->ui, monitor->chart,
                                  monitor->rms_series,
                                  (int32_t)(rms * milli));
    (void)bento_ui_chart_set_next(monitor->ui, monitor->chart,
                                  monitor->warn_series,
                                  (int32_t)(monitor->warn_at * milli));
    (void)bento_ui_chart_set_next(monitor->ui, monitor->chart,
                                  monitor->alarm_series,
                                  (int32_t)(monitor->alarm_at * milli));
    (void)bento_ui_set_value(monitor->ui, monitor->ratio_bar,
                             (int32_t)(ratio * (bento_real_t)PERCENT));
    format_round(number, ratio, 1);
    (void)snprintf(text, sizeof(text), "%sx", number);
    set_text(monitor->ui, monitor->seg, text);
}

/* One window measured, shown, lit and, every fifth, logged. */
static bento_status_t watch_window(monitor_t *monitor, int32_t window)
{
    bento_real_t rms = (bento_real_t)0.0;
    const char *tag = "";
    bento_status_t status = rms_window(&rms);

    if (status == BENTO_OK) {
        const bento_real_t ratio = rms / monitor->base;

        show_window(monitor, rms, ratio);
        show_state(monitor, ratio, &tag);
        if ((window % LOG_EVERY) == 0) {
            char rms_text[BENTO_FMT_BUF_SIZE];
            char ratio_text[BENTO_FMT_BUF_SIZE];
            char text[TEXT_BYTES];

            format_round(rms_text, rms, RMS_DIGITS);
            format_round(ratio_text, ratio, 1);
            (void)snprintf(text, sizeof(text),
                           "%" PRId32 ") RMS %s = %sx -> %s", window,
                           rms_text, ratio_text, tag);
            (void)bento_lcd_print(text);
        }
    }
    if (status == BENTO_OK) {
        service_ui(monitor);
    }
    return status;
}

/* WINDOWS windows; a paused chart only reads the button, every 30 ms. */
static bento_status_t run_windows(monitor_t *monitor)
{
    bento_status_t status = BENTO_OK;
    int32_t window = 0;

    while ((window < WINDOWS) && (status == BENTO_OK)) {
        if (monitor->paused) {
            service_ui(monitor);
            status = wait_ms(PAUSE_MS);
        } else {
            status = watch_window(monitor, window);
            window++;
        }
    }
    return status;
}

int main(void)
{
    monitor_t monitor;
    bento_status_t status;

    (void)memset(&monitor, 0, sizeof(monitor));
    monitor.ui = bento_runtime_get_ui();
    monitor.shown_tag = "";
    status = find_lamps(&monitor);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>เฝ้าการสั่นของเครื่องจักร</h2>");
        (void)bento_lcd_print("เก็บเส้นฐาน 3 หน้าต่าง ให้เครื่องเดินปกติ...");
        status = build_busy_screen(&monitor);
    }
    if (status == BENTO_OK) {
        status = take_baseline(&monitor);
    }
    if (status == BENTO_OK) {
        status = add_chart(&monitor);
    }
    if (status == BENTO_OK) {
        status = add_ratio(&monitor);
    }
    if (status == BENTO_OK) {
        status = add_controls(&monitor);
    }
    if (status == BENTO_OK) {
        skip_events(monitor.ui);
        status = run_windows(&monitor);
    }
    if (status == BENTO_OK) {
        lamps_off(&monitor);
        (void)bento_lcd_print(
            "<span class=muted>สัดส่วนกับเส้นฐาน เทียบข้ามเครื่อง</span>");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
