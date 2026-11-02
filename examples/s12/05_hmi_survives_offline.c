/*
 * 05_hmi_survives_offline.c -- เน็ตหลุดแล้วจอต้องยังทำงาน
 *
 * HMI ที่แยกลูปของจอออกจากลูปของเครือข่าย ลากสไลเดอร์แล้ว Seg7 เปลี่ยนตามทันที
 * ไม่ต้องรอเน็ต ทุก 3 วินาทีส่งค่าเป็น JSON ไปที่ broker กราฟมีสองเส้น ฟ้าคือค่าบนจอ
 * เขียวคือค่าที่ส่งออกไปได้จริง ถอดปลั๊กเราเตอร์แล้วเส้นเขียวตกลงพื้น ป้าย net
 * เป็นสีแดง และตัวนับ "ส่งไม่ออก" ไต่ขึ้น ส่วนจอยังใช้งานได้ตามปกติ
 *
 * An HMI whose screen loop is kept apart from its network loop. Drag the
 * slider and the Seg7 follows at once, without waiting for the network.
 * Every 3 seconds the value goes to the broker as JSON. The chart has two
 * lines: blue is the value on the screen, green the value that really got
 * out. Unplug the router: the green line drops to the floor, the net label
 * turns red and the "not sent" counter climbs, while the screen keeps
 * working.
 *
 * Twin of 05_hmi_survives_offline.py
 * (embedded-systems-for-aiot-developer/examples/s12/05_hmi_survives_offline.py),
 * from the library twin aiot-s12-05-hmi-survives-offline (same code, the
 * course file only writes a few lines shorter). Style: CLEAN_C.md.
 * What C changes:
 *  - json.dumps({"id": DEVICE_ID, "v": value}) is snprintf, with the
 *    separators ", " and ": " that json.dumps writes;
 *  - wifi.connect() and mqtt.connect() block in MicroPython; here they are
 *    started and polled every 50 ms, and a Stop cancels the wait. The
 *    binding's own console lines ("[WiFi] ...", "[MQTT] ...") are not
 *    printed: the C libraries log nothing;
 *  - mqtt.publish() raising OSError (the link dropped while sending) is
 *    BENTO_E_SESSION here; any other failed send is the binding's False;
 *  - the finally (mqtt.disconnect(), the lamp off) runs after the loop,
 *    whatever ended it. A board without Wi-Fi (BENTO_E_UNSUPPORTED) ends
 *    the program after the finally, where MicroPython has no wifi module.
 * In the BENTO Emulator, MQTT from C is not available yet: the program says
 * so and goes on offline, as the original does when the broker cannot be
 * reached.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_mqtt.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
static const char WIFI_SSID[] = "AIoT-Class";
static const char WIFI_PASS[] = "<รหัสผ่านของห้องเรียน>";
static const char BROKER[] = "192.168.1.50";
static const char DEVICE_ID[] = "team03";
static const char TOPIC[] = "bento/team03/telemetry";
/* ---- end of settings ---- */

enum { /* times in milliseconds, and values */
    SETTLE_MS = 200,
    WAIT_STEP_MS = 50, /* how often a join is polled */
    SEND_EVERY_MS = 3000,
    CHART_MS =
        300, /* the chart follows the screen loop, not the sends */
    LOOP_MS = 100,
    START_VALUE = 25,
    FULL_SCALE = 100,
    MQTT_PORT = 1883,
    CLIENT_SUFFIX_MASK = 0xFFFF, /* the low 16 bits of the clock */
    CLIENT_ID_CHARS = 24,        /* "team03-" and four hex digits */
    TEXT_CHARS = 128 /* a SET_TEXT carries at most 126 bytes */
};
enum { /* layout sizes and fonts */
    AUTO_PX = -1,
    TITLE_PX = 24,
    TEXT_PX = 20,      /* Thai below 20 px loses its strokes */
    CARD_EDGE_PX = 12, /* max= of the card panel */
    CARD_BORDER = 1,   /* value= of the card panel */
    NOTE_X = 20,       /* the Emulator note, under the chart legend */
    NOTE_Y = 372,
    NOTE_W = 760
};
enum { /* the course palette, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF,
    COLOR_WARN =
        0xF5A623 /* the course palette's warning, for the Emulator note */
};
enum { /* the widgets the program changes; ROLE_FIXED holds the rest */
    ROLE_FIXED,
    ROLE_LED,
    ROLE_BROKER_TEXT,
    ROLE_DIGITS,
    ROLE_SLIDER,
    ROLE_NET,
    ROLE_MISSED,
    ROLE_CHART,
    ROLE_COUNT
};
enum { /* the widget kinds of the layout tables */
    WIDGET_LABEL,
    WIDGET_LED,
    WIDGET_PANEL,
    WIDGET_SEG7,
    WIDGET_SLIDER,
    WIDGET_CHART,
    WIDGET_KIND_COUNT
};

