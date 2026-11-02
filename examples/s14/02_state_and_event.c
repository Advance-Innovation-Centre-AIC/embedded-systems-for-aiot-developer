/*
 * 02_state_and_event.c -- สถานะ + เหตุการณ์: สองหัวข้อ MQTT (state and event)
 *
 * ส่งผล AI สองแบบ แบบสถานะ (bento-aiot/<TEAM>/ai) ส่งเมื่อป้ายเปลี่ยนและทุก
 * 2 วินาที มีช่อง alarm บอกว่ากำลังเตือนไหม แบบเหตุการณ์ (bento-aiot/<TEAM>/event)
 * ส่งครั้งเดียวตอนเริ่มเตือนและตอนหายเตือน หลังกรองด้วยความมั่นใจ 60% และกฎ
 * 3 ใน 5 วงกลมบนจอเขียว = ปกติ แดง = เตือน จบโปรแกรมขณะเตือนจะส่งเหตุการณ์
 * หายเตือนให้ก่อน ยังไม่แก้ TEAM = ทำงานออฟไลน์ พิมพ์ลง Console แทน
 *
 * Sends the AI's results two ways. The state (bento-aiot/<TEAM>/ai) on
 * a label change and every 2 s, with an alarm field; the event
 * (bento-aiot/<TEAM>/event) once when the alarm starts and once when
 * it ends, after a 60 % confidence floor and a 3-of-5 rule. The disc
 * on the screen is green when normal and red in alarm. Ending during
 * an alarm sends its end event first. With TEAM not set it runs
 * offline and prints to the Console instead.
 *
 * Twin of 02_state_and_event.py
 * (embedded-systems-for-aiot-developer/examples/s14/02_state_and_event.py).
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - json.dumps is snprintf into a fixed buffer, keys in the order the
 *    dict is written (MicroPython's dict may walk them in another
 *    order); a text value escapes only quote, backslash and control
 *    bytes;
 *  - edge_ai.models() is bento_edgeai_count() and model_info() per
 *    row; conf is score_pct[top]; the label is bento_edgeai_label();
 *  - select(), stop(), wifi.connect() and mqtt.connect() wait inside
 *    the bindings; here they are started and polled. A refused join
 *    or broker is False, as the bindings answer; any other network
 *    failure prints "network:" with the C status number;
 *  - import edge_ai fails where there is no model link: here
 *    bento_edgeai_supported() false ends with BENTO_E_UNSUPPORTED;
 *  - Stop raises KeyboardInterrupt in MicroPython; here the wait
 *    returns and the program ends after the same finally (the model is
 *    stopped, an open alarm ended, the broker closed).
 * In the BENTO Emulator, Edge AI from C is not available yet: the
 * program says so, then goes on as the original does when no
 * model is on the board.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_edgeai.h"
#include "bento_mqtt.h"
#include "bento_runtime.h"
#include "bento_wifi.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
static const char WIFI_SSID[] = "<Hotspot name>";
static const char WIFI_PASS[] = "<Hotspot password>";
static const char TEAM[] = "teamXX";
/* BROKER: test.mosquitto.org is the spare */
static const char BROKER[] = "broker.hivemq.com";
/* ---- end of settings ---- */

/* The note for the BENTO Emulator, whose C runtime has no Edge AI yet. */
static const char EMULATOR_NOTE[] =
    "Edge AI จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";

/* Where that note sits on the screen. */
enum {
    EMULATOR_NOTE_X = 12,
    EMULATOR_NOTE_Y = 180,
    EMULATOR_NOTE_PX = 20,
    EMULATOR_NOTE_COLOR = 0xF5A623
};

