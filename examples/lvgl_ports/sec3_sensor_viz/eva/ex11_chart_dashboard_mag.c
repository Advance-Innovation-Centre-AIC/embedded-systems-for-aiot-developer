/*
 * ex11_chart_dashboard_mag.c -- แดชบอร์ดกราฟห้าแบบพร้อมเข็มทิศ
 * (Chart Dashboard + Compass)
 *
 * กราฟห้าแบบจากเซนเซอร์จริง สลับดูได้ทีละแท็บ: Bar แสดงความเร่งสามแกน
 * เป็นแท่ง Area แสดงขนาดมุมเอียงเป็นเส้น Scat แสดงลูกบอลที่กลิ้งตามมุม
 * roll/pitch Line แสดงความเร็วเชิงมุมสามเส้น และ Mag แสดงเข็มทิศจาก
 * BMM350 พร้อมทิศและค่าสนามแม่เหล็ก (อ่านทุกรอบที่ห้า เพราะ BMM350 ช้ากว่า
 * IMU) อ่านทุก 100 ms ทำงาน 4 นาทีแล้วจบ และวาดแถบ "ตัวอย่างจบแล้ว"
 * ทับแถวล่าง
 *
 * Five chart styles from the real sensors, one per tab: Bar shows the
 * three acceleration axes as bars, Area the tilt magnitude as a line,
 * Scat a ball that rolls with the roll and pitch angles, Line the three
 * turn rates, and Mag a compass from the BMM350 with the direction and
 * the field (read every fifth round: the BMM350 is slower than the
 * IMU). The program reads every 100 ms, ends after four minutes, and
 * then draws a "finished" band over the bottom rows.
 *
 * C twin of ex11_chart_dashboard_mag.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/eva/ex11_chart_dashboard_mag.py), from the library
 * twin aiot-lvgl-sec3-ex11-chart-dashboard-mag. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. The touch events are still read every
 * round. What C changes: Python's int is int32_t here; the original's
 * try/except around the compass becomes a status test (a failed BMM350
 * read shows "BMM350: no data", as the except branch does); sqrt() and
 * fmod() work in double and are rounded to the board's float width.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_dsp.h"
#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* Layout: each tab's own coordinates, the footer on the screen. */
enum {
    TITLE_FONT_PX = 20,
    SMALL_FONT_PX = 14,
    HEADING_FONT_PX = 24,
    TAB_TITLE_Y = 4,
    BAR_TITLE_X = 210,
    BAR_TITLE_Y = 6,
    BAR_X = 170,
    BAR_STEP = 160,
    BAR_Y = 60,
    BAR_W = 60,
    BAR_H = 180,
    BAR_NAME_X = 192,
    BAR_NAME_Y = 250,
    AREA_TITLE_X = 190,
    SCATTER_TITLE_X = 160,
    LINE_TITLE_X = 210,
    CHART_X = 70,
    CHART_Y = 36,
    CHART_W = 390,
    AREA_CHART_H = 200,
    LINE_CHART_H = 228,
    TILT_TEXT_X = 250,
    TILT_TEXT_Y = 246,
    BOX_X = 140,
    BOX_Y = 36,
    BOX_W = 290,
    BOX_H = 210,
    BOX_BORDER_PX = 4, /* the panel's max= */
    BOX_RADIUS = 2,    /* the panel's value= */
    DOT_X = 278,       /* the ball at rest, in the middle */
    DOT_Y = 134,
    DOT_SIZE = 14,
    DOT_RADIUS = 7,
    DOT_X_MIN = 148, /* the ball stays inside the box */
    DOT_X_MAX = 408,
    DOT_Y_MIN = 44,
    DOT_Y_MAX = 228,
    ROLL_PX = 130, /* the ball's travel at 90 degrees */
    PITCH_PX = 95,
    ANGLES_TEXT_X = 200,
    ANGLES_TEXT_Y = 252,
    COMPASS_X = 180,
    COMPASS_Y = 30,
    COMPASS_SIZE = 200,
    HEADING_X = 420,
    HEADING_Y = 70,
    MAG_TEXT_X = 410,
    FIELD_Y = 120,
    MAG_STATE_Y = 150,
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
    TABS = 5,
    TAB_BAR = 0,
    TAB_AREA = 1,
    TAB_SCATTER = 2,
    TAB_LINE = 3,
    TAB_MAG = 4,
    AXES = 3,
    BAR_MAX = 200,
    BAR_MIDDLE = 100, /* 0 m/s^2: the bar stands at half */
    BAR_SCALE = 10,
    TILT_MAX_DEG = 90,
    GYRO_MIN = -1000,
    GYRO_MAX = 1000,
    GYRO_SCALE = 10,   /* dps x10 on the chart */
    COMPASS_EVERY = 5, /* a 2 Hz compass is enough */
    DIRECTIONS = 8,
    FULL_TURN_DEG = 360,
    SECTOR_DEG = 45, /* one of the eight directions */
    TILT_DIGITS = 1,
    ANGLE_DIGITS = 2,
    TILT_TEXT_BYTES = 48,    /* "Tilt: ", a real, " deg" and the NUL */
    ANGLES_TEXT_BYTES = 84,  /* "X: ", two reals and "  Y: " */
    HEADING_TEXT_BYTES = 16, /* any int32_t, " " and "NW" */
    FIELD_TEXT_BYTES = 45    /* three int32_t and "X:", " Y:", " Z:" */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,
    RUN_MS = 240000 /* the example ends after four minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TAB = 0xF5F5F5,
    COLOR_INK = 0x111111,
    COLOR_GREY_INK = 0x555555,
    COLOR_X = 0xF44336,
    COLOR_Y = 0x4CAF50,
    COLOR_Z = 0x2196F3,
    COLOR_TILT = 0xFF9800,
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

static const uint32_t AXIS_COLORS[AXES] = {COLOR_X, COLOR_Y, COLOR_Z};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t tabs[TABS];
    bento_ui_id_t bars[AXES];
    bento_ui_id_t area_chart;
    bento_ui_id_t tilt_text;
    bento_ui_id_t dot;
    bento_ui_id_t angles_text;
    bento_ui_id_t line_chart;
    uint8_t line_series[AXES]; /* series 0 comes with the chart */
    bento_ui_id_t compass;
    bento_ui_id_t heading_text;
    bento_ui_id_t field_text;
    bento_ui_id_t mag_state;
} dashboard_t;

