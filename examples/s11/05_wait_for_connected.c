/*
 * 05_wait_for_connected.c -- connect() คืนค่าก่อนต่อเสร็จ
 *
 * tesaiot.connect() สั่งให้เริ่มต่อแล้วคืนค่าทันที จึงไม่ใช่สถานะสุดท้าย
 * โปรแกรมต่อ WiFi ตั้งค่าแพลตฟอร์มเป็นโหมด server TLS อ่านโหมดกลับมาตรวจ
 * แล้วจับเวลาสองช่วง: เลขซ้ายคือ ms ที่ connect() ใช้คืนค่า (แทบเป็นศูนย์)
 * เลขขวาคือ ms จนกว่า is_connected() ตอบว่าต่อแล้ว แถบเดินเข้าหาเพดาน
 * 30 วินาที ถ้าเกินเพดานก็ยอมแพ้อย่างมีสติ
 *
 * tesaiot.connect() starts the link and returns at once, so what it
 * returns is not the final state. The program joins Wi-Fi, sets the
 * platform to server-TLS mode, reads the mode back to check it, then times
 * two spans: the left number is how many ms connect() took to return
 * (almost none), the right one how many ms until is_connected() says yes.
 * A bar walks toward a 30 s ceiling; past it the program gives up on
 * purpose: every wait needs a way out.
 *
 * Twin of 05_wait_for_connected.py
 * (embedded-systems-for-aiot-developer/examples/s11/05_wait_for_connected.py),
 * from the library twin aiot-s11-05-wait-for-connected. Style: CLEAN_C.md.
 * What C changes:
 *   - tesaiot.config() builds a dict of every setting; only tls_mode is
 *     read, so only that field is asked for here.
 *   - tesaiot.config_set() and tesaiot.connect() answer True or False,
 *     which the original ignores; BENTO_OK and BENTO_E_FAILED are ignored
 *     the same way here. Any other status ends the program.
 *   - wifi.connect() prints console lines of its own. Here the program
 *     prints "[WiFi] Connecting..." and "[WiFi] Connected! IP=..."; the
 *     radio's per-attempt lines are not reported by the C API. The
 *     password is saved on a join with BENTO_WIFI_F_SAVE_CREDS.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_tesaiot.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 20,
    INNER_X = 40,
    TITLE_Y = 12,
    CARD_Y = 52,
    CARD_W = 652,
    CARD_H = 120,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    CAPTION_Y = 62,
    SEG_Y = 88,
    SEG_W = 180,
    SEG_H = 68,
    CONNECTED_X = 360,
    WAIT_Y = 186,
    BAR_Y = 212,
    BAR_H = 28,
    GAP_Y = 252,
    GAP_NOTE_X = 252,
    STEP_Y = 288,
    STATUS_Y = 336,
    TITLE_PX = 24,
    TEXT_PX = 20
};

/* Timing and sizes. */
enum {
    SETTLE_MS = 200,       /* let the cleared screen settle */
    WAIT_LIMIT_MS = 30000, /* wait this long at most, then give up */
    POLL_MS = 100,         /* often enough for the bar to move */
    WAIT_SLICE_MS = 50,    /* the Wi-Fi join is asked this often */
    MODE_BYTES = 128,      /* a config value is up to 127 bytes */
    TEXT_BYTES = 160,
    /* A line with the mode in it: up to 100 bytes of words and a
     * mode of up to 127. The label and the drawer cut it to what
     * they carry, as they do for the original. */
    MODE_LINE_BYTES = 100 + MODE_BYTES
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t seg_call;
    bento_ui_id_t seg_connected;
    bento_ui_id_t wait;
    bento_ui_id_t bar;
    bento_ui_id_t gap;
    bento_ui_id_t step;
    bento_ui_id_t status;
} link_screen_t;

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

/* Where a label goes and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
} look_t;

static const char WIFI_SSID[] = "AIoT-Class";
static const char WIFI_PASS[] = "<รหัสผ่านของห้องเรียน>";
static const char DEVICE_ID[] = "team03";
static const char API_KEY[] = "<api key ของทีม>";
static const char MQTT_PASS[] = "<รหัสผ่าน MQTT ของทีม>";
static const char BROKER[] = "<ชื่อโฮสต์ของแพลตฟอร์ม>";

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* wifi.ip(): the board's address, "0.0.0.0" when it has none. */
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

/* tesaiot.config_set(): its True or False is ignored, as in the
 * original; any other status means no TESAIoT service. */
static bento_status_t set_field(const char *key, const char *value)
{
    bento_status_t status = bento_tesaiot_config_set(
        key, (uint32_t)strlen(key), value, (uint32_t)strlen(value));

    return (status == BENTO_E_FAILED) ? BENTO_OK : status;
}

