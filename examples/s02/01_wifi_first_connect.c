/*
 * 01_wifi_first_connect.c -- ต่อ WiFi ครั้งแรกของคาบนี้
 *
 * พาบอร์ดออกเน็ตครั้งแรก ป้าย "กำลังต่อ" สีส้มขึ้นก่อน แล้วจอนิ่งไป
 * ระหว่างที่ wifi.connect() ทำงาน (นานได้ถึงราว 85 วินาที) พอกลับมา
 * จอบอกเวลาที่ใช้เป็น ms บนตัวเลขเจ็ดส่วน และเลข IP ตัวใหญ่ ถ้ายัง
 * ได้ 0.0.0.0 จะรอ DHCP อีกสองวินาทีแล้วอ่านซ้ำ สุดท้ายถาม
 * is_connected() ยืนยันอีกทาง ถ้าต่อไม่สำเร็จจอบอกเป็นสีแดงแล้วจบ
 *
 * Takes the board onto the network for the first time. An orange
 * "connecting" label shows first, then the screen holds still while
 * wifi.connect() works (up to about 85 s). When it returns, the
 * screen shows the time it took in ms on seven-segment digits and
 * the IP address. If the address is still 0.0.0.0 it waits two
 * seconds for DHCP and reads it again; last, is_connected() confirms
 * the link. A failed join is shown in red and the program ends.
 *
 * Twin of 01_wifi_first_connect.py
 * (embedded-systems-for-aiot-developer/examples/s02/01_wifi_first_connect.py).
 * Style: CLEAN_C.md.
 * Differs from the original:
 *   - wifi.connect() prints console lines of its own. Here the
 *     program prints "[WiFi] Connecting..." and "[WiFi] Connected!
 *     IP=..."; the radio's per-attempt and result-code lines are
 *     not reported by the C API. The binding saves the password on
 *     a successful join; C asks for that with
 *     BENTO_WIFI_F_SAVE_CREDS.
 *   - MicroPython waits inside wifi.connect(); C starts the join and
 *     asks every 50 ms, so Stop ends the wait in both.
 *   - wifi.connect() raises OSError when its worker is stuck past
 *     its own limit; C reads that as a failed join (False).
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

/* Timing and text sizes. */
enum {
    SETTLE_MS = 200,    /* let the cleared screen settle */
    WAIT_SLICE_MS = 50, /* the join is asked about this often */
    DHCP_ROUNDS = 10,   /* short rounds, then one more read */
    DHCP_ROUND_MS = 200,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t state;
    bento_ui_id_t seg;
    bento_ui_id_t ip;
    bento_ui_id_t step;
    bento_ui_id_t linked;
    bento_ui_id_t note;
} wifi_screen_t;

/* The course palette, 0xRRGGBB (SPEC S7.13). */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static const char *python_bool(bool value)
{
    return value ? "True" : "False";
}

/* wifi.ip(): always a text, "0.0.0.0" while there is no address. */
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

static bool is_connected(void)
{
    bool connected = false;

    if (bento_wifi_is_connected(&connected) != BENTO_OK) {
        connected = false;
    }
    return connected;
}

/* Layout on an 8 px grid, 24 px margin; all ends above y=340, where
 * the Console button is. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 128,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    CAPTION_Y = 70,
    VALUE_Y = 104,
    STATE_X = 40,
    TIME_X = 376,
    SEG_W = 144,
    SEG_H = 64,
    IP_X = 560,
    STEP_Y = 208,
    LINKED_Y = 256,
    NOTE_Y = 304,
    TITLE_PX = 24,
    TEXT_PX = 20
};

/* Where a label goes and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} look_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                look_t look, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = look.font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A dim caption above one of the card's three fields. */
static bento_status_t add_caption(bento_ui_t *ui, const char *text,
                                  int16_t x)
{
    const look_t look = {x, CAPTION_Y, COLOR_DIM, TEXT_PX};
    bento_ui_id_t unused;

    return add_label(ui, text, look, &unused);
}

static bento_status_t add_title_and_card(bento_ui_t *ui)
{
    const look_t title = {MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "ต่อ WiFi ครั้งแรกของคาบนี้", title, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = MARGIN_X;
        args.y = CARD_Y;
        args.w = CARD_W;
        args.h = CARD_H;
        args.color = COLOR_CARD;
        args.min_val = COLOR_CARD; /* the border colour */
        args.max_val = CARD_RADIUS;
        args.font_px = CARD_BORDER_PX;
        status = bento_ui_panel_create(ui, &args, &unused);
    }
    return status;
}

