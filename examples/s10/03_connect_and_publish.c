/*
 * 03_connect_and_publish.c -- three steps, then send
 *
 * ต่อ broker แล้วส่งค่าขึ้นไปหนึ่งชุด บันไดสามขั้นที่ห้ามสลับ WiFi
 * ให้ IP ก่อน แล้ว TCP จึงต่อได้ แล้ว MQTT จึงแนะนำตัวได้ ป้ายสามขั้น
 * ไล่เปลี่ยนจากเทาเป็นเขียวตามลำดับ แล้วส่งข้อความ JSON สิบใบ ใบละสอง
 * วินาที เลขใหญ่กับแท่งความคืบหน้าเดินขึ้นทีละใบ จบแล้วบอกลา broker
 * เสมอ แม้ถูกหยุดกลางทาง
 *
 * Connects to a broker and sends a set of values. Three steps that
 * must not be swapped: Wi-Fi gives an IP first, then TCP can connect,
 * then MQTT can introduce itself. The three step labels turn from
 * grey to green in order, then ten JSON messages go out, one every
 * two seconds, while the big number and the progress bar count up.
 * The program always says goodbye to the broker, even when stopped
 * half way.
 *
 * Twin of 03_connect_and_publish.py
 * (embedded-systems-for-aiot-developer/examples/s10/03_connect_and_publish.py),
 * merged from the library twin aiot-s10-03-connect-and-publish. Style: CLEAN_C.md.
 *
 * Differs from the original:
 *  - wifi.connect() is bento_wifi_connect_start/_poll with the
 *    binding's 86 s window and BENTO_WIFI_F_SAVE_CREDS. mqtt.connect()
 *    is bento_mqtt_open_start/_poll (10 s); any failure but Stop is
 *    False, as the binding answers. The bindings' own "[WiFi] ..." and
 *    "[MQTT] ..." console lines are not printed by the C modules.
 *  - publish() raises OSError in MicroPython when the client is no
 *    longer connected and answers False when the library refuses. The
 *    C asks bento_mqtt_state() first (the binding's own check), then
 *    publishes: not connected is the "link dropped" branch, a refused
 *    publish the "rejected" one.
 *  - json.dumps: the text is built with snprintf. MicroPython's dict
 *    does not keep the written order; json.dumps walks its hash table,
 *    so the keys come out as uptime_s, id, count on the PSoC Edge
 *    (1-byte qstr hashes) and count, id, uptime_s on the reTerminal
 *    (py/map.c rules, as read_sensors.c works them out).
 *  - `raise Stop` / `except Stop: pass` is the end of the status
 *    chain with 0. Stop during a sleep or a join ends the program with
 *    0, after the broker cleanup, as the original's finally does.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_mqtt.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

enum {
    MESSAGES = 10, /* the original's N */
    BROKER_PORT = 1883,
    KEEPALIVE_S = 60,   /* silent longer: the broker may drop us */
    SEND_MS = 2000,     /* one message every two seconds */
    SETTLE_MS = 200,    /* let the cleared screen settle */
    JOIN_POLL_MS = 100, /* how often a join is checked */
    MS_PER_SECOND = 1000,
    FONT_PX = 20,
    AUTO = -1,       /* x, y, w or h the original leaves out */
    TEXT_BYTES = 160 /* longer than SET_TEXT and lcd carry */
};

/* Colours, 0xRRGGBB. */
enum {
    COL_TEXT = 0xE8EAED,
    COL_DIM = 0x9AA3AF,
    COL_CARD = 0x171B22,
    COL_TRACK = 0x171B22, /* the bar's track; the bar is the theme's */
    COL_OK = 0x30A46C,
    COL_BAD = 0xE5484D,
    COL_INFO = 0x4A9EFF
};

/* Where the program keeps the ids of the widgets it changes later. */
enum {
    SLOT_LED,
    SLOT_LINK,
    SLOT_WIFI,
    SLOT_BROKER,
    SLOT_PUBLISH,
    SLOT_SENT,
    SLOT_BAR,
    SLOT_NOTE,
    SLOT_COUNT
};
enum {
    SLOT_NONE = SLOT_COUNT /* a widget nobody changes again */
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[SLOT_COUNT + 1]; /* + the spare for SLOT_NONE */
} publish_screen_t;

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

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void console_line(const char *text)
{
    (void)bento_lcd_print(text);
}

/* ---- Step 1: Wi-Fi ---- */

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

/* ---- Step 2: the broker ---- */