/* Identity and destination first: config_set only stores values,
 * nothing connects yet. Then the mode is set and read back. */
static bento_status_t configure_platform(char *mode, uint32_t cap)
{
    uint32_t length = 0U;
    bento_status_t status = set_field("device_id", DEVICE_ID);

    if (status == BENTO_OK) {
        status = set_field("api_key", API_KEY);
    }
    if (status == BENTO_OK) {
        status = set_field("mqtt_pass", MQTT_PASS);
    }
    if (status == BENTO_OK) {
        status = set_field("broker", BROKER);
    }
    /* the server picks its certificate by this name, sent before
     * encryption: unless it is the broker's, the link fails with no
     * message at all */
    if (status == BENTO_OK) {
        status = set_field("sni_hostname", BROKER);
    }
    if (status == BENTO_OK) {
        status = set_field("tls_mode", "server_tls");
    }
    if (status == BENTO_OK) {
        status = bento_tesaiot_config_get_field(
            "tls_mode", (uint32_t)strlen("tls_mode"), 0U, mode, cap,
            &length);
    }
    return status;
}

/* tesaiot.is_connected(): a refused question counts as "no". */
static bool link_is_up(void)
{
    bool connected = false;

    if (bento_tesaiot_is_connected(&connected) != BENTO_OK) {
        connected = false;
    }
    return connected;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                look_t look, int32_t font_px,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_seg(bento_ui_t *ui, int16_t x, uint32_t color,
                              bento_ui_id_t *out_id)
{
    static const char dashes[] = "----";
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = dashes;
    args.text_len = (uint32_t)strlen(dashes);
    args.x = x;
    args.y = SEG_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = color;
    return bento_ui_seg7_create(ui, &args, out_id);
}

/* The title and the card with the two numbers. */
static bento_status_t add_numbers(link_screen_t *screen)
{
    const look_t title = {MARGIN_X, TITLE_Y, COLOR_TEXT};
    const look_t call = {INNER_X, CAPTION_Y, COLOR_DIM};
    const look_t linked = {CONNECTED_X, CAPTION_Y, COLOR_DIM};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "connect() คืนค่าก่อนต่อเสร็จ", title,
                  TITLE_PX, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = MARGIN_X;
        args.y = CARD_Y;
        args.w = CARD_W;
        args.h = CARD_H;
        args.color = COLOR_CARD;
        args.min_val = COLOR_DIM; /* the border colour */
        args.max_val = CARD_RADIUS;
        args.font_px = CARD_BORDER_PX;
        status = bento_ui_panel_create(screen->ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "connect() คืนค่าใน (ms)", call,
                           TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_seg(screen->ui, INNER_X, COLOR_INFO, &screen->seg_call);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ต่อสำเร็จจริงที่ (ms)", linked,
                           TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg(screen->ui, CONNECTED_X, COLOR_OK,
                         &screen->seg_connected);
    }
    return status;
}

/* The ceiling line and the bar that walks toward it. */
static bento_status_t add_wait_bar(link_screen_t *screen)
{
    const look_t wait = {MARGIN_X, WAIT_Y, COLOR_DIM};
    char text[TEXT_BYTES];
    bento_ui_widget_args_t args;
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "เพดานเวลา %d ms",
                   WAIT_LIMIT_MS);
    status = add_label(screen->ui, text, wait, TEXT_PX, &screen->wait);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = MARGIN_X;
        args.y = BAR_Y;
        args.w = CARD_W;
        args.h = BAR_H;
        args.min_val = 0;
        args.max_val = WAIT_LIMIT_MS;
        args.font_px = 0;
        status = bento_ui_bar_create(screen->ui, &args, &screen->bar);
    }
    return status;
}

/* ui.poll() whose events are not wanted: they are dropped. */
static void drop_events(const link_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* a failed poll answers no events in MicroPython too */
    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
}

/* Text, then colour, the order the original sets them in; widget
 * setters ignore a refusal in MicroPython (modui.c). */
