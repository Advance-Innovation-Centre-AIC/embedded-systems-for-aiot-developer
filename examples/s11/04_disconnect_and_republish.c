/*
 * 04_disconnect_and_republish.c -- ปิดงานให้เรียบร้อย แล้วเปิดใหม่
 *
 * ต่อ WiFi ตั้งตัวตน TESAIoT ต่อแล้วรอจนต่อเสร็จจริง ส่งใบแรก ปิดด้วย
 * tesaiot.disconnect() ซึ่งคืน bool (ไม่เหมือน mqtt.disconnect() ที่คืน None)
 * แล้วต่อกลับและส่งอีกใบ ห้าขั้นบนจอไล่เปลี่ยนเป็นเขียว และ tesaiot.publish()
 * รับ payload มาก่อน topic ซึ่งสลับกับ mqtt.publish(topic, payload)
 *
 * Join Wi-Fi, set the TESAIoT identity, connect and wait until the link is
 * really up, send a first message, close with tesaiot.disconnect(), which
 * answers a bool (mqtt.disconnect() answers None), then reconnect and send
 * one more. Five steps on the screen turn green in turn, and
 * tesaiot.publish() takes the payload before the topic, the other way
 * round from mqtt.publish(topic, payload).
 *
 * Twin of 04_disconnect_and_republish.py
 * (embedded-systems-for-aiot-developer/examples/s11/04_disconnect_and_republish.py),
 * from the library twin aiot-s11-04-disconnect-and-republish.
 * Style: CLEAN_C.md. What C changes:
 *  - wifi.connect() is bento_wifi_connect_start/_poll with the binding's
 *    own window and BENTO_WIFI_F_SAVE_CREDS; the binding's "[WiFi] ..."
 *    console lines are not printed by the C module.
 *  - tesaiot.publish(payload) answers True when the message was queued:
 *    BENTO_OK here. tesaiot.connect() and config_set() answer a bool the
 *    original ignores; their BENTO_E_FAILED is ignored here too.
 *  - json.dumps({"round": 1}) is built with snprintf: the same text.
 *  - raise Stop / except Stop: pass is the end of the status chain with 0;
 *    Stop during a sleep or the join ends the program with 0.
 * In the BENTO Emulator, the TESAIoT platform from C is not available yet: the program says
 * so and ends.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_tesaiot.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200,    /* let the cleared screen settle */
    WAIT_MS = 30000,    /* the longest wait for the link */
    LINK_POLL_MS = 500, /* how often the link is asked about */
    JOIN_POLL_MS = 100, /* how often the Wi-Fi join is checked */
    AFTER_SEND_MS = 1000,
    AFTER_CLOSE_MS = 1200,
    NOT_UP = -1 /* wait_connected(): no link within the limit */
};

/* Where the Emulator note sits: over the note card, two lines at most. */
enum {
    EMU_NOTE_X = 40,
    EMU_NOTE_Y = 272,
    EMU_NOTE_W = 620
};

enum {
    FONT_PX = 20,
    AUTO = -1, /* x, y, w or h the original leaves out */
    /* The longest line: "โหมด <tls_mode> broker <broker>", two
     * config values of up to 127 bytes each and 22 bytes of words.
     * A label still shows only what SET_TEXT carries; the console
     * gets the whole text, as lcd.print() does. */
    TEXT_BYTES = 32 + (2 * (int)BENTO_TESAIOT_CONFIG_VALUE_MAX)
};

/* Colours, 0xRRGGBB. */
enum {
    COL_TEXT = 0xE8EAED,
    COL_DIM = 0x9AA3AF,
    COL_CARD = 0x171B22,
    COL_OK = 0x30A46C,
    COL_WARN = 0xF5A623,
    COL_BAD = 0xE5484D
};

/* Where the program keeps the ids of the widgets it changes later. */
enum {
    SLOT_STEP1,
    SLOT_STEP2,
    SLOT_STEP3,
    SLOT_STEP4,
    SLOT_STEP5,
    SLOT_NOTE,
    SLOT_NOTE2,
    SLOT_COUNT
};
enum {
    SLOT_NONE = SLOT_COUNT /* a widget nobody changes again */
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[SLOT_COUNT + 1]; /* + the spare for SLOT_NONE */
} steps_screen_t;

