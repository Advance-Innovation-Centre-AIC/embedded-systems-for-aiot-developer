/*
 * 04_subscribe_command.c -- รอคำสั่งจากข้างนอก
 * (Subscribe to a Command)
 *
 * ต่อ WiFi กับ broker ของห้องเรียน แล้ว subscribe topic
 * bento/team03/command ครั้งเดียว จากนั้นถาม get_message() ทุก
 * 100 ms ส่ง {"cmd":"beep"} หรือ {"cmd":"count"} จาก MQTT Explorer
 * มาที่ topic นั้น beep ส่งเสียงโน้ต A4 และนับ count นับอย่างเดียว
 * ป้าย "คำสั่งล่าสุด" บอกคำสั่งที่ได้ เลขใหญ่นับว่าทำไปกี่ครั้ง
 * ข้อความที่ไม่ใช่ JSON หรือคำสั่งที่ไม่รู้จักขึ้นสีแดงแต่ไม่ทำให้
 * โปรแกรมตาย ป้ายล่างสุดเต้นเป็นจุดตลอดเวลาเพื่อบอกว่าลูปยังเดิน
 * ทำงานจนกด Stop แล้วบอกลา broker เสมอ
 *
 * Joins the class Wi-Fi and broker, subscribes once to
 * bento/team03/command, then asks get_message() every 100 ms. Send
 * {"cmd":"beep"} or {"cmd":"count"} from MQTT Explorer to that
 * topic: beep plays the note A4 and counts, count only counts. The
 * "last command" label shows what came in and the big number how
 * many were carried out. A message that is not JSON, or a command
 * it does not know, turns red but never kills the program. The
 * bottom label keeps ticking to prove the loop is alive. Runs until
 * Stop, and always says goodbye to the broker.
 *
 * Twin of 04_subscribe_command.py
 * (embedded-systems-for-aiot-developer/examples/s10/04_subscribe_command.py),
 * merged from the library twin aiot-s10-04-subscribe-command. Style: CLEAN_C.md.
 *
 * Differs from the original:
 *   - There is no json module in C. read_command() follows
 *     MicroPython's own json.loads (extmod/modjson.c) step by step:
 *     "," and ":" count as spaces there, a closing bracket of either
 *     kind closes, and one pending key is shared by every level.
 *     So the same texts are JSON or "not JSON" here as there, and
 *     cmd gets the same value. str() of that value is the same for
 *     a string, an integer, true, false and null. A real number, a
 *     list or an object as cmd's value is shown as it was sent,
 *     where MicroPython prints its own form of it.
 *   - A JSON document that is not an object (a number, a list) has
 *     no .get(): MicroPython raises AttributeError there and the
 *     program ends after its cleanup. Here the same, with the last
 *     line of the traceback and BENTO_E_BAD_TYPE.
 *   - The binding keeps one message: a newer one overwrites an
 *     unread one, and the payload is cut to 255 bytes. The C client
 *     keeps four; the loop reads them all and handles only the
 *     newest, cut to 255 bytes, which is what the binding hands
 *     over. More than four between two reads keep the oldest four
 *     here, where the binding keeps the newest.
 *   - wifi.connect() and mqtt.* print console lines of their own.
 *     Here the program prints the ones C can know ("[WiFi]
 *     Connecting...", "[WiFi] Connected! IP=...", "[MQTT]
 *     Connecting to ...", "[MQTT] Connected to ... as '...'",
 *     "[MQTT] Subscribed to ...", "[MQTT] subscribe rejected by
 *     broker", "[MQTT] Disconnected"). Not printed, as no C call
 *     reports them: the radio's per-attempt and result-code lines,
 *     "[WiFi] Credentials saved to QSPI" and "[MQTT] Library
 *     initialized".
 *   - Stop ends the program quietly after the same cleanup, where
 *     MicroPython raises KeyboardInterrupt.
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

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 20,
    TITLE_Y = 12,
    TITLE_PX = 24,
    TEXT_PX = 20,
    LED_X = 606,
    LED_Y = 12,
    LED_SIZE = 18,
    LINK_X = 628,
    LINK_Y = 8,
    CARD_W = 652,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    TOPIC_CARD_Y = 52,
    TOPIC_CARD_H = 132,
    INNER_X = 36,
    TOPIC_CAPTION_Y = 62,
    TOPIC_Y = 88,
    LAST_CAPTION_Y = 122,
    LAST_Y = 148,
    COUNT_CARD_Y = 196,
    COUNT_CARD_H = 120,
    COUNT_CAPTION_Y = 202,
    SEG_Y = 228,
    SEG_W = 180,
    SEG_H = 76,
    HINT_X = 240,
    HINT_Y = 236,
    HINT2_Y = 264,
    ALIVE_Y = 336
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

/* Timing, the network and the sizes of texts. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,   /* often enough for a command a person sends */
    POLL_MS = 20,    /* ask a pending connect this often */
    BROKER_PORT = 1883,
    CLIENT_SUFFIX_MASK = 0xFFFF, /* four hex digits of the clock */
    NOTE_A4 = 69,                /* a MIDI note 0-127, not 880 Hz */
    TONE_VELOCITY = 90,
    TONE_MS = 150,
    DOT_KINDS = 4,     /* 0 to 3 dots, then again */
    PAYLOAD_MAX = 255, /* what the binding keeps of a payload */
    TOPIC_CHARS = 128,
    CLIENT_ID_CHARS = 24,
    TEXT_CHARS = 128,  /* a drawer line carries at most 127 bytes */
    VALUE_CHARS = 128, /* str() of cmd, as far as a label shows it */
    /* Words + str(cmd): up to 57 bytes of Thai and 127 of cmd. The
     * whole line goes to the label and the drawer, which cut it to
     * what they carry, as they do for the original. */
    ACTION_LINE_CHARS = VALUE_CHARS + 64
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t link_led;
    bento_ui_id_t link_text;
    bento_ui_id_t topic;
    bento_ui_id_t last;
    bento_ui_id_t count;
    bento_ui_id_t alive;
} command_screen_t;

