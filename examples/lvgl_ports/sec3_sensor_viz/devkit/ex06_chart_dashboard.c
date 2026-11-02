/*
 * ex06_chart_dashboard.c -- แดชบอร์ดกราฟสี่แบบ (Chart Dashboard)
 *
 * กราฟสี่แบบจาก IMU จริง สลับดูได้ทีละแท็บ: Bar แสดงความเร่งสามแกนเป็น
 * แท่งแนวตั้ง Area แสดงแนวโน้มความเร่งเป็นเส้น Scatter แสดงลูกบอลที่กลิ้ง
 * ตามมุม roll/pitch และ Line แสดงความเร็วเชิงมุมสามเส้น อ่านทุก 100 ms
 * ทำงาน 3 นาทีแล้วจบ และวาดแถบ "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * Four chart styles from the real IMU, one per tab: Bar shows the three
 * acceleration axes as upright bars, Area the acceleration trend as
 * lines, Scatter a ball that rolls with the roll and pitch angles, and
 * Line the three turn rates. The program reads every 100 ms, ends after
 * three minutes, and then draws a "finished" band over the bottom rows.
 *
 * C twin of ex06_chart_dashboard.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/devkit/ex06_chart_dashboard.py), from the library
 * twin aiot-lvgl-sec3-ex06-chart-dashboard. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. The touch events are still read every
 * round. What C changes: Python's int is int32_t here; atan2() and
 * sqrt() work in double and are rounded to the board's float width, so
 * the ball can sit one pixel off MicroPython's in a rare case.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* Layout: each tab's own coordinates, the footer on the screen. */
