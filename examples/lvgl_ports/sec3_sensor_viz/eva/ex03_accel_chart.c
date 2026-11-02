/*
 * ex03_accel_chart.c -- กราฟความเร่ง (Accelerometer Chart)
 *
 * เอียงบอร์ดแล้วดูความเร่งสามแกนจาก BMI270 จริง วิ่งเป็นเส้นบนกราฟ
 * (สเกล x100) พร้อมตัวเลข m/s^2 ของแต่ละแกน อ่านทุก 100 ms ทำงาน 2 นาที
 * แล้วจบ และวาดแถบ "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * Tilt the board and watch the real BMI270's three acceleration axes
 * scroll across the chart (scaled x100), with each axis in m/s^2 beside
 * it. It reads every 100 ms, ends after two minutes, and then draws a
 * "finished" band over the bottom rows.
 *
 * C twin of ex03_accel_chart.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/eva/ex03_accel_chart.py), from the library twin
 * aiot-lvgl-sec3-ex03-accel-chart. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. The touch events are still read every
 * round. What C changes: Python's int is int32_t here.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    TITLE_Y = 8,
    CHART_X = 166,
    CHART_Y = 99,
    CHART_W = 440,
    CHART_H = 216,
    CHART_MIN = -200,
    CHART_MAX = 1200,
    AXIS_LABEL_X = 636,
    AXIS_LABEL_Y = 142,
    AXIS_LABEL_STEP = 30,
    UNIT_Y = 320,
    FOOTER_Y = 378
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

/* The three axes. */
enum {
    AXES = 3,
    CHART_SCALE = 100, /* m/s^2 x100 on the chart */
    SIGNED_DIGITS = 2,
    AXIS_TEXT_BYTES = 40 /* "X: ", a sign, any real and the NUL */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,
    RUN_MS = 120000 /* the example ends after two minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x16213E,
    COLOR_TITLE = 0xFFFFFF,
    COLOR_X = 0xF44336,
    COLOR_Y = 0x4CAF50,
    COLOR_Z = 0x2196F3,
    COLOR_HINT = 0xAAAAAA,
    COLOR_FOOTER = 0x666666
};

/* The "finished" band's colours, 0xRRGGBB. */
enum {
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    uint8_t series[AXES]; /* series 0 comes with the chart */
    bento_ui_id_t axis_labels[AXES];
} accel_screen_t;

/* A label's place and look: the original's x=, y=, color= and value=. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} label_style_t;

/* Python's len(): characters, not bytes, so a text is centred like
 * the original. */
static int32_t utf8_length(const char *text)
{
    /* the two bits that mark a UTF-8 byte */
    const uint8_t top_bits = 0xC0U;
    /* 10xxxxxx: not the first byte of a character */
    const uint8_t continuation = 0x80U;
    int32_t count = 0;
    size_t i;

    for (i = 0U; text[i] != '\0'; i++) {
        if (((uint8_t)text[i] & top_bits) != continuation) {
            count++;
        }
    }
    return count;
}

/* x that centres a text: the original's len(text) * size, floor-divided
 * by 4, left of the centre. */
static int16_t centered_x(const char *text, int32_t font_px)
{
    return (int16_t)(SCREEN_CENTER_X -
                     (((utf8_length(text) * font_px) / 2) / 2));
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
    return bento_ui_label_create(ui, &args, out_id);
}

/* A label centred on the screen, as the original's cx() places it. */
static bento_status_t add_centered(bento_ui_t *ui, const char *text,
                                   int16_t y, uint32_t color,
                                   int32_t font_px,
                                   bento_ui_id_t *out_id)
{
    const label_style_t style = {centered_x(text, font_px), y, color,
                                 font_px};

    return add_label(ui, text, &style, out_id);
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

static bento_status_t add_background(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;

    bento_ui_widget_args_init(&args);
    args.x = 0;
    args.y = 0;
    args.w = BENTO_RUNTIME_SCREEN_WIDTH_PX;
    args.h = BENTO_RUNTIME_SCREEN_HEIGHT_PX;
    args.color = COLOR_BACKGROUND;
    args.min_val = COLOR_BACKGROUND; /* min= repeats the colour */
    args.max_val = 0;
    args.font_px = 0;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* The chart comes with series 0 in its own colour (X); Y and Z are
 * added. The firmware allows four series per chart. */
static bento_status_t add_chart(accel_screen_t *screen)
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
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    screen->series[0] = 0U;
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_Y, &screen->series[1]);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_Z, &screen->series[2]);
    }
    return status;
}