/* What one round reads. */
typedef struct {
    float accel[AXES];
    float rate[AXES];
    float roll_deg;
    float pitch_deg;
} reading_t;

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
    static const char *const tab_names[TABS] = {"Bar", "Area", "Scat",
                                                "Line", "Mag"};
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
            bento_ui_add_tab(ui, tabview, tab_names[i],
                             (uint32_t)strlen(tab_names[i]), &tabs[i]);
    }
    for (i = 0; (i < TABS) && (status == BENTO_OK); i++) {
        (void)bento_ui_set_color(ui, tabs[i], COLOR_TAB);
    }
    return status;
}

/* A tab's title at the top, in the ink colour. */
static bento_status_t add_title(const dashboard_t *board, int32_t tab,
                                const char *text, int16_t x, int16_t y)
{
    const label_style_t style = {x, y, COLOR_INK, TITLE_FONT_PX,
                                 board->tabs[tab]};
    bento_ui_id_t unused;

    return add_label(board->ui, text, &style, &unused);
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
    int32_t axis;
    bento_status_t status =
        add_title(board, TAB_BAR, "Bar: Real Accel X/Y/Z", BAR_TITLE_X,
                  BAR_TITLE_Y);

    for (axis = 0; (axis < AXES) && (status == BENTO_OK); axis++) {
        status = add_bar(board, axis);
    }
    return status;
}

