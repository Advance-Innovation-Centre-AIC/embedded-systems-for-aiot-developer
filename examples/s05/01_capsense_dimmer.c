/*
 * 01_capsense_dimmer.c -- สไลเดอร์สัมผัส = สวิตช์หรี่ไฟ
 *
 * ลากนิ้วบนแถบ CapSense แล้วไฟบนบอร์ดสว่างตาม กราฟวาดสองเส้น เส้นค่าดิบ
 * slider() กับเส้นระดับไฟที่สั่งจริง ปล่อยนิ้วแล้วไฟค้างที่ค่าสุดท้าย ไม่กลับ
 * เป็นศูนย์ ก่อนเริ่มมีรูปสาธิตให้เห็นรูปที่ต้องมองหา ไฟที่ใช้คือดวง RGB_*
 * ดวงแรกที่เฟิร์มแวร์รายงาน ลูปจริง 800 รอบ รอบละ 80 ms
 *
 * Drag a finger on the CapSense slider and an LED on the board follows.
 * The chart draws two lines: the raw slider() value and the level the
 * LED is really given. Lift the finger and the LED keeps the last
 * level, it does not fall back to zero. A demo shape comes first, so
 * the learner knows what to look for. The LED is the first RGB_* one
 * the firmware reports. The real loop runs 800 rounds of 80 ms.
 *
 * Twin of 01_capsense_dimmer.py
 * (embedded-systems-for-aiot-developer/examples/s05/01_capsense_dimmer.py),
 * rewritten from the library twin aiot-s05-01-capsense-dimmer. Style:
 * CLEAN_C.md. "except OSError" around slider() is a check for the
 * statuses the binding raises as OSError (E_IO, E_NOT_READY,
 * E_TIMEOUT); led.hold() takes the round's time, as in the original.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* Timing. */
enum {
    IDLE_ROUNDS =
        8, /* this many rounds unchanged: the finger is gone */
    CHART_MS =
        250,      /* a chart point this often: 50 points span seconds */
    LOOP_MS = 80, /* one round, and hold() itself takes that time */
    DEMO_STEP_MS = 40,
    ROUNDS = 800,
    DEMO_POINTS = 25
};

enum {
    AUTO = -1, /* no w= or h= given: the widget's own size */
    LEVEL_MAX = 100,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB: one accent on this screen. */
enum {
    COLOR_TEXT =
        0xE8EAED, /* main text, and the raw line on the chart */
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF
};

/* The widgets, in the order they are made. */
enum {
    W_TITLE,
    W_HINT,
    W_CHART,
    W_RAW_NOTE,
    W_DUTY_NOTE,
    W_LEVEL_CAPTION,
    W_SEG,
    W_BAR,
    W_STATE,
    W_SUB,
    W_COUNT
};

/* The demo: a made-up raw drag up and release, typed here, not read
 * from the sensor, so the learner sees the shape before dragging. */
static const int32_t DEMO[DEMO_POINTS] = {
    -1, -1, -1, -1, 5,  15, 28, 40, 52, 63, 74, 82, 88,
    88, 88, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
    uint8_t duty_series;
    uint32_t led;
    int32_t level;
    int32_t previous;
    int32_t idle;
} dimmer_t;

/* One widget as the original makes it: its factory, its text and its
 * keyword arguments. value= is a Label's or Button's font size and a
 * control's start value; min= and max= are a control's range (a
 * Panel's border colour and corner radius). */
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

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t widget,
                      const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_widget(bento_ui_t *ui,
                                 const widget_spec_t *spec,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    if (spec->text != NULL) {
        args.text = spec->text;
        args.text_len = (uint32_t)strlen(spec->text);
    }
    args.x = spec->x;
    args.y = spec->y;
    args.w = spec->w;
    args.h = spec->h;
    args.color = spec->color;
    args.font_px = spec->value;
    args.min_val = spec->min;
    args.max_val = spec->max;
    return bento_ui_create(ui, spec->type, &args, out_id);
}

/* Makes the widgets in table order (creation order is drawing order)
 * and keeps each id; the first refusal ends it, as a raise does in
 * MicroPython. */
static bento_status_t add_widgets(bento_ui_t *ui,
                                  const widget_spec_t *table,
                                  uint32_t count, bento_ui_id_t *ids)
{
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = add_widget(ui, &table[i], &ids[i]);
    }
    return status;
}

