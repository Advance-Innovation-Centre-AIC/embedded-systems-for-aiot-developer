/*
 * 07_disconnect_frees_id.c -- บอกลาให้ถูกวิธี แล้วกลับมาใหม่
 * (Say goodbye properly, then come back)
 *
 * ต่อ WiFi และ broker ในแลนด้วย client_id ตัวหนึ่ง ส่งหนึ่งใบ
 * แล้ว mqtt.disconnect() จากนั้นต่อกลับด้วย client_id ตัวเดิมทันที
 * ถ้าขั้นที่ 4 ผ่าน แปลว่าการบอกลาไปถึง broker จริง สุดท้ายตัดอีกครั้ง
 * แล้ว publish เพื่อดูว่ามันโยน OSError ไม่ได้คืน False ไฟ MQTT
 * มุมขวาบนบอกว่าต่ออยู่ ก่อนรันแก้ WIFI_SSID WIFI_PASS BROKER
 *
 * Joins the Wi-Fi and a broker on the LAN with one client_id, sends
 * one message, calls mqtt.disconnect(), then connects again at once
 * with the same client_id. If step 4 passes, the goodbye really
 * reached the broker. Last, it disconnects again and publishes, to
 * show that publish raises OSError instead of answering False. The
 * MQTT light at the top right says whether the broker is connected.
 * Set WIFI_SSID, WIFI_PASS and BROKER first.
 *
 * Twin of 07_disconnect_frees_id.py
 * (embedded-systems-for-aiot-developer/examples/s10/07_disconnect_frees_id.py),
 * merged from the library twin aiot-s10-07-disconnect-frees-id. Style: CLEAN_C.md.
 *
 * Differs from the original:
 *   - wifi.connect() and the mqtt calls print console lines of
 *     their own. This program prints the ones C can know: "[WiFi]
 *     Connecting...", "[WiFi] Connected! IP=...", "[MQTT]
 *     Connecting to ...", "[MQTT] Connected to ... as '...'" and
 *     "[MQTT] Disconnected". No C function reports the others: each
 *     failed join attempt with the radio's code, "[WiFi] Credentials
 *     saved to QSPI", "[MQTT] Library initialized" (the first
 *     connect after a reset, which a C program cannot know) and the
 *     cy result code of a failed connect or publish (modwifi.c,
 *     modmqtt.c).
 *   - A C client is a handle, and mqtt.disconnect() frees it. A
 *     publish on the freed handle is refused with a status; that
 *     status is the original's OSError, and its text is the one the
 *     binding raises then, "MQTT not connected" (modmqtt.c:291).
 *   - The binding keeps a connection after the program ends and
 *     drops it at the next connect. A C client still open when the
 *     program ends (Stop or an error) is closed then, silently: it
 *     would otherwise hold the PSoC Edge's only MQTT slot until a
 *     reset.
 *   - Stop ends the program quietly where MicroPython raises
 *     KeyboardInterrupt.
 *   - In the BENTO Emulator, MQTT from C is not available yet: the
 *     program says so and ends.
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

/* The course palette, 0xRRGGBB. */
enum {
    COL_TEXT = 0xE8EAED,
    COL_DIM = 0x9AA3AF,
    COL_CARD = 0x171B22,
    COL_OK = 0x30A46C,
    COL_BAD = 0xE5484D,
    COL_WARN = 0xF5A623
};

/* The five steps. */
enum {
    STEP_CONNECT = 0,
    STEP_SEND = 1,
    STEP_DISCONNECT = 2,
    STEP_RECONNECT = 3,
    STEP_AFTER = 4,
    STEP_COUNT = 5
};

/* Times in milliseconds, and sizes. */
enum {
    SETTLE_MS = 200,
    PAUSE_MS = 800,
    GOODBYE_MS = 1000,
    WAIT_STEP_MS = 50, /* between two polls of a long call */
    BROKER_PORT = 1883,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 */
};

