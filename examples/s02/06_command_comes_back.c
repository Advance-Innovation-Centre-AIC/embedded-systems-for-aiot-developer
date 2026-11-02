/*
 * 06_command_comes_back.c -- คำสั่งจากที่ไกลมาถึงโต๊ะเรา
 *
 * ต่อ WiFi (Hotspot มือถือของทีม) ต่อ broker สาธารณะ แล้วฟังหัวข้อ
 * bento-aiot/<ทีม>/cmd นาน 15 นาที ทุก 100 ms ถามกล่องรับหนึ่งครั้ง
 * คำสั่งที่รู้จักคือ JSON {"cmd": "beep"} เสียงบี๊บ, {"cmd": "led",
 * "n": 0, "on": 1} เปิดปิดไฟบนบอร์ด และ {"cmd": "say", "text": ...}
 * โชว์ข้อความ ข้อความมั่วหรือคำสั่งแปลกถูกนับว่าใช้ไม่ได้ แต่ไม่ทำให้
 * โปรแกรมตาย ถ้ายังไม่แก้ TEAM โปรแกรมบอกบนจอแล้วจบ
 *
 * Joins the team's phone hotspot and a public broker, then listens
 * on bento-aiot/<team>/cmd for 15 minutes, asking the one-message
 * inbox every 100 ms. Known commands are JSON: {"cmd": "beep"}
 * beeps, {"cmd": "led", "n": 0, "on": 1} switches a board LED and
 * {"cmd": "say", "text": ...} shows a text. Garbage and unknown
 * commands are counted as unusable but never stop the program.
 * Until TEAM is edited the program says so on the screen and ends.
 *
 * Twin of 06_command_comes_back.py
 * (embedded-systems-for-aiot-developer/examples/s02/06_command_comes_back.py).
 * Style: CLEAN_C.md.
 * Differs from the original:
 *   - There is no json module in C. read_command() below follows
 *     MicroPython's own json.loads (extmod/modjson.c): commas and
 *     colons count as spaces, ] and } close either kind, a key left
 *     without a value takes the next value (even outside its
 *     object), the last of two equal keys wins. It finds
 *     what the program asks of the top-level object: "cmd", "n",
 *     "on" and "text". Nesting deeper than JSON_DEPTH_MAX counts as
 *     not JSON (MicroPython nests until memory runs out).
 *   - str() of a value: a string, an integer, true, false and null
 *     read as MicroPython writes them. A real number, a list or an
 *     object is shown as its JSON text (MicroPython: str() of the
 *     parsed value, e.g. 1.50 is 1.5 there, ["a"] is ['a']). A real
 *     number that rounds to 0.0 (1e-99) counts as on here.
 *   - get_message() is one slot that a newer message overwrites
 *     (modmqtt.c:60-63). The C client keeps a ring of
 *     BENTO_MQTT_RING_SLOTS; each round takes them all and keeps the
 *     newest, the slot's behaviour. When more than that arrive
 *     between two rounds, the ring drops the newest instead.
 *   - Text is bytes on the PSoC Edge (its MicroPython has no
 *     unicode str): [:24] cuts bytes, and raw.decode() never fails.
 *     The reTerminal's MicroPython cuts characters and refuses
 *     bytes that are not UTF-8 (not JSON then). Both are kept.
 *   - wifi.* and mqtt.* print console lines of their own. The ones
 *     C can know are printed: "[WiFi] Connecting...", "[WiFi]
 *     Connected! IP=...", "[MQTT] Connecting to ...", "[MQTT]
 *     Connected to ... as '...'", "[MQTT] Subscribed to '...'
 *     (QoS=N)" and "[MQTT] subscribe rejected by broker". Not
 *     printed: the radio's per-attempt and failure lines with result
 *     codes, "Credentials saved to QSPI", "[MQTT] Library
 *     initialized", the cy codes of a failed connect or subscribe
 *     and "[MQTT] Disconnected (reason=N)" (modwifi.c, modmqtt.c).
 *   - wifi.scan(): the C scan keeps the first 20 results with
 *     hidden networks and drops those after (the binding drops them
 *     before counting), so with more than 20 networks around a
 *     hotspot may be "not heard" here where MicroPython hears it.
 *   - The original never disconnects; the soft reset after it does.
 *     Here the client is closed, silently, when the program ends.
 *   - Stop ends the program quietly, where MicroPython raises
 *     KeyboardInterrupt.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_mqtt.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF, /* a value that changes; say */
    COLOR_OK = 0x30A46C,     /* a usable command */
    COLOR_WARN = 0xF5A623,   /* working; an unknown command */
    COLOR_BAD = 0xE5484D
};

/* Times, the beep, and how much of a text is shown. */
enum {
    SETTLE_MS = 200,      /* let the cleared screen settle */
    LISTEN_MS = 900000,   /* 15 minutes: two class games */
    POLL_MS = 100,        /* the longer, the more is overwritten */
    CONNECT_POLL_MS = 20, /* how often a connect is asked */
    MS_PER_S = 1000,
    BROKER_PORT = 1883, /* MQTT without encryption */
    KEEPALIVE_S = 60,   /* quiet longer than this: dropped */
    BEEP_NOTE = 69,     /* MIDI note, not a frequency */
    BEEP_VELOCITY = 90,
    BEEP_MS = 150,
    SAY_MAX = 24, /* a label carries 126 bytes */
    CMD_SHOW_MAX = 16,
    TEAM_LEN = 6, /* "team" and two digits */
    TEAM_PREFIX_LEN = 4
};

/* Buffer sizes, in bytes. */
enum {
    TEXT_BYTES = 128,   /* a label text: 126 at most */
    LINE_BYTES = 160,   /* a console line: 127 are shown */
    NAME_BYTES = 48,    /* client id and topic */
    TOPIC_KEEP = 128,   /* the binding keeps 127 bytes */
    PAYLOAD_KEEP = 255, /* and 255 of the payload */
    KEY_BYTES = 8,      /* the longest key asked for, "text" */
    NUMBER_BYTES = 12,  /* any int32_t, its sign and the NUL */
    VALUE_BYTES = 256,  /* str() of any value: payload + NUL */
    LABEL_BYTES = 320,  /* a label with a whole str() in it */
    JSON_DEPTH_MAX = 16
};

/* The widgets the program changes after it has drawn them. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t status_line;
    bento_ui_id_t link_line;
    bento_ui_id_t count_digits;
    bento_ui_id_t last_line;
    bento_ui_id_t icon;
    bento_ui_id_t topic_line;
    bento_ui_id_t note;
} remote_screen_t;

/* The listener: names, the client and the counts. */
typedef struct {
    char device_id[NAME_BYTES]; /* unique on the broker */
    char topic[NAME_BYTES];
    uint32_t broker; /* the MQTT client, 0 when none */
    uint32_t led_count;
    int32_t got;
    int32_t bad;
    bool stopped; /* the original's raise Stop */
} listener_t;

/* One message taken from the inbox. */
typedef struct {
    uint8_t payload[PAYLOAD_KEEP];
    uint32_t payload_len;
} message_t;

/* What a JSON token or value is. */
typedef enum {
    JSON_MISSING = 0, /* not there, or not JSON at all */
    JSON_STRING,
    JSON_INT,
    JSON_FLOAT,
    JSON_TRUE,
    JSON_FALSE,
    JSON_NULL,
    JSON_LIST,
    JSON_OBJECT,
    JSON_CLOSE /* a ] or a }: either closes either kind */
} json_kind_t;

/* A value: its kind and where its text is in the payload. */
typedef struct {
    json_kind_t kind;
    uint32_t start;
    uint32_t end;     /* one past its last byte */
    bool has_members; /* a list or object with something in it */
} json_value_t;

/* The keys the program reads from the top-level object. */
enum {
    FIELD_CMD = 0,
    FIELD_N = 1,
    FIELD_ON = 2,
    FIELD_TEXT = 3,
    FIELD_COUNT = 4
};

enum {
    FIELD_NONE = FIELD_COUNT
};

