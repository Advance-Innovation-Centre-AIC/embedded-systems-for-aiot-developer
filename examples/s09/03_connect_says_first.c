/*
 * 03_connect_says_first.c -- say it first, then wait
 *
 * บอกก่อนแล้วค่อยรอ เพราะ wifi.connect() บล็อก ป้าย "กำลังต่อ" สีส้ม
 * ต้องขึ้นจอก่อนบรรทัดที่บล็อก แล้วจอจึงนิ่ง พอต่อเสร็จจึงเขียนเวลาที่
 * รอเป็น ms ลงเลขเจ็ดส่วน กับหมายเลข IP ที่ได้มา ถ้าได้ "0.0.0.0"
 * แปลว่า DHCP ยังไม่แจกเลข โปรแกรมรออีกสองวินาทีแล้วอ่านซ้ำ
 *
 * Say it first, then wait: wifi.connect() blocks, so the orange
 * "connecting" label must reach the screen before the blocking line,
 * and only then does the screen freeze. When the join returns, the
 * time it took (ms) goes on the Seg7 and the IP address next to it.
 * An address of "0.0.0.0" means DHCP has not handed one out yet: the
 * program waits two seconds more and reads it again.
 *
 * Twin of 03_connect_says_first.py
 * (embedded-systems-for-aiot-developer/examples/s09/03_connect_says_first.py),
 * merged from the library twin aiot-s09-03-connect-says-first. Style: CLEAN_C.md.
 *
 * Differs from the original:
 *  - wifi.connect() is bento_wifi_connect_start/_poll with the
 *    binding's 86 s window and BENTO_WIFI_F_SAVE_CREDS (the binding
 *    saves the network for boot). False is a refusal, a timeout or a
 *    name or password the radio cannot take. The binding's own
 *    "[WiFi] ..." console lines are not printed by the C module.
 *  - wifi.ip() and is_connected() never raise in MicroPython: a
 *    failed C call reads as "0.0.0.0" and False.
 *  - `raise Stop` / `except Stop: pass` is the end of the status
 *    chain with 0. Stop during a sleep or the join ends the program
 *    with 0 at once (MicroPython raises KeyboardInterrupt there).
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

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200,    /* let the cleared screen settle */
    JOIN_POLL_MS = 100, /* how often the join is checked */
    DHCP_TRIES = 10,    /* then read the address again: 2 s in all */
    DHCP_WAIT_MS = 200
};

enum {
    FONT_PX = 20,
    AUTO = -1,       /* x, y, w or h the original leaves out */
    TEXT_BYTES = 160 /* longer than SET_TEXT and lcd carry */
};

/* Colours, 0xRRGGBB. */
enum {
    COL_TEXT = 0xE8EAED,
    COL_DIM = 0x9AA3AF,
    COL_OK = 0x30A46C,
    COL_WARN = 0xF5A623,
    COL_BAD = 0xE5484D
};

/* Where the program keeps the ids of the widgets it changes later. */
enum {
    SLOT_STATE = 0,
    SLOT_TIME = 1,
    SLOT_IP = 2,
    SLOT_NOTE = 3,
    SLOT_COUNT = 4,
    SLOT_NONE = 4 /* a widget nobody changes again */
};

/* One ui.<Widget>(...) call of the original: the kind, the slot that
 * keeps its id, its text (NULL: worked out at run time) and the
 * factory's x, y, w, h, color, min, max and value. */
typedef struct {
    uint8_t kind;
    uint8_t slot;
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t minimum;
    int32_t maximum;
    int32_t value;
} widget_spec_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[SLOT_COUNT + 1]; /* + the spare for SLOT_NONE */
} wifi_screen_t;

/* A dotted-quad IPv4 address and its NUL. */
typedef struct {
    char text[BENTO_WIFI_IPV4_TEXT_MAX];
} ip_text_t;

