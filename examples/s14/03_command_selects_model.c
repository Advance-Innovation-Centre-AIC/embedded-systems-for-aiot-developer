/*
 * 03_command_selects_model.c -- แอปสั่งเปลี่ยนโมเดลหรือหยุด ผ่าน MQTT อย่างปลอดภัย
 * (an app picks or stops the model over MQTT, safely)
 *
 * บอร์ด subscribe หัวข้อ .../ai/cmd แล้วถามข้อความในลูปหลัก คำสั่ง
 * {"cmd":"select","model":"HumanActivity"} เปลี่ยนโมเดล {"cmd":"stop"} หยุด
 * {"cmd":"run"} รันต่อ ทำตามเฉพาะชื่อใน ALLOWED ที่มีบนบอร์ดจริง และรับคำสั่ง
 * ห่างกันอย่างน้อย 3 วินาที เปลี่ยนเสร็จส่งสถานะที่ .../ai ทันที จอบอกโมเดลที่รัน
 * ผลล่าสุด และคำสั่งล่าสุด (ทำ หรือไม่ทำเพราะอะไร) แก้ TEAM ก่อนรัน ไม่แก้ =
 * ทำงานออฟไลน์ พิมพ์ JSON ลง Console แทน
 *
 * The board subscribes to .../ai/cmd and asks for a message in its main
 * loop. {"cmd":"select","model":"HumanActivity"} picks a model,
 * {"cmd":"stop"} stops it and {"cmd":"run"} runs it again. Only names
 * in ALLOWED that are really on the board are obeyed, and commands
 * closer than 3 seconds apart are ignored. After each change the state
 * goes to .../ai at once. The screen shows the running model, the
 * latest result and the latest command (done, or ignored and why).
 * Set TEAM first; without it the program runs offline and prints the
 * JSON lines on the console instead.
 *
 * Twin of 03_command_selects_model.py
 * (embedded-systems-for-aiot-developer/examples/s14/
 *  03_command_selects_model.py), written new by hand.
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - json.loads() is a small reader for what this program uses: one
 *    JSON object whose "cmd" and "model" strings are read (the last
 *    key wins, as in a dict); nested arrays and objects are skipped by
 *    matching brackets, not checked byte by byte; \u escapes become
 *    UTF-8 without joining surrogate pairs; invalid UTF-8 is not
 *    refused (raw.decode() would refuse it);
 *  - json.dumps() is one snprintf with the keys in the order the dict
 *    is written (MicroPython's dict may print another order); names
 *    and labels are printed as they are, without JSON escapes;
 *  - the board's mqtt module keeps one waiting message; the C client
 *    keeps BENTO_MQTT_RING_SLOTS, so a burst of commands is read one by
 *    one (the 3 s rule still ignores the extra ones);
 *  - known_models() is a table over ALLOWED (index of the last row
 *    with each name, as the dict keeps it); sorted() is a small sort;
 *  - mqtt.connect(), subscribe() and wifi.connect() answer a status:
 *    any failure is the False answer, and the binding's OSError text
 *    ("network: ...") is not printed;
 *  - select() and stop() wait inside the binding; here they are
 *    started and polled, and the OSError text is the binding's words
 *    ("edge_ai: select send failed", "edge_ai: select not confirmed");
 *  - a result's conf is score_pct[top] in percent; its label is
 *    bento_edgeai_label() ("" when the board cannot name it);
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
static const char TEAM[] = "teamXX";
/* BROKER: test.mosquitto.org is the spare */
static const char BROKER[] = "broker.hivemq.com";
/* The rules and the rhythm, as the original sets them. */
enum {
    CMD_GAP_MS = 3000,     /* commands closer than this are not done */
    MAX_CMD_BYTES = 200,   /* longer messages are not read */
    ALIVE_EVERY_MS = 2000, /* sent again this often, change or not */
    GAP_MS = 200,          /* never sent closer than this */
    WIFI_WAIT_S = 5,       /* wait this long for the saved WiFi */
    DRAW_MS = 500,         /* the screen is redrawn this often */
    RUN_S = 600            /* the program runs this many seconds */
};
/* ---- end of settings ---- */

/* The note for the BENTO Emulator, whose C runtime has no Edge AI yet. */
static const char EMULATOR_NOTE[] =
    "Edge AI จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";

/* Where that note sits on the screen. */
enum {
    EMULATOR_NOTE_X = 12,
    EMULATOR_NOTE_Y = 340,
    EMULATOR_NOTE_PX = 20,
    EMULATOR_NOTE_COLOR = 0xF5A623
};