/* Waits for the broker's CONNACK. */
static bento_status_t wait_for_broker(uint32_t client)
{
    bento_status_t status = BENTO_E_IN_PROGRESS;

    while (status == BENTO_E_IN_PROGRESS) {
        status = bento_time_sleep_ms(JOIN_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_mqtt_open_poll(client);
        }
    }
    return status;
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
static void show_text(const publish_screen_t *screen, uint32_t slot,
                      const char *text)
{
    (void)bento_ui_set_text(screen->ui, screen->ids[slot], text,
                            (uint32_t)strlen(text));
}

static void show_color(const publish_screen_t *screen, uint32_t slot,
                       uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, screen->ids[slot], color);
}

static void show_value(const publish_screen_t *screen, uint32_t slot,
                       int32_t value)
{
    (void)bento_ui_set_value(screen->ui, screen->ids[slot], value);
}

/* Colour first, then text, as the original sets a failed step. */
static void show_step(const publish_screen_t *screen, uint32_t slot,
                      uint32_t color, const char *text)
{
    show_color(screen, slot, color);
    show_text(screen, slot, text);
}

/* The MQTT lamp and its label always change together. */
static void show_link(const publish_screen_t *screen, bool connected)
{
    show_value(screen, SLOT_LED, connected ? 1 : 0);
    show_text(screen, SLOT_LINK,
              connected ? "MQTT: เชื่อมต่อแล้ว" : "MQTT: ออฟไลน์");
}

static bento_status_t add_widget(publish_screen_t *screen,
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

/* The whole screen comes before the first join: a step still grey is
 * a step still to come. */
static bento_status_t add_widgets(publish_screen_t *screen)
{
    static const widget_spec_t rows[] = {
        {UI_WIDGET_LABEL, SLOT_NONE, "บันไดสามขั้นก่อนส่งได้", 20, 12, AUTO,
         AUTO, COL_TEXT, 0, 0, 24},
        {UI_WIDGET_LED, SLOT_LED, NULL, 606, 12, 18, 18, COL_OK, 0, 0,
         0},
        {UI_WIDGET_LABEL, SLOT_LINK, "MQTT: ออฟไลน์", 628, 8, AUTO, AUTO,
         COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_PANEL, SLOT_NONE, NULL, 20, 52, 652, 120, COL_CARD,
         COL_DIM, 12, 1},
        {UI_WIDGET_LABEL, SLOT_WIFI, "1) WiFi        รอ...", 36, 68,
         AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_BROKER, "2) broker      รอ...", 36, 100,
         AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_PUBLISH, "3) publish     รอ...", 36, 136,
         AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_PANEL, SLOT_NONE, NULL, 20, 184, 652, 140, COL_CARD,
         COL_DIM, 12, 1},
        {UI_WIDGET_LABEL, SLOT_NONE, "ส่งไปแล้วกี่ใบ", 36, 194, AUTO, AUTO,
         COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_SEG7, SLOT_SENT, "0", 36, 224, 180, 76, COL_INFO, 0,
         0, 0},
        {UI_WIDGET_LABEL, SLOT_NONE, "ความคืบหน้าของ 10 ใบ", 240, 210,
         AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_BAR, SLOT_BAR, NULL, 240, 244, 412, 24, COL_TRACK, 0,
         MESSAGES, 0},
        {UI_WIDGET_LABEL, SLOT_NOTE, "กำลังเริ่ม...", 20, 336, AUTO, AUTO,
         COL_DIM, 0, 0, FONT_PX}};
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U;
         (i < (sizeof(rows) / sizeof(rows[0]))) && (status == BENTO_OK);
         i++) {
        status = add_widget(screen, &rows[i]);
    }
    return status;
}

static bento_status_t build_screen(publish_screen_t *screen)
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

static void tell_refused(const publish_screen_t *screen)
{
    show_link(screen, false);
    show_step(screen, SLOT_BROKER, COL_BAD, "2) broker      ปฏิเสธ");
    show_step(screen, SLOT_NOTE, COL_BAD,
              "ตรวจ IP ของ broker และพอร์ต 1883");
    poll_ui(screen->ui);
    console_line("<span class=err>ต่อ broker ไม่ได้</span>");
    console_line("ตรวจ IP ของ broker และพอร์ต 1883");
}

static void show_progress(const publish_screen_t *screen, int32_t done)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%ld", (long)done);
    show_text(screen, SLOT_SENT, text); /* a Seg7 takes text */
    show_value(screen, SLOT_BAR, done);
    poll_ui(screen->ui);
    (void)snprintf(text, sizeof(text), "ส่งครั้งที่ %ld -> ok", (long)done);
    console_line(text);
}

static void tell_summary(const publish_screen_t *screen, int32_t done)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "3) publish     ส่งแล้ว %ld ใบ",
                   (long)done);
    show_text(screen, SLOT_PUBLISH, text);
    show_color(screen, SLOT_NOTE,
               (done == MESSAGES) ? COL_OK : COL_BAD);
    (void)snprintf(text, sizeof(text), "จบรอบ ส่งได้ %ld จาก %d ใบ",
                   (long)done, MESSAGES);
    show_text(screen, SLOT_NOTE, text);
    poll_ui(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "จบรอบทดสอบ ส่งได้ %ld จาก %d "
                   "ใบ",
                   (long)done, MESSAGES);
    console_line(text);
    console_line("ดูฝั่ง MQTT Explorer ว่าครบสิบข้อความไหม");
}

