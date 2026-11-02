/*
 * 04_hmi_to_mqtt.c -- ผล AI ของจอ Smart HMI ขึ้น MQTT ไปให้แดชบอร์ด
 * (the Smart HMI's AI results go to a dashboard over MQTT)
 *
 * ไฟล์เดียวทั้งห้อง แก้ TEAM กับ KIT สองบรรทัด ทุกทีมส่ง JSON หน้าตาเดียวกัน
 * {"id","kit","model","label","conf","state","events","n"} ตัวเดียวกับ snapshot()
 * ของคาบ 11 state คือ normal ปกติ, alert ยืนยันแล้วยังไม่มีใครรับ, ack รับทราบแล้ว
 * ส่งเมื่อ label state หรือ events เปลี่ยน และ heartbeat ทุก 2 วินาที ห่างกันอย่างน้อย
 * 200 ms จอบอกสถานะตัวใหญ่ ผลดิบ ไฟ 5 ดวง ปุ่มเดียว (รับทราบ แล้วรีเซ็ต) ไฟ MQTT
 * จำนวนข้อความ และ JSON ล่าสุด ยังไม่แก้ TEAM = ทำงานออฟไลน์ พิมพ์ JSON ลง Console
 *
 * One file for the whole class: set TEAM and KIT, and every team sends
 * the same JSON, snapshot() of session 11: {"id", "kit", "model",
 * "label", "conf", "state", "events", "n"}. state is normal, alert
 * (confirmed, nobody has taken it) or ack (someone acknowledged it). A
 * message goes when the label, state or events change, plus a
 * heartbeat every 2 s, at least 200 ms apart. The screen shows the
 * state in big type, the raw result, five lamps, one button
 * (acknowledge, then reset), an MQTT lamp, the message count and the
 * latest JSON. With TEAM not set it runs offline and prints the JSON on
 * the console.
 *
 * Twin of 04_hmi_to_mqtt.py
 * (embedded-systems-for-aiot-developer/examples/s14/04_hmi_to_mqtt.py),
 * written new by hand.
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - KITS (a dict of tuples) is a table of structs, CONF_FOR a small
 *    lookup; json.dumps(snapshot()) is one snprintf with the keys in
 *    the order the dict is written (MicroPython's dict may print
 *    another order), names and labels printed without JSON escapes;
 *  - the JSON is cut into 50-byte pieces; it holds ASCII names only, so
 *    this is the same as the original's 50-character slices;
 *  - edge_ai.models() is bento_edgeai_count() and model_info() per
 *    row, its labels bento_edgeai_label() ("" when the board cannot
 *    name one); a result's conf is score_pct[top] in percent, and a
 *    class score is score_pct[cls], the board's own rounded percent,
 *    where the original rounds the float score itself;
 *  - mqtt.connect() and wifi.connect() answer a status: any failure is
 *    the False answer, and the binding's OSError text ("network: ...")
 *    is not printed;
 *  - select() and stop() wait inside the binding; here they are
 *    started and polled, and the OSError text is the binding's words
 *    ("edge_ai: select send failed", "edge_ai: select not confirmed");
 *  - import edge_ai fails where there is no model link: here
 *    bento_edgeai_supported() false ends with BENTO_E_UNSUPPORTED;
 *  - Stop raises KeyboardInterrupt in MicroPython; here the wait
 *    returns and the program ends after the same finally (the model is
 *    stopped and the broker link closed).
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
static const char TEAM[] = "teamXX"; /* from the teacher, e.g. team05 */
/* KIT: machine (06) | safety (07) | nursery (08) | voice (09) */
static const char KIT[] = "machine";
/* BROKER: test.mosquitto.org is the spare */
static const char BROKER[] = "broker.hivemq.com";
/* The rules and the rhythm, as the original sets them. */
enum {
    CONF_MIN = 60, /* a result less sure than this does not count */
    CONFIRM_N = 3, /* wins needed ... */
    WINDOW_N = 5,  /* ... among this many latest results */
    ALIVE_EVERY_MS = 2000, /* nothing changed: sent again this often */
    GAP_MS = 200,          /* never sent closer than this */
    WIFI_WAIT_S = 5,       /* seconds to wait for a saved WiFi */
    DRAW_MS = 500,         /* the screen is redrawn this often */
    RUN_S = 300,           /* the program runs this many seconds */
    HOLD_S = 30            /* an error screen stays this long */
};
/* ---- end of settings ---- */

/* The note for the BENTO Emulator, whose C runtime has no Edge AI yet. */
static const char EMULATOR_NOTE[] =
    "Edge AI จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";

