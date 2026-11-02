/*
 * table.c -- ตารางสองคอลัมน์ (Table Widget)
 *
 * ตารางสองคอลัมน์ สี่แถว หัวตารางเป็นภาษาไทย คอลัมน์ซ้ายกว้างกว่าขวา เติมทีละแถว
 * ข้างตารางมีป้ายอธิบายสามบรรทัด โปรแกรมรอ 20 วินาทีแล้วจบเอง ไม่ต้องต่อเน็ต
 * ไม่ต้องมีเซนเซอร์
 *
 * A two-column, four-row table with a Thai header row, the left column
 * wider than the right, filled one row at a time, with three lines of
 * notes beside it. The program waits 20 seconds and ends by itself; it
 * needs no network and no sensor.
 *
 * Twin of table.py
 * (embedded-systems-for-aiot-developer/examples/widgets/table.py),
 * merged into one file from the library twin wref-table.
 *
 * Style: CLEAN_C.md. What differs from MicroPython only because the
 * language forces it:
 *  - Table.add_row() fills the next row cell by cell; here each cell is
 *    one bento_ui_set_cell() and the program counts the rows itself.
 *  - Stop during a sleep raises KeyboardInterrupt in MicroPython; here
 *    the sleep returns and the program ends at that point.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Timing. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 200,
    LOOPS = 100 /* 100 x 200 ms = 20 seconds */
};

/* The widgets the program fills after it has created them. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t table;
    uint8_t rows; /* rows added so far */
} table_screen_t;

/* Stop is a status: a sleep that Stop ended answers
 * BENTO_E_CANCELLED. */
static bento_status_t sleep_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    TITLE_FONT_PX = 28,
    TABLE_Y = 72,
    TABLE_W = 520,
    /* four rows at 24 px need a little more than 272: no scroll bar */
    TABLE_H = 288,
    TABLE_COLUMNS = 2,
    TABLE_ROWS = 4,
    CELL_FONT_PX = 24,
    /* 312 + 192 = 504, not 520: the table keeps its own inner border */
    LEFT_COLUMN_W = 312,
    RIGHT_COLUMN_W = 192,
    NOTE_X = 576,
    NOTE_Y = 96,
    NOTE_STEP_Y = 40,
    NOTE_FONT_PX = 20
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF
};

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t x, int16_t y, uint32_t color,
                                int16_t font_px)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, &label);
}

static bento_status_t add_table(table_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = TABLE_Y;
    args.w = TABLE_W;
    args.h = TABLE_H;
    args.color = COLOR_TEXT;
    args.min_val = TABLE_COLUMNS;
    args.max_val = TABLE_ROWS;
    args.font_px = CELL_FONT_PX;
    status = bento_ui_table_create(screen->ui, &args, &screen->table);
    if (status == BENTO_OK) {
        /* MicroPython's widget setters ignore a refusal (modui.c),
         * so the twin does too. */
        (void)bento_ui_set_col_width(screen->ui, screen->table, 0,
                                     LEFT_COLUMN_W);
        (void)bento_ui_set_col_width(screen->ui, screen->table, 1,
                                     RIGHT_COLUMN_W);
    }
    return status;
}

/* One row of two cells, below the rows already added. */
static void add_row(table_screen_t *screen, const char *left,
                    const char *right)
{
    /* a cell's refusal is ignored, as MicroPython's add_row()
     * ignores it */
    (void)bento_ui_set_cell(screen->ui, screen->table, screen->rows, 0U,
                            left, (uint32_t)strlen(left));
    (void)bento_ui_set_cell(screen->ui, screen->table, screen->rows, 1U,
                            right, (uint32_t)strlen(right));
    screen->rows++;
}

static void fill_table(table_screen_t *screen)
{
    add_row(screen, "รายการ", "ค่าที่อ่านได้");
    add_row(screen, "อุณหภูมิ", "25.4 องศา");
    add_row(screen, "ความชื้น", "55 เปอร์เซ็นต์");
    add_row(screen, "แรงดันไฟ", "3.28 โวลต์");
}

static bento_status_t add_notes(bento_ui_t *ui)
{
    bento_status_t status = add_label(ui, "สองคอลัมน์ สี่แถว", NOTE_X,
                                      NOTE_Y, COLOR_DIM, NOTE_FONT_PX);

    if (status == BENTO_OK) {
        status =
            add_label(ui, "เติมทีละช่อง", NOTE_X, NOTE_Y + NOTE_STEP_Y,
                      COLOR_DIM, NOTE_FONT_PX);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "คอลัมน์กว้างต่างกัน", NOTE_X,
                           NOTE_Y + (2 * NOTE_STEP_Y), COLOR_DIM,
                           NOTE_FONT_PX);
    }
    return status;
}

static bento_status_t build_screen(table_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ui.Table", MARGIN_X, TITLE_Y,
                           COLOR_TEXT, TITLE_FONT_PX);
    }
    if (status == BENTO_OK) {
        status = add_table(screen);
    }
    if (status == BENTO_OK) {
        fill_table(screen);
        status = add_notes(screen->ui);
    }
    return status;
}

/* Reads the touch events every LOOP_MS, LOOPS times; nothing on this
 * screen reacts to them. */
static bento_status_t run_event_loop(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    int32_t loop;
    bento_status_t status = BENTO_OK;

    for (loop = 0; (loop < LOOPS) && (status == BENTO_OK); loop++) {
        /* ui.poll() answers no events when it fails, and the events
         * are not used */
        (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX,
                            &count, NULL);
        status = sleep_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    table_screen_t screen = {bento_runtime_get_ui(), 0U, 0U};
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        status = run_event_loop(screen.ui);
    }
    if (status == BENTO_OK) {
        (void)printf(
            "ui.Table: min=คอลัมน์ max=แถว value=ขนาดตัวอักษรในช่อง\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