/* Where a label goes, its colour and its font size. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} spot_t;

/* The kinds of value json.loads makes. */
/* Text pieces of a bytes repr: one escaped byte, and the b' opening. */
enum {
    REPR_PIECE_BYTES = 8,
    REPR_OPENING_BYTES = 4
};

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

/* What json.loads(payload).get("cmd", "") comes to. */
typedef struct {
    bool parsed;      /* false: json.loads raised ValueError */
    value_kind_t top; /* the kind of the whole document */
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

/* The program's state between rounds. */
typedef struct {
    command_screen_t screen;
    uint32_t broker; /* the MQTT client, 0 when none */
    int32_t counter;
    int32_t ticks;
    const char *bad_type; /* the type that had no .get() */
} command_app_t;

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
    static const uint32_t limits[] = {0x80U, 0x800U, 0x10000U};
    static const uint8_t leads[] = {0x00U, 0xC0U, 0xE0U, 0xF0U};
    const uint32_t six_bits = 0x3FU;
    const uint32_t follow = 0x80U;
    const uint32_t bits_per_follow = 6U;
    const uint32_t limit_count =
        (uint32_t)(sizeof(limits) / sizeof(limits[0]));
    uint8_t bytes[sizeof(leads)];
    uint32_t count = 1U;
    uint32_t rest = code;
    uint32_t i;

    while ((count <= limit_count) && (code >= limits[count - 1U])) {
        count++;
    }
    for (i = count - 1U; i > 0U; i--) {
        bytes[i] = (uint8_t)(follow | (rest & six_bits));
        rest >>= bits_per_follow;
    }
    bytes[0] = (uint8_t)((uint32_t)leads[count - 1U] | rest);
    value_append(value, bytes, count);
}

/* \uXXXX: four characters read as hex digits with no check,
 * exactly the arithmetic modjson.c does on a byte. */
static uint32_t read_unicode_escape(json_reader_t *reader)
{
    const uint8_t lower_case = 0x20U;
    const uint8_t letter_gap = (uint8_t)('a' - ('9' + 1));
    const uint32_t hex_digits = 4U;
    const uint32_t bits_per_hex = 4U;
    const uint8_t highest_digit = 9U;
    uint32_t code = 0U;
    uint32_t i;

    for (i = 0U; i < hex_digits; i++) {
        uint8_t digit;

        reader_advance(reader);
        digit = (uint8_t)((reader_peek(reader) | lower_case) - '0');
        if (digit > highest_digit) {
            digit = (uint8_t)(digit - letter_gap);
        }
        code = (code << bits_per_hex) | digit;
    }
    return code;
}

