/*
 * 04_magnet_presence.c -- ตรวจว่ามีแม่เหล็กอยู่ใกล้หรือไม่ (คาบ 08)
 *
 * ตอนเริ่ม เก็บขนาดสนามแม่เหล็กสามแกนจาก BMM350 ยี่สิบครั้ง (ห่างกัน 60 ms)
 * เป็นเส้นฐานของห้องนี้ แล้วคิดเกณฑ์จากความผันผวนของห้องเอง (6 เท่า
 * อย่างน้อย 8) จากนั้น 600 รอบ รอบละ 80 ms กราฟเส้นฟ้าคือค่าเบี่ยงจาก
 * เส้นฐาน เส้นแดงคือเกณฑ์ Seg7 กับแถบบอกค่าเบี่ยง ป้ายล่างเปลี่ยนเป็น
 * "พบแม่เหล็ก" ตอนเส้นฟ้าข้ามเส้นแดง วางบอร์ดนิ่งตอนเริ่ม
 * แล้วเอาแม่เหล็กเข้าใกล้ บอร์ด: Dev Kit และ Eva Kit
 *
 * At the start it reads the size of the BMM350's three-axis field 20
 * times, 60 ms apart, as this room's baseline, and works out the
 * threshold from the room's own spread (6 times it, at least 8). Then
 * for 600 rounds of 80 ms the blue line is the deviation from the
 * baseline, the red line the threshold, a Seg7 and a bar show the
 * deviation, and the status label turns to "magnet found" when blue
 * crosses red. Keep the board still at the start, then bring a magnet
 * near. Boards: Dev Kit and Eva Kit.
 *
 * Twin of 04_magnet_presence.py
 * (embedded-systems-for-aiot-developer/examples/s08/04_magnet_presence.py),
 * started from the library twin aiot-s08-04-magnet-presence. Style:
 * CLEAN_C.md.
 * What C changes only because the language forces it: the list of
 * baseline samples is an array of 20; reals are bento_real_t, the board
 * MicroPython's float width; round(spread, 1) is nearbyint(spread * 10)
 * / 10 printed through bento_fmt (MicroPython's str of a float); a
 * failed magnetometer read ends the program with its status, as the
 * uncaught OSError ends the original.
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* The baseline, the run and the texts. */
enum {
    SAMPLES = 20,   /* samples for the baseline */
    BASE_MS = 60,   /* between baseline samples */
    LOOP_MS = 80,   /* one detection round */
    ROUNDS = 600,   /* detection rounds */
    Y_MIN_TOP = 20, /* the chart's top is at least this */
    PERCENT = 100,
    DEVIATION_SERIES = 0, /* the Chart is made with series 0 */
    TEXT_BYTES = 96 /* the longest text built here, with its numbers */
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 12,
    TITLE_Y = 8,
    RIGHT_X = 496,
    LEGEND_DEVIATION_Y = 40,
    LEGEND_THRESHOLD_Y = 68,
    NOW_CAPTION_Y = 98,
    SEG_Y = 124,
    SEG_W = 180,
    SEG_H = 44,
    BAR_CAPTION_X = 484,
    BAR_CAPTION_Y = 178,
    BAR_Y = 216,
    BAR_H = 16,
    PANEL_X = 12,
    PANEL_Y = 268,
    PANEL_W = 472,
    PANEL_H = 68,
    STATUS_X = 24,
    STATUS_Y = 276,
    SUB_Y = 304,
    CHART_X = 12,
    CHART_Y = 44,
    CHART_W = 472,
    CHART_H = 212,
    FONT_TITLE = 24,
    FONT_TEXT = 20
};

/* Colours, 0xRRGGBB; 0 is the theme's colour. */
enum {
    COLOR_THEME = 0,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_BAD = 0xE5484D,
    COLOR_THRESHOLD = 0xFF5555, /* the threshold line */
    COLOR_FOUND = 0x55DD55,
    COLOR_QUIET = 0x9AA0A6
};

static const char TEXT_NOT_FOUND[] = "ยังไม่พบแม่เหล็ก";

