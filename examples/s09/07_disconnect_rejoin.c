/*
 * 07_disconnect_rejoin.c -- สั่งตัดเน็ตเอง แล้วต่อกลับ
 * (Disconnect on purpose, then rejoin)
 *
 * ต่อ WiFi อ่านค่าไว้ แล้วสั่ง wifi.disconnect() อ่านค่าซ้ำ รอ 6 วินาที
 * แล้วต่อกลับ สี่บรรทัดบนเปลี่ยนพร้อมกันตอนตัดและตอนต่อใหม่ บรรทัด
 * "if wifi.ip():" ยังตอบ True ตอนหลุด เพราะ "0.0.0.0" ไม่ใช่สตริงว่าง
 * ถามสถานะด้วย is_connected() เท่านั้น ก่อนรันแก้ WIFI_SSID WIFI_PASS
 * ในกรอบ "แก้ก่อนรัน" ใต้ #include
 *
 * Joins the Wi-Fi and reads the link, calls wifi.disconnect(),
 * reads it again, waits 6 s and joins again. The top four lines
 * change together when the link drops and when it comes back. The
 * "if wifi.ip():" line still says True while the link is down,
 * because "0.0.0.0" is not an empty text: ask is_connected(). Set
 * WIFI_SSID and WIFI_PASS in the "แก้ก่อนรัน" settings block below
 * the includes first.
 *
 * Twin of 07_disconnect_rejoin.py
 * (embedded-systems-for-aiot-developer/examples/s09/07_disconnect_rejoin.py),
 * merged from the library twin aiot-s09-07-disconnect-rejoin. Style:
 * CLEAN_C.md.
 *
 * Differs from the original:
 *   - wifi.status() is a dict; here it is bento_wifi_status(), one
 *     reading into a struct, written the way Python prints it.
 *   - wifi.connect() prints console lines of its own. This program
 *     prints the two C can know, "[WiFi] Connecting..." and
 *     "[WiFi] Connected! IP=...". No C function reports the others:
 *     each failed attempt with the radio's code and "[WiFi]
 *     Credentials saved to QSPI (N entries)" (modwifi.c).
 *   - wifi.disconnect() never raises; a refusal from the C call is
 *     ignored the same way (Stop still ends the program).
 *   - The class Stop of the original is the status chain here: a failed
 *     first join shows its message and main() returns 0.
 *   - Stop ends the program quietly where MicroPython raises
 *     KeyboardInterrupt.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
static const char WIFI_SSID[] = "AIoT-Class";
static const char WIFI_PASS[] = "<รหัสผ่านของห้องเรียน>";
/* ---- end of settings ---- */

/* The course palette, 0xRRGGBB. */
enum {
    COL_TEXT = 0xE8EAED,
    COL_DIM = 0x9AA3AF,
    COL_CARD = 0x171B22,
    COL_OK = 0x30A46C,
    COL_WARN = 0xF5A623,
    COL_BAD = 0xE5484D
};

/* Times in milliseconds, and sizes. */
enum {
    SETTLE_MS = 200,
    BEFORE_PAUSE_MS = 1200,
    UP_PAUSE_MS = 1500,
    DOWN_PAUSE_MS = 600,
    DOWN_SECONDS = 6,
    SECOND_MS = 1000,
    WAIT_STEP_MS = 50, /* between two polls of connect */
    TEXT_BYTES = 128   /* a SET_TEXT carries at most 126 */
};

/* One reading of wifi.status(): the keys this program uses. */
typedef struct {
    bool connected;
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    const char *mode;
} reading_t;

/* The widgets the program changes. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t conn;
    bento_ui_id_t ip;
    bento_ui_id_t mode;
    bento_ui_id_t trap;
    bento_ui_id_t phase;
    bento_ui_id_t note;
    bento_ui_id_t note_2;
} screen_t;

/* wifi.status(): one reading. */
static bento_status_t read_status(reading_t *reading)
{
    bento_wifi_status_t link;
    uint32_t length = 0U;
    bento_status_t status;

    bento_wifi_status_init(&link);
    status = bento_wifi_status(&link);
    if (status == BENTO_OK) {
        reading->connected = link.connected;
        /* a BENTO_WIFI_IPV4_TEXT_MAX buffer holds any address */
        (void)bento_wifi_ipv4_format(link.ip_v4, reading->ip,
                                     (uint32_t)sizeof(reading->ip),
                                     &length);
        if (link.mode == BENTO_WIFI_MODE_STA) {
            reading->mode = "sta";
        } else if (link.mode == BENTO_WIFI_MODE_IDLE) {
            reading->mode = "idle";
        } else {
            reading->mode = "off";
        }
    }
    return status;
}