/* The character a backslash escape stands for. */
static uint8_t escaped_char(uint8_t letter)
{
    uint8_t result = letter; /* \" \\ \/ and any other: itself */

    if (letter == (uint8_t)'b') {
        result = (uint8_t)'\b';
    } else if (letter == (uint8_t)'f') {
        result = (uint8_t)'\f';
    } else if (letter == (uint8_t)'n') {
        result = (uint8_t)'\n';
    } else if (letter == (uint8_t)'r') {
        result = (uint8_t)'\r';
    } else if (letter == (uint8_t)'t') {
        result = (uint8_t)'\t';
    } else {
        /* kept as it is */
    }
    return result;
}

/* One character of a string, or one escape. Leaves the cursor on
 * the last character it used. */
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

/* null, true, false: the first letter is read, the rest must
 * follow. */
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

/* int(text): a sign, then digits only. str() of it drops leading
 * zeros and the sign of zero. */
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

/* After an exponent's 'e': an optional sign. */
static uint32_t skip_exponent_sign(const uint8_t *text, uint32_t length,
                                   uint32_t at)
{
    uint32_t next = at;

    if ((next < length) && ((text[next] == (uint8_t)'+') ||
                            (text[next] == (uint8_t)'-'))) {
        next++;
    }
    return next;
}

/* float(text) as parsenum.c reads it: a sign, digits with at most
 * one '.', then at most one exponent that must not end the text
 * right after its 'e' and sign. Shown as it was sent. */
static bool read_real(const uint8_t *text, uint32_t length,
                      json_value_t *value)
{
    const uint8_t lower_case = 0x20U;
    uint32_t at = ((text[0] == (uint8_t)'-') ? 1U : 0U);
    const uint32_t start = at;
    bool in_fraction = false;
    bool in_exponent = false;
    bool valid = true;
    bool more = true;

    while ((at < length) && valid && more) {
        const uint8_t current = text[at];

        at++;
        if (is_digit(current)) {
            /* a digit of whatever part it is in */
        } else if ((current == (uint8_t)'.') && !in_fraction &&
                   !in_exponent) {
            in_fraction = true;
        } else if (((current | lower_case) == (uint8_t)'e') &&
                   !in_exponent) {
            in_exponent = true;
            at = skip_exponent_sign(text, length, at);
            valid = (at < length);
        } else {
            at--; /* a sign in the wrong place ends the number */
            more = false;
        }
    }
    value_set(value, KIND_FLOAT, "");
    value_append(value, text, length);
    return valid && (at != start) && (at == length);
}

/* A number: its first character is read; digits, signs, '.', 'e'
 * and 'E' follow. A '.', 'e' or 'E' makes it a real number. */
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

/* null, true or false: the rest of the word must follow. */
static bool read_literal(json_reader_t *reader, json_value_t *value,
                         uint8_t first)
{
    bool valid;

    if (first == (uint8_t)'n') {
        value_set(value, KIND_NONE, "None");
        valid = read_word(reader, "ull");
    } else if (first == (uint8_t)'t') {
        value_set(value, KIND_BOOL, "True");
        valid = read_word(reader, "rue");
    } else {
        value_set(value, KIND_BOOL, "False");
        valid = read_word(reader, "alse");
    }
    return valid;
}

/* What one character starts. */
typedef enum {
    STEP_SKIP = 0,
    STEP_VALUE,
    STEP_OPEN,
    STEP_CLOSE,
    STEP_BAD
} step_t;

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
    case 'n':
    case 't':
    case 'f':
        valid = read_literal(reader, value, current);
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
        if ((current == (uint8_t)'-') || is_digit(current)) {
            valid = read_number(reader, value);
        } else {
            valid = false;
        }
        break;
    }
    return valid ? step : STEP_BAD;
}

/* A value inside an object: a key, or the value of the pending
 * key. Only cmd's value in the outermost object is kept. */