/* The screen on an 8 px grid with a 24 px margin. The chart's Y axis
 * starts at -10: the raw value can go below zero without a finger. A
 * line is named by what it does, not by its colour. A Seg7 takes no
 * value=: its digits come from h. */
static const widget_spec_t SCREEN_WIDGETS[W_COUNT] = {
    {UI_WIDGET_LABEL, "สไลเดอร์สัมผัส = สวิตช์หรี่ไฟ", 24, 8, AUTO, AUTO,
     COLOR_TEXT, 24, 0, 0},
    {UI_WIDGET_LABEL, "ลากนิ้วบนแถบ CapSense", 496, 14, AUTO, AUTO,
     COLOR_DIM, 20, 0, 0},
    {UI_WIDGET_CHART, NULL, 24, 56, 744, 152, COLOR_TEXT, 0, -10,
     LEVEL_MAX},
    {UI_WIDGET_LABEL, "เส้นที่ตกติดลบ = ค่าดิบ slider()", 24, 214, AUTO, AUTO,
     COLOR_TEXT, 20, 0, 0},
    {UI_WIDGET_LABEL, "เส้นที่ค้างระดับ = ไฟที่สั่งจริง", 24, 246, AUTO, AUTO,
     COLOR_ACCENT, 20, 0, 0},
    {UI_WIDGET_LABEL, "ความสว่าง (%)", 496, 214, AUTO, AUTO, COLOR_DIM,
     20, 0, 0},
    {UI_WIDGET_SEG7, NULL, 496, 248, 192, 56, COLOR_ACCENT, 0, 0, 0},
    {UI_WIDGET_BAR, NULL, 496, 312, 192, 32, COLOR_ACCENT, 0, 0,
     LEVEL_MAX},
    {UI_WIDGET_LABEL, "ปล่อยนิ้ว", 24, 288, AUTO, AUTO, COLOR_DIM, 28, 0,
     0},
    {UI_WIDGET_LABEL, "ค้างไว้ที่ 0%", 24, 336, AUTO, AUTO, COLOR_DIM, 20,
     0, 0}};

/* ui.poll() here only lets the screen draw; the events are not used. */
static void let_screen_draw(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The first RGB_* LED the firmware reports, seen on both boards (on
 * the Dev Kit LEDs 0-1 sit on the SoM); LED 0 when none is RGB_*. */
static bento_status_t pick_led(uint32_t *led)
{
    const uint32_t count = bento_gpio_num_leds();
    bento_status_t status = BENTO_OK;
    bool found = false;
    uint32_t i;

    *led = 0U;
    for (i = 0U; (i < count) && !found && (status == BENTO_OK); i++) {
        const char *name = NULL;

        status = bento_gpio_led_name(i, &name);
        if ((status == BENTO_OK) && (name != NULL) &&
            (strncmp(name, "RGB_", strlen("RGB_")) == 0)) {
            *led = i;
            found = true;
        }
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(*led);
    }
    return status;
}

static bento_status_t build_screen(dimmer_t *dimmer)
{
    bento_status_t status =
        bento_ui_screen(dimmer->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_widgets(dimmer->ui, SCREEN_WIDGETS, W_COUNT,
                             dimmer->ids);
    }
    if (status == BENTO_OK) {
        /* series 0 takes the chart's color= */
        status = bento_ui_chart_add_series(
            dimmer->ui, dimmer->ids[W_CHART], COLOR_ACCENT,
            &dimmer->duty_series);
    }
    if (status == BENTO_OK) {
        let_screen_draw(dimmer->ui);
    }
    return status;
}

static void print_led(uint32_t led)
{
    char text[TEXT_BYTES];
    const char *name = "";

    if (bento_gpio_led_name(led, &name) != BENTO_OK) {
        name = "";
    }
    (void)snprintf(text, sizeof(text), "ไฟที่ใช้: %s (ดวง %" PRIu32 " )",
                   name, led);
    /* lcd.print() ignores a console that is not ready */
    (void)bento_lcd_print(text);
}

/* Both chart lines get a point: the raw value and the LED level. */
static void chart_point(const dimmer_t *dimmer, int32_t raw,
                        int32_t level)
{
    (void)bento_ui_chart_set_next(dimmer->ui, dimmer->ids[W_CHART], 0U,
                                  raw);
    (void)bento_ui_chart_set_next(dimmer->ui, dimmer->ids[W_CHART],
                                  dimmer->duty_series, level);
}

static void show_level(const dimmer_t *dimmer, int32_t level)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, level);
    show_text(dimmer->ui, dimmer->ids[W_SEG], text);
    (void)bento_ui_set_value(dimmer->ui, dimmer->ids[W_BAR], level);
}