/* What json.loads gave, as far as the program asks. */
typedef struct {
    json_kind_t top; /* JSON_MISSING when the text is not JSON */
    json_value_t fields[FIELD_COUNT];
} command_t;

/* One open list or object. */
typedef struct {
    bool is_object;
    int32_t field; /* the field this container is the value of */
    bool has_members;
} json_level_t;

/* The reader's place in the payload and its open containers. Like
 * modjson.c it keeps ONE pending key, not one per level: a key left
 * without a value in an inner object is still waiting when that
 * object closes, and takes the next value outside it. */
typedef struct {
    const uint8_t *text;
    uint32_t len;
    uint32_t offset;
    json_level_t levels[JSON_DEPTH_MAX];
    int32_t depth;
    bool key_pending;  /* a key waits for its value */
    int32_t key_field; /* the field that key names */
    command_t *command;
} json_reader_t;

/* A console line built piece by piece. The console shows 127 bytes
 * of a line, so what does not fit here is never seen there. */
typedef struct {
    char text[LINE_BYTES];
    uint32_t used;
} line_t;

static bool is_digit(uint8_t byte)
{
    return (byte >= (uint8_t)'0') && (byte <= (uint8_t)'9');
}

/* MicroPython's json skips these between values. */
static bool is_separator(uint8_t byte)
{
    return (byte == (uint8_t)',') || (byte == (uint8_t)':') ||
           (byte == (uint8_t)' ') || (byte == (uint8_t)'\t') ||
           (byte == (uint8_t)'\n') || (byte == (uint8_t)'\r');
}

/* What may follow the last value (unichar_isspace). */
static bool is_space(uint8_t byte)
{
    return (byte == (uint8_t)' ') ||
           ((byte >= (uint8_t)'\t') && (byte <= (uint8_t)'\r'));
}

static bool is_exponent(uint8_t byte)
{
    return (byte == (uint8_t)'e') || (byte == (uint8_t)'E');
}

static bool is_number_byte(uint8_t byte)
{
    return is_digit(byte) || is_exponent(byte) ||
           (byte == (uint8_t)'.') || (byte == (uint8_t)'+') ||
           (byte == (uint8_t)'-');
}

/* Is this the reTerminal? Asked at run time, by the name the board
 * gives (gpio.board_info()["name"]), so one Emulator build serves
 * both of its board models. A board that cannot say keeps the build
 * target's answer, as the old #if did. */
static bool on_reterminal(void)
{
    bento_gpio_board_info_t info;
#if defined(BENTO_TARGET_RETERMINAL)
    bool answer = true;
#else
    bool answer = false;
#endif

    bento_gpio_board_info_init(&info);
    if ((bento_gpio_board_info(&info) == BENTO_OK) &&
        (info.name != NULL)) {
        answer = (strcmp(info.name, "Seeed reTerminal") == 0);
    }
    return answer;
}

/* UTF-8: a lead byte tells how many bytes follow; a code point is
 * written back in 1 to 4 bytes of six bits each after the first. */
enum {
    UTF8_FOLLOW_MASK = 0xC0, /* 10xxxxxx: a following byte */
    UTF8_FOLLOW = 0x80,
    UTF8_LEAD_TWO = 0xC0,
    UTF8_LEAD_THREE = 0xE0,
    UTF8_LEAD_FOUR = 0xF0,
    UTF8_LEAD_END = 0xF8, /* from here: not a lead byte */
    UTF8_EXTRA_MAX = 3,
    UTF8_MAX_BYTES = 4,
    UTF8_ONE_END = 0x80,
    UTF8_TWO_END = 0x800,
    UTF8_THREE_END = 0x10000,
    UTF8_SIX_BITS = 0x3F,
    UTF8_SHIFT = 6
};

/* The bytes that follow this lead byte. */
static uint32_t follow_count(uint8_t lead)
{
    return (lead >= (uint32_t)UTF8_LEAD_FOUR) ? (uint32_t)UTF8_EXTRA_MAX
           : (lead >= (uint32_t)UTF8_LEAD_THREE) ? 2U
                                                 : 1U;
}

/* py/unicode.c utf8_check: whole sequences, nothing cut off. */
static bool utf8_valid(const uint8_t *text, uint32_t len)
{
    uint32_t need = 0U;
    uint32_t i;
    bool ok = true;

    for (i = 0U; ok && (i < len); i++) {
        const uint8_t byte = text[i];

        if (need != 0U) {
            ok = (byte & (uint32_t)UTF8_FOLLOW_MASK) ==
                 (uint32_t)UTF8_FOLLOW;
            need--;
        } else if (byte >= (uint32_t)UTF8_LEAD_TWO) {
            ok = byte < (uint32_t)UTF8_LEAD_END;
            need = follow_count(byte);
        } else {
            ok = byte < (uint32_t)UTF8_FOLLOW;
        }
    }
    return ok && (need == 0U);
}

/* bytes.decode(): the reTerminal's MicroPython checks UTF-8; the
 * PSoC Edge's has no unicode str and takes any bytes. */
static bool text_decodes(const uint8_t *text, uint32_t len)
{
    return on_reterminal() ? utf8_valid(text, len) : true;
}

/* null, true, false: every letter must be there. */
static bool read_word(json_reader_t *reader, const char *word)
{
    const uint32_t length = (uint32_t)strlen(word);
    bool same = (reader->len - reader->offset) >= length;

    if (same) {
        same = memcmp(&reader->text[reader->offset], word, length) == 0;
    }
    if (same) {
        reader->offset += length;
    }
    return same;
}

/* Bytes an escape takes, 0 when it runs past the end: \u and four
 * more, any other one byte more. */
static uint32_t escape_span(const json_reader_t *reader,
                            uint32_t offset)
{
    const uint32_t unicode_span = 6U;
    const uint32_t unicode_last = unicode_span - 1U; /* the 4th digit */
    uint32_t span = 0U;

    if ((offset + 1U) < reader->len) {
        span = 2U;
        if (reader->text[offset + 1U] == (uint8_t)'u') {
            span = ((offset + unicode_last) < reader->len)
                       ? unicode_span
                       : 0U;
        }
    }
    return span;
}

/* A string, both quotes included. */
static bool read_string(json_reader_t *reader)
{
    uint32_t offset = reader->offset + 1U;
    bool closed = false;
    bool ok = true;

    while (ok && !closed && (offset < reader->len)) {
        const uint8_t byte = reader->text[offset];

        if (byte == (uint8_t)'"') {
            closed = true;
        } else if (byte == (uint8_t)'\\') {
            const uint32_t span = escape_span(reader, offset);

            ok = span != 0U;
            offset += span;
        } else {
            offset++;
        }
    }
    if (closed) {
        reader->offset = offset + 1U;
    }
    return ok && closed;
}

/* int(): an optional '-', then digits only. */
static bool int_text_ok(const uint8_t *text, uint32_t len)
{
    uint32_t i = (text[0] == (uint8_t)'-') ? 1U : 0U;
    bool ok = i < len;

    for (; ok && (i < len); i++) {
        ok = is_digit(text[i]);
    }
    return ok;
}

/* After an e or E: an optional sign, and something after it. */
static uint32_t skip_sign(const uint8_t *text, uint32_t len,
                          uint32_t at)
{
    const bool signed_exponent =
        (at < len) &&
        ((text[at] == (uint8_t)'+') || (text[at] == (uint8_t)'-'));

    return signed_exponent ? (at + 1U) : at;
}

/* float(), as py/parsenum.c reads it: digits, one '.' before the
 * exponent, one e with an optional sign and something after it. */
static bool float_text_ok(const uint8_t *text, uint32_t len)
{
    const uint32_t first = (text[0] == (uint8_t)'-') ? 1U : 0U;
    uint32_t i = first;
    bool in_fraction = false;
    bool in_exponent = false;
    bool ok = true;
    bool stopped = false;

    while (ok && !stopped && (i < len)) {
        const uint8_t byte = text[i];

        if (is_digit(byte)) {
            i++;
        } else if (!in_fraction && !in_exponent &&
                   (byte == (uint8_t)'.')) {
            in_fraction = true;
            i++;
        } else if (!in_exponent && is_exponent(byte)) {
            in_exponent = true;
            i = skip_sign(text, len, i + 1U);
            ok = i < len;
        } else {
            stopped = true;
        }
    }
    return ok && !stopped && (i > first);
}