typedef struct {
    int32_t kind;
    int32_t role;
    const char *text; /* NULL: the widget has none */
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t min_value;
    int32_t max_value;
    int32_t value; /* value=: a start value or a font size */
} widget_spec_t;

/* The broker link: the binding keeps one client. */
typedef struct {
    bool open;
    uint32_t handle;
} broker_link_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t widgets[ROLE_COUNT];
    uint8_t local_line; /* the chart's line of the screen value */
    uint8_t sent_line;  /* the chart's line of the value sent */
    char client_id[CLIENT_ID_CHARS];
    broker_link_t link;
} hmi_t;

/* What the loop keeps between rounds. */
typedef struct {
    bool online;
    int32_t value;
    int32_t missed;
    int32_t value_at_broker; /* 0 when the last send did not get out */
    uint32_t sent_ms;
    uint32_t charted_ms;
} telemetry_t;

typedef bento_ui_status_t (*widget_factory_t)(
    bento_ui_t *ui, const bento_ui_widget_args_t *args,
    bento_ui_id_t *out_id);

static bento_status_t stop_raised_in(bento_status_t status)
{
    const bool stopped = !bento_runtime_is_running();

    return ((status == BENTO_OK) && stopped) ? BENTO_E_CANCELLED
                                             : status;
}

static bento_status_t wait_step(void)
{
    return stop_raised_in(bento_time_sleep_ms((uint32_t)WAIT_STEP_MS));
}

/* The widget setters: MicroPython's ignore a refusal (modui.c). */
static void show_text(const hmi_t *hmi, int32_t role, const char *text)
{
    (void)bento_ui_set_text(hmi->ui, hmi->widgets[role], text,
                            (uint32_t)strlen(text));
}

static void show_color(const hmi_t *hmi, int32_t role, uint32_t color)
{
    (void)bento_ui_set_color(hmi->ui, hmi->widgets[role], color);
}

static void show_value(const hmi_t *hmi, int32_t role, int32_t value)
{
    (void)bento_ui_set_value(hmi->ui, hmi->widgets[role], value);
}

static bento_status_t
add_widgets(hmi_t *hmi, const widget_spec_t *specs, size_t count)
{
    static const widget_factory_t factories[WIDGET_KIND_COUNT] = {
        bento_ui_label_create,  bento_ui_led_create,
        bento_ui_panel_create,  bento_ui_seg7_create,
        bento_ui_slider_create, bento_ui_chart_create};
    bento_ui_widget_args_t args;
    size_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        const widget_spec_t *spec = &specs[i];

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
        args.min_val = spec->min_value;
        args.max_val = spec->max_value;
        args.font_px = spec->value;
        status = stop_raised_in(factories[spec->kind](
            hmi->ui, &args, &hmi->widgets[spec->role]));
    }
    return status;
}

/* The title, the MQTT lamp (green = on the broker, dim = offline) and the
 * card with the value. The Seg7 takes text: .value() takes integers only. */
