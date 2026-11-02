/*
 * 02_link_uptime.c -- connected, and still connected
 *
 * ต่อติดแล้ว กับยังต่ออยู่ ไม่ใช่คำถามเดียวกัน โปรแกรมต่อ WiFi
 * ครั้งเดียว แล้วถาม is_connected() ซ้ำทุกวินาทีนาน 30 วินาที ป้าย IP
 * มุมขวาบนค้างไว้ตลอด เลขวินาทีที่ออนไลน์เดินขึ้น เส้นกราฟอยู่สูงตอน
 * ลิงก์ปกติและทรุดลงทันทีที่หลุด ลิ้นชัก Console เพิ่มบรรทัดเฉพาะตอน
 * สถานะเปลี่ยน ลองปิด WiFi ที่เราเตอร์ระหว่างที่โปรแกรมเดินแล้วดูเส้น
 *
 * Connected once is not the same as still connected. The program
 * joins Wi-Fi once, then asks is_connected() every second for 30
 * seconds. The IP badge stays in the top right corner, the online
 * seconds count up, and the chart line stays high while the link is
 * up and drops to the floor the moment it is lost. The Console drawer
 * gets a line only when the state changes. Turn the router's Wi-Fi
 * off for a moment while it runs and watch the line.
 *
 * Twin of 02_link_uptime.py
 * (embedded-systems-for-aiot-developer/examples/s02/02_link_uptime.py).
 * Style: CLEAN_C.md.
 *
 * Differs from the original:
 *  - wifi.connect() is bento_wifi_connect_start/_poll with the
 *    binding's 86 s window and BENTO_WIFI_F_SAVE_CREDS (the binding
 *    saves the network for boot). False is a refusal, a timeout or a
 *    name or password the radio cannot take. The binding's own
 *    "[WiFi] ..." console lines are not printed by the C module.
 *  - wifi.ip() and is_connected() never raise in MicroPython: a
 *    failed C call reads as "0.0.0.0" and false.
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

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200,   /* let the cleared screen settle */
    WATCH_MS = 30000,  /* how long to watch the link */
    TICK_MS = 1000,    /* ask again this often */
    JOIN_POLL_MS = 100 /* how often the join is checked */
};

enum {
    MS_PER_SECOND = 1000,
    PERCENT = 100,
    LINE_HIGH = 100, /* the chart's value while the link is up */
    LINE_LOW = 0,
    UNKNOWN = -1, /* no state seen yet: the first round is a change */
    FONT_PX = 20,
    AUTO = -1,       /* x, y, w or h the original leaves out */
    TEXT_BYTES = 160 /* longer than SET_TEXT and lcd carry */
};

/* The course palette: one value per role. */
enum {
    COL_TEXT = 0xE8EAED,
    COL_DIM = 0x9AA3AF,
    COL_CARD = 0x171B22,
    COL_ACCENT = 0x4A9EFF,
    COL_OK = 0x30A46C,
    COL_WARN = 0xF5A623,
    COL_BAD = 0xE5484D
};

/* Where the program keeps the ids of the widgets it changes later. */
enum {
    SLOT_IP,
    SLOT_LINK,
    SLOT_DROPS,
    SLOT_SECONDS,
    SLOT_HISTORY,
    SLOT_CHART,
    SLOT_STATE,
    SLOT_COUNT
};

enum {
    SLOT_NONE = SLOT_COUNT /* a widget nobody changes again */
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[SLOT_COUNT + 1]; /* + the spare for SLOT_NONE */
    uint8_t series;                    /* the link's line */
} link_screen_t;

/* What the watch has counted. */
typedef struct {
    int32_t online_ms;
    int32_t drops;
    int32_t last; /* 1 up, 0 down, UNKNOWN */
} watch_t;

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

static bool link_is_up(void)
{
    bool up = false;

    if (bento_wifi_is_connected(&up) != BENTO_OK) {
        up = false;
    }
    return up;
}

/* One ui.<Widget>(...) call of the original: the kind, the slot that
 * keeps its id, its text and the factory's x, y, w, h, color, min,
 * max and value. */
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
static void show_text(const link_screen_t *screen, uint32_t slot,
                      const char *text)
{
    (void)bento_ui_set_text(screen->ui, screen->ids[slot], text,
                            (uint32_t)strlen(text));
}

static void show_color(const link_screen_t *screen, uint32_t slot,
                       uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, screen->ids[slot], color);
}

static void show(const link_screen_t *screen, uint32_t slot,
                 const char *text, uint32_t color)
{
    show_text(screen, slot, text);
    show_color(screen, slot, color);
}

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void console_line(const char *text)
{
    (void)bento_lcd_print(text);
}

static bento_status_t add_widget(link_screen_t *screen,
                                 const widget_spec_t *spec)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    if (spec->text != NULL) {
        args.text = spec->text;
        args.text_len = (uint32_t)strlen(spec->text);
    }
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

/* The status card. The drop counter is a count, not a state, so it
 * starts in the text colour: green before any check would read as
 * "confirmed fine". */
