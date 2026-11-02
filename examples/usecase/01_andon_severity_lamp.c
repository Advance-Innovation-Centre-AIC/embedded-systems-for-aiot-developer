/*
 * 01_andon_severity_lamp.c -- เสาไฟสถานะแบบโรงงาน (andon light)
 *
 * วัดสนามแม่เหล็กของห้องเป็นเส้นฐาน แล้วเฝ้าดูว่าค่าเบี่ยงไปเท่าไร เบี่ยงน้อยกว่า
 * 40 uT คือปกติ (ไฟเขียว) ถึง 40 คือเฝ้าดู (ไฟน้ำเงิน) ถึง 120 คือผิดปกติ (ไฟแดง)
 * ติดได้ทีละดวงเท่านั้น โค้ดดับทุกดวงก่อนจุดดวงใหม่เสมอ การ์ดใหญ่บอกระดับเป็นคำ
 * และเปลี่ยนสี กราฟมีเส้นอ้างอิงสองเส้น วัด 300 รอบ รอบละ 100 ms แล้วกลับสู่ปกติ
 *
 * Measures the room's magnetic field as a baseline, then watches how
 * far it deviates: under 40 uT is normal (green), from 40 watch (blue),
 * from 120 fault (red). Only one lamp is lit at a time: every lamp goes
 * off before the new one lights. The big card names the level in its
 * colour, and the chart has the two reference lines. It measures 300
 * rounds of 100 ms, then returns to normal.
 *
 * Twin of 01_andon_severity_lamp.py
 * (embedded-systems-for-aiot-developer/examples/usecase/
 *  01_andon_severity_lamp.py), rewritten clean from the library twin
 * aiot-usecase-01-andon-severity-lamp.
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - LED_NAMES is bento_gpio_led_name() for every LED; lamp() answers
 *    an LED index, and table.index() of a missing name (ValueError)
 *    ends the program with BENTO_E_NOT_FOUND;
 *  - NAME and TINT (dicts keyed by the lamp) become a table indexed by
 *    the severity level; the level change is still tested on the lamp;
 *  - a float is bento_real_t (float on the PSoC Edge kits, double on
 *    the reTerminal), the width of the board's MicroPython float;
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
#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* The severity levels, in the order of NAME and TINT. */
enum {
    LEVEL_OK = 0,
    LEVEL_WARN = 1,
    LEVEL_FAULT = 2,
    LEVEL_COUNT = 3
};

/* Thresholds and timing. */
enum {
    WARN_UT = 40,   /* deviation from the baseline that means watch */
    FAULT_UT = 120, /* deviation that means a real fault */
    CHART_POINTS = 50,
    ROUNDS = 300,
    LOOP_MS = 100,
    NO_LAMP = -1, /* no lamp lit yet */
    EVA_RED = 0,  /* the Eva Kit's measured indexes: 0 red, 1 green, */
    EVA_GREEN = 1,
    EVA_BLUE = 2, /* 2 blue */
    TEXT_CHARS = 128
};

/* Colours, 0xRRGGBB; 0 leaves the widget's own colour. */
enum {
    COLOR_DEFAULT = 0,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_FAULT = 0xE5484D,
    COLOR_LINE = 0x4A9EFF,
    COLOR_CARD = 0x171B22
};

/* Layout on the 792 x 398 canvas: the chart and the status card on the
 * left, the legend, the level card, the bar and the Seg7 on the right.
 */