static bento_status_t build_card(hmi_t *hmi)
{
    static const widget_spec_t layout[] = {
        {WIDGET_LABEL, ROLE_FIXED, "คาบ 12 - จอต้องอยู่ได้เมื่อเน็ตหลุด", 20, 8,
         AUTO_PX, AUTO_PX, COLOR_TEXT, 0, 0, TITLE_PX},
        {WIDGET_LED, ROLE_LED, NULL, 606, 12, 18, 18, COLOR_OK, 0, 0,
         0},
        {WIDGET_LABEL, ROLE_BROKER_TEXT, "MQTT: ออฟไลน์", 628, 8,
         AUTO_PX, AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_PANEL, ROLE_FIXED, NULL, 20, 44, 652, 128, COLOR_CARD,
         COLOR_DIM, CARD_EDGE_PX, CARD_BORDER},
        {WIDGET_LABEL, ROLE_FIXED, "ค่าที่กำลังเฝ้าดู", 40, 50, AUTO_PX,
         AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_SEG7, ROLE_DIGITS, "25", 40, 76, 172, 52, COLOR_TEXT, 0,
         0, 0},
        {WIDGET_LABEL, ROLE_FIXED, "ลากเพื่อเปลี่ยนค่า", 232, 50, AUTO_PX,
         AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_SLIDER, ROLE_SLIDER, NULL, 232, 80, 420, 28, 0U, 0,
         FULL_SCALE, START_VALUE},
        {WIDGET_LABEL, ROLE_NET, "net: -", 660, 128, AUTO_PX, AUTO_PX,
         COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_LABEL, ROLE_MISSED, "ส่งไม่ออก: 0", 660, 164, AUTO_PX,
         AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX}};

    return add_widgets(hmi, layout, sizeof(layout) / sizeof(layout[0]));
}

/* Two lines on one chart: what the screen shows and what got out are
 * different things, and must be seen to be. */
static bento_status_t build_chart(hmi_t *hmi)
{
    static const widget_spec_t chart_spec = {
        WIDGET_CHART, ROLE_CHART, NULL, 20,         200, 652,
        140,          COLOR_CARD, 0,    FULL_SCALE, 0};
    static const widget_spec_t notes[] = {
        {WIDGET_LABEL, ROLE_FIXED, "ฟ้า = ค่าบนจอ", 20, 346, AUTO_PX,
         AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_LABEL, ROLE_FIXED, "เขียว = ค่าที่ส่งออกไปได้จริง", 180, 346,
         AUTO_PX, AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX}};
    bento_status_t status = add_widgets(hmi, &chart_spec, 1U);

    if (status == BENTO_OK) {
        status = stop_raised_in(
            bento_ui_chart_add_series(hmi->ui, hmi->widgets[ROLE_CHART],
                                      COLOR_INFO, &hmi->local_line));
    }
    if (status == BENTO_OK) {
        status = stop_raised_in(
            bento_ui_chart_add_series(hmi->ui, hmi->widgets[ROLE_CHART],
                                      COLOR_OK, &hmi->sent_line));
    }
    if (status == BENTO_OK) {
        status =
            add_widgets(hmi, notes, sizeof(notes) / sizeof(notes[0]));
    }
    return status;
}

static bento_status_t build_screen(hmi_t *hmi)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    bento_status_t status;

    /* MicroPython ignores a refused ui.screen(): the runtime already set it up */
    (void)bento_ui_screen_default(hmi->ui);
    status = stop_raised_in(bento_time_sleep_ms((uint32_t)SETTLE_MS));
    if (status == BENTO_OK) {
        status = build_card(hmi);
    }
    if (status == BENTO_OK) {
        status = build_chart(hmi);
    }
    if (status == BENTO_OK) {
        /* drops stale events; a failed poll answers none */
        (void)bento_ui_poll(hmi->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                            &count, NULL);
    }
    return status;
}

/* The lamp and its label change together. No poll here: it would eat the
 * slider events the main loop waits for. */
static void show_link(const hmi_t *hmi, bool online)
{
    show_value(hmi, ROLE_LED, online ? 1 : 0);
    show_text(hmi, ROLE_BROKER_TEXT,
              online ? "MQTT: เชื่อมต่อแล้ว" : "MQTT: ออฟไลน์");
}

/* wifi.connect(): true when joined. A board without Wi-Fi ends the program. */
static bento_status_t join_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
    bento_status_t answer = BENTO_E_IN_PROGRESS;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = WIFI_PASS;
    params.pass_len = (uint32_t)strlen(WIFI_PASS);
    params.flags = BENTO_WIFI_F_SAVE_CREDS; /* as the binding does */
    status = bento_wifi_connect_start(&params);
    while ((status == BENTO_OK) && (answer == BENTO_E_IN_PROGRESS)) {
        answer = bento_wifi_connect_poll();
        if (answer == BENTO_E_IN_PROGRESS) {
            status = wait_step();
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)
            bento_wifi_connect_cancel(); /* Stop: give up, the program ends anyway */
    }
    *joined = (status == BENTO_OK) && (answer == BENTO_OK);
    return ((status == BENTO_E_CANCELLED) ||
            (status == BENTO_E_UNSUPPORTED))
               ? status
               : BENTO_OK;
}