static bool place_in_object(json_reader_t *reader,
                            const json_value_t *value, bool opens,
                            command_t *command)
{
    static const char cmd_key[] = "cmd";
    const uint32_t cmd_length = (uint32_t)(sizeof(cmd_key) - 1U);
    bool valid = true;

    if (!reader->key_pending) {
        reader->key_pending = true;
        reader->key_is_cmd =
            (value->kind == KIND_TEXT) &&
            (value->length == cmd_length) &&
            (memcmp(value->text, cmd_key, cmd_length) == 0);
        valid = !opens; /* a list or object cannot be a key */
    } else {
        reader->key_pending = false;
        if (reader->key_is_cmd && (reader->depth == 1U)) {
            command->action = *value;
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

/* A closing bracket: back to the parent, or the end of the
 * document. cmd's own list or object keeps its text. */
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
    return (character == (uint8_t)' ') ||
           ((character >= (uint8_t)'\t') &&
            (character <= (uint8_t)'\r'));
}

/* utf8_check(): what bytes.decode() accepts. */
static bool is_utf8(const uint8_t *bytes, uint32_t length)
{
    const uint8_t follow_mask = 0xC0U;
    const uint8_t follow = 0x80U;
    const uint8_t lead_limit =
        0xF8U; /* 11111xxx never starts a character */
    const uint32_t need_table =
        0xE5U; /* utf8_check's packed counts, two bits per lead */
    const uint32_t lead_shift = 3U;
    const uint32_t table_pick = 0x6U;
    const uint32_t count_mask = 3U;
    uint32_t need = 0U;
    uint32_t i;
    bool valid = true;

    for (i = 0U; (i < length) && valid; i++) {
        const uint8_t current = bytes[i];

        if (need != 0U) {
            valid = ((current & follow_mask) == follow);
            need--;
        } else if (current >= lead_limit) {
            valid = false;
        } else if (current >= follow_mask) {
            need = (need_table >>
                    (((uint32_t)current >> lead_shift) & table_pick)) &
                   count_mask;
        } else {
            valid = (current < follow);
        }
    }
    return valid && (need == 0U);
}

/* json.loads(raw.decode()) and .get("cmd", ""). The stream ends
 * at the first NUL byte, as modjson.c reads it. */
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
        value_set(&command->action, KIND_TEXT, ""); /* cmd = {} */
    }
}

/* ---- what the program does with a message ---- */

/* Appends text to a line, as far as the line holds. */
static void line_append(char *line, uint32_t cap, const char *text)
{
    const size_t used = strlen(line);

    if ((used + 1U) < cap) {
        (void)snprintf(&line[used], cap - used, "%s", text);
    }
}

/* One byte of a bytes repr: escaped as MicroPython escapes it. */
static void append_repr_byte(char *line, uint32_t cap, uint8_t byte,
                             uint8_t quote)
{
    const uint8_t first_printable = 0x20U;
    const uint8_t delete_char = 0x7FU;
    char piece[REPR_PIECE_BYTES];

    if (byte == quote) {
        (void)snprintf(piece, sizeof(piece), "\\%c", (char)quote);
    } else if (byte == (uint8_t)'\\') {
        (void)snprintf(piece, sizeof(piece), "\\\\");
    } else if ((byte >= first_printable) && (byte < delete_char)) {
        (void)snprintf(piece, sizeof(piece), "%c", (char)byte);
    } else if (byte == (uint8_t)'\n') {
        (void)snprintf(piece, sizeof(piece), "\\n");
    } else if (byte == (uint8_t)'\r') {
        (void)snprintf(piece, sizeof(piece), "\\r");
    } else if (byte == (uint8_t)'\t') {
        (void)snprintf(piece, sizeof(piece), "\\t");
    } else {
        (void)snprintf(piece, sizeof(piece), "\\x%02x",
                       (unsigned int)byte);
    }
    line_append(line, cap, piece);
}

/* str() of the raw payload, b'...': single quotes unless the
 * payload has a ' and no ". */
static void append_bytes_repr(char *line, uint32_t cap,
                              const uint8_t *payload, uint32_t length)
{
    bool has_single = false;
    bool has_double = false;
    uint8_t quote = (uint8_t)'\'';
    char opening[REPR_OPENING_BYTES];
    uint32_t i;

    for (i = 0U; (i < length) && !has_double; i++) {
        has_single = has_single || (payload[i] == (uint8_t)'\'');
        has_double = (payload[i] == (uint8_t)'"');
    }
    if (has_single && !has_double) {
        quote = (uint8_t)'"';
    }
    (void)snprintf(opening, sizeof(opening), "b%c", (char)quote);
    line_append(line, cap, opening);
    for (i = 0U; i < length; i++) {
        append_repr_byte(line, cap, payload[i], quote);
    }
    (void)snprintf(opening, sizeof(opening), "%c", (char)quote);
    line_append(line, cap, opening);
}