static bento_status_t add_seg(wifi_screen_t *screen)
{
    static const char dashes[] = "----";
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = dashes;
    args.text_len = (uint32_t)strlen(dashes);
    args.x = TIME_X;
    args.y = VALUE_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = COLOR_WARN;
    return bento_ui_seg7_create(screen->ui, &args, &screen->seg);
}

/* The card's three fields, spaced so a label that grows at run time
 * does not cover its neighbour. */
static bento_status_t add_fields(wifi_screen_t *screen)
{
    const look_t state = {STATE_X, VALUE_Y, COLOR_WARN, TEXT_PX};
    const look_t ip = {IP_X, VALUE_Y, COLOR_TEXT, TITLE_PX};
    bento_status_t status = add_caption(screen->ui, "สถานะ", STATE_X);

    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "กำลังจะเริ่มต่อ", state, &screen->state);
    }
    if (status == BENTO_OK) {
        status = add_caption(screen->ui, "เวลาที่ใช้ (ms)", TIME_X);
    }
    if (status == BENTO_OK) {
        status = add_seg(screen);
    }
    if (status == BENTO_OK) {
        status = add_caption(screen->ui, "IP ที่ได้", IP_X);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "-", ip, &screen->ip);
    }
    return status;
}

/* Three lines below the card that tell the story in order. */
static bento_status_t add_story(wifi_screen_t *screen)
{
    const look_t step = {MARGIN_X, STEP_Y, COLOR_TEXT, TEXT_PX};
    const look_t linked = {MARGIN_X, LINKED_Y, COLOR_DIM, TEXT_PX};
    const look_t note = {MARGIN_X, NOTE_Y, COLOR_DIM, TEXT_PX};
    bento_status_t status = add_label(screen->ui,
                                      "ป้ายนี้ขึ้นก่อน "
                                      "แล้วจอจะนิ่ง",
                                      step, &screen->step);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่ได้ถาม is_connected()",
                           linked, &screen->linked);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ครั้งแรกอาจรอนาน อย่ากดรีเซ็ต", note,
                           &screen->note);
    }
    return status;
}

static bento_status_t build_screen(wifi_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_title_and_card(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_fields(screen);
    }
    if (status == BENTO_OK) {
        status = add_story(screen);
    }
    return status;
}

/* ui.poll() whose events are not wanted; it also pushes the labels
 * onto the screen before a call that blocks. */
static void drop_events(const wifi_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* a failed poll answers no events in MicroPython too */
    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(const wifi_screen_t *screen, bento_ui_id_t label,
                     const char *text)
{
    (void)bento_ui_set_text(screen->ui, label, text,
                            (uint32_t)strlen(text));
}

static void set_color(const wifi_screen_t *screen, bento_ui_id_t label,
                      uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, label, color);
}

/* A failure has to speak too: a program that talks only on success
 * is silent exactly when someone wants to know. */
static void show_failure(const wifi_screen_t *screen,
                         int32_t elapsed_ms)
{
    char text[TEXT_BYTES];

    set_color(screen, screen->seg, COLOR_BAD);
    set_text(screen, screen->state, "ต่อไม่สำเร็จ");
    set_color(screen, screen->state, COLOR_BAD);
    set_text(screen, screen->note,
             "ตรวจชื่อวงกับรหัสผ่านอีกครั้ง "
             "แล้วรันใหม่");
    set_color(screen, screen->note, COLOR_BAD);
    drop_events(screen);
    (void)snprintf(text, sizeof(text),
                   "<span class=error>ต่อไม่สำเร็จใน %" PRId32
                   " ms</span>",
                   elapsed_ms);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
}

/* Linked but no address yet: short rounds, then one more read.
 * "0.0.0.0" is a non-empty text, so only comparing finds it. */
static bento_status_t wait_for_dhcp(const wifi_screen_t *screen,
                                    char *ip, uint32_t cap)
{
    char text[TEXT_BYTES];
    int32_t round;
    bento_status_t status = BENTO_OK;

    set_color(screen, screen->ip, COLOR_WARN);
    set_text(screen, screen->note,
             "ลิงก์ขึ้นแล้วแต่ยังไม่ได้เลข IP กำลังรอ DHCP");
    set_color(screen, screen->note, COLOR_WARN);
    drop_events(screen);
    (void)bento_lcd_print(
        "<span class=warn>ยังไม่ได้เลข IP รอ DHCP</span>");
    for (round = 0; (round < DHCP_ROUNDS) && (status == BENTO_OK);
         round++) {
        drop_events(screen);
        status = wait_ms(DHCP_ROUND_MS);
    }
    if (status == BENTO_OK) {
        read_ip(ip, cap);
        set_text(screen, screen->ip, ip);
        (void)snprintf(text, sizeof(text), "อ่านซ้ำได้ %s", ip);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* connect() said "it worked then"; is_connected() says "still now".
 * Two questions about two moments. */
static void confirm_link(const wifi_screen_t *screen,
                         int32_t elapsed_ms, const char *ip)
{
    char text[TEXT_BYTES];
    bool linked;

    (void)snprintf(text, sizeof(text), "is_connected() ตอบว่า %s",
                   python_bool(is_connected()));
    set_text(screen, screen->linked, text);
    set_color(screen, screen->linked,
              is_connected() ? COLOR_OK : COLOR_WARN);
    set_text(screen, screen->note, "เอาเลข IP นี้ไปกรอกในใบงานได้เลย");
    set_color(screen, screen->note, COLOR_DIM);
    drop_events(screen);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ต่อสำเร็จใน %" PRId32 " ms</span>",
                   elapsed_ms);
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text), "<span class=ok>IP: %s </span>",
                   ip);
    (void)bento_lcd_print(text);
    linked = is_connected();
    (void)printf("สรุป ip = %s | is_connected = %s\n", ip,
                 python_bool(linked));
}