static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Waits for the join the radio is making; Stop cancels it. */
static bento_status_t wait_for_join(void)
{
    bento_status_t status = BENTO_E_IN_PROGRESS;

    while (status == BENTO_E_IN_PROGRESS) {
        status = bento_time_sleep_ms(JOIN_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_connect_poll();
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* answers BENTO_OK only */
    }
    return status;
}

/* wifi.connect(ssid, password): *joined is its True or False. A
 * refusal, a timeout or a name or password the radio cannot take is
 * False; anything else is an error the original would raise. */
static bento_status_t join_wifi(const char *ssid, const char *password,
                                bool *joined)
{
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
    *joined = (status == BENTO_OK);
    if ((status == BENTO_E_FAILED) || (status == BENTO_E_TIMEOUT) ||
        (status == BENTO_E_INVALID_ARGUMENT)) {
        status = BENTO_OK;
    }
    return status;
}

/* wifi.ip(): "0.0.0.0" when there is no address. */
static void read_ip(ip_text_t *ip)
{
    uint32_t address = 0U;
    uint32_t length = 0U;

    if (bento_wifi_ip(BENTO_WIFI_IF_ANY, &address) != BENTO_OK) {
        address = 0U;
    }
    if (bento_wifi_ipv4_format(address, ip->text, sizeof(ip->text),
                               &length) != BENTO_OK) {
        (void)snprintf(ip->text, sizeof(ip->text), "0.0.0.0");
    }
}

static const char *python_bool(bool value)
{
    return value ? "True" : "False";
}

static bool link_is_up(void)
{
    bool up = false;

    if (bento_wifi_is_connected(&up) != BENTO_OK) {
        up = false;
    }
    return up;
}

/* ui.poll() here only lets the screen catch up; no event is read. */
static void poll_ui(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* a failed ui.poll() answers no events, which is all this needs */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The widget setters ignore a refusal in MicroPython (modui.c). */
static void show_text(const wifi_screen_t *screen, uint32_t slot,
                      const char *text)
{
    (void)bento_ui_set_text(screen->ui, screen->ids[slot], text,
                            (uint32_t)strlen(text));
}

static void show_color(const wifi_screen_t *screen, uint32_t slot,
                       uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, screen->ids[slot], color);
}

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void console_line(const char *text)
{
    (void)bento_lcd_print(text);
}

static bento_status_t add_widget(wifi_screen_t *screen,
                                 const widget_spec_t *spec,
                                 const char *text)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = spec->x;
    args.y = spec->y;
    args.w = spec->w;
    args.h = spec->h;
    args.color = spec->color;
    args.min_val = spec->minimum;
    args.max_val = spec->maximum;
    args.font_px = spec->value;
    return bento_ui_create(screen->ui, spec->kind, &args,
                           &screen->ids[spec->slot]);
}

static void tell_failure(const wifi_screen_t *screen, int32_t took_ms)
{
    char line[TEXT_BYTES];

    show_color(screen, SLOT_TIME, COL_BAD);
    show_color(screen, SLOT_STATE, COL_BAD);
    show_text(screen, SLOT_STATE,
              "ต่อไม่สำเร็จ "
              "ตรวจชื่อวงและรหัสผ่านอีกครั้ง");
    poll_ui(screen->ui);
    (void)snprintf(line, sizeof(line),
                   "<span class=err>ต่อไม่สำเร็จใน %ld ms</span>",
                   (long)took_ms);
    console_line(line);
    console_line("ตรวจชื่อวงและรหัสผ่านอีกครั้ง");
}

/* The link can be up before DHCP has handed out an address: wait a
 * little and read it again. ip() always answers a string, and
 * "0.0.0.0" is a true one, so it is compared, not just tested. */
static bento_status_t wait_for_address(const wifi_screen_t *screen)
{
    ip_text_t ip;
    char line[TEXT_BYTES];
    uint32_t i;
    bento_status_t status = BENTO_OK;

    show_color(screen, SLOT_IP, COL_WARN);
    show_color(screen, SLOT_NOTE, COL_WARN);
    show_text(screen, SLOT_NOTE,
              "ลิงก์ขึ้นแล้วแต่ยังไม่ได้เลข IP "
              "กำลังรอ DHCP");
    poll_ui(screen->ui);
    console_line("ยังไม่ได้เลข IP รอ DHCP อีกครู่");
    for (i = 0U; (i < (uint32_t)DHCP_TRIES) && (status == BENTO_OK);
         i++) {
        poll_ui(screen->ui);
        status = pause_ms(DHCP_WAIT_MS);
    }
    if (status == BENTO_OK) {
        read_ip(&ip);
        show_text(screen, SLOT_IP, ip.text);
        (void)snprintf(line, sizeof(line), "อ่านซ้ำได้ %s", ip.text);
        console_line(line);
    }
    return status;
}

static void tell_summary(const wifi_screen_t *screen)
{
    ip_text_t ip;
    char line[TEXT_BYTES];

    show_text(screen, SLOT_NOTE,
              "ip()==\"0.0.0.0\" ต้องเทียบตรง ๆ "
              "อย่าเช็กแค่ if wifi.ip():");
    poll_ui(screen->ui);
    read_ip(&ip);
    (void)printf("สรุป: ip = %s is_connected = %s\n", ip.text,
                 python_bool(link_is_up()));
    read_ip(&ip);
    (void)snprintf(line, sizeof(line), "สรุป ip = %s connected = %s",
                   ip.text, python_bool(link_is_up()));
    console_line(line);
}

/* The three steps of the lesson stay on screen the whole run. The
 * state label (text NULL) names the network. */