static void show(const link_screen_t *screen, bento_ui_id_t label,
                 const char *text, uint32_t color)
{
    (void)bento_ui_set_text(screen->ui, label, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_color(screen->ui, label, color);
}

static void set_text(const link_screen_t *screen, bento_ui_id_t label,
                     const char *text)
{
    (void)bento_ui_set_text(screen->ui, label, text,
                            (uint32_t)strlen(text));
}

/* TLS runs on TCP, which runs on IP: without Wi-Fi it ends here. */
static void show_no_wifi(const link_screen_t *screen)
{
    show(screen, screen->step, "ต่อ WiFi ไม่ได้", COLOR_BAD);
    show(screen, screen->status,
         "TLS วิ่งบน TCP ซึ่งวิ่งบน IP "
         "จบตรงนี้",
         COLOR_BAD);
    drop_events(screen);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(
        "<span class=error>ต่อ WiFi ไม่ได้ จบตรงนี้</span>");
}

static void show_address(const link_screen_t *screen)
{
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    char text[TEXT_BYTES];

    read_ip(ip, sizeof(ip));
    (void)snprintf(text, sizeof(text), "ได้ IP %s", ip);
    show(screen, screen->step, text, COLOR_OK);
    drop_events(screen);
    read_ip(ip, sizeof(ip));
    (void)snprintf(text, sizeof(text), "ได้ IP %s", ip);
    (void)bento_lcd_print(text);
}

/* Set, then read back at once, as practised in file 03. false when
 * the platform did not take server TLS. */
static bool check_mode(const link_screen_t *screen, const char *mode)
{
    const bool matches = strcmp(mode, "serverTLS") == 0;
    char text[MODE_LINE_BYTES];

    if (!matches) {
        (void)snprintf(text, sizeof(text),
                       "โหมดไม่ใช่ที่ขอไว้ ได้ %s แทน หยุดก่อน", mode);
        show(screen, screen->status, text, COLOR_BAD);
        drop_events(screen);
        (void)snprintf(text, sizeof(text),
                       "<span class=error>โหมดไม่ตรง ได้ %s </span>",
                       mode);
        (void)bento_lcd_print(text);
    } else {
        (void)snprintf(text, sizeof(text),
                       "โหมดตรวจแล้ว: %s -> พอร์ต 8884", mode);
        show(screen, screen->status, text, COLOR_INFO);
        drop_events(screen);
        (void)snprintf(text, sizeof(text),
                       "โหมดตรวจแล้ว: %s -> พอร์ต 8884", mode);
        (void)bento_lcd_print(text);
    }
    return matches;
}

/* tesaiot.connect(): its True or False is ignored, as there. The
 * time it takes to return is the first number. */
static bento_status_t start_link(const link_screen_t *screen,
                                 uint32_t start_ms, int32_t *call_ms)
{
    char text[TEXT_BYTES];
    bento_status_t status = bento_tesaiot_connect_start();

    *call_ms = bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);
    if (status == BENTO_E_FAILED) {
        status = BENTO_OK;
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%" PRId32, *call_ms);
        set_text(screen, screen->seg_call, text);
        show(screen, screen->step,
             "connect() คืนค่าแล้ว "
             "แต่ยังไม่ได้แปลว่าต่อแล้ว",
             COLOR_WARN);
        drop_events(screen);
        (void)snprintf(text, sizeof(text),
                       "connect() คืนค่าใน %" PRId32 " ms", *call_ms);
        (void)bento_lcd_print(text);
        (void)printf("connect() คืนค่าภายใน %" PRId32
                     " ms  <- ยังไม่ได้แปลว่าต่อแล้ว\n",
                     *call_ms);
    }
    return status;
}

static void show_give_up(const link_screen_t *screen)
{
    char text[TEXT_BYTES];

    show(screen, screen->step, "ยังต่อไม่สำเร็จภายในเพดานเวลา ยอมแพ้",
         COLOR_BAD);
    show(screen, screen->status,
         "ไล่ตรวจ: broker - sni_hostname - api_key - เวลาของเครื่อง",
         COLOR_BAD);
    drop_events(screen);
    (void)snprintf(text, sizeof(text),
                   "<span class=error>ยอมแพ้ที่ %d ms</span>",
                   WAIT_LIMIT_MS);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print("ไล่ตรวจ broker - sni - api_key - นาฬิกา");
}

/* Waits for is_connected(). The way out by time comes before every
 * further wait: a wait without one hangs forever the day the
 * platform is down. *gave_up says the ceiling was reached. */
static bento_status_t wait_for_link(const link_screen_t *screen,
                                    uint32_t start_ms, bool *gave_up)
{
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;

    *gave_up = false;
    while ((status == BENTO_OK) && !*gave_up && !link_is_up()) {
        const int32_t waited_ms =
            bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);

        *gave_up = waited_ms > WAIT_LIMIT_MS;
        if (*gave_up) {
            show_give_up(screen);
        } else {
            (void)bento_ui_set_value(screen->ui, screen->bar,
                                     waited_ms);
            (void)snprintf(text, sizeof(text),
                           "รออยู่ %" PRId32 " ms จากเพดาน %d ms",
                           waited_ms, WAIT_LIMIT_MS);
            set_text(screen, screen->wait, text);
            drop_events(screen);
            status = wait_ms(POLL_MS);
        }
    }
    return status;
}

