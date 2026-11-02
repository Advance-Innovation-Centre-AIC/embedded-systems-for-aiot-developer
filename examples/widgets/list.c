/*
 * list.c -- รายการเลื่อนได้ ui.List (Widget: ui.List)
 *
 * รายการที่เลื่อนได้ หัวข้อของกลุ่มมาจาก text= ตอนสร้าง แต่ละบรรทัดเติมด้วย
 * .add_item(ข้อความ, รหัสไอคอน) โดยไอคอนเป็นเลข ui.ICON_* ไม่ใช่ตัวอักษร
 * ห้าบรรทัด บรรทัดสุดท้ายโผล่มาครึ่งเดียวเพราะรายการยาวกว่ากรอบ ตัวอย่างจบเองใน 20
 * วินาที ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์
 *
 * A scrolling list: the group's heading is the text= it is created
 * with, and each row is added with .add_item(text, icon), where the
 * icon is a ui.ICON_* number, not a character. Five rows, the last one
 * half visible because the list is longer than its frame. The example
 * ends by itself after 20 seconds and needs no network and no sensor.
 *
 * Twin of list.py
 * (embedded-systems-for-aiot-developer/examples/widgets/list.py),
 * merged into one file from the library twin aiot-widget-list.
 * Style: CLEAN_C.md.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* The rows and the run. */
enum {
    ROW_COUNT = 5,
    SIDE_NOTE_COUNT = 3,
    STEPS = 100,     /* 100 steps of STEP_MS: 20 seconds */
    SETTLE_MS = 200, /* let the cleared screen settle */
    STEP_MS = 200    /* one step of the loop */
};

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    LIST_Y = 72,
    LIST_W = 440,
    LIST_H = 264,
    SIDE_X = 496,
    SIDE_Y = 72,
    SIDE_STEP_Y = 40,
    FOOTER_Y = 344
};

/* Font sizes, in pixels. */
enum {
    TITLE_PX = 28,
    CAPTION_PX = 20
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF
};

/* One row of the list: its text and its ui.ICON_* number. */
typedef struct {
    const char *text;
    uint8_t icon;
} row_t;

static int16_t px(int32_t value)
{
    return (int16_t)value;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int32_t x, int32_t y, uint32_t color,
                                int32_t font_px)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = px(x);
    args.y = px(y);
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, &label);
}

static void add_row(bento_ui_t *ui, bento_ui_id_t list,
                    const row_t *row)
{
    bento_ui_item_t item;

    (void)memset(&item, 0, sizeof(item));
    item.struct_size = (uint32_t)sizeof(item);
    item.a = row->icon;
    item.text = row->text;
    item.text_len = (uint32_t)strlen(row->text);
    /* .add_item() is sent without waiting for an answer (modui.c), so a
     * refusal is never seen there and is not seen here either. */
    (void)bento_ui_item_append(ui, list, &item);
}

/* The list with its heading, then its five rows. */
static bento_status_t add_list(bento_ui_t *ui)
{
    static const row_t rows[ROW_COUNT] = {
        {"เครือข่ายไร้สาย", BENTO_UI_ICON_WIFI},
        {"แบตเตอรี", BENTO_UI_ICON_BATTERY},
        {"การแจ้งเตือน", BENTO_UI_ICON_BELL},
        {"ตั้งค่า", BENTO_UI_ICON_SETTINGS},
        {"เสียง", BENTO_UI_ICON_AUDIO},
    };
    bento_ui_widget_args_t args;
    bento_ui_id_t list;
    bento_status_t status;
    int32_t i;

    bento_ui_widget_args_init(&args);
    args.text = "อุปกรณ์ที่ต่ออยู่";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = MARGIN_X;
    args.y = LIST_Y;
    args.w = LIST_W;
    args.h = LIST_H;
    args.color = COLOR_TEXT;
    status = bento_ui_list_create(ui, &args, &list);
    if (status == BENTO_OK) {
        for (i = 0; i < ROW_COUNT; i++) {
            add_row(ui, list, &rows[i]);
        }
    }
    return status;
}

/* The three notes beside the list, one under the other. */
static bento_status_t add_side_notes(bento_ui_t *ui)
{
    static const char *const notes[SIDE_NOTE_COUNT] = {
        "ไอคอนคือ ui.ICON_*",
        "ไม่ใช่ตัวอักษร",
        "รายการเลื่อนได้",
    };
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < SIDE_NOTE_COUNT) && (status == BENTO_OK); i++) {
        status =
            add_label(ui, notes[i], SIDE_X, SIDE_Y + (i * SIDE_STEP_Y),
                      COLOR_DIM, CAPTION_PX);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
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
        status = add_label(ui, "ui.List", MARGIN_X, TITLE_Y, COLOR_TEXT,
                           TITLE_PX);
    }
    if (status == BENTO_OK) {
        status = add_list(ui);
    }
    if (status == BENTO_OK) {
        status = add_side_notes(ui);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "หัวข้อมาจาก text= ตอนสร้าง", MARGIN_X,
                           FOOTER_Y, COLOR_DIM, CAPTION_PX);
    }
    return status;
}

/* The original asks for the touch events every step and reads none of
 * them; ui.poll() answers no events when it fails, so its status is not
 * an error here either. */
static void skip_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* STEPS steps of STEP_MS, or until Stop is pressed. */
static bento_status_t run_steps(bento_ui_t *ui)
{
    bento_status_t status = BENTO_OK;
    int32_t step;

    for (step = 0; (step < STEPS) && (status == BENTO_OK); step++) {
        skip_events(ui);
        status = bento_time_sleep_ms(STEP_MS);
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
        status = run_steps(ui);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.List: text=หัวข้อกลุ่ม, .add_item(ข้อความ, "
                     "ui.ICON_*)\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