static bool is_word(const json_value_t *action, const char *word)
{
    const size_t length = strlen(word);

    return (action->kind == KIND_TEXT) && (action->length == length) &&
           (memcmp(action->text, word, length) == 0);
}

/* The Python type name of a document that is not an object. */
static const char *type_name(value_kind_t kind)
{
    static const char *const names[] = {
        "NoneType", "bool", "int", "float", "str", "list", "dict"};

    return names[kind];
}

static const char WIFI_SSID[] = "AIoT-Class";
static const char WIFI_PASS[] = "<รหัสผ่านของห้องเรียน>";
static const char BROKER[] = "192.168.1.50";

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Prints the line wifi.connect() prints on success. */
static void print_wifi_address(void)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    uint32_t ip_v4 = 0U;
    uint32_t length = 0U;

    if (bento_wifi_ip(BENTO_WIFI_IF_STA, &ip_v4) != BENTO_OK) {
        ip_v4 = 0U; /* an unknown address prints as 0.0.0.0 */
    }
    /* a BENTO_WIFI_IPV4_TEXT_MAX buffer holds any address */
    (void)bento_wifi_ipv4_format(ip_v4, address,
                                 (uint32_t)sizeof(address), &length);
    (void)printf("[WiFi] Connected! IP=%s\r\n", address);
}

/* wifi.connect(): *joined is its True or False. A refused or
 * timed-out join is False; any other failure ends the program. */
static bento_status_t join_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
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
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(POLL_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_connect_poll();
        } else {
            (void)bento_wifi_connect_cancel(); /* Stop wins */
        }
    }
    *joined = (status == BENTO_OK);
    if ((status == BENTO_E_FAILED) || (status == BENTO_E_TIMEOUT) ||
        (status == BENTO_E_NOT_FOUND)) {
        status = BENTO_OK;
    }
    if (*joined) {
        print_wifi_address();
    }
    return status;
}

/* mqtt.connect(): a refused or timed-out connect is its False. A
 * client that could not even start (no radio) ends the program,
 * as the binding's raise does. */
static bento_status_t open_broker(const char *client_id,
                                  uint32_t *broker, bool *online)
{
    bento_mqtt_config_t config;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = (uint16_t)BROKER_PORT;
    config.client_id = client_id;
    config.client_id_len = (uint32_t)strlen(client_id);
    (void)printf("[MQTT] Connecting to %s:%d ...\r\n", BROKER,
                 BROKER_PORT);
    status = bento_mqtt_open_start(&config, broker);
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(POLL_MS);
        if (status == BENTO_OK) {
            status = bento_mqtt_open_poll(*broker);
        }
    }
    *online = (status == BENTO_OK);
    if (*online) {
        (void)printf("[MQTT] Connected to %s:%d as '%s'\r\n", BROKER,
                     BROKER_PORT, client_id);
    } else if ((status != BENTO_E_CANCELLED) && (*broker != 0U)) {
        status = BENTO_OK;
    } else {
        /* no client, or Stop: the status ends the program */
    }
    return status;
}

/* The widget setters ignore a refusal in MicroPython (modui.c), so
 * they do here too. */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

/* ui.poll() hands the touches over; nothing reads them, and a
 * failed poll answers no events, so its result is not needed. */
static void poll_display(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const spot_t *at, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = at->x;
    args.y = at->y;
    args.color = at->color;
    args.font_px = at->font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A dim caption nobody changes later. */
static bento_status_t add_caption(bento_ui_t *ui, const char *text,
                                  int16_t x, int16_t y)
{
    const spot_t at = {x, y, COLOR_DIM, TEXT_PX};
    bento_ui_id_t label;

    return add_label(ui, text, &at, &label);
}

/* A rounded card with a dim one-pixel border. */
static bento_status_t add_card(bento_ui_t *ui, int16_t y, int16_t h)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = y;
    args.w = CARD_W;
    args.h = h;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM;   /* the border colour */
    args.max_val = CARD_RADIUS; /* the corner radius */
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* The title and the MQTT light: green = on the broker. */
static bento_status_t add_header(command_screen_t *screen)
{
    const spot_t title = {MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX};
    const spot_t link = {LINK_X, LINK_Y, COLOR_DIM, TEXT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t label;
    bento_status_t status =
        add_label(screen->ui, "รอคำสั่งจากข้างนอก", &title, &label);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = LED_X;
        args.y = LED_Y;
        args.w = LED_SIZE;
        args.h = LED_SIZE;
        args.color = COLOR_OK;
        args.font_px = 0;
        status =
            bento_ui_led_create(screen->ui, &args, &screen->link_led);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "MQTT: ออฟไลน์", &link,
                           &screen->link_text);
    }
    return status;
}