/* The widgets the program changes, and the broker client. */
/* The "round" each publish carries in its payload. */
enum {
    ROUND_FIRST = 1,
    ROUND_BACK,
    ROUND_AFTER
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t led;
    bento_ui_id_t link;
    bento_ui_id_t steps[STEP_COUNT];
    bento_ui_id_t note;
    bento_ui_id_t note_2;
    uint32_t client; /* 0: none */
} demo_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The address text of wifi.ip(): "0.0.0.0" while there is none. */
static void read_ip(char *text, uint32_t size)
{
    uint32_t ip_v4 = 0U;
    uint32_t length = 0U;

    if (bento_wifi_ip(BENTO_WIFI_IF_ANY, &ip_v4) != BENTO_OK) {
        ip_v4 = 0U;
    }
    /* a BENTO_WIFI_IPV4_TEXT_MAX buffer holds any address */
    (void)bento_wifi_ipv4_format(ip_v4, text, size, &length);
}

/* wifi.connect(): *joined is its True or False. A refused or
 * timed-out join is False; any other failure ends the program,
 * as the binding's OSError does. */
static bento_status_t join_wifi(bool *joined)
{
    /* Change these lines to the class network and the LAN broker. */
    static const char wifi_ssid[] = "AIoT-Class";
    static const char wifi_pass[] = "<รหัสผ่านของห้องเรียน>";
    bento_wifi_connect_params_t params;
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = wifi_ssid;
    params.ssid_len = (uint32_t)strlen(wifi_ssid);
    params.pass = wifi_pass;
    params.pass_len = (uint32_t)strlen(wifi_pass);
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
        read_ip(address, (uint32_t)sizeof(address));
        (void)printf("[WiFi] Connected! IP=%s\r\n", address);
    }
    return status;
}

/* mqtt.disconnect(): frees the client and always says so; it
 * answers None. */
static void close_broker(demo_t *demo)
{
    if (demo->client != 0U) {
        (void)bento_mqtt_close(demo->client); /* the client is freed */
        demo->client = 0U;
    }
    (void)printf("[MQTT] Disconnected\r\n");
}

/* mqtt.is_connected(): an unknown or freed client is offline. */
static bool broker_connected(const demo_t *demo)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;

    if (bento_mqtt_state(demo->client, &state) != BENTO_OK) {
        state = BENTO_MQTT_STATE_CLOSED;
    }
    return state == BENTO_MQTT_STATE_CONNECTED;
}

/* A refusal that is the binding's "MQTT not connected" OSError. */
static bool is_not_connected(bento_status_t status)
{
    return (status == BENTO_E_SESSION) || (status == BENTO_E_STALE) ||
           (status == BENTO_E_BAD_HANDLE);
}

/* mqtt.publish(TOPIC, json.dumps({"round": n})): *sent is its True
 * or False. A link that is gone raises in the original; here that
 * status is returned for the caller to decide. */
static bento_status_t publish_round(const demo_t *demo,
                                    int32_t round_number, bool *sent)
{
    static const char topic[] = "bento/team03/telemetry";
    char payload[TEXT_BYTES];
    bento_status_t status;

    (void)snprintf(payload, sizeof(payload), "{\"round\": %d}",
                   (int)round_number);
    status = bento_mqtt_publish(
        demo->client, topic, (uint32_t)strlen(topic),
        (const uint8_t *)payload, (uint32_t)strlen(payload), 0U, 0U);
    *sent = (status == BENTO_OK);
    if (!*sent && !is_not_connected(status)) {
        status = BENTO_OK; /* the network refused: False */
    }
    return status;
}

/* Layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 20,
    TITLE_Y = 12,
    LED_X = 606,
    LED_Y = 12,
    LED_SIZE = 18,
    LINK_X = 628,
    LINK_Y = 8,
    CARD_X = 20,
    CARD_W = 652,
    CARD_1_Y = 52,
    CARD_1_H = 192,
    CARD_2_Y = 256,
    CARD_2_H = 72,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    STEP_X = 36,
    NOTE_Y = 272,
    NOTE_2_Y = 298,
    FOOT_Y = 338,
    TITLE_PX = 24,
    TEXT_PX = 20
};

static const char *bool_text(bool value)
{
    return value ? "True" : "False";
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(const demo_t *demo, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(demo->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void set_color(const demo_t *demo, bento_ui_id_t widget,
                      uint32_t color)
{
    (void)bento_ui_set_color(demo->ui, widget, color);
}

/* ui.poll() here only empties the queue; it answers no events
 * when it fails. */
static void drain_events(const demo_t *demo)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(demo->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
}

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void lcd_line(const char *text)
{
    (void)bento_lcd_print(text);
}

static bento_status_t add_label(const demo_t *demo, const char *text,
                                const int16_t spot[2], int32_t font_px,
                                uint32_t color, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = spot[0];
    args.y = spot[1];
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(demo->ui, &args, out_id);
}

static bento_status_t add_card(const demo_t *demo, int16_t y, int16_t h)
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
    return bento_ui_panel_create(demo->ui, &args, &card);
}

/* The title, and the MQTT light: green = connected, dim = off. */
static bento_status_t add_header(demo_t *demo)
{
    const int16_t title_spot[2] = {TITLE_X, TITLE_Y};
    const int16_t link_spot[2] = {LINK_X, LINK_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t title;
    bento_status_t status =
        add_label(demo, "บอกลาให้ถูกวิธี แล้วกลับมาใหม่", title_spot, TITLE_PX,
                  COL_TEXT, &title);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = LED_X;
        args.y = LED_Y;
        args.w = LED_SIZE;
        args.h = LED_SIZE;
        args.color = COL_OK;
        args.font_px = 0;
        status = bento_ui_led_create(demo->ui, &args, &demo->led);
    }
    if (status == BENTO_OK) {
        status = add_label(demo, "MQTT: ออฟไลน์", link_spot, TEXT_PX,
                           COL_DIM, &demo->link);
    }
    return status;
}

/* The five step lines, each "waiting" until its step runs. */
static bento_status_t add_steps(demo_t *demo)
{
    static const char *const waiting[STEP_COUNT] = {
        "1) ต่อรอบแรก           รอ...", "2) ส่งหนึ่งใบ          รอ...",
        "3) disconnect()        รอ...", "4) ต่อกลับชื่อเดิม     รอ...",
        "5) publish หลังตัด     รอ..."};
    static const int16_t step_y[STEP_COUNT] = {68, 100, 136, 168, 204};
    bento_status_t status = add_card(demo, CARD_1_Y, CARD_1_H);
    int32_t step;

    for (step = 0; (step < STEP_COUNT) && (status == BENTO_OK);
         step++) {
        const int16_t spot[2] = {STEP_X, step_y[step]};

        status = add_label(demo, waiting[step], spot, TEXT_PX, COL_DIM,
                           &demo->steps[step]);
    }
    return status;
}

static bento_status_t add_notes(demo_t *demo)
{
    const int16_t note_spot[2] = {STEP_X, NOTE_Y};
    const int16_t note_2_spot[2] = {STEP_X, NOTE_2_Y};
    const int16_t foot_spot[2] = {TITLE_X, FOOT_Y};
    bento_ui_id_t foot;
    bento_status_t status = add_card(demo, CARD_2_Y, CARD_2_H);

    if (status == BENTO_OK) {
        status = add_label(demo, "กำลังเริ่ม", note_spot, TEXT_PX, COL_DIM,
                           &demo->note);
    }
    if (status == BENTO_OK) {
        status = add_label(demo, "-", note_2_spot, TEXT_PX, COL_DIM,
                           &demo->note_2);
    }
    if (status == BENTO_OK) {
        status = add_label(demo,
                           "client_id "
                           "เดิมถูกใช้ซ้ำทั้งสองรอบโดยตั้งใจ",
                           foot_spot, TEXT_PX, COL_DIM, &foot);
    }
    return status;
}