/* A number: the run of number bytes; a '.', e or E makes it real. */
static bool read_number(json_reader_t *reader, json_value_t *token)
{
    uint32_t offset = reader->offset + 1U;
    bool real = false;
    bool ok;

    while ((offset < reader->len) &&
           is_number_byte(reader->text[offset])) {
        real = real || is_exponent(reader->text[offset]) ||
               (reader->text[offset] == (uint8_t)'.');
        offset++;
    }
    token->kind = real ? JSON_FLOAT : JSON_INT;
    ok = real ? float_text_ok(&reader->text[reader->offset],
                              offset - reader->offset)
              : int_text_ok(&reader->text[reader->offset],
                            offset - reader->offset);
    reader->offset = offset;
    return ok;
}

/* The next token at reader->offset, which is not a separator. */
static bool read_token(json_reader_t *reader, json_value_t *token)
{
    const uint8_t byte = reader->text[reader->offset];
    bool ok = true;

    token->start = reader->offset;
    token->has_members = false;
    if ((byte == (uint8_t)'{') || (byte == (uint8_t)'[')) {
        token->kind = (byte == (uint8_t)'{') ? JSON_OBJECT : JSON_LIST;
        reader->offset++;
    } else if ((byte == (uint8_t)'}') || (byte == (uint8_t)']')) {
        token->kind = JSON_CLOSE;
        reader->offset++;
    } else if (byte == (uint8_t)'"') {
        token->kind = JSON_STRING;
        ok = read_string(reader);
    } else if (byte == (uint8_t)'n') {
        token->kind = JSON_NULL;
        ok = read_word(reader, "null");
    } else if (byte == (uint8_t)'t') {
        token->kind = JSON_TRUE;
        ok = read_word(reader, "true");
    } else if (byte == (uint8_t)'f') {
        token->kind = JSON_FALSE;
        ok = read_word(reader, "false");
    } else if ((byte == (uint8_t)'-') || is_digit(byte)) {
        ok = read_number(reader, token);
    } else {
        ok = false;
    }
    token->end = reader->offset;
    return ok;
}

/* The byte an escape stands for: \b \f \n \r \t, else the byte
 * itself (\" \\ \/ and any other). */
static uint8_t escaped_byte(uint8_t letter)
{
    static const uint8_t letters[] = {'b', 'f', 'n', 'r', 't'};
    static const uint8_t bytes[] = {0x08U, 0x0CU, 0x0AU, 0x0DU, 0x09U};
    uint8_t byte = letter;
    size_t i;

    for (i = 0U; i < sizeof(letters); i++) {
        if (letter == letters[i]) {
            byte = bytes[i];
        }
    }
    return byte;
}

/* The number after \u, computed as modjson.c does: it does not
 * check that the four bytes are hex digits. */
static uint32_t unicode_number(const uint8_t *digits)
{
    const uint8_t lower_case = 0x20U;
    const uint8_t letter_gap = 39U; /* from '9' + 1 to 'a' */
    const uint8_t digit_max = 9U;
    const uint32_t hex_digits = 4U;
    const uint32_t hex_shift = 4U;
    uint32_t number = 0U;
    uint32_t i;

    for (i = 0U; i < hex_digits; i++) {
        uint8_t digit = (uint8_t)((digits[i] | lower_case) - '0');

        if (digit > digit_max) {
            digit = (uint8_t)(digit - letter_gap);
        }
        number = (number << hex_shift) | digit;
    }
    return number;
}

/* A code point as UTF-8, 1 to 4 bytes; answers how many. */
static uint32_t encode_utf8(uint32_t number, uint8_t *bytes)
{
    static const uint8_t leads[UTF8_MAX_BYTES] = {
        0U, UTF8_LEAD_TWO, UTF8_LEAD_THREE, UTF8_LEAD_FOUR};
    uint32_t count = UTF8_MAX_BYTES;
    uint32_t i;

    if (number < (uint32_t)UTF8_ONE_END) {
        count = 1U;
    } else if (number < (uint32_t)UTF8_TWO_END) {
        count = 2U;
    } else if (number < (uint32_t)UTF8_THREE_END) {
        count = (uint32_t)UTF8_EXTRA_MAX;
    } else {
        count = UTF8_MAX_BYTES;
    }
    for (i = count - 1U; i > 0U; i--) {
        bytes[i] = (uint8_t)((uint32_t)UTF8_FOLLOW |
                             (number & (uint32_t)UTF8_SIX_BITS));
        number >>= (uint32_t)UTF8_SHIFT;
    }
    bytes[0] = (uint8_t)(leads[count - 1U] | number);
    return count;
}

/* What \u adds to the string: its UTF-8 bytes on the reTerminal;
 * on the PSoC Edge (no unicode str) only its low byte. */
static uint32_t unicode_bytes(uint32_t number, uint8_t *bytes)
{
    uint32_t count = 1U;

    if (on_reterminal()) {
        count = encode_utf8(number, bytes);
    } else {
        bytes[0] = (uint8_t)number;
    }
    return count;
}

/* One piece of a string token at text[*offset]: a plain byte, an
 * escape, or \u and four digits. Answers how many bytes it adds. */
static uint32_t decode_piece(const uint8_t *text, uint32_t *offset,
                             uint8_t *piece)
{
    const uint32_t unicode_span = 6U; /* \u and four digits */
    const uint32_t at = *offset;
    uint32_t count = 1U;

    piece[0] = text[at];
    if (text[at] != (uint8_t)'\\') {
        *offset = at + 1U;
    } else if (text[at + 1U] == (uint8_t)'u') {
        count = unicode_bytes(unicode_number(&text[at + 2U]), piece);
        *offset = at + unicode_span;
    } else {
        piece[0] = escaped_byte(text[at + 1U]);
        *offset = at + 2U;
    }
    return count;
}

/* The decoded string inside a string token, cut to cap - 1 bytes
 * and ended with a NUL. Answers its whole length. */
static uint32_t decode_string(const uint8_t *text,
                              const json_value_t *token, char *out,
                              uint32_t cap)
{
    const uint32_t end = token->end - 1U; /* the closing quote */
    uint32_t offset = token->start + 1U;
    uint32_t length = 0U;

    while (offset < end) {
        uint8_t piece[UTF8_MAX_BYTES];
        const uint32_t count = decode_piece(text, &offset, piece);
        uint32_t i;

        for (i = 0U; i < count; i++) {
            out[((length + 1U) < cap) ? length : (cap - 1U)] =
                (char)piece[i];
            length++;
        }
    }
    out[(length < cap) ? length : (cap - 1U)] = '\0';
    return length;
}

/* The field a decoded key names, else FIELD_NONE. */
static int32_t field_index(const char *key, uint32_t length)
{
    static const char *const names[FIELD_COUNT] = {"cmd", "n", "on",
                                                   "text"};
    int32_t found = FIELD_NONE;
    int32_t i;

    for (i = 0; i < FIELD_COUNT; i++) {
        if ((length == (uint32_t)strlen(names[i])) &&
            (memcmp(key, names[i], length) == 0)) {
            found = i;
        }
    }
    return found;
}

/* Which field a key names: a string equal to "cmd", "n", "on" or
 * "text", else FIELD_NONE. */
static int32_t field_named(const json_reader_t *reader,
                           const json_value_t *token)
{
    char key[KEY_BYTES];
    int32_t found = FIELD_NONE;

    if (token->kind == JSON_STRING) {
        const uint32_t length =
            decode_string(reader->text, token, key, sizeof(key));

        found = field_index(key, length);
    }
    return found;
}

static bool is_container(json_kind_t kind)
{
    return (kind == JSON_LIST) || (kind == JSON_OBJECT);
}

/* A token inside a container: a list member, an object's key or
 * that key's value. *field: the field the token is the value of.
 * false: a list or object used as a key. */
