/*
 * ex05_sensor_dashboard.c -- แดชบอร์ดเซนเซอร์สามแท็บ (Sensor Dashboard, Dev Kit)
 *
 * แท็บ ADC แสดงลูกบิดเป็นแถบและแรงดัน แท็บ Accel แสดงความเร่ง BMI270
 * เป็นกราฟสามเส้นพร้อมตัวเลข และแท็บ Gyro แสดงความเร็วเชิงมุมเป็นส่วนโค้ง
 * สามวง ข้อมูลจริงทุกแท็บ อ่านทุก 100 ms ทำงาน 3 นาทีแล้วจบ
 * และวาดแถบ "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * The ADC tab shows the knob as a bar and a voltage, the Accel tab the
 * BMI270's acceleration as a three-line chart with numbers, and the
 * Gyro tab the turn rates as three arcs. Every tab shows real data. The
 * program reads every 100 ms, ends after three minutes, and then draws
 * a "finished" band over the bottom rows.
 *
 * C twin of ex05_sensor_dashboard.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/devkit/ex05_sensor_dashboard.py), from the
 * library twin aiot-lvgl-sec3-devkit-ex05-sensor-dashboard.
 * Style: CLEAN_C.md. Left out on purpose: the "< Menu" button, which the
 * original shows only when the board's own menu starts it (MENU_MODE);
 * a C program is never started from that menu. The touch events are
 * still read every round. The round counter n, which nothing reads, is
 * left out too. What C changes: Python's int is int32_t here.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_pots.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* Layout: each tab's own coordinates, the footer on the screen. */
enum {
    TITLE_FONT_PX = 24,
    AXIS_FONT_PX = 20,
    FOOTER_FONT_PX = 14,
    ADC_TITLE_X = 250,
    ADC_TITLE_Y = 33,
    ADC_BAR_X = 90,
    ADC_BAR_Y = 120,
    ADC_BAR_W = 450,
    ADC_BAR_H = 50,
    ADC_TEXT_X = 190,
    ADC_TEXT_Y = 200,
    ACCEL_TITLE_X = 190,
    GYRO_TITLE_X = 230,
    TAB_TITLE_Y = 4,
    CHART_X = 60,
    CHART_Y = 48,
    CHART_W = 400,
    CHART_H = 175,
    CHART_MIN = -200,
    CHART_MAX = 1200,
    ACCEL_TEXT_X = 560,
    ACCEL_TEXT_Y = 40,
    ACCEL_TEXT_STEP = 55,
    ARC_CENTER_X = 230, /* the middle arc's column */
    ARC_STEP = 155,
    ARC_DX = 90,
    ARC_Y = 60,
    ARC_SIZE = 140,
    ARC_NAME_DX = 152,
    ARC_NAME_Y = 210,
    FOOTER_X = 180,
    FOOTER_Y = 372
};

/* The "finished" band over the bottom rows. */
enum {
    END_Y = 330,
    END_W = 792,
    END_H = 36,
    END_TEXT_X = 232,
    END_TEXT_Y = 334,
    END_FONT_PX = 20
};

/* The tabs, the axes and the readings. */
enum {
    TABS = 3,
    TAB_ADC = 0,
    TAB_ACCEL = 1,
    TAB_GYRO = 2,
    AXES = 3,
    POT_VR1 = 0,    /* the Dev Kit's VR1, as printed on the board */
    ADC_MAX = 4095, /* the ADC's full scale */
    PERCENT = 100,
    CHART_SCALE = 100, /* m/s^2 x100 on the chart */
    ACCEL_DIGITS = 1,
    VOLT_DIGITS = 2,
    ARC_MIDDLE = 50,    /* no turn: the arc stands at half */
    GYRO_PER_STEP = 5,  /* 5 dps move the arc by one step */
    ADC_TEXT_BYTES = 56 /* "ADC: ", an int32_t, " (", a real, "V)" */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,
    RUN_MS = 180000 /* the example ends after three minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TAB = 0xFFFFFF,
    COLOR_INK = 0x111111,
    COLOR_BAR = 0x1565C0,
    COLOR_X = 0xF44336,
    COLOR_Y = 0x4CAF50,
    COLOR_Z = 0x2196F3,
    COLOR_FOOTER = 0x666666
};

/* The "finished" band's colours, 0xRRGGBB. */
enum {
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

static const char FOOTER[] =
    "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";

static const char *const TAB_NAMES[TABS] = {"ADC", "Accel", "Gyro"};
static const char *const AXIS_NAMES[AXES] = {"X", "Y", "Z"};
static const uint32_t AXIS_COLORS[AXES] = {COLOR_X, COLOR_Y, COLOR_Z};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t tabs[TABS];
    bento_ui_id_t adc_bar;
    bento_ui_id_t adc_text;
    bento_ui_id_t chart;
    uint8_t series[AXES]; /* series 0 comes with the chart */
    bento_ui_id_t accel_texts[AXES];
    bento_ui_id_t arcs[AXES];
} dashboard_t;

/* A label's place and look: the original's x=, y=, color=, value= and
 * parent= (0 is the screen itself). */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
    bento_ui_id_t parent;
} label_style_t;