enum {
    TITLE_FONT_PX = 20,
    FOOTER_FONT_PX = 14,
    TAB_TITLE_Y = 4,
    BAR_TITLE_X = 200,
    BAR_TITLE_Y = 6,
    BAR_X = 170,
    BAR_STEP = 160,
    BAR_Y = 60,
    BAR_W = 60,
    BAR_H = 180,
    BAR_NAME_X = 192,
    BAR_NAME_Y = 250,
    AREA_TITLE_X = 250,
    SCATTER_TITLE_X = 210,
    LINE_TITLE_X = 240,
    CHART_X = 70,
    CHART_Y = 36,
    CHART_W = 390,
    CHART_H = 228,
    BOX_X = 140,
    BOX_Y = 36,
    BOX_W = 290,
    BOX_H = 230,
    BOX_BORDER_PX = 4, /* the panel's max= */
    BOX_RADIUS = 2,    /* the panel's value= */
    DOT_X = 278,       /* the ball at rest, in the middle */
    DOT_Y = 144,
    DOT_SIZE = 14,
    DOT_RADIUS = 7,
    DOT_X_MIN = 148, /* the ball stays 8 px inside the box */
    DOT_X_MAX = 408,
    DOT_Y_MIN = 44,
    DOT_Y_MAX = 244,
    ROLL_PX = 130, /* the ball's travel at the full angle */
    PITCH_PX = 100,
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

/* The tabs, the axes and the scales. */
enum {
    TABS = 4,
    TAB_BAR = 0,
    TAB_AREA = 1,
    TAB_SCATTER = 2,
    TAB_LINE = 3,
    AXES = 3,
    BAR_MAX = 200,
    BAR_MIDDLE = 100, /* 0 m/s^2: the bar stands at half */
    BAR_SCALE = 10,
    ACCEL_MIN = -200,
    ACCEL_MAX = 1200,
    ACCEL_SCALE = 100, /* m/s^2 x100 on the chart */
    GYRO_MIN = -1000,
    GYRO_MAX = 1000,
    GYRO_SCALE = 10 /* dps x10 on the chart */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,
    RUN_MS = 180000 /* the example ends after three minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TAB = 0xF5F5F5,
    COLOR_INK = 0x111111,
    COLOR_X = 0xF44336,
    COLOR_Y = 0x4CAF50,
    COLOR_Z = 0x2196F3,
    COLOR_BOX = 0xFFFFFF,
    COLOR_BOX_EDGE = 0x888888,
    COLOR_DOT = 0xE91E63,
    COLOR_FOOTER = 0x666666
};

/* The "finished" band's colours, 0xRRGGBB. */
enum {
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

static const char FOOTER[] =
    "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";

static const char *const TAB_NAMES[TABS] = {"Bar", "Area", "Scatter",
                                            "Line"};
static const uint32_t AXIS_COLORS[AXES] = {COLOR_X, COLOR_Y, COLOR_Z};

/* A chart with its three series: series 0 comes with the chart. */
typedef struct {
    bento_ui_id_t chart;
    uint8_t series[AXES];
} chart_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t tabs[TABS];
    bento_ui_id_t bars[AXES];
    chart_t area;
    bento_ui_id_t dot;
    chart_t line;
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

/* A three-line chart in a tab: X in the chart's own colour, then Y and
 * Z added. */
static bento_status_t add_chart(bento_ui_t *ui, bento_ui_id_t tab,
                                int32_t low, int32_t high,
                                chart_t *chart)
{
    bento_ui_widget_args_t args;
    int32_t axis;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CHART_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.min_val = low;
    args.max_val = high;
    args.color = COLOR_X;
    args.parent = tab;
    status = bento_ui_chart_create(ui, &args, &chart->chart);
    chart->series[0] = 0U;
    for (axis = 1; (axis < AXES) && (status == BENTO_OK); axis++) {
        status = bento_ui_chart_add_series(
            ui, chart->chart, AXIS_COLORS[axis], &chart->series[axis]);
    }
    return status;
}

/* One upright bar and its axis name. The bar is created at 0, then
 * set: a Bar created with a non-zero value blanks the whole screen. */
static bento_status_t add_bar(dashboard_t *board, int32_t axis)
{
    static const char *const axis_names[AXES] = {"X", "Y", "Z"};
    const bento_ui_id_t tab = board->tabs[TAB_BAR];
    const label_style_t name = {
        (int16_t)(BAR_NAME_X + (axis * BAR_STEP)), BAR_NAME_Y,
        COLOR_INK, TITLE_FONT_PX, tab};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = (int16_t)(BAR_X + (axis * BAR_STEP));
    args.y = BAR_Y;
    args.w = BAR_W;
    args.h = BAR_H;
    args.min_val = 0;
    args.max_val = BAR_MAX;
    args.font_px = 0;
    args.color = AXIS_COLORS[axis];
    args.parent = tab;
    status = bento_ui_bar_create(board->ui, &args, &board->bars[axis]);
    if (status == BENTO_OK) {
        /* a setter's refusal is ignored, as MicroPython ignores it */
        (void)bento_ui_set_value(board->ui, board->bars[axis],
                                 BAR_MIDDLE);
        status = add_label(board->ui, axis_names[axis], &name, &unused);
    }
    return status;
}

static bento_status_t add_bar_tab(dashboard_t *board)
{
    const label_style_t title = {BAR_TITLE_X, BAR_TITLE_Y, COLOR_INK,
                                 TITLE_FONT_PX, board->tabs[TAB_BAR]};
    bento_ui_id_t unused;
    int32_t axis;
    bento_status_t status =
        add_label(board->ui, "Accel snapshot (m/s^2)", &title, &unused);

    for (axis = 0; (axis < AXES) && (status == BENTO_OK); axis++) {
        status = add_bar(board, axis);
    }
    return status;
}

static bento_status_t add_area_tab(dashboard_t *board)
{
    const label_style_t title = {AREA_TITLE_X, TAB_TITLE_Y, COLOR_INK,
                                 TITLE_FONT_PX, board->tabs[TAB_AREA]};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(board->ui, "Accel trend", &title, &unused);

    if (status == BENTO_OK) {
        status = add_chart(board->ui, board->tabs[TAB_AREA], ACCEL_MIN,
                           ACCEL_MAX, &board->area);
    }
    return status;
}

/* A white box with a grey edge, and the ball in its middle. */
static bento_status_t add_scatter_tab(dashboard_t *board)
{
    const bento_ui_id_t tab = board->tabs[TAB_SCATTER];
    const label_style_t title = {SCATTER_TITLE_X, TAB_TITLE_Y,
                                 COLOR_INK, TITLE_FONT_PX, tab};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(
        board->ui, "Tilt ball (roll vs pitch)", &title, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = BOX_X;
        args.y = BOX_Y;
        args.w = BOX_W;
        args.h = BOX_H;
        args.color = COLOR_BOX;
        args.min_val = COLOR_BOX_EDGE;
        args.max_val = BOX_BORDER_PX;
        args.font_px = BOX_RADIUS;
        args.parent = tab;
        status = bento_ui_panel_create(board->ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = DOT_X;
        args.y = DOT_Y;
        args.w = DOT_SIZE;
        args.h = DOT_SIZE;
        args.color = COLOR_DOT;
        args.min_val = COLOR_DOT;
        args.max_val = DOT_RADIUS;
        args.font_px = 0;
        args.parent = tab;
        status = bento_ui_panel_create(board->ui, &args, &board->dot);
    }
    return status;
}

static bento_status_t add_line_tab(dashboard_t *board)
{
    const label_style_t title = {LINE_TITLE_X, TAB_TITLE_Y, COLOR_INK,
                                 TITLE_FONT_PX, board->tabs[TAB_LINE]};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(board->ui, "Gyro (dps x10)", &title, &unused);

    if (status == BENTO_OK) {
        status = add_chart(board->ui, board->tabs[TAB_LINE], GYRO_MIN,
                           GYRO_MAX, &board->line);
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
        status = add_bar_tab(board);
    }
    if (status == BENTO_OK) {
        status = add_area_tab(board);
    }
    if (status == BENTO_OK) {
        status = add_scatter_tab(board);
    }
    if (status == BENTO_OK) {
        status = add_line_tab(board);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, FOOTER, &footer, &unused);
    }
    return status;
}

/* int(value * scale): the product at the board's float width, cut
 * toward zero as int() cuts it. */
static int32_t chart_point(float value, int32_t scale)
{
    return (int32_t)((bento_real_t)value * (bento_real_t)scale);
}

/* value kept between low and high, as the original's two-sided
 * conditional expression keeps it. */
static int32_t clamp(int32_t value, int32_t low, int32_t high)
{
    int32_t kept = value;

    if (value < low) {
        kept = low;
    } else if (value > high) {
        kept = high;
    } else {
        /* inside the range already */
    }
    return kept;
}

/* A new point on each of the chart's three lines. A point is sent and
 * not answered, as in MicroPython. */
static void push_points(bento_ui_t *ui, const chart_t *chart,
                        const float *values, int32_t scale)
{
    int32_t axis;

    for (axis = 0; axis < AXES; axis++) {
        (void)bento_ui_chart_set_next(ui, chart->chart,
                                      chart->series[axis],
                                      chart_point(values[axis], scale));
    }
}

/* roll and pitch from the acceleration (radians), and the ball's place
 * in the box: 1.2 rad moves it ROLL_PX / PITCH_PX from the middle. */
static void move_ball(const dashboard_t *board, const float *accel)
{
    const bento_real_t full_angle = (bento_real_t)1.2;
    const bento_real_t roll =
        (bento_real_t)atan2((double)accel[1], (double)accel[2]);
    const bento_real_t across = (bento_real_t)sqrt(
        (double)((accel[1] * accel[1]) + (accel[2] * accel[2])));
    const bento_real_t pitch =
        (bento_real_t)atan2((double)(-accel[0]), (double)across);
    const int32_t x =
        (int32_t)((bento_real_t)DOT_X +
                  ((roll / full_angle) * (bento_real_t)ROLL_PX));
    const int32_t y =
        (int32_t)((bento_real_t)DOT_Y -
                  ((pitch / full_angle) * (bento_real_t)PITCH_PX));

    /* a setter's refusal is ignored, as MicroPython ignores it */
    (void)bento_ui_set_pos(board->ui, board->dot,
                           (int16_t)clamp(x, DOT_X_MIN, DOT_X_MAX),
                           (int16_t)clamp(y, DOT_Y_MIN, DOT_Y_MAX));
}

/* One round: every tab is updated, the hidden ones too, because the
 * program cannot read which tab is shown. */
static bento_status_t show_round(const dashboard_t *board)
{
    float accel[AXES] = {0.0F, 0.0F, 0.0F};
    float rate[AXES] = {0.0F, 0.0F, 0.0F};
    int32_t axis;
    bento_status_t status = bento_sensors_bmi270_acceleration(
        &accel[0], &accel[1], &accel[2]);

    if (status == BENTO_OK) {
        status = bento_sensors_bmi270_gyroscope(&rate[0], &rate[1],
                                                &rate[2]);
    }
    if (status == BENTO_OK) {
        for (axis = 0; axis < AXES; axis++) {
            (void)bento_ui_set_value(
                board->ui, board->bars[axis],
                (int32_t)((bento_real_t)BAR_MIDDLE +
                          ((bento_real_t)accel[axis] *
                           (bento_real_t)BAR_SCALE)));
        }
        push_points(board->ui, &board->area, accel, ACCEL_SCALE);
        move_ball(board, accel);
        push_points(board->ui, &board->line, rate, GYRO_SCALE);
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
 * the IMU fails. */
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
        (void)bento_lcd_print("sec3 ex06: four chart styles from real "
                              "IMU - switch the tabs");
        status = run_loop(&board);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex06: done\n");
        status = show_finished(board.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
