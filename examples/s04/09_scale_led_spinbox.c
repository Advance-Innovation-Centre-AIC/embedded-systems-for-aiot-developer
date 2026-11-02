/*
 * 09_scale_led_spinbox.c -- สาม widget ที่แยกหน้าจอ HMI ออกจากหน้าจอเล่น ๆ
 *
 * ui.Scale (ไม้บรรทัดมีขีดและตัวเลข) ui.Led (ไฟสถานะ) และ ui.Spinbox (ป้อนเลข
 * ทีละหลัก) ประกอบเป็นแผงคุมอุณหภูมิหนึ่งแผง แถบค่าวางเหนือไม้บรรทัด 0-100
 * ไฟสถานะสามดวงติดทีละดวง โปรแกรมไล่ค่าขึ้นหกสิบก้าว ให้เห็นไฟสลับกันทำงาน
 * กับดัก: Scale ไม่มีเข็มและไม่รับ .value() ส่วน Led ที่ value(0) จะหรี่ ไม่ใช่หาย
 *
 * ui.Scale (a ruler with ticks and numbers), ui.Led (a status light)
 * and ui.Spinbox (digit-by-digit entry) make one temperature panel: a
 * bar laid over a 0-100 scale, three status lights of which one is lit
 * at a time, and a target spinbox. The program ramps the value in
 * sixty steps so the lights take turns. The trap: a Scale has no
 * needle and takes no .value(); an Led at value(0) dims, not vanishes.
 *
 * Twin of 09_scale_led_spinbox.py
 * (embedded-systems-for-aiot-developer/examples/s04/09_scale_led_spinbox.py),
 * written new in the style of the library twin aiot-s04-09-scale-led-spinbox
 * (similarity 0.37). Style: CLEAN_C.md. Every division has
 * non-negative operands, so C's / gives what floor division gives.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* The temperature range and its thresholds, in degrees. */
enum {
    T_MIN = 0,
    T_MAX = 100,
    WARN_AT = 60,
    ALARM_AT = 80
};

/* The ramp. */
enum {
    ROUNDS = 60,
    PRINT_EVERY = 12, /* a console line every this many rounds */
    SETTLE_MS = 200,  /* let the cleared screen settle */
    STEP_MS = 120
};