/* Two numbers that differ: returned is not the same as done. */
static void show_linked(const link_screen_t *screen, uint32_t start_ms,
                        int32_t call_ms)
{
    const int32_t total_ms =
        bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);
    char text[TEXT_BYTES];

    /* the bar stays where the wait really ended, far from the top */
    (void)bento_ui_set_value(screen->ui, screen->bar, total_ms);
    (void)snprintf(text, sizeof(text), "%" PRId32, total_ms);
    set_text(screen, screen->seg_connected, text);
    (void)snprintf(text, sizeof(text),
                   "รอจริง %" PRId32 " ms จากเพดาน %d ms", total_ms,
                   WAIT_LIMIT_MS);
    set_text(screen, screen->wait, text);
    (void)snprintf(text, sizeof(text), "ต่างกัน %" PRId32 " ms",
                   total_ms - call_ms);
    show(screen, screen->gap, text, COLOR_WARN);
    show(screen, screen->step, "ต่อสำเร็จจริงแล้ว ตอนนี้ publish ได้",
         COLOR_OK);
    show(screen, screen->status,
         "เลขสองตัวไม่เท่ากัน = คืนค่าแล้ว "
         "ไม่ใช่เสร็จ",
         COLOR_OK);
    drop_events(screen);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ต่อสำเร็จจริงที่ %" PRId32 " ms</span>",
                   total_ms);
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "ต่างกัน %" PRId32 " ms คือเวลาที่ TLS จับมือ",
                   total_ms - call_ms);
    (void)bento_lcd_print(text);
    (void)printf("ต่อสำเร็จจริงที่ %" PRId32 " ms\n", total_ms);
    (void)printf("ต่างกัน %" PRId32 " ms คือเวลาที่ TLS ใช้จับมือ\n",
                 total_ms - call_ms);
}

/* Measures the two spans apart: the call, then the real link. */
static bento_status_t connect_and_time(const link_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    int32_t call_ms = 0;
    bool gave_up = false;
    bento_status_t status = start_link(screen, start_ms, &call_ms);

    if (status == BENTO_OK) {
        status = wait_for_link(screen, start_ms, &gave_up);
    }
    if ((status == BENTO_OK) && !gave_up) {
        show_linked(screen, start_ms, call_ms);
    }
    return status;
}

/* Two labels on one line keep each under ui.Label's 126 bytes (a
 * Thai letter is 3). */
static bento_status_t add_lines(link_screen_t *screen)
{
    const look_t gap = {MARGIN_X, GAP_Y, COLOR_DIM};
    const look_t gap_note = {GAP_NOTE_X, GAP_Y, COLOR_DIM};
    const look_t step = {MARGIN_X, STEP_Y, COLOR_WARN};
    const look_t status_line = {MARGIN_X, STATUS_Y, COLOR_DIM};
    char text[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_status_t status = add_label(screen->ui, "ช่องว่างระหว่างสองเลข",
                                      gap, TEXT_PX, &screen->gap);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "คือเวลาที่ TLS ใช้จับมือ", gap_note,
                           TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "กำลังต่อ WiFi %s", WIFI_SSID);
        status =
            add_label(screen->ui, text, step, TEXT_PX, &screen->step);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "รอสัญญาณ...", status_line,
                           TEXT_PX, &screen->status);
    }
    return status;
}

static bento_status_t build_screen(link_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_numbers(screen);
    }
    if (status == BENTO_OK) {
        status = add_wait_bar(screen);
    }
    if (status == BENTO_OK) {
        status = add_lines(screen);
    }
    return status;
}

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

/* Wi-Fi, the platform's settings, the mode check, then the link. */
static bento_status_t run_lesson(const link_screen_t *screen)
{
    char mode[MODE_BYTES];
    bool joined = false;
    bento_status_t status = join_wifi(&joined);

    if ((status == BENTO_OK) && !joined) {
        show_no_wifi(screen);
    }
    if ((status == BENTO_OK) && joined) {
        show_address(screen);
        status = configure_platform(mode, sizeof(mode));
        if ((status == BENTO_OK) && check_mode(screen, mode)) {
            status = connect_and_time(screen);
        }
    }
    return status;
}

int main(void)
{
    link_screen_t screen = {0};
    char text[TEXT_BYTES];
    bento_status_t status;

    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        drop_events(&screen);
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>คาบ 11 - รอให้ต่อเสร็จจริง</h2>");
        (void)snprintf(text, sizeof(text), "กำลังต่อ WiFi %s", WIFI_SSID);
        (void)bento_lcd_print(text);
        /* the text shows before the call: wifi.connect blocks long */
        status = run_lesson(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
