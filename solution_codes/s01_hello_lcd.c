/*
 * s01_hello_lcd.c -- ข้อความแรกของทีม ขึ้นจอบอร์ด (Hello LCD)
 *
 * วิธีรัน: 1) บนจอบอร์ด แตะการ์ด Playground บนหน้า Home ก่อน (สำคัญมาก)
 *          2) แก้ชื่อทีม คำขวัญ และรายชื่อสมาชิกให้เป็นของทีมเราก่อน
 *          3) เปิดไฟล์นี้ใน BENTO IDE กด "Program to Device" แล้วอ่านผลบนจอบอร์ด
 * ท่าที่ 1-4 ส่งบรรทัดเข้าลิ้นชัก Console ที่ต้องกดเปิดเองก่อนจะเห็น ท่าที่ 5
 * วางป้ายประจำทีมลงหน้า Playground ตรง ๆ ซึ่งค้างอยู่หลังโปรแกรมจบ ปลายการ์ด
 * ล่างคือมาตรวัดไบต์ บอกว่าบรรทัดปิดท้ายกินไปเท่าไรจากเพดาน 127
 *
 * The team's first message on the board. Steps 1-4 send lines to the
 * Console drawer, which must be opened by hand to be seen. Step 5 puts
 * the team badge straight on the Playground page, where it stays after
 * the program ends: a card with the team name, the motto, a table of
 * members filled one row at a time, two status lights and a byte meter
 * that shows how much of the 127-byte limit the closing line uses.
 *
 * Twin of s01_hello_lcd.py
 * (embedded-systems-for-aiot-developer/solution_codes/s01_hello_lcd.py).
 * Style: CLEAN_C.md.
 * Differs from the original:
 *   - lcd.print() with several values joins them with spaces itself;
 *     here snprintf builds the same line first.
 *   - Stop between the steps ends the program at once (MicroPython
 *     raises KeyboardInterrupt there).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
/* ชื่อทีม (ไทยหรืออังกฤษก็ได้) */
static const char TEAM_NAME[] = "BentoBuilders";
/* รายชื่อสมาชิก 2-3 คน */
static const char *const MEMBERS[] = {"สมชาย", "สมหญิง", "สมศรี"};
/* คำขวัญทีมสั้น ๆ */
static const char MOTTO[] = "เล่นของจริงก่อน แล้วค่อยแกะ";
/* ---- end of settings ---- */

enum { /* times, ms */
    MEMBER_PAUSE_MS = 800,
    SETTLE_MS = 200,
    ROW_PAUSE_MS = 500
};

enum { /* the badge card */
    CARD_X = 16,
    CARD_Y = 8,
    CARD_W = 760,
    CARD_H = 372,
    CARD_RADIUS = 12,
    TEXT_X = 32,
    HEADING_Y = 16,
    NAME_Y = 40,
    MOTTO_Y = 84,
    SMALL_PX = 20,
    NAME_PX = 28
};

enum { /* the member table */
    TABLE_Y = 116,
    TABLE_W = 340,
    TABLE_H = 252,
    TABLE_COLUMNS = 2,
    NUMBER_COLUMN_W = 100,
    NAME_COLUMN_W = 230
};

enum { /* the two status lights and the byte meter */
    STATUS_X = 400,
    STATUS_Y = 116,
    LIGHT_X = 408,
    LIGHT_SIZE = 48,
    RUN_LIGHT_Y = 144,
    DONE_LIGHT_Y = 188,
    LIGHT_TEXT_X = 452,
    LIGHT_TEXT_DY = 4,
    METER_TITLE_Y = 240,
    METER_X = 408,
    METER_W = 332,
    BAR_Y = 272,
    BAR_H = 16,
    SCALE_Y = 288,
    SCALE_H = 44,
    BYTES_Y = 336,
    BYTE_LIMIT = 127
};

enum { /* colours, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_RUN = 0x4A9EFF
};

enum { /* text buffers */
    LINE_BYTES =
        256, /* a console line before lcd cuts it at 127 bytes */
    ROW_BYTES = 128, /* two table cells with the NUL between them */
    NUMBER_BYTES = 12 /* any int32_t with its sign and NUL */,
    TAIL_BYTES =
        192, /* the closing line's text: a long team name and 60 bytes of Thai */
    CLOSING_BYTES =
        TAIL_BYTES + 32 /* that text inside its span tags (22 bytes) */
};

/* The lights the table loop switches at the end. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t table;
    bento_ui_id_t run_light;
    bento_ui_id_t done_light;
} badge_t;

static uint32_t member_count(void)
{
    return (uint32_t)(sizeof(MEMBERS) / sizeof(MEMBERS[0]));
}

/* The closing line's text (TAIL in the original). Answers its whole
 * length in bytes, even when the buffer cuts the text. */