/* The rules, the network and the screen's rhythm. */
enum {
    WIFI_POLLS_PER_S = 10,
    WIFI_POLL_MS = 100,
    MS_PER_S = 1000,
    LOOP_MS = 50,
    SETTLE_MS = 150, /* let the cleared screen settle */
    OP_POLL_MS = 100,
    NET_POLL_MS = 100,
    MQTT_PORT = 1883,
    KEEPALIVE_S = 60,
    CLOCK_MASK =
        0xFFFF, /* the client id's suffix: the clock's low bits */
    ALLOWED_COUNT = 3,
    TEAM_PREFIX_LEN = 4,
    TEAM_LEN = 6,
    FIELD_CHARS = 16,
    TOPIC_CHARS = 64,
    COMMAND_TOPIC_CHARS = 68, /* the topic, "/cmd" and the NUL */
    ID_CHARS = 24,
    TEXT_CHARS = 160,
    LINE_CHARS = 256
};

/* JSON escapes and UTF-8. */
enum {
    HEX_DIGITS = 4,
    HEX_BASE = 16,
    HEX_LETTER = 10, /* the value of hex digit a */
    UTF8_ONE_LIMIT = 0x80,
    UTF8_TWO_LIMIT = 0x800,
    UTF8_LEAD_TWO = 0xC0,
    UTF8_LEAD_THREE = 0xE0,
    UTF8_FOLLOW = 0x80,
    UTF8_FOLLOW_MASK = 0x3F,
    UTF8_FOLLOW_BITS = 6
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
    MARGIN_X = 12,
    TITLE_Y = 6,
    MODEL_Y = 48,
    RESULT_Y = 88,
    CAPTION_Y = 150,
    COMMAND_Y = 174,
    COUNT_Y = 214,
    NOTE_Y = 300,
    SMALL_PX = 16,
    TEXT_PX = 20,
    TITLE_PX = 24,
    BIG_PX = 28
};

/* Only these names are obeyed. */
static const char *const ALLOWED[ALLOWED_COUNT] = {
    "AnomalousVibration", "HumanActivity", "Motion Detection"};

/* What a command asks for. */
typedef enum {
    COMMAND_NONE = 0,
    COMMAND_MODEL = 1,
    COMMAND_STOP = 2,
    COMMAND_RUN = 3
} command_kind_t;

/* A parsed command: what to do, the model (an ALLOWED index), or why
 * it is not done. */
typedef struct {
    command_kind_t kind;
    int32_t model;
    const char *reason;
} command_t;

/* What the JSON reader keeps of an object. */
typedef struct {
    bool cmd_is_text;
    bool model_is_text;
    char cmd[FIELD_CHARS];
    char model[BENTO_EDGEAI_NAME_LEN];
} fields_t;

/* A place in the bytes being read. */
typedef struct {
    const uint8_t *bytes;
    uint32_t length;
    uint32_t at;
} cursor_t;

/* How a message reads as JSON. */
typedef enum {
    JSON_INVALID = 0,
    JSON_OTHER = 1, /* valid JSON, but not an object */
    JSON_OBJECT = 2
} json_kind_t;

/* The widgets, the models, the network and the loop's memory. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t model_label;
    bento_ui_id_t result_label;
    bento_ui_id_t command_label;
    bento_ui_id_t count_label;
    bento_ui_id_t note_label;
    bool known[ALLOWED_COUNT];
    uint32_t index[ALLOWED_COUNT];
    int32_t name;
    command_t want;
    bool running;
    bool online;
    bool has_handle;
    uint32_t handle;
    bool has_sequence;
    uint32_t sequence;
    bool has_label;
    char label[BENTO_EDGEAI_LABEL_LEN];
    int32_t conf;
    bool has_sent_label;
    char sent_label[BENTO_EDGEAI_LABEL_LEN];
    int32_t sent;
    int32_t done;
    int32_t refused;
    bool has_shown;
    char shown[TEXT_CHARS];
    uint32_t command_ms;
    uint32_t start_ms;
    uint32_t sent_ms;
    uint32_t drawn_ms;
    char client_id[ID_CHARS];
    char topic[TOPIC_CHARS];
    char topic_command[COMMAND_TOPIC_CHARS];
    uint8_t payload[BENTO_MQTT_PAYLOAD_MAX];
} commander_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern commander_t g_commander;
commander_t g_commander;

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

/* ---- the JSON reader ---- */

static int32_t peek(const cursor_t *cursor)
{
    return (cursor->at < cursor->length)
               ? (int32_t)cursor->bytes[cursor->at]
               : -1;
}

static void skip_space(cursor_t *cursor)
{
    int32_t byte = peek(cursor);

    while ((byte == (int32_t)' ') || (byte == (int32_t)'\t') ||
           (byte == (int32_t)'\n') || (byte == (int32_t)'\r')) {
        cursor->at++;
        byte = peek(cursor);
    }
}

static bool take(cursor_t *cursor, char wanted)
{
    const bool match = peek(cursor) == (int32_t)wanted;

    if (match) {
        cursor->at++;
    }
    return match;
}