/* The widgets the program changes, and what the detector knows. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t seg;
    bento_ui_id_t bar;
    bento_ui_id_t status;
    bento_ui_id_t sub;
    bento_ui_id_t chart;
    uint8_t threshold_series;
    bento_real_t mean;      /* the room's baseline */
    bento_real_t threshold; /* the deviation that counts as a magnet */
    bool present;
} detector_t;

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

static bento_real_t real_fabs(bento_real_t value)
{
#if BENTO_FMT_REAL_DOUBLE
    return fabs(value);
#else
    return fabsf(value);
#endif
}

/* int() of a real: truncates toward zero, as the cast does. A function,
 * so that the cast never applies to a composite expression (MISRA 10.8). */
static int32_t truncate_to_int(bento_real_t value)
{
    return (int32_t)value;
}

/* The three-axis field's size, in the board's units (unverified, so
 * the program measures against its own baseline instead). */
static bento_status_t magnitude(bento_real_t *out)
{
    float x_axis = 0.0F;
    float y_axis = 0.0F;
    float z_axis = 0.0F;
    const bento_status_t status =
        bento_sensors_bmm350_magnetic(&x_axis, &y_axis, &z_axis);

    if (status == BENTO_OK) {
        const bento_real_t x = (bento_real_t)x_axis;
        const bento_real_t y = (bento_real_t)y_axis;
        const bento_real_t z = (bento_real_t)z_axis;

        *out = real_sqrt(((x * x) + (y * y)) + (z * z));
    }
    return status;
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

/* ui.poll() is called as the original does; the events are not used,
 * and it answers no events when it fails, so its status is not an
 * error here. */
static void skip_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the twin
 * does too. */
static void set_text(bento_ui_t *ui, bento_ui_id_t label,
                     const char *text)
{
    (void)bento_ui_set_text(ui, label, text, (uint32_t)strlen(text));
}

static void show_state(const detector_t *detector, const char *text,
                       uint32_t color)
{
    set_text(detector->ui, detector->status, text);
    (void)bento_ui_set_color(detector->ui, detector->status, color);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const int16_t place[2], int32_t font_px,
                                uint32_t color, bento_ui_id_t *out_id)
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

/* Where a widget sits and how big it is. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} box_t;

/* How a Seg7, a Bar or a Panel is made: all three take a box. */
typedef bento_ui_status_t (*create_t)(
    bento_ui_t *ui, const bento_ui_widget_args_t *args,
    bento_ui_id_t *out_id);

/* A widget in this box; top is max= (100 for the bar, else 0). */
static bento_status_t add_box(bento_ui_t *ui, create_t create,
                              const box_t *box, int32_t top,
                              bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = box->x;
    args.y = box->y;
    args.w = box->w;
    args.h = box->h;
    args.min_val = 0;
    args.max_val = top;
    return create(ui, &args, out_id);
}

/* The title and the right column: the legend, the deviation now and
 * the bar against the threshold. */
static bento_status_t add_readouts(detector_t *detector)
{
    static const int16_t title_place[2] = {TITLE_X, TITLE_Y};
    static const int16_t deviation_place[2] = {RIGHT_X,
                                               LEGEND_DEVIATION_Y};
    static const int16_t threshold_place[2] = {RIGHT_X,
                                               LEGEND_THRESHOLD_Y};
    static const int16_t now_place[2] = {RIGHT_X, NOW_CAPTION_Y};
    static const int16_t bar_caption_place[2] = {BAR_CAPTION_X,
                                                 BAR_CAPTION_Y};
    static const box_t seg_box = {RIGHT_X, SEG_Y, SEG_W, SEG_H};
    static const box_t bar_box = {RIGHT_X, BAR_Y, SEG_W, BAR_H};
    bento_ui_t *ui = detector->ui;
    bento_ui_id_t unused;
    bento_status_t status = add_label(ui, "ตัวตรวจจับแม่เหล็ก", title_place,
                                      FONT_TITLE, COLOR_THEME, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "ฟ้า = เบี่ยงจากเส้นฐาน", deviation_place,
                           FONT_TEXT, COLOR_ACCENT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "แดง = เกณฑ์ที่คำนวณได้", threshold_place,
                           FONT_TEXT, COLOR_BAD, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เบี่ยงตอนนี้ (หน่วยบอร์ด)", now_place,
                           FONT_TEXT, COLOR_THEME, &unused);
    }
    if (status == BENTO_OK) {
        status = add_box(ui, bento_ui_seg7_create, &seg_box, 0,
                         &detector->seg);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "เทียบเกณฑ์ (100% = ถึงเกณฑ์)", bar_caption_place,
                      FONT_TEXT, COLOR_THEME, &unused);
    }
    if (status == BENTO_OK) {
        status = add_box(ui, bento_ui_bar_create, &bar_box, PERCENT,
                         &detector->bar);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it); the chart comes later, once the threshold is
 * known. */