/* The rule, sending, waiting and the screen's rhythm. */
enum {
    CONF_MIN = 60, /* a result less sure than this does not count */
    CONFIRM_N = 3, /* wins needed ... */
    WINDOW_N = 5,  /* ... among this many latest results */
    ALIVE_EVERY_MS = 2000, /* sent again this often, change or not */
    GAP_MS = 200,          /* never closer than this */
    WIFI_WAIT_S = 5,       /* wait for the saved WiFi this long */
    WIFI_POLLS_PER_S = 10,
    WIFI_POLL_MS = 100,
    DRAW_MS = 500,
    RUN_S = 300,
    MS_PER_S = 1000,
    LOOP_MS = 100,
    SETTLE_MS = 150, /* let the cleared screen settle */
    OP_POLL_MS = 100,
    BROKER_PORT = 1883,
    KEEPALIVE_S = 60,
    CLIENT_MASK = 0xFFFF,
    MODEL_KEY_COUNT = 2,
    DANGER_COUNT = 2,
    TEAM_CHARS = 6,
    TEAM_PREFIX_CHARS = 4,
    CLIENT_ID_CHARS = 24,
    TOPIC_CHARS = 64, /* "bento-aiot/", the team and "/event" */
    VALUE_CHARS = 100,
    MESSAGE_CHARS = 320,
    TEXT_CHARS = 160
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

/* Layout on the 792 x 398 canvas. */
enum {
    LEFT_X = 12,
    TITLE_Y = 6,
    DISC_Y = 50,
    DISC_SIZE = 96,
    DISC_RADIUS = 48, /* half the size: a round disc */
    STATE_X = 128,
    STATE_Y = 78,
    RAW_Y = 164,
    COUNT_Y = 200,
    EVENT_Y = 236,
    NOTE_Y = 300,
    SMALL_PX = 16,
    TEXT_PX = 20,
    BIG_PX = 28
};

/* Where a widget sits and how big it is (-1: the widget's default). */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} place_t;

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

/* The model, the widgets, the link, the rule and what was sent. */
typedef struct {
    bento_ui_t *ui;
    uint32_t model;
    char model_name[BENTO_EDGEAI_NAME_LEN];
    bento_ui_id_t disc;
    bento_ui_id_t state_label;
    bento_ui_id_t raw_label;
    bento_ui_id_t count_label;
    bento_ui_id_t event_label;
    bento_ui_id_t note;
    char client_id[CLIENT_ID_CHARS];
    char topic[TOPIC_CHARS];
    char topic_event[TOPIC_CHARS];
    uint32_t client;
    bool online;
    uint8_t hist[WINDOW_N];
    int32_t hist_count;
    bool alerting;
    bool has_sequence;
    uint32_t sequence;
    bool has_label;
    char label[BENTO_EDGEAI_LABEL_LEN];
    int32_t conf;
    bool has_sent_label;
    char sent_label[BENTO_EDGEAI_LABEL_LEN];
    int32_t state_count;
    int32_t event_count;
    bool has_raw;
    char raw[TEXT_CHARS];
    char message[MESSAGE_CHARS];
    uint32_t start_ms;
    uint32_t sent_ms;
    uint32_t drawn_ms;
} link_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern link_t g_link;
link_t g_link;

/* time.sleep_ms(): BENTO_E_CANCELLED when Stop arrives, as the
 * original's KeyboardInterrupt. */
static bento_status_t pause_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static void place_args(bento_ui_widget_args_t *args, place_t place)
{
    bento_ui_widget_args_init(args);
    args->x = place.x;
    args->y = place.y;
    args->w = place.w;
    args->h = place.h;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    place_args(&args, (place_t){look.x, look.y, -1, -1});
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

/* A ui.poll() whose events the original throws away. */
static void poll_drop(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL); /* the events are not read, as before */
}

/* The note line at the bottom, shown at once. */
static void say(const link_t *link, const char *text, uint32_t color)
{
    (void)bento_ui_set_color(link->ui, link->note, color);
    set_text(link->ui, link->note, text);
    poll_drop(link->ui);
}

