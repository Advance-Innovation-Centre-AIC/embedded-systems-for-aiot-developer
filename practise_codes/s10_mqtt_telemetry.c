/*
 * s10_mqtt_telemetry.c -- ส่ง telemetry ขึ้น broker และรับคำสั่งกลับ (ฉบับฝึกเติมโค้ด)
 *
 * วิธีรัน: 1) แก้ค่าเจ็ดบรรทัดบนหัวไฟล์ให้เป็นของทีม (NN คือหมายเลขทีมเรา)
 *          2) เติมท่าที่ 1 แล้วรันจนจอขึ้นว่าต่อแล้ว ห้ามข้ามไปท่าอื่นก่อน
 *          3) เปิด MQTT Explorer ค้างไว้ แล้วค่อยเติมท่า 2 และ 3 ทีละจุด
 * ก่อนแก้ TOPIC_* อ่าน examples/s10/01_topic_design.py กับ 02_payload_shape.py
 * ดูที่จอ: ไฟสี่ดวงบอกสถานะลิงก์ ค่าที่จะส่ง สามใบล่าสุด และคำสั่งที่รับ
 *         แผงเขียนมาให้แล้ว ช่องว่างหกจุดอยู่ที่ตรรกะ
 * กับดัก : เติมไม่ครบ ไฟ MQTT จะไม่ติด และรายการล่าสุดจะว่าง
 *
 * The practice version of the MQTT telemetry program: the panel is
 * written for you; fill in the six blanks (connect to the broker,
 * build the reading, publish it, subscribe, read a message, switch
 * the lamp). Unfilled, it runs as the Python skeleton does: offline,
 * counting empty messages ("pot -") every 5 s.
 *
 * Twin of practise_codes/s10_mqtt_telemetry.py
 * (embedded-systems-for-aiot-developer/practise_codes/s10_mqtt_telemetry.py).
 * Style: CLEAN_C.md.
 *   - A blank does nothing here, as the skeleton's pass does. Each
 *     blank names, in its comment, the C call that fills it. A helper
 *     only a blank calls is referenced once with (void) so the
 *     unfilled file compiles under -Werror; the line can stay after
 *     the blank is filled in.
 *   - The skeleton's data = {} is a reading_t with has_data false:
 *     "pot -" on the drawer, 0 on the screen, as d.get("pot", 0).
 * Differs from the original:
 *   - There is no json module in C. The payload is written with
 *     snprintf in the key order MicroPython's dict gives (ax, ay, az,
 *     pot on both boards' qstr hashes), each number as str() of the
 *     rounded value. A command is read by read_command(), which follows
 *     MicroPython's json.loads (extmod/modjson.c) step by step, the
 *     reader of the library twin aiot-s10-04-subscribe-command: the
 *     same texts are JSON or "not JSON" here as there. A real number,
 *     a list or an object as cmd's value is shown as it was sent.
 *   - A JSON document that is not an object has no .get():
 *     MicroPython raises AttributeError and the program ends after its
 *     cleanup. Here the same, with the traceback's last line and
 *     BENTO_E_BAD_TYPE.
 *   - The binding keeps one message, the newest, cut to 255 bytes; the
 *     C client keeps four. The loop reads them all and handles only
 *     the newest, cut to 255 bytes.
 *   - mqtt.publish() raises once the link is gone, which the skeleton
 *     does not catch; publish_reading() answers that as a status, which
 *     the blank's (void) drops.
 *   - Any refusal of a sensor read is the original's OSError.
 *   - wifi.connect() and mqtt.* print lines of their own. The program
 *     prints the ones C can know ("[WiFi] Connecting...", "[WiFi]
 *     Connected! IP=...", "[MQTT] Connecting to ...", "[MQTT]
 *     Connected to ... as '...'", "[MQTT] Subscribed to ...", "[MQTT]
 *     subscribe rejected by broker", "[MQTT] Disconnected"); the
 *     radio's per-attempt and result-code lines are not reported.
 *   - The binding cuts an MQTT password to 31 bytes; C sends it whole.
 *   - Stop ends the program quietly after the same cleanup, where
 *     MicroPython raises KeyboardInterrupt.
 *   - In the BENTO Emulator, MQTT from C is not available yet: the
 *     program (once the connect blank is filled in) says so and then
 *     works offline, as the original does when the broker cannot be
 *     reached.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_mqtt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
static const char WIFI_SSID[] = "AIoT-Class";
static const char WIFI_PASSWORD[] = "<รหัสผ่านของห้องเรียน>";
/* the LAN IP of TESAIoT CE, not localhost */
static const char BROKER[] = "192.168.1.50";
/* the platform's device_id, <= 31 characters */
static const char DEVICE_ID[] = "team03";
/* the practice broker does not check it */
static const char MQTT_PASS[] = "bento";
/* wildcards (+ and #) are for subscribe only: in a publish nobody gets it */
static const char TOPIC_PUB[] = "device/team03/telemetry";
static const char TOPIC_CMD[] = "device/team03/commands";
/* ---- end of settings ---- */

enum {
    SEND_MS = 5000,
    STALE_MS =
        12000, /* two sends without a reading: the screen's value is old */
    UI_EVERY_MS =
        200, /* every screen poll is an IPC to the other core */
    LOOP_MS =
        100, /* fast enough that get_message() sees each command */
    SETTLE_MS = 200,
    SLICE_MS = 20,
    BROKER_PORT = 1883,
    KEEPALIVE_S = 60, /* silent longer and the broker may drop us */
    RECENT_MAX = 3,
    PERCENT = 100,
    SCALE_TICKS = 11,
    SCALE_MAJOR = 5,
    HUNDREDTHS = 100,
    TENTHS = 10,
    PAYLOAD_MAX = 255, /* what the binding keeps of a payload */
    TOPIC_CHARS = 128,
    VALUE_CHARS = 128,
    TEXT_BYTES = 128,
    RECENT_CHARS = 64,
    JSON_BYTES = 192
};

enum { /* the JSON reader */
    UTF8_BYTES_MAX = 4,
    HEX_DIGITS = 4, /* a backslash-u escape */
    ESCAPE_COUNT = 5
};

enum { /* the three cards */
    CARD_VALUE_X = 24,
    CARD_VALUE_W = 232,
    CARD_RECENT_X = 272,
    CARD_CONTROL_X = 528,
    CARD_W = 240
};

enum { /* colours, 0xRRGGBB */
    COL_TEXT = 0xE8EAED,
    COL_DIM = 0x9AA3AF,
    COL_CARD = 0x171B22,
    COL_OK = 0x30A46C,
    COL_WARN = 0xF5A623,
    COL_RUN = 0x4A9EFF,
    COL_BUTTON = 0x3A4150
};

/* One widget's place and look. */
typedef struct {
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value;
} spec_t;