static bento_status_t add_card(link_screen_t *screen)
{
    static const widget_spec_t rows[] = {
        {UI_WIDGET_LABEL, SLOT_NONE, "ต่อติดแล้ว แต่ยังต่ออยู่ไหม", 24, 8, AUTO,
         AUTO, COL_TEXT, 0, 0, 24},
        {UI_WIDGET_LABEL, SLOT_IP, "ยังไม่ได้ต่อ", 480, 16, AUTO, AUTO,
         COL_WARN, 0, 0, FONT_PX},
        {UI_WIDGET_PANEL, SLOT_NONE, NULL, 24, 56, 744, 160, COL_CARD,
         COL_CARD, 12, 1},
        {UI_WIDGET_LABEL, SLOT_NONE, "สถานะลิงก์", 40, 70, AUTO, AUTO,
         COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_LINK, "ยังไม่ได้ตรวจ", 40, 104, AUTO, AUTO,
         COL_DIM, 0, 0, 28},
        {UI_WIDGET_LABEL, SLOT_NONE, "หลุดไปแล้ว (ครั้ง)", 456, 70, AUTO,
         AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_DROPS, "0", 456, 104, AUTO, AUTO,
         COL_TEXT, 0, 0, 28},
        {UI_WIDGET_LABEL, SLOT_NONE, "ออนไลน์มาแล้ว (วินาที)", 40, 156,
         AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_SEG7, SLOT_SECONDS, "0", 304, 144, 144, 56, COL_DIM,
         0, 0, 0},
        {UI_WIDGET_LABEL, SLOT_NONE,
         "เส้นสูง = ต่ออยู่  เส้นต่ำ = "
         "หลุด",
         24, 224, AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_HISTORY, "ประวัติอยู่ในลิ้นชัก Console", 440,
         224, AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_CHART, SLOT_CHART, NULL, 24, 256, 744, 80, COL_CARD,
         0, LINE_HIGH, 0}};
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U;
         (i < (sizeof(rows) / sizeof(rows[0]))) && (status == BENTO_OK);
         i++) {
        status = add_widget(screen, &rows[i]);
    }
    return status;
}

/* The chart's one line is the accent colour: green means "fine"
 * only. The status line sits at the bottom. */
static bento_status_t build_screen(link_screen_t *screen)
{
    static const widget_spec_t state = {
        UI_WIDGET_LABEL, SLOT_STATE, "กำลังเริ่ม", 24,     348, AUTO, AUTO,
        COL_DIM,         0,          0,         FONT_PX};
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_card(screen);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui,
                                           screen->ids[SLOT_CHART],
                                           COL_ACCENT, &screen->series);
    }
    if (status == BENTO_OK) {
        status = add_widget(screen, &state);
    }
    if (status == BENTO_OK) {
        /* the labels must be on screen before the blocking join */
        poll_ui(screen->ui);
    }
    return status;
}

static void tell_no_join(const link_screen_t *screen)
{
    show(screen, SLOT_IP, "ต่อไม่ติด", COL_BAD);
    show(screen, SLOT_LINK, "ต่อไม่ติด", COL_BAD);
    show(screen, SLOT_STATE,
         "ตรวจชื่อวงกับรหัสผ่านอีกครั้ง "
         "แล้วรันใหม่",
         COL_BAD);
    poll_ui(screen->ui);
    console_line("<span class=error>ต่อไม่ติด - "
                 "ยังไม่ได้เริ่มเฝ้าดู</span>");
}

static void tell_joined(const link_screen_t *screen)
{
    ip_text_t ip;
    char text[TEXT_BYTES];

    read_ip(&ip);
    (void)snprintf(text, sizeof(text), "IP %s", ip.text);
    show(screen, SLOT_IP, text, COL_OK);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ต่อสำเร็จ IP %s </span>", ip.text);
    console_line(text);
}

/* The screen answers "how is it now", every round. */
static void show_link(const link_screen_t *screen, bool up,
                      const watch_t *watch)
{
    char seconds[TEXT_BYTES];
    const uint32_t color = up ? COL_OK : COL_BAD;

    show(screen, SLOT_LINK, up ? "ต่ออยู่" : "หลุด", color);
    show_color(screen, SLOT_SECONDS, color);
    /* chart.set_next() is a setter: a refusal is ignored (modui.c) */
    (void)bento_ui_chart_set_next(screen->ui, screen->ids[SLOT_CHART],
                                  screen->series,
                                  up ? LINE_HIGH : LINE_LOW);
    (void)snprintf(seconds, sizeof(seconds), "%ld",
                   (long)(watch->online_ms / MS_PER_SECOND));
    show_text(screen, SLOT_SECONDS, seconds);
}

/* The drawer answers "what happened", only when there is news: a
 * line every round would bury the second the link dropped. */