enum {
    LEFT_X = 12,
    RIGHT_X = 496,
    CARD_TEXT_X = 512,
    STATUS_TEXT_X = 24,
    UNIT_X = 664,
    TITLE_Y = 8,
    CHART_Y = 44,
    CHART_W = 472,
    CHART_H = 212,
    CHART_MIN = -10,
    CHART_MAX = 160,
    LEGEND_LINE_Y = 38,
    LEGEND_WARN_Y = 66,
    LEGEND_FAULT_Y = 94,
    LEVEL_CARD_Y = 124,
    LEVEL_CARD_W = 284,
    LEVEL_CARD_H = 88,
    LEVEL_CAPTION_Y = 130,
    LEVEL_Y = 160,
    BAR_CAPTION_Y = 218,
    BAR_Y = 244,
    BAR_W = 280,
    BAR_H = 20,
    SEG_Y = 276,
    SEG_W = 160,
    SEG_H = 40,
    UNIT_Y = 284,
    STATUS_CARD_Y = 292,
    STATUS_CARD_H = 44,
    STATUS_Y = 304,
    CARD_BORDER = 2,
    TITLE_PX = 24,
    LEVEL_PX = 28,
    TEXT_PX = 20
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

/* The lamps, the widgets the loop changes and the baseline. */
typedef struct {
    bento_ui_t *ui;
    uint32_t lamp[LEVEL_COUNT];
    bento_ui_id_t chart;
    uint8_t series_warn;
    uint8_t series_fault;
    bento_ui_id_t level_label;
    bento_ui_id_t bar;
    bento_ui_id_t seg;
    bento_ui_id_t status_label;
    bento_real_t base_ut;
} andon_t;

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

/* The index of the LED with this name, as LED_NAMES.index(name). */
static bento_status_t led_index(const char *name, uint32_t *index)
{
    const uint32_t led_count = bento_gpio_num_leds();
    bento_status_t status = BENTO_E_NOT_FOUND;
    uint32_t i;

    for (i = 0U; (i < led_count) && (status != BENTO_OK); i++) {
        const char *led_name = "";

        if ((bento_gpio_led_name(i, &led_name) == BENTO_OK) &&
            (strcmp(led_name, name) == 0)) {
            *index = i;
            status = BENTO_OK;
        }
    }
    return status;
}

/* The lamps by colour. A board that names all three RGB LEDs (the Dev
 * Kit) is trusted by name; the Eva Kit names only RGB_RED, which shines
 * blue, so its measured indexes are used instead. */
static bento_status_t find_lamps(uint32_t lamp[LEVEL_COUNT])
{
    bento_gpio_board_info_t info;
    uint32_t unused = 0U;
    bool named = false;
    bento_status_t status;

    bento_gpio_board_info_init(&info);
    status = bento_gpio_board_info(&info);
    lamp[LEVEL_OK] = EVA_GREEN;
    lamp[LEVEL_WARN] = EVA_BLUE;
    lamp[LEVEL_FAULT] = EVA_RED;
    if (status == BENTO_OK) {
        named = (led_index("RGB_GREEN", &unused) == BENTO_OK) &&
                (led_index("RGB_BLUE", &unused) == BENTO_OK);
    }
    if ((status == BENTO_OK) && named) {
        status = led_index("RGB_GREEN", &lamp[LEVEL_OK]);
    }
    if ((status == BENTO_OK) && named) {
        status = led_index("RGB_BLUE", &lamp[LEVEL_WARN]);
    }
    if ((status == BENTO_OK) && named) {
        status = led_index("RGB_RED", &lamp[LEVEL_FAULT]);
    }
    return status;
}

/* Every lamp off first, always: two lamps lit at once cannot be read.
 * Then the one lamp on. */
static bento_status_t set_lamp(uint32_t active)
{
    const uint32_t led_count = bento_gpio_num_leds();
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < led_count) && (status == BENTO_OK); i++) {
        status = bento_gpio_led_off(i);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_on(active);
    }
    return status;
}

/* The magnetic field's strength in uT. */
static bento_status_t field_strength(bento_real_t *strength_ut)
{
    float mx = 0.0F;
    float my = 0.0F;
    float mz = 0.0F;
    bento_status_t status =
        bento_sensors_bmm350_magnetic(&mx, &my, &mz);

    if (status == BENTO_OK) {
        const bento_real_t x = (bento_real_t)mx;
        const bento_real_t y = (bento_real_t)my;
        const bento_real_t z = (bento_real_t)mz;
        bento_real_t sum = x * x;

        sum += y * y; /* one operation per statement: see FP_CONTRACT */
        sum += z * z;
        *strength_ut = (bento_real_t)sqrt((double)sum);
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

/* A dark card with a thin border of the same colour. */
static bento_status_t add_card(bento_ui_t *ui, place_t place)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    place_args(&args, place);
    args.color = COLOR_CARD;
    args.min_val = COLOR_CARD;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &unused);
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

/* The title, the chart with its two reference series, and the legend.
 */
static bento_status_t add_chart(andon_t *andon)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        andon->ui, "เสาไฟสถานะ - andon light",
        (label_look_t){LEFT_X, TITLE_Y, COLOR_DEFAULT, TITLE_PX},
        &unused);

    if (status == BENTO_OK) {
        place_args(&args, (place_t){LEFT_X, CHART_Y, CHART_W, CHART_H});
        args.color = COLOR_LINE;
        args.min_val = CHART_MIN;
        args.max_val = CHART_MAX;
        status = bento_ui_chart_create(andon->ui, &args, &andon->chart);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            andon->ui, andon->chart, COLOR_WARN, &andon->series_warn);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            andon->ui, andon->chart, COLOR_FAULT, &andon->series_fault);
    }
    return status;
}

static bento_status_t add_legend(bento_ui_t *ui)
{
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        ui, "เส้นฟ้า = เบี่ยงเบนจริง",
        (label_look_t){RIGHT_X, LEGEND_LINE_Y, COLOR_LINE, TEXT_PX},
        &unused);

    if (status == BENTO_OK) {
        status = add_label(
            ui, "เส้นเหลือง = เฝ้าดู 40 uT",
            (label_look_t){RIGHT_X, LEGEND_WARN_Y, COLOR_WARN, TEXT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เส้นแดง = ผิดปกติ 120 uT",
                           (label_look_t){RIGHT_X, LEGEND_FAULT_Y,
                                          COLOR_FAULT, TEXT_PX},
                           &unused);
    }
    return status;
}