/* One line, the tilt magnitude from 0 to 90 degrees, and its text. */
static bento_status_t add_area_tab(dashboard_t *board)
{
    const bento_ui_id_t tab = board->tabs[TAB_AREA];
    const label_style_t tilt = {TILT_TEXT_X, TILT_TEXT_Y, COLOR_INK,
                                TITLE_FONT_PX, tab};
    bento_ui_widget_args_t args;
    bento_status_t status =
        add_title(board, TAB_AREA, "Area: Tilt Magnitude (deg)",
                  AREA_TITLE_X, TAB_TITLE_Y);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = CHART_X;
        args.y = CHART_Y;
        args.w = CHART_W;
        args.h = AREA_CHART_H;
        args.min_val = 0;
        args.max_val = TILT_MAX_DEG;
        args.color = COLOR_TILT;
        args.parent = tab;
        status =
            bento_ui_chart_create(board->ui, &args, &board->area_chart);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, "Tilt: 0.0 deg", &tilt,
                           &board->tilt_text);
    }
    return status;
}

/* A white box with a grey edge, and the ball in its middle. */
static bento_status_t add_box_and_ball(dashboard_t *board)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t box;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = BOX_X;
    args.y = BOX_Y;
    args.w = BOX_W;
    args.h = BOX_H;
    args.color = COLOR_BOX;
    args.min_val = COLOR_BOX_EDGE;
    args.max_val = BOX_BORDER_PX;
    args.font_px = BOX_RADIUS;
    args.parent = board->tabs[TAB_SCATTER];
    status = bento_ui_panel_create(board->ui, &args, &box);
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
        args.parent = board->tabs[TAB_SCATTER];
        status = bento_ui_panel_create(board->ui, &args, &board->dot);
    }
    return status;
}

static bento_status_t add_scatter_tab(dashboard_t *board)
{
    const label_style_t angles = {ANGLES_TEXT_X, ANGLES_TEXT_Y,
                                  COLOR_INK, SMALL_FONT_PX,
                                  board->tabs[TAB_SCATTER]};
    bento_status_t status = add_title(
        board, TAB_SCATTER, "Scatter: Roll vs Pitch (Tilt Ball)",
        SCATTER_TITLE_X, TAB_TITLE_Y);

    if (status == BENTO_OK) {
        status = add_box_and_ball(board);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, "X: 0.00  Y: 0.00", &angles,
                           &board->angles_text);
    }
    return status;
}

/* Three lines: X in the chart's own colour, then Y and Z added. */
static bento_status_t add_line_tab(dashboard_t *board)
{
    bento_ui_widget_args_t args;
    int32_t axis;
    bento_status_t status =
        add_title(board, TAB_LINE, "Line: Real Gyro R/P/Y",
                  LINE_TITLE_X, TAB_TITLE_Y);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = CHART_X;
        args.y = CHART_Y;
        args.w = CHART_W;
        args.h = LINE_CHART_H;
        args.min_val = GYRO_MIN;
        args.max_val = GYRO_MAX;
        args.color = COLOR_X;
        args.parent = board->tabs[TAB_LINE];
        status =
            bento_ui_chart_create(board->ui, &args, &board->line_chart);
    }
    board->line_series[0] = 0U;
    for (axis = 1; (axis < AXES) && (status == BENTO_OK); axis++) {
        status = bento_ui_chart_add_series(board->ui, board->line_chart,
                                           AXIS_COLORS[axis],
                                           &board->line_series[axis]);
    }
    return status;
}

/* The compass, the heading, the field and the sensor's state. */
static bento_status_t add_mag_tab(dashboard_t *board)
{
    const bento_ui_id_t tab = board->tabs[TAB_MAG];
    const label_style_t heading = {HEADING_X, HEADING_Y, COLOR_INK,
                                   HEADING_FONT_PX, tab};
    const label_style_t field = {MAG_TEXT_X, FIELD_Y, COLOR_GREY_INK,
                                 SMALL_FONT_PX, tab};
    const label_style_t state = {MAG_TEXT_X, MAG_STATE_Y,
                                 COLOR_GREY_INK, SMALL_FONT_PX, tab};
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = COMPASS_X;
    args.y = COMPASS_Y;
    args.w = COMPASS_SIZE;
    args.h = COMPASS_SIZE;
    args.font_px = 0;
    args.parent = tab;
    status = bento_ui_compass_create(board->ui, &args, &board->compass);
    if (status == BENTO_OK) {
        status =
            add_label(board->ui, "0 N", &heading, &board->heading_text);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, "X:0 Y:0 Z:0", &field,
                           &board->field_text);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui, "BMM350: Init...", &state,
                           &board->mag_state);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(dashboard_t *board)
{
    const label_style_t footer = {FOOTER_X, FOOTER_Y, COLOR_FOOTER,
                                  SMALL_FONT_PX, 0U};
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
        status = add_mag_tab(board);
    }
    if (status == BENTO_OK) {
        status = add_label(board->ui,
                           "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                           "Burapha University",
                           &footer, &unused);
    }
    return status;
}