static void show_state(const dimmer_t *dimmer, const char *state,
                       uint32_t color)
{
    show_text(dimmer->ui, dimmer->ids[W_STATE], state);
    (void)bento_ui_set_color(dimmer->ui, dimmer->ids[W_STATE], color);
}

/* hold() takes the round's time, so no sleep is needed; it ends dark
 * at once on Stop, which is checked after it. */
static bento_status_t hold_led(const dimmer_t *dimmer, int32_t level,
                               int32_t duration_ms)
{
    bento_status_t status =
        bento_gpio_led_hold(dimmer->led, level, duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The demo first, to show how the two lines part when the finger
 * lifts. */
static bento_status_t run_demo(const dimmer_t *dimmer)
{
    bento_status_t status = BENTO_OK;
    int32_t demo_level = 0;
    uint32_t i;

    show_text(dimmer->ui, dimmer->ids[W_STATE], "รูปสาธิต ยังไม่ใช่ค่าจริง");
    show_text(dimmer->ui, dimmer->ids[W_SUB],
              "ลากขึ้น แล้วปล่อย - ดูเส้นที่ค้าง");
    for (i = 0U; (i < (uint32_t)DEMO_POINTS) && (status == BENTO_OK);
         i++) {
        if (DEMO[i] >= 0) {
            demo_level = DEMO[i];
        }
        chart_point(dimmer, DEMO[i], demo_level);
        show_level(dimmer, demo_level);
        let_screen_draw(dimmer->ui);
        status = hold_led(dimmer, demo_level, DEMO_STEP_MS);
    }
    return status;
}

/* A new value means a finger is dragging: an accent, not green, since
 * green only confirms that all is well. Unchanged for IDLE_ROUNDS
 * means the finger has left, not that it rests there. */
static void follow_finger(dimmer_t *dimmer, int32_t raw)
{
    char text[TEXT_BYTES];

    if (raw != dimmer->previous) {
        dimmer->idle = 0;
        dimmer->level = raw;
        dimmer->previous = raw;
        show_state(dimmer, "กำลังลาก", COLOR_ACCENT);
        (void)snprintf(text, sizeof(text), "ความสว่าง %" PRId32 "%%",
                       raw);
        show_text(dimmer->ui, dimmer->ids[W_SUB], text);
        show_level(dimmer, raw);
        (void)snprintf(text, sizeof(text), "ความสว่าง %" PRId32 " %%",
                       raw);
        (void)bento_lcd_print(text);
    } else {
        dimmer->idle++;
        if (dimmer->idle == IDLE_ROUNDS) {
            show_state(dimmer, "ปล่อยนิ้ว", COLOR_DIM);
            (void)snprintf(text, sizeof(text), "ค้างไว้ที่ %" PRId32 "%%",
                           dimmer->level);
            show_text(dimmer->ui, dimmer->ids[W_SUB], text);
            (void)snprintf(text, sizeof(text),
                           "ปล่อยนิ้ว - ค้างไว้ที่ %" PRId32 " %%",
                           dimmer->level);
            (void)bento_lcd_print(text);
        }
    }
}

/* The statuses the binding raises as OSError. */
static bool is_os_error(bento_status_t status)
{
    return (status == BENTO_E_IO) || (status == BENTO_E_NOT_READY) ||
           (status == BENTO_E_TIMEOUT);
}

static int32_t not_below_zero(int32_t level)
{
    return (level < 0) ? 0 : level;
}

/* One real round. After a reset the display core may not answer yet:
 * wait for the next round instead of letting the program die. */
static bento_status_t run_round(dimmer_t *dimmer, uint32_t *chart_ms)
{
    uint8_t position = 0U;
    bento_status_t status = bento_sensors_capsense_slider(&position);
    const int32_t raw = (int32_t)position;

    if (is_os_error(status)) {
        show_text(dimmer->ui, dimmer->ids[W_SUB], "รอคอร์จอตอบ");
        let_screen_draw(dimmer->ui);
        status = pause_ms(LOOP_MS);
    } else if (status == BENTO_OK) {
        follow_finger(dimmer, raw);
        /* the raw line keeps its values below zero: they are data */
        if (bento_time_ticks_diff(bento_time_ticks_ms(), *chart_ms) >=
            CHART_MS) {
            *chart_ms = bento_time_ticks_ms();
            chart_point(dimmer, raw, not_below_zero(dimmer->level));
        }
        let_screen_draw(dimmer->ui);
        /* every round, so the eye sees the level that stays */
        status =
            hold_led(dimmer, not_below_zero(dimmer->level), LOOP_MS);
    } else {
        /* not an OSError: it ends the program, as it would there */
    }
    return status;
}

static bento_status_t run_dimmer(dimmer_t *dimmer)
{
    uint32_t chart_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;
    int32_t round;

    show_state(dimmer, "ปล่อยนิ้ว", COLOR_DIM);
    show_text(dimmer->ui, dimmer->ids[W_SUB],
              "ค่าจริงจากเซนเซอร์ - ลากได้เลย");
    show_level(dimmer, 0);
    let_screen_draw(dimmer->ui);
    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = run_round(dimmer, &chart_ms);
    }
    return status;
}

static void show_end(const dimmer_t *dimmer)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "จบ - ค่าสุดท้าย %" PRId32 "%% ไม่กลับเป็นศูนย์",
                   dimmer->level);
    show_text(dimmer->ui, dimmer->ids[W_SUB], text);
    let_screen_draw(dimmer->ui);
    (void)snprintf(text, sizeof(text),
                   "ค่าสุดท้าย %" PRId32 " %% - ไม่กลับเป็นศูนย์",
                   dimmer->level);
    (void)bento_lcd_print(text);
}

int main(void)
{
    dimmer_t dimmer;
    bento_status_t status;

    (void)memset(&dimmer, 0, sizeof(dimmer));
    dimmer.ui = bento_runtime_get_ui();
    dimmer.previous = -1;
    status = pick_led(&dimmer.led);
    if (status == BENTO_OK) {
        status = build_screen(&dimmer);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>สไลเดอร์สัมผัส = สวิตช์หรี่ไฟ</h2>");
        (void)bento_lcd_print("ลากนิ้วบนสไลเดอร์ CapSense");
        print_led(dimmer.led);
        show_level(&dimmer, 0);
        status = run_demo(&dimmer);
    }
    if (status == BENTO_OK) {
        status = run_dimmer(&dimmer);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(dimmer.led);
    }
    if (status == BENTO_OK) {
        show_end(&dimmer);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
