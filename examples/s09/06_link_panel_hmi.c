/*
 * 06_link_panel_hmi.c -- หน้าจอสถานะลิงก์ (Link status panel)
 *
 * การ์ดเดียวกลางจอบอก SSID, IP และเวลา ping ไป 8.8.8.8 อัปเดตทุก
 * วินาที ทุกตัวเลขบนจอวัดมาจริง ลองถอดสาย LAN ของเราเตอร์แล้วดูว่า
 * ping เปลี่ยนเป็น "ไม่ตอบ" แต่ IP ยังอยู่ ก่อนกด Run ให้แก้ชื่อวง
 * WIFI_SSID (เหนือฟังก์ชัน connect_wifi) และรหัสผ่าน wifi_pass
 * (ต้นฟังก์ชัน connect_wifi) เป็นวงของห้องเรียน
 *
 * One card shows the SSID, the IP address and the ping time to
 * 8.8.8.8, updated every second; every number on it is measured.
 * Unplug the router's LAN cable and watch ping turn to "no answer"
 * while the IP stays. Before you run it, set the network name
 * WIFI_SSID (just above the function connect_wifi) and the password
 * wifi_pass (at the top of the function connect_wifi).
 *
 * Twin of 06_link_panel_hmi.py
 * (embedded-systems-for-aiot-developer/examples/s09/06_link_panel_hmi.py),
 * merged from the library twin aiot-s09-06-link-panel-hmi. Style: CLEAN_C.md.
 * Differs from the original: wifi.connect() is
 * bento_wifi_connect_start and its poll. The binding's lines
 * "[WiFi] Connecting..." and "[WiFi] Connected! IP=..." are written
 * by the program here; its per-attempt and "not found" lines and
 * "Credentials saved" are not, the C API does not report them.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* Ping limits, in milliseconds. */
enum {
    PING_TIMEOUT_MS = 1500, /* short, so a dead link never hangs */
    GOOD_MS = 60,           /* faster than this is good */
    SLOW_MS = 200           /* slower than this needs a look */
};

/* Timing, in milliseconds. One second between rounds is waited in
 * 5 steps with a poll between them: one long sleep makes the
 * firmware hide the widgets. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    WAIT_STEPS = 5,
    WAIT_STEP_MS = 200,
    WIFI_SLICE_MS = 50 /* how often a running Wi-Fi call is asked */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* Layout on the 792 x 398 canvas. */
enum {
    FONT_PX = 20,
    CARD_X = 40,
    CARD_Y = 32,
    CARD_W = 700,
    CARD_H = 300,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    TEXT_X = 60,
    HEADING_Y = 44,
    SSID_Y = 88,
    IP_Y = 132,
    PING_Y = 176,
    BAR_Y = 216,
    BAR_W = 620,
    BAR_H = 24,
    LEGEND_Y = 242,
    NOTE_Y = 276,
    LINE_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ssid;
    bento_ui_id_t ip;
    bento_ui_id_t ping;
    bento_ui_id_t bar;
    bento_ui_id_t note;
} link_screen_t;

/* What one round found, as the ping line, the bar and the note. */
typedef struct {
    uint32_t color;
    const char *ping_text;
    int32_t bar_ms;
    uint32_t note_color;
    const char *note_text;
} verdict_t;

static bento_status_t check_running(bento_status_t status)
{
    bento_status_t result = status;

    if ((result == BENTO_OK) && !bento_runtime_is_running()) {
        result = BENTO_E_CANCELLED;
    }
    return result;
}

/* wifi.ip(): "0.0.0.0" when there is no address. */
static void read_ip(char *text, uint32_t capacity)
{
    uint32_t address = 0U;
    uint32_t length = 0U;

    if ((bento_wifi_ip(BENTO_WIFI_IF_ANY, &address) != BENTO_OK) ||
        (bento_wifi_ipv4_format(address, text, capacity, &length) !=
         BENTO_OK)) {
        (void)snprintf(text, capacity, "0.0.0.0");
    }
}

/* Asks the started join every WIFI_SLICE_MS until it ends. The
 * poll itself answers BENTO_E_CANCELLED on Stop. */