/* Adds one byte to a text; a text that does not fit becomes "", which
 * matches no command and no model. */
static void put_byte(char *text, uint32_t capacity, uint32_t *length,
                     uint32_t byte)
{
    if ((*length + 1U) < capacity) {
        text[*length] = (char)byte;
        (*length)++;
        text[*length] = '\0';
    } else {
        text[0] = '\0';
        *length = capacity;
    }
}

static int32_t hex_value(int32_t byte)
{
    int32_t value = -1;

    if ((byte >= (int32_t)'0') && (byte <= (int32_t)'9')) {
        value = byte - (int32_t)'0';
    } else if ((byte >= (int32_t)'a') && (byte <= (int32_t)'f')) {
        value = (byte - (int32_t)'a') + (int32_t)HEX_LETTER;
    } else if ((byte >= (int32_t)'A') && (byte <= (int32_t)'F')) {
        value = (byte - (int32_t)'A') + (int32_t)HEX_LETTER;
    } else {
        value = -1;
    }
    return value;
}

/* A code point below 0x10000 as its UTF-8 bytes. */
static void put_utf8(char *text, uint32_t capacity, uint32_t *length,
                     uint32_t code)
{
    const uint32_t follow = (uint32_t)UTF8_FOLLOW;
    const uint32_t mask = (uint32_t)UTF8_FOLLOW_MASK;
    const uint32_t bits = (uint32_t)UTF8_FOLLOW_BITS;

    if (code < (uint32_t)UTF8_ONE_LIMIT) {
        put_byte(text, capacity, length, code);
    } else if (code < (uint32_t)UTF8_TWO_LIMIT) {
        put_byte(text, capacity, length,
                 (uint32_t)UTF8_LEAD_TWO | (code >> bits));
        put_byte(text, capacity, length, follow | (code & mask));
    } else {
        put_byte(text, capacity, length,
                 (uint32_t)UTF8_LEAD_THREE | (code >> (bits + bits)));
        put_byte(text, capacity, length,
                 follow | ((code >> bits) & mask));
        put_byte(text, capacity, length, follow | (code & mask));
    }
}

/* A \uXXXX escape, as its UTF-8 bytes. */
static bool read_unicode(cursor_t *cursor, char *text,
                         uint32_t capacity, uint32_t *length)
{
    uint32_t code = 0U;
    bool ok = true;
    int32_t digit;

    for (digit = 0; (digit < HEX_DIGITS) && ok; digit++) {
        const int32_t value = hex_value(peek(cursor));

        ok = value >= 0;
        if (ok) {
            code = (code * (uint32_t)HEX_BASE) + (uint32_t)value;
            cursor->at++;
        }
    }
    if (ok) {
        put_utf8(text, capacity, length, code);
    }
    return ok;
}

/* One escape after a backslash. */
static bool read_escape(cursor_t *cursor, char *text, uint32_t capacity,
                        uint32_t *length)
{
    static const char escaped[] = "\"\\/bfnrt";
    static const char meaning[] = "\"\\/\b\f\n\r\t";
    const int32_t byte = peek(cursor);
    bool ok = false;
    uint32_t i;

    cursor->at++;
    if (byte == (int32_t)'u') {
        ok = read_unicode(cursor, text, capacity, length);
    }
    for (i = 0U; (i < (uint32_t)(sizeof(escaped) - 1U)) && !ok; i++) {
        if (byte == (int32_t)escaped[i]) {
            put_byte(text, capacity, length, (uint8_t)meaning[i]);
            ok = true;
        }
    }
    return ok;
}

/* A JSON string into text; the cursor stands on its opening quote. */
static bool read_string(cursor_t *cursor, char *text, uint32_t capacity)
{
    uint32_t length = 0U;
    bool ok = take(cursor, '"');
    bool closed = false;

    text[0] = '\0';
    while (ok && !closed) {
        const int32_t byte = peek(cursor);

        if (byte < 0) {
            ok = false;
        } else if (byte == (int32_t)'"') {
            cursor->at++;
            closed = true;
        } else if (byte == (int32_t)'\\') {
            cursor->at++;
            ok = read_escape(cursor, text, capacity, &length);
        } else {
            put_byte(text, capacity, &length, (uint32_t)byte);
            cursor->at++;
        }
    }
    return ok;
}

static bool is_number_byte(int32_t byte)
{
    static const char number[] = "+-.0123456789eE";

    return (byte > 0) && (strchr(number, byte) != NULL);
}

/* Whether the bytes at the cursor spell this word. */
static bool spells(const cursor_t *cursor, const char *word)
{
    const uint32_t size = (uint32_t)strlen(word);
    bool same = (cursor->length - cursor->at) >= size;
    uint32_t i;

    for (i = 0U; (i < size) && same; i++) {
        same = cursor->bytes[cursor->at + i] == (uint8_t)word[i];
    }
    return same;
}