/* The board's knob, 0-4095: the Dev Kit's VR1. */
static bento_status_t read_raw(int32_t *raw)
{
    uint16_t value = 0U;
    const bento_status_t status = bento_pots_read(POT_VR1, &value);

    if (status == BENTO_OK) {
        *raw = (int32_t)value;
    }
    return status;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const label_style_t *style,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = style->x;
    args.y = style->y;
    args.color = style->color;
    args.font_px = style->font_px;
    args.parent = style->parent;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A fresh screen, then a pause to let it settle; Stop during the pause
 * ends the program. */
static bento_status_t clear_screen(bento_ui_t *ui)
{
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The tab view over the whole screen and its tabs. All tabs are added
 * before any is coloured, as the original does. A tab's colour is a
 * setter, whose refusal MicroPython ignores (modui.c). */
static bento_status_t add_tabs(bento_ui_t *ui, bento_ui_id_t *tabs)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t tabview;
    int32_t i;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = 0;
    args.y = 0;
    args.w = BENTO_RUNTIME_SCREEN_WIDTH_PX;
    args.h = BENTO_RUNTIME_SCREEN_HEIGHT_PX;
    status = bento_ui_tabview_create(ui, &args, &tabview);
    for (i = 0; (i < TABS) && (status == BENTO_OK); i++) {
        status =
            bento_ui_add_tab(ui, tabview, TAB_NAMES[i],
                             (uint32_t)strlen(TAB_NAMES[i]), &tabs[i]);
    }
    for (i = 0; (i < TABS) && (status == BENTO_OK); i++) {
        (void)bento_ui_set_color(ui, tabs[i], COLOR_TAB);
    }
    return status;
}

static bento_status_t add_adc_tab(dashboard_t *board)
{
    const bento_ui_id_t tab = board->tabs[TAB_ADC];
    const label_style_t title = {ADC_TITLE_X, ADC_TITLE_Y, COLOR_INK,
                                 TITLE_FONT_PX, tab};
    const label_style_t reading = {ADC_TEXT_X, ADC_TEXT_Y, COLOR_INK,
                                   TITLE_FONT_PX, tab};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(board->ui, "ADC Monitor", &title, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = ADC_BAR_X;
        args.y = ADC_BAR_Y;
        args.w = ADC_BAR_W;
        args.h = ADC_BAR_H;
        args.min_val = 0;
        args.max_val = PERCENT;
        args.font_px = 0;
        args.color = COLOR_BAR;
        args.parent = tab;
        status = bento_ui_bar_create(board->ui, &args, &board->adc_bar);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, "ADC: 0 (0.00V)", &reading,
                           &board->adc_text);
    }
    return status;
}

/* The chart comes with series 0 in its own colour (X); Y and Z are
 * added. */
static bento_status_t add_accel_chart(dashboard_t *board)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CHART_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.min_val = CHART_MIN;
    args.max_val = CHART_MAX;
    args.color = COLOR_X;
    args.parent = board->tabs[TAB_ACCEL];
    status = bento_ui_chart_create(board->ui, &args, &board->chart);
    board->series[0] = 0U;
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(board->ui, board->chart,
                                           COLOR_Y, &board->series[1]);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(board->ui, board->chart,
                                           COLOR_Z, &board->series[2]);
    }
    return status;
}

