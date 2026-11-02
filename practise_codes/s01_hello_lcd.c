/*
 * s01_hello_lcd.c -- ข้อความแรกของทีม ขึ้นจอบอร์ด (ฉบับฝึกเติมโค้ด, Hello LCD practice)
 *
 * วิธีรัน: 1) บนจอบอร์ด แตะการ์ด Playground บนหน้า Home ก่อน (สำคัญมาก)
 *          2) แก้ชื่อทีมและรายชื่อสมาชิก แล้วเติมช่องว่างตามคำใบ้ให้ครบ
 *          3) เปิดไฟล์นี้ใน BENTO IDE กด "Program to Device" แล้วอ่านผลบนจอบอร์ด
 * ตัวอย่างของคาบนี้อยู่ที่ examples/s01/ ทุกไฟล์รันเดี่ยว ๆ ได้ ถ้าส่งขึ้นบอร์ดแล้วจอเงียบ
 * สนิทตั้งแต่บรรทัดแรก ให้เปิด examples/s01/01_first_line.py ก่อนอย่างอื่น
 * ดูที่จอ: ท่าที่ 1-4 คือช่องว่างที่ต้องเติม ทุกบรรทัดไปเข้าลิ้นชัก Console ท่าที่ 5 เขียนมา
 * ให้ครบแล้ว มันวางป้ายประจำทีมลงหน้า Playground อ่านมันให้จบก่อนรัน แล้วเทียบว่าของที่
 * ui วาง กับของที่ lcd พิมพ์ ไปคนละที่กันยังไง
 *
 * The practice skeleton of the team's first message. Steps 1-4 are
 * blanks to fill in (each marked TODO, with the C call to use); until
 * then they send only the lines the skeleton already has. Step 5 is
 * complete: the team badge on the Playground page with the member
 * table, two status lights and the byte meter of the closing line.
 *
 * Twin of s01_hello_lcd.py
 * (embedded-systems-for-aiot-developer/practise_codes/s01_hello_lcd.py).
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

#include "bento_runtime.h"

#include "bento_lcd.h"
#include "bento_time.h"

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
    LINE_BYTES = 256, /* a console line before lcd cuts it at 127 bytes */
    ROW_BYTES = 128,  /* two table cells with the NUL between them */
    NUMBER_BYTES = 12 /* any int32_t with its sign and NUL */
};

/* The team: edit these three before running. */
static const char TEAM_NAME[] = "BentoBuilders";
static const char MOTTO[] = "เล่นของจริงก่อน แล้วค่อยแกะ";
static const char *const MEMBERS[] = {"สมชาย", "สมหญิง", "สมศรี"};
static const char TAIL_START[] = "ทีม ";
static const char TAIL_END[] = " พร้อมลุยคาบต่อไป";

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

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count, NULL);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text, int16_t x, int16_t y,
                                uint32_t color, int32_t size_px)
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

