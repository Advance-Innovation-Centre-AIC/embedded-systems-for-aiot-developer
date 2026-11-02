/*
 * s02_ai_observer.c -- สมุดบันทึกผลการทดลอง แล้วพาบอร์ดขึ้นเน็ต (ฉบับฝึกเติมโค้ด, AI Observer
 * practice)
 *
 * วิธีรัน: 1) ทดลองกับบอร์ดจริงจนได้ผลครบ 3 แถว แล้วจดลงตาราง TRIALS
 *          2) แตะการ์ด Playground บนจอบอร์ดค้างไว้ ก่อนส่งโค้ดขึ้นไป
 *          3) เติมช่องว่างตามคำใบ้ให้ครบ กด Program to Device แล้วอ่านผลบนจอ
 * ไฟล์นี้ทำสองอย่าง เอาผลที่จดมาด้วยมือขึ้นจอเป็นตาราง แล้วพาบอร์ดออกเน็ตและรายงานเลข IP
 * ชุดตัวอย่างของคาบนี้อยู่ที่ examples/s02/ สามไฟล์ และมันคือลำดับคำถามเรื่องลิงก์
 * ต่อครั้งแรกแล้วได้เลขที่อยู่มาไหม (examples/s02/01_wifi_first_connect.py)
 * "ต่อติดตอนนั้น" กับ "ยังต่ออยู่ตอนนี้" ต่างกันอย่างไร (examples/s02/02_link_uptime.py)
 * และลิงก์แบบไหนที่เรียกว่าใช้งานได้จริง (examples/s02/03_your_link_rule.py)
 * หน้าจอเขียนมาให้ครบแล้ว ช่องว่างหกจุด (TODO) อยู่ที่ตรรกะ ไม่ได้อยู่ที่การวาด
 * ดูที่จอ: ซ้ายคือตารางผลการทดลองที่เติมทีละแถวตอนรัน ขวาคือไฟสถานะลิงก์สองดวง ล่างคือ
 * มาตรวัดเวลาต่อเฉลี่ย เทียบกับเพดาน 85 วินาทีที่รอจริงได้
 * กับดัก: ตารางนี้เป็นค่าที่ทีมจดมาด้วยมือ ไม่ใช่ค่าที่บอร์ดวัดสด ๆ ตอนนี้ หน้าจอจึงเขียนกำกับไว้
 *
 * The practice skeleton: six blanks (each a TODO naming the C call
 * to use) do nothing until they are filled in, exactly as the .py
 * skeleton's pass lines. A notebook of the team's trials, then the
 * board goes online. The
 * left table fills one row at a time with the results written down by
 * hand, the right card has two link lights and the IP address, and a
 * meter shows the average join time against the 85-second ceiling a
 * person will wait. The same rows go to the Console drawer with a
 * summary, then the board joins Wi-Fi and reports its address.
 *
 * Twin of s02_ai_observer.py
 * (embedded-systems-for-aiot-developer/practise_codes/s02_ai_observer.py).
 * Style: CLEAN_C.md.
 * Differs from the original:
 *   - WIFI_SSID and WIFI_PASS sit at the top of join_wifi(), the one
 *     function that reads them (MISRA 8.9), not at the top of the file.
 *   - The list of (name, cond, hit, ms) tuples is a static const table
 *     of trial_t.
 *   - lcd.print() with several values joins them with spaces itself;
 *     here snprintf builds the same line first.
 *   - wifi.connect() is bento_wifi_connect_start/_poll with the
 *     binding's own window and BENTO_WIFI_F_SAVE_CREDS; any failure is
 *     its False, Stop cancels the join. The binding's own "[WiFi] ..."
 *     console lines are not printed by the C module.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

enum { /* times, ms */
    SETTLE_MS = 200,
    ROW_PAUSE_MS = 400,
    JOIN_POLL_MS = 100,
    WAIT_CEILING_MS =
        85000, /* the longest a person waits before calling the board hung */
    MS_PER_SECOND = 1000
};