/* mqtt.connect(BROKER, port=1883, client_id=CLIENT_ID): false on a refusal. */
/* The BENTO Emulator's C runtime has no MQTT yet: bento_mqtt_open_start
 * answers BENTO_E_UNSUPPORTED there. A line under the chart and the
 * console say so; the HMI then goes on offline, as with a broker that
 * cannot be reached. */
static void show_emulator_note(const hmi_t *hmi)
{
    static const char note[] =
        "MQTT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";
    bento_ui_widget_args_t args;
    bento_ui_id_t id;

    bento_ui_widget_args_init(&args);
    args.text = note;
    args.text_len = (uint32_t)strlen(note);
    args.x = NOTE_X;
    args.y = NOTE_Y;
    args.w = NOTE_W;
    args.color = (uint32_t)COLOR_WARN;
    args.font_px = TEXT_PX;
    /* best effort: the HMI goes on offline whether the note shows or not */
    (void)bento_ui_label_create(hmi->ui, &args, &id);
    (void)printf("MQTT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ "
                 "Eva Kit / MQTT from C is not in the BENTO Emulator "
                 "yet: run it on a Dev Kit or Eva Kit.\n");
}

static bento_status_t connect_broker(hmi_t *hmi, bool *connected)
{
    broker_link_t *link = &hmi->link;
    bento_mqtt_config_t config;
    bento_status_t answer = BENTO_E_IN_PROGRESS;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = MQTT_PORT;
    config.client_id = hmi->client_id;
    config.client_id_len = (uint32_t)strlen(hmi->client_id);
    status = bento_mqtt_open_start(&config, &link->handle);
    link->open = status == BENTO_OK;
    if (status == BENTO_E_UNSUPPORTED) {
        show_emulator_note(hmi);
    }
    while ((status == BENTO_OK) && (answer == BENTO_E_IN_PROGRESS)) {
        answer = bento_mqtt_open_poll(link->handle);
        if (answer == BENTO_E_IN_PROGRESS) {
            status = wait_step();
        }
    }
    *connected = (status == BENTO_OK) && (answer == BENTO_OK);
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* The screen is ready first; then what may fail. */
static bento_status_t go_online(hmi_t *hmi, bool *online)
{
    bool joined = false;
    bento_status_t status = join_wifi(&joined);

    *online = false;
    if ((status == BENTO_OK) && joined) {
        status = connect_broker(hmi, online);
    }
    return status;
}

/* mqtt.is_connected(): whether the broker still holds the link. */
static bool broker_connected(const hmi_t *hmi)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;
    const bool answered =
        hmi->link.open &&
        (bento_mqtt_state(hmi->link.handle, &state) == BENTO_OK);

    return answered && (state == BENTO_MQTT_STATE_CONNECTED);
}

/* The screen loop: the slider moves the Seg7 at once. Events are picked by
 * widget and kind, so a second widget cannot mix in. */
static void follow_slider(const hmi_t *hmi, telemetry_t *telemetry)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    char text[TEXT_CHARS];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(hmi->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U; /* a failed poll answers no events */
    }
    for (i = 0U; i < count; i++) {
        if ((bento_ui_id_to_handle(events[i].id) ==
             bento_ui_id_to_handle(hmi->widgets[ROLE_SLIDER])) &&
            (events[i].type == (uint8_t)BENTO_UI_EVENT_VALUE_CHANGED)) {
            telemetry->value = events[i].value;
            (void)snprintf(text, sizeof(text), "%d",
                           (int)telemetry->value);
            show_text(hmi, ROLE_DIGITS, text);
        }
    }
}

/* One send: a value that does not get out is dropped, not kept back, since
 * an old value is of no use. A link lost while sending is never fatal. */
static bool send_value(const hmi_t *hmi, telemetry_t *telemetry)
{
    char body[TEXT_CHARS];
    bool sent = false;

    (void)snprintf(body, sizeof(body), "{\"id\": \"%s\", \"v\": %d}",
                   DEVICE_ID, (int)telemetry->value);
    if (telemetry->online) {
        const bento_status_t status = bento_mqtt_publish(
            hmi->link.handle, TOPIC, (uint32_t)strlen(TOPIC),
            (const uint8_t *)body, (uint32_t)strlen(body), 0U, 0U);

        sent = status == BENTO_OK;
        telemetry->online = status != BENTO_E_SESSION;
    }
    return sent;
}