/* One config field as text, and its NUL. */
typedef struct {
    char text[BENTO_TESAIOT_CONFIG_VALUE_MAX + 1U];
} field_text_t;

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

static const char *python_bool(bool value)
{
    return value ? "True" : "False";
}

/* ---- Step 1: Wi-Fi and the identity ---- */

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

/* config_set() answers a bool the original never reads. */
static bento_status_t set_field(const char *key, const char *value)
{
    bento_status_t status = bento_tesaiot_config_set(
        key, (uint32_t)strlen(key), value, (uint32_t)strlen(value));

    if ((status == BENTO_E_FAILED) ||
        (status == BENTO_E_INVALID_ARGUMENT)) {
        status = BENTO_OK;
    }
    return status;
}

/* One field of tesaiot.config(). */
static bento_status_t read_field(const char *key, field_text_t *field)
{
    uint32_t length = 0U;

    return bento_tesaiot_config_get_field(key, (uint32_t)strlen(key),
                                          0U, field->text,
                                          sizeof(field->text), &length);
}

/* ---- Steps 2 to 5: the link ---- */

static bool link_is_up(void)
{
    bool up = false;

    if (bento_tesaiot_is_connected(&up) != BENTO_OK) {
        up = false;
    }
    return up;
}

/* tesaiot.publish(payload): the payload comes first; with no topic
 * the firmware builds one from device_id. True when queued. */
static bool publish_round(int32_t round)
{
    char payload[TEXT_BYTES];
    bento_status_t status;

    (void)snprintf(payload, sizeof(payload), "{\"round\": %ld}",
                   (long)round);
    status = bento_tesaiot_publish(NULL, 0U, (const uint8_t *)payload,
                                   (uint32_t)strlen(payload), 0U, NULL);
    return status == BENTO_OK;
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
static void show_text(const steps_screen_t *screen, uint32_t slot,
                      const char *text)
{
    (void)bento_ui_set_text(screen->ui, screen->ids[slot], text,
                            (uint32_t)strlen(text));
}

static void show_color(const steps_screen_t *screen, uint32_t slot,
                       uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, screen->ids[slot], color);
}

/* The original's step(): text, colour, and onto the screen now. */
static void step(const steps_screen_t *screen, uint32_t slot,
                 const char *text, uint32_t color)
{
    show_text(screen, slot, text);
    show_color(screen, slot, color);
    poll_ui(screen->ui);
}

/* A note: colour first, then text, as the original sets them. */
static void note(const steps_screen_t *screen, uint32_t slot,
                 uint32_t color, const char *text)
{
    show_color(screen, slot, color);
    show_text(screen, slot, text);
}