enum { /* the trial table */
    TABLE_X = 16,
    TABLE_Y = 36,
    TABLE_W = 500,
    TABLE_H = 352,
    TABLE_COLUMNS = 4,
    BAND_COLUMN_W = 120,
    COND_COLUMN_W = 190,
    RESULT_COLUMN_W = 90,
    MS_COLUMN_W = 100,
    TITLE_Y = 8
};

enum { /* the link card */
    CARD_X = 524,
    CARD_Y = 36,
    CARD_W = 256,
    CARD_H = 352,
    CARD_RADIUS = 12,
    CARD_TEXT_X = 536,
    LINK_TITLE_Y = 44,
    LIGHT_SIZE = 48,
    UP_LIGHT_X = 540,
    UP_LIGHT_Y = 72,
    UP_TEXT_X = 584,
    UP_TEXT_Y = 76,
    DOWN_LIGHT_X = 596,
    DOWN_LIGHT_Y = 112,
    DOWN_TEXT_X = 652,
    DOWN_TEXT_Y = 120,
    IP_Y = 164,
    SOURCE_Y = 200,
    NOT_LIVE_Y = 224,
    METER_TITLE_X = 528,
    METER_TITLE_Y = 256,
    WAIT_Y = 292,
    BAR_X = 632,
    BAR_Y = 312,
    BAR_W = 148,
    BAR_H = 12,
    SCALE_Y = 328,
    SCALE_W = 148,
    SCALE_H = 44,
    SCALE_TICKS = 11,
    SCALE_MAJOR_EVERY = 5,
    TEXT_PX = 20
};

enum { /* the table's columns, left to right */
    COLUMN_BAND = 0,
    COLUMN_COND = 1,
    COLUMN_RESULT = 2,
    COLUMN_MS = 3
};

enum { /* colours, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_BAD = 0xE5484D,
    COLOR_BAR = 0x4A9EFF
};

enum { /* text buffers */
    LINE_BYTES =
        256, /* a console line before lcd cuts it at 127 bytes */
    ROW_BYTES = 128,  /* the cells of one table row, NUL between them */
    NUMBER_BYTES = 12 /* any int32_t with its sign and NUL */
};

/* Edit this first, and the hotspot's name and password in join_wifi(). */
static const char TEAM_NAME[] = "BentoBuilders";

/* One trial, written down by hand on the board. */
typedef struct {
    const char *name; /* what was tried */
    const char *cond; /* under which condition */
    bool hit;         /* did it join */
    int32_t ms;       /* how long it took */
} trial_t;

/* สามแถวที่ใส่ไว้ให้เป็นแค่ "ตัวอย่างรูปแบบ" จากรอบเก่า ทีมต้องเขียนทับให้ครบทั้งสามแถว
 * ด้วยผลที่ลงมือทดลองเองบนบอร์ดตัวนี้ ไม่ใช่ผลที่ลอกจากเอกสารหรือจากทีมอื่น
 * ทีมที่จดผลการต่อลิงก์: examples/s02/02_link_uptime.py บอกไว้ว่าทำไม "ต่อติดตอนนั้น"
 * กับ "ยังต่ออยู่ตอนนี้" ถึงต้องจดแยกกัน สองคำถามนี้คำตอบต่างกันได้ทุกวินาที */
static const trial_t TRIALS[] = {
    {"2.4GHz", "ยืนข้างเราเตอร์", true, 3180},
    {"2.4GHz", "เดินไปสุดห้อง", true, 9240},
    {"5GHz", "ยืนข้างเราเตอร์", false, 85000},
};

/* The widgets the program changes after building the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t table;
    bento_ui_id_t up_light;
    bento_ui_id_t down_light;
    bento_ui_id_t ip_label;
    bento_ui_id_t wait_label;
    bento_ui_id_t wait_bar;
} notebook_t;

static uint32_t trial_count(void)
{
    return (uint32_t)(sizeof(TRIALS) / sizeof(TRIALS[0]));
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

/* The widget setters ignore a refusal, as modui.c does. */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t x, int16_t y, uint32_t color,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.color = color;
    args.font_px = TEXT_PX;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_note(bento_ui_t *ui, const char *text,
                               int16_t x, int16_t y)
{
    bento_ui_id_t unused = 0U;

    return add_label(ui, text, x, y, COLOR_DIM, &unused);
}