/* The title, the alarm disc and its word, and the four text lines. */
static bento_status_t add_widgets(link_t *link)
{
    bento_ui_widget_args_t args;
    char title[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    (void)snprintf(title, sizeof(title), "State + event: %s",
                   link->model_name);
    status = add_label(
        link->ui, title,
        (label_look_t){LEFT_X, TITLE_Y, COLOR_TEXT, TEXT_PX}, &unused);
    if (status == BENTO_OK) {
        place_args(&args,
                   (place_t){LEFT_X, DISC_Y, DISC_SIZE, DISC_SIZE});
        args.color = COLOR_OK;
        args.min_val = COLOR_DIM;
        args.max_val = DISC_RADIUS;
        args.font_px = 0;
        status = bento_ui_panel_create(link->ui, &args, &link->disc);
    }
    if (status == BENTO_OK) {
        status = add_label(
            link->ui, "OK",
            (label_look_t){STATE_X, STATE_Y, COLOR_OK, BIG_PX},
            &link->state_label);
    }
    return status;
}

static bento_status_t add_lines(link_t *link)
{
    bento_status_t status =
        add_label(link->ui, " ",
                  (label_look_t){LEFT_X, RAW_Y, COLOR_DIM, SMALL_PX},
                  &link->raw_label);

    if (status == BENTO_OK) {
        status = add_label(
            link->ui, " ",
            (label_look_t){LEFT_X, COUNT_Y, COLOR_TEXT, SMALL_PX},
            &link->count_label);
    }
    if (status == BENTO_OK) {
        status = add_label(
            link->ui, " ",
            (label_look_t){LEFT_X, EVENT_Y, COLOR_INFO, SMALL_PX},
            &link->event_label);
    }
    if (status == BENTO_OK) {
        status = add_label(
            link->ui, " ",
            (label_look_t){LEFT_X, NOTE_Y, COLOR_WARN, SMALL_PX},
            &link->note);
    }
    return status;
}

static bento_status_t build_screen(link_t *link)
{
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(link->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_widgets(link);
    }
    if (status == BENTO_OK) {
        status = add_lines(link);
    }
    if (status == BENTO_OK) {
        poll_drop(link->ui);
    }
    return status;
}

/* The row whose name is key, among count rows. */
static bento_status_t find_row(const char *key, int32_t count,
                               link_t *link, bool *found)
{
    bento_edgeai_model_info_t info;
    int32_t row;
    bento_status_t status = BENTO_OK;

    for (row = 0; (row < count) && !*found && (status == BENTO_OK);
         row++) {
        bento_edgeai_model_info_init(&info);
        status = bento_edgeai_model_info((uint32_t)row, &info);
        *found = (status == BENTO_OK) && (strcmp(info.name, key) == 0);
        if (*found) {
            link->model = (uint32_t)row;
            (void)snprintf(link->model_name, sizeof(link->model_name),
                           "%s", info.name);
        }
    }
    return status;
}

/* The first model whose name matches, in the order of the names. */
static bento_status_t find_model(link_t *link, bool *found)
{
    static const char *const keys[MODEL_KEY_COUNT] = {
        "AnomalousVibration", "Motion Detection"};
    int32_t count = 0;
    uint32_t key;
    bento_status_t status = bento_edgeai_count(&count);

    *found = false;
    for (key = 0U; (key < (uint32_t)MODEL_KEY_COUNT) && !*found &&
                   (status == BENTO_OK);
         key++) {
        status = find_row(keys[key], count, link, found);
    }
    return status;
}

/* team01 to team99: six characters, "team" and two digits. */
static bool team_ok(void)
{
    bool digits = strlen(TEAM) == (size_t)TEAM_CHARS;
    size_t i;

    for (i = (size_t)TEAM_PREFIX_CHARS;
         digits && (i < sizeof(TEAM) - 1U); i++) {
        digits = (TEAM[i] >= '0') && (TEAM[i] <= '9');
    }
    return digits && (strncmp(TEAM, "team", TEAM_PREFIX_CHARS) == 0) &&
           (strcmp(TEAM, "team00") != 0);
}

/* The WiFi the board saved comes up by itself: wait up to WIFI_WAIT_S
 * seconds for it. */
static bento_status_t wait_saved_wifi(bool *connected)
{
    bento_status_t status = BENTO_OK;
    int32_t poll;

    *connected = false;
    for (poll = 0; (poll < (WIFI_WAIT_S * WIFI_POLLS_PER_S)) &&
                   !*connected && (status == BENTO_OK);
         poll++) {
        if (bento_wifi_is_connected(connected) != BENTO_OK) {
            *connected = false; /* an unknown link counts as down */
        }
        if (!*connected) {
            status = pause_ms(WIFI_POLL_MS);
        }
    }
    return status;
}

/* wifi.connect(ssid, password): *joined is its True or False; a
 * refusal or a timeout is False, anything else an OSError. */
static bento_status_t join_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = WIFI_PASS;
    params.pass_len = (uint32_t)strlen(WIFI_PASS);
    params.timeout_ms = 0U; /* the binding's own window */
    params.flags = BENTO_WIFI_F_SAVE_CREDS;
    status = bento_wifi_connect_start(&params);
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = pause_ms(OP_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_connect_poll();
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* answers BENTO_OK only */
    }
    *joined = status == BENTO_OK;
    if ((status == BENTO_E_FAILED) || (status == BENTO_E_TIMEOUT) ||
        (status == BENTO_E_INVALID_ARGUMENT)) {
        status = BENTO_OK;
    }
    return status;
}