static bool add_member(json_reader_t *reader, const json_value_t *token,
                       int32_t *field)
{
    json_level_t *level = &reader->levels[reader->depth - 1];
    bool ok = true;

    *field = FIELD_NONE;
    if (!level->is_object) {
        level->has_members = true;
    } else if (!reader->key_pending) {
        ok = !is_container(token->kind);
        reader->key_pending = true;
        reader->key_field = field_named(reader, token);
    } else {
        reader->key_pending = false; /* the last equal key wins */
        level->has_members = true;
        if (reader->depth == 1) {
            *field = reader->key_field;
        }
        if (*field != FIELD_NONE) {
            reader->command->fields[*field] = *token;
        }
    }
    return ok;
}

static bool open_level(json_reader_t *reader, bool is_object,
                       int32_t field)
{
    const bool room = reader->depth < JSON_DEPTH_MAX;

    if (room) {
        json_level_t *level = &reader->levels[reader->depth];

        level->is_object = is_object;
        level->field = field;
        level->has_members = false;
        reader->depth++;
    }
    return room;
}

/* A ] or }: the innermost container ends here. */
static void close_level(json_reader_t *reader, uint32_t end)
{
    const json_level_t *level = &reader->levels[reader->depth - 1];

    if (level->field != FIELD_NONE) {
        json_value_t *value = &reader->command->fields[level->field];

        value->end = end;
        value->has_members = level->has_members;
    }
    reader->depth--;
}

/* One token into the structure. *done: the top value is complete. */
static bool take_token(json_reader_t *reader, const json_value_t *token,
                       bool *done)
{
    int32_t field = FIELD_NONE;
    bool ok = true;

    if (token->kind == JSON_CLOSE) {
        ok = reader->depth > 0; /* nothing open: no value at all */
        if (ok) {
            close_level(reader, token->end);
            *done = reader->depth == 0;
        }
    } else if (reader->command->top == JSON_MISSING) {
        reader->command->top = token->kind;
        *done = !is_container(token->kind);
    } else {
        ok = add_member(reader, token, &field);
    }
    if (ok && is_container(token->kind)) {
        ok = open_level(reader, token->kind == JSON_OBJECT, field);
    }
    return ok;
}

/* json.loads(raw.decode()), as far as the program asks: the kind of
 * the top value, and the four fields when it is an object. */
static void read_command(const uint8_t *payload, uint32_t len,
                         command_t *command)
{
    json_reader_t reader;
    json_value_t token;
    bool done = false;
    bool ok = text_decodes(payload, len);
    int32_t i;

    command->top = JSON_MISSING;
    for (i = 0; i < FIELD_COUNT; i++) {
        command->fields[i].kind = JSON_MISSING;
        command->fields[i].has_members = false;
    }
    reader.text = payload;
    reader.len = len;
    reader.offset = 0U;
    reader.depth = 0;
    reader.key_pending = false;
    reader.key_field = FIELD_NONE;
    reader.command = command;
    while (ok && !done) {
        while ((reader.offset < len) &&
               is_separator(payload[reader.offset])) {
            reader.offset++;
        }
        ok = reader.offset < len; /* ended inside a value */
        if (ok) {
            ok = read_token(&reader, &token);
        }
        if (ok) {
            ok = take_token(&reader, &token, &done);
        }
    }
    while ((reader.offset < len) && is_space(payload[reader.offset])) {
        reader.offset++;
    }
    if (!ok || (reader.offset != len)) {
        command->top = JSON_MISSING;
    }
}

/* Copies len bytes after out[length], as far as cap - 1 allows,
 * and ends the text with a NUL. Answers the new length. */
static uint32_t copy_bytes(char *out, uint32_t cap, uint32_t length,
                           const uint8_t *bytes, uint32_t len)
{
    uint32_t used = length;
    uint32_t i;

    for (i = 0U; (i < len) && ((used + 1U) < cap); i++) {
        out[used] = (char)bytes[i];
        used++;
    }
    out[used] = '\0';
    return used;
}

/* str() of an integer: its sign and digits without leading zeros
 * (MicroPython's int has no size limit, so no number is parsed). */
static uint32_t int_text(const uint8_t *text, uint32_t len, char *out,
                         uint32_t cap)
{
    const bool negative = text[0] == (uint8_t)'-';
    uint32_t first = negative ? 1U : 0U;
    uint32_t length = 0U;
    bool zero;

    while (((first + 1U) < len) && (text[first] == (uint8_t)'0')) {
        first++;
    }
    zero = ((first + 1U) == len) && (text[first] == (uint8_t)'0');
    if (negative && !zero) {
        length = copy_bytes(out, cap, length, text, 1U);
    }
    return copy_bytes(out, cap, length, &text[first], len - first);
}

/* str() of a value, cut to cap - 1 bytes. A real number, a list or
 * an object is its JSON text (header). */
static uint32_t value_text(const uint8_t *payload,
                           const json_value_t *value, char *out,
                           uint32_t cap)
{
    uint32_t length = 0U;
    static const char *const words[] = {"True", "False", "None"};

    if (value->kind == JSON_STRING) {
        length = decode_string(payload, value, out, cap);
        length = (length < cap) ? length : (cap - 1U);
    } else if (value->kind == JSON_INT) {
        length = int_text(&payload[value->start],
                          value->end - value->start, out, cap);
    } else if ((value->kind == JSON_TRUE) ||
               (value->kind == JSON_FALSE) ||
               (value->kind == JSON_NULL)) {
        const char *word =
            words[(int32_t)value->kind - (int32_t)JSON_TRUE];

        length = copy_bytes(out, cap, 0U, (const uint8_t *)word,
                            (uint32_t)strlen(word));
    } else {
        length = copy_bytes(out, cap, 0U, &payload[value->start],
                            value->end - value->start);
    }
    return length;
}

/* text[:keep]: bytes on the PSoC Edge, characters on the
 * reTerminal. The reTerminal's str counts characters: the byte where
 * character `keep` starts (UTF-8), or the whole text. */
static uint32_t utf8_cut(const char *text, uint32_t length,
                         uint32_t keep)
{
    const uint8_t top_bits = 0xC0U;
    const uint8_t continuation = 0x80U;
    uint32_t characters = 0U;
    uint32_t cut = length;
    uint32_t i;

    for (i = 0U; (i < length) && (cut == length); i++) {
        const bool starts =
            ((uint8_t)text[i] & top_bits) != continuation;

        if (starts && (characters == keep)) {
            cut = i;
        }
        characters += starts ? 1U : 0U;
    }
    return cut;
}

static void cut_text(char *text, uint32_t length, uint32_t keep)
{
    uint32_t cut = (length < keep) ? length : keep;

    if (on_reterminal()) {
        cut = utf8_cut(text, length, keep);
    }
    text[cut] = '\0';
}

/* Any digit but 0 before the exponent: the number is not zero. */
static bool number_is_nonzero(const uint8_t *text, uint32_t len)
{
    bool nonzero = false;
    uint32_t i;

    for (i = 0U; (i < len) && !is_exponent(text[i]); i++) {
        nonzero =
            nonzero || (is_digit(text[i]) && (text[i] != (uint8_t)'0'));
    }
    return nonzero;
}

/* Python's truth of a value; a missing "on" is 1. */
static bool value_is_true(const uint8_t *payload,
                          const json_value_t *value)
{
    const uint32_t length = value->end - value->start;
    const uint32_t empty_string = 2U; /* the two quotes */
    bool truth = false;

    if ((value->kind == JSON_MISSING) || (value->kind == JSON_TRUE)) {
        truth = true;
    } else if (value->kind == JSON_STRING) {
        truth = length > empty_string;
    } else if ((value->kind == JSON_INT) ||
               (value->kind == JSON_FLOAT)) {
        truth = number_is_nonzero(&payload[value->start], length);
    } else if (is_container(value->kind)) {
        truth = value->has_members;
    } else {
        truth = false; /* false, null */
    }
    return truth;
}

/* "n": an int (True is 1) from 0 to led_count - 1. A missing "n"
 * is 0. false for anything else. */
