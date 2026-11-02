/*
 * 12_table_and_list.c -- สามค่าในตารางเดียว: Table, List, Line และ Picture
 *
 * ตารางสองคอลัมน์แยกค่ากับหน่วยไว้คนละช่อง คอลัมน์จึงไม่เลื่อนตามความยาวค่า
 * รายการมีไอคอนช่วยให้กวาดตาเจอบรรทัดที่ต้องการ ข้างล่างมีเส้นแนวโน้มกับไอคอน
 * ดาวหนึ่งใบ ทุกวินาทีโปรแกรมอ่านตัวหมุน ความเร่งแกน X และแผ่นสัมผัส แล้วเขียน
 * ทับช่องเดิมของตาราง ด้วย .cell() ตำแหน่งของตัวเลขจึงอยู่นิ่ง 40 รอบ
 *
 * A two-column table keeps each value apart from its label, so the
 * columns never shift. A list with icons lets the eye find a line
 * without reading every word; below them sit a trend line and a star
 * icon. Every second the program reads the knob, the X acceleration
 * and the touch slider and overwrites the same table cells, so the
 * numbers stay put. It runs 40 rounds.
 *
 * Twin of 12_table_and_list.py
 * (embedded-systems-for-aiot-developer/examples/s04/12_table_and_list.py),
 * rewritten from the library twin aiot-s04-12-table-and-list. Style:
 * CLEAN_C.md. tb.add_row(a, b) is bento_ui_add_row() with the cells
 * "a\0b", the rows numbered from 0 as modui.c counts them; "%.1f" is
 * bento_fmt_real_precision(); int(pot) truncates as (int32_t) does.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* The table's rows. */
enum {
    ROW_HEADER = 0,
    ROW_POT = 1,
    ROW_TILT = 2,
    ROW_TOUCH = 3,
    TABLE_ROWS = 4,
    VALUE_COLUMN = 1
};

/* Timing. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    ROUND_MS = 1000, /* a value people read never blinks faster */
    ROUNDS = 40,
    PRINT_EVERY = 10
};

enum {
    AUTO = -1, /* no w= or h= given: the widget's own size */
    POINT_COUNT = 6,
    CELLS_BYTES = 96, /* two cells and their NUL */
    TEXT_BYTES = 128  /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_THEME = 0 /* no color= given */
};

/* The widgets, in the order they are made. */
enum {
    W_TITLE,
    W_TABLE_CAPTION,
    W_TABLE,
    W_LIST_CAPTION,
    W_LIST,
    W_TREND_CAPTION,
    W_STAR,
    W_LINE,
    W_COUNT
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
} table_screen_t;

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

/* One table row of two cells, "first\0second". The table's setters
 * never raise in MicroPython, so their status is not checked. */
static void add_table_row(const table_screen_t *screen, uint8_t row,
                          const char *first, const char *second)
{
    char cells[CELLS_BYTES];
    const size_t first_bytes = strlen(first) + 1U; /* with its NUL */
    const size_t second_bytes = strlen(second) + 1U;

    if ((first_bytes + second_bytes) <= sizeof(cells)) {
        (void)memcpy(cells, first, first_bytes);
        (void)memcpy(&cells[first_bytes], second, second_bytes);
        /* the last cell's NUL is not sent */
        (void)bento_ui_add_row(
            screen->ui, screen->ids[W_TABLE], row, cells,
            (uint32_t)(first_bytes + second_bytes - 1U));
    }
}

/* An icon helps the eye find a line without reading every word. */
static void add_list_item(const table_screen_t *screen,
                          const char *text, uint8_t icon)
{
    bento_ui_item_t item;

    (void)memset(&item, 0, sizeof(item));
    item.struct_size = (uint32_t)sizeof(item);
    item.a = icon;
    item.text = text;
    item.text_len = (uint32_t)strlen(text);
    (void)bento_ui_item_append(screen->ui, screen->ids[W_LIST], &item);
}

/* The rows, the list items and the trend line's points. */
static void fill_screen(const table_screen_t *screen)
{
    static const int32_t points[POINT_COUNT][2] = {
        {0, 48}, {80, 16}, {160, 40}, {240, 8}, {320, 32}, {400, 24}};
    uint32_t i;

    add_table_row(screen, ROW_HEADER, "สิ่งที่วัด", "ค่า");
    add_table_row(screen, ROW_POT, "ตัวหมุน", "รอ");
    add_table_row(screen, ROW_TILT, "เอียงแกน X", "รอ");
    add_table_row(screen, ROW_TOUCH, "แผ่นสัมผัส", "รอ");
    add_list_item(screen, "ตัวหมุน", (uint8_t)BENTO_UI_ICON_SETTINGS);
    add_list_item(screen, "แผ่นสัมผัส", (uint8_t)BENTO_UI_ICON_EDIT);
    add_list_item(screen, "ตัววัดความเร่ง",
                  (uint8_t)BENTO_UI_ICON_REFRESH);
    for (i = 0U; i < (uint32_t)POINT_COUNT; i++) {
        (void)bento_ui_add_point(screen->ui, screen->ids[W_LINE],
                                 points[i][0], points[i][1]);
    }
}