/* Change these to the classroom's network, broker and team. */
static const char WIFI_SSID[] = "AIoT-Class";
static const char WIFI_PASS[] = "<รหัสผ่านของห้องเรียน>";
/* the broker's machine on the LAN, not localhost */
static const char BROKER[] = "192.168.1.50";
static const char DEVICE_ID[] = "team03";
/* the CE checks that the user equals the client id */
static const char MQTT_USER[] = "team03";
static const char MQTT_PASS[] = "<รหัสผ่าน MQTT ของทีม>";
static const char TOPIC[] = "bento/team03/telemetry";

/* The "connecting" label goes up before connect(): the screen does
 * not change while it waits. */
static bento_status_t step_wifi(const publish_screen_t *screen,
                                bool *joined)
{
    char line[TEXT_BYTES];
    ip_text_t ip;
    bento_status_t status;

    (void)snprintf(line, sizeof(line), "1) WiFi        กำลังต่อ %s",
                   WIFI_SSID);
    show_text(screen, SLOT_WIFI, line);
    poll_ui(screen->ui);
    (void)snprintf(line, sizeof(line), "1) กำลังต่อ WiFi %s", WIFI_SSID);
    console_line(line);
    status = join_wifi(WIFI_SSID, WIFI_PASS, joined);
    if ((status == BENTO_OK) && !*joined) {
        show_step(screen, SLOT_WIFI, COL_BAD,
                  "1) WiFi        "
                  "ต่อไม่ได้");
        show_step(screen, SLOT_NOTE, COL_BAD,
                  "จบตรงนี้ ตรวจชื่อวงกับรหัสผ่าน");
        poll_ui(screen->ui);
        console_line("<span class=err>ต่อ WiFi ไม่ได้ จบตรงนี้</span>");
    } else if (status == BENTO_OK) {
        read_ip(&ip);
        (void)snprintf(line, sizeof(line), "1) WiFi        IP %s",
                       ip.text);
        show_step(screen, SLOT_WIFI, COL_OK, line);
        poll_ui(screen->ui);
        read_ip(&ip);
        (void)snprintf(line, sizeof(line),
                       "<span class=ok>ได้ IP %s </span>", ip.text);
        console_line(line);
    } else {
        /* an error the original would raise ends the program */
    }
    return status;
}

/* mqtt.connect(...): *connected is its True or False. The client id
 * must be unique on the broker, or two boards keep kicking each
 * other off all lesson long. *client is 0 when nothing was opened. */
static bento_status_t open_broker(uint32_t *client, bool *connected)
{
    bento_mqtt_config_t config;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = BROKER_PORT;
    config.keepalive_s = KEEPALIVE_S;
    config.client_id = DEVICE_ID;
    config.client_id_len = (uint32_t)strlen(DEVICE_ID);
    config.user = MQTT_USER;
    config.user_len = (uint32_t)strlen(MQTT_USER);
    config.pass = MQTT_PASS;
    config.pass_len = (uint32_t)strlen(MQTT_PASS);
    status = bento_mqtt_open_start(&config, client);
    if (status == BENTO_OK) {
        status = wait_for_broker(*client);
    }
    *connected = (status == BENTO_OK);
    if ((status != BENTO_E_CANCELLED) &&
        (status != BENTO_E_UNSUPPORTED)) {
        status = BENTO_OK; /* the binding answers False to the rest */
    }
    return status;
}

static void tell_connected(const publish_screen_t *screen)
{
    char line[TEXT_BYTES];

    (void)snprintf(line, sizeof(line), "2) broker      ต่อแล้ว %s",
                   BROKER);
    show_step(screen, SLOT_BROKER, COL_OK, line);
    show_link(screen, true);
    show_step(screen, SLOT_PUBLISH, COL_OK, "3) publish     กำลังส่ง");
    show_text(screen, SLOT_NOTE, "ส่งทุก 2 วินาที ดูเลขเดินขึ้น");
    poll_ui(screen->ui);
    console_line("<span class=ok>ต่อ broker แล้ว</span>");
}

/* ---- Step 3: ten messages ---- */

/* json.dumps({"id": .., "count": .., "uptime_s": ..}) in the order
 * MicroPython's dict walks those three keys on this board. A counter
 * shows the receiver at once whether a message got lost: a number is
 * skipped. */