static bento_status_t add_light(bento_ui_t *ui, int16_t y, uint32_t color, int32_t lit,
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
static void add_row(bento_ui_t *ui, bento_ui_id_t table, uint8_t row, const char *left,
                    const char *right)
{
    char cells[ROW_BYTES];
    const size_t left_bytes = strlen(left);

    if ((left_bytes + strlen(right) + 2U) <= sizeof(cells)) {
        (void)memcpy(cells, left, left_bytes + 1U);
        (void)memcpy(&cells[left_bytes + 1U], right, strlen(right) + 1U);
        /* a setter's refusal was ignored in MicroPython too */
        (void)bento_ui_add_row(ui, table, row, cells,
                               (uint32_t)(left_bytes + 1U + strlen(right)));
    }
}

/* Steps 1-4: everything goes to the Console drawer. lcd.print()
 * ignores a console that is not ready (modlcd.c), and so does this. */
static bento_status_t write_console(void)
{
    char line[LINE_BYTES];
    uint32_t i;
    bento_status_t status = BENTO_OK;

    /* TODO ท่าที่ 1: ล้างจอให้สะอาด แล้วปักหัวเรื่อง
     * จอ Playground อาจมีของจากโปรแกรมก่อนหน้าค้างอยู่ เริ่มจากสถานะที่รู้แน่เสมอ
     * เติม: เรียก bento_lcd_clear() หน้าบรรทัดนี้
     * ยังไม่แน่ใจว่า clear ล้างอะไรไปบ้าง: examples/s01/05_clear_and_refresh.py วางท่าล้าง
     *   แล้ววาดใหม่ทั้งหน้า ไว้ข้างท่าที่แก้เฉพาะค่าเดิม ให้เห็นกับตาว่าสองท่านี้ให้ผลบนจอคนละแบบ */

    /* TODO ท่าที่ 1 (ต่อ): bento_lcd_console() เขียนลงพื้นที่ console บนจอ แท็ก h2 ทำให้เป็น
     * หัวเรื่องใหญ่
     * เติม: bento_lcd_console("<h2>AIoT in Action - คาบ 1</h2>") ที่นี่
     * ใส่แท็กแล้วหน้าตาไม่เปลี่ยน: examples/s01/02_markup_tags.py พิมพ์เนื้อความชุดเดียวกันสองรอบ
     *   ติดคลาสกับไม่ติดคลาส และบอกไว้ด้วยว่าชื่อคลาสที่พิมพ์ผิดจะเงียบสนิท ไม่มี error ให้จับ */

    /* span class=muted คือตัวหนังสือสีจาง เหมาะกับข้อความประกอบที่ไม่ใช่พระเอก */
    (void)snprintf(line, sizeof(line), "<span class=muted>ทดสอบจอครั้งแรกของทีม %s</span>",
                   TEAM_NAME);
    (void)bento_lcd_console(line);

    /* TODO ท่าที่ 2: ประโยคทักทายจากทีม
     * lcd.print ของ Python คั่นแต่ละค่าด้วยช่องว่างให้เอง ใน C ให้ snprintf ประกอบบรรทัดก่อน
     * เติม: snprintf(line, sizeof(line), "สวัสดีจากทีม %s", TEAM_NAME) แล้ว bento_lcd_print(line)
     * พิมพ์แล้วหาไม่เจอบนจอ: examples/s01/01_first_line.py แยกให้ดูว่า ui.Label ขึ้นหน้า Playground
     *   ทันที ส่วนบรรทัดของ lcd ไปรออยู่ในลิ้นชัก Console มุมขวาล่าง ที่ต้องกดเปิดเองก่อนถึงจะเห็น */

    (void)snprintf(line, sizeof(line), "คำขวัญของเรา: %s", MOTTO);
    (void)bento_lcd_print(line);

    /* printf ขึ้นที่คอนโซลของ BENTO IDE ฝั่งคอม ไม่ใช่จอบอร์ด มีไว้เทียบปลายทางของสองคำสั่ง */
    (void)printf("ส่งข้อความทักทายขึ้นจอบอร์ดแล้ว ดูที่จอ ไม่ใช่ที่หน้าคอม\n");

    /* ท่าที่ 3: แนะนำสมาชิกทีละคนด้วยลูป เพิ่มหรือลดสมาชิกได้โดยไม่ต้องแตะตัวลูปเลย */
    for (i = 0U; (i < member_count()) && (status == BENTO_OK); i++) {
        /* i นับจาก 0 ตามธรรมเนียม บวก 1 ตอนแสดงผลให้คนอ่านเข้าใจง่าย
         * TODO ท่าที่ 3: เติม snprintf(line, sizeof(line), "สมาชิกคนที่ %" PRIu32 " : %s",
         *   i + 1U, MEMBERS[i]) แล้ว bento_lcd_print(line)
         * ชื่อยาวแล้วบรรทัดขาดหายไปดื้อ ๆ: examples/s01/06_safe_print.py ย้ายกฎ 127 ไบต์เข้าไปอยู่ใน
         *   ฟังก์ชันเดียว แล้วเรียกฟังก์ชันนั้นแทน จะได้ไม่ต้องนั่งนับไบต์เองทุกบรรทัดที่พิมพ์ */

        /* TODO ท่าที่ 3 (ต่อ): หน่วง 800 ms ให้ชื่อขึ้นทีละคน คนดูอ่านทัน
         * เติม: status = wait_ms(MEMBER_PAUSE_MS) */
    }

    /* TODO ท่าที่ 4: ปิดท้ายด้วยสถานะสีเขียว span class=ok คือ "งานสำเร็จ"
     * ระวังความยาวรวมต่อการเรียกหนึ่งครั้ง: อย่าเกิน 127 ไบต์ (ไทยตัวละ 3 ไบต์ = ราว 42 ตัวอักษร)
     * เกินแล้วถูกตัดเงียบ ต้องแยกเป็นสองครั้ง เพราะรวมกันแล้วเกิน 127 ไบต์
     * เติม: bento_lcd_print("<span class=ok>ขึ้นจอสำเร็จ</span>")
     * อยากเห็นเพดาน 127 ไบต์ด้วยตาก่อนเชื่อ: examples/s01/03_byte_limit.py วัดไบต์ของทุกก้อนขึ้นจอสด ๆ
     *   เขียวคือส่งได้ครบ แดงคือเกินแล้ว และมันชี้ให้ดูด้วยว่า ขึ้นบรรทัดใหม่ท้ายบรรทัดหายไปพร้อมกับ
     *   ส่วนที่ถูกตัด */

    /* TODO ท่าที่ 4 (ต่อ): บรรทัดปิดท้ายคือ TAIL_START + TEAM_NAME + TAIL_END
     * เติม: snprintf(line, sizeof(line), "<span class=ok>%s%s%s</span>", TAIL_START, TEAM_NAME,
     *   TAIL_END) แล้ว bento_lcd_print(line) */

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
        (void)bento_ui_set_col_width(badge->ui, badge->table, 0, NUMBER_COLUMN_W);
        (void)bento_ui_set_col_width(badge->ui, badge->table, 1, NAME_COLUMN_W);
        add_row(badge->ui, badge->table, 0U, "ลำดับ", "ชื่อสมาชิก");
    }
    return status;
}