/* Change these to the network the teacher hands out: the team's
 * phone hotspot (the faculty Wi-Fi needs a login the board cannot
 * do), and its password of 8 characters or more. */
static const char WIFI_SSID[] = "bento-teamXX";
static const char WIFI_PASS[] = "<รหัส Hotspot ของทีม>";

/* wifi.connect(): waits for the join in slices so Stop can end it.
 * *joined is its True or False. */
static bento_status_t join_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_status_t join = BENTO_E_IN_PROGRESS;
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
        join = bento_wifi_connect_poll();
    }
    while ((status == BENTO_OK) && (join == BENTO_E_IN_PROGRESS)) {
        status = wait_ms(WAIT_SLICE_MS);
        join = (status == BENTO_OK) ? bento_wifi_connect_poll() : join;
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* Stop wins either way */
    }
    *joined = (status == BENTO_OK) && (join == BENTO_OK);
    if (*joined) {
        read_ip(ip, sizeof(ip));
        (void)printf("[WiFi] Connected! IP=%s\r\n", ip);
    }
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

static bento_status_t show_success(const wifi_screen_t *screen,
                                   int32_t elapsed_ms)
{
    char text[TEXT_BYTES];
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_status_t status = BENTO_OK;

    set_color(screen, screen->seg, COLOR_OK);
    (void)snprintf(text, sizeof(text), "ต่อสำเร็จ - %s", WIFI_SSID);
    set_text(screen, screen->state, text);
    set_color(screen, screen->state, COLOR_OK);
    read_ip(ip, sizeof(ip));
    set_text(screen, screen->ip, ip);
    if (strcmp(ip, "0.0.0.0") == 0) {
        status = wait_for_dhcp(screen, ip, sizeof(ip));
    }
    if (status == BENTO_OK) {
        confirm_link(screen, elapsed_ms, ip);
    }
    return status;
}

/* Times the call that blocks, so the wait is a number, not a
 * feeling. */
static bento_status_t connect_and_report(const wifi_screen_t *screen)
{
    char text[TEXT_BYTES];
    const uint32_t start_ms = bento_time_ticks_ms();
    bool joined = false;
    bento_status_t status = join_wifi(&joined);
    const int32_t elapsed_ms =
        bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);

    if (status == BENTO_OK) {
        /* text, not value(): the digits stay under our control */
        (void)snprintf(text, sizeof(text), "%" PRId32, elapsed_ms);
        set_text(screen, screen->seg, text);
        (void)snprintf(text, sizeof(text),
                       "กลับมาแล้ว - connect() คืนค่า %s",
                       python_bool(joined));
        set_text(screen, screen->step, text);
        (void)printf("connect() ใช้เวลา %" PRId32 " ms คืนค่า %s\n",
                     elapsed_ms, python_bool(joined));
    }
    if ((status == BENTO_OK) && !joined) {
        show_failure(screen, elapsed_ms);
    } else if (status == BENTO_OK) {
        status = show_success(screen, elapsed_ms);
    } else {
        /* Stop pressed during the join */
    }
    return status;
}

int main(void)
{
    wifi_screen_t screen = {0};
    char text[TEXT_BYTES];
    bento_status_t status;

    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        /* the labels must reach the screen before the call blocks,
         * or the wait shows an empty screen */
        drop_events(&screen);
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ต่อ WiFi</h2>");
        (void)snprintf(text, sizeof(text), "กำลังต่อวง %s", WIFI_SSID);
        (void)bento_lcd_print(text);
        (void)printf("กำลังต่อ %s - บรรทัดถัดไปจะบล็อก\n", WIFI_SSID);
        status = connect_and_report(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