/* ui.poll() here only lets the screen draw; the events are not used. */
static void let_screen_draw(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The screen, in creation order. A Table takes cols= in min and rows=
 * in max, as modui.c copies them. */
static bento_status_t add_screen_widgets(bento_ui_t *ui, uint32_t first,
                                         uint32_t count,
                                         bento_ui_id_t *ids)
{
    static const widget_spec_t screen_widgets[W_COUNT] = {
        {UI_WIDGET_LABEL, "สามค่าในตารางเดียว", 24, 16, AUTO, AUTO,
         COLOR_TEXT, 28, 0, 0},
        {UI_WIDGET_LABEL, "ค่าที่อ่านได้ตอนนี้", 24, 64, AUTO, AUTO, COLOR_DIM,
         20, 0, 0},
        {UI_WIDGET_TABLE, NULL, 24, 96, 352, 152, COLOR_THEME, 0, 2,
         TABLE_ROWS},
        {UI_WIDGET_LABEL, "เลือกดูเซ็นเซอร์", 416, 64, AUTO, AUTO,
         COLOR_DIM, 20, 0, 0},
        {UI_WIDGET_LIST, "เซ็นเซอร์บนบอร์ด", 416, 96, 352, 152,
         COLOR_THEME, 0, 0, 0},
        {UI_WIDGET_LABEL, "รูปร่างของค่าที่ผ่านมา", 24, 272, AUTO, AUTO,
         COLOR_DIM, 20, 0, 0},
        {UI_WIDGET_IMG, "star", 24, 304, 48, 48, COLOR_ACCENT, 0, 0, 0},
        {UI_WIDGET_LINE, NULL, 96, 304, AUTO, AUTO, COLOR_ACCENT, 0, 0,
         0}};

    return add_widgets(ui, &screen_widgets[first], count, &ids[first]);
}

static bento_status_t build_screen(table_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status =
            add_screen_widgets(screen->ui, 0U, W_COUNT, screen->ids);
    }
    if (status == BENTO_OK) {
        fill_screen(screen);
        let_screen_draw(screen->ui);
    }
    return status;
}

/* .cell() overwrites the same cell, so the number stays where it is
 * instead of flowing down the table. */
static void show_cell(const table_screen_t *screen, uint8_t row,
                      const char *text)
{
    (void)bento_ui_set_cell(screen->ui, screen->ids[W_TABLE], row,
                            VALUE_COLUMN, text, (uint32_t)strlen(text));
}

/* One reading of the three sensors into the table. A sensor that
 * fails raises in MicroPython, so its status ends the program. */
static bento_status_t show_readings(const table_screen_t *screen,
                                    int32_t round)
{
    char text[TEXT_BYTES];
    char number[BENTO_FMT_BUF_SIZE];
    float pot = 0.0F;
    float ax = 0.0F;
    float ay = 0.0F;
    float az = 0.0F;
    uint8_t slider = 0U;
    uint32_t length = 0U;
    bento_status_t status = bento_sensors_pot_percent(&pot);

    if (status == BENTO_OK) {
        status = bento_sensors_bmi270_acceleration(&ax, &ay, &az);
    }
    if (status == BENTO_OK) {
        status = bento_sensors_capsense_slider(&slider);
    }
    if (status == BENTO_OK) {
        status = bento_fmt_real_precision(number, BENTO_FMT_BUF_SIZE,
                                          (bento_real_t)ax, 1, &length);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%" PRId32 " %%",
                       (int32_t)pot);
        show_cell(screen, ROW_POT, text);
        (void)snprintf(text, sizeof(text), "%s m/s2", number);
        show_cell(screen, ROW_TILT, text);
        (void)snprintf(text, sizeof(text), "%u", (unsigned int)slider);
        show_cell(screen, ROW_TOUCH, text);
        if ((round % PRINT_EVERY) == 0) {
            (void)snprintf(text, sizeof(text), "ตัวหมุน %" PRId32 " %%",
                           (int32_t)pot);
            /* lcd.print() ignores a console that is not ready */
            (void)bento_lcd_print(text);
        }
    }
    return status;
}

static bento_status_t run_loop(const table_screen_t *screen)
{
    bento_status_t status = BENTO_OK;
    int32_t round;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = show_readings(screen, round);
        if (status == BENTO_OK) {
            let_screen_draw(screen->ui);
            status = pause_ms(ROUND_MS);
        }
    }
    return status;
}

int main(void)
{
    table_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console(
            "<h2>Table + List + Line + Picture</h2>");
        (void)printf("Table + List + Line + Picture พร้อมแสดงผล\n");
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_print(
            "<span class=ok>จบรอบ - ตารางอยู่นิ่งทั้งรอบ</span>");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