/* int(value * scale): the product at the board's float width, cut
 * toward zero as int() cuts it. */
static int32_t chart_point(float value, int32_t scale)
{
    const bento_real_t product =
        (bento_real_t)value * (bento_real_t)scale;

    return (int32_t)product;
}

/* int(value): Python's int() of a real cuts toward zero, as the cast
 * does; the real expression is worked out first, at the board's float
 * width. */
static int32_t int_of(bento_real_t value)
{
    return (int32_t)value;
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

/* Sets a label's text; MicroPython's widget setters ignore a refusal
 * (modui.c), so the twin does too, in every show_ function below. */
static void set_text(bento_ui_t *ui, bento_ui_id_t label,
                     const char *text)
{
    (void)bento_ui_set_text(ui, label, text, (uint32_t)strlen(text));
}

/* '%.<digits>f' % value into number; the buffer holds any real, so the
 * formatter cannot refuse it. */
static void format_real(char *number, float value, int32_t digits)
{
    uint32_t length = 0U;

    (void)bento_fmt_real_precision(number, BENTO_FMT_BUF_SIZE,
                                   (bento_real_t)value, digits,
                                   &length);
}

static void show_bars(const dashboard_t *board, const float *accel)
{
    int32_t axis;

    for (axis = 0; axis < AXES; axis++) {
        (void)bento_ui_set_value(board->ui, board->bars[axis],
                                 int_of((bento_real_t)BAR_MIDDLE +
                                        ((bento_real_t)accel[axis] *
                                         (bento_real_t)BAR_SCALE)));
    }
}

/* The tilt magnitude, sqrt(roll^2 + pitch^2): a point and a text. */
static void show_tilt(const dashboard_t *board,
                      const reading_t *reading)
{
    const float squares = (reading->roll_deg * reading->roll_deg) +
                          (reading->pitch_deg * reading->pitch_deg);
    const bento_real_t magnitude = (bento_real_t)sqrt((double)squares);
    char number[BENTO_FMT_BUF_SIZE];
    char text[TILT_TEXT_BYTES];

    (void)bento_ui_chart_set_next(board->ui, board->area_chart, 0U,
                                  (int32_t)magnitude);
    format_real(number, (float)magnitude, TILT_DIGITS);
    (void)snprintf(text, sizeof(text), "Tilt: %s deg", number);
    set_text(board->ui, board->tilt_text, text);
}

/* The ball: 90 degrees move it ROLL_PX / PITCH_PX from the middle. */
static void show_ball(const dashboard_t *board,
                      const reading_t *reading)
{
    const int32_t x =
        int_of((bento_real_t)DOT_X + (((bento_real_t)reading->roll_deg /
                                       (bento_real_t)TILT_MAX_DEG) *
                                      (bento_real_t)ROLL_PX));
    const int32_t y = int_of((bento_real_t)DOT_Y -
                             (((bento_real_t)reading->pitch_deg /
                               (bento_real_t)TILT_MAX_DEG) *
                              (bento_real_t)PITCH_PX));
    char roll[BENTO_FMT_BUF_SIZE];
    char pitch[BENTO_FMT_BUF_SIZE];
    char text[ANGLES_TEXT_BYTES];

    (void)bento_ui_set_pos(board->ui, board->dot,
                           (int16_t)clamp(x, DOT_X_MIN, DOT_X_MAX),
                           (int16_t)clamp(y, DOT_Y_MIN, DOT_Y_MAX));
    format_real(roll, reading->roll_deg, ANGLE_DIGITS);
    format_real(pitch, reading->pitch_deg, ANGLE_DIGITS);
    (void)snprintf(text, sizeof(text), "X: %s  Y: %s", roll, pitch);
    set_text(board->ui, board->angles_text, text);
}

/* A point on each gyro line; a point is sent and not answered. */
static void show_rates(const dashboard_t *board, const float *rate)
{
    int32_t axis;

    for (axis = 0; axis < AXES; axis++) {
        (void)bento_ui_chart_set_next(
            board->ui, board->line_chart, board->line_series[axis],
            chart_point(rate[axis], GYRO_SCALE));
    }
}

/* One of N, NE, ... NW: (heading + 22.5) % 360, floor-divided by 45.
 * Python's % of a real takes the divisor's sign, so a negative
 * remainder gets a full turn added. */
static const char *direction_of(float heading_deg)
{
    static const char *const directions[DIRECTIONS] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    const double half_sector = (double)SECTOR_DEG / 2.0;
    double turned =
        fmod((double)heading_deg + half_sector, (double)FULL_TURN_DEG);
    double sectors;
    int32_t sector;

    if (turned < 0.0) {
        turned += (double)FULL_TURN_DEG;
    }
    sectors = turned / (double)SECTOR_DEG;
    sector = (int32_t)sectors;
    /* a remainder below 360 gives 0-7; the test only guards the table */
    return directions[clamp(sector, 0, DIRECTIONS - 1)];
}

/* The compass, every COMPASS_EVERY rounds. The original catches any
 * error of the two BMM350 reads and says "no data"; so does this. */
static void show_compass(const dashboard_t *board)
{
    float heading_deg = 0.0F;
    float field[AXES] = {0.0F, 0.0F, 0.0F};
    char text[FIELD_TEXT_BYTES];
    bento_status_t status = bento_sensors_bmm350_heading(&heading_deg);

    if (status == BENTO_OK) {
        status = bento_sensors_bmm350_magnetic(&field[0], &field[1],
                                               &field[2]);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_value(board->ui, board->compass,
                                 (int32_t)heading_deg);
        (void)snprintf(text, sizeof(text), "%d %s", (int)heading_deg,
                       direction_of(heading_deg));
        set_text(board->ui, board->heading_text, text);
        /* '%d' % a real cuts it toward zero, as the casts do */
        (void)snprintf(text, sizeof(text), "X:%d Y:%d Z:%d",
                       (int)field[0], (int)field[1], (int)field[2]);
        set_text(board->ui, board->field_text, text);
        set_text(board->ui, board->mag_state, "BMM350: OK");
    } else {
        set_text(board->ui, board->mag_state, "BMM350: no data");
    }
}

/* The IMU and the tilt; a failure ends the program, as it raises in
 * MicroPython. */
static bento_status_t read_imu(reading_t *reading)
{
    bento_status_t status = bento_sensors_bmi270_acceleration(
        &reading->accel[0], &reading->accel[1], &reading->accel[2]);

    if (status == BENTO_OK) {
        status = bento_sensors_bmi270_gyroscope(
            &reading->rate[0], &reading->rate[1], &reading->rate[2]);
    }
    if (status == BENTO_OK) {
        status = bento_dsp_tilt(reading->accel[0], reading->accel[1],
                                reading->accel[2], &reading->roll_deg,
                                &reading->pitch_deg);
    }
    return status;
}

/* One round: every tab is updated, the hidden ones too, because the
 * program cannot read which tab is shown. */
static bento_status_t show_round(const dashboard_t *board,
                                 int32_t round)
{
    reading_t reading;
    bento_status_t status;

    (void)memset(&reading, 0, sizeof(reading));
    status = read_imu(&reading);
    if (status == BENTO_OK) {
        show_bars(board, reading.accel);
        show_tilt(board, &reading);
        show_ball(board, &reading);
        show_rates(board, reading.rate);
        if ((round % COMPASS_EVERY) == 0) {
            show_compass(board);
        }
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
    int32_t round = 0;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        skip_events(board->ui);
        round++;
        status = show_round(board, round);
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
        (void)bento_lcd_print("sec3 ex11: five real-data chart styles "
                              "incl BMM350 compass");
        status = run_loop(&board);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex11: done\n");
        status = show_finished(board.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