static bento_status_t build_screen(demo_t *demo)
{
    bento_status_t status =
        bento_ui_screen(demo->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_header(demo);
    }
    if (status == BENTO_OK) {
        status = add_steps(demo);
    }
    if (status == BENTO_OK) {
        status = add_notes(demo);
    }
    if (status == BENTO_OK) {
        drain_events(demo);
    }
    return status;
}

/* step(): a step line's text and colour, then a poll. */
static void show_step(const demo_t *demo, int32_t step,
                      const char *text, uint32_t color)
{
    set_text(demo, demo->steps[step], text);
    set_color(demo, demo->steps[step], color);
    drain_events(demo);
}

/* show_link(): the light and its label. */
static void show_link(const demo_t *demo, bool connected)
{
    /* a setter's refusal was ignored before too (modui.c) */
    (void)bento_ui_set_value(demo->ui, demo->led, connected ? 1 : 0);
    set_text(demo, demo->link,
             connected ? "MQTT: เชื่อมต่อแล้ว" : "MQTT: ออฟไลน์");
}

static const char DEVICE_ID[] = "team03"; /* the same both times */

/* mqtt.connect(): *linked is its True or False. A client that
 * cannot even start (no radio) ends the program, as the binding's
 * OSError does. */
static bento_status_t open_broker(demo_t *demo, bool *linked)
{
    static const char broker[] = "192.168.1.50"; /* not localhost */
    bento_mqtt_config_t config;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = broker;
    config.host_len = (uint32_t)strlen(broker);
    config.port = (uint16_t)BROKER_PORT;
    config.client_id = DEVICE_ID;
    config.client_id_len = (uint32_t)strlen(DEVICE_ID);
    (void)printf("[MQTT] Connecting to %s:%d ...\r\n", broker,
                 BROKER_PORT);
    status = bento_mqtt_open_start(&config, &demo->client);
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(WAIT_STEP_MS);
        if (status == BENTO_OK) {
            status = bento_mqtt_open_poll(demo->client);
        }
    }
    *linked = (status == BENTO_OK);
    if (*linked) {
        (void)printf("[MQTT] Connected to %s:%d as '%s'\r\n", broker,
                     BROKER_PORT, DEVICE_ID);
    } else if ((status != BENTO_E_CANCELLED) && (demo->client != 0U)) {
        /* refused or timed out: False, and the binding deletes the
         * failed client at once */
        (void)bento_mqtt_close(demo->client); /* freed either way */
        demo->client = 0U;
        status = BENTO_OK;
    } else {
        /* no client, or Stop: the status ends the program */
    }
    return status;
}

/* Stops the run with a red first step and a note. */
static void fail_first(const demo_t *demo, const char *step_text,
                       const char *note, const char *log_line)
{
    show_step(demo, STEP_CONNECT, step_text, COL_BAD);
    set_color(demo, demo->note, COL_BAD);
    set_text(demo, demo->note, note);
    lcd_line(log_line);
}

/* The BENTO Emulator's C runtime has no MQTT yet (BENTO_E_UNSUPPORTED):
 * the note line says so, and the console too. */
static void tell_emulator(const demo_t *demo)
{
    static const char note[] =
        "MQTT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";

    set_color(demo, demo->note, (uint32_t)COL_WARN);
    set_text(demo, demo->note, note);
    drain_events(demo);
    (void)printf("%s / MQTT from C is not in the BENTO Emulator yet: "
                 "run it on a Dev Kit or Eva "
                 "Kit.\n",
                 note);
}

/* Step 1: the Wi-Fi, then the broker. *ok is false when the run
 * stops here. */