/* The lower card: how many commands were carried out. */
static bento_status_t add_count_card(command_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status =
        add_card(screen->ui, COUNT_CARD_Y, COUNT_CARD_H);

    if (status == BENTO_OK) {
        status = add_caption(screen->ui, "ทำตามคำสั่งไปแล้วกี่ครั้ง", INNER_X,
                             COUNT_CAPTION_Y);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "0";
        args.text_len = 1U;
        args.x = INNER_X;
        args.y = SEG_Y;
        args.w = SEG_W;
        args.h = SEG_H;
        args.color = COLOR_OK;
        status =
            bento_ui_seg7_create(screen->ui, &args, &screen->count);
    }
    if (status == BENTO_OK) {
        status = add_caption(screen->ui,
                             "ส่ง {\"cmd\":\"beep\"}"
                             " หรือ {\"cmd\":\"count\"}",
                             HINT_X, HINT_Y);
    }
    if (status == BENTO_OK) {
        status = add_caption(screen->ui,
                             "มาที่ topic ข้างบน จาก MQTT "
                             "Explorer",
                             HINT_X, HINT2_Y);
    }
    return status;
}

/* The light and its label always change together. */
static void show_link(const command_screen_t *screen, bool online)
{
    (void)bento_ui_set_value(screen->ui, screen->link_led,
                             online ? 1 : 0);
    set_text(screen->ui, screen->link_text,
             online ? "MQTT: เชื่อมต่อแล้ว" : "MQTT: ออฟไลน์");
}

/* A failure the program stops on: red on the screen, red in the
 * drawer. */
static void show_failure(const command_screen_t *screen,
                         const char *text)
{
    char line[TEXT_CHARS];

    (void)bento_ui_set_color(screen->ui, screen->alive, COLOR_BAD);
    set_text(screen->ui, screen->alive, text);
    poll_display(screen->ui);
    (void)snprintf(line, sizeof(line), "<span class=err>%s</span>",
                   text);
    (void)bento_lcd_print(line);
}

/* People send anything: a text that is not JSON must not kill
 * the program. */
static void show_not_json(const command_screen_t *screen,
                          const uint8_t *payload, uint32_t length)
{
    char line[TEXT_CHARS];

    (void)snprintf(line, sizeof(line), "ได้ข้อความที่ไม่ใช่ JSON: ");
    (void)bento_ui_set_color(screen->ui, screen->last, COLOR_BAD);
    set_text(screen->ui, screen->last, "ไม่ใช่ JSON");
    append_bytes_repr(line, sizeof(line), payload, length);
    (void)bento_lcd_print(line);
}

/* The ticking dots prove the loop is alive: a still screen and a
 * dead one look exactly alike. */
static void show_alive(command_app_t *app)
{
    static const char *const dots[DOT_KINDS] = {"", ".", "..", "..."};
    char text[TEXT_CHARS];

    app->ticks++;
    (void)snprintf(text, sizeof(text), "กำลังรอคำสั่ง%s   รอบที่ %ld",
                   dots[app->ticks % DOT_KINDS], (long)app->ticks);
    set_text(app->screen.ui, app->screen.alive, text);
}

static const char DEVICE_ID[] = "team03";
static const char TOPIC_CMD[] = "bento/team03/command";