static bento_status_t add_widgets(wifi_screen_t *screen)
{
    static const widget_spec_t rows[] = {
        {UI_WIDGET_LABEL, SLOT_NONE, "คาบ 9 - ต่อ WiFi แล้วบอกก่อนรอ", 20,
         12, AUTO, AUTO, COL_TEXT, 0, 0, 24},
        {UI_WIDGET_LABEL, SLOT_STATE, NULL, 40, 68, AUTO, AUTO,
         COL_WARN, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_NONE, "เวลาที่ใช้ (ms)", 40, 102, AUTO,
         AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_SEG7, SLOT_TIME, "----", 40, 128, 180, 60, COL_WARN,
         0, 0, 0},
        {UI_WIDGET_LABEL, SLOT_NONE, "IP ที่ได้", 260, 102, AUTO, AUTO,
         COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_IP, "-", 260, 132, AUTO, AUTO, COL_TEXT,
         0, 0, 24},
        {UI_WIDGET_LABEL, SLOT_NONE,
         "1) ป้าย \"กำลังต่อ\" ขึ้นจอ แล้ว "
         "ui.poll()",
         40, 228, AUTO, AUTO, COL_TEXT, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_NONE,
         "2) wifi.connect() บล็อก "
         "จอนิ่งตลอดช่วงนี้",
         40, 260, AUTO, AUTO, COL_TEXT, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_NONE, "3) กลับมาแล้วจึงเขียนผลลงจอ", 40, 288,
         AUTO, AUTO, COL_TEXT, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_NOTE, "ป้ายต้องขึ้นก่อนบรรทัดที่บล็อก", 20, 330,
         AUTO, AUTO, COL_DIM, 0, 0, FONT_PX}};
    char connecting[TEXT_BYTES];
    uint32_t i;
    bento_status_t status = BENTO_OK;

    (void)snprintf(connecting, sizeof(connecting),
                   "กำลังต่อ %s ... จอจะนิ่งไปครู่หนึ่ง", WIFI_SSID);
    for (i = 0U;
         (i < (sizeof(rows) / sizeof(rows[0]))) && (status == BENTO_OK);
         i++) {
        const char *text =
            (rows[i].text != NULL) ? rows[i].text : connecting;

        status = add_widget(screen, &rows[i], text);
    }
    return status;
}

/* The label and the poll come before connect(), always: they promise
 * the user the system is alive and busy with something slow. */
static bento_status_t build_screen(wifi_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_widgets(screen);
    }
    if (status == BENTO_OK) {
        poll_ui(screen->ui);
    }
    return status;
}

static bento_status_t tell_success(const wifi_screen_t *screen,
                                   int32_t took_ms)
{
    ip_text_t ip;
    char line[TEXT_BYTES];
    bento_status_t status = BENTO_OK;

    read_ip(&ip);
    show_color(screen, SLOT_TIME, COL_OK);
    show_color(screen, SLOT_STATE, COL_OK);
    (void)snprintf(line, sizeof(line), "ต่อสำเร็จ - %s", WIFI_SSID);
    show_text(screen, SLOT_STATE, line);
    show_text(screen, SLOT_IP, ip.text);
    poll_ui(screen->ui);
    (void)snprintf(line, sizeof(line),
                   "<span class=ok>ต่อสำเร็จใน %ld ms</span>",
                   (long)took_ms);
    console_line(line);
    (void)snprintf(line, sizeof(line), "IP: %s", ip.text);
    console_line(line);
    if (strcmp(ip.text, "0.0.0.0") == 0) {
        status = wait_for_address(screen);
    }
    return status;
}

/* connect() answers a plain bool, unlike tesaiot.connect() in period
 * 11, which returns before the link is up. The time goes to the Seg7
 * as text, to keep its format under control. */
static bento_status_t run(const wifi_screen_t *screen)
{
    char line[TEXT_BYTES];
    bool joined = false;
    uint32_t start_ms;
    int32_t took_ms;
    bento_status_t status;

    (void)snprintf(line, sizeof(line), "กำลังต่อ %s - อาจรอนาน",
                   WIFI_SSID);
    console_line(line);
    (void)printf("กำลังต่อ %s\n", WIFI_SSID);
    start_ms = bento_time_ticks_ms();
    status = join_wifi(WIFI_SSID, WIFI_PASS, &joined);
    took_ms = bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);
    if (status == BENTO_OK) {
        (void)snprintf(line, sizeof(line), "%ld", (long)took_ms);
        show_text(screen, SLOT_TIME, line);
        (void)printf("connect() ใช้เวลา %ld ms คืนค่า %s\n", (long)took_ms,
                     python_bool(joined));
        if (joined) {
            status = tell_success(screen, took_ms);
        } else {
            tell_failure(screen, took_ms);
        }
    }
    if ((status == BENTO_OK) && joined) {
        tell_summary(screen);
    }
    return status;
}

int main(void)
{
    wifi_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    (void)bento_lcd_clear(); /* refused like print: ignored */
    (void)bento_lcd_console("<h2>คาบ 9 - ต่อ WiFi</h2>");
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        status = run(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