static bento_status_t first_connect(demo_t *demo, bool *ok)
{
    char text[TEXT_BYTES];
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    bool joined = false;
    bento_status_t status = join_wifi(&joined);

    *ok = false;
    if ((status == BENTO_OK) && !joined) {
        fail_first(demo, "1) ต่อรอบแรก           WiFi ไม่ติด",
                   "ตรวจ WIFI_SSID กับ WIFI_PASS ที่หัวไฟล์",
                   "<span class=err>WiFi ไม่ติด</span>");
    } else if (status == BENTO_OK) {
        read_ip(address, (uint32_t)sizeof(address));
        (void)snprintf(text, sizeof(text), "WiFi ได้ IP %s", address);
        lcd_line(text);
        status = open_broker(demo, ok);
        if (status == BENTO_E_UNSUPPORTED) {
            tell_emulator(demo);
            status =
                BENTO_E_CANCELLED; /* a normal end: main returns 0 */
        }
    } else {
        /* Stop or an error ends the program */
    }
    if ((status == BENTO_OK) && joined && !*ok) {
        show_link(demo, false);
        fail_first(demo, "1) ต่อรอบแรก           ต่อ broker ไม่ได้",
                   "ตรวจ BROKER ว่าเป็น IP ของเครื่องในแลนจริง",
                   "<span class=err>ต่อ broker ไม่ได้</span>");
    }
    if ((status == BENTO_OK) && *ok) {
        show_link(demo, true);
        show_step(demo, STEP_CONNECT, "1) ต่อรอบแรก           สำเร็จ",
                  COL_OK);
        (void)snprintf(text, sizeof(text), "ต่อด้วย client_id = %s",
                       DEVICE_ID);
        set_text(demo, demo->note, text);
        lcd_line("<span class=ok>ต่อรอบแรกสำเร็จ</span>");
    }
    return status;
}

/* Step 2: one message on the first session. */
static bento_status_t send_one(const demo_t *demo)
{
    char text[TEXT_BYTES];
    bool sent = false;
    bento_status_t status = wait_ms(PAUSE_MS);

    if (status == BENTO_OK) {
        status = publish_round(demo, ROUND_FIRST, &sent);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "2) ส่งหนึ่งใบ          publish คืน %s",
                       bool_text(sent));
        show_step(demo, STEP_SEND, text,
                  sent ? (uint32_t)COL_OK : (uint32_t)COL_WARN);
        (void)snprintf(text, sizeof(text), "publish รอบแรกคืนค่า %s",
                       bool_text(sent));
        lcd_line(text);
        status = wait_ms(PAUSE_MS);
    }
    return status;
}

/* Step 3: say goodbye. disconnect() answers None, never True. */
static bento_status_t say_goodbye(demo_t *demo)
{
    char text[TEXT_BYTES];
    bool still;

    close_broker(demo);
    still = broker_connected(demo);
    show_link(demo, still); /* the light follows is_connected() */
    (void)snprintf(text, sizeof(text),
                   "3) disconnect()  คืน None is_connected %s",
                   bool_text(still));
    show_step(demo, STEP_DISCONNECT, text,
              still ? (uint32_t)COL_BAD : (uint32_t)COL_OK);
    set_text(demo, demo->note,
             "ตัดแล้ว broker รู้ทันที ไม่ต้องรอ keepalive หมด");
    (void)snprintf(text, sizeof(text),
                   "disconnect() คืน None แล้ว is_connected() = %s",
                   bool_text(still));
    lcd_line(text);
    (void)printf("disconnect() คืนค่า None (None เสมอ)\n");
    return wait_ms(GOODBYE_MS);
}