/* The kinds of value json.loads makes. */
typedef enum {
    KIND_NONE = 0,
    KIND_BOOL,
    KIND_INT,
    KIND_FLOAT,
    KIND_TEXT,
    KIND_LIST,
    KIND_DICT
} value_kind_t;

/* One value: its kind and the text str() gives of it. */
typedef struct {
    value_kind_t kind;
    uint32_t length;
    char text[VALUE_CHARS];
} json_value_t;

/* What json.loads(payload) and its "cmd" come to. */
typedef struct {
    bool parsed;      /* false: json.loads raised ValueError */
    value_kind_t top; /* the kind of the whole document */
    bool has_action;  /* the object has a "cmd" key */
    json_value_t action;
} command_t;

/* json.loads, one character at a time. */
typedef struct {
    const uint8_t *text;
    uint32_t length;
    uint32_t at;
    uint8_t kinds[PAYLOAD_MAX]; /* the open lists and objects */
    uint32_t depth;
    bool has_top;
    bool key_pending; /* one key for every level, as modjson.c */
    bool key_is_cmd;
    bool cmd_open; /* cmd's value is the list or object open */
    uint32_t cmd_start;
} json_reader_t;

/* A real number being read: where, which part, still valid. */
typedef struct {
    uint32_t at;
    bool in_fraction;
    bool in_exponent;
    bool valid;
} real_scan_t;

/* One telemetry reading, rounded as it is sent. */
typedef struct {
    bento_real_t ax;
    bento_real_t ay;
    bento_real_t az;
    bento_real_t pot;
    bool has_data; /* false: the skeleton's empty dict */
} reading_t;

/* The widgets the program changes, the link and the loop's state. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t mqtt_text;
    bento_ui_id_t led_wifi;
    bento_ui_id_t led_mqtt;
    bento_ui_id_t led_stale;
    bento_ui_id_t led_remote;
    bento_ui_id_t pot_text;
    bento_ui_id_t pot_bar;
    bento_ui_id_t sent_digits;
    bento_ui_id_t sent_list;
    bento_ui_id_t command_text;
    bento_ui_id_t state_text;
    bento_ui_id_t go;
    bento_ui_id_t hold;
    uint32_t broker; /* the MQTT client, 0 when none */
    uint32_t lamp;   /* the real LED someone else may switch */
    bool
        led_on; /* kept here: the pin answers its level, not the intent */
    bool sending;
    bool stale_shown;
    int32_t sent;
    uint32_t recent_count;
    char recent[RECENT_MAX][RECENT_CHARS];
    uint32_t last_send_ms;
    uint32_t good_ms; /* the last real reading */
    uint32_t ui_ms;
    const char *bad_type; /* the type that had no .get() */
} telemetry_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* ---- json.loads, as extmod/modjson.c reads ---- */

static bool reader_at_end(const json_reader_t *reader)
{
    return reader->at >= reader->length;
}

/* The character under the cursor; 0 past the end. */
static uint8_t reader_peek(const json_reader_t *reader)
{
    uint8_t current = 0U;

    if (!reader_at_end(reader)) {
        current = reader->text[reader->at];
    }
    return current;
}

static void reader_advance(json_reader_t *reader)
{
    if (!reader_at_end(reader)) {
        reader->at++;
    }
}

/* Appends bytes to a value's text, as far as the text holds. */
static void value_append(json_value_t *value, const uint8_t *bytes,
                         uint32_t count)
{
    uint32_t i;

    for (i = 0U; i < count; i++) {
        if ((value->length + 1U) < (uint32_t)VALUE_CHARS) {
            value->text[value->length] = (char)bytes[i];
            value->length++;
        }
    }
    value->text[value->length] = '\0';
}

static void value_set(json_value_t *value, value_kind_t kind,
                      const char *text)
{
    value->kind = kind;
    value->length = 0U;
    value_append(value, (const uint8_t *)text, (uint32_t)strlen(text));
}

/* A \u escape: the code as UTF-8, as vstr_add_char writes it. */
static void value_append_code(json_value_t *value, uint32_t code)
{
    static const uint32_t limits[UTF8_BYTES_MAX - 1] = {0x80U, 0x800U,
                                                        0x10000U};
    static const uint8_t leads[UTF8_BYTES_MAX] = {0x00U, 0xC0U, 0xE0U,
                                                  0xF0U};
    const uint32_t six_bits = 0x3FU;
    const uint32_t follow = 0x80U;
    const uint32_t bits_per_byte = 6U;
    uint8_t bytes[UTF8_BYTES_MAX];
    uint32_t count = 1U;
    uint32_t rest = code;
    uint32_t i;

    for (i = 0U; i < ((uint32_t)UTF8_BYTES_MAX - 1U); i++) {
        if (code >= limits[i]) {
            count++;
        }
    }
    for (i = count - 1U; i > 0U; i--) {
        bytes[i] = (uint8_t)(follow | (rest & six_bits));
        rest >>= bits_per_byte;
    }
    bytes[0] = (uint8_t)(leads[count - 1U] | rest);
    value_append(value, bytes, count);
}

/* \uXXXX: four characters read as hex digits with no check, exactly
 * the arithmetic modjson.c does on a byte. */
static uint32_t read_unicode_escape(json_reader_t *reader)
{
    const uint8_t lower_case = 0x20U;
    const uint8_t zero = (uint8_t)'0';
    const uint8_t letter_gap = (uint8_t)('a' - ('9' + 1));
    const uint8_t digit_max = 9U;
    const uint32_t bits_per_digit = 4U;
    uint32_t code = 0U;
    uint32_t i;

    for (i = 0U; i < (uint32_t)HEX_DIGITS; i++) {
        uint8_t digit;

        reader_advance(reader);
        digit = (uint8_t)((reader_peek(reader) | lower_case) - zero);
        if (digit > digit_max) {
            digit = (uint8_t)(digit - letter_gap);
        }
        code = (code << bits_per_digit) | digit;
    }
    return code;
}

/* The character a backslash escape stands for. */
static uint8_t escaped_char(uint8_t letter)
{
    static const char letters[ESCAPE_COUNT] = {'b', 'f', 'n', 'r', 't'};
    static const uint8_t codes[ESCAPE_COUNT] = {0x08U, 0x0CU, 0x0AU,
                                                0x0DU, 0x09U};
    uint8_t result =
        letter; /* a quote, a backslash, a slash and any other: itself */
    uint32_t i;

    for (i = 0U; i < (uint32_t)ESCAPE_COUNT; i++) {
        if (letter == (uint8_t)letters[i]) {
            result = codes[i];
        }
    }
    return result;
}

/* One character of a string, or one escape. Leaves the cursor on the
 * last character it used. */
static void read_string_char(json_reader_t *reader, json_value_t *value)
{
    uint8_t current = reader_peek(reader);

    if (current == (uint8_t)'\\') {
        reader_advance(reader);
        current = reader_peek(reader);
        if (current == (uint8_t)'u') {
            value_append_code(value, read_unicode_escape(reader));
        } else {
            current = escaped_char(current);
            value_append(value, &current, 1U);
        }
    } else {
        value_append(value, &current, 1U);
    }
}