static bento_status_t add_light(bento_ui_t *ui, int16_t x, int16_t y,
                                uint32_t color, int32_t lit,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = x;
    args.y = y;
    args.w = LIGHT_SIZE;
    args.h = LIGHT_SIZE;
    args.color = color;
    args.font_px = lit;
    return bento_ui_led_create(ui, &args, out_id);
}

/* One table row: the cells travel one after the other, each ended by
 * a NUL. A row that does not fit the buffer is not sent. */
static void add_row(bento_ui_t *ui, bento_ui_id_t table, uint8_t row,
                    const char *const cells[], uint32_t count)
{
    char packed[ROW_BYTES];
    size_t used = 0U;
    uint32_t i;
    bool fits = true;

    for (i = 0U; (i < count) && fits; i++) {
        const size_t cell_bytes = strlen(cells[i]) + 1U;

        fits = (used + cell_bytes) <= sizeof(packed);
        if (fits) {
            (void)memcpy(&packed[used], cells[i], cell_bytes);
            used += cell_bytes;
        }
    }
    if (fits && (used > 0U)) {
        /* a setter's refusal was ignored in MicroPython too */
        (void)bento_ui_add_row(ui, table, row, packed,
                               (uint32_t)(used - 1U));
    }
}

/* Four columns of uneven texts would never line up as labels; set
 * each width for its longest text, or a row wraps to twice its height. */