/* The alert rule, the network and the screen's rhythm. */
enum {
    SIREN_CONF_MIN = 45, /* the firmware's confirm level for sirens */
    WIFI_POLLS_PER_S = 10,
    WIFI_POLL_MS = 100,
    HOLD_POLLS_PER_S = 5,
    HOLD_POLL_MS = 200,
    MS_PER_S = 1000,
    LOOP_MS = 100,
    SETTLE_MS = 150, /* let the cleared screen settle */
    OP_POLL_MS = 100,
    NET_POLL_MS = 100,
    MQTT_PORT = 1883,
    KEEPALIVE_S = 60,
    CLOCK_MASK =
        0xFFFF, /* the client id's suffix: the clock's low bits */
    KIT_COUNT = 4,
    KEYS_MAX = 3,
    DANGER_MAX = 2,
    HOLD_LINES = 4,
    JSON_LINES = 3,
    JSON_PIECE = 50, /* the longest JSON is about 150 bytes */
    TEAM_PREFIX_LEN = 4,
    TEAM_LEN = 6,
    NO_CLASS = -1,
    NO_KIT = -1,
    TOPIC_CHARS = 64,
    ID_CHARS = 24,
    TEXT_CHARS = 192,
    LINE_CHARS = 256
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF,
    COLOR_KEY = 0x212431
};

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 12,
    TITLE_Y = 6,
    MODEL_Y = 44,
    STATE_Y = 84,
    RAW_Y = 128,
    DOT_Y = 164,
    DOT_STEP = 40,
    DOT_SIZE = 28,
    KEY_Y = 204,
    KEY_W = 300,
    KEY_H = 48,
    LINK_X = 560,
    LINK_CAPTION_Y = 48,
    LINK_LED_Y = 74,
    LINK_LED_SIZE = 36,
    LINK_TEXT_X = 606,
    LINK_TEXT_Y = 82,
    NOTE_Y = 262,
    SENT_Y = 288,
    JSON_Y = 312,
    JSON_STEP = 22,
    HOLD_X = 24,
    HOLD_Y = 24,
    HOLD_STEP = 44,
    SMALL_PX = 16,
    TEXT_PX = 20,
    TITLE_PX = 24,
    BIG_PX = 28
};

/* The panel's three states. */
typedef enum {
    STATE_NORMAL = 0,
    STATE_ALERT = 1,
    STATE_ACKNOWLEDGED = 2,
    STATE_COUNT = 3
} panel_state_t;

/* One kit of session 11: how many model names it may use (the first
 * found wins) and danger classes it has, whether normal comes back
 * only by the button, and its name on the screen. Its names are the
 * same row of KIT_KEYS, and its danger names that row in is_danger(). */
typedef struct {
    const char *kit;
    uint32_t key_count;
    uint32_t danger_count;
    bool manual;
    const char *title;
} kit_t;

/* The same values as the files 06 07 08 of session 11; voice needs its
 * model deployed from the Store first. */
static const kit_t KITS[KIT_COUNT] = {
    {"machine", 2U, 2U, true, "ปั๊มหล่อเย็น P-101"},
    {"safety", 3U, 2U, true, "แผงแจ้งเหตุ ห้องคอม 202"},
    {"nursery", 3U, 2U, false, "ห้องเด็กอ่อน เตียง 4"},
    {"voice", 1U, 1U, false, "บ้านสั่งด้วยเสียง  stop = หยุด"}};

/* Each kit's model names, in the order they are tried. */
static const char *const KIT_KEYS[KIT_COUNT][KEYS_MAX] = {
    {"AnomalousVibration", "Motion Detection", ""},
    {"Alarm Detection", "Siren Detection", "SirenDetection"},
    {"Baby Cry Detection", "HomeSounds", "Environment Sounds"},
    {"Voice Commands", "", ""}};

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

/* Up to HOLD_LINES lines for an error screen. */
typedef struct {
    const char *line[HOLD_LINES];
    uint32_t count;
} hold_text_t;

/* What was last sent: the label, the state and the events. */
typedef struct {
    bool known;
    char label[BENTO_EDGEAI_LABEL_LEN];
    panel_state_t state;
    int32_t events;
} sent_key_t;

/* The kit, the model, the widgets, the network, the rule's memory and
 * the snapshot. */
typedef struct {
    bento_ui_t *ui;
    int32_t kit; /* the row in KITS */
    uint32_t model;
    char model_name[BENTO_EDGEAI_NAME_LEN];
    int32_t danger_class;
    int32_t conf_min;
    bento_ui_id_t model_label;
    bento_ui_id_t state_label;
    bento_ui_id_t raw_label;
    bento_ui_id_t dots[WINDOW_N];
    bento_ui_id_t key;
    bento_ui_id_t link_led;
    bento_ui_id_t link_label;
    bento_ui_id_t note_label;
    bento_ui_id_t sent_label;
    bento_ui_id_t json_label[JSON_LINES];
    bool online;
    bool has_handle;
    uint32_t handle;
    bool blink;
    uint8_t hist[WINDOW_N];
    int32_t hist_count;
    bool alerting;
    int32_t lit[WINDOW_N];
    bool has_sequence;
    uint32_t last_sequence;
    char label[BENTO_EDGEAI_LABEL_LEN];
    int32_t conf;
    panel_state_t state;
    int32_t events;
    uint32_t sequence;
    sent_key_t sent_key;
    int32_t messages;
    int32_t reached;
    bool has_last;
    char last[LINE_CHARS];
    uint32_t start_ms;
    uint32_t sent_ms;
    uint32_t drawn_ms;
    char client_id[ID_CHARS];
    char topic[TOPIC_CHARS];
} hmi_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern hmi_t g_hmi;
hmi_t g_hmi;

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