static void make_payload(char *text, size_t size, int32_t count)
{
    const unsigned long uptime_s =
        (unsigned long)(bento_time_ticks_ms() / MS_PER_SECOND);

#if defined(BENTO_NATIVE_H)
    (void)snprintf(text, size,
                   "{\"uptime_s\": %lu, \"id\": \"%s\", "
                   "\"count\": %ld}",
                   uptime_s, DEVICE_ID, (long)count);
#else
    (void)snprintf(text, size,
                   "{\"count\": %ld, \"id\": \"%s\", "
                   "\"uptime_s\": %lu}",
                   (long)count, DEVICE_ID, uptime_s);
#endif
}

/* One message. *sent is false when the link dropped or the publish
 * was refused; the screen and the drawer say which. */
static bento_status_t publish_one(const publish_screen_t *screen,
                                  uint32_t client, int32_t count,
                                  bool *sent)
{
    char payload[TEXT_BYTES];
    char line[TEXT_BYTES];
    uint32_t state = BENTO_MQTT_STATE_CLOSED;
    bento_status_t status = bento_mqtt_state(client, &state);

    make_payload(payload, sizeof(payload), count);
    *sent = false;
    if ((status != BENTO_OK) || (state != BENTO_MQTT_STATE_CONNECTED)) {
        (void)snprintf(line, sizeof(line),
                       "3) publish     สายหลุดที่ใบที่ "
                       "%ld",
                       (long)count);
        show_step(screen, SLOT_PUBLISH, COL_BAD, line);
        show_link(screen, false);
        poll_ui(screen->ui);
        (void)snprintf(line, sizeof(line),
                       "<span class=err>สายหลุดระหว่างส่ง "
                       "หยุดที่ครั้งที่ %ld </span>",
                       (long)count);
        console_line(line);
        status = BENTO_OK;
    } else {
        status =
            bento_mqtt_publish(client, TOPIC, (uint32_t)strlen(TOPIC),
                               (const uint8_t *)payload,
                               (uint32_t)strlen(payload), 0U, 0U);
        *sent = (status == BENTO_OK);
    }
    if ((status != BENTO_OK) && (status != BENTO_E_CANCELLED)) {
        (void)snprintf(line, sizeof(line),
                       "3) publish     ถูกปฏิเสธที่ใบที่ "
                       "%ld",
                       (long)count);
        show_step(screen, SLOT_PUBLISH, COL_BAD, line);
        poll_ui(screen->ui);
        (void)snprintf(line, sizeof(line),
                       "<span class=err>ใบที่ %ld ถูกปฏิเสธ</span>",
                       (long)count);
        console_line(line);
        status = BENTO_OK;
    }
    return status;
}

static bento_status_t publish_all(const publish_screen_t *screen,
                                  uint32_t client, int32_t *done)
{
    int32_t count;
    bool sent = true;
    bento_status_t status = BENTO_OK;

    for (count = 1; (count <= MESSAGES) && sent && (status == BENTO_OK);
         count++) {
        status = publish_one(screen, client, count, &sent);
        if ((status == BENTO_OK) && sent) {
            *done = count;
            show_progress(screen, count);
            status = pause_ms(SEND_MS);
        }
    }
    return status;
}

/* Say goodbye to the broker every time, even after Stop, so the name
 * is not left hanging there for another minute. */
static void close_broker(const publish_screen_t *screen,
                         uint32_t client)
{
    if (client != 0U) {
        /* disconnect() answers None in MicroPython: nothing to check */
        (void)bento_mqtt_close(client);
    }
    show_link(screen, false);
}

static bento_status_t step_broker(const publish_screen_t *screen)
{
    char line[TEXT_BYTES];
    uint32_t client = 0U;
    bool connected = false;
    int32_t done = 0;
    bento_status_t status;

    (void)snprintf(line, sizeof(line), "2) broker      กำลังต่อ %s",
                   BROKER);
    show_text(screen, SLOT_BROKER, line);
    poll_ui(screen->ui);
    (void)snprintf(line, sizeof(line), "2) กำลังต่อ broker %s", BROKER);
    console_line(line);
    status = open_broker(&client, &connected);
    if ((status == BENTO_OK) && !connected) {
        tell_refused(screen);
    } else if (status == BENTO_OK) {
        tell_connected(screen);
        status = publish_all(screen, client, &done);
    } else {
        /* Stop, or no MQTT on this firmware */
    }
    close_broker(screen, client);
    if ((status == BENTO_OK) && connected) {
        tell_summary(screen, done);
    }
    return status;
}

int main(void)
{
    publish_screen_t screen;
    bool joined = false;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear(); /* refused like print: ignored */
        (void)bento_lcd_console("<h2>คาบ 10 - publish ครั้งแรก</h2>");
        status = step_wifi(&screen, &joined);
    }
    if ((status == BENTO_OK) && joined) {
        status = step_broker(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