/* Step 4: the same client_id again, at once. */
static bento_status_t come_back(demo_t *demo)
{
    char text[TEXT_BYTES];
    bool back = false;
    bool sent = false;
    bento_status_t status = open_broker(demo, &back);

    if (status == BENTO_OK) {
        show_link(demo, back);
        (void)snprintf(text, sizeof(text), "4) ต่อกลับชื่อเดิม     %s",
                       back ? "สำเร็จ" : "ไม่สำเร็จ");
        show_step(demo, STEP_RECONNECT, text,
                  back ? (uint32_t)COL_OK : (uint32_t)COL_BAD);
        (void)snprintf(text, sizeof(text),
                       "ต่อกลับด้วยชื่อเดิมคืนค่า "
                       "%s",
                       bool_text(back));
        lcd_line(text);
    }
    if ((status == BENTO_OK) && back) {
        set_color(demo, demo->note, COL_OK);
        (void)snprintf(text, sizeof(text), "ชื่อ %s ว่างทันทีหลังบอกลา",
                       DEVICE_ID);
        set_text(demo, demo->note, text);
        status = publish_round(demo, ROUND_BACK,
                               &sent); /* the answer unused */
    }
    if ((status == BENTO_OK) && back) {
        set_text(demo, demo->note_2, "ส่งรอบสองสำเร็จบนเซสชันใหม่");
    }
    return status;
}

/* Step 5: after the second goodbye, publish raises OSError; it
 * does not answer False. */
static bento_status_t publish_after(demo_t *demo)
{
    const char *error = "MQTT not connected"; /* the binding's text */
    char text[TEXT_BYTES];
    bool sent = false;
    bento_status_t status;

    close_broker(demo);
    show_link(demo, false);
    status = publish_round(demo, ROUND_AFTER, &sent);
    if (status == BENTO_OK) {
        show_step(demo, STEP_AFTER,
                  "5) publish หลังตัด     ไม่โยน error "
                  "(ผิดคาด)",
                  COL_WARN);
    } else if (is_not_connected(status)) {
        show_step(demo, STEP_AFTER,
                  "5) publish หลังตัด     OSError ตามคาด", COL_OK);
        set_text(demo, demo->note_2,
                 "publish ตอนไม่ได้ต่อ = OSError ไม่ใช่ False");
        (void)snprintf(text, sizeof(text),
                       "publish หลังตัดได้ OSError ตามคาด: %s", error);
        lcd_line(text);
        (void)printf("publish หลังตัด โยน OSError: %s\n", error);
        status = BENTO_OK;
    } else {
        /* another error ends the program */
    }
    return status;
}

static void print_summary(const demo_t *demo)
{
    drain_events(demo);
    (void)printf("สรุปสามข้อ:\n");
    (void)printf("  1. disconnect() คืน None ห้ามใส่ใน if\n");
    (void)printf("  2. บอกลาแล้ว client_id ว่างทันที"
                 " ต่อใหม่ชื่อเดิมได้เลย\n");
    (void)printf("  3. publish ตอนไม่ได้ต่อ โยน OSError"
                 " ไม่ได้คืน False\n");
}

/* Steps 2 to 5, after a good first connect. */
static bento_status_t run_steps(demo_t *demo)
{
    bento_status_t status = send_one(demo);

    if (status == BENTO_OK) {
        status = say_goodbye(demo);
    }
    if (status == BENTO_OK) {
        status = come_back(demo);
    }
    if (status == BENTO_OK) {
        status = publish_after(demo);
    }
    if (status == BENTO_OK) {
        print_summary(demo);
    }
    return status;
}

int main(void)
{
    demo_t demo;
    bool ok = false;
    bento_status_t status;

    (void)memset(&demo, 0, sizeof(demo));
    demo.ui = bento_runtime_get_ui();
    status = build_screen(&demo);
    if (status == BENTO_OK) {
        /* the lcd calls ignore a console that is not ready */
        (void)bento_lcd_clear();
        (void)bento_lcd_console(
            "<h2>คาบ 10 - disconnect() คืนชื่อให้ว่าง</h2>");
        set_text(&demo, demo.note, "ต่อ WiFi ก่อน จอจะนิ่งครู่หนึ่ง");
        drain_events(&demo);
        status = first_connect(&demo, &ok);
    }
    if ((status == BENTO_OK) && ok) {
        status = run_steps(&demo);
    }
    if (demo.client != 0U) {
        /* a client left open would hold the only slot until a reset */
        (void)bento_mqtt_close(demo.client); /* nothing to report */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
