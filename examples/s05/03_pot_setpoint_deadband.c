/*
 * 03_pot_setpoint_deadband.c -- ลูกบิดตั้งอุณหภูมิ กับแถบตาย
 *
 * ลูกบิด (potentiometer) ตั้งอุณหภูมิ 20-80 องศา ค่าดิบสั่นเล็กน้อยตลอดเวลา
 * จึงรายงานค่าใหม่เฉพาะเมื่อขยับเกินแถบตาย 2% และยึดปลายสเกล 3% เป็น 0 หรือ 100
 * กราฟวาดเส้นค่าดิบ (ต่อเนื่อง) กับค่าที่รายงาน (ขั้นบันได) เริ่มด้วยรูปสาธิต
 * ไฟแดงบอกตั้งสูง ไฟเขียวบอกตั้งต่ำ ตัวนับเทียบจำนวนครั้งที่อ่านกับที่รายงาน
 *
 * A knob (potentiometer) sets a temperature of 20-80 degrees. Its raw
 * value always jitters a little, so a new value is reported only when
 * it moves past a 2% dead band, and the last 3% at each end snap to 0
 * or 100. The chart draws the raw line (smooth) and the reported one
 * (steps), starting with a demo. A red LED means set high, a green one
 * set low; a counter compares reads with reports. 700 rounds of 80 ms.
 *
 * Twin of 03_pot_setpoint_deadband.py
 * (embedded-systems-for-aiot-developer/examples/s05/03_pot_setpoint_deadband.py),
 * rewritten from the library twin aiot-s05-03-pot-setpoint-deadband.
 * Style: CLEAN_C.md. Reals are bento_real_t, printed by bento_fmt as
 * the board's MicroPython prints them; round(x, 1) rounds half to
 * even as MicroPython's nearbyint does, without <math.h>.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_gpio.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add, or a value differs in the last digit. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

enum {
    SET_MIN = 20, /* the temperatures that can be set, degrees C */
    SET_MAX = 80,
    ROUNDS = 700,
    LOOP_MS = 80,
    DEMO_POINTS = 50,
    DEMO_TURN = 38, /* the hand still turns for the first points */
    JITTER_COUNT = 4,
    PERCENT_MAX = 100,
    AUTO = -1,       /* no w= or h= given: the widget's own size */
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* A move smaller than this is no move. */
static const bento_real_t DEAD_PCT = (bento_real_t)2.0;
/* Far from any reading, so the first one is always reported. */
static const bento_real_t NOT_SHOWN_YET = (bento_real_t)-999.0;

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED, /* main text, and the raw line */
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF /* the value reported: this lesson's hero */
};

/* The widgets, in the order they are made. */
enum {
    W_TITLE,
    W_CHART,
    W_RAW_NOTE,
    W_REPORT_NOTE,
    W_SET_CAPTION,
    W_SEG,
    W_KNOB_CAPTION,
    W_KNOB_BAR,
    W_KNOB_TEXT,
    W_COUNT_TEXT,
    W_COUNT
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
    uint8_t report_series;
    uint32_t led_high;
    uint32_t led_low;
    bento_real_t shown;
    int32_t changes;
    int32_t reads;
} knob_t;

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

/* The index of an LED by name, if the board reports it. */
static bool find_led(const char *wanted, uint32_t *led)
{
    const uint32_t count = bento_gpio_num_leds();
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < count) && !found; i++) {
        const char *name = NULL;

        if ((bento_gpio_led_name(i, &name) == BENTO_OK) &&
            (name != NULL) && (strcmp(name, wanted) == 0)) {
            *led = i;
            found = true;
        }
    }
    return found;
}

/* A board with RGB_RED, RGB_GREEN and RGB_BLUE (the Dev Kit) is asked
 * by name, as LED numbers may change in a later firmware; any other
 * (the Eva Kit: LED 0 red, LED 1 green) uses the numbers. */
static void pick_red_green(uint32_t *red, uint32_t *green)
{
    uint32_t blue = 0U;
    uint32_t red_found = 0U;
    uint32_t green_found = 0U;
    const bool full = find_led("RGB_RED", &red_found) &&
                      find_led("RGB_GREEN", &green_found) &&
                      find_led("RGB_BLUE", &blue);

    *red = full ? red_found : 0U;
    *green = full ? green_found : 1U;
}