/* true, false, null or a number. */
static bool read_scalar(cursor_t *cursor)
{
    static const char *const words[] = {"true", "false", "null"};
    const uint32_t first = cursor->at;
    bool matched = false;
    uint32_t i;

    for (i = 0U;
         (i < (uint32_t)(sizeof(words) / sizeof(words[0]))) && !matched;
         i++) {
        matched = spells(cursor, words[i]);
        if (matched) {
            cursor->at = first + (uint32_t)strlen(words[i]);
        }
    }
    while (!matched && is_number_byte(peek(cursor))) {
        cursor->at++;
    }
    return cursor->at > first;
}

/* One step inside a nested value: a whole string, or one byte that
 * may open or close a bracket. */
static bool skip_nested_step(cursor_t *cursor, int32_t *depth)
{
    char scratch[FIELD_CHARS];
    const int32_t byte = peek(cursor);
    bool ok = true;

    if (byte < 0) {
        ok = false;
    } else if (byte == (int32_t)'"') {
        ok = read_string(cursor, scratch, (uint32_t)sizeof(scratch));
    } else if ((byte == (int32_t)'[') || (byte == (int32_t)'{')) {
        (*depth)++;
        cursor->at++;
    } else if ((byte == (int32_t)']') || (byte == (int32_t)'}')) {
        (*depth)--;
        cursor->at++;
    } else {
        cursor
            ->at++; /* any other byte inside the brackets is skipped */
    }
    return ok;
}

/* A nested array or object, skipped by matching its brackets. */
static bool skip_nested(cursor_t *cursor)
{
    int32_t depth = 0;
    bool ok = true;

    do {
        ok = skip_nested_step(cursor, &depth);
    } while (ok && (depth > 0));
    return ok;
}

/* Any value that is not a string the program reads. */
static bool skip_value(cursor_t *cursor)
{
    char scratch[FIELD_CHARS];
    const int32_t byte = peek(cursor);
    bool ok = false;

    if (byte == (int32_t)'"') {
        ok = read_string(cursor, scratch, (uint32_t)sizeof(scratch));
    } else if ((byte == (int32_t)'[') || (byte == (int32_t)'{')) {
        ok = skip_nested(cursor);
    } else {
        ok = read_scalar(cursor);
    }
    return ok;
}

/* A value into a field when it is a string, else skipped; the last key
 * wins, as in a dict. */
static bool read_field(cursor_t *cursor, char *text, uint32_t capacity,
                       bool *is_text)
{
    bool ok = false;

    *is_text = peek(cursor) == (int32_t)'"';
    if (*is_text) {
        ok = read_string(cursor, text, capacity);
    } else {
        ok = skip_value(cursor);
    }
    return ok;
}

/* One "key": value pair of the object. */
static bool read_member(cursor_t *cursor, fields_t *fields)
{
    char key[FIELD_CHARS];
    bool ok = read_string(cursor, key, (uint32_t)sizeof(key));

    skip_space(cursor);
    ok = ok && take(cursor, ':');
    skip_space(cursor);
    if (ok && (strcmp(key, "cmd") == 0)) {
        ok = read_field(cursor, fields->cmd,
                        (uint32_t)sizeof(fields->cmd),
                        &fields->cmd_is_text);
    } else if (ok && (strcmp(key, "model") == 0)) {
        ok = read_field(cursor, fields->model,
                        (uint32_t)sizeof(fields->model),
                        &fields->model_is_text);
    } else if (ok) {
        ok = skip_value(cursor);
    } else {
        /* no colon: not JSON */
    }
    skip_space(cursor);
    return ok;
}

/* The members of an object; the cursor stands after its "{". */
static bool read_members(cursor_t *cursor, fields_t *fields)
{
    bool ok = true;
    bool more = true;

    skip_space(cursor);
    if (take(cursor, '}')) {
        more = false;
    }
    while (ok && more) {
        ok = read_member(cursor, fields);
        if (ok && !take(cursor, ',')) {
            ok = take(cursor, '}');
            more = false;
        }
        skip_space(cursor);
    }
    return ok;
}

/* How the message reads: an object (its fields kept), another JSON
 * value, or not JSON at all. */
static json_kind_t read_json(const uint8_t *bytes, uint32_t length,
                             fields_t *fields)
{
    cursor_t cursor = {bytes, length, 0U};
    json_kind_t kind = JSON_OTHER;
    bool ok;

    (void)memset(fields, 0, sizeof(*fields));
    skip_space(&cursor);
    if (take(&cursor, '{')) {
        kind = JSON_OBJECT;
        ok = read_members(&cursor, fields);
    } else {
        ok = skip_value(&cursor);
    }
    skip_space(&cursor);
    if (!ok || (cursor.at != cursor.length)) {
        kind = JSON_INVALID;
    }
    return kind;
}