static bento_status_t wait_connect(void)
{
    bento_status_t status = BENTO_OK;
    bool waiting = true;

    while (waiting) {
        status = bento_wifi_connect_poll();
        waiting = status == BENTO_E_IN_PROGRESS;
        if (waiting) {
            status = check_running(bento_time_sleep_ms(WIFI_SLICE_MS));
            waiting = status == BENTO_OK;
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* answers BENTO_OK always */
    }
    return status;
}

/* wifi.ping() as the original reads it: the round trip in ms, or -1
 * when the target did not answer or the ping could not start (the
 * original catches that OSError as -1 too). Only Stop is an error. */
static bento_status_t ping_ms(uint32_t target, int32_t *round_trip_ms)
{
    uint32_t rtt_ms = 0U;
    bento_status_t status =
        bento_wifi_ping_start(target, PING_TIMEOUT_MS);
    bool waiting = status == BENTO_OK;

    while (waiting) {
        status = bento_wifi_ping_poll(&rtt_ms);
        waiting = status == BENTO_E_IN_PROGRESS;
        if (waiting) {
            status = check_running(bento_time_sleep_ms(WIFI_SLICE_MS));
            waiting = status == BENTO_OK;
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_ping_cancel(); /* answers BENTO_OK always */
    }
    *round_trip_ms = (status == BENTO_OK) ? (int32_t)rtt_ms : -1;
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t label,
                      const char *text)
{
    (void)bento_ui_set_text(ui, label, text, (uint32_t)strlen(text));
}

static void show_color(bento_ui_t *ui, bento_ui_id_t widget,
                       uint32_t color)
{
    (void)bento_ui_set_color(ui, widget, color);
}

/* ui.poll() is called only to keep the screen alive; its events are
 * not used, and a failed poll answers none. */
static void keep_alive(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = TEXT_X;
    args.y = y;
    args.color = color;
    args.font_px = FONT_PX;
    return bento_ui_label_create(ui, &args, out_id);
}

/* The card: a panel whose min= is its border colour, max= its
 * corner radius and value= its border width. */
static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = CARD_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM;
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &card);
}

/* The ping bar: a full bar means no answer at all. */
static bento_status_t add_bar(link_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = TEXT_X;
    args.y = BAR_Y;
    args.w = BAR_W;
    args.h = BAR_H;
    args.max_val = PING_TIMEOUT_MS;
    args.color = COLOR_DIM;
    return bento_ui_bar_create(screen->ui, &args, &screen->bar);
}

static bento_status_t add_readings(link_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    bento_ui_id_t unused;
    bento_status_t status;

    status =
        add_label(ui, "LINK STATUS", HEADING_Y, COLOR_DIM, &unused);
    if (status == BENTO_OK) {
        status =
            add_label(ui, "SSID: -", SSID_Y, COLOR_TEXT, &screen->ssid);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "IP: -", IP_Y, COLOR_TEXT, &screen->ip);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ping 8.8.8.8: -", PING_Y, COLOR_TEXT,
                           &screen->ping);
    }
    return status;
}

/* Every widget exists before the loop: making one inside it costs
 * time and a share of the 32-widget quota. */
static bento_status_t build_screen(link_screen_t *screen)
{
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = check_running(bento_time_sleep_ms(SETTLE_MS));
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_readings(screen);
    }
    if (status == BENTO_OK) {
        status = add_bar(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "แถบยาว = ตอบช้า - เต็มราง = "
                           "ไม่ตอบเลย",
                           LEGEND_Y, COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังต่อ...", NOTE_Y, COLOR_DIM,
                           &screen->note);
    }
    if (status == BENTO_OK) {
        keep_alive(screen->ui);
    }
    return status;
}

static void show_verdict(const link_screen_t *screen,
                         const verdict_t *verdict)
{
    show_color(screen->ui, screen->ping, verdict->color);
    show_text(screen->ui, screen->ping, verdict->ping_text);
    show_color(screen->ui, screen->bar, verdict->color);
    (void)bento_ui_set_value(screen->ui, screen->bar, verdict->bar_ms);
    show_color(screen->ui, screen->note, verdict->note_color);
    show_text(screen->ui, screen->note, verdict->note_text);
}

/* Green when fast, orange when slow, red when very slow. */
static uint32_t color_of(int32_t round_trip_ms)
{
    uint32_t color = COLOR_BAD;

    if (round_trip_ms <= GOOD_MS) {
        color = COLOR_OK;
    } else if (round_trip_ms <= SLOW_MS) {
        color = COLOR_WARN;
    } else {
        /* very slow stays red */
    }
    return color;
}

/* Connected but the target silent is another fault than a lost
 * link, and it is fixed another way. */
static void show_ping(const link_screen_t *screen, int32_t ms)
{
    char line[LINE_BYTES];
    verdict_t verdict;

    verdict.ping_text = line;
    if (ms < 0) {
        (void)snprintf(line, sizeof(line),
                       "ping 8.8.8.8: ไม่ตอบใน %d ms", PING_TIMEOUT_MS);
        verdict.color = COLOR_WARN;
        verdict.bar_ms = PING_TIMEOUT_MS;
        verdict.note_color = COLOR_WARN;
        verdict.note_text = "ต่อวงได้ แต่ออกอินเทอร์เน็ตไม่ได้";
    } else {
        (void)snprintf(line, sizeof(line), "ping 8.8.8.8: %d ms",
                       (int)ms);
        verdict.color = color_of(ms);
        verdict.bar_ms = ms;
        verdict.note_color = COLOR_OK;
        verdict.note_text = "ปกติ - อัปเดตทุก 1 วินาที";
    }
    show_verdict(screen, &verdict);
}

/* A lost link must say so: a screen that keeps old numbers is worse
 * than an empty one, because the reader believes it. */