static bool led_number(const uint8_t *payload,
                       const json_value_t *value, uint32_t led_count,
                       uint32_t *led)
{
    const uint32_t digits_max = 9U; /* more cannot be an LED */
    const uint32_t decimal_base = 10U;
    char digits[VALUE_BYTES];
    bool usable = true;

    *led = 0U;
    if (value->kind == JSON_TRUE) {
        *led = 1U;
    } else if (value->kind == JSON_INT) {
        const uint32_t length =
            int_text(&payload[value->start], value->end - value->start,
                     digits, sizeof(digits));
        uint32_t i;

        usable = (digits[0] != '-') && (length <= digits_max);
        for (i = 0U; usable && (i < length); i++) {
            *led = (*led * decimal_base) + (uint32_t)(digits[i] - '0');
        }
    } else {
        usable = (value->kind == JSON_MISSING) ||
                 (value->kind == JSON_FALSE);
    }
    return usable && (*led < led_count);
}

static bool text_is(const char *text, uint32_t length, const char *word)
{
    return (length == (uint32_t)strlen(word)) &&
           (memcmp(text, word, length) == 0);
}

static void line_add_bytes(line_t *line, const uint8_t *bytes,
                           uint32_t len)
{
    line->used =
        copy_bytes(line->text, LINE_BYTES, line->used, bytes, len);
}

static void line_add(line_t *line, const char *text)
{
    line_add_bytes(line, (const uint8_t *)text, (uint32_t)strlen(text));
}

static void line_add_count(line_t *line, int32_t count)
{
    char number[NUMBER_BYTES];

    (void)snprintf(number, sizeof(number), "%" PRId32, count);
    line_add(line, number);
}

/* One byte of a bytes repr: the quote and \ escaped, printable
 * ASCII as it is, \n \r \t, else \xNN (py/objstr.c). */
static void add_repr_byte(line_t *line, uint8_t byte, char quote)
{
    const uint8_t printable_min = 0x20U;
    const uint8_t printable_end = 0x7FU;
    char piece[NUMBER_BYTES];

    if (byte == (uint8_t)quote) {
        (void)snprintf(piece, sizeof(piece), "\\%c", quote);
    } else if (byte == (uint8_t)'\\') {
        (void)snprintf(piece, sizeof(piece), "\\\\");
    } else if ((byte >= printable_min) && (byte < printable_end)) {
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
    line_add(line, piece);
}

/* print() of the payload as bytes: b'...', or b"..." when it holds
 * a ' and no ". */
static void line_add_repr(line_t *line, const uint8_t *bytes,
                          uint32_t len)
{
    bool has_single = false;
    bool has_double = false;
    char quote[2] = {'\'', '\0'};
    uint32_t i;

    for (i = 0U; i < len; i++) {
        has_single = has_single || (bytes[i] == (uint8_t)'\'');
        has_double = has_double || (bytes[i] == (uint8_t)'"');
    }
    if (has_single && !has_double) {
        quote[0] = '"';
    }
    line_add(line, "b");
    line_add(line, quote);
    for (i = 0U; i < len; i++) {
        add_repr_byte(line, bytes[i], quote[0]);
    }
    line_add(line, quote);
}

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* "<span class=...>ใบที่ N -> what" and the end of the span. */
static void print_done(const listener_t *listener, const char *kind,
                       const char *what, const char *tail)
{
    line_t line;

    line.used = 0U;
    line_add(&line, "<span class=");
    line_add(&line, kind);
    line_add(&line, ">ใบที่ ");
    line_add_count(&line, listener->got);
    line_add(&line, " -> ");
    line_add(&line, what);
    line_add(&line, tail);
    (void)bento_lcd_print(line.text);
}

/* The next step runs only when nothing failed and nothing stopped. */
static bool going_on(bento_status_t status, const listener_t *listener)
{
    return (status == BENTO_OK) && !listener->stopped;
}

/* No need to edit: the same set as file 05. */
static const char BROKER[] = "broker.hivemq.com";

/* Prints the line wifi.connect() prints on success. */
static void print_wifi_address(void)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    uint32_t ip_v4 = 0U;
    uint32_t length = 0U;

    /* an address that cannot be read prints as 0.0.0.0 */
    if (bento_wifi_ip(BENTO_WIFI_IF_STA, &ip_v4) != BENTO_OK) {
        ip_v4 = 0U;
    }
    /* a BENTO_WIFI_IPV4_TEXT_MAX buffer holds any address */
    (void)bento_wifi_ipv4_format(ip_v4, address,
                                 (uint32_t)sizeof(address), &length);
    (void)printf("[WiFi] Connected! IP=%s\r\n", address);
}

/* Waits for the broker's answer to a connect that has started. */
static bento_status_t wait_for_broker(uint32_t broker)
{
    bento_status_t status = BENTO_E_IN_PROGRESS;

    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(CONNECT_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_mqtt_open_poll(broker);
        }
    }
    return status;
}

/* MicroPython's widget setters ignore a refusal (modui.c): the
 * colour first, then the text, as the original sets them. */
static void paint(bento_ui_t *ui, bento_ui_id_t widget, uint32_t color,
                  const char *text)
{
    (void)bento_ui_set_color(ui, widget, color);
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

/* ui.poll() here only keeps the queue empty; it answers no events
 * when it fails. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* Ends politely: the screen says how far it got, nothing waits. */
static void stop_here(const remote_screen_t *screen,
                      listener_t *listener, const char *screen_text,
                      const char *log_text)
{
    line_t line;

    paint(screen->ui, screen->link_line, COLOR_BAD, screen_text);
    paint(screen->ui, screen->note, COLOR_BAD, log_text);
    paint(screen->ui, screen->status_line, COLOR_BAD,
          "จบตรงนี้ ยังไม่ได้เข้าส่วนรับคำสั่ง");
    drain_events(screen->ui);
    line.used = 0U;
    line_add(&line, "<span class=error>");
    line_add(&line, log_text);
    line_add(&line, "</span>");
    (void)bento_lcd_print(line.text); /* lcd.* ignore a console
                                         that is not ready */
    (void)printf("หยุดที่: %s\n", log_text);
    listener->stopped = true;
}

/* mqtt.connect(): it raises only when the board's own Wi-Fi layer
 * is not ready (a client that cannot start here), and answers False
 * when the broker does not (the port blocked, a wrong name, the
 * broker down). */
static bento_status_t connect_broker(const remote_screen_t *screen,
                                     listener_t *listener)
{
    char why[TEXT_BYTES];
    bento_mqtt_config_t config;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = (uint16_t)BROKER_PORT;
    config.keepalive_s = (uint16_t)KEEPALIVE_S;
    config.client_id = listener->device_id;
    config.client_id_len = (uint32_t)strlen(listener->device_id);
    status = bento_mqtt_open_start(&config, &listener->broker);
    if (status != BENTO_OK) {
        stop_here(screen, listener, "ต่อ broker ไม่ได้",
                  "ชั้น WiFi ของบอร์ดไม่พร้อม ลองรันใหม่");
        status = BENTO_OK;
    } else {
        (void)printf("[MQTT] Connecting to %s:%d ...\r\n", BROKER,
                     BROKER_PORT);
        status = wait_for_broker(listener->broker);
        if (status == BENTO_OK) {
            (void)printf("[MQTT] Connected to %s:%d as '%s'\r\n",
                         BROKER, BROKER_PORT, listener->device_id);
        } else if (status != BENTO_E_CANCELLED) {
            (void)snprintf(why, sizeof(why),
                           "%s ไม่ตอบ: เน็ตกันพอร์ต 1883 "
                           "หรือชื่อผิด",
                           BROKER);
            stop_here(screen, listener, "broker ไม่ตอบ", why);
            status = BENTO_OK;
        } else {
            /* Stop: the status ends the program */
        }
    }
    return status;
}

/* mqtt.subscribe(): must follow connect(), and be done again after
 * a reconnect. A lost link raises "MQTT not connected" there, which
 * nothing catches: the status ends the program here. */
static bento_status_t subscribe_topic(const remote_screen_t *screen,
                                      listener_t *listener)
{
    char why[TEXT_BYTES];
    uint8_t granted_qos = 0U;
    bento_status_t status = bento_mqtt_subscribe(
        listener->broker, listener->topic,
        (uint32_t)strlen(listener->topic), 0U, &granted_qos);

    if (status == BENTO_OK) {
        (void)printf("[MQTT] Subscribed to '%s' (QoS=%d)\r\n",
                     listener->topic, (int)granted_qos);
    } else if (status == BENTO_E_FAILED) {
        (void)printf("[MQTT] subscribe rejected by broker\r\n");
    } else {
        /* a failure the binding prints with a cy code */
    }
    if ((status != BENTO_OK) && (status != BENTO_E_SESSION)) {
        (void)snprintf(why, sizeof(why), "broker ไม่ยอมให้ฟังหัวข้อ %s",
                       listener->topic);
        stop_here(screen, listener, "subscribe ไม่ผ่าน", why);
        status = BENTO_OK;
    }
    return status;
}

/* Everything is up: the screen and the console say so. */
static void show_ready(const remote_screen_t *screen,
                       const listener_t *listener)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "ต่อแล้ว %s", BROKER);
    paint(screen->ui, screen->link_line, COLOR_OK, text);
    paint(screen->ui, screen->topic_line, COLOR_TEXT, listener->topic);
    paint(screen->ui, screen->status_line, COLOR_OK, "พร้อมรับคำสั่งแล้ว");
    paint(screen->ui, screen->note, COLOR_DIM,
          "กดปุ่มบนหน้าเว็บของทีม แล้วดูจอนี้");
    drain_events(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ฟังหัวข้อ %s อยู่</span>",
                   listener->topic);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print("หน้าเว็บส่ง {\"cmd\":\"beep\"} หรือ"
                          " {\"cmd\":\"led\",\"n\":0,\"on\":1}");
}