/* A string after its opening quote. false: it never ends. */
static bool read_string(json_reader_t *reader, json_value_t *value)
{
    bool closed;

    value_set(value, KIND_TEXT, "");
    while (!reader_at_end(reader) &&
           (reader_peek(reader) != (uint8_t)'"')) {
        read_string_char(reader, value);
        reader_advance(reader);
    }
    closed = !reader_at_end(reader);
    reader_advance(reader);
    return closed;
}

/* null, true, false: the first letter is read, the rest must follow. */
static bool read_word(json_reader_t *reader, const char *rest)
{
    bool same = true;
    uint32_t i;

    for (i = 0U; (rest[i] != '\0') && same; i++) {
        same = (reader_peek(reader) == (uint8_t)rest[i]);
        if (same) {
            reader_advance(reader);
        }
    }
    return same;
}

static bool is_digit(uint8_t character)
{
    return (character >= (uint8_t)'0') && (character <= (uint8_t)'9');
}

/* int(text): a sign, then digits only. str() of it drops leading zeros
 * and the sign of zero. */
static bool read_integer(const uint8_t *text, uint32_t length,
                         json_value_t *value)
{
    const bool negative = (text[0] == (uint8_t)'-');
    uint32_t first = negative ? 1U : 0U;
    uint32_t i;
    bool valid = (length > first);
    bool is_zero;

    for (i = first; (i < length) && valid; i++) {
        valid = is_digit(text[i]);
    }
    while (((first + 1U) < length) && (text[first] == (uint8_t)'0')) {
        first++;
    }
    value_set(value, KIND_INT, "");
    is_zero = ((length - first) == 1U) && (text[first] == (uint8_t)'0');
    if (negative && !is_zero) {
        value_append(value, text, 1U);
    }
    if (valid) {
        value_append(value, &text[first], length - first);
    }
    return valid;
}

/* A sign, as a number's exponent may start with. */
static bool is_sign(uint8_t character)
{
    return (character == (uint8_t)'+') || (character == (uint8_t)'-');
}

/* One character of a real number. false: it ends the number. */
static bool take_real_char(const uint8_t *text, uint32_t length,
                           real_scan_t *scan)
{
    const uint8_t lower_case = 0x20U;
    const uint8_t current = text[scan->at];
    bool more = true;

    scan->at++;
    if (is_digit(current)) {
        /* a digit of whatever part it is in */
    } else if ((current == (uint8_t)'.') && !scan->in_fraction &&
               !scan->in_exponent) {
        scan->in_fraction = true;
    } else if (((current | lower_case) == (uint8_t)'e') &&
               !scan->in_exponent) {
        scan->in_exponent = true;
        if ((scan->at < length) && is_sign(text[scan->at])) {
            scan->at++;
        }
        scan->valid = (scan->at < length);
    } else {
        scan->at--; /* a sign in the wrong place ends the number */
        more = false;
    }
    return more;
}

/* float(text) as parsenum.c reads it: a sign, digits with at most one
 * '.', then at most one exponent that must not end the text right
 * after its 'e' and sign. Shown as it was sent. */
static bool read_real(const uint8_t *text, uint32_t length,
                      json_value_t *value)
{
    real_scan_t scan;
    bool more = true;

    scan.at = ((text[0] == (uint8_t)'-') ? 1U : 0U);
    scan.in_fraction = false;
    scan.in_exponent = false;
    scan.valid = true;
    while ((scan.at < length) && scan.valid && more) {
        more = take_real_char(text, length, &scan);
    }
    value_set(value, KIND_FLOAT, "");
    value_append(value, text, length);
    return scan.valid &&
           (scan.at != ((text[0] == (uint8_t)'-') ? 1U : 0U)) &&
           (scan.at == length);
}

/* A number: its first character is read; digits, signs, '.', 'e' and
 * 'E' follow. A '.', 'e' or 'E' makes it a real number. */
static bool read_number(json_reader_t *reader, json_value_t *value)
{
    const uint32_t start = reader->at - 1U;
    bool is_real = false;
    bool more = true;

    while (more) {
        const uint8_t current = reader_peek(reader);

        if ((current == (uint8_t)'.') || (current == (uint8_t)'e') ||
            (current == (uint8_t)'E')) {
            is_real = true;
        } else if (!is_digit(current) && (current != (uint8_t)'+') &&
                   (current != (uint8_t)'-')) {
            more = false;
        } else {
            /* a digit or a sign: part of the number */
        }
        if (more) {
            reader_advance(reader);
        }
    }
    return is_real ? read_real(&reader->text[start], reader->at - start,
                               value)
                   : read_integer(&reader->text[start],
                                  reader->at - start, value);
}

/* What one character starts. */
typedef enum {
    STEP_SKIP = 0,
    STEP_VALUE,
    STEP_OPEN,
    STEP_CLOSE,
    STEP_BAD
} step_t;

/* null, true, false or a number, after its first character. */
static bool read_scalar(json_reader_t *reader, uint8_t current,
                        json_value_t *value)
{
    bool valid = false;

    if (current == (uint8_t)'n') {
        value_set(value, KIND_NONE, "None");
        valid = read_word(reader, "ull");
    } else if (current == (uint8_t)'t') {
        value_set(value, KIND_BOOL, "True");
        valid = read_word(reader, "rue");
    } else if (current == (uint8_t)'f') {
        value_set(value, KIND_BOOL, "False");
        valid = read_word(reader, "alse");
    } else if ((current == (uint8_t)'-') || is_digit(current)) {
        valid = read_number(reader, value);
    } else {
        /* no JSON value starts with it */
    }
    return valid;
}

static step_t read_step(json_reader_t *reader, json_value_t *value)
{
    const uint8_t current = reader_peek(reader);
    step_t step = STEP_VALUE;
    bool valid = true;

    reader_advance(reader);
    switch (current) {
    case ',': /* modjson.c skips these like spaces */
    case ':':
    case ' ':
    case '\t':
    case '\n':
    case '\r':
        step = STEP_SKIP;
        break;
    case '"':
        valid = read_string(reader, value);
        break;
    case '[':
        value_set(value, KIND_LIST, "");
        step = STEP_OPEN;
        break;
    case '{':
        value_set(value, KIND_DICT, "");
        step = STEP_OPEN;
        break;
    case ']': /* either bracket closes either kind */
    case '}':
        step = STEP_CLOSE;
        break;
    default:
        valid = read_scalar(reader, current, value);
        break;
    }
    return valid ? step : STEP_BAD;
}

/* A value inside an object: a key, or the value of the pending key.
 * Only cmd's value in the outermost object is kept. */