static void show_link_lost(const link_screen_t *screen)
{
    static const verdict_t lost = {COLOR_BAD, "ping 8.8.8.8: ลิงก์หลุด",
                                   PING_TIMEOUT_MS, COLOR_BAD,
                                   "ลิงก์หลุด - "
                                   "ตัวเลขที่เห็นเป็นของรอบก่อนหน้า"};

    show_color(screen->ui, screen->ip, COLOR_BAD);
    show_verdict(screen, &lost);
}

static void show_join_failed(const link_screen_t *screen)
{
    show_color(screen->ui, screen->note, COLOR_BAD);
    show_text(screen->ui, screen->note,
              "ต่อไม่สำเร็จ "
              "ตรวจชื่อวงและรหัสผ่าน");
    keep_alive(screen->ui);
}

/* The classroom network the teacher hands out (its password is set
 * where the program joins). */
static const char WIFI_SSID[] = "AIoT-Class";

/* wifi.connect(): *joined is false when the network was not found or
 * refused us, as the original's False; other failures end the run.
 * The original's binding saves the network after a join, so the
 * twin asks for that too. */
static bento_status_t connect_wifi(bool *joined)
{
    /* the classroom password, WIFI_PASS in the .py: set it before you
     * run */
    static const char wifi_pass[] = "<รหัสผ่านของห้องเรียน>";
    bento_wifi_connect_params_t params;
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = wifi_pass;
    params.pass_len = (uint32_t)strlen(wifi_pass);
    params.flags = BENTO_WIFI_F_SAVE_CREDS;
    (void)printf("[WiFi] Connecting...\n");
    status = bento_wifi_connect_start(&params);
    if (status == BENTO_OK) {
        status = wait_connect();
    }
    *joined = status == BENTO_OK;
    if (*joined) {
        read_ip(address, sizeof(address));
        (void)printf("[WiFi] Connected! IP=%s\n", address);
    }
    if ((status == BENTO_E_TIMEOUT) || (status == BENTO_E_FAILED)) {
        status = BENTO_OK;
    }
    return status;
}

/* One round: each value is read in turn and shown at once. */
static bento_status_t update_round(const link_screen_t *screen,
                                   uint32_t target)
{
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    char line[LINE_BYTES];
    bool up = false;
    int32_t ms = -1;
    bento_status_t status = BENTO_OK;

    /* wifi.is_connected() answers False when it cannot tell */
    if (bento_wifi_is_connected(&up) != BENTO_OK) {
        up = false;
    }
    read_ip(ip, sizeof(ip));
    (void)snprintf(line, sizeof(line), "IP: %s", ip);
    show_text(screen->ui, screen->ip, line);
    if (!up || (strcmp(ip, "0.0.0.0") == 0)) {
        show_link_lost(screen);
    } else {
        show_color(screen->ui, screen->ip, COLOR_TEXT);
        status = ping_ms(target, &ms);
        if (status == BENTO_OK) {
            show_ping(screen, ms);
        }
    }
    return status;
}

/* One second, polling in between so the widgets stay shown. */
static bento_status_t wait_second(bento_ui_t *ui)
{
    bento_status_t status = BENTO_OK;
    int32_t step;

    for (step = 0; (step < WAIT_STEPS) && (status == BENTO_OK);
         step++) {
        keep_alive(ui);
        status = check_running(bento_time_sleep_ms(WAIT_STEP_MS));
    }
    return status;
}

/* Runs until Stop. */
static bento_status_t run_loop(const link_screen_t *screen)
{
    /* Google Public DNS: outside our network, and it always answers. */
    static const char ping_ip[] = "8.8.8.8";
    uint32_t target = 0U;
    bento_status_t status = bento_wifi_ipv4_parse(
        ping_ip, (uint32_t)strlen(ping_ip), &target);

    while (status == BENTO_OK) {
        status = update_round(screen, target);
        if (status == BENTO_OK) {
            status = wait_second(screen->ui);
        }
    }
    return status;
}

/* The SSID comes from our own constant, written once after the
 * join: the board cannot say which network it joined. */
static void show_joined(const link_screen_t *screen)
{
    char line[LINE_BYTES];

    (void)snprintf(line, sizeof(line), "SSID: %s", WIFI_SSID);
    show_text(screen->ui, screen->ssid, line);
    show_color(screen->ui, screen->ssid, COLOR_OK);
    show_text(screen->ui, screen->note,
              "อัปเดตทุก 1 วินาที - ทุกตัวเลขบนจอนี้วัดมาจริง");
}

int main(void)
{
    link_screen_t screen = {0};
    bool joined = false;
    bento_status_t status;

    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        /* the note "connecting" is up already: the screen does not
         * change while connect() runs */
        status = connect_wifi(&joined);
    }
    if ((status == BENTO_OK) && !joined) {
        show_join_failed(&screen);
    }
    if ((status == BENTO_OK) && joined) {
        show_joined(&screen);
        status = run_loop(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
