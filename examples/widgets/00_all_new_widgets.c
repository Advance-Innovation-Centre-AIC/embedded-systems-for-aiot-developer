/*
 * 00_all_new_widgets.c -- widget ใหม่สี่ตัวบนจอเดียว (All New Widgets)
 *
 * สร้าง Table, Roller, Led และ Spinbox พร้อมกันบนจอเดียวพร้อมข้อความไทย
 * แล้วค้างไว้ราว 20 วินาทีให้ตรวจด้วยตาบนบอร์ดจริง: ทุกตัวต้องเห็นครบทั้งตัว
 * ข้อความไทยต้องเป็นตัวอักษร ไม่ใช่กล่องสี่เหลี่ยม ตารางต้องเห็นสามแถว
 * วงล้อต้องไม่ตัดกลางคำ และไฟดวงที่สี่ต้องหรี่ ไม่ใช่หายไป ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์
 * จบเอง
 *
 * Creates a Table, a Roller, four Leds and a Spinbox on one screen with
 * Thai texts, then holds them for about 20 seconds for a visual check
 * on a real board: every widget must be whole, Thai must show as
 * letters rather than boxes, the table must show three rows, the roller
 * must not cut a word, and the fourth LED must be dimmed, not gone. No
 * network and no sensor needed; it ends by itself.
 *
 * Twin of 00_all_new_widgets.py
 * (embedded-systems-for-aiot-developer/examples/widgets/00_all_new_widgets.py),
 * merged into one file from the library twin aiot-widget-00-all-new-widgets.
 * Style: CLEAN_C.md.
 * Stop is not caught by the original: a Stop in any sleep ends the
 * program here too.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

enum {
    TABLE_COLS = 2,
    TABLE_ROWS = 3,
    LED_COUNT = 4,
    SPINBOX_MAX = 9999,
    SPINBOX_START = 2375,
    SETTLE_MS = 200,   /* let the cleared screen settle */
    HOLD_PASSES = 100, /* hold the screen for 100 x 200 ms */
    HOLD_MS = 200
};

/* Screen layout on the 792 x 398 canvas: the title line, then the table
 * and the roller, then the LEDs and the spinbox (everything stops at x
 * = 664, clear of the Console button). */
enum {
    MARGIN_X = 24,
    TITLE_Y = 4,
    TOP_ROW_Y = 40,
    /* a row is about 72 px at font 20: three rows need 224, not 192 */
    TOP_ROW_H = 224,
    TABLE_W = 440,
    NAME_COL_W = 232,
    VALUE_COL_W = 192,
    ROLLER_X = 488,
    ROLLER_W = 264,
    LED_Y = 288,
    LED_SIZE = 64,
    LED_X0 = 24,
    LED_STEP = 88,
    SPINBOX_X = 488,
    SPINBOX_Y = 280,
    SPINBOX_W = 176,
    /* 88, not 64: the + and - buttons are touch targets */
    SPINBOX_H = 88
};

/* The firmware has five font sizes and rounds up; 24 and 20 are the
 * readable ones. */
enum {
    FONT_TITLE = 24,
    FONT_BODY = 20
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_OK = 0x30A46C
};

/* A rectangle on the canvas. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

/* The one narrowing into the API's int16_t positions. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

static bento_status_t add_title(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t title;

    bento_ui_widget_args_init(&args);
    args.text = "Table / Roller / Led / Spinbox";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = MARGIN_X;
    args.y = TITLE_Y;
    args.color = COLOR_TEXT;
    args.font_px = FONT_TITLE;
    return bento_ui_label_create(ui, &args, &title);
}

static void args_for(bento_ui_widget_args_t *args, rect_t place,
                     uint32_t color)
{
    bento_ui_widget_args_init(args);
    args->x = place.x;
    args->y = place.y;
    args->w = place.w;
    args->h = place.h;
    args->color = color;
}

/* The column widths, then three rows of two cells. They are sent
 * without a reply, and MicroPython's .col_width() and .add_row() return
 * nothing, so their status is not a failure here either. */
static void fill_table(bento_ui_t *ui, bento_ui_id_t table)
{
    static const char *const cells[TABLE_ROWS][TABLE_COLS] = {
        {"เซนเซอร์", "ค่าที่อ่านได้"},
        {"อุณหภูมิ", "25.4 องศา"},
        {"ความชื้น", "55 เปอร์เซ็นต์"}};
    uint8_t row;
    uint8_t col;

    (void)bento_ui_set_col_width(ui, table, 0, NAME_COL_W);
    (void)bento_ui_set_col_width(ui, table, 1, VALUE_COL_W);
    for (row = 0U; row < (uint8_t)TABLE_ROWS; row++) {
        for (col = 0U; col < (uint8_t)TABLE_COLS; col++) {
            const char *cell = cells[row][col];

            (void)bento_ui_set_cell(ui, table, row, col, cell,
                                    (uint32_t)strlen(cell));
        }
    }
}