/* The upper card: the topic watched and the last command. */
static bento_status_t add_topic_card(command_screen_t *screen)
{
    const spot_t topic = {INNER_X, TOPIC_Y, COLOR_INFO, TEXT_PX};
    const spot_t last = {INNER_X, LAST_Y, COLOR_TEXT, TEXT_PX};
    bento_status_t status =
        add_card(screen->ui, TOPIC_CARD_Y, TOPIC_CARD_H);

    if (status == BENTO_OK) {
        status = add_caption(screen->ui, "topic ที่เฝ้าอยู่", INNER_X,
                             TOPIC_CAPTION_Y);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, TOPIC_CMD, &topic, &screen->topic);
    }
    if (status == BENTO_OK) {
        status = add_caption(screen->ui, "คำสั่งล่าสุดที่ได้รับ", INNER_X,
                             LAST_CAPTION_Y);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "- ยังไม่มี -", &last, &screen->last);
    }
    return status;
}

/* Draws the screen in the original's order. */
static bento_status_t build_screen(command_screen_t *screen)
{
    const spot_t alive = {MARGIN_X, ALIVE_Y, COLOR_DIM, TEXT_PX};
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_header(screen);
    }
    if (status == BENTO_OK) {
        status = add_topic_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_count_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังต่อเน็ต...", &alive,
                           &screen->alive);
    }
    return status;
}

/* mqtt.subscribe(): false when the broker refuses this topic,
 * which happens when the account may not read it. */
static bool subscribe_topic(uint32_t broker)
{
    uint8_t granted_qos = 0U;
    const bento_status_t status = bento_mqtt_subscribe(
        broker, TOPIC_CMD, (uint32_t)strlen(TOPIC_CMD), 0U,
        &granted_qos);

    if (status == BENTO_OK) {
        (void)printf("[MQTT] Subscribed to '%s' (QoS=%d)\r\n",
                     TOPIC_CMD, (int)granted_qos);
    } else if (status == BENTO_E_FAILED) {
        (void)printf("[MQTT] subscribe rejected by broker\r\n");
    } else {
        /* the binding prints a result code C does not have */
    }
    return status == BENTO_OK;
}

/* beep, count, or a command nobody knows. */
static void carry_out(command_app_t *app, const json_value_t *action)
{
    const command_screen_t *screen = &app->screen;
    char line[ACTION_LINE_CHARS];

    if (is_word(action, "beep") || is_word(action, "count")) {
        if (is_word(action, "beep")) {
            /* the sound core may be absent; MicroPython's tone()
             * sends and does not wait for an answer either */
            (void)bento_ui_sfx_tone(screen->ui, NOTE_A4,
                                    BENTO_UI_WAVE_SQUARE, TONE_VELOCITY,
                                    TONE_MS);
        }
        app->counter++;
        (void)bento_ui_set_color(screen->ui, screen->last, COLOR_OK);
        set_text(screen->ui, screen->last, action->text);
        if (is_word(action, "beep")) {
            (void)snprintf(line, sizeof(line), "ทำตามคำสั่ง beep");
        } else {
            (void)snprintf(line, sizeof(line),
                           "ทำตามคำสั่ง count -> "
                           "%ld",
                           (long)app->counter);
        }
    } else {
        (void)bento_ui_set_color(screen->ui, screen->last, COLOR_BAD);
        (void)snprintf(line, sizeof(line), "ไม่รู้จัก: %s", action->text);
        set_text(screen->ui, screen->last, line);
        (void)snprintf(line, sizeof(line), "ไม่รู้จักคำสั่งนี้: %s",
                       action->text);
    }
    (void)bento_lcd_print(line);
}

/* One message: parse it, carry it out, show the count. */
static bento_status_t handle_message(command_app_t *app,
                                     const uint8_t *payload,
                                     uint32_t length)
{
    command_t command;
    char digits[TEXT_CHARS];
    bento_status_t status = BENTO_OK;

    read_command(payload, length, &command);
    if (!command.parsed) {
        show_not_json(&app->screen, payload, length);
    }
    if (command.parsed && (command.top != KIND_DICT)) {
        app->bad_type = type_name(command.top); /* no .get() */
        status = BENTO_E_BAD_TYPE;
    } else {
        carry_out(app, &command.action);
        (void)snprintf(digits, sizeof(digits), "%ld",
                       (long)app->counter);
        set_text(app->screen.ui, app->screen.count, digits);
    }
    return status;
}

/* mqtt.get_message(): the binding keeps one message, the newest,
 * cut to 255 bytes. Reading the client's ring empty gives the
 * same. Answers whether there was one. */