static bool place_in_object(json_reader_t *reader,
                            const json_value_t *value, bool opens,
                            command_t *command)
{
    static const char cmd_key[] = "cmd";
    const uint32_t cmd_length = (uint32_t)sizeof(cmd_key) - 1U;
    bool valid = true;

    if (!reader->key_pending) {
        reader->key_pending = true;
        reader->key_is_cmd =
            (value->kind == KIND_TEXT) &&
            (value->length == cmd_length) &&
            (strncmp(value->text, cmd_key, cmd_length) == 0);
        valid = !opens; /* a list or object cannot be a key */
    } else {
        reader->key_pending = false;
        if (reader->key_is_cmd && (reader->depth == 1U)) {
            command->action = *value;
            command->has_action = true;
            reader->cmd_open = opens;
            reader->cmd_start = reader->at - 1U;
        }
    }
    return valid;
}

/* After a value or an opening bracket. */
static bool place_value(json_reader_t *reader,
                        const json_value_t *value, bool opens,
                        command_t *command)
{
    bool valid = true;

    if (!reader->has_top) {
        reader->has_top = true;
        command->top = value->kind;
    } else if (reader->kinds[reader->depth - 1U] ==
               (uint8_t)KIND_DICT) {
        valid = place_in_object(reader, value, opens, command);
    } else {
        /* a list item: nothing here reads it */
    }
    if (valid && opens && (reader->depth < (uint32_t)PAYLOAD_MAX)) {
        reader->kinds[reader->depth] = (uint8_t)value->kind;
        reader->depth++;
    }
    return valid;
}

/* A closing bracket: back to the parent, or the end of the document.
 * cmd's own list or object keeps its text. */
static bool close_container(json_reader_t *reader, command_t *command,
                            bool *finished)
{
    const bool valid = reader->has_top && (reader->depth > 0U);

    if (valid && (reader->depth == 1U)) {
        *finished = true;
    } else if (valid) {
        if ((reader->depth == 2U) && reader->cmd_open) {
            json_value_t *action = &command->action;

            action->length = 0U;
            value_append(action, &reader->text[reader->cmd_start],
                         reader->at - reader->cmd_start);
            reader->cmd_open = false;
        }
        reader->depth--;
    } else {
        /* a closing bracket before anything: syntax error */
    }
    return valid;
}

static bool take_step(json_reader_t *reader, command_t *command,
                      bool *finished)
{
    json_value_t value;
    const step_t step = read_step(reader, &value);
    bool valid = true;

    if (step == STEP_BAD) {
        valid = false;
    } else if (step == STEP_CLOSE) {
        valid = close_container(reader, command, finished);
    } else if (step == STEP_SKIP) {
        /* nothing to place */
    } else {
        valid = place_value(reader, &value, step == STEP_OPEN, command);
        *finished = valid && (reader->depth == 0U);
    }
    return valid;
}

/* unichar_isspace(): space, tab, LF, VT, FF, CR. */
static bool is_space(uint8_t character)
{
    const uint8_t tab = 0x09U;
    const uint8_t carriage_return = 0x0DU;

    return (character == (uint8_t)' ') ||
           ((character >= tab) && (character <= carriage_return));
}

/* utf8_check(): what bytes.decode() accepts. */
static bool is_utf8(const uint8_t *bytes, uint32_t length)
{
    const uint8_t follow_mask = 0xC0U;
    const uint8_t follow = 0x80U;
    const uint8_t lead_min = 0xC0U;
    const uint8_t lead_max = 0xF8U; /* from here no lead byte */
    const uint32_t follow_table =
        0xE5U; /* utf8_check's packed count of follow bytes */
    const uint32_t table_shift = 3U;
    const uint32_t table_index = 0x6U;
    const uint32_t count_mask = 3U;
    uint32_t need = 0U;
    uint32_t i;
    bool valid = true;

    for (i = 0U; (i < length) && valid; i++) {
        const uint8_t current = bytes[i];

        if (need != 0U) {
            valid = ((current & follow_mask) == follow);
            need--;
        } else if (current >= lead_max) {
            valid = false;
        } else if (current >= lead_min) {
            need =
                (follow_table >>
                 (((uint32_t)current >> table_shift) & table_index)) &
                count_mask;
        } else {
            valid = (current < follow);
        }
    }
    return valid && (need == 0U);
}

/* json.loads(payload.decode()) and its "cmd". The stream ends at the
 * first NUL byte, as modjson.c reads it. Not JSON is cmd = {}. */
static void read_command(const uint8_t *payload, uint32_t length,
                         command_t *command)
{
    json_reader_t reader;
    bool finished = false;
    bool valid = is_utf8(payload, length);

    (void)memset(&reader, 0, sizeof(reader));
    reader.text = payload;
    while ((reader.length < length) && (payload[reader.length] != 0U)) {
        reader.length++;
    }
    command->top = KIND_NONE;
    command->has_action = false;
    value_set(&command->action, KIND_TEXT, "");
    while (valid && !finished) {
        valid = !reader_at_end(&reader) &&
                take_step(&reader, command, &finished);
    }
    while (valid && is_space(reader_peek(&reader))) {
        reader_advance(&reader);
    }
    command->parsed = valid && reader_at_end(&reader);
    if (!command->parsed) {
        command->top = KIND_DICT;
        command->has_action = false;
    }
}

static bool is_word(const json_value_t *action, const char *word)
{
    const size_t length = strlen(word);

    return (action->kind == KIND_TEXT) && (action->length == length) &&
           (strncmp(action->text, word, length) == 0);
}

/* The Python type name of a document that is not an object. */
static const char *type_name(value_kind_t kind)
{
    static const char *const names[] = {
        "NoneType", "bool", "int", "float", "str", "list", "dict"};

    return names[kind];
}

/* ---- the screen ---- */