/* The WiFi the board already has first; joins by itself only once
 * WIFI_SSID was edited. */
static bento_status_t wifi_up(const link_t *link, bool *up)
{
    bento_status_t status = wait_saved_wifi(up);

    if ((status == BENTO_OK) && !*up && (WIFI_SSID[0] == '<')) {
        say(link, "No WiFi - set WIFI_SSID (offline)", COLOR_WARN);
    } else if ((status == BENTO_OK) && !*up) {
        say(link, "Connecting WiFi ...", COLOR_WARN);
        status = join_wifi(up);
    } else {
        /* the saved WiFi is up, or Stop came */
    }
    return status;
}

/* mqtt.connect(): any failure but Stop is False, as the binding
 * answers. The client is kept for the finally's disconnect. */
static bento_status_t open_broker(link_t *link, bool *opened)
{
    bento_mqtt_config_t config;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = (uint16_t)BROKER_PORT;
    config.keepalive_s = (uint16_t)KEEPALIVE_S;
    config.client_id = link->client_id;
    config.client_id_len = (uint32_t)strlen(link->client_id);
    status = bento_mqtt_open_start(&config, &link->client);
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = pause_ms(OP_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_mqtt_open_poll(link->client);
        }
    }
    *opened = status == BENTO_OK;
    if (status != BENTO_E_CANCELLED) {
        status = BENTO_OK;
    }
    return status;
}

/* Online needs a team, WiFi and the broker; each missing step says
 * why on the note line, and the program runs offline. */
static bento_status_t go_online(link_t *link)
{
    char text[TEXT_CHARS];
    bool wifi = false;
    bool failed = false;
    bento_status_t status = BENTO_OK;

    if (!team_ok()) {
        say(link, "Set TEAM first (team01-team99) - offline",
            COLOR_WARN);
    } else {
        status = wifi_up(link, &wifi);
        failed = (status != BENTO_OK) && (status != BENTO_E_CANCELLED);
    }
    if (failed) {
        (void)printf("network: %" PRId32 "\n", status);
        status = BENTO_OK;
    }
    if ((status == BENTO_OK) && wifi) {
        say(link, "Connecting broker ...", COLOR_WARN);
        status = open_broker(link, &link->online);
        failed = !link->online;
    }
    if ((status == BENTO_OK) && link->online) {
        (void)snprintf(text, sizeof(text), "Online: bento-aiot/%s/#",
                       TEAM);
        say(link, text, COLOR_OK);
    } else if ((status == BENTO_OK) && failed) {
        say(link, "Broker failed - offline", COLOR_BAD);
    } else {
        /* offline already said why, or Stop came */
    }
    return status;
}

/* A JSON string: quote, backslash and control bytes escaped; the
 * text is cut at the first byte that does not fit. */