static bento_status_t build_screen(detector_t *detector)
{
    static const box_t panel_box = {PANEL_X, PANEL_Y, PANEL_W, PANEL_H};
    static const int16_t status_place[2] = {STATUS_X, STATUS_Y};
    static const int16_t sub_place[2] = {STATUS_X, SUB_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(detector->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_readouts(detector);
    }
    if (status == BENTO_OK) {
        status = add_box(detector->ui, bento_ui_panel_create,
                         &panel_box, 0, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(detector->ui, "กำลังเก็บเส้นฐาน", status_place,
                           FONT_TITLE, COLOR_THEME, &detector->status);
    }
    if (status == BENTO_OK) {
        status = add_label(detector->ui, "อย่าขยับบอร์ด", sub_place,
                           FONT_TEXT, COLOR_THEME, &detector->sub);
    }
    if (status == BENTO_OK) {
        skip_events(detector->ui);
    }
    return status;
}

/* SAMPLES readings while the board lies still; the bar shows the
 * progress. */
static bento_status_t read_samples(const detector_t *detector,
                                   bento_real_t samples[SAMPLES])
{
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < SAMPLES) && (status == BENTO_OK); i++) {
        status = magnitude(&samples[i]);
        if (status == BENTO_OK) {
            const bento_real_t done =
                (bento_real_t)((i + 1) * PERCENT) /
                (bento_real_t)SAMPLES;

            (void)bento_ui_set_value(detector->ui, detector->bar,
                                     (int32_t)done);
            skip_events(detector->ui);
            status = wait_ms(BASE_MS);
        }
    }
    return status;
}

/* The two console lines about the baseline and the threshold. */
static void report_baseline(const detector_t *detector,
                            bento_real_t spread)
{
    const bento_real_t tenths = (bento_real_t)10.0;
    const bento_real_t rounded =
        (bento_real_t)nearbyint(spread * tenths) / tenths;
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint32_t length = 0U;

    /* BENTO_FMT_BUF_SIZE holds every real, so the format cannot fail */
    (void)bento_fmt_real(number, BENTO_FMT_BUF_SIZE, rounded, &length);
    (void)snprintf(text, sizeof(text), "เส้นฐาน %" PRId32 " | ผันผวน %s",
                   (int32_t)detector->mean, number);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "เกณฑ์ที่คำนวณได้: %" PRId32 " (หน่วยบอร์ด)",
                   (int32_t)detector->threshold);
    (void)bento_lcd_print(text);
}

/* Both the mean and the spread of this room: the threshold comes from
 * the room itself, never from a guessed number. */
static bento_status_t take_baseline(detector_t *detector)
{
    /* deviations of background noise that count */
    const bento_real_t threshold_k = (bento_real_t)6.0;
    /* a floor for a room so quiet the noise is nearly 0 */
    const bento_real_t floor_deviation = (bento_real_t)8.0;
    bento_real_t samples[SAMPLES];
    bento_status_t status = read_samples(detector, samples);

    if (status == BENTO_OK) {
        bento_real_t sum = (bento_real_t)0.0;
        bento_real_t spread_sum = (bento_real_t)0.0;
        bento_real_t spread;
        int32_t i;

        for (i = 0; i < SAMPLES; i++) {
            sum += samples[i];
        }
        detector->mean = sum / (bento_real_t)SAMPLES;
        for (i = 0; i < SAMPLES; i++) {
            spread_sum += real_fabs(samples[i] - detector->mean);
        }
        spread = spread_sum / (bento_real_t)SAMPLES;
        detector->threshold = ((spread * threshold_k) > floor_deviation)
                                  ? (spread * threshold_k)
                                  : floor_deviation;
        report_baseline(detector, spread);
    }
    return status;
}