/* The widget setters ignore a refusal, as MicroPython's do (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static void set_value(bento_ui_t *ui, bento_ui_id_t widget,
                      int32_t value)
{
    (void)bento_ui_set_value(ui, widget, value);
}

/* ui.poll() only to keep the screen alive; a failed poll answers none. */
static void keep_alive(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static void args_from(const spec_t *spec, bento_ui_widget_args_t *args)
{
    bento_ui_widget_args_init(args);
    if (spec->text != NULL) {
        args->text = spec->text;
        args->text_len = (uint32_t)strlen(spec->text);
    }
    args->x = spec->x;
    args->y = spec->y;
    args->w = spec->w;
    args->h = spec->h;
    args->color = spec->color;
    args->font_px = spec->value;
}

static bento_status_t add_label(bento_ui_t *ui, const spec_t *spec,
                                bento_ui_id_t *out)
{
    bento_ui_widget_args_t args;

    args_from(spec, &args);
    return bento_ui_label_create(ui, &args, out);
}

/* A card: min= is its border colour, max= its radius, value= its border. */
static bento_status_t add_card(bento_ui_t *ui, int16_t x, int16_t w)
{
    const int16_t card_y = 112;
    const int16_t card_h = 280;
    const int32_t radius_px = 12;
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;

    bento_ui_widget_args_init(&args);
    args.x = x;
    args.y = card_y;
    args.w = w;
    args.h = card_h;
    args.color = COL_CARD;
    args.min_val = (int32_t)COL_DIM;
    args.max_val = radius_px;
    args.font_px = 1;
    return bento_ui_panel_create(ui, &args, &unused);
}

/* One light with its caption below. */
static bento_status_t add_light(bento_ui_t *ui, const spec_t *light,
                                const char *caption, bento_ui_id_t *out)
{
    const int16_t caption_y = 72;
    const int32_t caption_px = 20;
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    spec_t text = {NULL, 0, 0, -1, -1, COL_DIM, 0};
    bento_status_t status;

    args_from(light, &args);
    status = bento_ui_led_create(ui, &args, out);
    if (status == BENTO_OK) {
        text.text = caption;
        text.x = light->x;
        text.y = caption_y;
        text.value = caption_px;
        status = add_label(ui, &text, &unused);
    }
    return status;
}

/* Four lights along the data's path: WiFi, MQTT, stale value, the lamp
 * someone else switches. */
static bento_status_t add_header(telemetry_t *app)
{
    static const spec_t title = {
        "MQTT Telemetry - คาบ 10", 24, 36, -1, -1, COL_TEXT, 20};
    static const spec_t link = {"MQTT: ออฟไลน์", 24, 72, -1, -1,
                                COL_DIM,        20};
    static const spec_t wifi = {NULL, 328, 16, 48, 48, COL_OK, 0};
    static const spec_t mqtt = {NULL, 440, 16, 48, 48, COL_OK, 0};
    static const spec_t stale = {NULL, 552, 16, 48, 48, COL_WARN, 0};
    static const spec_t remote = {NULL, 664, 16, 48, 48, COL_RUN, 0};
    bento_ui_id_t unused;
    bento_status_t status = add_label(app->ui, &title, &unused);

    if (status == BENTO_OK) {
        status = add_label(app->ui, &link, &app->mqtt_text);
    }
    if (status == BENTO_OK) {
        status = add_light(app->ui, &wifi, "WiFi", &app->led_wifi);
    }
    if (status == BENTO_OK) {
        status = add_light(app->ui, &mqtt, "MQTT", &app->led_mqtt);
    }
    if (status == BENTO_OK) {
        status = add_light(app->ui, &stale, "ค่าค้าง", &app->led_stale);
    }
    if (status == BENTO_OK) {
        status =
            add_light(app->ui, &remote, "ไฟสั่งไกล", &app->led_remote);
    }
    return status;
}

/* Card 1: the value sent and how many went. The card comes before the
 * labels on it, or it covers them without a word. */
static bento_status_t add_value_card(telemetry_t *app)
{
    static const spec_t caption = {"ลูกบิดที่ส่งจริง", 40,      124, -1,
                                   -1,           COL_DIM, 20};
    static const spec_t pot = {"- %", 40, 160, -1, -1, COL_TEXT, 24};
    static const spec_t bar = {NULL, 40, 204, 200, 12, COL_RUN, 0};
    static const spec_t scale = {NULL, 40, 224, 200, 44, COL_TEXT, 0};
    static const spec_t count = {"ส่งไปแล้ว (ใบ)", 40, 288, -1, -1,
                                 COL_DIM,        20};
    static const spec_t digits = {"0", 40, 324, 200, 48, COL_TEXT, 0};
    bento_ui_widget_args_t args;
    bento_ui_id_t ruler = 0U;
    bento_ui_id_t unused;
    bento_status_t status =
        add_card(app->ui, CARD_VALUE_X, CARD_VALUE_W);

    if (status == BENTO_OK) {
        status = add_label(app->ui, &caption, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, &pot, &app->pot_text);
    }
    if (status == BENTO_OK) {
        args_from(&bar, &args);
        args.max_val = PERCENT;
        status = bento_ui_bar_create(app->ui, &args, &app->pot_bar);
    }
    if (status == BENTO_OK) {
        args_from(&scale, &args);
        args.max_val = PERCENT;
        status = bento_ui_scale_create(app->ui, &args, &ruler);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_ticks(app->ui, ruler, SCALE_TICKS,
                                 SCALE_MAJOR);
        status = add_label(app->ui, &count, &unused);
    }
    if (status == BENTO_OK) {
        args_from(&digits, &args);
        status =
            bento_ui_seg7_create(app->ui, &args, &app->sent_digits);
    }
    return status;
}

/* Card 2: the last three messages and the command received. */
static bento_status_t add_recent_card(telemetry_t *app)
{
    static const spec_t caption = {"สามใบล่าสุด", 288,     124, -1,
                                   -1,          COL_DIM, 20};
    static const spec_t list = {NULL, 288, 160, 208, 168, 0U, 0};
    static const spec_t received = {"รับล่าสุด", 288,     344, -1,
                                    -1,       COL_DIM, 20};
    static const spec_t command = {"ยังไม่มี", 400,     344, -1,
                                   -1,      COL_DIM, 20};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_card(app->ui, CARD_RECENT_X, CARD_W);

    if (status == BENTO_OK) {
        status = add_label(app->ui, &caption, &unused);
    }
    if (status == BENTO_OK) {
        args_from(&list, &args);
        status = bento_ui_list_create(app->ui, &args, &app->sent_list);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, &received, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, &command, &app->command_text);
    }
    return status;
}

/* Card 3: start and hold. Either can be undone at once, so neither
 * asks to confirm. From x=690, y=340 the corner is the Console
 * button's. */
static bento_status_t add_control_card(telemetry_t *app)
{
    static const spec_t caption = {"คำสั่งการส่งข้อมูล", 544, 124, -1, -1,
                                   COL_DIM,         20};
    static const spec_t state = {"กำลังส่ง", 544,     160, -1,
                                 -1,       COL_DIM, 20};
    static const spec_t go = {"เริ่มส่ง", 544, 240, 88, 88, COL_OK, 20};
    static const spec_t hold = {"หยุดส่ง", 664,        240, 88,
                                88,      COL_BUTTON, 20};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_card(app->ui, CARD_CONTROL_X, CARD_W);

    if (status == BENTO_OK) {
        status = add_label(app->ui, &caption, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, &state, &app->state_text);
    }
    if (status == BENTO_OK) {
        args_from(&go, &args);
        status = bento_ui_button_create(app->ui, &args, &app->go);
    }
    if (status == BENTO_OK) {
        args_from(&hold, &args);
        status = bento_ui_button_create(app->ui, &args, &app->hold);
    }
    return status;
}

/* The link panel, built before the network is touched. */
static bento_status_t build_screen(telemetry_t *app)
{
    bento_status_t status =
        bento_ui_screen(app->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_header(app);
    }
    if (status == BENTO_OK) {
        status = add_value_card(app);
    }
    if (status == BENTO_OK) {
        status = add_recent_card(app);
    }
    if (status == BENTO_OK) {
        status = add_control_card(app);
    }
    if (status == BENTO_OK) {
        keep_alive(app->ui);
    }
    return status;
}