/* wifi.is_connected(): it never raises, so a refusal is False. */
static bool is_connected(void)
{
    bool connected = false;

    if (bento_wifi_is_connected(&connected) != BENTO_OK) {
        connected = false;
    }
    return connected;
}

static const char *bool_text(bool value)
{
    return value ? "True" : "False";
}

static uint32_t pick(bool good, uint32_t if_good, uint32_t if_bad)
{
    return good ? if_good : if_bad;
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

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void lcd_line(const char *text)
{
    (void)bento_lcd_print(text);
}

static void print_lessons(void)
{
    (void)printf("บทเรียนของไฟล์นี้:\n");
    (void)printf("  1. disconnect() คืน None ไม่ใช่ True\n");
    (void)printf("  2. ip() ตอนหลุดคือ \"0.0.0.0\" ซึ่ง Python"
                 " ถือว่าเป็นจริง\n");
    (void)printf("  3. คำถาม 'ต่ออยู่ไหม' มีคำตอบเดียวที่เชื่อได้คือ"
                 " is_connected()\n");
}

/* Layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 20,
    TITLE_Y = 12,
    CARD_X = 20,
    CARD_W = 652,
    CARD_1_Y = 52,
    CARD_1_H = 152,
    CARD_2_Y = 216,
    CARD_2_H = 112,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    ROW_X = 40,
    CONN_Y = 68,
    IP_Y = 100,
    MODE_Y = 136,
    TRAP_Y = 168,
    PHASE_Y = 232,
    NOTE_Y = 262,
    NOTE_2_Y = 290,
    FOOT_Y = 338,
    TITLE_PX = 24,
    TEXT_PX = 20
};

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(const screen_t *screen, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(screen->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void set_color(const screen_t *screen, bento_ui_id_t widget,
                      uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, widget, color);
}

/* ui.poll() here only empties the queue; it answers no events
 * when it fails. */
static void drain_events(const screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
}

static bento_status_t add_label(const screen_t *screen,
                                const char *text, int16_t y,
                                uint32_t color, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = ROW_X;
    args.y = y;
    args.color = color;
    args.font_px = TEXT_PX;
    return bento_ui_label_create(screen->ui, &args, out_id);
}

static bento_status_t add_card(const screen_t *screen, int16_t y,
                               int16_t h)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = CARD_X;
    args.y = y;
    args.w = CARD_W;
    args.h = h;
    args.color = COL_CARD;
    args.min_val = COL_DIM; /* the border colour */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(screen->ui, &args, &card);
}

/* A label at the left edge (x = 20) instead of the rows' x = 40. */
static bento_status_t add_edge_label(const screen_t *screen,
                                     const char *text, int16_t y,
                                     int32_t font_px, uint32_t color)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = TITLE_X;
    args.y = y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(screen->ui, &args, &label);
}

/* The four lines that are read from one moment. */
static bento_status_t add_link_card(screen_t *screen)
{
    bento_status_t status = add_card(screen, CARD_1_Y, CARD_1_H);

    if (status == BENTO_OK) {
        status = add_label(screen, "is_connected() = -", CONN_Y,
                           COL_TEXT, &screen->conn);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen, "ip() = -", IP_Y, COL_TEXT, &screen->ip);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, "status()['mode'] = -", MODE_Y,
                           COL_TEXT, &screen->mode);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, "if wifi.ip(): -> -", TRAP_Y,
                           COL_WARN, &screen->trap);
    }
    return status;
}

static bento_status_t add_phase_card(screen_t *screen)
{
    bento_status_t status = add_card(screen, CARD_2_Y, CARD_2_H);

    if (status == BENTO_OK) {
        status = add_label(screen, "กำลังเริ่ม", PHASE_Y, COL_DIM,
                           &screen->phase);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, "-", NOTE_Y, COL_DIM, &screen->note);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen, "-", NOTE_2_Y, COL_DIM, &screen->note_2);
    }
    return status;
}

static bento_status_t build_screen(screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_edge_label(screen, "สั่งตัดเน็ตเอง แล้วต่อกลับ", TITLE_Y,
                                TITLE_PX, COL_TEXT);
    }
    if (status == BENTO_OK) {
        status = add_link_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_phase_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_edge_label(screen, "สี่บรรทัดบนอ่านจากจังหวะเดียวกันเสมอ",
                                FOOT_Y, TEXT_PX, COL_DIM);
    }
    if (status == BENTO_OK) {
        drain_events(screen);
    }
    return status;
}

/* The four lines from one reading. bool(ip) is True for any text
 * that is not empty, "0.0.0.0" too: the trap this file shows. */