static bento_status_t add_table(bento_ui_t *ui)
{
    const rect_t place = {MARGIN_X, TOP_ROW_Y, TABLE_W, TOP_ROW_H};
    bento_ui_widget_args_t args;
    bento_ui_id_t table;
    bento_status_t status;

    args_for(&args, place, COLOR_TEXT);
    args.min_val = TABLE_COLS;
    args.max_val = TABLE_ROWS;
    args.font_px = FONT_BODY;
    status = bento_ui_table_create(ui, &args, &table);
    if (status == BENTO_OK) {
        fill_table(ui, table);
    }
    return status;
}

/* The roller's three options; .add_option() answers nothing in
 * MicroPython either. */
static bento_status_t add_roller(bento_ui_t *ui)
{
    static const char *const options[] = {"ปกติ", "เฝ้าระวัง", "หยุดฉุกเฉิน"};
    const rect_t place = {ROLLER_X, TOP_ROW_Y, ROLLER_W, TOP_ROW_H};
    bento_ui_widget_args_t args;
    bento_ui_id_t roller;
    bento_status_t status;
    uint32_t i;

    args_for(&args, place, COLOR_TEXT);
    args.font_px = FONT_BODY;
    status = bento_ui_roller_create(ui, &args, &roller);
    for (i = 0U; (i < (sizeof(options) / sizeof(options[0]))) &&
                 (status == BENTO_OK);
         i++) {
        (void)bento_ui_add_option(ui, roller, options[i],
                                  (uint32_t)strlen(options[i]));
    }
    return status;
}

/* Four status LEDs; the fourth is created off and must show dimmed,
 * not vanish. */
static bento_status_t add_leds(bento_ui_t *ui)
{
    static const uint32_t colors[LED_COUNT] = {COLOR_OK, COLOR_WARN,
                                               COLOR_BAD, COLOR_OK};
    static const bool lit[LED_COUNT] = {true, true, true, false};
    bento_ui_widget_args_t args;
    bento_ui_id_t led;
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < LED_COUNT) && (status == BENTO_OK); i++) {
        const rect_t place = {px(LED_X0 + (i * LED_STEP)), LED_Y,
                              LED_SIZE, LED_SIZE};

        args_for(&args, place, colors[i]);
        args.font_px = lit[i] ? 1 : 0;
        status = bento_ui_led_create(ui, &args, &led);
    }
    return status;
}

static bento_status_t add_spinbox(bento_ui_t *ui)
{
    const rect_t place = {SPINBOX_X, SPINBOX_Y, SPINBOX_W, SPINBOX_H};
    bento_ui_widget_args_t args;
    bento_ui_id_t spinbox;

    args_for(&args, place, COLOR_OK);
    args.min_val = 0;
    args.max_val = SPINBOX_MAX;
    args.font_px = SPINBOX_START;
    return bento_ui_spinbox_create(ui, &args, &spinbox);
}

/* ui.poll() here only keeps the display's queue moving: its events are
 * not used, and a failed poll answers none in MicroPython, so its
 * status is not a failure. */
static void poll_display(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* Draws the screen in the original's order (the widgets' handles
 * follow it). */
static bento_status_t build_screen(bento_ui_t *ui)
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
    if (status == BENTO_OK) {
        /* the lcd calls ignore a console that is not ready
         * (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ตรวจ widget ใหม่</h2>");
        status = add_title(ui);
    }
    if (status == BENTO_OK) {
        status = add_table(ui);
    }
    if (status == BENTO_OK) {
        status = add_roller(ui);
    }
    if (status == BENTO_OK) {
        status = add_leds(ui);
    }
    if (status == BENTO_OK) {
        status = add_spinbox(ui);
    }
    return status;
}

/* Keeps the screen up for the visual check; nothing needs pressing. */
static bento_status_t hold_screen(bento_ui_t *ui)
{
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < HOLD_PASSES) && (status == BENTO_OK); i++) {
        poll_display(ui);
        status = bento_time_sleep_ms(HOLD_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_status_t status = build_screen(ui);

    if (status == BENTO_OK) {
        poll_display(ui);
        (void)bento_lcd_print("ตรวจด้วยตา: ทุกตัวเห็นครบทั้งตัวไหม");
        (void)bento_lcd_print("และข้อความไทยเป็นตัวอักษร "
                              "ไม่ใช่กล่องสี่เหลี่ยม");
        status = hold_screen(ui);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_print("<span class=ok>จบการตรวจ</span>");
        (void)printf("ถ้าทุกตัวเห็นครบและอ่านภาษาไทยออก แปลว่า widget "
                     "ใหม่ใช้ได้ทั้งชุด\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