static void show_link(const telemetry_t *app, bool connected)
{
    set_value(app->ui, app->led_mqtt, connected ? 1 : 0);
    set_text(app->ui, app->mqtt_text,
             connected ? "MQTT: เชื่อมต่อแล้ว" : "MQTT: ออฟไลน์");
}

/* ---- the board: the lamp and the sensors ---- */

/* An LED by its name in the firmware's table: the index differs from
 * board to board, the name does not. Eva Kit: LED1 red, LED2 green,
 * RGB_RED shines blue. Dev Kit: RGB_RED, RGB_BLUE, RGB_GREEN (LED1 and
 * LED2 sit on the SoM, out of sight). Dev Kit's name first, then Eva's. */
/* The LED with this name in the firmware's table. false: none. */
static bool find_led(const char *wanted, uint32_t *found)
{
    const uint32_t leds = bento_gpio_num_leds();
    bool known = false;
    uint32_t led;

    for (led = 0U; (led < leds) && !known; led++) {
        const char *name = NULL;

        known = (bento_gpio_led_name(led, &name) == BENTO_OK) &&
                (name != NULL) && (strcmp(name, wanted) == 0);
        if (known) {
            *found = led;
        }
    }
    return known;
}

static uint32_t led_named(const char *const names[],
                          uint32_t name_count, uint32_t fallback)
{
    uint32_t found = fallback;
    bool known = false;
    uint32_t i;

    for (i = 0U; (i < name_count) && !known; i++) {
        known = find_led(names[i], &found);
    }
    return found;
}

/* No sensors init (it raises on the Eva Kit), but the Eva Kit's screen
 * core answers about 13 s after a reset: one read warms it up. */
static void warm_up_sensors(void)
{
    bento_sensors_imu6_t motion;

    (void)memset(&motion, 0, sizeof(motion));
    motion.struct_size = (uint32_t)sizeof(motion);
    if (bento_sensors_bmi270_motion(&motion) != BENTO_OK) {
        (void)printf("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ตอนส่ง\n");
    }
}

/* round(value, digits) as modbuiltins.c does it: nearbyint(value * 10^n)
 * divided by 10^n, in the board's float width. */
static bento_real_t round_to(bento_real_t value, int32_t scale)
{
    const bento_real_t factor = (bento_real_t)scale;
    const bento_real_t scaled = value * factor;

    return (bento_real_t)nearbyint((double)scaled) / factor;
}

/* str() of a number, as this board's MicroPython writes it. */
static void real_text(bento_real_t value, char *text)
{
    uint32_t length = 0U;

    if (bento_fmt_real(text, BENTO_FMT_BUF_SIZE, value, &length) !=
        BENTO_OK) {
        text[0] = '\0';
    }
}

/* The reading from one motion read and the knob, rounded: a small
 * payload, numbers as numbers (a dashboard cannot chart a string).
 * false: the knob could not be read. */
static bool fill_reading(const bento_sensors_imu6_t *motion,
                         reading_t *reading)
{
    float pot_percent = 0.0F;
    const bool good =
        bento_sensors_pot_percent(&pot_percent) == BENTO_OK;

    if (good) {
        reading->ax = round_to((bento_real_t)motion->ax, HUNDREDTHS);
        reading->ay = round_to((bento_real_t)motion->ay, HUNDREDTHS);
        reading->az = round_to((bento_real_t)motion->az, HUNDREDTHS);
        reading->pot = round_to((bento_real_t)pot_percent, TENTHS);
        reading->has_data = true;
    }
    return good;
}

/* ---- the network ---- */

/* wifi.connect(); the original does not look at its answer. A refused
 * or timed-out join goes on offline; another failure ends the program. */
static bento_status_t join_wifi(void)
{
    bento_wifi_connect_params_t params;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = WIFI_PASSWORD;
    params.pass_len = (uint32_t)strlen(WIFI_PASSWORD);
    params.flags = BENTO_WIFI_F_SAVE_CREDS; /* the binding saves too */
    (void)printf("[WiFi] Connecting...\n");
    status = bento_wifi_connect_start(&params);
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(SLICE_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_connect_poll();
        } else {
            (void)
                bento_wifi_connect_cancel(); /* Stop: nothing to report */
        }
    }
    if ((status == BENTO_E_FAILED) || (status == BENTO_E_TIMEOUT)) {
        status = BENTO_OK;
    }
    return status;
}

/* The address on the drawer and the WiFi light. */
static void show_wifi(const telemetry_t *app)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    char line[TEXT_BYTES];
    uint32_t ip_v4 = 0U;
    uint32_t length = 0U;
    bool connected = false;

    if ((bento_wifi_ip(BENTO_WIFI_IF_ANY, &ip_v4) != BENTO_OK) ||
        (bento_wifi_ipv4_format(ip_v4, address,
                                (uint32_t)sizeof(address),
                                &length) != BENTO_OK)) {
        (void)snprintf(address, sizeof(address), "0.0.0.0");
    }
    (void)snprintf(line, sizeof(line), "WiFi: %s", address);
    (void)bento_lcd_print(
        line); /* lcd.print ignores a refusal (modlcd.c) */
    if (bento_wifi_is_connected(&connected) != BENTO_OK) {
        connected = false;
    }
    set_value(app->ui, app->led_wifi, connected ? 1 : 0);
}

/* The BENTO Emulator has no MQTT from C yet: say so on the screen, over
 * the empty list of sent messages, and on the console. */
static bento_status_t show_emulator_note(const telemetry_t *app)
{
    /* The line breaks are in the text: the Emulator does not wrap a
     * label at a width, so all of it stays on the screen. */
    static const spec_t note = {"MQTT จาก C ยังไม่มีใน\nBENTO "
                                "Emulator\nให้รันบน Dev Kit\nหรือ Eva Kit",
                                288,
                                160,
                                -1,
                                -1,
                                COL_WARN,
                                20};
    bento_ui_id_t unused;

    (void)printf("MQTT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ "
                 "Eva Kit / MQTT from C is not in the BENTO Emulator "
                 "yet: run it on a Dev Kit or Eva Kit.\n");
    return add_label(app->ui, &note, &unused);
}

/* mqtt.connect(): *connected is its True or False. A client that could
 * not even start (no radio) ends the program, as the binding's raise. */