/* The card with the level in words. */
static bento_status_t add_level_card(andon_t *andon)
{
    bento_ui_id_t unused = 0U;
    bento_status_t status =
        add_card(andon->ui, (place_t){RIGHT_X, LEVEL_CARD_Y,
                                      LEVEL_CARD_W, LEVEL_CARD_H});

    if (status == BENTO_OK) {
        status = add_label(andon->ui, "ระดับตอนนี้",
                           (label_look_t){CARD_TEXT_X, LEVEL_CAPTION_Y,
                                          COLOR_DEFAULT, TEXT_PX},
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            andon->ui, "ปกติ",
            (label_look_t){CARD_TEXT_X, LEVEL_Y, COLOR_OK, LEVEL_PX},
            &andon->level_label);
    }
    return status;
}

/* The deviation against the fault line, as a bar and as a number; the
 * Seg7 gets "0" at once, or it would show its own 0000. */
static bento_status_t add_meter(andon_t *andon)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        andon->ui, "เบี่ยงเบนเทียบเส้นผิดปกติ",
        (label_look_t){RIGHT_X, BAR_CAPTION_Y, COLOR_DEFAULT, TEXT_PX},
        &unused);

    if (status == BENTO_OK) {
        place_args(&args, (place_t){RIGHT_X, BAR_Y, BAR_W, BAR_H});
        args.min_val = 0;
        args.max_val = FAULT_UT;
        args.font_px = 0;
        status = bento_ui_bar_create(andon->ui, &args, &andon->bar);
    }
    if (status == BENTO_OK) {
        place_args(&args, (place_t){RIGHT_X, SEG_Y, SEG_W, SEG_H});
        status = bento_ui_seg7_create(andon->ui, &args, &andon->seg);
    }
    if (status == BENTO_OK) {
        set_text(andon->ui, andon->seg, "0");
        status = add_label(
            andon->ui, "uT",
            (label_look_t){UNIT_X, UNIT_Y, COLOR_DEFAULT, TEXT_PX},
            &unused);
    }
    return status;
}

