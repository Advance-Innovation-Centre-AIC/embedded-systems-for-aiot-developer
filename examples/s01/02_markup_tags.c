/*
 * 02_markup_tags.c -- ห้าระดับความสำคัญ (Five levels of importance)
 *
 * ทำให้บรรทัดที่ต้องรีบอ่าน เด่นออกมาจากบรรทัดอื่น ห้าคลาสของ span คือ
 * ห้าระดับความสำคัญ ไม่ใช่ห้าสีให้เลือกตามชอบ ป้ายบนหน้า Playground บอกว่า
 * กำลังพิมพ์ชุดไหน ตัวรายงานอยู่ในลิ้นชัก Console เปิดแล้วเทียบสามชุด
 * กับดัก: ชื่อคลาสที่พิมพ์ผิด เช่น class=okay ไม่มี error ให้จับ
 * บรรทัดนั้นแค่ออกมาเป็นสีปกติ
 *
 * Make the line that must be read first stand out. The five span
 * classes are five levels of importance, not five colours to pick
 * from. The label on the Playground says which set is printing; the
 * report is in the Console drawer, three sets to compare top to
 * bottom. Trap: a misspelt class such as class=okay raises no error,
 * the line just comes out in the plain colour.
 *
 * Twin of 02_markup_tags.py
 * (embedded-systems-for-aiot-developer/examples/s01/02_markup_tags.py).
 * Style: CLEAN_C.md.
 * Where C differs: the "+" string joins become one snprintf() into a
 * buffer sized for the longest line.
 *
 * ตาคุณ: ใน REPORT มีบรรทัดหนึ่งที่ติดระดับผิด หาแล้วแก้ จากนั้นเพิ่มข่าว
 * ของทีมคุณอีกหนึ่งบรรทัดพร้อมระดับของมัน
 * Your turn: one REPORT line carries the wrong level; find and fix
 * it, then add one line of your team's own news with its level.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* Layout: the title on the top row, the content starts at y=56, the
 * same vertical rhythm as every screen of this session. */
enum {
    LEFT_X = 24,
    TITLE_Y = 8,
    TITLE_FONT_PX = 24,
    SAY_Y = 56,
    SAY_FONT_PX = 20
};

/* The course palette, one value per role, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,  /* main text */
    COLOR_ACCENT = 0x4A9EFF /* a value that changes */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    SET_MS = 1500    /* time to read one set before the next */
};

enum {
    LINE_BYTES = 96 /* the longest span line is 79 bytes */
};

typedef struct {
    const char *level; /* the span class */
    const char *text;
} report_line_t;

/* One body of text, the level kept apart from the words because set 2
 * prints the same words without it. From what needs no attention to
 * what makes you get up from the chair. */
static const report_line_t REPORT[] = {
    {"muted", "รุ่นเฟิร์มแวร์ 1.7.0"}, {"info", "เริ่มรอบตรวจใหม่"},
    {"ok", "ต่อเน็ตสำเร็จ"},         {"warn", "แบตเตอรี่เหลือ <b>12%</b>"},
    {"error", "อ่านเซนเซอร์ไม่ได้"},
};

enum {
    REPORT_COUNT = sizeof(REPORT) / sizeof(REPORT[0])
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

/* One poll after a change shows it at once instead of about two
 * seconds later. ui.poll() answers no events when it fails, and the
 * events are not read. */
static void poll_once(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The setter's refusal is ignored, as in MicroPython (modui.c). */
static void say_and_poll(bento_ui_t *ui, bento_ui_id_t say,
                         const char *text)
{
    (void)bento_ui_set_text(ui, say, text, (uint32_t)strlen(text));
    poll_once(ui);
}

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Set 1. lcd.print() and lcd.console() ignore a console that is not
 * ready (modlcd.c), so these calls do too. The opening and closing
 * tags go in the same call: the drawer parses one line at a time, and
 * a <b> left open does not carry over to the next line. */
static void print_with_levels(void)
{
    char line[LINE_BYTES];
    uint32_t i;

    /* one <h2> per set: when every line is big, none stands out */
    (void)bento_lcd_console("<h2>ชุดที่ 1 ติดระดับครบ</h2>");
    for (i = 0U; i < (uint32_t)REPORT_COUNT; i++) {
        (void)snprintf(line, sizeof(line), "<span class=%s>%s</span>",
                       REPORT[i].level, REPORT[i].text);
        (void)bento_lcd_print(line);
    }
}

/* Set 2: the very same words, no level at all. */
static void print_without_levels(void)
{
    uint32_t i;

    (void)bento_lcd_console("<h2>ชุดที่ 2 ไม่ติดระดับ</h2>");
    for (i = 0U; i < (uint32_t)REPORT_COUNT; i++) {
        (void)bento_lcd_print(REPORT[i].text);
    }
}

/* Set 3: good news painted red. The colour is not wrong in itself;
 * what is wrong is the meaning the reader gets before the words. */
static void print_wrong_level(void)
{
    (void)bento_lcd_console("<h2>ชุดที่ 3 ติดระดับผิด</h2>");
    (void)bento_lcd_print("<span class=error>ต่อเน็ตสำเร็จ</span>");
    (void)bento_lcd_print(
        "<span class=muted>คนอ่านตกใจก่อนได้อ่านเนื้อความ</span>");
}

static bento_status_t build_screen(bento_ui_t *ui, bento_ui_id_t *say)
{
    bento_ui_id_t title = 0U;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ห้าระดับความสำคัญ", TITLE_Y, COLOR_TEXT,
                           TITLE_FONT_PX, &title);
    }
    /* accent, not green: green means "normal" in this course and
     * cannot also mean "working on it" */
    if (status == BENTO_OK) {
        status = add_label(ui, "ชุดที่ 1 - ติดระดับครบทุกบรรทัด", SAY_Y,
                           COLOR_ACCENT, SAY_FONT_PX, say);
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_ui_id_t say = 0U;
    bento_status_t status = build_screen(ui, &say);

    if (status == BENTO_OK) {
        poll_once(ui);
        (void)bento_lcd_clear();
        print_with_levels();
        status = sleep_checked(SET_MS);
    }
    if (status == BENTO_OK) {
        say_and_poll(ui, say, "ชุดที่ 2 - เนื้อความเดิม แต่ไม่ติดระดับ");
        print_without_levels();
        status = sleep_checked(SET_MS);
    }
    if (status == BENTO_OK) {
        say_and_poll(ui, say, "ชุดที่ 3 - ข่าวดีที่ติดระดับผิด");
        print_wrong_level();
        say_and_poll(ui, say, "พิมพ์ครบ 3 ชุดแล้ว เปิดลิ้นชัก Console เทียบดู");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