enum {
    AUTO = -1,       /* no w= or h= given: the widget's own size */
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* The widgets, in the order they are made. */
enum {
    W_TITLE,
    W_GAUGE_CARD,
    W_GAUGE_CAPTION,
    W_BAR,
    W_SCALE,
    W_TARGET_CARD,
    W_TARGET_CAPTION,
    W_TARGET,
    W_STATUS_CARD,
    W_STATUS_CAPTION,
    W_LED_OK,
    W_OK_NAME,
    W_LED_WARN,
    W_WARN_NAME,
    W_LED_BAD,
    W_MESSAGE,
    W_COUNT
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
} gauge_screen_t;

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

/* The screen, in creation order (creation order is drawing order).
 * The bar sits above the ruler: a Scale has no needle, so the bar is
 * what moves. */
static const widget_spec_t SCREEN_WIDGETS[W_COUNT] = {
    {UI_WIDGET_LABEL, "แผงคุมอุณหภูมิ", 20, 12, AUTO, AUTO, COLOR_TEXT, 24,
     0, 0},
    {UI_WIDGET_PANEL, NULL, 20, 48, 740, 112, COLOR_CARD, 1, COLOR_DIM,
     12},
    {UI_WIDGET_LABEL, "อุณหภูมิเทียบพิสัยใช้งาน", 40, 60, AUTO, AUTO,
     COLOR_DIM, 20, 0, 0},
    {UI_WIDGET_BAR, NULL, 40, 84, 660, 16, COLOR_OK, 0, T_MIN, T_MAX},
    {UI_WIDGET_SCALE, NULL, 40, 104, 660, 48, COLOR_TEXT, 0, T_MIN,
     T_MAX},
    {UI_WIDGET_PANEL, NULL, 20, 176, 360, 120, COLOR_CARD, 1, COLOR_DIM,
     12},
    {UI_WIDGET_LABEL, "เป้าหมาย (องศา)", 40, 188, AUTO, AUTO, COLOR_DIM,
     20, 0, 0},
    {UI_WIDGET_SPINBOX, NULL, 40, 220, 200, 88, COLOR_OK, 45, T_MIN,
     T_MAX},
    {UI_WIDGET_PANEL, NULL, 400, 176, 360, 120, COLOR_CARD, 1,
     COLOR_DIM, 12},
    {UI_WIDGET_LABEL, "สถานะ", 420, 188, AUTO, AUTO, COLOR_DIM, 20, 0,
     0},
    {UI_WIDGET_LED, NULL, 428, 224, 48, 48, COLOR_OK, 1, 0, 0},
    {UI_WIDGET_LABEL, "ปกติ", 480, 232, AUTO, AUTO, COLOR_DIM, 20, 0, 0},
    {UI_WIDGET_LED, NULL, 560, 224, 48, 48, COLOR_WARN, 0, 0, 0},
    {UI_WIDGET_LABEL, "เฝ้าระวัง", 612, 280, AUTO, AUTO, COLOR_DIM, 20, 0,
     0},
    {UI_WIDGET_LED, NULL, 700, 224, 48, 48, COLOR_BAD, 0, 0, 0},
    {UI_WIDGET_LABEL, "ค่าลอย ๆ ไม่บอกว่าสูงไหม มาตรวัดบอก", 20, 312, AUTO,
     AUTO, COLOR_DIM, 20, 0, 0}};

/* ui.poll() here only lets the screen draw; the events are not used. */
static void let_screen_draw(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t build_screen(gauge_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_widgets(screen->ui, SCREEN_WIDGETS, W_COUNT,
                             screen->ids);
    }
    if (status == BENTO_OK) {
        let_screen_draw(screen->ui);
    }
    return status;
}

/* One light only: a panel with several lit at once cannot be read.
 * An Led at 0 dims, it does not vanish. */
static void show_lights(const gauge_screen_t *screen, int32_t degrees)
{
    bento_ui_t *ui = screen->ui;

    (void)bento_ui_set_value(ui, screen->ids[W_LED_OK],
                             (degrees < WARN_AT) ? 1 : 0);
    (void)bento_ui_set_value(
        ui, screen->ids[W_LED_WARN],
        ((degrees >= WARN_AT) && (degrees < ALARM_AT)) ? 1 : 0);
    (void)bento_ui_set_value(ui, screen->ids[W_LED_BAD],
                             (degrees >= ALARM_AT) ? 1 : 0);
}

/* The bar's colour and the message follow the zone. */
static void show_zone(const gauge_screen_t *screen, int32_t degrees)
{
    bento_ui_t *ui = screen->ui;
    const bento_ui_id_t message = screen->ids[W_MESSAGE];
    char text[TEXT_BYTES];
    uint32_t bar_color = COLOR_OK;
    uint32_t message_color = COLOR_DIM;

    if (degrees >= ALARM_AT) {
        bar_color = COLOR_BAD;
        message_color = COLOR_BAD;
        (void)snprintf(text, sizeof(text), "เกินเกณฑ์อันตราย %d องศา",
                       ALARM_AT);
    } else if (degrees >= WARN_AT) {
        bar_color = COLOR_WARN;
        message_color = COLOR_WARN;
        (void)snprintf(text, sizeof(text), "เข้าเขตเฝ้าระวัง %d องศา",
                       WARN_AT);
    } else {
        (void)snprintf(text, sizeof(text), "%s", "อยู่ในพิสัยปกติ");
    }
    (void)bento_ui_set_color(ui, screen->ids[W_BAR], bar_color);
    show_text(ui, message, text);
    (void)bento_ui_set_color(ui, message, message_color);
}

/* Ramps the value up so the three lights take turns. */
static bento_status_t run_ramp(const gauge_screen_t *screen)
{
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < ROUNDS) && (status == BENTO_OK); i++) {
        const int32_t degrees =
            T_MIN + (((T_MAX - T_MIN) * i) / (ROUNDS - 1));

        (void)bento_ui_set_value(screen->ui, screen->ids[W_BAR],
                                 degrees);
        show_lights(screen, degrees);
        show_zone(screen, degrees);
        if ((i % PRINT_EVERY) == 0) {
            (void)snprintf(text, sizeof(text), "อุณหภูมิ %" PRId32 " องศา",
                           degrees);
            /* lcd.print() ignores a console that is not ready */
            (void)bento_lcd_print(text);
        }
        let_screen_draw(screen->ui);
        status = pause_ms(STEP_MS);
    }
    return status;
}

int main(void)
{
    gauge_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>Scale + Led + Spinbox</h2>");
        status = run_ramp(&screen);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_print(
            "<span class=ok>จบรอบ - ไล่ครบทั้งสามสถานะ</span>");
        (void)printf(
            "Scale บอกพิสัย · Led บอกสถานะ · Spinbox รับค่าจากคน\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