/* Two lights instead of coloured text: a light has a shape and a
 * brightness, so a black-and-white photo still tells them apart. */
static bento_status_t add_lights(badge_t *badge)
{
    bento_status_t status =
        add_label(badge->ui, "สถานะการเขียนตาราง", STATUS_X, STATUS_Y, COLOR_DIM, SMALL_PX);

    if (status == BENTO_OK) {
        status = add_light(badge->ui, RUN_LIGHT_Y, COLOR_RUN, 1, &badge->run_light);
    }
    if (status == BENTO_OK) {
        status = add_label(badge->ui, "กำลังเขียน", LIGHT_TEXT_X, RUN_LIGHT_Y + LIGHT_TEXT_DY,
                           COLOR_DIM, SMALL_PX);
    }
    if (status == BENTO_OK) {
        status = add_light(badge->ui, DONE_LIGHT_Y, COLOR_OK, 0, &badge->done_light);
    }
    if (status == BENTO_OK) {
        status = add_label(badge->ui, "เขียนครบแล้ว", LIGHT_TEXT_X, DONE_LIGHT_Y + LIGHT_TEXT_DY,
                           COLOR_DIM, SMALL_PX);
    }
    return status;
}

/* ท่าที่ 5: ป้ายประจำทีมบนหน้าจอ (เขียนมาให้ครบแล้ว ไม่ต้องเติม): ป้ายที่คนเดินผ่าน
 * โต๊ะต้องอ่านได้ทันทีจึงเป็นงานของ ui ไม่ใช่ของ lcd */
static bento_status_t build_badge(badge_t *badge)
{
    bento_status_t status =
        bento_ui_screen(badge->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX, BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS); /* let CM55 finish clearing before anything is placed */
    }
    if (status == BENTO_OK) {
        status = add_card(badge->ui);
    }
    if (status == BENTO_OK) {
        status = add_label(badge->ui, "ป้ายประจำทีม", TEXT_X, HEADING_Y, COLOR_DIM, SMALL_PX);
    }
    if (status == BENTO_OK) {
        status = add_label(badge->ui, TEAM_NAME, TEXT_X, NAME_Y, COLOR_TEXT, NAME_PX);
    }
    if (status == BENTO_OK) {
        status = add_label(badge->ui, MOTTO, TEXT_X, MOTTO_Y, COLOR_DIM, SMALL_PX);
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
        add_row(badge->ui, badge->table, (uint8_t)(i + 1U), number, MEMBERS[i]);
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
    const int32_t used = (int32_t)(strlen(TAIL_START) + strlen(TEAM_NAME) + strlen(TAIL_END));
    char text[NUMBER_BYTES + sizeof(" ไบต์")];
    bento_ui_widget_args_t args;
    bento_ui_id_t bar = 0U;
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(ui, "ความยาวบรรทัดปิดท้าย จากเพดาน 127", STATUS_X,
                                      METER_TITLE_Y, COLOR_DIM, SMALL_PX);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = METER_X;
        args.y = BAR_Y;
        args.w = METER_W;
        args.h = BAR_H;
        args.color = COLOR_OK;
        args.max_val = BYTE_LIMIT;
        args.font_px = 0; /* created at 0: a Bar created with a value blanks the page */
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
        status = add_label(ui, text, METER_X, BYTES_Y, COLOR_TEXT, SMALL_PX);
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
        (void)printf("โปรแกรมจบแล้ว ถ้าจอไม่ขึ้นข้อความ เช็กว่าเข้าเมนู Playground หรือยัง\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