/* get_message() is one slot a newer message overwrites: take every
 * message waiting and keep the newest. */
static bool take_newest(uint32_t broker, message_t *message)
{
    char topic[TOPIC_KEEP]; /* the program never reads the topic */
    bento_mqtt_msg_t info;
    bool found = false;
    bool more = true;

    while (more) {
        bento_mqtt_msg_init(&info);
        more =
            bento_mqtt_poll(broker, &info, topic, sizeof(topic),
                            message->payload, PAYLOAD_KEEP) == BENTO_OK;
        if (more) {
            message->payload_len = info.payload_len;
            found = true;
        }
    }
    return found;
}

/* mqtt.is_connected(): an unknown state counts as dropped. */
static bool link_is_up(uint32_t broker)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;

    if (bento_mqtt_state(broker, &state) != BENTO_OK) {
        state = BENTO_MQTT_STATE_CLOSED;
    }
    return state == BENTO_MQTT_STATE_CONNECTED;
}

/* The last command's line and picture. */
static void show_mark(const remote_screen_t *screen, uint32_t color,
                      const char *text, const char *icon)
{
    paint(screen->ui, screen->last_line, color, text);
    (void)bento_ui_image_icon(screen->ui, screen->icon, icon,
                              (uint32_t)strlen(icon));
    (void)bento_ui_set_color(screen->ui, screen->icon, color);
}

/* A message that is no use: counted, shown, and its raw bytes on
 * the console. Garbage must never stop the program. */
static void reject(const remote_screen_t *screen, listener_t *listener,
                   const message_t *message, bool is_json)
{
    line_t line;

    listener->bad++;
    show_mark(screen, COLOR_BAD,
              is_json ? "ไม่ใช่ JSON object" : "ไม่ใช่ JSON", "cross");
    line.used = 0U;
    if (is_json) {
        line_add(&line, "<span class=error>ได้ JSON ที่ไม่ใช่ object: ");
    } else {
        line_add(&line, "<span class=error>ได้ของที่ไม่ใช่ JSON: ");
    }
    line_add_repr(&line, message->payload, message->payload_len);
    line_add(&line, " </span>");
    (void)bento_lcd_print(line.text);
}

/* "beep": a MIDI note, not a frequency; ui.tone() ignores a
 * refusal, so the twin does too. */
static void do_beep(const remote_screen_t *screen,
                    const listener_t *listener)
{
    (void)bento_ui_sfx_tone(screen->ui, BEEP_NOTE,
                            (uint8_t)BENTO_UI_WAVE_SQUARE,
                            BEEP_VELOCITY, BEEP_MS);
    show_mark(screen, COLOR_OK, "beep", "star");
    print_done(listener, "ok", "beep", "</span>");
}

/* "led": an LED number the board lacks is checked here; never trust
 * the sender. */
static void do_led(const remote_screen_t *screen, listener_t *listener,
                   const message_t *message, const command_t *command)
{
    const json_value_t *number = &command->fields[FIELD_N];
    char number_text[VALUE_BYTES] = "0"; /* a missing "n" is 0 */
    char text[LABEL_BYTES];
    uint32_t led = 0U;

    if (number->kind != JSON_MISSING) {
        (void)value_text(message->payload, number, number_text,
                         sizeof(number_text));
    }
    if (!led_number(message->payload, number, listener->led_count,
                    &led)) {
        listener->bad++;
        (void)snprintf(text, sizeof(text), "led เลขดวงผิด %s",
                       number_text);
        show_mark(screen, COLOR_BAD, text, "cross");
        (void)snprintf(text, sizeof(text),
                       "<span class=error>ไม่มีดวงที่ %s </span>",
                       number_text);
        (void)bento_lcd_print(text);
    } else {
        const bool on =
            value_is_true(message->payload, &command->fields[FIELD_ON]);

        /* .on() and .off() raise nothing (modgpio.c) */
        if (on) {
            (void)bento_gpio_led_on(led);
        } else {
            (void)bento_gpio_led_off(led);
        }
        (void)snprintf(text, sizeof(text), "led %s%s", number_text,
                       on ? " ติด" : " ดับ");
        show_mark(screen, COLOR_OK, text, "check");
        (void)snprintf(text, sizeof(text), "led %s", number_text);
        print_done(listener, "ok", text, " </span>");
    }
}

/* "say": a label carries 126 bytes, so the text is cut first. */
static void do_say(const remote_screen_t *screen,
                   const listener_t *listener, const message_t *message,
                   const command_t *command)
{
    const json_value_t *said = &command->fields[FIELD_TEXT];
    char text[VALUE_BYTES] = "";
    char what[TEXT_BYTES];
    uint32_t length = 0U;

    if (said->kind != JSON_MISSING) {
        length = value_text(message->payload, said, text, sizeof(text));
    }
    cut_text(text, length, SAY_MAX);
    show_mark(screen, COLOR_ACCENT,
              (text[0] != '\0') ? text : "say ที่ไม่มีข้อความ", "flag");
    (void)snprintf(what, sizeof(what), "say %s", text);
    print_done(listener, "info", what, " </span>");
}

/* Any other "cmd" that is not "": counted, shown cut short. */
static void do_unknown(const remote_screen_t *screen,
                       listener_t *listener, char *action,
                       uint32_t length)
{
    line_t line;
    char text[TEXT_BYTES];

    listener->bad++;
    line.used = 0U;
    line_add(&line, "<span class=warn>ไม่รู้จักคำสั่ง ");
    line_add(&line, action);
    line_add(&line, " </span>");
    cut_text(action, length, CMD_SHOW_MAX);
    (void)snprintf(text, sizeof(text), "ไม่รู้จักคำสั่ง %s", action);
    show_mark(screen, COLOR_WARN, text, "cross");
    (void)bento_lcd_print(line.text);
}

/* cmd.get("cmd", ""): only a string can be a known command. */
static void run_action(const remote_screen_t *screen,
                       listener_t *listener, const message_t *message,
                       const command_t *command)
{
    const json_value_t *action = &command->fields[FIELD_CMD];
    char text[VALUE_BYTES] = "";
    uint32_t length = 0U;
    bool is_string = action->kind == JSON_STRING;

    if (action->kind != JSON_MISSING) {
        length =
            value_text(message->payload, action, text, sizeof(text));
    }
    if (is_string && text_is(text, length, "beep")) {
        do_beep(screen, listener);
    } else if (is_string && text_is(text, length, "led")) {
        do_led(screen, listener, message, command);
    } else if (is_string && text_is(text, length, "say")) {
        do_say(screen, listener, message, command);
    } else if ((action->kind != JSON_MISSING) &&
               !(is_string && (length == 0U))) {
        do_unknown(screen, listener, text, length);
    } else {
        /* no command at all: nothing to do */
    }
}

