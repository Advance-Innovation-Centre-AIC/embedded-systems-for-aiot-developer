/*
 * tileview.c -- ผืนใหญ่ที่ปัดไปทีละช่อง (Tileview Widget)
 *
 * ผืนใหญ่ที่แบ่งเป็นสามช่อง เห็นช่องแรกเต็มพื้นที่ ปัดไปทางซ้ายเจอช่องขวา ปัดขึ้นเจอช่องล่าง
 * แต่ละช่องมีป้ายของตัวเอง โปรแกรมรอ 20 วินาทีแล้วจบเอง ไม่ต้องต่อเน็ต
 * ไม่ต้องมีเซนเซอร์
 *
 * A large area split into three tiles: the first fills the area, a
 * swipe left shows the right tile and a swipe up shows the tile below.
 * Each tile has its own labels. The program waits 20 seconds and ends
 * by itself; it needs no network and no sensor.
 *
 * Twin of tileview.py
 * (embedded-systems-for-aiot-developer/examples/widgets/tileview.py),
 * merged into one file from the library twin wref-tileview.
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
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

/* Layout on the 792 x 398 canvas; a tile's children count from the
 * tile's top-left corner. */
enum {
    TEXT_X = 24, /* on the screen and inside a tile alike */
    TITLE_Y = 16,
    TITLE_FONT_PX = 28,
    TILEVIEW_X = 24,
    TILEVIEW_Y = 72,
    TILEVIEW_W = 744,
    TILEVIEW_H = 248,
    TILE_HEADING_Y = 24,
    TILE_NOTE_Y = 72,
    HEADING_FONT_PX = 24,
    NOTE_FONT_PX = 20,
    FOOTER_Y = 344
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF
};

/* The three tiles, each the parent of its own labels. */
typedef struct {
    bento_ui_id_t home;
    bento_ui_id_t right;
    bento_ui_id_t down;
} tiles_t;

/* A label inside `parent` (0: the screen). */
static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                int16_t font_px, bento_ui_id_t parent)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = TEXT_X;
    args.y = y;
    args.color = color;
    args.font_px = font_px;
    args.parent = parent;
    return bento_ui_label_create(ui, &args, &label);
}

/* The tileview and its tiles: (column, row) and the directions a
 * swipe may leave each one. */
static bento_status_t add_tileview(bento_ui_t *ui, tiles_t *tiles)
{
    const uint8_t home_directions =
        (uint8_t)BENTO_UI_DIR_RIGHT | (uint8_t)BENTO_UI_DIR_BOTTOM;
    bento_ui_widget_args_t args;
    bento_ui_id_t tileview;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = TILEVIEW_X;
    args.y = TILEVIEW_Y;
    args.w = TILEVIEW_W;
    args.h = TILEVIEW_H;
    args.color = COLOR_CARD;
    status = bento_ui_tileview_create(ui, &args, &tileview);
    if (status == BENTO_OK) {
        status = bento_ui_add_tile(ui, tileview, 0U, 0U,
                                   home_directions, &tiles->home);
    }
    if (status == BENTO_OK) {
        status = bento_ui_add_tile(ui, tileview, 1U, 0U,
                                   BENTO_UI_DIR_LEFT, &tiles->right);
    }
    if (status == BENTO_OK) {
        status = bento_ui_add_tile(ui, tileview, 0U, 1U,
                                   BENTO_UI_DIR_TOP, &tiles->down);
    }
    return status;
}

static bento_status_t fill_tiles(bento_ui_t *ui, const tiles_t *tiles)
{
    bento_status_t status =
        add_label(ui, "ช่องแรก - ปัดซ้ายหรือขึ้น", TILE_HEADING_Y, COLOR_TEXT,
                  HEADING_FONT_PX, tiles->home);

    if (status == BENTO_OK) {
        status = add_label(ui, "กราฟรวมอยู่ที่นี่ได้", TILE_NOTE_Y, COLOR_DIM,
                           NOTE_FONT_PX, tiles->home);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ช่องขวา - รายละเอียด", TILE_HEADING_Y,
                           COLOR_ACCENT, HEADING_FONT_PX, tiles->right);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ช่องล่าง - ประวัติ", TILE_HEADING_Y,
                           COLOR_ACCENT, HEADING_FONT_PX, tiles->down);
    }
    return status;
}

static bento_status_t build_screen(bento_ui_t *ui)
{
    tiles_t tiles = {0U, 0U, 0U};
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ui.Tileview", TITLE_Y, COLOR_TEXT,
                           TITLE_FONT_PX, 0U);
    }
    if (status == BENTO_OK) {
        status = add_tileview(ui, &tiles);
    }
    if (status == BENTO_OK) {
        status = fill_tiles(ui, &tiles);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "สามช่อง กินสี่แฮนเดิล", FOOTER_Y, COLOR_DIM,
                           NOTE_FONT_PX, 0U);
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
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_status_t status = build_screen(ui);

    if (status == BENTO_OK) {
        status = run_event_loop(ui);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.Tileview: .add_tile(col, row, dir) คืนช่อง "
                     "แล้วส่งช่องนั้นเป็น parent=\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