static void show_reading(const screen_t *screen,
                         const reading_t *reading)
{
    const bool ip_is_true = reading->ip[0] != '\0';
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "is_connected() = %s",
                   bool_text(is_connected()));
    set_text(screen, screen->conn, text);
    (void)snprintf(text, sizeof(text), "ip() = %s", reading->ip);
    set_text(screen, screen->ip, text);
    (void)snprintf(text, sizeof(text), "status()['mode'] = %s",
                   reading->mode);
    set_text(screen, screen->mode, text);
    (void)snprintf(
        text, sizeof(text), "if wifi.ip(): -> %s   (ต่ออยู่จริง %s)",
        bool_text(ip_is_true), bool_text(reading->connected));
    set_text(screen, screen->trap, text);
    set_color(
        screen, screen->trap,
        pick(ip_is_true && !reading->connected, COL_BAD, COL_DIM));
    set_color(screen, screen->conn,
              pick(reading->connected, COL_OK, COL_BAD));
    set_color(screen, screen->ip,
              pick(reading->connected, COL_OK, COL_BAD));
}

/* refresh(): one reading onto the screen and both consoles. */
static bento_status_t refresh(const screen_t *screen, const char *phase,
                              reading_t *reading)
{
    char line[TEXT_BYTES];
    bento_status_t status = read_status(reading);

    if (status == BENTO_OK) {
        show_reading(screen, reading);
        set_text(screen, screen->phase, phase);
        drain_events(screen);
        (void)snprintf(line, sizeof(line),
                       "<b>%s</b> connected=%s ip=%s mode=%s", phase,
                       bool_text(reading->connected), reading->ip,
                       reading->mode);
        lcd_line(line);
        (void)printf("%s | connected = %s | ip = %s | mode = %s\n",
                     phase, bool_text(reading->connected), reading->ip,
                     reading->mode);
    }
    return status;
}

/* wifi.connect(): *joined is its True or False. A refused or
 * timed-out join is False; any other failure ends the program,
 * as the binding's OSError does. */
static bento_status_t join_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
    reading_t reading;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = WIFI_PASS;
    params.pass_len = (uint32_t)strlen(WIFI_PASS);
    params.flags = BENTO_WIFI_F_SAVE_CREDS; /* the binding saves too */
    (void)printf("[WiFi] Connecting...\r\n");
    status = bento_wifi_connect_start(&params);
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(WAIT_STEP_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_connect_poll();
        } else { /* Stop: leave the radio alone */
            (void)bento_wifi_connect_cancel(); /* nothing to report */
        }
    }
    *joined = (status == BENTO_OK);
    if ((status == BENTO_E_FAILED) || (status == BENTO_E_TIMEOUT)) {
        status = BENTO_OK;
    }
    if (*joined) {
        status = read_status(&reading);
    }
    if (*joined && (status == BENTO_OK)) {
        (void)printf("[WiFi] Connected! IP=%s\r\n", reading.ip);
    }
    return status;
}

/* Step 1 and the first join. */
static bento_status_t first_join(const screen_t *screen, bool *joined)
{
    reading_t reading;
    char text[TEXT_BYTES];
    bento_status_t status = refresh(screen, "1) ก่อนต่อ", &reading);

    if (status == BENTO_OK) {
        set_text(screen, screen->note, "ยังไม่ได้เรียก connect() เลย");
        status = wait_ms(BEFORE_PAUSE_MS);
    }
    if (status == BENTO_OK) {
        set_color(screen, screen->phase, COL_WARN);
        (void)snprintf(text, sizeof(text),
                       "2) กำลังต่อ %s "
                       "จอจะนิ่งครู่หนึ่ง",
                       WIFI_SSID);
        set_text(screen, screen->phase, text);
        drain_events(screen);
        status = join_wifi(joined);
    }
    if (status == BENTO_OK) {
        (void)printf("connect() คืนค่า %s\n", bool_text(*joined));
    }
    return status;
}