/* The ALLOWED index of a name that is on the board, or -1. */
static int32_t known_index(const commander_t *commander,
                           const char *name)
{
    int32_t found = -1;
    int32_t i;

    for (i = 0; i < ALLOWED_COUNT; i++) {
        if (commander->known[i] && (strcmp(ALLOWED[i], name) == 0)) {
            found = i;
        }
    }
    return found;
}

/* The command an object's fields ask for. */
static command_t command_of(const commander_t *commander,
                            const fields_t *fields)
{
    command_t command = {COMMAND_NONE, -1, "unknown command"};
    const char *cmd = fields->cmd_is_text ? fields->cmd : "";

    if (strcmp(cmd, "select") == 0) {
        /* the type is checked first: a model that is no string is
         * unknown */
        command.model = fields->model_is_text
                            ? known_index(commander, fields->model)
                            : -1;
        command.kind =
            (command.model >= 0) ? COMMAND_MODEL : COMMAND_NONE;
        command.reason = "unknown model";
    } else if (strcmp(cmd, "stop") == 0) {
        command.kind = COMMAND_STOP;
    } else if (strcmp(cmd, "run") == 0) {
        command.kind = COMMAND_RUN;
    } else {
        /* any other cmd, or none: unknown command */
    }
    return command;
}

/* What a message asks for, or why it is not done: a message from
 * anyone, so no shape of it may end the program. */
static command_t parse_command(const commander_t *commander,
                               uint32_t length, bool truncated)
{
    command_t command = {COMMAND_NONE, -1, "too long"};
    fields_t fields;
    json_kind_t kind = JSON_INVALID;

    if ((length <= (uint32_t)MAX_CMD_BYTES) && !truncated) {
        kind = read_json(commander->payload, length, &fields);
        command.reason = "not JSON";
    }
    if (kind == JSON_OTHER) {
        command.reason = "not a JSON object";
    } else if (kind == JSON_OBJECT) {
        command = command_of(commander, &fields);
    } else {
        /* too long, or not JSON */
    }
    return command;
}

/* ---- the screen ---- */

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                int32_t size_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = MARGIN_X;
    args.y = y;
    args.color = color;
    args.font_px = size_px;
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

/* The note line: its colour, its text, and a poll so it shows now. */
static void say(const commander_t *commander, const char *text,
                uint32_t color)
{
    (void)bento_ui_set_color(commander->ui, commander->note_label,
                             color);
    set_text(commander->ui, commander->note_label, text);
    poll_drop(commander->ui);
}

static bento_status_t build_screen(commander_t *commander)
{
    bento_ui_t *ui = commander->ui;
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(ui, "Commands pick the model", TITLE_Y,
                           COLOR_TEXT, TITLE_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, " ", MODEL_Y, COLOR_INFO, TEXT_PX,
                           &commander->model_label);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "-", RESULT_Y, COLOR_TEXT, BIG_PX,
                           &commander->result_label);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "last command", CAPTION_Y, COLOR_DIM,
                           SMALL_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "(none yet)", COMMAND_Y, COLOR_TEXT,
                           SMALL_PX, &commander->command_label);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, " ", COUNT_Y, COLOR_DIM, SMALL_PX,
                           &commander->count_label);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, " ", NOTE_Y, COLOR_WARN, SMALL_PX,
                           &commander->note_label);
    }
    if (status == BENTO_OK) {
        poll_drop(ui);
    }
    return status;
}

/* ---- the models ---- */

/* A row whose name is in ALLOWED is kept; a name seen twice keeps
 * its last row, as a dict does. */
static bool note_row(commander_t *commander, const char *name,
                     uint32_t row)
{
    bool allowed = false;
    int32_t i;

    for (i = 0; i < ALLOWED_COUNT; i++) {
        if (strcmp(name, ALLOWED[i]) == 0) {
            commander->known[i] = true;
            commander->index[i] = row;
            allowed = true;
        }
    }
    return allowed;
}

/* The ALLOWED names on the board now, each with its row. */
static bento_status_t known_models(commander_t *commander, bool *any)
{
    bento_edgeai_model_info_t info;
    int32_t count = 0;
    int32_t row;
    bento_status_t status = bento_edgeai_count(&count);

    *any = false;
    for (row = 0; (row < count) && (status == BENTO_OK); row++) {
        bento_edgeai_model_info_init(&info);
        status = bento_edgeai_model_info((uint32_t)row, &info);
        if ((status == BENTO_OK) &&
            note_row(commander, info.name, (uint32_t)row)) {
            *any = true;
        }
    }
    return status;
}

/* The smallest accepted name not printed yet, or -1. */
static int32_t next_in_order(const commander_t *commander,
                             const bool printed[ALLOWED_COUNT])
{
    int32_t least = -1;
    int32_t i;

    for (i = 0; i < ALLOWED_COUNT; i++) {
        const bool open = commander->known[i] && !printed[i];

        if (open &&
            ((least < 0) || (strcmp(ALLOWED[i], ALLOWED[least]) < 0))) {
            least = i;
        }
    }
    return least;
}