static bento_status_t add_axis_labels(accel_screen_t *screen)
{
    static const char *const start_text[AXES] = {"X: +0.00", "Y: +0.00",
                                                 "Z: +0.00"};
    static const uint32_t axis_color[AXES] = {COLOR_X, COLOR_Y,
                                              COLOR_Z};
    int32_t axis;
    bento_status_t status = BENTO_OK;

    for (axis = 0; (axis < AXES) && (status == BENTO_OK); axis++) {
        const label_style_t style = {
            AXIS_LABEL_X,
            (int16_t)(AXIS_LABEL_Y + (axis * AXIS_LABEL_STEP)),
            axis_color[axis], FONT_PX};

        status = add_label(screen->ui, start_text[axis], &style,
                           &screen->axis_labels[axis]);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(accel_screen_t *screen)
{
    const label_style_t unit_style = {CHART_X, UNIT_Y, COLOR_HINT,
                                      FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status = clear_screen(screen->ui);

    if (status == BENTO_OK) {
        status = add_background(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_centered(screen->ui,
                              "Part 2 - Example 3: Accelerometer Chart",
                              TITLE_Y, COLOR_TITLE, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_axis_labels(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "m/s^2 x100 on chart",
                           &unit_style, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(screen->ui,
                         "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                         "Burapha University",
                         FOOTER_Y, COLOR_FOOTER, FONT_PX, &unused);
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

/* prefix + '%+.2f' % value: MicroPython's '+' flag puts a sign in
 * front of a number that has none. The buffer holds any real, so the
 * formatter cannot refuse it. */
static void format_signed(char *text, size_t size, const char *prefix,
                          float value)
{
    char number[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;

    (void)bento_fmt_real_precision(number, sizeof(number),
                                   (bento_real_t)value, SIGNED_DIGITS,
                                   &length);
    (void)snprintf(text, size, "%s%s%s", prefix,
                   (number[0] == '-') ? "" : "+", number);
}

/* One reading: a new point per axis on the chart, and the numbers.
 * MicroPython's widget setters ignore a refusal (modui.c), so the twin
 * does too. */
static void show_accel(const accel_screen_t *screen, const float *accel)
{
    static const char *const axis_prefix[AXES] = {"X: ", "Y: ", "Z: "};
    char text[AXIS_TEXT_BYTES];
    int32_t axis;

    for (axis = 0; axis < AXES; axis++) {
        (void)bento_ui_chart_set_next(
            screen->ui, screen->chart, screen->series[axis],
            chart_point(accel[axis], CHART_SCALE));
    }
    for (axis = 0; axis < AXES; axis++) {
        format_signed(text, sizeof(text), axis_prefix[axis],
                      accel[axis]);
        (void)bento_ui_set_text(screen->ui, screen->axis_labels[axis],
                                text, (uint32_t)strlen(text));
    }
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

/* Reads the BMI270 every LOOP_MS until RUN_MS has passed, Stop is
 * pressed or the sensor fails. */
static bento_status_t run_loop(const accel_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        float accel[AXES] = {0.0F, 0.0F, 0.0F};

        skip_events(screen->ui);
        status = bento_sensors_bmi270_acceleration(&accel[0], &accel[1],
                                                   &accel[2]);
        if (status == BENTO_OK) {
            show_accel(screen, accel);
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
                                 END_FONT_PX};
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
    accel_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print(
            "sec3 ex03: live BMI270 accel chart - tilt the board");
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex03: done\n");
        status = show_finished(screen.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