/* One message: counted, read as JSON, then acted on. */
static void handle_message(const remote_screen_t *screen,
                           listener_t *listener,
                           const message_t *message)
{
    char count[NUMBER_BYTES];
    command_t command;

    listener->got++;
    (void)snprintf(count, sizeof(count), "%" PRId32, listener->got);
    (void)bento_ui_set_text(screen->ui, screen->count_digits, count,
                            (uint32_t)strlen(count));
    read_command(message->payload, message->payload_len, &command);
    if (command.top == JSON_MISSING) {
        reject(screen, listener, message, false);
    } else if (command.top != JSON_OBJECT) {
        reject(screen, listener, message, true);
    } else {
        run_action(screen, listener, message, &command);
    }
}

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    INNER_X = 40,
    TITLE_Y = 8,
    STATUS_X = 344,
    STATUS_Y = 16,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 120,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CAPTION_Y = 70,
    LINK_Y = 104,
    COUNT_X = 520,
    COUNT_W = 144,
    COUNT_H = 56,
    LAST_CAPTION_Y = 190,
    LAST_Y = 224,
    ICON_X = 560,
    ICON_Y = 192,
    ICON_SIZE = 64,
    TOPIC_Y = 272,
    TOPIC_X = 224,
    NOTE_Y = 312,
    KNOWN_Y = 348,
    BIG_PX = 24,
    TEXT_PX = 20
};

/* Where a label goes, its colour and its font size. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} label_at_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const label_at_t *at,
                                bento_ui_id_t *out_id)
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

/* The title, the status line and the link card. */
static bento_status_t add_link_card(remote_screen_t *screen)
{
    const label_at_t title_at = {MARGIN_X, TITLE_Y, COLOR_TEXT, BIG_PX};
    const label_at_t status_at = {STATUS_X, STATUS_Y, COLOR_DIM,
                                  TEXT_PX};
    const label_at_t caption_at = {INNER_X, CAPTION_Y, COLOR_DIM,
                                   TEXT_PX};
    const label_at_t link_at = {INNER_X, LINK_Y, COLOR_WARN, BIG_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "รับคำสั่งจากที่ไกล", &title_at, &unused);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังจะเริ่ม", &status_at,
                           &screen->status_line);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = MARGIN_X;
        args.y = CARD_Y;
        args.w = CARD_W;
        args.h = CARD_H;
        args.color = COLOR_CARD;
        args.min_val = COLOR_CARD; /* the border: the card itself */
        args.max_val = CARD_RADIUS;
        args.font_px = CARD_BORDER;
        status = bento_ui_panel_create(screen->ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "สถานะสาย", &caption_at, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่ได้ต่อ", &link_at,
                           &screen->link_line);
    }
    return status;
}

/* The message counter, right in the link card. */
static bento_status_t add_counter(remote_screen_t *screen)
{
    const label_at_t caption_at = {COUNT_X, CAPTION_Y, COLOR_DIM,
                                   TEXT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "ได้รับแล้ว (ใบ)", &caption_at, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "0"; /* a Seg7 takes a text, not a number */
        args.text_len = (uint32_t)strlen(args.text);
        args.x = COUNT_X;
        args.y = LINK_Y;
        args.w = COUNT_W;
        args.h = COUNT_H;
        args.color = COLOR_ACCENT;
        status = bento_ui_seg7_create(screen->ui, &args,
                                      &screen->count_digits);
    }
    return status;
}

/* The last command, with its picture beside it. */
static bento_status_t add_last_command(remote_screen_t *screen)
{
    const label_at_t caption_at = {MARGIN_X, LAST_CAPTION_Y, COLOR_DIM,
                                   TEXT_PX};
    const label_at_t last_at = {MARGIN_X, LAST_Y, COLOR_DIM, BIG_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "คำสั่งล่าสุดที่ได้รับ", &caption_at, &unused);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่มีคำสั่งเข้ามา", &last_at,
                           &screen->last_line);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "smiley"; /* an Image takes an icon's name */
        args.text_len = (uint32_t)strlen(args.text);
        args.x = ICON_X;
        args.y = ICON_Y;
        args.w = ICON_SIZE;
        args.h = ICON_SIZE;
        args.color = COLOR_DIM;
        status =
            bento_ui_image_create(screen->ui, &args, &screen->icon);
    }
    return status;
}

/* The topic, the note and the known commands. */
static bento_status_t add_footer(remote_screen_t *screen)
{
    const label_at_t caption_at = {MARGIN_X, TOPIC_Y, COLOR_DIM,
                                   TEXT_PX};
    const label_at_t topic_at = {TOPIC_X, TOPIC_Y, COLOR_DIM, TEXT_PX};
    const label_at_t note_at = {MARGIN_X, NOTE_Y, COLOR_DIM, TEXT_PX};
    const label_at_t known_at = {MARGIN_X, KNOWN_Y, COLOR_DIM, TEXT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "หัวข้อที่ฟังอยู่", &caption_at, &unused);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่ได้ subscribe", &topic_at,
                           &screen->topic_line);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "กำลังเริ่ม", &note_at, &screen->note);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "คำสั่งที่รู้จัก beep / led / "
                           "say",
                           &known_at, &unused);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(remote_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_link_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_counter(screen);
    }
    if (status == BENTO_OK) {
        status = add_last_command(screen);
    }
    if (status == BENTO_OK) {
        status = add_footer(screen);
    }
    if (status == BENTO_OK) {
        drain_events(screen->ui);
    }
    return status;
}

/* wifi.ip(): the station's address, else the access point's, else
 * 0.0.0.0, which means DHCP gave none. */
static void check_address(const remote_screen_t *screen,
                          listener_t *listener)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    char text[TEXT_BYTES];
    uint32_t ip_v4 = 0U;
    uint32_t length = 0U;

    if (bento_wifi_ip(BENTO_WIFI_IF_ANY, &ip_v4) != BENTO_OK) {
        ip_v4 = 0U;
    }
    /* a BENTO_WIFI_IPV4_TEXT_MAX buffer holds any address */
    (void)bento_wifi_ipv4_format(ip_v4, address,
                                 (uint32_t)sizeof(address), &length);
    if (ip_v4 == 0U) {
        stop_here(screen, listener, "ไม่มีเลข IP",
                  "ลิงก์ขึ้นแล้วแต่ DHCP ไม่ให้เลข รับอะไรไม่ได้");
    } else {
        (void)snprintf(text, sizeof(text), "IP %s กำลังต่อ broker",
                       address);
        paint(screen->ui, screen->link_line, COLOR_WARN, text);
        drain_events(screen->ui);
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>ได้ IP %s </span>", address);
        (void)bento_lcd_print(text);
    }
}

/* Listening is over: every LED off, and the totals. */
static void finish(const remote_screen_t *screen,
                   const listener_t *listener)
{
    char text[TEXT_BYTES];
    uint32_t led;

    for (led = 0U; led < listener->led_count; led++) {
        (void)bento_gpio_led_off(led); /* .off() raises nothing */
    }
    (void)snprintf(text, sizeof(text),
                   "เลิกฟังแล้ว - ได้รับ %" PRId32 " ใบ ใช้ไม่ได้ %" PRId32,
                   listener->got, listener->bad);
    paint(screen->ui, screen->status_line, COLOR_DIM, text);
    drain_events(screen->ui);
    (void)bento_lcd_console(
        "<span class=muted>------------------------</span>");
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ได้รับ %" PRId32
                   " ใบ | ใช้ไม่ได้ %" PRId32 " ใบ</span>",
                   listener->got, listener->bad);
    (void)bento_lcd_print(text);
    (void)printf("ได้รับ %" PRId32 " ใบ | ใช้ไม่ได้ %" PRId32 " ใบ\n",
                 listener->got, listener->bad);
}