static int32_t format_tail(char *text, size_t cap)
{
    const int written =
        snprintf(text, cap, "ทีม %s พร้อมลุยคาบต่อไป", TEAM_NAME);

    return (written > 0) ? (int32_t)written : 0;
}

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* ui.poll() here only keeps the event queue empty. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t x, int16_t y, uint32_t color,
                                int32_t size_px)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.color = color;
    args.font_px = size_px;
    return bento_ui_label_create(ui, &args, &unused);
}

static bento_status_t add_light(bento_ui_t *ui, int16_t y,
                                uint32_t color, int32_t lit,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = LIGHT_X;
    args.y = y;
    args.w = LIGHT_SIZE;
    args.h = LIGHT_SIZE;
    args.color = color;
    args.font_px = lit;
    return bento_ui_led_create(ui, &args, out_id);
}

/* One table row: the two cells travel as "left\0right". */
static void add_row(bento_ui_t *ui, bento_ui_id_t table, uint8_t row,
                    const char *left, const char *right)
{
    char cells[ROW_BYTES];
    const size_t left_bytes = strlen(left);

    if ((left_bytes + strlen(right) + 2U) <= sizeof(cells)) {
        (void)memcpy(cells, left, left_bytes + 1U);
        (void)memcpy(&cells[left_bytes + 1U], right,
                     strlen(right) + 1U);
        /* a setter's refusal was ignored in MicroPython too */
        (void)bento_ui_add_row(
            ui, table, row, cells,
            (uint32_t)(left_bytes + 1U + strlen(right)));
    }
}

/* Steps 1-2: the heading and the greeting. lcd.print() ignores a
 * console that is not ready (modlcd.c), and so does every console
 * call here. */
static void greet_console(void)
{
    char line[LINE_BYTES];

    /* start from a known state: the Playground may still hold the last program's lines */
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>AIoT in Action - คาบ 1</h2>");
    (void)snprintf(line, sizeof(line),
                   "<span class=muted>ทดสอบจอครั้งแรกของทีม %s</span>",
                   TEAM_NAME);
    (void)bento_lcd_console(line);
    (void)snprintf(line, sizeof(line), "สวัสดีจากทีม %s", TEAM_NAME);
    (void)bento_lcd_print(line);
    (void)snprintf(line, sizeof(line), "คำขวัญของเรา: %s", MOTTO);
    (void)bento_lcd_print(line);
    (void)printf("ส่งข้อความทักทายขึ้นจอบอร์ดแล้ว ดูที่จอ ไม่ใช่ที่หน้าคอม\n");
}

/* Step 3: one member at a time, so people can read them. */
static bento_status_t list_members(void)
{
    char line[LINE_BYTES];
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < member_count()) && (status == BENTO_OK); i++) {
        (void)snprintf(line, sizeof(line), "สมาชิกคนที่ %" PRIu32 " : %s",
                       i + 1U, MEMBERS[i]);
        (void)bento_lcd_print(line);
        status = wait_ms(MEMBER_PAUSE_MS);
    }
    return status;
}

/* Step 4: two calls on purpose: together they pass 127 bytes and
 * would be cut silently. */
static void close_console(void)
{
    char tail[TAIL_BYTES];
    char line[CLOSING_BYTES];

    (void)bento_lcd_print("<span class=ok>ขึ้นจอสำเร็จ</span>");
    (void)format_tail(
        tail, sizeof(tail)); /* the length is the byte meter's */
    (void)snprintf(line, sizeof(line), "<span class=ok>%s</span>",
                   tail);
    (void)bento_lcd_print(line);
}

/* Steps 1-4: everything goes to the Console drawer. */
static bento_status_t write_console(void)
{
    bento_status_t status;

    greet_console();
    status = list_members();
    if (status == BENTO_OK) {
        close_console();
    }
    return status;
}

static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    bento_ui_widget_args_init(&args);
    args.x = CARD_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* the border colour */
    args.max_val = CARD_RADIUS;
    args.font_px = 1; /* the border width */
    return bento_ui_panel_create(ui, &args, &unused);
}

/* The table is laid out by the screen: a row is about 62 px, so a
 * header and three members need 250 px or the last row is cut. */
static bento_status_t add_table(badge_t *badge)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = TEXT_X;
    args.y = TABLE_Y;
    args.w = TABLE_W;
    args.h = TABLE_H;
    args.min_val = TABLE_COLUMNS;
    status = bento_ui_table_create(badge->ui, &args, &badge->table);
    if (status == BENTO_OK) {
        (void)bento_ui_set_col_width(badge->ui, badge->table, 0,
                                     NUMBER_COLUMN_W);
        (void)bento_ui_set_col_width(badge->ui, badge->table, 1,
                                     NAME_COLUMN_W);
        add_row(badge->ui, badge->table, 0U, "ลำดับ", "ชื่อสมาชิก");
    }
    return status;
}

/* Two lights instead of coloured text: a light has a shape and a
 * brightness, so a black-and-white photo still tells them apart. */