/* ui.poll() here only lets the screen draw; the events are not used. */
static void let_screen_draw(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The ends a real knob never reaches snap to 0 or 100. */
static bento_real_t clamp_ends(bento_real_t pct)
{
    const bento_real_t edge_pct = (bento_real_t)3.0;
    const bento_real_t full = (bento_real_t)100.0;
    bento_real_t clamped = pct;

    if (pct < edge_pct) {
        clamped = (bento_real_t)0.0;
    } else if (pct > (full - edge_pct)) {
        clamped = full;
    } else {
        /* inside the scale: kept */
    }
    return clamped;
}

static bento_real_t to_setpoint(bento_real_t pct)
{
    const bento_real_t full = (bento_real_t)100.0;
    bento_real_t setpoint =
        ((bento_real_t)SET_MAX - (bento_real_t)SET_MIN) * pct;

    setpoint = setpoint / full;
    return (bento_real_t)SET_MIN + setpoint;
}

static bento_real_t distance(bento_real_t a, bento_real_t b)
{
    const bento_real_t gap = a - b;

    return (gap < (bento_real_t)0.0) ? -gap : gap;
}

/* round(x, 1) for x >= 0: nearbyint(x * 10) / 10, half to even. */
static bento_real_t round_tenth(bento_real_t value)
{
    const bento_real_t ten = (bento_real_t)10.0;
    const bento_real_t half = (bento_real_t)0.5;
    const bento_real_t scaled = value * ten;
    int32_t whole = (int32_t)scaled;
    const bento_real_t fraction = scaled - (bento_real_t)whole;

    if ((fraction > half) ||
        ((fraction == half) && ((whole % 2) != 0))) {
        whole++;
    }
    return (bento_real_t)whole / ten;
}

/* str() of a real, as the board's MicroPython writes it. */
static void real_text(bento_real_t value, char *text,
                      uint32_t text_bytes)
{
    uint32_t length = 0U;

    if (bento_fmt_real(text, text_bytes, value, &length) != BENTO_OK) {
        text[0] = '\0';
    }
}

/* The screen on an 8 px grid: one full-width chart, then the knob on
 * the left and the set point on the right. A line is named by its
 * shape, not its colour. The counter is not a state, so it is dim. */
static bento_status_t add_screen_widgets(bento_ui_t *ui, uint32_t first,
                                         uint32_t count,
                                         bento_ui_id_t *ids)
{
    static const widget_spec_t screen_widgets[W_COUNT] = {
        {UI_WIDGET_LABEL, "ลูกบิดตั้งอุณหภูมิ กับแถบตาย", 24, 8, AUTO, AUTO,
         COLOR_TEXT, 24, 0, 0},
        {UI_WIDGET_CHART, NULL, 24, 56, 744, 136, COLOR_TEXT, 0, 0,
         PERCENT_MAX},
        {UI_WIDGET_LABEL, "เส้นต่อเนื่อง = ค่าดิบจากลูกบิด", 24, 198, AUTO,
         AUTO, COLOR_TEXT, 20, 0, 0},
        {UI_WIDGET_LABEL, "เส้นขั้นบันได = ค่าที่รายงาน", 24, 230, AUTO, AUTO,
         COLOR_ACCENT, 20, 0, 0},
        {UI_WIDGET_LABEL, "ตั้งไว้ (องศา C)", 496, 198, AUTO, AUTO,
         COLOR_DIM, 20, 0, 0},
        {UI_WIDGET_SEG7, NULL, 496, 232, 192, 56, COLOR_ACCENT, 0, 0,
         0},
        {UI_WIDGET_LABEL, "ตำแหน่งลูกบิด", 24, 262, AUTO, AUTO, COLOR_DIM,
         20, 0, 0},
        {UI_WIDGET_BAR, NULL, 24, 304, 320, 32, COLOR_ACCENT, 0, 0,
         PERCENT_MAX},
        {UI_WIDGET_LABEL, "0 %", 376, 304, AUTO, AUTO, COLOR_DIM, 16, 0,
         0},
        {UI_WIDGET_LABEL, "อ่าน 0 ครั้ง / รายงาน 0 ครั้ง", 24, 344, AUTO,
         AUTO, COLOR_DIM, 20, 0, 0}};

    return add_widgets(ui, &screen_widgets[first], count, &ids[first]);
}

static bento_status_t build_screen(knob_t *knob)
{
    bento_status_t status =
        bento_ui_screen(knob->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_screen_widgets(knob->ui, 0U, W_COUNT, knob->ids);
    }
    if (status == BENTO_OK) {
        /* series 0 takes the chart's color= */
        status = bento_ui_chart_add_series(knob->ui, knob->ids[W_CHART],
                                           COLOR_ACCENT,
                                           &knob->report_series);
    }
    return status;
}

/* The demo first, so the shape shows from the first second: 38 points
 * of a hand turning 1.5% a step, below the dead band, so the report
 * moves in steps; then 12 points of jitter only (1.8% wide at most),
 * all swallowed, so the steps lie still. */
static void draw_demo(const knob_t *knob)
{
    static const bento_real_t jitter[JITTER_COUNT] = {
        (bento_real_t)0.9, (bento_real_t)0.3, (bento_real_t)-0.9,
        (bento_real_t)-0.3};
    const bento_real_t start = (bento_real_t)6.0;
    const bento_real_t step_pct = (bento_real_t)1.5;
    bento_real_t shown = NOT_SHOWN_YET;
    int32_t i;

    for (i = 0; i < DEMO_POINTS; i++) {
        const int32_t step = (i < DEMO_TURN) ? i : (DEMO_TURN - 1);
        bento_real_t raw = (bento_real_t)step * step_pct;

        raw = start + raw;
        raw = clamp_ends(raw + jitter[i % JITTER_COUNT]);
        if (distance(raw, shown) >= DEAD_PCT) {
            shown = raw;
        }
        (void)bento_ui_chart_set_next(knob->ui, knob->ids[W_CHART], 0U,
                                      (int32_t)raw);
        (void)bento_ui_chart_set_next(knob->ui, knob->ids[W_CHART],
                                      knob->report_series,
                                      (int32_t)shown);
    }
    let_screen_draw(knob->ui);
}

/* A new report: the set point, its line, and the LED that says high
 * or low without reading the number. */
static bento_status_t report(knob_t *knob, bento_real_t pct)
{
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    const bento_real_t setpoint = to_setpoint(pct);
    const bento_real_t middle =
        ((bento_real_t)SET_MIN + (bento_real_t)SET_MAX) /
        (bento_real_t)2.0;
    bento_status_t status;

    knob->shown = pct;
    knob->changes++;
    real_text(round_tenth(setpoint), number, sizeof(number));
    show_text(knob->ui, knob->ids[W_SEG], number);
    (void)snprintf(text, sizeof(text), "ตั้งไว้ %s C  ( %" PRId32 " %% )",
                   number, (int32_t)pct);
    /* lcd.print() ignores a console that is not ready */
    (void)bento_lcd_print(text);
    status = bento_gpio_led_off(knob->led_high);
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(knob->led_low);
    }
    if (status == BENTO_OK) {
        status = (setpoint > middle) ? bento_gpio_led_on(knob->led_high)
                                     : bento_gpio_led_on(knob->led_low);
    }
    return status;
}