static bento_status_t add_accel_tab(dashboard_t *board)
{
    const bento_ui_id_t tab = board->tabs[TAB_ACCEL];
    const label_style_t title = {ACCEL_TITLE_X, TAB_TITLE_Y, COLOR_INK,
                                 TITLE_FONT_PX, tab};
    bento_ui_id_t unused;
    int32_t axis;
    bento_status_t status =
        add_label(board->ui, "Accelerometer (m/s^2)", &title, &unused);

    if (status == BENTO_OK) {
        status = add_accel_chart(board);
    }
    for (axis = 0; (axis < AXES) && (status == BENTO_OK); axis++) {
        const label_style_t style = {
            ACCEL_TEXT_X,
            (int16_t)(ACCEL_TEXT_Y + (axis * ACCEL_TEXT_STEP)),
            AXIS_COLORS[axis], TITLE_FONT_PX, tab};

        status = add_label(board->ui, "0.0", &style,
                           &board->accel_texts[axis]);
    }
    return status;
}

/* One gyro axis: its arc at half, and its name under it. */
static bento_status_t add_gyro_axis(dashboard_t *board, int32_t axis)
{
    const bento_ui_id_t tab = board->tabs[TAB_GYRO];
    const int32_t column = ARC_CENTER_X + ((axis - 1) * ARC_STEP);
    const label_style_t name = {(int16_t)(column + ARC_NAME_DX),
                                ARC_NAME_Y, COLOR_INK, AXIS_FONT_PX,
                                tab};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = (int16_t)(column + ARC_DX);
    args.y = ARC_Y;
    args.w = ARC_SIZE;
    args.h = ARC_SIZE;
    args.min_val = 0;
    args.max_val = PERCENT;
    args.font_px = ARC_MIDDLE;
    args.color = AXIS_COLORS[axis];
    args.parent = tab;
    status = bento_ui_arc_create(board->ui, &args, &board->arcs[axis]);
    if (status == BENTO_OK) {
        status = add_label(board->ui, AXIS_NAMES[axis], &name, &unused);
    }
    return status;
}

