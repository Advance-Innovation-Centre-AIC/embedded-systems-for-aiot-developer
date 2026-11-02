/*
 * 01_topic_design.c -- ออกแบบชื่อ topic ก่อนเขียนโค้ดส่ง
 *
 * topic คือที่อยู่ของข้อความ โครง ราก/ตัวตน/ชนิด ทำให้ทุกใบไม่ชนกัน และฝั่งรับ
 * กรองได้ตั้งแต่ชั้น broker การ์ดบนคือ topic ที่ publish ได้จริง (เขียว)
 * การ์ดล่างคือ wildcard ที่ subscribe ได้อย่างเดียว (ส้ม) แล้ว lcd เก็บกฎสามข้อ
 * ไว้ให้อ่านย้อนหลัง ไฟล์นี้ไม่ต่อเน็ตเลย เป็นแผนภาพนิ่ง วาดครั้งเดียวแล้วจบ
 *
 * A topic is the address of a message. Root / identity / kind keeps every
 * board apart and lets a receiver filter at the broker. The top card holds
 * the topics that can really be published (green), the bottom card the
 * wildcards that may only be subscribed (orange); the lcd console then keeps
 * three rules to read later. It connects to no network: a still diagram,
 * drawn once.
 *
 * Twin of 01_topic_design.py
 * (embedded-systems-for-aiot-developer/examples/s10/01_topic_design.py),
 * from the library twin aiot-s10-01-topic-design. Style: CLEAN_C.md.
 * What C changes: the string concatenations and lcd.print(a, b, c) (one
 * space between the arguments) are snprintf into one buffer; a widget that
 * fails ends the program with its status, as the raise does in MicroPython.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
static const char DEVICE_ID[] =
    "team03"; /* ชื่อเดียวกับที่ขึ้นทะเบียนไว้ ยาวไม่เกิน 31 ตัวอักษร */
static const char ROOT[] =
    "bento"; /* รากของทั้งโครงการ กันชนกับข้อความของกลุ่มอื่นบน broker เดียวกัน */
/* ---- end of settings ---- */

enum { /* layout: grid 8, outer margin 24 */
    MARGIN_X = 24,
    TITLE_Y = 8,
    ROW_X = 40,
    CARD_W = 744,
    TOP_CARD_Y = 56,
    TOP_CARD_H = 192,
    TOP_HEAD_Y = 72,
    TOP_ROWS_Y = 112,
    ROW_STEP_Y = 32,
    BOTTOM_CARD_Y = 264,
    BOTTOM_CARD_H = 120,
    BOTTOM_HEAD_Y = 280,
    WILDCARD_ONE_Y = 312,
    WILDCARD_ALL_Y = 344
};
enum { /* fonts, card shape, times, sizes */
    FONT_TITLE = 28,
    FONT_ROW = 20,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    SETTLE_MS = 200,
    TEXT_BYTES = 128 /* the display carries at most 127 bytes */
};
enum { /* the course palette, one value per role (SPEC S7.13) */
    COLOR_TEXT = 0xE8EAED,
    COLOR_CARD = 0x171B22,
    COLOR_PUBLISH = 0x30A46C, /* COL_OK: an address that really works */
    COLOR_SUBSCRIBE = 0xF5A623 /* COL_WARN: never put it in a publish */
};

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

static int16_t px(int32_t value)
{
    return (int16_t)value;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const rect_t *place, uint32_t color,
                                int32_t font_px)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t id;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, &id);
}

static bento_status_t add_card(bento_ui_t *ui, const rect_t *place)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t id;

    bento_ui_widget_args_init(&args);
    args.x = place->x;
    args.y = place->y;
    args.w = place->w;
    args.h = place->h;
    args.color = (uint32_t)COLOR_CARD;
    args.min_val = COLOR_CARD;
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &id);
}

/* The top card: four real addresses, all built from the same ROOT and
 * DEVICE_ID; the gaps around "/" show which layer stays and which changes. */