/* Edit these three as the teacher hands them out. */
static const char WIFI_SSID[] = "bento-teamXX";
static const char WIFI_PASS[] = "<รหัส Hotspot ของทีม>";
static const char TEAM[] = "teamXX"; /* team01 .. team19 */
static const char ROOT[] = "bento-aiot";

/* A scan's results: too big for the 8 KB program stack, so at file
 * scope, zeroed by the loader on every load and set again at the
 * start of main() (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern bento_wifi_ap_t g_scan_results[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_scan_results[BENTO_WIFI_SCAN_MAX];

/* Another team's name would make the broker drop that team's
 * board (team00 is the teacher's): "team" and two digits. */
static bool team_is_set(const char *team)
{
    const uint32_t length = (uint32_t)strlen(team);
    bool set = (length == (uint32_t)TEAM_LEN) &&
               (strncmp(team, "team", TEAM_PREFIX_LEN) == 0);
    uint32_t i;

    for (i = TEAM_PREFIX_LEN; set && (i < length); i++) {
        set = is_digit((uint8_t)team[i]);
    }
    return set;
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
        status = wait_ms(CONNECT_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_connect_poll();
        } else { /* Stop between two polls: leave the radio alone */
            (void)bento_wifi_connect_cancel(); /* nothing to report */
        }
    }
    *joined = (status == BENTO_OK);
    if ((status == BENTO_E_FAILED) || (status == BENTO_E_TIMEOUT)) {
        status = BENTO_OK;
    }
    if (*joined) {
        print_wifi_address();
    }
    return status;
}

/* wifi.scan(): *heard when the hotspot is among the networks. A
 * failed scan hears nothing (the original passes its OSError). */
static bento_status_t hear_hotspot(bool *heard)
{
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = bento_wifi_scan_start();

    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(CONNECT_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_scan_poll(g_scan_results,
                                          BENTO_WIFI_SCAN_MAX, &count);
        } else {
            (void)bento_wifi_scan_cancel(); /* nothing to report */
        }
    }
    *heard = false;
    for (i = 0U; (status == BENTO_OK) && (i < count); i++) {
        *heard =
            *heard || (strcmp(g_scan_results[i].ssid, WIFI_SSID) == 0);
    }
    if (status != BENTO_E_CANCELLED) {
        status = BENTO_OK;
    }
    return status;
}

/* Joins the hotspot, or says why not: heard but refused (the
 * password), or not heard at all (the name, or the place). */
static bento_status_t join_or_explain(const remote_screen_t *screen,
                                      listener_t *listener)
{
    char why[LINE_BYTES]; /* the longest SSID fits */
    bool joined = false;
    bool heard = false;
    bento_status_t status;

    paint(screen->ui, screen->status_line, COLOR_WARN,
          "กำลังต่อ WiFi จอจะนิ่งสักครู่");
    drain_events(screen->ui);
    (void)snprintf(why, sizeof(why), "กำลังต่อ WiFi %s", WIFI_SSID);
    (void)bento_lcd_print(why);
    status = join_wifi(&joined);
    if ((status == BENTO_OK) && !joined) {
        status = hear_hotspot(&heard);
    }
    if ((status == BENTO_OK) && !joined) {
        if (heard) {
            (void)snprintf(why, sizeof(why),
                           "ได้ยินวง %s แต่ต่อไม่ผ่าน "
                           "ตรวจรหัสผ่าน",
                           WIFI_SSID);
        } else {
            (void)snprintf(why, sizeof(why),
                           "ไม่ได้ยินวง %s เลย ตรวจชื่อวงหรือย้ายที่",
                           WIFI_SSID);
        }
        stop_here(screen, listener, "ต่อ WiFi ไม่ติด", why);
    }
    return status;
}

/* One round: the inbox, then the link, then the time left.
 * *dropped: the link went down and listening is over. */
static void listen_once(const remote_screen_t *screen,
                        listener_t *listener, uint32_t start_ms,
                        int32_t left_ms, bool *dropped)
{
    message_t message;
    char text[TEXT_BYTES];

    if (take_newest(listener->broker, &message)) {
        handle_message(screen, listener, &message);
    }
    *dropped = !link_is_up(listener->broker);
    if (*dropped) {
        paint(screen->ui, screen->link_line, COLOR_BAD, "สายหลุดแล้ว");
        (void)snprintf(
            text, sizeof(text), "หลุดตอนวินาทีที่ %" PRId32,
            bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) /
                MS_PER_S);
        paint(screen->ui, screen->note, COLOR_BAD, text);
        drain_events(screen->ui);
        (void)bento_lcd_print(
            "<span class=error>สายหลุดระหว่างฟัง</span>");
    } else {
        (void)snprintf(text, sizeof(text),
                       "ฟังอยู่ - เหลืออีก %" PRId32 " วินาที",
                       left_ms / MS_PER_S);
        (void)bento_ui_set_text(screen->ui, screen->status_line, text,
                                (uint32_t)strlen(text));
        drain_events(screen->ui);
    }
}

/* The rest of a POLL_MS round; a round that took its whole time has
 * none, but Stop is still seen. */
static bento_status_t rest_after(uint32_t round_ms)
{
    const int32_t rest_ms =
        POLL_MS -
        bento_time_ticks_diff(bento_time_ticks_ms(), round_ms);
    bento_status_t status = BENTO_OK;

    if (rest_ms > 0) {
        status = wait_ms((uint32_t)rest_ms);
    } else if (!bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    } else {
        /* the round took its whole time: no rest */
    }
    return status;
}

static bento_status_t listen(const remote_screen_t *screen,
                             listener_t *listener)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;
    bool done = false;

    while ((status == BENTO_OK) && !done) {
        const uint32_t round_ms = bento_time_ticks_ms();
        const int32_t left_ms =
            LISTEN_MS - bento_time_ticks_diff(round_ms, start_ms);

        done = left_ms <= 0;
        if (!done) {
            listen_once(screen, listener, start_ms, left_ms, &done);
        }
        if (!done) {
            status = rest_after(round_ms);
        }
    }
    return status;
}

/* From the team check to the totals; a step that cannot go on ends
 * the program politely (the original's raise Stop). */
static bento_status_t run_listener(const remote_screen_t *screen,
                                   listener_t *listener)
{
    bento_status_t status = BENTO_OK;

    if (!team_is_set(TEAM)) {
        stop_here(screen, listener, "ยังไม่ได้ตั้งชื่อทีม",
                  "แก้ TEAM เป็นเลขทีมของคุณก่อน เช่น team03");
    }
    if (going_on(status, listener)) {
        status = join_or_explain(screen, listener);
    }
    if (going_on(status, listener)) {
        check_address(screen, listener);
    }
    if (going_on(status, listener)) {
        status = connect_broker(screen, listener);
    }
    if (going_on(status, listener)) {
        status = subscribe_topic(screen, listener);
    }
    if (going_on(status, listener)) {
        show_ready(screen, listener);
        status = listen(screen, listener);
    }
    if (going_on(status, listener)) {
        finish(screen, listener);
    }
    return status;
}

int main(void)
{
    remote_screen_t screen = {0};
    listener_t listener = {0};
    bento_status_t status;

    (void)memset(g_scan_results, 0, sizeof(g_scan_results));
    screen.ui = bento_runtime_get_ui();
    listener.led_count = bento_gpio_num_leds();
    (void)snprintf(listener.device_id, sizeof(listener.device_id),
                   "%s-%s", ROOT, TEAM);
    (void)snprintf(listener.topic, sizeof(listener.topic), "%s/%s/cmd",
                   ROOT, TEAM);
    status = build_screen(&screen);
    if (status == BENTO_OK) { /* lcd.* ignore a console that is not
                                 ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>รับคำสั่งจากที่ไกล</h2>");
        status = run_listener(&screen, &listener);
    }
    if (listener.broker != 0U) {
        (void)bento_mqtt_close(listener.broker); /* header */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