static bento_status_t open_broker(telemetry_t *app, bool *connected)
{
    bento_mqtt_config_t config;
    bool in_emulator;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = (uint16_t)BROKER_PORT;
    config.keepalive_s = (uint16_t)KEEPALIVE_S;
    config.client_id = DEVICE_ID;
    config.client_id_len = (uint32_t)strlen(DEVICE_ID);
    config.user = DEVICE_ID;
    config.user_len = (uint32_t)strlen(DEVICE_ID);
    config.pass = MQTT_PASS;
    config.pass_len = (uint32_t)strlen(MQTT_PASS);
    status = bento_mqtt_open_start(&config, &app->broker);
    in_emulator = status == BENTO_E_UNSUPPORTED;
    if (status == BENTO_OK) {
        (void)printf("[MQTT] Connecting to %s:%d ...\n", BROKER,
                     BROKER_PORT);
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(SLICE_MS);
        if (status == BENTO_OK) {
            status = bento_mqtt_open_poll(app->broker);
        }
    }
    *connected = status == BENTO_OK;
    if (*connected) {
        (void)printf("[MQTT] Connected to %s:%d as '%s'\n", BROKER,
                     BROKER_PORT, DEVICE_ID);
    } else if (in_emulator) {
        status = show_emulator_note(
            app); /* then offline, as a refused connect */
    } else if ((status != BENTO_E_CANCELLED) && (app->broker != 0U)) {
        status =
            BENTO_OK; /* refused or timed out: False, go on offline */
    } else {
        /* no client, or Stop: the status ends the program */
    }
    return status;
}

/* mqtt.subscribe(): once, before the loop. A refusal is printed and the
 * program goes on. */
static void subscribe_commands(const telemetry_t *app)
{
    uint8_t granted_qos = 0U;
    const bento_status_t status = bento_mqtt_subscribe(
        app->broker, TOPIC_CMD, (uint32_t)strlen(TOPIC_CMD), 0U,
        &granted_qos);

    if (status == BENTO_OK) {
        (void)printf("[MQTT] Subscribed to '%s' (QoS=%d)\n", TOPIC_CMD,
                     (int)granted_qos);
    } else if (status == BENTO_E_FAILED) {
        (void)printf("[MQTT] subscribe rejected by broker\n");
    } else {
        /* the binding prints a result code C does not have */
    }
}

/* The JSON of a reading, published flat on TOPIC_PUB. Answers the
 * publish's status: True does not mean it reached the broker. */
static bento_status_t publish_reading(const telemetry_t *app,
                                      const reading_t *reading)
{
    char ax[BENTO_FMT_BUF_SIZE];
    char ay[BENTO_FMT_BUF_SIZE];
    char az[BENTO_FMT_BUF_SIZE];
    char pot[BENTO_FMT_BUF_SIZE];
    char payload[JSON_BYTES];

    real_text(reading->ax, ax);
    real_text(reading->ay, ay);
    real_text(reading->az, az);
    real_text(reading->pot, pot);
    (void)snprintf(payload, sizeof(payload),
                   "{\"ax\": %s, \"ay\": %s, \"az\": %s, \"pot\": %s}",
                   ax, ay, az, pot);
    return bento_mqtt_publish(
        app->broker, TOPIC_PUB, (uint32_t)strlen(TOPIC_PUB),
        (const uint8_t *)payload, (uint32_t)strlen(payload), 0U, 0U);
}

/* Read, build the reading, publish. The reading stays empty until the
 * blanks are filled in. */
static void publish_telemetry(const telemetry_t *app,
                              reading_t *reading)
{
    bento_sensors_imu6_t motion;

    reading->has_data = false;
    (void)memset(&motion, 0, sizeof(motion));
    motion.struct_size = (uint32_t)sizeof(motion);
    /* อ่านพลาดหนึ่งรอบ โปรแกรมไม่ควรตาย ข้ามไปส่งรอบหน้า
     * ทุกบรรทัดที่อ่านค่าต้องอยู่ใน try เดียวกัน */
    if (bento_sensors_bmi270_motion(&motion) == BENTO_OK) {
        /* เติม: (void)fill_reading(&motion, reading);
         * round ให้ payload เล็ก อย่าส่งตัวเลขเป็นสตริง dashboard จะวาดกราฟไม่ได้ */
        (void)
            fill_reading; /* called once the blank above is filled in */

        /* publish() รับตามตำแหน่งเท่านั้น ไม่มีอาร์กิวเมนต์ retain
         * เติม: (void)publish_reading(app, reading);
         * ส่งแบน ห้ามห่อใต้ {"data": ...} แพลตฟอร์มห่อให้เอง
         * คืน True ไม่ได้แปลว่าถึง broker แล้ว */
        (void)
            publish_reading; /* called once the blank above is filled in */
        (void)app;           /* the blank above sends with it */
    }
}

/* The three newest lines, oldest first, each ticked. A short row: a
 * long one scrolls until its start is gone. */
static void show_recent(telemetry_t *app, const char *pot)
{
    bento_ui_item_t item;
    uint32_t i;

    if (app->recent_count == (uint32_t)RECENT_MAX) {
        for (i = 1U; i < (uint32_t)RECENT_MAX; i++) {
            (void)memcpy(app->recent[i - 1U], app->recent[i],
                         sizeof(app->recent[0]));
        }
        app->recent_count--;
    }
    (void)snprintf(app->recent[app->recent_count],
                   sizeof(app->recent[0]), "ใบ %ld : %s",
                   (long)app->sent, pot);
    app->recent_count++;
    (void)bento_ui_items_clear(app->ui, app->sent_list);
    for (i = 0U; i < app->recent_count; i++) {
        (void)memset(&item, 0, sizeof(item));
        item.struct_size = (uint32_t)sizeof(item);
        item.a = (uint8_t)BENTO_UI_ICON_OK;
        item.text = app->recent[i];
        item.text_len = (uint32_t)strlen(app->recent[i]);
        (void)bento_ui_item_append(app->ui, app->sent_list,
                                   &item); /* sent and forgotten */
    }
}

/* One send round: counted whether or not the reading came, as the
 * skeleton does; "-" on the drawer and 0 on the screen for none. */
static void send_round(telemetry_t *app)
{
    reading_t reading;
    char pot[BENTO_FMT_BUF_SIZE];
    char line[TEXT_BYTES];

    (void)memset(&reading, 0, sizeof(reading));
    publish_telemetry(app, &reading);
    app->sent++;
    if (reading.has_data) {
        real_text(reading.pot, pot);
    } else {
        (void)snprintf(pot, sizeof(pot), "-");
    }
    (void)snprintf(line, sizeof(line), "ส่งครั้งที่ %ld | pot %s",
                   (long)app->sent, pot);
    (void)bento_lcd_print(
        line); /* lcd.print ignores a refusal (modlcd.c) */
    app->last_send_ms = bento_time_ticks_ms();
    if (!reading.has_data) {
        (void)snprintf(pot, sizeof(pot), "0");
    }
    app->good_ms = bento_time_ticks_ms();
    (void)snprintf(line, sizeof(line), "%s %%", pot);
    set_text(app->ui, app->pot_text, line);
    set_value(app->ui, app->pot_bar, (int32_t)reading.pot);
    (void)snprintf(line, sizeof(line), "%ld", (long)app->sent);
    set_text(app->ui, app->sent_digits, line);
    show_recent(app, pot);
}