static bool take_newest(uint32_t broker, uint8_t *payload,
                        uint32_t *length)
{
    bento_mqtt_msg_t message;
    char topic[TOPIC_CHARS];
    bool found = false;
    bool more = true;

    while (more) {
        bento_mqtt_msg_init(&message);
        /* get_message() never raises: any refusal is "no message" */
        more = (bento_mqtt_poll(broker, &message, topic,
                                (uint32_t)sizeof(topic), payload,
                                (uint32_t)PAYLOAD_MAX) == BENTO_OK);
        if (more) {
            found = true;
            *length = message.payload_len;
        }
    }
    return found;
}

/* Ask every round and never sleep long: messages that come close
 * together are lost when the rounds are far apart. */
static bento_status_t run_loop(command_app_t *app)
{
    uint8_t payload[PAYLOAD_MAX];
    uint32_t length = 0U;
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        if (take_newest(app->broker, payload, &length)) {
            status = handle_message(app, payload, length);
        }
        if (status == BENTO_OK) {
            show_alive(app);
            poll_display(app->screen.ui);
            status = wait_ms(LOOP_MS);
        }
    }
    return status;
}

/* Subscribe once, before the loop, then wait for commands. */
static bento_status_t watch_topic(command_app_t *app)
{
    const command_screen_t *screen = &app->screen;
    char line[TEXT_CHARS];
    bento_status_t status = BENTO_OK;

    if (!subscribe_topic(app->broker)) {
        (void)bento_ui_set_color(screen->ui, screen->topic, COLOR_BAD);
        show_failure(screen, "broker ปฏิเสธการ subscribe");
    } else {
        (void)bento_ui_set_color(screen->ui, screen->topic, COLOR_OK);
        set_text(screen->ui, screen->alive, "กำลังรอคำสั่ง");
        poll_display(screen->ui);
        (void)snprintf(line, sizeof(line),
                       "<span class=ok>รอคำสั่งที่</span> "
                       "%s",
                       TOPIC_CMD);
        (void)bento_lcd_print(line);
        (void)bento_lcd_print(
            "ลองส่ง {\"cmd\":\"beep\"} หรือ {\"cmd\":\"count\"} มาดู");
        status = run_loop(app);
    }
    return status;
}

/* Wi-Fi, the broker, the topic. A step that fails says so and
 * ends the program normally. */
static bento_status_t run_session(command_app_t *app,
                                  const char *client_id)
{
    bool joined = false;
    bool online = false;
    bento_status_t status = join_wifi(&joined);

    if ((status == BENTO_OK) && !joined) {
        show_failure(&app->screen, "ต่อ WiFi ไม่ได้");
    }
    if ((status == BENTO_OK) && joined) {
        status = open_broker(client_id, &app->broker, &online);
    }
    if ((status == BENTO_OK) && joined && !online) {
        show_link(&app->screen, false);
        show_failure(&app->screen, "ต่อ broker ไม่ได้");
    }
    if ((status == BENTO_OK) && online) {
        show_link(&app->screen, true);
        status = watch_topic(app);
    }
    return status;
}

/* Always say goodbye to the broker, even after Stop, so the
 * client id does not linger for another minute. */
static void leave_broker(command_app_t *app)
{
    if (app->broker != 0U) {
        (void)bento_mqtt_close(app->broker); /* the client is freed */
        app->broker = 0U;
    }
    (void)printf("[MQTT] Disconnected\r\n");
    show_link(&app->screen, false);
}

int main(void)
{
    command_app_t app;
    char client_id[CLIENT_ID_CHARS];
    bento_status_t status;

    /* a fresh suffix each run: a restart right after Stop does not
     * collide with the last run's id */
    (void)snprintf(client_id, sizeof(client_id), "%s-%04lx", DEVICE_ID,
                   (unsigned long)(bento_time_ticks_ms() &
                                   (uint32_t)CLIENT_SUFFIX_MASK));
    (void)memset(&app, 0, sizeof(app));
    app.screen.ui = bento_runtime_get_ui();
    status = build_screen(&app.screen);
    if (status == BENTO_OK) {
        poll_display(app.screen.ui);
        /* lcd calls ignore a console that is not ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>คาบ 10 - รับคำสั่ง</h2>");
        status = run_session(&app, client_id);
        leave_broker(&app);
    }
    if (status == BENTO_E_BAD_TYPE) {
        (void)printf("AttributeError: '%s' object has no attribute"
                     " 'get'\n",
                     app.bad_type);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