static bento_status_t add_table(notebook_t *book)
{
    static const char *const header[TABLE_COLUMNS] = {"ย่าน", "เงื่อนไข",
                                                      "ผล", "ms"};
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = TABLE_X;
    args.y = TABLE_Y;
    args.w = TABLE_W;
    args.h = TABLE_H;
    args.min_val = TABLE_COLUMNS;
    status = bento_ui_table_create(book->ui, &args, &book->table);
    if (status == BENTO_OK) {
        (void)bento_ui_set_col_width(book->ui, book->table, COLUMN_BAND,
                                     BAND_COLUMN_W);
        (void)bento_ui_set_col_width(book->ui, book->table, COLUMN_COND,
                                     COND_COLUMN_W);
        (void)bento_ui_set_col_width(book->ui, book->table,
                                     COLUMN_RESULT, RESULT_COLUMN_W);
        (void)bento_ui_set_col_width(book->ui, book->table, COLUMN_MS,
                                     MS_COLUMN_W);
        add_row(book->ui, book->table, 0U, header, TABLE_COLUMNS);
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

/* One light at a time is lit: a card with both lit cannot be read. */
static bento_status_t add_link_card(notebook_t *book)
{
    bento_status_t status = add_card(book->ui);

    if (status == BENTO_OK) {
        status = add_note(book->ui, "สถานะลิงก์ตอนนี้", CARD_TEXT_X,
                          LINK_TITLE_Y);
    }
    if (status == BENTO_OK) {
        status = add_light(book->ui, UP_LIGHT_X, UP_LIGHT_Y, COLOR_OK,
                           0, &book->up_light);
    }
    if (status == BENTO_OK) {
        status = add_note(book->ui, "ต่ออยู่", UP_TEXT_X, UP_TEXT_Y);
    }
    if (status == BENTO_OK) {
        status = add_light(book->ui, DOWN_LIGHT_X, DOWN_LIGHT_Y,
                           COLOR_BAD, 1, &book->down_light);
    }
    if (status == BENTO_OK) {
        status = add_note(book->ui, "ยังไม่ต่อ", DOWN_TEXT_X, DOWN_TEXT_Y);
    }
    if (status == BENTO_OK) {
        status = add_label(book->ui, "IP: ยังไม่มี", CARD_TEXT_X, IP_Y,
                           COLOR_DIM, &book->ip_label);
    }
    if (status ==
        BENTO_OK) { /* where a value comes from belongs on the same screen */
        status = add_note(book->ui, "ตารางซ้าย = ค่าที่จดมือ", CARD_TEXT_X,
                          SOURCE_Y);
    }
    if (status == BENTO_OK) {
        status = add_note(book->ui, "ไม่ใช่ค่าที่วัดสดตอนนี้", CARD_TEXT_X,
                          NOT_LIVE_Y);
    }
    return status;
}

/* The Scale is a ruler with no needle; the Bar laid over it moves. */
static bento_status_t add_wait_meter(notebook_t *book)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t scale = 0U;
    bento_status_t status = add_note(book->ui, "เวลาต่อเฉลี่ย เทียบเพดาน",
                                     METER_TITLE_X, METER_TITLE_Y);

    if (status == BENTO_OK) {
        status = add_label(book->ui, "- วินาที", CARD_TEXT_X, WAIT_Y,
                           COLOR_TEXT, &book->wait_label);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = BAR_X;
        args.y = BAR_Y;
        args.w = BAR_W;
        args.h = BAR_H;
        args.color = COLOR_BAR;
        args.max_val = WAIT_CEILING_MS;
        args.font_px = 0;
        status = bento_ui_bar_create(book->ui, &args, &book->wait_bar);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = CARD_TEXT_X;
        args.y = SCALE_Y;
        args.w = SCALE_W;
        args.h = SCALE_H;
        args.color = COLOR_TEXT;
        args.max_val = WAIT_CEILING_MS / MS_PER_SECOND;
        status = bento_ui_scale_create(book->ui, &args, &scale);
    }
    if (status == BENTO_OK) {
        /* six numbers on a short ruler overlap: three are enough */
        (void)bento_ui_set_ticks(book->ui, scale, SCALE_TICKS,
                                 SCALE_MAJOR_EVERY);
    }
    return status;
}

/* The page is laid out before any data arrives, then filled in. */
static bento_status_t build_screen(notebook_t *book)
{
    char title[LINE_BYTES];
    bento_status_t status =
        bento_ui_screen(book->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_table(book);
    }
    if (status == BENTO_OK) {
        bento_ui_id_t unused = 0U;

        (void)snprintf(title, sizeof(title), "ผลการทดลองของทีม %s",
                       TEAM_NAME);
        status = add_label(book->ui, title, TABLE_X, TITLE_Y,
                           COLOR_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_link_card(book);
    }
    if (status == BENTO_OK) {
        status = add_wait_meter(book);
    }
    if (status == BENTO_OK) {
        drain_events(book->ui);
    }
    return status;
}

/* Each trial goes to the drawer (what happened) and to the table
 * (what it adds up to). The result is a word too, not only a colour. */
static bento_status_t print_trials(const notebook_t *book)
{
    char ms_text[NUMBER_BYTES];
    const char *cells[TABLE_COLUMNS];
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < trial_count()) && (status == BENTO_OK); i++) {
        const trial_t *trial = &TRIALS[i];
        const char *mark = trial->hit ? "<span class=ok>ต่อติด</span>"
                                      : "<span class=error>ไม่ติด</span>";

        /* TODO ท่าที่ 4: เติม snprintf(line, sizeof(line), "%s | %s | %s | %" PRId32 " ms",
         *   trial->name, trial->cond, mark, trial->ms) แล้ว bento_lcd_print(line)
         *   (ประกาศ char line[LINE_BYTES] ไว้บนสุดของฟังก์ชันด้วย)
         * คอลัมน์ผลบอกได้แค่ติดหรือไม่ติด ซึ่งทิ้งรายละเอียดไปมาก
         *   examples/s02/03_your_link_rule.py ชี้ให้เห็นว่า "ไม่ผ่าน" มีได้หลายแบบ
         *   และกฎที่มองเห็นแค่แบบเดียว จะรายงานน้อยกว่าความจริงโดยไม่มีอะไรฟ้อง
         *   ทีมที่อยากได้คะแนนเต็มข้อวิเคราะห์ ควรจดให้เห็นด้วยว่าไม่ผ่านแบบไหน */
        (void)mark; /* used once ท่าที่ 4 is filled in */

        /* แถวเดียวกันลงตารางบนจอด้วย (เขียนมาให้แล้ว) */
        (void)snprintf(ms_text, sizeof(ms_text), "%" PRId32, trial->ms);
        cells[COLUMN_BAND] = trial->name;
        cells[COLUMN_COND] = trial->cond;
        cells[COLUMN_RESULT] = trial->hit ? "ต่อติด" : "ไม่ติด";
        cells[COLUMN_MS] = ms_text;
        add_row(book->ui, book->table, (uint8_t)(i + 1U), cells,
                TABLE_COLUMNS);
        drain_events(book->ui);
        status =
            wait_ms(ROW_PAUSE_MS); /* the rows climb one at a time */
    }
    return status;
}