static void json_text(char *out, size_t capacity, const char *text)
{
    size_t used = 0U;
    size_t i = 0U;
    bool fits = true;

    while (fits && (text[i] != '\0')) {
        const unsigned char byte = (unsigned char)text[i];
        const bool quoted = (byte == (unsigned char)'"') ||
                            (byte == (unsigned char)'\\');
        const bool control = byte < (unsigned char)' ';
        const size_t need =
            control ? (sizeof("\\u0000") - 1U) : (quoted ? 2U : 1U);

        fits = (used + need) < capacity;
        if (fits && control) {
            (void)snprintf(&out[used], capacity - used, "\\u%04x",
                           (unsigned int)byte);
        } else if (fits) {
            out[used] = '\\'; /* a plain byte writes over it */
            out[used + need - 1U] = text[i];
        } else {
            /* the rest is cut */
        }
        used += fits ? need : 0U;
        i++;
    }
    out[used] = '\0';
}

/* The k-of-n rule: confirmed at CONFIRM_N hits among the latest
 * WINDOW_N results, and it stays confirmed while any hit remains. */
static void alert_rule(link_t *link, bool danger)
{
    int32_t hits = 0;
    int32_t i;

    if (link->hist_count == WINDOW_N) {
        for (i = 1; i < WINDOW_N; i++) {
            link->hist[i - 1] = link->hist[i];
        }
        link->hist_count--;
    }
    link->hist[link->hist_count] = danger ? 1U : 0U;
    link->hist_count++;
    for (i = 0; i < link->hist_count; i++) {
        hits += (int32_t)link->hist[i];
    }
    link->alerting =
        (hits >= CONFIRM_N) || (link->alerting && (hits > 0));
}

static bool is_danger(const char *label)
{
    static const char *const danger[DANGER_COUNT] = {"anomaly",
                                                     "shaking"};
    bool found = false;
    uint32_t i;

    for (i = 0U; i < (uint32_t)DANGER_COUNT; i++) {
        found = found || (strcmp(label, danger[i]) == 0);
    }
    return found;
}

/* Printed always with its topic; published when online. A refused
 * publish is passed over. */
static void send(const link_t *link, const char *topic)
{
    (void)printf("%s %s\n", topic, link->message);
    if (link->online) {
        (void)bento_mqtt_publish(
            link->client, topic, (uint32_t)strlen(topic),
            (const uint8_t *)link->message,
            (uint32_t)strlen(link->message), 0U,
            0U); /* its True or False is not used */
    }
}

/* The event: id, event, model, label, conf. */
static void send_event(link_t *link, bool start)
{
    char model[VALUE_CHARS];
    char label[VALUE_CHARS];

    json_text(model, sizeof(model), link->model_name);
    json_text(label, sizeof(label), link->label);
    (void)snprintf(
        link->message, sizeof(link->message),
        "{\"id\": \"%s\", \"event\": \"%s\", \"model\": \"%s\", "
        "\"label\": \"%s\", \"conf\": %" PRId32 "}",
        TEAM, start ? "ai_alarm_start" : "ai_alarm_end", model, label,
        link->conf);
    send(link, link->topic_event);
}

/* The state: id, n, model, label, conf, alarm. */
static void send_state(link_t *link)
{
    char model[VALUE_CHARS];
    char label[VALUE_CHARS];

    json_text(model, sizeof(model), link->model_name);
    json_text(label, sizeof(label), link->label);
    (void)snprintf(link->message, sizeof(link->message),
                   "{\"id\": \"%s\", \"n\": %" PRId32
                   ", \"model\": \"%s\", \"label\": \"%s\", \"conf\": "
                   "%" PRId32 ", \"alarm\": %d}",
                   TEAM, link->state_count, model, label, link->conf,
                   link->alerting ? 1 : 0);
    send(link, link->topic);
}

/* An event goes out once, when the alarm starts or ends. */
static void announce_change(link_t *link)
{
    char text[TEXT_CHARS];
    const uint32_t color = link->alerting ? COLOR_BAD : COLOR_OK;

    link->event_count++;
    send_event(link, link->alerting);
    (void)snprintf(text, sizeof(text), "event: %s (%s %" PRId32 "%%)",
                   link->alerting ? "ai_alarm_start" : "ai_alarm_end",
                   link->label, link->conf);
    set_text(link->ui, link->event_label, text);
    (void)bento_ui_set_color(link->ui, link->disc, color);
    set_text(link->ui, link->state_label,
             link->alerting ? "ALARM" : "OK");
    (void)bento_ui_set_color(link->ui, link->state_label, color);
}