/* The chart, its top above the threshold so the found line is never
 * cut off, and the status lines for the detection rounds. */
static bento_status_t show_threshold(detector_t *detector)
{
    const bento_real_t headroom = (bento_real_t)2.5;
    const int32_t top = truncate_to_int(detector->threshold * headroom);
    bento_ui_widget_args_t args;
    char text[TEXT_BYTES];
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CHART_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.min_val = 0;
    args.max_val = (top > Y_MIN_TOP) ? top : Y_MIN_TOP;
    status =
        bento_ui_chart_create(detector->ui, &args, &detector->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            detector->ui, detector->chart, COLOR_THRESHOLD,
            &detector->threshold_series);
    }
    if (status == BENTO_OK) {
        show_state(detector, TEXT_NOT_FOUND, COLOR_QUIET);
        (void)snprintf(
            text, sizeof(text), "เส้นฐาน %" PRId32 " | เกณฑ์ %" PRId32,
            (int32_t)detector->mean, (int32_t)detector->threshold);
        set_text(detector->ui, detector->sub, text);
        skip_events(detector->ui);
    }
    return status;
}

/* Said only when the state changes, not every round. */
static void report_change(detector_t *detector, bool now,
                          bento_real_t deviation)
{
    if (now != detector->present) {
        detector->present = now;
        if (now) {
            char text[TEXT_BYTES];

            show_state(detector, "พบแม่เหล็ก", COLOR_FOUND);
            (void)snprintf(text, sizeof(text),
                           "<span class=ok>พบแม่เหล็ก - เบี่ยง %" PRId32
                           "</span>",
                           (int32_t)deviation);
            (void)bento_lcd_print(text);
        } else {
            show_state(detector, TEXT_NOT_FOUND, COLOR_QUIET);
            (void)bento_lcd_print(
                "<span class=muted>แม่เหล็กออกไปแล้ว</span>");
        }
    }
}

/* One detection round: the deviation onto the chart, the Seg7 and the
 * bar, and the state when it changed. */
static bento_status_t run_round(detector_t *detector)
{
    const bento_real_t full_scale = (bento_real_t)PERCENT;
    bento_real_t field = (bento_real_t)0.0;
    bento_status_t status = magnitude(&field);

    if (status == BENTO_OK) {
        const bento_real_t deviation =
            real_fabs(field - detector->mean);
        const bento_real_t percent =
            (deviation / detector->threshold) * full_scale;
        char text[TEXT_BYTES];

        (void)bento_ui_chart_set_next(detector->ui, detector->chart,
                                      DEVIATION_SERIES,
                                      (int32_t)deviation);
        /* a horizontal line at the threshold, for the eye to compare */
        (void)bento_ui_chart_set_next(detector->ui, detector->chart,
                                      detector->threshold_series,
                                      (int32_t)detector->threshold);
        (void)snprintf(text, sizeof(text), "%" PRId32,
                       (int32_t)deviation);
        set_text(detector->ui, detector->seg, text);
        (void)bento_ui_set_value(
            detector->ui, detector->bar,
            (int32_t)((percent < full_scale) ? percent : full_scale));
        report_change(detector, deviation >= detector->threshold,
                      deviation);
        skip_events(detector->ui);
        status = wait_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    detector_t detector;
    bento_status_t status;

    (void)memset(&detector, 0, sizeof(detector));
    detector.ui = bento_runtime_get_ui();
    status = build_screen(&detector);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ตัวตรวจจับแม่เหล็ก</h2>");
        (void)bento_lcd_print("กำลังเก็บเส้นฐาน อย่าขยับบอร์ด...");
        status = take_baseline(&detector);
    }
    if (status == BENTO_OK) {
        status = show_threshold(&detector);
    }
    if (status == BENTO_OK) {
        int32_t round;

        for (round = 0; (round < ROUNDS) && (status == BENTO_OK);
             round++) {
            status = run_round(&detector);
        }
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_print("เกณฑ์นี้คิดจากห้องนี้ ย้ายห้องแล้วรันใหม่");
        (void)printf("เกณฑ์คำนวณจากห้องนี้ ไม่ใช่ตัวเลขที่เดามา "
                     "ย้ายห้องแล้วรันใหม่ได้เลย\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