static bento_status_t add_lights(badge_t *badge)
{
    bento_status_t status =
        add_label(badge->ui, "สถานะการเขียนตาราง", STATUS_X, STATUS_Y,
                  COLOR_DIM, SMALL_PX);

    if (status == BENTO_OK) {
        status = add_light(badge->ui, RUN_LIGHT_Y, COLOR_RUN, 1,
                           &badge->run_light);
    }
    if (status == BENTO_OK) {
        status =
            add_label(badge->ui, "กำลังเขียน", LIGHT_TEXT_X,
                      RUN_LIGHT_Y + LIGHT_TEXT_DY, COLOR_DIM, SMALL_PX);
    }
    if (status == BENTO_OK) {
        status = add_light(badge->ui, DONE_LIGHT_Y, COLOR_OK, 0,
                           &badge->done_light);
    }
    if (status == BENTO_OK) {
        status = add_label(badge->ui, "เขียนครบแล้ว", LIGHT_TEXT_X,
                           DONE_LIGHT_Y + LIGHT_TEXT_DY, COLOR_DIM,
                           SMALL_PX);
    }
    return status;
}

static bento_status_t build_badge(badge_t *badge)
{
    bento_status_t status =
        bento_ui_screen(badge->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(
            SETTLE_MS); /* let CM55 finish clearing before anything is placed */
    }
    if (status == BENTO_OK) {
        status = add_card(badge->ui);
    }
    if (status == BENTO_OK) {
        status = add_label(badge->ui, "ป้ายประจำทีม", TEXT_X, HEADING_Y,
                           COLOR_DIM, SMALL_PX);
    }
    if (status == BENTO_OK) {
        status = add_label(badge->ui, TEAM_NAME, TEXT_X, NAME_Y,
                           COLOR_TEXT, NAME_PX);
    }
    if (status == BENTO_OK) {
        status = add_label(badge->ui, MOTTO, TEXT_X, MOTTO_Y, COLOR_DIM,
                           SMALL_PX);
    }
    if (status == BENTO_OK) {
        status = add_table(badge);
    }
    if (status == BENTO_OK) {
        status = add_lights(badge);
    }
    return status;
}

/* The same members as step 3, now in the table on the screen. */
static bento_status_t fill_table(const badge_t *badge)
{
    char number[NUMBER_BYTES];
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < member_count()) && (status == BENTO_OK); i++) {
        (void)snprintf(number, sizeof(number), "%" PRIu32, i + 1U);
        add_row(badge->ui, badge->table, (uint8_t)(i + 1U), number,
                MEMBERS[i]);
        drain_events(badge->ui);
        status = wait_ms(ROW_PAUSE_MS);
    }
    if (status == BENTO_OK) {
        /* value 0 dims a light rather than hiding it: a dim light still proves the screen draws */
        (void)bento_ui_set_value(badge->ui, badge->run_light, 0);
        (void)bento_ui_set_value(badge->ui, badge->done_light, 1);
    }
    return status;
}

/* A bare number does not say how close the limit is; a meter does.
 * A Thai letter is 3 bytes, so a short-looking line fills it fast. */
static bento_status_t add_byte_meter(bento_ui_t *ui)
{
    char tail[TAIL_BYTES];
    const int32_t used = format_tail(tail, sizeof(tail));
    char text[NUMBER_BYTES + sizeof(" ไบต์")];
    bento_ui_widget_args_t args;
    bento_ui_id_t bar = 0U;
    bento_ui_id_t unused = 0U;
    bento_status_t status =
        add_label(ui, "ความยาวบรรทัดปิดท้าย จากเพดาน 127", STATUS_X,
                  METER_TITLE_Y, COLOR_DIM, SMALL_PX);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = METER_X;
        args.y = BAR_Y;
        args.w = METER_W;
        args.h = BAR_H;
        args.color = COLOR_OK;
        args.max_val = BYTE_LIMIT;
        args.font_px =
            0; /* created at 0: a Bar created with a value blanks the page */
        status = bento_ui_bar_create(ui, &args, &bar);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_value(ui, bar, used);
        bento_ui_widget_args_init(&args);
        args.x = METER_X;
        args.y = SCALE_Y;
        args.w = METER_W;
        args.h = SCALE_H;
        args.color = COLOR_TEXT;
        args.max_val = BYTE_LIMIT;
        status = bento_ui_scale_create(ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%" PRId32 " ไบต์", used);
        status =
            add_label(ui, text, METER_X, BYTES_Y, COLOR_TEXT, SMALL_PX);
    }
    return status;
}

int main(void)
{
    badge_t badge = {bento_runtime_get_ui(), 0U, 0U, 0U};
    bento_status_t status = write_console();

    if (status == BENTO_OK) {
        status = build_badge(&badge);
    }
    if (status == BENTO_OK) {
        status = fill_table(&badge);
    }
    if (status == BENTO_OK) {
        status = add_byte_meter(badge.ui);
    }
    if (status == BENTO_OK) {
        drain_events(badge.ui);
        (void)printf("โปรแกรมจบแล้ว ถ้าจอไม่ขึ้นข้อความ เช็กว่าเข้าเมนู "
                     "Playground หรือยัง\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