/* mqtt.get_message(): the binding keeps one message, the newest, cut to
 * 255 bytes. Reading the client's ring empty gives the same. */
static bool take_newest(const telemetry_t *app, uint8_t *payload,
                        uint32_t *length)
{
    bento_mqtt_msg_t message;
    char topic[TOPIC_CHARS];
    bool found = false;
    bool more = app->broker != 0U;

    while (more) {
        bento_mqtt_msg_init(&message);
        /* get_message() never raises: any refusal is "no message" */
        more = (bento_mqtt_poll(app->broker, &message, topic,
                                (uint32_t)sizeof(topic), payload,
                                (uint32_t)PAYLOAD_MAX) == BENTO_OK);
        if (more) {
            found = true;
            *length = message.payload_len;
        }
    }
    return found;
}

/* A command: "toggle" switches the real lamp. Text that is not JSON
 * must not kill the program. */
static bento_status_t carry_out(telemetry_t *app,
                                const uint8_t *payload, uint32_t length)
{
    command_t command;
    bento_status_t status = BENTO_OK;

    read_command(payload, length, &command);
    if (command.top != KIND_DICT) {
        app->bad_type = type_name(command.top); /* no .get() */
        status = BENTO_E_BAD_TYPE;
    }
    if ((status == BENTO_OK) && command.has_action &&
        is_word(&command.action, "toggle")) {
        app->led_on = !app->led_on;
        /* เติม: status = bento_gpio_led_set_value(app->lamp, app->led_on ? 1U : 0U); */
        set_value(app->ui, app->led_remote, app->led_on ? 1 : 0);
    }
    if (status == BENTO_OK) {
        set_text(app->ui, app->command_text,
                 command.has_action ? command.action.text : "อ่านไม่ออก");
    }
    return status;
}

/* The start and hold buttons. */
static void read_buttons(telemetry_t *app)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(app->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U; /* a failed poll answers no events */
    }
    for (i = 0U; i < count; i++) {
        const bool tapped =
            events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED;

        if (tapped && (events[i].id == app->go)) {
            app->sending = true;
            set_text(app->ui, app->state_text, "กำลังส่ง");
        } else if (tapped && (events[i].id == app->hold)) {
            app->sending = false;
            set_text(app->ui, app->state_text, "หยุดส่งชั่วคราว");
        } else {
            /* another widget, or not a tap */
        }
    }
}

/* One round: send when due (ticks, not a 5 s sleep, or get_message()
 * goes unasked), the stale light, a command, the buttons. */
static bento_status_t session_round(telemetry_t *app)
{
    uint8_t payload[PAYLOAD_MAX];
    uint32_t length = 0U;
    bool found = false;
    bool stale;
    bento_status_t status = BENTO_OK;

    (void)memset(payload, 0, sizeof(payload));

    if (app->sending &&
        (bento_time_ticks_diff(bento_time_ticks_ms(),
                               app->last_send_ms) >= SEND_MS)) {
        send_round(app);
    }
    /* a value that cannot be read keeps the old number: the light says so,
     * written only when it changes, as a full display queue drops commands */
    stale = bento_time_ticks_diff(bento_time_ticks_ms(),
                                  app->good_ms) >= STALE_MS;
    if (stale != app->stale_shown) {
        app->stale_shown = stale;
        set_value(app->ui, app->led_stale, stale ? 1 : 0);
    }
    /* get_message() มีบัฟเฟอร์ช่องเดียว ข้อความใหม่ทับของเก่าเงียบ ๆ
     * เติม: found = take_newest(app, payload, &length);
     * ลูปต้องเดินเร็ว (100 ms) ใส่ wait_ms(5000) แทน คำสั่งที่มาระหว่างนั้นจะหาย */
    (void)take_newest; /* called once the blank above is filled in */
    if (found) {
        status = carry_out(app, payload, length);
    }
    if ((status == BENTO_OK) &&
        (bento_time_ticks_diff(bento_time_ticks_ms(), app->ui_ms) >=
         UI_EVERY_MS)) {
        app->ui_ms = bento_time_ticks_ms();
        read_buttons(app);
    }
    if (status == BENTO_OK) {
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* The broker, the subscription, then the loop until Stop. */
static bento_status_t run_session(telemetry_t *app)
{
    bool connected = false; /* ให้ไฟล์รันได้ก่อนเติมครบ */
    bento_status_t status = BENTO_OK;

    /* เติม: status = open_broker(app, &connected);
     * ค่าใน open_broker ผิดตัวเดียวก็ต่อไม่ได้ (ชื่อผู้ใช้อยู่ใน config.user)
     * ลำดับ: WiFi ได้ IP ก่อน แล้ว TCP จึงต่อได้ แล้ว MQTT จึงแนะนำตัวได้
     * ได้ false ให้ไล่ย้อนทีละขั้น อย่าเดา */
    (void)open_broker; /* called once the blank above is filled in */

    if (status == BENTO_OK) {
        (void)bento_lcd_print(connected
                                  ? "<span class=ok>MQTT ต่อแล้ว</span>"
                                  : "MQTT ต่อไม่ได้");
        show_link(app, connected);
        keep_alive(app->ui);
        /* เติม: subscribe_commands(app);
         * เรียกครั้งเดียวก่อนเข้าลูปก็พอ */
        (void)
            subscribe_commands; /* called once the blank above is filled in */
        app->sending = true;
        app->last_send_ms = bento_time_ticks_ms();
        app->good_ms = app->last_send_ms;
        app->ui_ms = app->last_send_ms;
    }
    while (status == BENTO_OK) {
        status = session_round(app);
    }
    return status;
}

/* Always say goodbye to the broker, even when stopped half way. */
static void close_broker(telemetry_t *app)
{
    if (app->broker != 0U) {
        (void)bento_mqtt_close(
            app->broker); /* the client is freed either way */
        app->broker = 0U;
    }
    (void)printf("[MQTT] Disconnected\n");
    show_link(app, false);
}

int main(void)
{
    static const char *const lamp_names[] = {"RGB_GREEN", "LED2"};
    telemetry_t app;
    bento_status_t status;

    (void)memset(&app, 0, sizeof(app));
    app.ui = bento_runtime_get_ui();
    app.lamp = led_named(lamp_names, 2U, 0U);
    warm_up_sensors();
    (void)bento_lcd_clear(); /* lcd's refusals are ignored (modlcd.c) */
    (void)bento_lcd_console("<h2>MQTT Telemetry - คาบ 10</h2>");
    status = build_screen(&app);
    if (status == BENTO_OK) {
        status = join_wifi();
    }
    if (status == BENTO_OK) {
        show_wifi(&app);
        status = run_session(&app);
        close_broker(&app);
    }
    if (status == BENTO_E_BAD_TYPE) {
        (void)printf(
            "AttributeError: '%s' object has no attribute 'get'\n",
            app.bad_type);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