/* Steps 2 and 3: read while up, cut the link, read again. */
static bento_status_t cut_link(const screen_t *screen, reading_t *up)
{
    reading_t down;
    char text[TEXT_BYTES];
    bento_status_t status;

    set_color(screen, screen->phase, COL_OK);
    status = refresh(screen, "2) ต่อแล้ว", up);
    if (status == BENTO_OK) {
        set_text(screen, screen->note,
                 "จำเลข IP ก้อนนี้ไว้ เดี๋ยวเทียบกับตอนหลุด");
        status = wait_ms(UP_PAUSE_MS);
    }
    if (status == BENTO_OK) {
        status = bento_wifi_disconnect();
        /* disconnect() never raises; only Stop ends the program */
        status = (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
    }
    if (status == BENTO_OK) {
        (void)printf("disconnect() คืนค่า None (None เสมอ)\n");
        lcd_line("สั่ง disconnect() แล้ว - ค่าที่คืนกลับมาคือ None");
        status = wait_ms(DOWN_PAUSE_MS);
    }
    if (status == BENTO_OK) {
        set_color(screen, screen->phase, COL_BAD);
        status = refresh(screen, "3) สั่งตัดแล้ว", &down);
    }
    if (status == BENTO_OK) {
        set_color(screen, screen->note, COL_BAD);
        (void)snprintf(text, sizeof(text), "ip() เปลี่ยนจาก %s เป็น %s",
                       up->ip, down.ip);
        set_text(screen, screen->note, text);
        set_color(screen, screen->note_2, COL_BAD);
        set_text(screen, screen->note_2,
                 "แต่ if wifi.ip(): ยังตอบ True อยู่ - "
                 "อย่าใช้มันตัดสิน");
    }
    return status;
}

/* The link stays down for DOWN_SECONDS, counted on the screen. */
static bento_status_t stay_down(const screen_t *screen)
{
    char text[TEXT_BYTES];
    int32_t second;
    bento_status_t status = BENTO_OK;

    for (second = 0; (second < DOWN_SECONDS) && (status == BENTO_OK);
         second++) {
        (void)snprintf(text, sizeof(text),
                       "3) หลุดอยู่ เหลืออีก %d "
                       "วินาที",
                       (int)(DOWN_SECONDS - second));
        set_text(screen, screen->phase, text);
        drain_events(screen);
        status = wait_ms(SECOND_MS);
    }
    return status;
}

/* Step 4: join again and compare the address with the first one. */
static bento_status_t rejoin(const screen_t *screen, const char *ip_up)
{
    reading_t again;
    char text[TEXT_BYTES];
    bool back = false;
    bento_status_t status;

    set_color(screen, screen->phase, COL_WARN);
    set_text(screen, screen->phase, "4) กำลังต่อกลับ");
    drain_events(screen);
    status = join_wifi(&back);
    if (status == BENTO_OK) {
        (void)printf("ต่อกลับคืนค่า %s\n", bool_text(back));
        set_color(screen, screen->phase, pick(back, COL_OK, COL_BAD));
        status = refresh(
            screen, back ? "4) ต่อกลับแล้ว" : "4) ต่อกลับไม่สำเร็จ", &again);
    }
    if (status == BENTO_OK) {
        set_color(screen, screen->note, COL_DIM);
        set_color(screen, screen->note_2, COL_DIM);
        if (strcmp(again.ip, ip_up) == 0) {
            (void)snprintf(text, sizeof(text),
                           "ได้ IP เดิม %s - "
                           "เราเตอร์จำเราได้",
                           again.ip);
        } else {
            (void)snprintf(text, sizeof(text),
                           "IP เปลี่ยน %s -> %s - "
                           "DHCP ให้ยืมเป็นครั้ง ๆ",
                           ip_up, again.ip);
        }
        set_text(screen, screen->note, text);
        set_text(screen, screen->note_2,
                 "สรุป: ถามสถานะด้วย is_connected() ไม่ใช่ด้วย "
                 "if wifi.ip():");
        drain_events(screen);
    }
    return status;
}

/* A failed first join: say so and end; there is nothing to cut. */
static void give_up(const screen_t *screen)
{
    set_color(screen, screen->phase, COL_BAD);
    set_text(screen, screen->phase,
             "ต่อไม่ติด ตรวจ WIFI_SSID กับรหัส Wi-Fi ในโค้ดไฟล์นี้");
    set_text(screen, screen->note,
             "ไฟล์นี้ต้องต่อติดก่อน "
             "จึงจะสาธิตการตัดได้");
    drain_events(screen);
    lcd_line("<span class=err>ต่อไม่ติด จบการทำงาน</span>");
}

/* Cut, wait, join again. */
static bento_status_t run_demo(const screen_t *screen)
{
    reading_t up;
    bento_status_t status = cut_link(screen, &up);

    if (status == BENTO_OK) {
        status = stay_down(screen);
    }
    if (status == BENTO_OK) {
        status = rejoin(screen, up.ip);
    }
    if (status == BENTO_OK) {
        print_lessons();
    }
    return status;
}

int main(void)
{
    screen_t screen;
    bool joined = false;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    /* the lcd calls ignore a console that is not ready (modlcd.c) */
    (void)bento_lcd_clear();
    (void)bento_lcd_console(
        "<h2>คาบ 9 - สั่งให้เน็ตหลุด แล้วดูว่าอะไรเปลี่ยน</h2>");
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        status = first_join(&screen, &joined);
    }
    if ((status == BENTO_OK) && !joined) {
        give_up(&screen);
    } else if (status == BENTO_OK) {
        status = run_demo(&screen);
    } else {
        /* an error or Stop ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