static bento_status_t add_gyro_tab(dashboard_t *board)
{
    const label_style_t title = {GYRO_TITLE_X, TAB_TITLE_Y, COLOR_INK,
                                 TITLE_FONT_PX, board->tabs[TAB_GYRO]};
    bento_ui_id_t unused;
    int32_t axis;
    bento_status_t status =
        add_label(board->ui, "Gyroscope (dps)", &title, &unused);

    for (axis = 0; (axis < AXES) && (status == BENTO_OK); axis++) {
        status = add_gyro_axis(board, axis);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(dashboard_t *board)
{
    const label_style_t footer = {FOOTER_X, FOOTER_Y, COLOR_FOOTER,
                                  FOOTER_FONT_PX, 0U};
    bento_ui_id_t unused;
    bento_status_t status = clear_screen(board->ui);

    if (status == BENTO_OK) {
        status = add_tabs(board->ui, board->tabs);
    }
    if (status == BENTO_OK) {
        status = add_adc_tab(board);
    }
    if (status == BENTO_OK) {
        status = add_accel_tab(board);
    }
    if (status == BENTO_OK) {
        status = add_gyro_tab(board);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, FOOTER, &footer, &unused);
    }
    return status;
}

/* The knob as a bar and a text. MicroPython's widget setters ignore a
 * refusal (modui.c), so the twin does too, here and below. */
static void show_adc(const dashboard_t *board, int32_t raw)
{
    const bento_real_t full_scale_volts = (bento_real_t)3.3;
    const bento_real_t fraction =
        (bento_real_t)raw / (bento_real_t)ADC_MAX;
    char volts[BENTO_FMT_BUF_SIZE];
    char text[ADC_TEXT_BYTES];
    uint32_t length = 0U;

    (void)bento_ui_set_value(board->ui, board->adc_bar,
                             (raw * PERCENT) / ADC_MAX);
    /* the buffer holds any real, so the formatter cannot refuse it */
    (void)bento_fmt_real_precision(volts, sizeof(volts),
                                   fraction * full_scale_volts,
                                   VOLT_DIGITS, &length);
    (void)snprintf(text, sizeof(text), "ADC: %" PRId32 " (%sV)", raw,
                   volts);
    (void)bento_ui_set_text(board->ui, board->adc_text, text,
                            (uint32_t)strlen(text));
}

/* int(value * scale): the product at the board's float width, cut
 * toward zero as int() cuts it. */
static int32_t chart_point(float value, int32_t scale)
{
    return (int32_t)((bento_real_t)value * (bento_real_t)scale);
}

static void show_accel(const dashboard_t *board, const float *accel)
{
    char text[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;
    int32_t axis;

    for (axis = 0; axis < AXES; axis++) {
        (void)bento_ui_chart_set_next(
            board->ui, board->chart, board->series[axis],
            chart_point(accel[axis], CHART_SCALE));
    }
    for (axis = 0; axis < AXES; axis++) {
        (void)bento_fmt_real_precision(text, sizeof(text),
                                       (bento_real_t)accel[axis],
                                       ACCEL_DIGITS, &length);
        (void)bento_ui_set_text(board->ui, board->accel_texts[axis],
                                text, length);
    }
}

/* int(50 + g / 5), kept on the arc's 0-100. */
static int32_t gyro_to_arc(float rate_dps)
{
    const int32_t step = (int32_t)((bento_real_t)ARC_MIDDLE +
                                   ((bento_real_t)rate_dps /
                                    (bento_real_t)GYRO_PER_STEP));
    int32_t value = step;

    if (step < 0) {
        value = 0;
    } else if (step > PERCENT) {
        value = PERCENT;
    } else {
        /* inside the arc's range already */
    }
    return value;
}

static void show_gyro(const dashboard_t *board, const float *rate)
{
    int32_t axis;

    for (axis = 0; axis < AXES; axis++) {
        (void)bento_ui_set_value(board->ui, board->arcs[axis],
                                 gyro_to_arc(rate[axis]));
    }
}

/* One round: the knob, the acceleration and the turn rates, in the
 * original's order. A sensor that fails ends the program, as it raises
 * in MicroPython. */
static bento_status_t show_round(const dashboard_t *board)
{
    float accel[AXES] = {0.0F, 0.0F, 0.0F};
    float rate[AXES] = {0.0F, 0.0F, 0.0F};
    int32_t raw = 0;
    bento_status_t status = read_raw(&raw);

    if (status == BENTO_OK) {
        show_adc(board, raw);
        status = bento_sensors_bmi270_acceleration(&accel[0], &accel[1],
                                                   &accel[2]);
    }
    if (status == BENTO_OK) {
        show_accel(board, accel);
        status = bento_sensors_bmi270_gyroscope(&rate[0], &rate[1],
                                                &rate[2]);
    }
    if (status == BENTO_OK) {
        show_gyro(board, rate);
    }
    return status;
}

/* The original reads the touch events every round; with no "< Menu"
 * button none of them matters, and ui.poll() answers no events when it
 * fails, so its status is not an error. */
static void skip_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* One round every LOOP_MS until RUN_MS has passed, Stop is pressed or
 * a sensor fails. */
static bento_status_t run_loop(const dashboard_t *board)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        skip_events(board->ui);
        status = show_round(board);
        if (status == BENTO_OK) {
            status = bento_time_sleep_ms(LOOP_MS);
        }
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

/* The run is over but the picture stays: a band over the bottom rows
 * says so. */
static bento_status_t show_finished(bento_ui_t *ui)
{
    const label_style_t style = {END_TEXT_X, END_TEXT_Y, COLOR_END_TEXT,
                                 END_FONT_PX, 0U};
    bento_ui_widget_args_t args;
    bento_ui_id_t band;
    bento_ui_id_t label;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = 0;
    args.y = END_Y;
    args.w = END_W;
    args.h = END_H;
    args.color = COLOR_END;
    args.min_val = COLOR_END_TEXT; /* the original's min= */
    args.max_val = 0;
    args.font_px = 1;
    status = bento_ui_panel_create(ui, &args, &band);
    if (status == BENTO_OK) {
        status = add_label(ui, "ตัวอย่างจบแล้ว กดปุ่มไม่ได้", &style, &label);
    }
    return status;
}

int main(void)
{
    dashboard_t board;
    bento_status_t status;

    (void)memset(&board, 0, sizeof(board));
    board.ui = bento_runtime_get_ui();
    status = build_screen(&board);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print("sec3 ex05: 3-tab dashboard - pot + "
                              "accel + gyro, all real");
        status = run_loop(&board);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex05: done\n");
        status = show_finished(board.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