/* A new result of this model with a label goes through the rule. */
static void take_result(link_t *link)
{
    bento_edgeai_result_t result;

    bento_edgeai_result_init(&result);
    if ((bento_edgeai_result(&result) == BENTO_OK) &&
        result.has_verdict && (result.index == link->model) &&
        (!link->has_sequence || (result.seq != link->sequence)) &&
        (result.top < BENTO_EDGEAI_MAX_CLASSES)) {
        const bool was = link->alerting;

        link->has_sequence = true;
        link->sequence = result.seq;
        if (bento_edgeai_label(result.index, result.top, link->label,
                               (uint32_t)sizeof(link->label),
                               NULL) != BENTO_OK) {
            link->label[0] = '\0';
        }
        link->has_label = true;
        link->conf = (int32_t)result.score_pct[result.top];
        alert_rule(link,
                   is_danger(link->label) && (link->conf >= CONF_MIN));
        (void)snprintf(link->raw, sizeof(link->raw),
                       "raw: %s %" PRId32 "%%", link->label,
                       link->conf);
        link->has_raw = true;
        if (link->alerting != was) {
            announce_change(link);
        }
    }
}

/* Far enough from the last message, and the label changed or the
 * heartbeat is due. */
static bool should_send(bool changed, int32_t since_ms)
{
    return (since_ms >= GAP_MS) &&
           (changed || (since_ms >= ALIVE_EVERY_MS));
}

static void send_if_due(link_t *link, uint32_t now_ms)
{
    const bool changed = !link->has_sent_label ||
                         (strcmp(link->label, link->sent_label) != 0);

    if (link->has_label &&
        should_send(changed,
                    bento_time_ticks_diff(now_ms, link->sent_ms))) {
        link->state_count++;
        send_state(link);
        link->sent_ms = now_ms;
        link->has_sent_label = true;
        (void)snprintf(link->sent_label, sizeof(link->sent_label), "%s",
                       link->label);
    }
}

/* The broker went away: the AI keeps running, offline. */
static void check_link(link_t *link)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;

    if (link->online &&
        ((bento_mqtt_state(link->client, &state) != BENTO_OK) ||
         (state != BENTO_MQTT_STATE_CONNECTED))) {
        link->online = false;
        say(link, "Network lost - AI keeps running", COLOR_WARN);
    }
}

static void redraw(const link_t *link)
{
    char text[TEXT_CHARS];

    set_text(link->ui, link->raw_label, link->raw);
    (void)snprintf(text, sizeof(text),
                   "sent: state %" PRId32 " | event %" PRId32,
                   link->state_count, link->event_count);
    set_text(link->ui, link->count_label, text);
}

/* RUN_S seconds of reading, sending and drawing. */
static bento_status_t run_rounds(link_t *link)
{
    bento_status_t status = BENTO_OK;

    link->start_ms = bento_time_ticks_ms();
    while (
        (status == BENTO_OK) &&
        (bento_time_ticks_diff(bento_time_ticks_ms(), link->start_ms) <
         (RUN_S * MS_PER_S))) {
        const uint32_t now_ms = bento_time_ticks_ms();

        take_result(link);
        send_if_due(link, now_ms);
        check_link(link);
        if (link->has_raw && (bento_time_ticks_diff(
                                  now_ms, link->drawn_ms) >= DRAW_MS)) {
            link->drawn_ms = now_ms;
            redraw(link);
        }
        poll_drop(link->ui);
        status = pause_ms(LOOP_MS);
    }
    return status;
}

/* Waits for a started select; Stop cancels it. */
static bento_status_t select_model(uint32_t model)
{
    bento_status_t status = bento_edgeai_select_start(model);

    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = pause_ms(OP_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_edgeai_op_poll(NULL);
        } else {
            (void)bento_edgeai_op_cancel(); /* Stop wins */
        }
    }
    return status;
}