/* ---- the screen ---- */

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_led(bento_ui_t *ui, int16_t x, int16_t y,
                              int16_t size, uint32_t color,
                              bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = x;
    args.y = y;
    args.w = size;
    args.h = size;
    args.color = color;
    args.font_px = 0;
    return bento_ui_led_create(ui, &args, out_id);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static void set_text_color(bento_ui_t *ui, bento_ui_id_t widget,
                           const char *text, uint32_t color)
{
    set_text(ui, widget, text);
    (void)bento_ui_set_color(ui, widget, color);
}

/* A ui.poll() whose events the original throws away. */
static void poll_drop(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL); /* the events are not read, as before */
}

/* The note line, then a poll so it shows now (that poll drops a tap,
 * which is why a refused reset does not come here). */
static void say(const hmi_t *hmi, const char *text, uint32_t color)
{
    (void)bento_ui_set_color(hmi->ui, hmi->note_label, color);
    set_text(hmi->ui, hmi->note_label, text);
    poll_drop(hmi->ui);
}

/* The title, the model, the state, the raw result, the lamps and the
 * button. */
static bento_status_t add_panel(hmi_t *hmi)
{
    bento_ui_t *ui = hmi->ui;
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    int32_t i;
    bento_status_t status = add_label(
        ui, "Smart HMI to MQTT",
        (label_look_t){MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX},
        &unused);

    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){MARGIN_X, MODEL_Y, COLOR_INFO, TEXT_PX},
            &hmi->model_label);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){MARGIN_X, STATE_Y, COLOR_OK, BIG_PX},
            &hmi->state_label);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "-",
            (label_look_t){MARGIN_X, RAW_Y, COLOR_DIM, TEXT_PX},
            &hmi->raw_label);
    }
    for (i = 0; (i < WINDOW_N) && (status == BENTO_OK); i++) {
        status = add_led(ui, (int16_t)(MARGIN_X + (i * DOT_STEP)),
                         DOT_Y, DOT_SIZE, COLOR_BAD, &hmi->dots[i]);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "รับทราบ";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = MARGIN_X;
        args.y = KEY_Y;
        args.w = KEY_W;
        args.h = KEY_H;
        args.color = COLOR_KEY;
        args.font_px = TEXT_PX;
        status = bento_ui_button_create(ui, &args, &hmi->key);
    }
    return status;
}

/* The MQTT lamp, the note, the message count and the JSON lines. */
static bento_status_t add_link(hmi_t *hmi)
{
    bento_ui_t *ui = hmi->ui;
    bento_ui_id_t unused = 0U;
    int32_t i;
    bento_status_t status = add_label(
        ui, "MQTT",
        (label_look_t){LINK_X, LINK_CAPTION_Y, COLOR_DIM, SMALL_PX},
        &unused);

    if (status == BENTO_OK) {
        status = add_led(ui, LINK_X, LINK_LED_Y, LINK_LED_SIZE,
                         COLOR_OK, &hmi->link_led);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "offline",
                           (label_look_t){LINK_TEXT_X, LINK_TEXT_Y,
                                          COLOR_DIM, SMALL_PX},
                           &hmi->link_label);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){MARGIN_X, NOTE_Y, COLOR_WARN, SMALL_PX},
            &hmi->note_label);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){MARGIN_X, SENT_Y, COLOR_TEXT, SMALL_PX},
            &hmi->sent_label);
    }
    for (i = 0; (i < JSON_LINES) && (status == BENTO_OK); i++) {
        status =
            add_label(ui, " ",
                      (label_look_t){
                          MARGIN_X, (int16_t)(JSON_Y + (i * JSON_STEP)),
                          COLOR_DIM, SMALL_PX},
                      &hmi->json_label[i]);
    }
    return status;
}

static bento_status_t build_screen(hmi_t *hmi)
{
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(hmi->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_panel(hmi);
    }
    if (status == BENTO_OK) {
        status = add_link(hmi);
    }
    if (status == BENTO_OK) {
        poll_drop(hmi->ui);
    }
    return status;
}

static void show_link(const hmi_t *hmi, bool online)
{
    (void)bento_ui_set_value(hmi->ui, hmi->link_led, online ? 1 : 0);
    set_text(hmi->ui, hmi->link_label,
             online ? "connected" : "offline");
}

/* The state word and the button's text and colour. */
static void draw_state(const hmi_t *hmi)
{
    static const char *const words[STATE_COUNT] = {
        "ปกติ", "เตือน ยังไม่มีใครรับ", "รับทราบแล้ว"};
    static const uint32_t colors[STATE_COUNT] = {COLOR_OK, COLOR_BAD,
                                                 COLOR_WARN};
    const bool alert = hmi->state == STATE_ALERT;

    set_text_color(hmi->ui, hmi->state_label, words[hmi->state],
                   (alert && hmi->blink) ? COLOR_DIM
                                         : colors[hmi->state]);
    if (alert) {
        set_text_color(hmi->ui, hmi->key, "รับทราบ", COLOR_WARN);
    } else if ((hmi->state == STATE_ACKNOWLEDGED) &&
               KITS[hmi->kit].manual) {
        set_text_color(hmi->ui, hmi->key, "รีเซ็ต", COLOR_INFO);
    } else {
        set_text_color(hmi->ui, hmi->key,
                       (hmi->state == STATE_NORMAL) ? "รับทราบ"
                                                    : "รอให้เหตุหาย",
                       COLOR_KEY);
    }
}