/* The raw table says what happened; the summary says what it means.
 * Floor division: the answer is no finer than the instrument. */
static void summarise(const notebook_t *book)
{
    char line[LINE_BYTES];
    int32_t hits = 0;
    int32_t total_ms = 0;
    int32_t average_ms =
        0; /* the file runs before the blank below is filled in */
    uint32_t i;

    for (i = 0U; i < trial_count(); i++) {
        if (TRIALS[i].hit) {
            hits++;
        }
        total_ms += TRIALS[i].ms;
    }
    /* TODO ท่าที่ 5: เติม average_ms = total_ms / (int32_t)trial_count();
     * ก่อนเชื่อเลขเฉลี่ยนี้: ค่าเฉลี่ยจากสามครั้งยังไม่นับว่าเป็นการวัด ให้จดต่ำสุดกับสูงสุด
     *   คู่ไปด้วยเสมอ เพราะช่วงของค่าบอกเรื่องที่ค่าเฉลี่ยเดี่ยว ๆ กลบไว้หมด
     *   และเวลาที่จดต้องเป็นเวลาที่คนยืนรอจริง ไม่ใช่แค่ช่วงที่โปรแกรมนับได้ */
    (void)total_ms; /* used once ท่าที่ 5 is filled in */
    (void)snprintf(line, sizeof(line),
                   "สรุป: ต่อติด %" PRId32 " จาก %" PRIu32 " ครั้ง", hits,
                   trial_count());
    (void)bento_lcd_print(line);
    (void)snprintf(line, sizeof(line), "เวลาที่ใช้ต่อ เฉลี่ย %" PRId32 " ms",
                   average_ms);
    (void)bento_lcd_print(line);
    /* the meter moves once, after the sum: a number faster than once a second helps nobody.
     * ถ้ายังไม่เติม average_ms ข้างบน แถบนี้จะนิ่งอยู่ที่ศูนย์ นั่นคือคำตอบว่าเติมครบหรือยัง */
    (void)bento_ui_set_value(book->ui, book->wait_bar, average_ms);
    (void)snprintf(line, sizeof(line), "%" PRId32 " วินาที",
                   average_ms / MS_PER_SECOND);
    set_text(book->ui, book->wait_label, line);
    drain_events(book->ui);
}

/* Waits for the join the radio is making; Stop cancels it. */
static bento_status_t wait_for_join(void)
{
    bento_status_t status = BENTO_E_IN_PROGRESS;

    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(JOIN_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_connect_poll();
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* answers BENTO_OK only */
    }
    return status;
}

/* wifi.connect(ssid, password) never raises: *joined is its True or
 * False. Only Stop ends the program here. */
static bento_status_t join_wifi(bool *joined)
{
    /* แก้สองบรรทัดนี้ให้เป็น Hotspot มือถือของทีม (WiFi คณะต้อง login บอร์ดใช้ไม่ได้) */
    static const char ssid[] = "bento-teamXX";
    static const char password[] =
        "<รหัส Hotspot ของทีม>"; /* at least 8 characters */
    bento_wifi_connect_params_t params;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = ssid;
    params.ssid_len = (uint32_t)strlen(ssid);
    params.pass = password;
    params.pass_len = (uint32_t)strlen(password);
    params.timeout_ms = 0U; /* the binding's own window */
    params.flags = BENTO_WIFI_F_SAVE_CREDS;
    status = bento_wifi_connect_start(&params);
    if (status == BENTO_OK) {
        status = wait_for_join();
    }
    *joined = status == BENTO_OK;
    if (status != BENTO_E_CANCELLED) {
        status = BENTO_OK;
    }
    return status;
}