static void log_change(const link_screen_t *screen, bool up,
                       int32_t elapsed_ms, watch_t *watch)
{
    const int32_t now = up ? 1 : 0;
    const long seconds = (long)(elapsed_ms / MS_PER_SECOND);
    char text[TEXT_BYTES];

    if (now != watch->last) {
        watch->last = now;
        if (up) {
            ip_text_t ip;

            read_ip(&ip);
            (void)snprintf(text, sizeof(text),
                           "<span class=ok>%ld s  ลิงก์กลับมา  IP "
                           "%s</span>",
                           seconds, ip.text);
        } else {
            watch->drops++;
            (void)snprintf(text, sizeof(text), "%ld",
                           (long)watch->drops);
            show(screen, SLOT_DROPS, text, COL_BAD);
            (void)snprintf(text, sizeof(text),
                           "<span class=error>%ld s  ลิงก์หลุด</span>",
                           seconds);
        }
        console_line(text);
    }
}

/* The final values stay on the screen so they can be read. The
 * history badge is on the right half, so its last text is shorter
 * than its first: a longer one would run off the edge silently. */
static void tell_summary(const link_screen_t *screen,
                         const watch_t *watch)
{
    const long percent_up =
        (long)((watch->online_ms * PERCENT) / WATCH_MS);
    ip_text_t ip;
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "จบแล้ว - ต่ออยู่ %ld%% "
                   "ของเวลาที่เฝ้าดู",
                   percent_up);
    show(screen, SLOT_STATE, text,
         (watch->drops == 0) ? COL_OK : COL_WARN);
    (void)snprintf(text, sizeof(text), "หลุด %ld ครั้ง - ดูในลิ้นชัก",
                   (long)watch->drops);
    show_text(screen, SLOT_HISTORY, text);
    poll_ui(screen->ui);
    (void)bento_lcd_console(
        "<span class=muted>------------------------</span>");
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ต่ออยู่ %ld %% | หลุด %ld "
                   "ครั้ง</span>",
                   percent_up, (long)watch->drops);
    console_line(text);
    read_ip(&ip);
    (void)printf("uptime %ld %% | drops %ld | ip %s\n", percent_up,
                 (long)watch->drops, ip.text);
}

/* Change these two to the network the teacher hands out. A faculty
 * Wi-Fi that needs a login page cannot be used by the board. */
static const char WIFI_SSID[] = "bento-teamXX"; /* the team's hotspot */
static const char WIFI_PASS[] = "<รหัส Hotspot ของทีม>"; /* 8 or more */

/* One round: ask again, never trust the old answer, then sleep what
 * is left of TICK_MS. */
static bento_status_t watch_round(const link_screen_t *screen,
                                  int32_t elapsed_ms,
                                  uint32_t work_start_ms,
                                  watch_t *watch)
{
    const bool up = link_is_up();
    int32_t left_ms;
    bento_status_t status = BENTO_OK;

    if (up) {
        watch->online_ms += TICK_MS;
    }
    show_link(screen, up, watch);
    log_change(screen, up, elapsed_ms, watch);
    poll_ui(screen->ui);
    left_ms = TICK_MS - bento_time_ticks_diff(bento_time_ticks_ms(),
                                              work_start_ms);
    if (left_ms > 0) {
        status = pause_ms((uint32_t)left_ms);
    }
    return status;
}

static bento_status_t watch_link(const link_screen_t *screen,
                                 watch_t *watch)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;
    bool watching = true;

    show(screen, SLOT_STATE,
         "กำลังเฝ้าดู - ลองปิด WiFi "
         "ที่เราเตอร์ดูได้",
         COL_DIM);
    while ((status == BENTO_OK) && watching) {
        const uint32_t work_start_ms = bento_time_ticks_ms();
        const int32_t elapsed_ms =
            bento_time_ticks_diff(work_start_ms, start_ms);

        if (elapsed_ms >= WATCH_MS) {
            watching = false;
        } else {
            status =
                watch_round(screen, elapsed_ms, work_start_ms, watch);
        }
    }
    return status;
}

static bento_status_t run(const link_screen_t *screen)
{
    watch_t watch = {0, 0, UNKNOWN};
    bool joined = false;
    bento_status_t status;

    show(screen, SLOT_STATE, "กำลังต่อเน็ต จอจะนิ่งสักครู่", COL_WARN);
    poll_ui(screen->ui);
    status = join_wifi(WIFI_SSID, WIFI_PASS, &joined);
    if ((status == BENTO_OK) && !joined) {
        tell_no_join(screen);
    } else if (status == BENTO_OK) {
        tell_joined(screen);
        status = watch_link(screen, &watch);
        if (status == BENTO_OK) {
            tell_summary(screen, &watch);
        }
    } else {
        /* an error the original would raise ends the program */
    }
    return status;
}

int main(void)
{
    link_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear(); /* refused like print: ignored */
        (void)bento_lcd_console("<h2>ประวัติของลิงก์</h2>");
        (void)bento_lcd_console("<span class=muted>บรรทัดจะเพิ่มเฉพาะ"
                                "ตอนสถานะเปลี่ยน</span>");
        status = run(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