/* The finally: the AI core is released; ending during an alarm tells
 * the dashboard it is over; the broker is closed, always. Their
 * OSErrors are passed over, and the wait runs even after Stop, as the
 * binding does. */
static void release_all(link_t *link)
{
    bento_status_t status = bento_edgeai_stop_start();

    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        /* the IDE's Stop may already be in: the wait is not checked */
        (void)bento_time_sleep_ms(OP_POLL_MS);
        status = bento_edgeai_op_poll(NULL);
    }
    if (link->alerting && link->online) {
        send_event(link, false);
    }
    if (link->client != 0U) {
        (void)bento_mqtt_close(
            link->client); /* never connected is fine */
        link->client = 0U;
    }
}

/* Online if it can be, then the model, under the finally. */
static bento_status_t run_link(link_t *link)
{
    bento_status_t status;

    link->start_ms = bento_time_ticks_ms();
    link->sent_ms = link->start_ms;
    link->drawn_ms = link->start_ms;
    status = go_online(link);
    if (status == BENTO_OK) {
        status = select_model(link->model);
    }
    if (status == BENTO_OK) {
        status = run_rounds(link);
    }
    release_all(link);
    return status;
}

/* The BENTO Emulator's C runtime answers BENTO_E_UNSUPPORTED to every
 * Edge AI call: the console says so. */
static void print_emulator_note(void)
{
    (void)printf(
        "%s / Edge AI from C is not in the BENTO Emulator yet: "
        "run it on a Dev Kit or Eva Kit.\n",
        EMULATOR_NOTE);
}

/* The same note as a label on the current screen. It is best effort,
 * as a widget setter is, so its status is not checked. */
static void add_emulator_note(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    bento_ui_widget_args_init(&args);
    args.text = EMULATOR_NOTE;
    args.text_len = (uint32_t)strlen(EMULATOR_NOTE);
    args.x = EMULATOR_NOTE_X;
    args.y = EMULATOR_NOTE_Y;
    args.color = EMULATOR_NOTE_COLOR;
    args.font_px = EMULATOR_NOTE_PX;
    (void)bento_ui_label_create(ui, &args, &unused);
}

int main(void)
{
    link_t *link = &g_link;
    bool supported = false;
    bool in_emulator = false;
    bool found = false;
    bento_status_t status;

    (void)memset(link, 0, sizeof(*link));
    link->ui = bento_runtime_get_ui();
    (void)snprintf(link->client_id, sizeof(link->client_id),
                   "bento-ai-%s-%04" PRIx32, TEAM,
                   bento_time_ticks_ms() & (uint32_t)CLIENT_MASK);
    (void)snprintf(link->topic, sizeof(link->topic), "bento-aiot/%s/ai",
                   TEAM);
    (void)snprintf(link->topic_event, sizeof(link->topic_event),
                   "bento-aiot/%s/event", TEAM);
    status = bento_edgeai_supported(&supported);
    in_emulator = status == BENTO_E_UNSUPPORTED;
    if (in_emulator) {
        (void)bento_ui_screen(link->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                              BENTO_RUNTIME_SCREEN_HEIGHT_PX);
        add_emulator_note(link->ui);
        print_emulator_note();
        status = BENTO_OK; /* on as if no model were on the board */
    }
    if ((status == BENTO_OK) && !supported && !in_emulator) {
        status = BENTO_E_UNSUPPORTED; /* import edge_ai fails */
    }
    if ((status == BENTO_OK) && !in_emulator) {
        status = find_model(link, &found);
    }
    if ((status == BENTO_OK) && !found) {
        (void)printf("none of these models is on the board: "
                     "('AnomalousVibration', 'Motion Detection')\n");
    } else if (status == BENTO_OK) {
        status = build_screen(link);
        if (status == BENTO_OK) {
            status = run_link(link);
        }
    } else {
        /* an OSError from edge_ai.models() ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