/* wifi.ip(): "0.0.0.0" when there is no address. */
static void read_ip(char *text, uint32_t cap)
{
    uint32_t address = 0U;
    uint32_t length = 0U;

    if ((bento_wifi_ip(BENTO_WIFI_IF_ANY, &address) != BENTO_OK) ||
        (bento_wifi_ipv4_format(address, text, cap, &length) !=
         BENTO_OK)) {
        (void)snprintf(text, (size_t)cap, "0.0.0.0");
    }
}

/* The warning goes out before the join: it may block about 85 s, and
 * a silent screen looks like a hung board. */
static bento_status_t report_link(const notebook_t *book)
{
    char ip_text[BENTO_WIFI_IPV4_TEXT_MAX];
    char line[LINE_BYTES];
    bool joined =
        false; /* set before the blank, so the file runs before it is filled in */
    bento_status_t status = BENTO_OK;

    (void)bento_lcd_print(
        "<span class=muted>กำลังต่อ WiFi รอได้ถึง 85 วินาที</span>");
    /* TODO ท่าที่ 6: เติม status = join_wifi(&joined);
     * join_wifi() (เขียนมาให้แล้ว ข้างบน) คืน joined เป็น true หรือ false ไม่มีข้อยกเว้นให้จับ
     * เราจึงต้องเช็กค่าที่คืนมาเอง ไม่มีใครเตือนเรา */
    (void)join_wifi; /* used once ท่าที่ 6 is filled in */
    if ((status == BENTO_OK) && joined) {
        read_ip(ip_text, sizeof(ip_text));
        /* TODO ท่าที่ 6 (ต่อ): เติม snprintf(line, sizeof(line),
         *   "<span class=ok>WiFi OK - IP %s</span>", ip_text) แล้ว bento_lcd_print(line) */

        /* ไฟสลับกันสองดวง ดวงที่ดับจะหรี่ลง ไม่ใช่หายไป คนดูจึงยังเห็นว่ามีดวงนั้นอยู่ */
        (void)bento_ui_set_value(book->ui, book->up_light, 1);
        (void)bento_ui_set_value(book->ui, book->down_light, 0);
        (void)snprintf(line, sizeof(line), "IP: %s", ip_text);
        set_text(book->ui, book->ip_label, line);
    } else if (status == BENTO_OK) {
        /* always an else: a program that reports only success is silent when it matters most */
        (void)bento_lcd_print(
            "<span class=muted>ต่อไม่สำเร็จ ตรวจ SSID และรหัสผ่าน</span>");
        (void)bento_ui_set_value(book->ui, book->up_light, 0);
        (void)bento_ui_set_value(book->ui, book->down_light, 1);
        set_text(book->ui, book->ip_label, "IP: ต่อไม่สำเร็จ");
    } else {
        /* Stop during the join */
    }
    if (status == BENTO_OK) {
        drain_events(book->ui);
    }
    return status;
}

int main(void)
{
    char line[LINE_BYTES];
    notebook_t book = {bento_runtime_get_ui(), 0U, 0U, 0U, 0U, 0U, 0U};
    bento_status_t status;

    /* ท่าที่ 1: เปิดสมุดบันทึกของทีม ท่าเปิดเหมือนคาบ 1 ทุกประการ เพราะมันคือการพิสูจน์ว่า
     * ช่องทางถึงจอยังใช้ได้
     * TODO ท่าที่ 1: เติม bento_lcd_clear() ที่นี่
     * TODO ท่าที่ 1 (ต่อ): เติม bento_lcd_console("<h2>บันทึกการต่อเน็ต - คาบ 2</h2>") ต่อจากนั้น */
    (void)snprintf(line, sizeof(line),
                   "<span class=muted>ทีม %s - บันทึกผลการทดลอง</span>",
                   TEAM_NAME);
    (void)bento_lcd_console(line);
    status = build_screen(&book);
    if (status == BENTO_OK) {
        status = print_trials(&book);
    }
    if (status == BENTO_OK) {
        summarise(&book);
        status = report_link(&book);
    }
    if (status == BENTO_OK) {
        (void)printf(
            "จบการบันทึกผล - ถ้าจอเงียบ เช็กว่าเข้าเมนู Playground หรือยัง\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