static bento_status_t add_publish_card(bento_ui_t *ui)
{
    static const char *const kinds[] = {"telemetry", "event", "status",
                                        "command"};
    const rect_t card = {MARGIN_X, TOP_CARD_Y, CARD_W, TOP_CARD_H};
    const rect_t head = {ROW_X, TOP_HEAD_Y, 0, 0};
    char text[TEXT_BYTES];
    uint32_t i;
    bento_status_t status = add_card(ui, &card);

    if (status == BENTO_OK) {
        status = add_label(ui, "PUBLISH ได้จริง - ราก / ตัวตน / ชนิด",
                           &head, (uint32_t)COLOR_PUBLISH, FONT_ROW);
    }
    for (i = 0U; (i < (sizeof(kinds) / sizeof(kinds[0]))) &&
                 (status == BENTO_OK);
         i++) {
        const rect_t row = {
            ROW_X, px(TOP_ROWS_Y + ((int32_t)i * ROW_STEP_Y)), 0, 0};

        (void)snprintf(text, sizeof(text), "%s / %s / %s", ROOT,
                       DEVICE_ID, kinds[i]);
        status = add_label(ui, text, &row, (uint32_t)COLOR_PUBLISH,
                           FONT_ROW);
    }
    return status;
}

/* The bottom card looks alike in another colour, because it works the
 * other way; its rows end before x=690, where the Console button sits. */
static bento_status_t add_wildcard_card(bento_ui_t *ui)
{
    const rect_t card = {MARGIN_X, BOTTOM_CARD_Y, CARD_W,
                         BOTTOM_CARD_H};
    const rect_t head = {ROW_X, BOTTOM_HEAD_Y, 0, 0};
    const rect_t one = {ROW_X, WILDCARD_ONE_Y, 0, 0};
    const rect_t all = {ROW_X, WILDCARD_ALL_Y, 0, 0};
    char text[TEXT_BYTES];
    bento_status_t status = add_card(ui, &card);

    if (status == BENTO_OK) {
        status = add_label(ui, "SUBSCRIBE เท่านั้น - ห้ามใส่ใน publish",
                           &head, (uint32_t)COLOR_SUBSCRIBE, FONT_ROW);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "%s/+/telemetry   = telemetry ของทุกอุปกรณ์", ROOT);
        status = add_label(ui, text, &one, (uint32_t)COLOR_SUBSCRIBE,
                           FONT_ROW);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "%s/%s/#   = ทุกข้อความของตัวเราเอง", ROOT,
                       DEVICE_ID);
        status = add_label(ui, text, &all, (uint32_t)COLOR_SUBSCRIBE,
                           FONT_ROW);
    }
    return status;
}

static bento_status_t draw_diagram(bento_ui_t *ui)
{
    const rect_t title = {MARGIN_X, TITLE_Y, 0, 0};
    bento_status_t status = add_label(ui, "ต้นไม้ของ topic", &title,
                                      (uint32_t)COLOR_TEXT, FONT_TITLE);

    if (status == BENTO_OK) {
        status = add_publish_card(ui);
    }
    if (status == BENTO_OK) {
        status = add_wildcard_card(ui);
    }
    return status;
}

/* The screen keeps the shape, the lcd drawer keeps the reasons; lcd.print()
 * ignores a console that is not ready (modlcd.c), so its status is too. */
static void print_rules(void)
{
    char text[TEXT_BYTES];

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>คาบ 10 - ออกแบบ topic</h2>");
    (void)bento_lcd_print("1) ห้ามขึ้นต้นด้วย /");
    (void)snprintf(text, sizeof(text),
                   "   /%s/%s/telemetry คนละที่กับ %s/%s/telemetry", ROOT,
                   DEVICE_ID, ROOT, DEVICE_ID);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print("2) device id อยู่ใน topic");
    (void)bento_lcd_print("   ฝั่งรับกรองได้โดยไม่ต้องแกะ payload");
    (void)bento_lcd_print("3) แยก telemetry / event / status");
    (void)bento_lcd_print("   คนละจังหวะ คนละคนสนใจ");
    (void)snprintf(text, sizeof(text),
                   "ระวัง %s/# = ทุกอย่างใต้ราก ปริมาณเยอะมาก", ROOT);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print("ทั้งไฟล์นี้ไม่ได้ต่อเน็ตเลย");
    (void)bento_lcd_print("เพราะออกแบบ topic ทำบนกระดาษได้ก่อนเสมอ");
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    bento_status_t status;

    /* MicroPython ignores a refused ui.screen(): the runtime already set it up */
    (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = bento_time_sleep_ms((uint32_t)SETTLE_MS);
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = draw_diagram(ui);
    }
    if (status == BENTO_OK) {
        /* the poll's events are thrown away, as in the original */
        (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX,
                            &count, NULL);
        print_rules();
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