/* The accepted names as print() shows a sorted list of strings. */
static void print_known(const commander_t *commander)
{
    bool printed[ALLOWED_COUNT] = {false, false, false};
    const char *separator = "";
    int32_t least = next_in_order(commander, printed);

    (void)printf("models I accept: [");
    while (least >= 0) {
        printed[least] = true;
        (void)printf("%s'%s'", separator, ALLOWED[least]);
        separator = ", ";
        least = next_in_order(commander, printed);
    }
    (void)printf("]\n");
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
static void stop_ai(void)
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

/* select() waits until the AI core confirms; a refusal is shown, not
 * fatal. Only Stop ends the program from here. */
static bento_status_t select_model(commander_t *commander)
{
    char text[TEXT_CHARS];
    const char *reason = "edge_ai: select not confirmed";
    bento_status_t status =
        bento_edgeai_select_start(commander->index[commander->name]);

    if (status == BENTO_OK) {
        status = finish_op();
    } else {
        reason = "edge_ai: select send failed";
    }
    commander->running = status == BENTO_OK;
    if ((status != BENTO_OK) && (status != BENTO_E_CANCELLED)) {
        (void)snprintf(text, sizeof(text), "select failed: %s", reason);
        say(commander, text, COLOR_BAD);
        status = BENTO_OK;
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
static bento_status_t wifi_up(const commander_t *commander,
                              bool *joined)
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
        say(commander, "No WiFi - set WIFI_SSID (offline)", COLOR_WARN);
    } else if ((status == BENTO_OK) && !*joined) {
        say(commander, "Connecting WiFi ...", COLOR_WARN);
        status = connect_wifi(joined);
    } else {
        /* joined already, or Stop */
    }
    return status;
}

/* mqtt.connect() and subscribe(TOPIC_CMD); *ready when both answer
 * yes. The handle is kept for the disconnect in any case. */
static bento_status_t connect_broker(commander_t *commander,
                                     bool *ready)
{
    bento_mqtt_config_t config;
    bento_status_t answer = BENTO_E_IN_PROGRESS;
    bento_status_t status;
    uint8_t granted = 0U;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = MQTT_PORT;
    config.keepalive_s = KEEPALIVE_S;
    config.client_id = commander->client_id;
    config.client_id_len = (uint32_t)strlen(commander->client_id);
    status = bento_mqtt_open_start(&config, &commander->handle);
    commander->has_handle = status == BENTO_OK;
    while ((status == BENTO_OK) && (answer == BENTO_E_IN_PROGRESS)) {
        answer = bento_mqtt_open_poll(commander->handle);
        if (answer == BENTO_E_IN_PROGRESS) {
            status = pause_ms(NET_POLL_MS);
        }
    }
    *ready = (status == BENTO_OK) && (answer == BENTO_OK) &&
             (bento_mqtt_subscribe(
                  commander->handle, commander->topic_command,
                  (uint32_t)strlen(commander->topic_command), 0U,
                  &granted) == BENTO_OK);
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* The broker, once the WiFi is up; the note says how it went. */
static bento_status_t join_broker(commander_t *commander)
{
    char text[TEXT_CHARS];
    bento_status_t status;

    say(commander, "Connecting broker ...", COLOR_WARN);
    status = connect_broker(commander, &commander->online);
    if ((status == BENTO_OK) && commander->online) {
        (void)snprintf(text, sizeof(text), "Listening: %s",
                       commander->topic_command);
        say(commander, text, COLOR_OK);
    } else if (status == BENTO_OK) {
        say(commander, "Broker failed - offline (no commands)",
            COLOR_BAD);
    } else {
        /* Stop */
    }
    return status;
}

static bento_status_t go_online(commander_t *commander)
{
    bool joined = false;
    bento_status_t status = BENTO_OK;

    commander->online = false;
    if (!team_ok()) {
        say(commander, "Set TEAM first (team01-team99) - offline",
            COLOR_WARN);
    } else {
        status = wifi_up(commander, &joined);
    }
    if ((status == BENTO_OK) && joined) {
        status = join_broker(commander);
    }
    return status;
}

static bool broker_connected(const commander_t *commander)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;

    return (bento_mqtt_state(commander->handle, &state) == BENTO_OK) &&
           (state == BENTO_MQTT_STATE_CONNECTED);
}

/* ---- the loop ---- */

/* The command line and the counters on the screen. */
static void show_command(commander_t *commander,
                         const command_t *command)
{
    static const char *const words[] = {"", "model", "stop", "run"};
    char text[TEXT_CHARS];

    if (command->kind == COMMAND_MODEL) {
        (void)snprintf(text, sizeof(text), "do: model %s",
                       ALLOWED[command->model]);
    } else if (command->kind != COMMAND_NONE) {
        (void)snprintf(text, sizeof(text), "do: %s",
                       words[command->kind]);
    } else {
        (void)snprintf(text, sizeof(text), "ignored: %s",
                       command->reason);
    }
    set_text(commander->ui, commander->command_label, text);
    (void)snprintf(text, sizeof(text),
                   "commands done %" PRId32 " | ignored %" PRId32,
                   commander->done, commander->refused);
    set_text(commander->ui, commander->count_label, text);
}

/* A message from the app: done when allowed and not too soon. */
static void take_message(commander_t *commander, uint32_t now_ms)
{
    char topic[TOPIC_CHARS];
    bento_mqtt_msg_t message;

    bento_mqtt_msg_init(&message);
    if (bento_mqtt_poll(commander->handle, &message, topic,
                        (uint32_t)sizeof(topic), commander->payload,
                        (uint32_t)sizeof(commander->payload)) ==
        BENTO_OK) {
        command_t command = parse_command(
            commander, message.payload_len, message.truncated != 0U);

        if (bento_time_ticks_diff(now_ms, commander->command_ms) <
            CMD_GAP_MS) {
            command.kind = COMMAND_NONE;
            command.reason = "too soon";
        }
        if (command.kind != COMMAND_NONE) {
            commander->want = command;
            commander->command_ms = now_ms;
            commander->done++;
        } else {
            commander->refused++;
        }
        show_command(commander, &command);
    }
}

/* The wanted change, done in the main loop only; then the state is
 * sent at once. */
static bento_status_t do_wanted(commander_t *commander, uint32_t now_ms)
{
    char text[TEXT_CHARS];
    bento_status_t status = BENTO_OK;

    if (commander->want.kind == COMMAND_STOP) {
        stop_ai();
        commander->running = false;
    } else {
        if (commander->want.kind == COMMAND_MODEL) {
            commander->name = commander->want.model;
        }
        (void)snprintf(text, sizeof(text), "Loading %s ...",
                       ALLOWED[commander->name]);
        set_text(commander->ui, commander->model_label, text);
        poll_drop(commander->ui);
        status = select_model(commander);
    }
    commander->want.kind = COMMAND_NONE;
    commander->has_sequence = false;
    commander->has_label = false;
    commander->has_sent_label = false;
    (void)snprintf(text, sizeof(text), "%s%s",
                   commander->running ? "Running: " : "Stopped: ",
                   ALLOWED[commander->name]);
    set_text(commander->ui, commander->model_label, text);
    commander->sent_ms = bento_time_ticks_add(now_ms, -ALIVE_EVERY_MS);
    return status;
}

/* A new result of the running model is kept. */
static void read_result(commander_t *commander)
{
    bento_edgeai_result_t result;

    bento_edgeai_result_init(&result);
    if (commander->running &&
        (bento_edgeai_result(&result) == BENTO_OK) &&
        result.has_verdict &&
        (result.index == commander->index[commander->name]) &&
        (!commander->has_sequence ||
         (result.seq != commander->sequence)) &&
        (result.top < BENTO_EDGEAI_MAX_CLASSES)) {
        commander->has_sequence = true;
        commander->sequence = result.seq;
        commander->has_label = true;
        if (bento_edgeai_label(
                result.index, result.top, commander->label,
                (uint32_t)sizeof(commander->label), NULL) != BENTO_OK) {
            commander->label[0] = '\0';
        }
        commander->conf = (int32_t)result.score_pct[result.top];
    }
}

/* Far enough apart, and the label changed or the heartbeat is due. */
static bool should_send(bool changed, int32_t since_ms)
{
    return (since_ms >= GAP_MS) &&
           (changed || (since_ms >= ALIVE_EVERY_MS));
}

static bool label_changed(const commander_t *commander)
{
    bool changed = commander->has_label != commander->has_sent_label;

    if (!changed && commander->has_label) {
        changed = strcmp(commander->label, commander->sent_label) != 0;
    }
    return changed;
}

/* One JSON line on the console, and to the broker when online; "-" is
 * no result. The publish answer is not needed here. */
static void send_state(commander_t *commander)
{
    char line[LINE_CHARS];
    const bool has_text =
        commander->has_label && (commander->label[0] != '\0');

    commander->sent++;
    (void)snprintf(
        line, sizeof(line),
        "{\"id\": \"%s\", \"n\": %" PRId32
        ", \"model\": \"%s\", \"label\": \"%s\", \"conf\": %" PRId32
        "}",
        TEAM, commander->sent,
        commander->running ? ALLOWED[commander->name] : "",
        has_text ? commander->label : "-",
        has_text ? commander->conf : 0);
    (void)printf("%s\n", line);
    if (commander->online) {
        (void)bento_mqtt_publish(
            commander->handle, commander->topic,
            (uint32_t)strlen(commander->topic), (const uint8_t *)line,
            (uint32_t)strlen(line), 0U, 0U); /* an OSError is passed */
    }
}

/* The result line, at most every DRAW_MS and only when it changed. */
static void draw_result(commander_t *commander, uint32_t now_ms)
{
    char text[TEXT_CHARS];
    const bool has_text =
        commander->has_label && (commander->label[0] != '\0');

    if (has_text) {
        (void)snprintf(text, sizeof(text), "%s  %" PRId32 "%%",
                       commander->label, commander->conf);
    } else {
        (void)snprintf(text, sizeof(text), "-");
    }
    if ((!commander->has_shown ||
         (strcmp(text, commander->shown) != 0)) &&
        (bento_time_ticks_diff(now_ms, commander->drawn_ms) >=
         DRAW_MS)) {
        commander->drawn_ms = now_ms;
        commander->has_shown = true;
        (void)snprintf(commander->shown, sizeof(commander->shown), "%s",
                       text);
        set_text(commander->ui, commander->result_label, text);
    }
}

/* One pass of the main loop. */
static bento_status_t run_once(commander_t *commander)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    if (commander->online) {
        take_message(commander, now_ms);
    }
    if (commander->want.kind != COMMAND_NONE) {
        status = do_wanted(commander, now_ms);
    }
    if (status == BENTO_OK) {
        read_result(commander);
        if (should_send(
                label_changed(commander),
                bento_time_ticks_diff(now_ms, commander->sent_ms))) {
            send_state(commander);
            commander->sent_ms = now_ms;
            commander->has_sent_label = commander->has_label;
            (void)snprintf(commander->sent_label,
                           sizeof(commander->sent_label), "%s",
                           commander->label);
        }
        if (commander->online && !broker_connected(commander)) {
            commander->online = false;
            say(commander, "Network lost - AI keeps running",
                COLOR_WARN);
        }
        draw_result(commander, now_ms);
        poll_drop(commander->ui);
        status = pause_ms(LOOP_MS);
    }
    return status;
}