/* One round; a failed read raises in the original, so it ends here. */
static bento_status_t run_round(knob_t *knob)
{
    char text[TEXT_BYTES];
    float percent = 0.0F;
    bento_status_t status = bento_sensors_pot_percent(&percent);
    const bento_real_t pct = clamp_ends((bento_real_t)percent);

    if (status == BENTO_OK) {
        knob->reads++;
        (void)bento_ui_set_value(knob->ui, knob->ids[W_KNOB_BAR],
                                 (int32_t)pct);
        (void)snprintf(text, sizeof(text), "%" PRId32 " %%",
                       (int32_t)pct);
        show_text(knob->ui, knob->ids[W_KNOB_TEXT], text);
        if (distance(pct, knob->shown) >= DEAD_PCT) {
            status = report(knob, pct);
        }
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "อ่าน %" PRId32 " ครั้ง / รายงาน %" PRId32 " ครั้ง",
                       knob->reads, knob->changes);
        show_text(knob->ui, knob->ids[W_COUNT_TEXT], text);
        let_screen_draw(knob->ui);
        status = pause_ms(LOOP_MS);
    }
    return status;
}

static void print_start(void)
{
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>ลูกบิดตั้งอุณหภูมิ</h2>");
    real_text(DEAD_PCT, number, sizeof(number));
    (void)snprintf(text, sizeof(text),
                   "แถบตาย %s %% | ช่วงตั้งได้ %d - %d C", number, SET_MIN,
                   SET_MAX);
    (void)bento_lcd_print(text);
}

int main(void)
{
    knob_t knob;
    char text[TEXT_BYTES];
    bento_status_t status;
    int32_t round;

    (void)memset(&knob, 0, sizeof(knob));
    knob.ui = bento_runtime_get_ui();
    knob.shown = NOT_SHOWN_YET;
    pick_red_green(&knob.led_high, &knob.led_low);
    print_start();
    status = build_screen(&knob);
    if (status == BENTO_OK) {
        draw_demo(&knob);
    }
    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = run_round(&knob);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(knob.led_high);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(knob.led_low);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "อ่าน %" PRId32 " ครั้ง | รายงานจริง %" PRId32
                       " ครั้ง",
                       knob.reads, knob.changes);
        (void)bento_lcd_print(text);
        (void)printf("ลองตั้ง DEAD_PCT = 0 แล้วดูว่าจำนวนรายงานพุ่งขึ้นกี่เท่า\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