/* The network loop's report: net label, lamp, missed counter. */
static void show_report(const hmi_t *hmi, const telemetry_t *telemetry)
{
    char text[TEXT_CHARS];

    show_color(hmi, ROLE_NET,
               telemetry->online ? (uint32_t)COLOR_OK
                                 : (uint32_t)COLOR_BAD);
    show_text(hmi, ROLE_NET,
              telemetry->online ? "net: online" : "net: offline");
    show_link(hmi, telemetry->online);
    show_color(hmi, ROLE_MISSED,
               (telemetry->missed == 0) ? (uint32_t)COLOR_DIM
                                        : (uint32_t)COLOR_BAD);
    (void)snprintf(text, sizeof(text), "ส่งไม่ออก: %d",
                   (int)telemetry->missed);
    show_text(hmi, ROLE_MISSED, text);
}

/* Every SEND_EVERY_MS: ask the link, send, report. */
static void send_when_due(const hmi_t *hmi, telemetry_t *telemetry,
                          uint32_t now_ms)
{
    bool sent;

    if (bento_time_ticks_diff(now_ms, telemetry->sent_ms) >=
        SEND_EVERY_MS) {
        telemetry->sent_ms = now_ms;
        telemetry->online = broker_connected(hmi);
        sent = send_value(hmi, telemetry);
        if (!sent) {
            telemetry->missed++;
        }
        telemetry->value_at_broker = sent ? telemetry->value : 0;
        show_report(hmi, telemetry);
    }
}

/* The chart follows the screen loop, not the sends: the blue line moves as
 * soon as the slider does, the green one every send at most. The gap
 * between them is the time the far end did not know what happened. */
static void chart_when_due(const hmi_t *hmi, telemetry_t *telemetry,
                           uint32_t now_ms)
{
    if (bento_time_ticks_diff(now_ms, telemetry->charted_ms) >=
        CHART_MS) {
        telemetry->charted_ms = now_ms;
        /* set_next() sends without an answer (modui.c) */
        (void)bento_ui_chart_set_next(hmi->ui, hmi->widgets[ROLE_CHART],
                                      hmi->local_line,
                                      telemetry->value);
        (void)bento_ui_chart_set_next(hmi->ui, hmi->widgets[ROLE_CHART],
                                      hmi->sent_line,
                                      telemetry->value_at_broker);
    }
}

/* The try block: the link, then the two loops until Stop. */
static bento_status_t run_hmi(hmi_t *hmi)
{
    telemetry_t telemetry;
    bento_status_t status;

    (void)memset(&telemetry, 0, sizeof(telemetry));
    status = go_online(hmi, &telemetry.online);
    if (status == BENTO_OK) {
        show_link(hmi, telemetry.online);
        telemetry.value = START_VALUE;
        telemetry.sent_ms = bento_time_ticks_ms();
        telemetry.charted_ms = bento_time_ticks_ms();
    }
    while (status == BENTO_OK) {
        uint32_t now_ms;

        follow_slider(hmi, &telemetry);
        now_ms = bento_time_ticks_ms();
        send_when_due(hmi, &telemetry, now_ms);
        chart_when_due(hmi, &telemetry, now_ms);
        status = stop_raised_in(bento_time_sleep_ms((uint32_t)LOOP_MS));
    }
    return status;
}

int main(void)
{
    hmi_t hmi;
    bento_status_t status;

    (void)memset(&hmi, 0, sizeof(hmi));
    hmi.ui = bento_runtime_get_ui();
    /* a new suffix each run: a quick rerun never meets its old id */
    (void)snprintf(hmi.client_id, sizeof(hmi.client_id), "%s-%04x",
                   DEVICE_ID,
                   (unsigned)(bento_time_ticks_ms() &
                              (uint32_t)CLIENT_SUFFIX_MASK));
    status = build_screen(&hmi);
    if (status == BENTO_OK) {
        status = run_hmi(&hmi);
        /* the finally: leave the broker so the name is not held a minute */
        if (hmi.link.open) {
            (void)bento_mqtt_close(
                hmi.link.handle); /* disconnect answers None */
        }
        show_link(&hmi, false);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