/* Clears the screen, shows why line by line, and keeps it HOLD_S
 * seconds to read; then the program ends by itself without failing. */
static bento_status_t hold(bento_ui_t *ui, const hold_text_t *text,
                           uint32_t color)
{
    bento_ui_id_t unused = 0U;
    uint32_t i;
    int32_t poll;
    bento_status_t status;

    (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    for (i = 0U; (i < text->count) && (status == BENTO_OK); i++) {
        const int16_t y = (int16_t)(HOLD_Y + ((int32_t)i * HOLD_STEP));

        status = add_label(
            ui, text->line[i],
            (label_look_t){HOLD_X, y, (i == 0U) ? color : COLOR_TEXT,
                           TITLE_PX},
            &unused);
    }
    for (poll = 0;
         (poll < ((int32_t)HOLD_S * (int32_t)HOLD_POLLS_PER_S)) &&
         (status == BENTO_OK);
         poll++) {
        poll_drop(ui);
        status = pause_ms(HOLD_POLL_MS);
    }
    return status;
}

static void add_line(hold_text_t *text, const char *line)
{
    if (text->count < (uint32_t)HOLD_LINES) {
        text->line[text->count] = line;
        text->count++;
    }
}

/* ---- the kit and the model ---- */

/* The kit named in KIT, as its row in KITS, or NO_KIT. */
static int32_t find_kit(void)
{
    int32_t found = NO_KIT;
    int32_t i;

    for (i = 0; (i < KIT_COUNT) && (found == NO_KIT); i++) {
        if (strcmp(KITS[i].kit, KIT) == 0) {
            found = i;
        }
    }
    return found;
}

static bento_status_t hold_bad_kit(bento_ui_t *ui)
{
    char first[TEXT_CHARS];
    hold_text_t text;

    (void)snprintf(first, sizeof(first), "KIT ไม่ถูกต้อง: %s", KIT);
    text.count = 0U;
    add_line(&text, first);
    add_line(&text, "machine safety nursery voice");
    return hold(ui, &text, COLOR_BAD);
}

/* The kit's names as print() shows a tuple of strings, or joined by
 * " / " for the screen. */
static void join_keys(int32_t kit, bool as_tuple, char *text,
                      uint32_t capacity)
{
    uint32_t used = 0U;
    uint32_t i;

    text[0] = '\0';
    for (i = 0U; i < KITS[kit].key_count; i++) {
        const char *separator =
            (i == 0U) ? "" : (as_tuple ? ", " : " / ");
        const char *quote = as_tuple ? "'" : "";

        (void)snprintf(&text[used], capacity - used, "%s%s%s%s",
                       separator, quote, KIT_KEYS[kit][i], quote);
        used = (uint32_t)strlen(text);
    }
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

static bento_status_t hold_no_model(const hmi_t *hmi, bool in_emulator)
{
    char first[TEXT_CHARS];
    char names[TEXT_CHARS];
    hold_text_t text;

    join_keys(hmi->kit, true, names, (uint32_t)sizeof(names));
    (void)printf("none of these models is on the board: (%s%s)\n",
                 names, (KITS[hmi->kit].key_count == 1U) ? "," : "");
    (void)snprintf(first, sizeof(first), "ไม่พบโมเดลของ KIT %s", KIT);
    join_keys(hmi->kit, false, names, (uint32_t)sizeof(names));
    text.count = 0U;
    add_line(&text, first);
    add_line(&text, names);
    add_line(&text, "deploy จาก Store ก่อน");
    if (in_emulator) {
        add_line(&text, EMULATOR_NOTE);
    }
    return hold(hmi->ui, &text, COLOR_BAD);
}

/* The row of one model name, if it is on the board. */
static bento_status_t find_row(hmi_t *hmi, const char *name,
                               bool *found)
{
    bento_edgeai_model_info_t info;
    int32_t count = 0;
    int32_t row;
    bento_status_t status = bento_edgeai_count(&count);

    *found = false;
    for (row = 0; (row < count) && !*found && (status == BENTO_OK);
         row++) {
        bento_edgeai_model_info_init(&info);
        status = bento_edgeai_model_info((uint32_t)row, &info);
        *found = (status == BENTO_OK) && (strcmp(info.name, name) == 0);
        if (*found) {
            hmi->model = (uint32_t)row;
            (void)snprintf(hmi->model_name, sizeof(hmi->model_name),
                           "%s", info.name);
        }
    }
    return status;
}

/* The first of the kit's model names that is on the board. */
static bento_status_t find_model(hmi_t *hmi, bool *found)
{
    bento_status_t status = BENTO_OK;
    uint32_t key;

    *found = false;
    for (key = 0U; (key < KITS[hmi->kit].key_count) && !*found &&
                   (status == BENTO_OK);
         key++) {
        status = find_row(hmi, KIT_KEYS[hmi->kit][key], found);
    }
    return status;
}

static bool is_danger(int32_t kit, const char *label)
{
    /* each kit's danger classes, by the names the board reports */
    static const char *const danger_names[KIT_COUNT][DANGER_MAX] = {
        {"anomaly", "shaking"},
        {"alarm", "sirens"},
        {"baby_cry", "crying_baby"},
        {"stop", ""}};
    bool danger = false;
    uint32_t i;

    for (i = 0U; i < KITS[kit].danger_count; i++) {
        danger = danger || (strcmp(label, danger_names[kit][i]) == 0);
    }
    return danger;
}

/* The first class whose label is a danger class, or NO_CLASS. */
static int32_t danger_class_of(const hmi_t *hmi)
{
    bento_edgeai_model_info_t info;
    char label[BENTO_EDGEAI_LABEL_LEN];
    int32_t found = NO_CLASS;
    uint32_t cls;

    bento_edgeai_model_info_init(&info);
    if (bento_edgeai_model_info(hmi->model, &info) != BENTO_OK) {
        info.class_count = 0U; /* no labels: m.get("labels", []) */
    }
    for (cls = 0U; (cls < (uint32_t)info.class_count) && (found < 0);
         cls++) {
        if (bento_edgeai_label(hmi->model, cls, label,
                               (uint32_t)sizeof(label),
                               NULL) != BENTO_OK) {
            label[0] = '\0';
        }
        if (is_danger(hmi->kit, label)) {
            found = (int32_t)cls;
        }
    }
    return found;
}

/* The confirm threshold of the firmware for this model (as file 07). */
static int32_t conf_min_for(const char *name)
{
    return (strcmp(name, "Siren Detection") == 0)
               ? (int32_t)SIREN_CONF_MIN
               : (int32_t)CONF_MIN;
}

/* Waits for a started select or stop; Stop cancels it. */
static bento_status_t finish_op(void)
{
    bento_status_t status = BENTO_E_IN_PROGRESS;

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

/* edge_ai.stop(), its OSError ignored. */
static void stop_model(void)
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
}

/* select() waits until the AI core confirms. Not confirmed: a screen
 * asks for RESET instead of failing. *started says whether it runs. */
static bento_status_t start_model(const hmi_t *hmi, bool *started)
{
    hold_text_t text;
    const char *reason = "edge_ai: select not confirmed";
    bento_status_t status = bento_edgeai_select_start(hmi->model);

    if (status == BENTO_OK) {
        status = finish_op();
    } else {
        reason = "edge_ai: select send failed";
    }
    *started = status == BENTO_OK;
    if ((status != BENTO_OK) && (status != BENTO_E_CANCELLED)) {
        (void)printf("select failed: %s\n", reason);
        text.count = 0U;
        add_line(&text, "เปิดโมเดลไม่สำเร็จ");
        add_line(&text, reason);
        add_line(&text, "กดปุ่ม RESET บนบอร์ด");
        add_line(&text, "แล้วกด Run ใหม่อีกครั้ง");
        status = hold(hmi->ui, &text, COLOR_BAD);
    }
    return status;
}

/* ---- the network ---- */

static bool team_ok(void)
{
    bool digits = true;
    uint32_t i;

    for (i = TEAM_PREFIX_LEN; i < (uint32_t)strlen(TEAM); i++) {
        digits = digits && (TEAM[i] >= '0') && (TEAM[i] <= '9');
    }
    return (strlen(TEAM) == (size_t)TEAM_LEN) &&
           (strncmp(TEAM, "team", TEAM_PREFIX_LEN) == 0) && digits &&
           (strcmp(TEAM, "team00") != 0);
}

/* wifi.connect(WIFI_SSID, WIFI_PASS), waited for. */
static bento_status_t connect_wifi(bool *joined)
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
            status = pause_ms(NET_POLL_MS);
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* Stop: give up */
    }
    *joined = (status == BENTO_OK) && (answer == BENTO_OK);
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* The WiFi the board already joined comes first; it connects itself
 * only once WIFI_SSID is set. */
static bento_status_t wifi_up(const hmi_t *hmi, bool *joined)
{
    bento_status_t status = BENTO_OK;
    int32_t poll;

    *joined = false;
    for (poll = 0;
         (poll < ((int32_t)WIFI_WAIT_S * (int32_t)WIFI_POLLS_PER_S)) &&
         !*joined && (status == BENTO_OK);
         poll++) {
        if (bento_wifi_is_connected(joined) != BENTO_OK) {
            *joined = false; /* an unanswered question is not joined */
        }
        if (!*joined) {
            status = pause_ms(WIFI_POLL_MS);
        }
    }
    if ((status == BENTO_OK) && !*joined && (WIFI_SSID[0] == '<')) {
        say(hmi, "No WiFi - set WIFI_SSID (offline)", COLOR_WARN);
    } else if ((status == BENTO_OK) && !*joined) {
        say(hmi, "Connecting WiFi ...", COLOR_WARN);
        status = connect_wifi(joined);
    } else {
        /* joined already, or Stop */
    }
    return status;
}

/* mqtt.connect(); the handle is kept for the disconnect in any case. */
static bento_status_t connect_broker(hmi_t *hmi, bool *connected)
{
    bento_mqtt_config_t config;
    bento_status_t answer = BENTO_E_IN_PROGRESS;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = MQTT_PORT;
    config.keepalive_s = KEEPALIVE_S;
    config.client_id = hmi->client_id;
    config.client_id_len = (uint32_t)strlen(hmi->client_id);
    status = bento_mqtt_open_start(&config, &hmi->handle);
    hmi->has_handle = status == BENTO_OK;
    while ((status == BENTO_OK) && (answer == BENTO_E_IN_PROGRESS)) {
        answer = bento_mqtt_open_poll(hmi->handle);
        if (answer == BENTO_E_IN_PROGRESS) {
            status = pause_ms(NET_POLL_MS);
        }
    }
    *connected = (status == BENTO_OK) && (answer == BENTO_OK);
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* The broker, once the WiFi is up; the note says how it went. */
static bento_status_t join_broker(hmi_t *hmi)
{
    char text[TEXT_CHARS];
    bento_status_t status;

    say(hmi, "Connecting broker ...", COLOR_WARN);
    status = connect_broker(hmi, &hmi->online);
    if ((status == BENTO_OK) && hmi->online) {
        (void)snprintf(text, sizeof(text), "Online: %s", hmi->topic);
        say(hmi, text, COLOR_OK);
    } else if (status == BENTO_OK) {
        say(hmi, "Broker failed - offline", COLOR_BAD);
    } else {
        /* Stop */
    }
    return status;
}

static bento_status_t go_online(hmi_t *hmi)
{
    bool joined = false;
    bento_status_t status = BENTO_OK;

    hmi->online = false;
    if (!team_ok()) {
        say(hmi, "Set TEAM first (team01-team99) - offline",
            COLOR_WARN);
    } else {
        status = wifi_up(hmi, &joined);
    }
    if ((status == BENTO_OK) && joined) {
        status = join_broker(hmi);
    }
    return status;
}

static bool broker_connected(const hmi_t *hmi)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;

    return (bento_mqtt_state(hmi->handle, &state) == BENTO_OK) &&
           (state == BENTO_MQTT_STATE_CONNECTED);
}

/* ---- the rule and the state ---- */

/* Adds one flag to the window of the latest results, dropping the
 * oldest when full, and answers how many are set. */
static int32_t push_flag(hmi_t *hmi, bool flag)
{
    int32_t hits = 0;
    int32_t i;

    if (hmi->hist_count == WINDOW_N) {
        for (i = 1; i < WINDOW_N; i++) {
            hmi->hist[i - 1] = hmi->hist[i];
        }
        hmi->hist_count--;
    }
    hmi->hist[hmi->hist_count] = flag ? 1U : 0U;
    hmi->hist_count++;
    for (i = 0; i < hmi->hist_count; i++) {
        hits += (int32_t)hmi->hist[i];
    }
    return hits;
}

/* The k-of-n rule: confirmed at CONFIRM_N hits in the window, and it
 * stays confirmed while any hit remains. */
static void alert_rule(hmi_t *hmi, bool danger)
{
    const int32_t hits = push_flag(hmi, danger);

    hmi->alerting =
        (hits >= CONFIRM_N) || (hmi->alerting && (hits > 0));
}

/* The next state. act: the main button was tapped. *refused: a reset
 * was asked while the danger is still there. */
static panel_state_t step(panel_state_t state, bool alerting, bool act,
                          bool manual, bool *refused)
{
    panel_state_t next = STATE_ACKNOWLEDGED;

    *refused = false;
    if (state == STATE_NORMAL) {
        next = alerting ? STATE_ALERT : STATE_NORMAL;
    } else if (state == STATE_ALERT) {
        /* a danger that went away still alerts until someone takes it */
        next = act ? STATE_ACKNOWLEDGED : STATE_ALERT;
    } else if (!alerting && (act || !manual)) {
        next = STATE_NORMAL;
    } else {
        *refused = act && alerting; /* still detected: no reset yet */
    }
    return next;
}

/* Every AI result in one JSON line, as snapshot() of session 11. */
static void snapshot(const hmi_t *hmi, char *line, uint32_t capacity)
{
    static const char *const state_names[STATE_COUNT] = {
        "normal", "alert", "ack"};

    (void)snprintf(
        line, capacity,
        "{\"id\": \"%s\", \"kit\": \"%s\", \"model\": \"%s\", "
        "\"label\": \"%s\", \"conf\": %" PRId32
        ", \"state\": \"%s\", \"events\": %" PRId32 ", \"n\": %" PRIu32
        "}",
        TEAM, KIT, hmi->model_name, hmi->label, hmi->conf,
        state_names[hmi->state], hmi->events, hmi->sequence);
}

/* ---- the loop ---- */

/* Whether the main button was tapped in this round's events. */
static bool key_tapped(const hmi_t *hmi)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bool tapped = false;

    if (bento_ui_poll(hmi->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U; /* ui.poll() answers no events when it fails */
    }
    for (i = 0U; i < count; i++) {
        if ((events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) &&
            (events[i].id == hmi->key)) {
            tapped = true;
        }
    }
    return tapped;
}

/* The danger class's own score, not only the winning label. */
static int32_t danger_score(const hmi_t *hmi,
                            const bento_edgeai_result_t *result,
                            int32_t pct)
{
    int32_t score = is_danger(hmi->kit, hmi->label) ? pct : 0;

    if ((hmi->danger_class >= 0) &&
        (hmi->danger_class < (int32_t)result->class_count) &&
        (hmi->danger_class < (int32_t)BENTO_EDGEAI_MAX_CLASSES)) {
        score = (int32_t)result->score_pct[hmi->danger_class];
    }
    return score;
}

/* A new result of this model feeds the rule; true when there was one.
 * A link too slow for this round is no result, not a failure. */
static bool read_result(hmi_t *hmi)
{
    bento_edgeai_result_t result;
    bool fresh;

    bento_edgeai_result_init(&result);
    fresh =
        (bento_edgeai_result(&result) == BENTO_OK) &&
        result.has_verdict && (result.index == hmi->model) &&
        (!hmi->has_sequence || (result.seq != hmi->last_sequence)) &&
        (result.top < BENTO_EDGEAI_MAX_CLASSES);
    if (fresh) {
        const int32_t pct = (int32_t)result.score_pct[result.top];

        hmi->has_sequence = true;
        hmi->last_sequence = result.seq;
        if (bento_edgeai_label(result.index, result.top, hmi->label,
                               (uint32_t)sizeof(hmi->label),
                               NULL) != BENTO_OK) {
            hmi->label[0] = '\0';
        }
        alert_rule(hmi,
                   danger_score(hmi, &result, pct) >= hmi->conf_min);
        hmi->conf = pct;
        hmi->sequence = result.seq;
    }
    return fresh;
}

/* A new result or a tap moves the state. The refusal note does not use
 * say(): its poll would swallow the next tap. */
static void update_state(hmi_t *hmi, bool act)
{
    const panel_state_t was = hmi->state;
    bool refused = false;

    hmi->state =
        step(was, hmi->alerting, act, KITS[hmi->kit].manual, &refused);
    if (refused) {
        set_text_color(hmi->ui, hmi->note_label,
                       "Still detected - cannot reset yet", COLOR_WARN);
    }
    if ((hmi->state != was) && (hmi->state == STATE_ALERT)) {
        hmi->events++;
    }
    if (act || (hmi->state != was)) {
        draw_state(hmi);
    }
}

static bool key_changed(const hmi_t *hmi)
{
    const sent_key_t *key = &hmi->sent_key;

    return !key->known || (strcmp(key->label, hmi->label) != 0) ||
           (key->state != hmi->state) || (key->events != hmi->events);
}

/* Far enough apart, and something changed or the heartbeat is due;
 * nothing goes before the first result. */
static void send_if_due(hmi_t *hmi, uint32_t now_ms)
{
    const int32_t since_ms =
        bento_time_ticks_diff(now_ms, hmi->sent_ms);
    const bool due = (since_ms >= GAP_MS) &&
                     (key_changed(hmi) || (since_ms >= ALIVE_EVERY_MS));

    if ((strcmp(hmi->label, "-") != 0) && due) {
        hmi->messages++;
        snapshot(hmi, hmi->last, (uint32_t)sizeof(hmi->last));
        hmi->has_last = true;
        (void)printf("%s\n", hmi->last);
        if (hmi->online &&
            (bento_mqtt_publish(
                 hmi->handle, hmi->topic, (uint32_t)strlen(hmi->topic),
                 (const uint8_t *)hmi->last,
                 (uint32_t)strlen(hmi->last), 0U, 0U) == BENTO_OK)) {
            hmi->reached++;
        }
        hmi->sent_ms = now_ms;
        hmi->sent_key.known = true;
        (void)snprintf(hmi->sent_key.label, sizeof(hmi->sent_key.label),
                       "%s", hmi->label);
        hmi->sent_key.state = hmi->state;
        hmi->sent_key.events = hmi->events;
    }
}

/* Only the lamps that changed are sent. */
static void draw_dots(hmi_t *hmi)
{
    int32_t i;

    for (i = 0; i < WINDOW_N; i++) {
        const int32_t value =
            (i < hmi->hist_count) ? (int32_t)hmi->hist[i] : 0;

        if (value != hmi->lit[i]) {
            (void)bento_ui_set_value(hmi->ui, hmi->dots[i], value);
            hmi->lit[i] = value;
        }
    }
}

/* The message count and the latest JSON in three 50-byte lines. */
static void draw_last_message(hmi_t *hmi)
{
    char text[TEXT_CHARS];
    const uint32_t length = (uint32_t)strlen(hmi->last);
    uint32_t i;

    (void)snprintf(text, sizeof(text),
                   "messages %" PRId32 " | reached broker %" PRId32,
                   hmi->messages, hmi->reached);
    set_text(hmi->ui, hmi->sent_label, text);
    for (i = 0U; i < (uint32_t)JSON_LINES; i++) {
        const uint32_t from = i * (uint32_t)JSON_PIECE;

        if (from < length) {
            (void)snprintf(text, sizeof(text), "%.*s", JSON_PIECE,
                           &hmi->last[from]);
        } else {
            (void)snprintf(text, sizeof(text), " ");
        }
        set_text(hmi->ui, hmi->json_label[i], text);
    }
    hmi->has_last = false;
}

/* Every DRAW_MS: the blink, the raw line, the lamps and the latest
 * message. */
static void draw_round(hmi_t *hmi, uint32_t now_ms)
{
    char text[TEXT_CHARS];

    if (bento_time_ticks_diff(now_ms, hmi->drawn_ms) >= DRAW_MS) {
        hmi->drawn_ms = now_ms;
        hmi->blink = !hmi->blink;
        if (hmi->state == STATE_ALERT) {
            draw_state(hmi);
        }
        (void)snprintf(text, sizeof(text),
                       "ผลดิบ: %s  %" PRId32 "%%  เหตุ %" PRId32 " ครั้ง",
                       hmi->label, hmi->conf, hmi->events);
        set_text(hmi->ui, hmi->raw_label, text);
        draw_dots(hmi);
        if (hmi->has_last) {
            draw_last_message(hmi);
        }
    }
}

/* One round of the main loop. */
static bento_status_t run_once(hmi_t *hmi)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    const bool act = key_tapped(hmi);
    const bool fresh = read_result(hmi);

    if (fresh || act) {
        update_state(hmi, act);
    }
    send_if_due(hmi, now_ms);
    if (hmi->online && !broker_connected(hmi)) {
        hmi->online = false;
        show_link(hmi, false);
        say(hmi, "Network lost - AI keeps running", COLOR_WARN);
    }
    draw_round(hmi, now_ms);
    return pause_ms(LOOP_MS);
}