/* Online if it can be, then RUN_S seconds of the loop; the model is
 * stopped and the broker link closed whatever ended it. */
static bento_status_t run_commands(commander_t *commander)
{
    bento_status_t status;

    commander->command_ms =
        bento_time_ticks_add(bento_time_ticks_ms(), -CMD_GAP_MS);
    commander->start_ms = bento_time_ticks_ms();
    commander->sent_ms = commander->start_ms;
    commander->drawn_ms = commander->start_ms;
    status = go_online(commander);
    commander->start_ms = bento_time_ticks_ms();
    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(),
                                  commander->start_ms) <
            ((int32_t)RUN_S * (int32_t)MS_PER_S))) {
        status = run_once(commander);
    }
    stop_ai();
    if (commander->has_handle) {
        (void)bento_mqtt_close(
            commander->handle); /* an OSError is passed */
    }
    return status;
}

/* The client id ends with the clock's low bits, so two boards of one
 * team do not throw each other off the broker. */
static void name_topics(commander_t *commander)
{
    (void)snprintf(commander->client_id, sizeof(commander->client_id),
                   "bento-ai-%s-%04" PRIx32, TEAM,
                   bento_time_ticks_ms() & (uint32_t)CLOCK_MASK);
    (void)snprintf(commander->topic, sizeof(commander->topic),
                   "bento-aiot/%s/ai", TEAM);
    (void)snprintf(commander->topic_command,
                   sizeof(commander->topic_command), "%s/cmd",
                   commander->topic);
}

/* The first ALLOWED name on the board is the first model wanted. */
static void want_first(commander_t *commander)
{
    int32_t i;

    commander->name = -1;
    for (i = ALLOWED_COUNT - 1; i >= 0; i--) {
        if (commander->known[i]) {
            commander->name = i;
        }
    }
    commander->want.kind = COMMAND_MODEL;
    commander->want.model = commander->name;
    commander->want.reason = "";
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
    commander_t *commander = &g_commander;
    bool supported = false;
    bool in_emulator = false;
    bool any = false;
    bento_status_t status;

    (void)memset(commander, 0, sizeof(*commander));
    commander->ui = bento_runtime_get_ui();
    name_topics(commander);
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
        status = build_screen(commander);
    }
    if ((status == BENTO_OK) && !in_emulator) {
        status = known_models(commander, &any);
    }
    if ((status == BENTO_OK) && !any) {
        if (in_emulator) {
            add_emulator_note(commander->ui);
        }
        say(commander, "None of ALLOWED is on this board", COLOR_BAD);
    } else if (status == BENTO_OK) {
        print_known(commander);
        want_first(commander);
        status = run_commands(commander);
    } else {
        /* an OSError from edge_ai.models() ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