static bento_status_t build_screen(andon_t *andon)
{
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(andon->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = add_chart(andon);
    if (status == BENTO_OK) {
        status = add_legend(andon->ui);
    }
    if (status == BENTO_OK) {
        status = add_level_card(andon);
    }
    if (status == BENTO_OK) {
        status = add_meter(andon);
    }
    if (status == BENTO_OK) {
        status = add_card(andon->ui, (place_t){LEFT_X, STATUS_CARD_Y,
                                               CHART_W, STATUS_CARD_H});
    }
    if (status == BENTO_OK) {
        status = add_label(andon->ui, "กำลังวัดเส้นฐานของห้อง...",
                           (label_look_t){STATUS_TEXT_X, STATUS_Y,
                                          COLOR_DEFAULT, TEXT_PX},
                           &andon->status_label);
    }
    if (status == BENTO_OK) {
        poll_drop(andon->ui);
    }
    return status;
}

static void draw_reference_lines(const andon_t *andon)
{
    (void)bento_ui_chart_set_next(andon->ui, andon->chart,
                                  andon->series_warn, WARN_UT);
    (void)bento_ui_chart_set_next(andon->ui, andon->chart,
                                  andon->series_fault, FAULT_UT);
}

/* The two reference lines fill the window first, so the chart reads
 * from the first second; then the room's own baseline, since every
 * room's field differs. */
static bento_status_t measure_baseline(andon_t *andon)
{
    char text[TEXT_CHARS];
    int32_t point;
    bento_status_t status;

    for (point = 0; point < CHART_POINTS; point++) {
        draw_reference_lines(andon);
    }
    status = field_strength(&andon->base_ut);
    if (status == BENTO_OK) {
        const int32_t base = (int32_t)andon->base_ut;

        (void)snprintf(text, sizeof(text),
                       "เส้นฐานของห้องนี้: %" PRId32 " uT", base);
        (void)bento_lcd_print(text); /* modlcd.c ignores a refusal */
        (void)bento_lcd_print("<span class=muted>เขียว=ปกติ ฟ้า=เฝ้าดู "
                              "แดง=ผิดปกติ</span>");
        (void)snprintf(text, sizeof(text), "เส้นฐานห้องนี้ %" PRId32 " uT",
                       base);
        set_text(andon->ui, andon->status_label, text);
    }
    return status;
}

static int32_t level_of(bento_real_t deviation_ut)
{
    int32_t level = LEVEL_OK;

    if (deviation_ut >= (bento_real_t)FAULT_UT) {
        level = LEVEL_FAULT;
    } else if (deviation_ut >= (bento_real_t)WARN_UT) {
        level = LEVEL_WARN;
    } else {
        level = LEVEL_OK;
    }
    return level;
}

/* The chart, the bar and the Seg7 take every round. */
static void draw_deviation(const andon_t *andon,
                           bento_real_t deviation_ut)
{
    char text[TEXT_CHARS];
    const int32_t deviation = (int32_t)deviation_ut;
    const bento_real_t capped = (deviation_ut < (bento_real_t)FAULT_UT)
                                    ? deviation_ut
                                    : (bento_real_t)FAULT_UT;

    (void)bento_ui_chart_set_next(andon->ui, andon->chart, 0U,
                                  deviation);
    draw_reference_lines(andon);
    (void)bento_ui_set_value(andon->ui, andon->bar, (int32_t)capped);
    (void)snprintf(text, sizeof(text), "%" PRId32, deviation);
    set_text(andon->ui, andon->seg, text);
}

/* The lamp, the card and the LCD change only when the level does, so
 * the screen is not flooded. The word and the colour are per level. */
static bento_status_t show_level(const andon_t *andon, int32_t level,
                                 int32_t deviation)
{
    static const char *const level_name[LEVEL_COUNT] = {"ปกติ", "เฝ้าดู",
                                                        "ผิดปกติ"};
    static const uint32_t level_tint[LEVEL_COUNT] = {
        COLOR_OK, COLOR_WARN, COLOR_FAULT};
    char text[TEXT_CHARS];
    bento_status_t status = set_lamp(andon->lamp[level]);

    if (status == BENTO_OK) {
        set_text(andon->ui, andon->level_label, level_name[level]);
        (void)bento_ui_set_color(andon->ui, andon->level_label,
                                 level_tint[level]);
        (void)snprintf(text, sizeof(text),
                       "เส้นฐาน %" PRId32 " uT | เบี่ยง %" PRId32,
                       (int32_t)andon->base_ut, deviation);
        set_text(andon->ui, andon->status_label, text);
        (void)snprintf(text, sizeof(text),
                       "ระดับเปลี่ยน | เบี่ยงเบน %" PRId32 " uT", deviation);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* One round: the deviation, its level, the screen, and the lamp when
 * the level changed. */
static bento_status_t watch_round(const andon_t *andon,
                                  int32_t *last_lamp)
{
    bento_real_t strength_ut = (bento_real_t)0;
    bento_status_t status = field_strength(&strength_ut);

    if (status == BENTO_OK) {
        const bento_real_t deviation_ut =
            real_abs(strength_ut - andon->base_ut);
        const int32_t level = level_of(deviation_ut);
        const int32_t lamp = (int32_t)andon->lamp[level];

        draw_deviation(andon, deviation_ut);
        if (lamp != *last_lamp) {
            status = show_level(andon, level, (int32_t)deviation_ut);
            *last_lamp = lamp;
        }
    }
    if (status == BENTO_OK) {
        poll_drop(andon->ui);
        status = pause_ms(LOOP_MS);
    }
    return status;
}

static bento_status_t run_rounds(const andon_t *andon)
{
    int32_t last_lamp = NO_LAMP;
    bento_status_t status = BENTO_OK;
    int32_t round;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = watch_round(andon, &last_lamp);
    }
    return status;
}

/* After the rounds the lamp tower returns to normal. */
static bento_status_t finish(const andon_t *andon)
{
    bento_status_t status = set_lamp(andon->lamp[LEVEL_OK]);

    if (status == BENTO_OK) {
        set_text(andon->ui, andon->level_label, "ปกติ");
        (void)bento_ui_set_color(andon->ui, andon->level_label,
                                 COLOR_OK);
        set_text(andon->ui, andon->status_label,
                 "จบรอบวัด - เสาไฟกลับสู่สถานะปกติ");
        (void)bento_lcd_print(
            "<span class=ok>จบรอบวัด - กลับสู่ปกติ</span>");
        (void)printf("เสาไฟกลับสู่สถานะปกติ\n");
    }
    return status;
}

int main(void)
{
    andon_t andon;
    bento_status_t status;

    (void)memset(&andon, 0, sizeof(andon));
    andon.ui = bento_runtime_get_ui();
    status = find_lamps(andon.lamp);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear(); /* lcd calls ignore a refusal */
        (void)bento_lcd_console("<h2>เสาไฟสถานะ - andon light</h2>");
        status = build_screen(&andon);
    }
    if (status == BENTO_OK) {
        status = measure_baseline(&andon);
    }
    if (status == BENTO_OK) {
        status = run_rounds(&andon);
    }
    if (status == BENTO_OK) {
        status = finish(&andon);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