/* Online if it can be, the model started, then RUN_S seconds of the
 * loop; the model is stopped and the broker link closed whatever ended
 * it. */
static bento_status_t run_hmi(hmi_t *hmi)
{
    bool started = false;
    bento_status_t status = go_online(hmi);

    if (status == BENTO_OK) {
        show_link(hmi, hmi->online);
        set_text(hmi->ui, hmi->raw_label, "กำลังโหลดโมเดล รอสักครู่");
        poll_drop(hmi->ui);
        status = start_model(hmi, &started);
    }
    hmi->start_ms = bento_time_ticks_ms();
    while (
        (status == BENTO_OK) && started &&
        (bento_time_ticks_diff(bento_time_ticks_ms(), hmi->start_ms) <
         ((int32_t)RUN_S * (int32_t)MS_PER_S))) {
        status = run_once(hmi);
    }
    stop_model();
    if (hmi->has_handle) {
        (void)bento_mqtt_close(hmi->handle); /* an OSError is passed */
    }
    return status;
}

/* The client id ends with the clock's low bits, so a second board of
 * one team does not throw the first off the broker. */
static void name_topics(hmi_t *hmi)
{
    (void)snprintf(hmi->client_id, sizeof(hmi->client_id),
                   "bento-ai-%s-%04" PRIx32, TEAM,
                   bento_time_ticks_ms() & (uint32_t)CLOCK_MASK);
    (void)snprintf(hmi->topic, sizeof(hmi->topic), "bento-aiot/%s/ai",
                   TEAM);
}