static bento_status_t add_widget(steps_screen_t *screen,
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

static bento_status_t add_widgets(steps_screen_t *screen)
{
    static const widget_spec_t rows[] = {
        {UI_WIDGET_LABEL, SLOT_NONE, "ปิดงานให้เรียบร้อย แล้วเปิดใหม่", 20, 12,
         AUTO, AUTO, COL_TEXT, 0, 0, 24},
        {UI_WIDGET_PANEL, SLOT_NONE, NULL, 20, 52, 652, 192, COL_CARD,
         COL_DIM, 12, 1},
        {UI_WIDGET_LABEL, SLOT_STEP1, "1) WiFi + ตั้งตัวตน    รอ...", 40,
         68, AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_STEP2, "2) connect + รอจริง    รอ...", 40,
         100, AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_STEP3, "3) publish รอบแรก      รอ...",
         40, 136, AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_STEP4, "4) disconnect()        รอ...",
         40, 168, AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_STEP5, "5) ต่อกลับ + ส่งอีกใบ  รอ...", 40,
         204, AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_PANEL, SLOT_NONE, NULL, 20, 256, 652, 72, COL_CARD,
         COL_DIM, 12, 1},
        {UI_WIDGET_LABEL, SLOT_NOTE, "กำลังเริ่ม", 40, 272, AUTO, AUTO,
         COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_NOTE2, "-", 40, 298, AUTO, AUTO, COL_DIM,
         0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_NONE,
         "tesaiot.publish รับ payload ก่อน topic", 20, 338, AUTO, AUTO,
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

static bento_status_t build_screen(steps_screen_t *screen)
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

static bento_status_t tell_identity(const steps_screen_t *screen)
{
    field_text_t mode;
    field_text_t broker;
    ip_text_t ip;
    char line[TEXT_BYTES];
    bento_status_t status = read_field("tls_mode", &mode);

    if (status == BENTO_OK) {
        status = read_field("broker", &broker);
    }
    if (status == BENTO_OK) {
        read_ip(&ip);
        (void)snprintf(line, sizeof(line),
                       "1) WiFi + ตั้งตัวตน    IP "
                       "%s",
                       ip.text);
        step(screen, SLOT_STEP1, line, COL_OK);
        (void)snprintf(line, sizeof(line), "โหมด %s broker %s",
                       mode.text, broker.text);
        show_text(screen, SLOT_NOTE, line);
        (void)snprintf(line, sizeof(line), "ตั้งตัวตนแล้ว โหมด %s",
                       mode.text);
        console_line(line);
    }
    return status;
}

static void print_lessons(const steps_screen_t *screen)
{
    poll_ui(screen->ui);
    (void)printf("\n");
    (void)printf("สรุปสามข้อของไฟล์นี้:\n");
    (void)printf("  1. tesaiot.disconnect() คืน bool ส่วน "
                 "mqtt.disconnect() คืน None\n");
    (void)printf("  2. tesaiot.publish(payload) - payload มาก่อน "
                 "ต่างจาก mqtt.publish(topic, payload)\n");
    (void)printf("  3. connect() คืนค่าทันที ต้องวนรอ is_connected() "
                 "เองทุกครั้ง\n");
}

static bento_status_t set_identity(void)
{
    static const char device_id[] = "team03";
    static const char api_key[] = "<api key ของทีม>";
    static const char mqtt_pass[] = "<รหัสผ่าน MQTT ของทีม>";
    static const char broker[] = "mqtt.tesaiot.dev";
    bento_status_t status = set_field("device_id", device_id);

    if (status == BENTO_OK) {
        status = set_field("api_key", api_key);
    }
    if (status == BENTO_OK) {
        status = set_field("mqtt_pass", mqtt_pass);
    }
    if (status == BENTO_OK) {
        status = set_field("broker", broker);
    }
    if (status == BENTO_OK) {
        status = set_field("sni_hostname", broker);
    }
    return status;
}

/* The BENTO Emulator's C runtime has no TESAIoT link yet (every
 * bento_tesaiot_* call answers BENTO_E_UNSUPPORTED there): the note card
 * says so, the console too, and the program ends quietly. Answers
 * BENTO_E_CANCELLED, the quiet end, once the note is up. */
static bento_status_t show_emulator_note(const steps_screen_t *screen)
{
    static const char note[] = "TESAIoT จาก C ยังไม่มีใน BENTO "
                               "Emulator\nให้รันบน Dev Kit หรือ Eva Kit";
    bento_ui_widget_args_t args;
    bento_ui_id_t label;
    bento_status_t status;

    show_text(screen, SLOT_NOTE, "");
    show_text(screen, SLOT_NOTE2, "");
    bento_ui_widget_args_init(&args);
    args.text = note;
    args.text_len = (uint32_t)strlen(note);
    args.x = EMU_NOTE_X;
    args.y = EMU_NOTE_Y;
    args.w = EMU_NOTE_W;
    args.color = (uint32_t)COL_WARN;
    args.font_px = FONT_PX;
    status = bento_ui_label_create(screen->ui, &args, &label);
    poll_ui(screen->ui);
    (void)printf(
        "TESAIoT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva "
        "Kit / TESAIoT platform from C is not in the BENTO Emulator "
        "yet: run it on a Dev Kit or Eva Kit.\n");
    return (status == BENTO_OK) ? BENTO_E_CANCELLED : status;
}

/* *ready is false when Wi-Fi did not join; the screen says so. */
static bento_status_t step_identity(const steps_screen_t *screen,
                                    bool *ready)
{
    /* Change these to the classroom's network and the team's identity. */
    static const char wifi_ssid[] = "AIoT-Class";
    static const char wifi_pass[] = "<รหัสผ่านของห้องเรียน>";
    bento_status_t status;

    show_text(screen, SLOT_NOTE, "ต่อ WiFi ก่อน จอจะนิ่งครู่หนึ่ง");
    poll_ui(screen->ui);
    status = join_wifi(wifi_ssid, wifi_pass, ready);
    if ((status == BENTO_OK) && !*ready) {
        step(screen, SLOT_STEP1, "1) WiFi + ตั้งตัวตน    WiFi ไม่ติด",
             COL_BAD);
        note(screen, SLOT_NOTE, COL_BAD,
             "ตรวจ WIFI_SSID กับ WIFI_PASS ที่หัวไฟล์");
    } else if (status == BENTO_OK) {
        status = set_identity();
        if (status == BENTO_E_UNSUPPORTED) {
            status = show_emulator_note(screen);
        }
        if (status == BENTO_OK) {
            status = tell_identity(screen);
        }
    } else {
        /* an error the original would raise ends the program */
    }
    return status;
}

/* connect() returns at once; only is_connected() can tell when the
 * link is really up. Every wait needs a limit, or the day the network
 * is down the program hangs here for ever. *took_ms is the wait, or
 * NOT_UP. */
static bento_status_t wait_connected(const steps_screen_t *screen,
                                     int32_t *took_ms)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;
    bool up = link_is_up();
    bool expired = false;

    while ((status == BENTO_OK) && !up && !expired) {
        if (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) >
            WAIT_MS) {
            expired = true;
        } else {
            poll_ui(screen->ui);
            status = pause_ms(LINK_POLL_MS);
            up = (status == BENTO_OK) && link_is_up();
        }
    }
    *took_ms =
        up ? bento_time_ticks_diff(bento_time_ticks_ms(), start_ms)
           : NOT_UP;
    return status;
}

/* tesaiot.connect() then the wait. Its own True/False is not read in
 * the original, so a refusal is left to the wait to find. */
static bento_status_t connect_and_wait(const steps_screen_t *screen,
                                       int32_t *took_ms)
{
    bento_status_t status = bento_tesaiot_connect_start();

    if (status == BENTO_E_FAILED) {
        status = BENTO_OK;
    }
    *took_ms = NOT_UP;
    if (status == BENTO_OK) {
        status = wait_connected(screen, took_ms);
    }
    return status;
}

/* *up is false when the link did not come up in time. */
static bento_status_t step_connect(const steps_screen_t *screen,
                                   bool *up)
{
    char line[TEXT_BYTES];
    int32_t took_ms = NOT_UP;
    bento_status_t status;

    step(screen, SLOT_STEP2, "2) connect + รอจริง    กำลังรอ...",
         COL_WARN);
    status = connect_and_wait(screen, &took_ms);
    *up = (took_ms != NOT_UP);
    if ((status == BENTO_OK) && !*up) {
        step(screen, SLOT_STEP2,
             "2) connect + รอจริง    ไม่ติดใน 30 "
             "วินาที",
             COL_BAD);
        note(screen, SLOT_NOTE, COL_BAD,
             "ตรวจ broker sni_hostname และตัวตนของทีม");
        console_line("<span class=err>ต่อไม่ติดใน 30 วินาที</span>");
    } else if (status == BENTO_OK) {
        (void)snprintf(line, sizeof(line),
                       "2) connect + รอจริง    ต่อเสร็จใน %ld ms",
                       (long)took_ms);
        step(screen, SLOT_STEP2, line, COL_OK);
        (void)snprintf(line, sizeof(line),
                       "<span class=ok>ต่อเสร็จใน %ld ms</span>",
                       (long)took_ms);
        console_line(line);
    } else {
        /* Stop, or no tesaiot on this firmware */
    }
    return status;
}

static bento_status_t step_first_publish(const steps_screen_t *screen)
{
    const bool sent = publish_round(1);
    char line[TEXT_BYTES];

    (void)snprintf(line, sizeof(line), "3) publish รอบแรก      คืน %s",
                   python_bool(sent));
    step(screen, SLOT_STEP3, line, sent ? COL_OK : COL_WARN);
    (void)snprintf(line, sizeof(line), "publish รอบแรกคืนค่า %s",
                   python_bool(sent));
    console_line(line);
    return pause_ms(AFTER_SEND_MS);
}

/* tesaiot.disconnect() answers a bool, unlike mqtt.disconnect(). */
static bento_status_t step_disconnect(const steps_screen_t *screen)
{
    const bool closed = bento_tesaiot_disconnect() == BENTO_OK;
    const bool still = link_is_up();
    char line[TEXT_BYTES];

    (void)snprintf(line, sizeof(line),
                   "4) disconnect()  คืน %s "
                   "ต่ออยู่ %s",
                   python_bool(closed), python_bool(still));
    step(screen, SLOT_STEP4, line,
         (closed && !still) ? COL_OK : COL_BAD);
    note(screen, SLOT_NOTE, COL_DIM,
         "tesaiot.disconnect() คืน bool ส่วน mqtt คืน None");
    (void)snprintf(line, sizeof(line),
                   "disconnect() คืน %s แล้ว is_connected() = %s",
                   python_bool(closed), python_bool(still));
    console_line(line);
    (void)printf("tesaiot.disconnect() คืนค่า %s (เป็น bool ไม่ใช่ "
                 "None)\n",
                 python_bool(closed));
    return pause_ms(AFTER_CLOSE_MS);
}

static bento_status_t step_reconnect(const steps_screen_t *screen)
{
    char line[TEXT_BYTES];
    int32_t took_ms = NOT_UP;
    bento_status_t status;

    step(screen, SLOT_STEP5, "5) ต่อกลับ + ส่งอีกใบ  กำลังรอ...", COL_WARN);
    status = connect_and_wait(screen, &took_ms);
    if ((status == BENTO_OK) && (took_ms == NOT_UP)) {
        step(screen, SLOT_STEP5, "5) ต่อกลับ + ส่งอีกใบ  ต่อกลับไม่ติด",
             COL_BAD);
        note(screen, SLOT_NOTE2, COL_BAD,
             "ปิดแล้วเปิดใหม่ต้องเว้นจังหวะให้ broker ตามทัน");
    } else if (status == BENTO_OK) {
        const bool sent = publish_round(2);

        (void)snprintf(line, sizeof(line),
                       "5) ต่อกลับ + ส่งอีกใบ  %ld ms publish %s",
                       (long)took_ms, python_bool(sent));
        step(screen, SLOT_STEP5, line, sent ? COL_OK : COL_WARN);
        note(screen, SLOT_NOTE2, COL_OK,
             "ปิดแล้วเปิดใหม่ได้ "
             "ไม่ต้องรีเซ็ตบอร์ด");
        (void)snprintf(line, sizeof(line),
                       "<span class=ok>ต่อกลับใน %ld ms "
                       "และส่งได้อีกใบ</span>",
                       (long)took_ms);
        console_line(line);
    } else {
        /* Stop, or no tesaiot on this firmware */
    }
    return status;
}

static bento_status_t run(const steps_screen_t *screen)
{
    bool ready = false;
    bool up = false;
    bento_status_t status = step_identity(screen, &ready);

    if ((status == BENTO_OK) && ready) {
        status = step_connect(screen, &up);
    }
    if ((status == BENTO_OK) && ready && up) {
        status = step_first_publish(screen);
        if (status == BENTO_OK) {
            status = step_disconnect(screen);
        }
        if (status == BENTO_OK) {
            status = step_reconnect(screen);
        }
        if (status == BENTO_OK) {
            print_lessons(screen);
        }
    }
    return status;
}

int main(void)
{
    steps_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear(); /* refused like print: ignored */
        (void)bento_lcd_console("<h2>คาบ 11 - ปิดแล้วเปิดใหม่</h2>");
        status = run(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
