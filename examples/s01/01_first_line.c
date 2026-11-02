/*
 * 01_first_line.c -- บรรทัดแรกที่ขึ้นจอบอร์ด (First line on the board)
 *
 * ไฟล์นี้สอนว่าข้อความหนึ่งบรรทัดไปได้สามปลายทาง และแต่ละทางอยู่คนละที่
 * ป้ายบนหน้า Playground เห็นทันที สองบรรทัดของ lcd รออยู่ในลิ้นชัก Console
 * มุมขวาล่าง และ printf() ขึ้นที่คอนโซลฝั่งคอม ไม่ได้ขึ้นบนจอบอร์ด
 * กับดัก: ถ้าไม่ได้แตะการ์ด Playground บนหน้า Home ก่อน จอจะเงียบสนิท
 *
 * One line of text can go to three places, each somewhere else: a
 * label on the Playground screen (seen at once), two lcd lines in the
 * Console drawer (bottom right), and printf() on the computer's
 * console, not on the board. Trap: open the Playground card on the
 * Home page first, or the screen stays silent although the code ran.
 *
 * Twin of 01_first_line.py
 * (embedded-systems-for-aiot-developer/examples/s01/01_first_line.py).
 * Style: CLEAN_C.md.
 * Where C differs: lcd.print("...", 1 + 1) joins its arguments with one
 * space; here snprintf() builds the same "... 2" first.
 *
 * ตาคุณ: เพิ่มชื่อทีมเข้าไปทั้งสามปลายทาง แล้วตอบว่าต้องเปิดดูที่ไหนบ้าง
 * Your turn: add your team name to all three places, then say where
 * you must look to see all three (hint: one is not on the board).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* Layout: one title line on top, the content starts at y=56. */
enum {
    LEFT_X = 24,
    TITLE_Y = 8,
    TITLE_FONT_PX = 28,
    SAY_Y = 56,
    SAY_FONT_PX = 24
};

/* The course palette, one value per role, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,  /* main text */
    COLOR_ACCENT = 0x4A9EFF /* a value that changes */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    READ_MS = 1000
};

enum {
    LINE_BYTES = 64 /* the Thai text (48 bytes), a space, any int */
};

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = LEFT_X;
    args.y = y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A new label shows at once only after one poll; without it, it comes
 * about two seconds later. The events are not read. */
static void poll_once(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* ui.poll() answers no events when it fails */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The second place: the Console drawer, cleared first so this run's
 * lines stand apart. lcd.print() ignores a console that is not ready
 * (modlcd.c), so these calls do too. */
static void print_to_drawer(void)
{
    const int32_t sum = 1 + 1;
    char line[LINE_BYTES];

    (void)bento_lcd_clear();
    (void)bento_lcd_print("สวัสดี บอร์ด PSoC Edge");
    (void)snprintf(line, sizeof(line), "%s %d", "หนึ่งบวกหนึ่งได้",
                   (int)sum);
    (void)bento_lcd_print(line);
}

int main(void)
{
    static const char done_text[] =
        "พิมพ์ 2 บรรทัดแล้ว - เปิดลิ้นชัก Console เพื่ออ่าน";
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_ui_id_t title = 0U;
    bento_ui_id_t say = 0U;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "บรรทัดแรกที่ขึ้นจอบอร์ด", TITLE_Y, COLOR_TEXT,
                           TITLE_FONT_PX, &title);
    }
    /* accent colour: this is the label whose text changes during the
     * run */
    if (status == BENTO_OK) {
        status = add_label(ui, "กำลังพิมพ์ลงลิ้นชัก Console...", SAY_Y,
                           COLOR_ACCENT, SAY_FONT_PX, &say);
    }
    if (status == BENTO_OK) {
        poll_once(ui);
        print_to_drawer();
        (void)printf("บรรทัดนี้อยู่บนคอม ไม่ได้อยู่บนจอบอร์ด\n");
        status = sleep_checked(READ_MS);
    }
    /* change the same label instead of laying a new one on top; a
     * setter's refusal is ignored, as in MicroPython (modui.c) */
    if (status == BENTO_OK) {
        (void)bento_ui_set_text(ui, say, done_text,
                                (uint32_t)strlen(done_text));
        poll_once(ui);
        (void)bento_lcd_print("<span class=ok>จบแล้ว</span>");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