/* The model found: its danger class, its threshold, and the screen's
 * first state; then the run. */
static bento_status_t start_hmi(hmi_t *hmi)
{
    char text[TEXT_CHARS];

    hmi->danger_class = danger_class_of(hmi);
    hmi->conf_min = conf_min_for(hmi->model_name);
    (void)snprintf(text, sizeof(text), "%s  AI: %s",
                   KITS[hmi->kit].title, hmi->model_name);
    set_text(hmi->ui, hmi->model_label, text);
    hmi->sent_ms = bento_time_ticks_ms();
    hmi->drawn_ms = hmi->sent_ms;
    draw_state(hmi);
    return run_hmi(hmi);
}

static bento_status_t run_kit(hmi_t *hmi, bool in_emulator)
{
    bool found = false;
    bento_status_t status = BENTO_OK;

    hmi->kit = find_kit();
    if (hmi->kit == NO_KIT) {
        status = hold_bad_kit(hmi->ui);
    } else if (!in_emulator) {
        status = find_model(hmi, &found);
    } else {
        found = false; /* the Emulator: no model */
    }
    if ((status == BENTO_OK) && (hmi->kit != NO_KIT) && !found) {
        status = hold_no_model(hmi, in_emulator);
    } else if ((status == BENTO_OK) && found) {
        status = start_hmi(hmi);
    } else {
        /* a bad kit was shown, or an OSError from edge_ai.models() */
    }
    return status;
}

int main(void)
{
    hmi_t *hmi = &g_hmi;
    bool supported = false;
    bool in_emulator = false;
    bento_status_t status;

    (void)memset(hmi, 0, sizeof(*hmi));
    hmi->ui = bento_runtime_get_ui();
    hmi->state = STATE_NORMAL;
    hmi->danger_class = NO_CLASS;
    hmi->kit = NO_KIT;
    (void)snprintf(hmi->model_name, sizeof(hmi->model_name), "-");
    (void)snprintf(hmi->label, sizeof(hmi->label), "-");
    name_topics(hmi);
    status = bento_edgeai_supported(&supported);
    in_emulator = status == BENTO_E_UNSUPPORTED;
    if (in_emulator) {
        print_emulator_note();
        status = BENTO_OK; /* on as if no model were on the board */
    }
    if ((status == BENTO_OK) && !supported && !in_emulator) {
        status = BENTO_E_UNSUPPORTED; /* import edge_ai fails */
    }
    if (status == BENTO_OK) {
        status = build_screen(hmi);
    }
    if (status == BENTO_OK) {
        status = run_kit(hmi, in_emulator);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
