/*
 * 17_panel_to_broker.c -- แผงของเราบน broker
 *
 * แตะปุ่มบนจอแล้วบอร์ดส่ง event ขึ้น broker ทันที ลากแถบแล้วค่าไปกับ telemetry
 * ที่ส่งซ้ำทุก 2 วินาที เว็บส่งคำสั่งกลับมาได้ที่หัวข้อ .../cmd:
 * {"cmd":"set","v":80} ขยับแถบ {"cmd":"say","text":"hi"} ขึ้นข้อความ (กรองเฉพาะ
 * อักขระที่จอวาดได้) {"cmd":"beep"} ส่งเสียง ก่อนเริ่มต้องแก้ TEAM ให้เป็นทีม
 * ของเรา (teamXX จงใจให้หยุด) แล้วต่อ WiFi และ broker ทีละขั้น ขั้นไหนไม่ผ่าน
 * จอบอกและจบอย่างสุภาพ แผงเปิดอยู่ 2 นาที พอร์ต 1883 ไม่เข้ารหัส
 *
 * Tap the button and the board sends an event to the broker at once;
 * drag the slider and the value rides on the telemetry sent every
 * 2 s. A web page can command it on .../cmd: "set" moves the slider,
 * "say" shows a text (only characters the screen can draw), "beep"
 * sounds a note. First set TEAM to your own team (teamXX stops on
 * purpose); then Wi-Fi and the broker are joined step by step, and a
 * step that fails says so on screen and ends politely. The panel stays
 * open for two minutes. Port 1883 is not encrypted.
 *
 * Twin of 17_panel_to_broker.py
 * (embedded-systems-for-aiot-developer/examples/s04/17_panel_to_broker.py),
 * rewritten from the library twin aiot-s04-17-panel-to-broker. Style:
 * CLEAN_C.md. Where C must differ:
 *   - no json module: json.dumps is snprintf in MicroPython's key
 *     order for these dicts (id, v, ev, n and id, v, presses, n), and
 *     json.loads is a small strict JSON reader that keeps "cmd", "v"
 *     and "text". str() of a non-text "text" shows its JSON spelling
 *     (true, false and null as True, False and None); bytes that are
 *     not UTF-8 are read as they are, as the PSoC Edge's decode() does.
 *   - the say filter walks UTF-8 characters on the reTerminal and
 *     bytes on the PSoC Edge, as each board's MicroPython str does.
 *   - "raise Stop" is the flag panel.stopped; the status chain ends.
 *   - wifi.connect, mqtt.connect and mqtt.subscribe print console lines
 *     of their own; this program prints the ones C can know.
 *   - bento_mqtt keeps four messages where the binding keeps one; the
 *     newest one waiting is acted on, as the one slot would. A C
 *     program closes its broker client at the end.
 * In the BENTO Emulator, MQTT from C is not available yet: the program
 * says so and ends, through the original's "broker does not answer"
 * path.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_gpio.h"
#include "bento_mqtt.h"
#include "bento_runtime.h"
#include "bento_wifi.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
/* Change these to what the teacher hands out. A phone hotspot: the
 * faculty Wi-Fi needs a login the board cannot do. */
static const char WIFI_SSID[] = "bento-teamXX";
static const char WIFI_PASS[] = "<รหัส Hotspot ของทีม>"; /* 8 or more */
static const char BROKER[] =
    "broker.hivemq.com";             /* or test.mosquitto.org */
static const char TEAM[] = "teamXX"; /* team01 to team19 */
/* ---- end of settings ---- */

static const char ROOT[] = "bento-aiot";

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    RUN_MS = 120000, /* how long the panel stays open */
    TELE_MS = 2000,  /* telemetry again this often */
    POLL_MS = 100,   /* screen and inbox asked this often */
    WAIT_SLICE_MS = 100
};

/* The broker and the team name. */
enum {
    BROKER_PORT = 1883,
    KEEPALIVE_S = 60,
    TEAM_LENGTH = 6, /* "team" and two digits */
    TEAM_PREFIX = 4,
    VALUE_MIN = 0,
    VALUE_MAX = 100,
    VALUE_START = 50,
    SAY_LIMIT = 24, /* characters of a web text shown */
    /* the shown text: 24 letters of 3 bytes (Thai), and the NUL */
    SAY_BYTES = (SAY_LIMIT * 3) + 1,
    BEEP_NOTE = 69, /* a MIDI note, not hertz: A4 */
    BEEP_VELOCITY = 90,
    BEEP_MS = 150
};

/* Buffer sizes. */
enum {
    NAME_BYTES = 64,  /* a topic or the client id */
    TEXT_BYTES = 128, /* a SET_TEXT carries at most 126 bytes */
    JSON_BYTES = 128, /* one message out */
    PAYLOAD_BYTES =
        256, /* one message in, cut to 255 as the binding does */
    VALUE_BYTES = 100, /* one JSON value kept: 24 characters and more */
    EXPONENT_CAP =
        1000 /* past this an exponent cannot change the answer */
};

enum {
    AUTO = -1, /* no w= or h= given: the widget's own size */
    NOTE_X = 24,
    NOTE_Y = 342,
    NOTE_FONT_PX = 20,
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* The widgets, in the order they are made; the note line comes last. */
enum {
    W_TITLE,
    W_STATUS,
    W_TOP_CARD,
    W_BUTTON,
    W_SLIDER_CAPTION,
    W_SLIDER,
    W_SEG,
    W_MIDDLE_CARD,
    W_LINK,
    W_SENT,
    W_LAST,
    W_NOTE,
    W_COUNT
};

/* The kinds of JSON value the reader keeps. */
enum {
    VALUE_MISSING,
    VALUE_STRING,
    VALUE_NUMBER,
    VALUE_TRUE,
    VALUE_FALSE,
    VALUE_NULL,
    VALUE_CONTAINER
};

typedef struct {
    char device_id[NAME_BYTES]; /* 17 characters, under 31 */
    char topic_tele[NAME_BYTES];
    char topic_event[NAME_BYTES];
    char topic_cmd[NAME_BYTES];
} names_t;

/* The variable is the truth; the slider is only how it looks. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
    names_t names;
    bool stopped; /* stop_here() ended the run politely */
    bool has_sound;
    uint32_t client;
    bool client_open;
    bool alive;
    int32_t value;
    int32_t presses;
    int32_t tele_count;
    int32_t event_count;
    int32_t lost_count; /* publish() said no: that one is lost */
} panel_t;

typedef struct {
    const uint8_t *text;
    uint32_t length;
    uint32_t at;
    bool ok;
} json_reader_t;

typedef struct {
    uint8_t kind;
    char text[VALUE_BYTES]; /* a string's letters, or the spelling */
    uint32_t text_len;
    bento_real_t number;
} json_value_t;

typedef struct {
    json_value_t cmd;
    json_value_t v;
    json_value_t text;
} command_t;

static const uint32_t UTF8_LEAD_TWO = 0xC0U;
static const uint32_t UTF8_LEAD_THREE = 0xE0U;
static const uint32_t UTF8_FOLLOW_BITS = 0x3FU;
static const uint32_t UTF8_FOLLOW_SHIFT = 6U;
static const uint32_t UTF8_LEAD_THREE_SHIFT = 12U;
/* Up to BENTO_WIFI_SCAN_MAX networks: too big for the 8 KB program
 * stack, so file scope, cleared at the start of main() (C9). */
extern bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];

/* One widget as the original makes it: its factory, its text and its
 * keyword arguments. value= is a Label's or Button's font size and a
 * control's start value; min= and max= are a control's range (a
 * Panel's border colour and corner radius). */
typedef struct {
    uint8_t type;
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value;
    int32_t min;
    int32_t max;
} widget_spec_t;

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t widget,
                      const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_widget(bento_ui_t *ui,
                                 const widget_spec_t *spec,
                                 bento_ui_id_t *out_id)
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
    args.font_px = spec->value;
    args.min_val = spec->min;
    args.max_val = spec->max;
    return bento_ui_create(ui, spec->type, &args, out_id);
}

/* Makes the widgets in table order (creation order is drawing order)
 * and keeps each id; the first refusal ends it, as a raise does in
 * MicroPython. */
static bento_status_t add_widgets(bento_ui_t *ui,
                                  const widget_spec_t *table,
                                  uint32_t count, bento_ui_id_t *ids)
{
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = add_widget(ui, &table[i], &ids[i]);
    }
    return status;
}

/* ---- reading a command: a strict JSON reader ---------------------- */

static uint8_t peek(const json_reader_t *reader)
{
    return (reader->at < reader->length) ? reader->text[reader->at]
                                         : 0U;
}

/* The value of a digit letter. */
static int32_t digit_value(uint8_t letter)
{
    const uint8_t zero = (uint8_t)'0';

    return (int32_t)letter - (int32_t)zero;
}

static bool is_digit(uint8_t letter)
{
    return (letter >= (uint8_t)'0') && (letter <= (uint8_t)'9');
}

static void skip_space(json_reader_t *reader)
{
    uint8_t letter = peek(reader);

    while ((reader->at < reader->length) &&
           ((letter == (uint8_t)' ') || (letter == (uint8_t)'\t') ||
            (letter == (uint8_t)'\n') || (letter == (uint8_t)'\r'))) {
        reader->at++;
        letter = peek(reader);
    }
}

/* Takes the letter after any spaces, if it is there. */
static bool take(json_reader_t *reader, uint8_t letter)
{
    bool taken = false;

    skip_space(reader);
    if ((reader->at < reader->length) && (peek(reader) == letter)) {
        reader->at++;
        taken = true;
    }
    return taken;
}

static void value_clear(json_value_t *value, uint8_t kind)
{
    value->kind = kind;
    value->text[0] = '\0';
    value->text_len = 0U;
    value->number = (bento_real_t)0.0;
}

/* Keeps one byte; past the buffer the rest is read but not kept. */
static void value_put(json_value_t *value, uint8_t byte)
{
    if ((value->text_len + 1U) < (uint32_t)VALUE_BYTES) {
        value->text[value->text_len] = (char)byte;
        value->text_len++;
        value->text[value->text_len] = '\0';
    }
}

/* A \u escape, written as UTF-8. */
static void value_put_code(json_value_t *value, uint32_t code)
{
    static const uint32_t utf8_follow = 0x80U;
    static const uint32_t utf8_two_byte_end =
        0x800U; /* below: two bytes */
    /* UTF-8 and the character ranges the screen draws. */
    static const uint32_t utf8_one_byte_end =
        0x80U; /* below: one byte */
    if (code < utf8_one_byte_end) {
        value_put(value, (uint8_t)code);
    } else if (code < utf8_two_byte_end) {
        value_put(value, (uint8_t)(UTF8_LEAD_TWO |
                                   (code >> UTF8_FOLLOW_SHIFT)));
        value_put(value,
                  (uint8_t)(utf8_follow | (code & UTF8_FOLLOW_BITS)));
    } else {
        value_put(value, (uint8_t)(UTF8_LEAD_THREE |
                                   (code >> UTF8_LEAD_THREE_SHIFT)));
        value_put(value,
                  (uint8_t)(utf8_follow | ((code >> UTF8_FOLLOW_SHIFT) &
                                           UTF8_FOLLOW_BITS)));
        value_put(value,
                  (uint8_t)(utf8_follow | (code & UTF8_FOLLOW_BITS)));
    }
}

static uint32_t hex_digit(uint8_t letter, bool *ok)
{
    static const uint32_t hex_letter_value = 10U; /* 'a' and 'A' */
    uint32_t digit = 0U;

    if (is_digit(letter)) {
        digit = (uint32_t)letter - (uint32_t)'0';
    } else if ((letter >= (uint8_t)'a') && (letter <= (uint8_t)'f')) {
        digit = ((uint32_t)letter - (uint32_t)'a') + hex_letter_value;
    } else if ((letter >= (uint8_t)'A') && (letter <= (uint8_t)'F')) {
        digit = ((uint32_t)letter - (uint32_t)'A') + hex_letter_value;
    } else {
        *ok = false;
    }
    return digit;
}

static void read_unicode_escape(json_reader_t *reader,
                                json_value_t *value)
{
    const uint32_t hex_digits = 4U;
    const uint32_t hex_base = 16U;
    uint32_t code = 0U;
    uint32_t i;

    for (i = 0U; (i < hex_digits) && reader->ok; i++) {
        code = (code * hex_base) + hex_digit(peek(reader), &reader->ok);
        reader->at++;
    }
    if (reader->ok) {
        value_put_code(value, code);
    }
}

/* The letter after a backslash. */
static void read_escape(json_reader_t *reader, json_value_t *value)
{
    static const char escaped[] = "\"\\/bfnrt";
    static const char meant[] = "\"\\/\b\f\n\r\t";
    const uint8_t letter = peek(reader);
    bool found = false;
    uint32_t i;

    reader->at++;
    for (i = 0U; (escaped[i] != '\0') && !found; i++) {
        if ((uint8_t)escaped[i] == letter) {
            value_put(value, (uint8_t)meant[i]);
            found = true;
        }
    }
    if (!found && (letter == (uint8_t)'u')) {
        read_unicode_escape(reader, value);
    } else if (!found) {
        reader->ok = false;
    } else {
        /* one escaped letter kept */
    }
}

/* A string, its opening quote already seen. */
static void read_string(json_reader_t *reader, json_value_t *value)
{
    bool closed = false;

    value_clear(value, VALUE_STRING);
    reader->at++;
    while (reader->ok && !closed && (reader->at < reader->length)) {
        const uint8_t letter = peek(reader);

        if (letter == (uint8_t)'"') {
            reader->at++;
            closed = true;
        } else if (letter == (uint8_t)'\\') {
            reader->at++;
            read_escape(reader, value);
        } else {
            value_put(value, letter);
            reader->at++;
        }
    }
    reader->ok = reader->ok && closed;
}

/* Digits into a running number; answers how many there were. */
static uint32_t read_digits(json_reader_t *reader, bento_real_t *number)
{
    const bento_real_t ten = (bento_real_t)10.0;
    uint32_t count = 0U;

    while (is_digit(peek(reader))) {
        const bento_real_t digit =
            (bento_real_t)digit_value(peek(reader));

        *number = *number * ten;
        *number = *number + digit;
        reader->at++;
        count++;
    }
    return count;
}

/* The exponent's digits; past EXPONENT_CAP they change nothing. */
static int32_t read_exponent_digits(json_reader_t *reader)
{
    const int32_t ten = 10;
    int32_t exponent = 0;

    reader->ok = is_digit(peek(reader));
    while (is_digit(peek(reader))) {
        if (exponent < EXPONENT_CAP) {
            exponent = (exponent * ten) + digit_value(peek(reader));
        }
        reader->at++;
    }
    return exponent;
}

/* "e" or "E", a sign and digits; the power of ten they ask for. */
static int32_t read_exponent(json_reader_t *reader)
{
    int32_t exponent = 0;

    if ((peek(reader) == (uint8_t)'e') ||
        (peek(reader) == (uint8_t)'E')) {
        bool negative;

        reader->at++;
        negative = peek(reader) == (uint8_t)'-';
        if (negative || (peek(reader) == (uint8_t)'+')) {
            reader->at++;
        }
        exponent = read_exponent_digits(reader);
        if (negative) {
            exponent = -exponent;
        }
    }
    return exponent;
}

/* Dividing for a negative power keeps 100.0 exactly 100. Past the
 * largest real it becomes inf, which int() refuses. */
static bento_real_t scale_by_ten(bento_real_t number, int32_t exponent)
{
    const bento_real_t ten = (bento_real_t)10.0;
    bento_real_t scaled = number;
    int32_t i;

    for (i = 0; i < exponent; i++) {
        scaled = scaled * ten;
    }
    for (i = 0; i > exponent; i--) {
        scaled = scaled / ten;
    }
    return scaled;
}

static void read_number(json_reader_t *reader, json_value_t *value)
{
    const uint32_t start = reader->at;
    bento_real_t number = (bento_real_t)0.0;
    int32_t exponent = 0;
    uint32_t i;
    const bool negative = (peek(reader) == (uint8_t)'-');

    value_clear(value, VALUE_NUMBER);
    if (negative) {
        reader->at++;
    }
    reader->ok = read_digits(reader, &number) > 0U;
    if (reader->ok && (peek(reader) == (uint8_t)'.')) {
        reader->at++;
        exponent = -(int32_t)read_digits(reader, &number);
        reader->ok = exponent < 0;
    }
    if (reader->ok) {
        exponent += read_exponent(reader);
        number = scale_by_ten(number, exponent);
        value->number = negative ? -number : number;
    }
    for (i = start; i < reader->at; i++) {
        value_put(value, reader->text[i]);
    }
}

/* true, false or null; kept with the spelling Python's str() gives. */
static void read_word(json_reader_t *reader, json_value_t *value,
                      const char *word, uint8_t kind, const char *shown)
{
    const uint32_t length = (uint32_t)strlen(word);
    uint32_t i;

    value_clear(value, kind);
    reader->ok = (reader->at + length) <= reader->length;
    for (i = 0U; reader->ok && (i < length); i++) {
        reader->ok = reader->text[reader->at + i] == (uint8_t)word[i];
    }
    reader->at += length;
    for (i = 0U; shown[i] != '\0'; i++) {
        value_put(value, (uint8_t)shown[i]);
    }
}

/* An array or object kept only as its spelling: brackets are counted,
 * strings are stepped over whole. */
static void skip_container(json_reader_t *reader, json_value_t *value)
{
    uint32_t depth = 0U;
    bool in_string = false;
    bool escaped = false;

    value_clear(value, VALUE_CONTAINER);
    do {
        const uint8_t letter = peek(reader);

        value_put(value, letter);
        if (in_string) {
            in_string = escaped || (letter != (uint8_t)'"');
            escaped = !escaped && (letter == (uint8_t)'\\');
        } else if ((letter == (uint8_t)'[') ||
                   (letter == (uint8_t)'{')) {
            depth++;
        } else if ((letter == (uint8_t)']') ||
                   (letter == (uint8_t)'}')) {
            depth--;
        } else {
            in_string = letter == (uint8_t)'"';
        }
        reader->at++;
    } while ((depth > 0U) && (reader->at < reader->length));
    reader->ok = depth == 0U;
}

static void read_value(json_reader_t *reader, json_value_t *value)
{
    uint8_t letter;

    skip_space(reader);
    letter = peek(reader);
    if (letter == (uint8_t)'"') {
        read_string(reader, value);
    } else if ((letter == (uint8_t)'-') || is_digit(letter)) {
        read_number(reader, value);
    } else if (letter == (uint8_t)'t') {
        read_word(reader, value, "true", VALUE_TRUE, "True");
    } else if (letter == (uint8_t)'f') {
        read_word(reader, value, "false", VALUE_FALSE, "False");
    } else if (letter == (uint8_t)'n') {
        read_word(reader, value, "null", VALUE_NULL, "None");
    } else if ((letter == (uint8_t)'[') || (letter == (uint8_t)'{')) {
        skip_container(reader, value);
    } else {
        reader->ok = false;
    }
}

/* The place a key's value goes: one of the three kept, or scratch. */
static json_value_t *slot_for(command_t *command,
                              const json_value_t *key,
                              json_value_t *scratch)
{
    json_value_t *slot = scratch;

    if (strcmp(key->text, "cmd") == 0) {
        slot = &command->cmd;
    } else if (strcmp(key->text, "v") == 0) {
        slot = &command->v;
    } else if (strcmp(key->text, "text") == 0) {
        slot = &command->text;
    } else {
        /* a key this program does not use */
    }
    return slot;
}

/* "key": value pairs until "}"; a later key wins, as in a dict. */
static void read_members(json_reader_t *reader, command_t *command)
{
    json_value_t key;
    json_value_t scratch;
    bool more = !take(reader, (uint8_t)'}');

    while (reader->ok && more) {
        skip_space(reader);
        reader->ok = peek(reader) == (uint8_t)'"';
        if (reader->ok) {
            read_string(reader, &key);
        }
        reader->ok = reader->ok && take(reader, (uint8_t)':');
        if (reader->ok) {
            read_value(reader, slot_for(command, &key, &scratch));
        }
        more = reader->ok && take(reader, (uint8_t)',');
        if (reader->ok && !more) {
            reader->ok = take(reader, (uint8_t)'}');
        }
    }
}

/* json.loads(payload): false when it is not JSON, or not an object
 * (the original's cmd = {} and "not a dict" both act on nothing). */
static bool read_command(const uint8_t *payload, uint32_t length,
                         command_t *command)
{
    json_reader_t reader = {payload, length, 0U, true};

    value_clear(&command->cmd, VALUE_MISSING);
    value_clear(&command->v, VALUE_MISSING);
    value_clear(&command->text, VALUE_MISSING);
    reader.ok = take(&reader, (uint8_t)'{');
    if (reader.ok) {
        read_members(&reader, command);
    }
    skip_space(&reader);
    return reader.ok && (reader.at == reader.length);
}

/* ---- text the screen can draw -------------------------------------- */

/* On the PSoC Edge MicroPython's str is bytes; on the reTerminal it is
 * characters. */
static bool text_is_unicode(void)
{
    bento_gpio_board_info_t info;
#if defined(BENTO_TARGET_RETERMINAL)
    bool unicode = true;
#else
    bool unicode = false;
#endif

    bento_gpio_board_info_init(&info);
    if ((bento_gpio_board_info(&info) == BENTO_OK) &&
        (info.name != NULL)) {
        unicode = strcmp(info.name, "Seeed reTerminal") == 0;
    }
    return unicode;
}

/* The width and code of the character at text[at]. */
static uint32_t character_at(const char *text, uint32_t at,
                             uint32_t length, uint32_t *code)
{
    static const uint32_t utf8_width_four = 4U;
    static const uint32_t utf8_width_three = 3U;
    static const uint32_t utf8_lead_three_bits = 0x0FU;
    static const uint32_t utf8_lead_four = 0xF0U;
    const uint32_t lead = (uint32_t)(uint8_t)text[at];
    uint32_t width = 1U;

    *code = lead;
    if (lead >= utf8_lead_four) {
        width = utf8_width_four;
    } else if (lead >= UTF8_LEAD_THREE) {
        width = utf8_width_three;
    } else if (lead >= UTF8_LEAD_TWO) {
        width = 2U;
    } else {
        /* one byte */
    }
    if ((at + width) > length) {
        width = length - at;
    }
    if (width == utf8_width_three) {
        *code =
            ((lead & utf8_lead_three_bits) << UTF8_LEAD_THREE_SHIFT) |
            (((uint32_t)(uint8_t)text[at + 1U] & UTF8_FOLLOW_BITS)
             << UTF8_FOLLOW_SHIFT);
    } else if (width != 1U) {
        *code = 0U; /* not a code the screen draws */
    } else {
        /* the byte is the code */
    }
    return width;
}

/* Keeps only what the screen draws (ASCII and Thai), at most limit
 * characters. A text from the web is someone else's: one emoji turns
 * the whole line into empty boxes. */
static void screen_safe(const char *text, uint32_t length, char *out,
                        uint32_t out_bytes)
{
    static const uint32_t thai_block_mask = 0xFF80U;
    static const uint32_t thai_block = 0x0E00U; /* U+0E00 to U+0E7F */
    static const uint32_t ascii_last = 0x7EU;
    static const uint32_t ascii_first = 0x20U;
    /* the most one character writes: a Thai letter's 3 bytes */
    const uint32_t widest = 3U;
    const bool unicode = text_is_unicode();
    uint32_t at = 0U;
    uint32_t used = 0U;
    uint32_t count = 0U;

    while ((at < length) && (count < (uint32_t)SAY_LIMIT) &&
           ((used + widest) < out_bytes)) {
        uint32_t code = (uint32_t)(uint8_t)text[at];
        const uint32_t width =
            unicode ? character_at(text, at, length, &code) : 1U;

        if (((code >= ascii_first) && (code <= ascii_last)) ||
            ((code & thai_block_mask) == thai_block)) {
            (void)memcpy(&out[used], &text[at], (size_t)width);
            used += width;
        } else {
            out[used] = '?';
            used++;
        }
        at += width;
        count++;
    }
    out[used] = '\0';
}

/* ---- the screen ----------------------------------------------------- */

/* A label's colour and text together. */
static void show(const panel_t *panel, uint32_t widget, uint32_t color,
                 const char *text)
{
    (void)bento_ui_set_color(panel->ui, panel->ids[widget], color);
    show_text(panel->ui, panel->ids[widget], text);
}

/* ui.poll() here only lets the screen draw; the events are not used. */
static void let_screen_draw(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The topics and the client id, built from TEAM as the original adds
 * its strings. */
static void build_names(names_t *names)
{
    (void)snprintf(names->device_id, sizeof(names->device_id), "%s-%s",
                   ROOT, TEAM);
    (void)snprintf(names->topic_tele, sizeof(names->topic_tele),
                   "%s/%s/telemetry", ROOT, TEAM);
    (void)snprintf(names->topic_event, sizeof(names->topic_event),
                   "%s/%s/event", ROOT, TEAM);
    (void)snprintf(names->topic_cmd, sizeof(names->topic_cmd),
                   "%s/%s/cmd", ROOT, TEAM);
}

/* The note line ends before x = 690: the corner is the Console's. */
static bento_status_t add_note(panel_t *panel)
{
    char text[TEXT_BYTES];
    bento_ui_widget_args_t args;

    (void)snprintf(text, sizeof(text), "หัวข้อ %s/%s/...", ROOT, TEAM);
    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = NOTE_X;
    args.y = NOTE_Y;
    args.color = COLOR_DIM;
    args.font_px = NOTE_FONT_PX;
    return bento_ui_label_create(panel->ui, &args, &panel->ids[W_NOTE]);
}

/* The screen, in creation order. Touch targets are 88 px tall. */
static bento_status_t add_screen_widgets(bento_ui_t *ui, uint32_t first,
                                         uint32_t count,
                                         bento_ui_id_t *ids)
{
    static const widget_spec_t screen_widgets[W_NOTE] = {
        {UI_WIDGET_LABEL, "แผงของเราบน broker", 24, 8, AUTO, AUTO,
         COLOR_TEXT, 24, 0, 0},
        {UI_WIDGET_LABEL, "กำลังจะเริ่ม", 384, 16, AUTO, AUTO, COLOR_DIM,
         20, 0, 0},
        {UI_WIDGET_PANEL, NULL, 24, 56, 744, 128, COLOR_CARD, 1,
         COLOR_CARD, 12},
        {UI_WIDGET_BUTTON, "ส่ง event", 40, 72, 200, 88, COLOR_ACCENT,
         24, 0, 0},
        {UI_WIDGET_LABEL, "ค่าที่ตั้ง 0-100", 272, 70, AUTO, AUTO, COLOR_DIM,
         20, 0, 0},
        {UI_WIDGET_SLIDER, NULL, 272, 120, 320, 24, COLOR_ACCENT,
         VALUE_START, VALUE_MIN, VALUE_MAX},
        {UI_WIDGET_SEG7, "50", 624, 88, 128, 56, COLOR_ACCENT, 0, 0, 0},
        {UI_WIDGET_PANEL, NULL, 24, 200, 744, 128, COLOR_CARD, 1,
         COLOR_CARD, 12},
        {UI_WIDGET_LABEL, "สาย: ยังไม่ได้ต่อ", 40, 208, AUTO, AUTO,
         COLOR_WARN, 20, 0, 0},
        {UI_WIDGET_LABEL, "ส่งแล้ว telemetry 0  event 0  หาย 0", 40, 248,
         AUTO, AUTO, COLOR_TEXT, 20, 0, 0},
        {UI_WIDGET_LABEL, "ยังไม่มีคำสั่งจากเว็บ", 40, 288, AUTO, AUTO,
         COLOR_DIM, 20, 0, 0}};

    return add_widgets(ui, &screen_widgets[first], count, &ids[first]);
}

static bento_status_t build_screen(panel_t *panel)
{
    bento_status_t status =
        bento_ui_screen(panel->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_screen_widgets(panel->ui, 0U, W_NOTE, panel->ids);
    }
    if (status == BENTO_OK) {
        status = add_note(panel);
    }
    if (status == BENTO_OK) {
        let_screen_draw(panel->ui);
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>แผงของเราบน broker</h2>");
    }
    return status;
}

/* Ends the work politely: the screen says how far it got, then the
 * program ends instead of waiting. */
static void stop_here(panel_t *panel, const char *screen_text,
                      const char *log_text)
{
    char text[TEXT_BYTES];

    show(panel, W_LINK, COLOR_BAD, screen_text);
    show(panel, W_NOTE, COLOR_BAD, log_text);
    show(panel, W_STATUS, COLOR_BAD, "จบตรงนี้");
    let_screen_draw(panel->ui);
    (void)snprintf(text, sizeof(text), "<span class=error>%s</span>",
                   log_text);
    (void)bento_lcd_print(text);
    (void)printf("หยุดที่: %s\n", log_text);
    panel->stopped = true;
}

static bool go_on(const panel_t *panel, bento_status_t status)
{
    return (status == BENTO_OK) && !panel->stopped;
}

/* ---- step 0: the team name must really be ours ----------------------
 * teamXX fails on purpose: a client id another team uses makes the
 * broker throw that team out. team00 is the teacher's. */
static void check_team(panel_t *panel)
{
    bool valid = (strlen(TEAM) == (size_t)TEAM_LENGTH) &&
                 (strncmp(TEAM, "team", (size_t)TEAM_PREFIX) == 0) &&
                 (strcmp(TEAM, "team00") != 0);
    size_t i;

    for (i = (size_t)TEAM_PREFIX; valid && (TEAM[i] != '\0'); i++) {
        valid = is_digit((uint8_t)TEAM[i]);
    }
    if (!valid) {
        stop_here(panel, "ยังไม่ได้ตั้งชื่อทีม",
                  "แก้ TEAM เป็นเลขทีมของคุณก่อน เช่น team03");
    }
}

/* ---- step 1: Wi-Fi must give an address first ----------------------- */

/* wifi.ip(): "0.0.0.0" when the interface has no address. */
static void read_ip(char *text, uint32_t text_bytes)
{
    uint32_t address = 0U;
    uint32_t length = 0U;

    if ((bento_wifi_ip(BENTO_WIFI_IF_ANY, &address) != BENTO_OK) ||
        (bento_wifi_ipv4_format(address, text, text_bytes, &length) !=
         BENTO_OK)) {
        (void)snprintf(text, (size_t)text_bytes, "0.0.0.0");
    }
}

/* Waits for the join in slices, so Stop ends it as in MicroPython.
 * *joined is wifi.connect()'s True or False. */
static bento_status_t join_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_status_t join = BENTO_E_IN_PROGRESS;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = WIFI_PASS;
    params.pass_len = (uint32_t)strlen(WIFI_PASS);
    params.flags = BENTO_WIFI_F_SAVE_CREDS; /* the binding saves too */
    (void)printf("[WiFi] Connecting...\r\n");
    status = bento_wifi_connect_start(&params);
    while ((status == BENTO_OK) && (join == BENTO_E_IN_PROGRESS)) {
        join = bento_wifi_connect_poll();
        if (join == BENTO_E_IN_PROGRESS) {
            status = pause_ms(WAIT_SLICE_MS);
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* Stop wins either way */
    }
    *joined = (status == BENTO_OK) && (join == BENTO_OK);
    if (*joined) {
        read_ip(ip, sizeof(ip));
        (void)printf("[WiFi] Connected! IP=%s\r\n", ip);
    }
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* Did a scan hear our network? A scan that fails says no: the
 * original catches its OSError, no failure on top of a failure. */
static bento_status_t heard_network(bool *heard)
{
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t scan = bento_wifi_scan_start();
    bento_status_t status = BENTO_OK;

    if (scan == BENTO_OK) {
        scan = BENTO_E_IN_PROGRESS;
    }
    while ((status == BENTO_OK) && (scan == BENTO_E_IN_PROGRESS)) {
        scan = bento_wifi_scan_poll(g_networks, BENTO_WIFI_SCAN_MAX,
                                    &count);
        if (scan == BENTO_E_IN_PROGRESS) {
            status = pause_ms(WAIT_SLICE_MS);
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_scan_cancel(); /* Stop wins either way */
    }
    *heard = false;
    for (i = 0U; (scan == BENTO_OK) && (i < count); i++) {
        *heard = *heard || (strcmp(g_networks[i].ssid, WIFI_SSID) == 0);
    }
    return status;
}

/* The join failed: say whether the network was heard at all. */
static bento_status_t explain_no_wifi(panel_t *panel)
{
    char text[TEXT_BYTES];
    bool heard = false;
    const bento_status_t status = heard_network(&heard);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       heard ? "ได้ยินวง %s แต่ต่อไม่ผ่าน ตรวจรหัส"
                             : "ไม่ได้ยินวง %s เลย",
                       WIFI_SSID);
        stop_here(panel, "ต่อ WiFi ไม่ติด", text);
    }
    return status;
}

static bento_status_t step_wifi(panel_t *panel, char *ip,
                                uint32_t ip_bytes)
{
    bool joined = false;
    bento_status_t status;

    show(panel, W_STATUS, COLOR_WARN, "กำลังต่อ WiFi จอจะนิ่ง");
    let_screen_draw(panel->ui);
    status = join_wifi(&joined);
    if ((status == BENTO_OK) && !joined) {
        status = explain_no_wifi(panel);
    }
    if (go_on(panel, status)) {
        read_ip(ip, ip_bytes);
        if (strcmp(ip, "0.0.0.0") == 0) {
            stop_here(panel, "ไม่มีเลข IP",
                      "ลิงก์ขึ้นแต่ DHCP ไม่ให้เลข ส่งอะไรออกไม่ได้");
        }
    }
    return status;
}

/* ---- step 2: the broker, then our team's command topic -------------- */

/* Opens the client and waits for the broker's answer in slices.
 * *answer is the open's result once it is no longer in progress. */
static bento_status_t open_broker(panel_t *panel,
                                  bento_status_t *answer)
{
    bento_mqtt_config_t config;
    bento_status_t status = BENTO_OK;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = BROKER_PORT;
    config.keepalive_s = KEEPALIVE_S;
    config.client_id = panel->names.device_id;
    config.client_id_len = (uint32_t)strlen(panel->names.device_id);
    *answer = bento_mqtt_open_start(&config, &panel->client);
    panel->client_open = *answer == BENTO_OK;
    if ((*answer != BENTO_E_NOT_READY) &&
        (*answer != BENTO_E_UNSUPPORTED)) {
        (void)printf("[MQTT] Connecting to %s:%d ...\r\n", BROKER,
                     BROKER_PORT);
    }
    if (*answer == BENTO_OK) {
        *answer = BENTO_E_IN_PROGRESS;
    }
    while ((status == BENTO_OK) && (*answer == BENTO_E_IN_PROGRESS)) {
        *answer = bento_mqtt_open_poll(panel->client);
        if (*answer == BENTO_E_IN_PROGRESS) {
            status = pause_ms(WAIT_SLICE_MS);
        }
    }
    if ((status == BENTO_OK) && (*answer == BENTO_OK)) {
        (void)printf("[MQTT] Connected to %s:%d as '%s'\r\n", BROKER,
                     BROKER_PORT, panel->names.device_id);
    }
    return status;
}

/* mqtt.subscribe(topic): its console line, and True or False. */
static bool subscribe_commands(const panel_t *panel)
{
    const char *topic = panel->names.topic_cmd;
    uint8_t granted = 0U;
    const bento_status_t answer = bento_mqtt_subscribe(
        panel->client, topic, (uint32_t)strlen(topic), 0U, &granted);

    if (answer == BENTO_OK) {
        (void)printf("[MQTT] Subscribed to '%s' (QoS=%u)\r\n", topic,
                     (unsigned int)granted);
    }
    return answer == BENTO_OK;
}

/* mqtt.connect() raises OSError only when Wi-Fi cannot start; any
 * other failure is its False. */
static void explain_no_broker(panel_t *panel, bento_status_t answer)
{
    char text[TEXT_BYTES];

    if ((answer == BENTO_E_NOT_READY) ||
        (answer == BENTO_E_UNSUPPORTED)) {
        (void)snprintf(text, sizeof(text),
                       "%s ไม่ตอบ เครือข่ายอาจปิดพอร์ต 1883", BROKER);
        stop_here(panel, "broker ไม่ตอบ", text);
    } else {
        stop_here(panel, "broker ปฏิเสธ", "ลองสำรอง test.mosquitto.org");
    }
}

/* The BENTO Emulator's C runtime has no MQTT yet: its connect answers
 * BENTO_E_UNSUPPORTED. The screen and the console say so; then the
 * program takes the original's own "broker does not answer" path. */
static void show_no_mqtt_here(const panel_t *panel)
{
    static const char sentence[] =
        "MQTT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";

    show(panel, W_LAST, COLOR_WARN, sentence);
    let_screen_draw(panel->ui);
    (void)printf(
        "%s / MQTT from C is not in the BENTO Emulator yet: run it "
        "on a Dev Kit or Eva Kit.\n",
        sentence);
}

static void show_linked(const panel_t *panel)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "สาย: ต่อแล้ว %s", BROKER);
    show(panel, W_LINK, COLOR_OK, text);
    show(panel, W_STATUS, COLOR_DIM, "แตะปุ่ม ลากแถบ หรือสั่งจากเว็บ");
    let_screen_draw(panel->ui);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ฟังคำสั่งที่ %s </span>",
                   panel->names.topic_cmd);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print("ลองส่ง {\"cmd\":\"set\",\"v\":80} หรือ "
                          "{\"cmd\":\"say\",\"text\":\"hi\"}");
}

static bento_status_t step_broker(panel_t *panel, const char *ip)
{
    char text[TEXT_BYTES];
    bento_status_t answer = BENTO_OK;
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "สาย: IP %s กำลังต่อ %s", ip,
                   BROKER);
    show_text(panel->ui, panel->ids[W_LINK], text);
    let_screen_draw(panel->ui);
    status = open_broker(panel, &answer);
    if ((status == BENTO_OK) && (answer == BENTO_E_UNSUPPORTED)) {
        show_no_mqtt_here(panel);
    }
    if ((status == BENTO_OK) && (answer != BENTO_OK)) {
        explain_no_broker(panel, answer);
    }
    if (go_on(panel, status) && !subscribe_commands(panel)) {
        (void)snprintf(text, sizeof(text), "broker ไม่ยอมให้ฟัง %s",
                       panel->names.topic_cmd);
        stop_here(panel, "subscribe ไม่ผ่าน", text);
    }
    if (go_on(panel, status)) {
        show_linked(panel);
    }
    return status;
}

/* ---- step 3: one loop, three jobs ------------------------------------ */

/* One message out; false only when the link is gone. publish() fails
 * two ways: a refusal loses that one message but keeps the link, so it
 * is counted and the work goes on; a lost link ends the work. */
static bool send(panel_t *panel, const char *topic, const char *json)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;
    const bool linked =
        (bento_mqtt_state(panel->client, &state) == BENTO_OK) &&
        (state == BENTO_MQTT_STATE_CONNECTED);

    if (linked && (bento_mqtt_publish(
                       panel->client, topic, (uint32_t)strlen(topic),
                       (const uint8_t *)json, (uint32_t)strlen(json),
                       0U, 0U) != BENTO_OK)) {
        panel->lost_count++;
    }
    return linked;
}

static void send_press(panel_t *panel)
{
    char json[JSON_BYTES];

    panel->presses++;
    panel->event_count++;
    (void)snprintf(json, sizeof(json),
                   "{\"id\": \"%s\", \"v\": %" PRId32
                   ", \"ev\": \"press\", \"n\": %" PRId32 "}",
                   TEAM, panel->value, panel->event_count);
    if (!send(panel, panel->names.topic_event, json)) {
        panel->alive = false;
    }
}

static void send_telemetry(panel_t *panel)
{
    char json[JSON_BYTES];

    panel->tele_count++;
    (void)snprintf(json, sizeof(json),
                   "{\"id\": \"%s\", \"v\": %" PRId32
                   ", \"presses\": %" PRId32 ", \"n\": %" PRId32 "}",
                   TEAM, panel->value, panel->presses,
                   panel->tele_count);
    if (!send(panel, panel->names.topic_tele, json)) {
        panel->alive = false;
    }
}

/* The one gate that changes the value: the finger on the screen and
 * the web's commands both pass here, clamped to 0-100. */
static void set_value(panel_t *panel, int32_t value)
{
    char text[TEXT_BYTES];

    panel->value = value;
    if (panel->value < VALUE_MIN) {
        panel->value = VALUE_MIN;
    }
    if (panel->value > VALUE_MAX) {
        panel->value = VALUE_MAX;
    }
    (void)bento_ui_set_value(panel->ui, panel->ids[W_SLIDER],
                             panel->value);
    (void)snprintf(text, sizeof(text), "%" PRId32, panel->value);
    show_text(panel->ui, panel->ids[W_SEG], text);
}

/* int(v) of a command's v: a bool is an int in Python, a real is cut
 * toward zero, and inf is refused (OverflowError). false: unusable. */
static bool setting_from(const panel_t *panel, const json_value_t *v,
                         int32_t *setting)
{
    const bento_real_t number = v->number;
    bool usable = true;

    if (v->kind == VALUE_MISSING) {
        *setting =
            panel->value; /* no v: the current value is the default */
    } else if ((v->kind == VALUE_TRUE) || (v->kind == VALUE_FALSE)) {
        *setting = (v->kind == VALUE_TRUE) ? 1 : 0;
    } else if ((v->kind != VALUE_NUMBER) ||
               ((number - number) != (bento_real_t)0.0)) {
        usable =
            false; /* not a number, or inf: past the largest real */
    } else if (number >= (bento_real_t)VALUE_MAX) {
        *setting = VALUE_MAX;
    } else if (number <= (bento_real_t)VALUE_MIN) {
        *setting = VALUE_MIN;
    } else {
        *setting = (int32_t)number;
    }
    return usable;
}

static void act_on_set(panel_t *panel, const json_value_t *v)
{
    char text[TEXT_BYTES];
    int32_t setting = 0;

    if (setting_from(panel, v, &setting)) {
        set_value(panel, setting);
        (void)snprintf(text, sizeof(text), "เว็บตั้งค่าเป็น %" PRId32,
                       panel->value);
        show(panel, W_LAST, COLOR_ACCENT, text);
    } else {
        show(panel, W_LAST, COLOR_WARN, "set ได้ค่าที่ใช้ไม่ได้ ไม่เปลี่ยนอะไร");
    }
}

static void act_on_say(const panel_t *panel, const json_value_t *said)
{
    char safe[SAY_BYTES];
    char text[TEXT_BYTES];

    screen_safe(said->text, said->text_len, safe, sizeof(safe));
    (void)snprintf(text, sizeof(text), "เว็บบอกว่า %s", safe);
    show(panel, W_LAST, COLOR_ACCENT, text);
}

/* ui.tone takes a MIDI note, not hertz. */
static void act_on_beep(const panel_t *panel)
{
    if (panel->has_sound) {
        (void)bento_ui_sfx_tone(panel->ui, (uint8_t)BEEP_NOTE,
                                (uint8_t)BENTO_UI_WAVE_SQUARE,
                                (uint8_t)BEEP_VELOCITY,
                                (uint16_t)BEEP_MS);
    }
    show(panel, W_LAST, COLOR_ACCENT,
         panel->has_sound ? "beep" : "beep แต่บอร์ดนี้ไม่มีเสียง");
}

static bool is_word(const json_value_t *value, const char *word)
{
    return (value->kind == VALUE_STRING) &&
           (strcmp(value->text, word) == 0);
}

static void act_on(panel_t *panel, const uint8_t *payload,
                   uint32_t length)
{
    command_t command;
    const bool usable = read_command(payload, length, &command);

    if (usable && is_word(&command.cmd, "set")) {
        act_on_set(panel, &command.v);
    } else if (usable && is_word(&command.cmd, "say")) {
        act_on_say(panel, &command.text);
    } else if (usable && is_word(&command.cmd, "beep")) {
        act_on_beep(panel);
    } else {
        show(panel, W_LAST, COLOR_WARN, "ไม่รู้จักคำสั่งนี้");
    }
}

/* The binding's inbox has one slot, a new message over the old, so it
 * is asked every round and only the newest message is acted on. */
static void read_inbox(panel_t *panel)
{
    char topic[NAME_BYTES];
    uint8_t payload[PAYLOAD_BYTES];
    uint8_t newest[PAYLOAD_BYTES];
    bento_mqtt_msg_t message;
    bool has_message = false;
    uint32_t length = 0U;

    bento_mqtt_msg_init(&message);
    while (bento_mqtt_poll(panel->client, &message, topic,
                           sizeof(topic), payload,
                           PAYLOAD_BYTES - 1U) == BENTO_OK) {
        length = message.payload_len;
        (void)memcpy(newest, payload, (size_t)length);
        has_message = true;
    }
    if (has_message) {
        act_on(panel, newest, length);
    }
}

/* True when the event is of this kind; the kind passes through a
 * uint8_t, so both sides of the comparison have one type. */
static bool type_is(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static void handle_events(panel_t *panel)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(panel->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (type_is(&events[i], BENTO_UI_EVENT_CLICKED) &&
            (events[i].id == panel->ids[W_BUTTON])) {
            send_press(panel);
        } else if (type_is(&events[i], BENTO_UI_EVENT_VALUE_CHANGED) &&
                   (events[i].id == panel->ids[W_SLIDER])) {
            set_value(panel, events[i].value);
        } else {
            /* nothing else changes anything */
        }
    }
}

/* The firmware does not reconnect by itself; the line says so. The
 * screen is not polled again here: a tap it answered would be lost. */
static void check_link(panel_t *panel)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;
    char text[TEXT_BYTES];

    if (panel->alive &&
        ((bento_mqtt_state(panel->client, &state) != BENTO_OK) ||
         (state != BENTO_MQTT_STATE_CONNECTED))) {
        panel->alive = false;
    }
    if (!panel->alive) {
        show(panel, W_LINK, COLOR_BAD,
             "สาย: หลุดแล้ว เฟิร์มแวร์ไม่ต่อใหม่ให้เอง");
    }
    (void)snprintf(
        text, sizeof(text),
        "ส่งแล้ว telemetry %" PRId32 "  event %" PRId32 "  หาย %" PRId32,
        panel->tele_count, panel->event_count, panel->lost_count);
    show_text(panel->ui, panel->ids[W_SENT], text);
}

/* One round: the screen, the inbox, and the telemetry when it is due
 * (a page just opened sees it within 2 s). Then the rest of POLL_MS. */
static bento_status_t run_round(panel_t *panel, uint32_t *tele_ms)
{
    const uint32_t work_ms = bento_time_ticks_ms();
    int32_t rest_ms;
    bento_status_t status = BENTO_OK;

    handle_events(panel);
    read_inbox(panel);
    if (panel->alive &&
        ((panel->tele_count == 0) ||
         (bento_time_ticks_diff(work_ms, *tele_ms) >= TELE_MS))) {
        *tele_ms = work_ms;
        send_telemetry(panel);
    }
    check_link(panel);
    rest_ms =
        POLL_MS - bento_time_ticks_diff(bento_time_ticks_ms(), work_ms);
    if (rest_ms > 0) {
        status = pause_ms((uint32_t)rest_ms);
    } else if (!bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    } else {
        /* the round took all its time: straight on */
    }
    return status;
}

static bento_status_t run_panel(panel_t *panel)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    uint32_t tele_ms = start_ms;
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;

    panel->alive = true;
    while ((status == BENTO_OK) && panel->alive &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = run_round(panel, &tele_ms);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "จบแล้ว ส่ง %" PRId32 " ใบ",
                       panel->tele_count + panel->event_count);
        show(panel, W_STATUS, COLOR_DIM, text);
        let_screen_draw(panel->ui);
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>ส่ง telemetry %" PRId32
                       " ใบ event %" PRId32 " ใบ</span>",
                       panel->tele_count, panel->event_count);
        (void)bento_lcd_print(text);
        (void)printf("ส่ง telemetry %" PRId32 " ใบ | event %" PRId32
                     " ใบ | หาย %" PRId32 " ใบ\n",
                     panel->tele_count, panel->event_count,
                     panel->lost_count);
    }
    return status;
}

/* A sound chip is there when the ui core says so (hasattr(ui, "tone")). */
static bool board_has_sound(const bento_ui_t *ui)
{
    bento_ui_caps_t caps;

    return (bento_ui_get_capabilities(ui, &caps) == BENTO_OK) &&
           ((caps.feature_bits & (uint32_t)BENTO_UI_FEATURE_SFX) != 0U);
}

int main(void)
{
    panel_t panel;
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_status_t status;

    (void)memset(&panel, 0, sizeof(panel));
    (void)memset(g_networks, 0, sizeof(g_networks));
    panel.ui = bento_runtime_get_ui();
    panel.value = VALUE_START;
    panel.has_sound = board_has_sound(panel.ui);
    build_names(&panel.names);
    status = build_screen(&panel);
    if (status == BENTO_OK) {
        check_team(&panel);
    }
    if (go_on(&panel, status)) {
        status = step_wifi(&panel, ip, sizeof(ip));
    }
    if (go_on(&panel, status)) {
        status = step_broker(&panel, ip);
    }
    if (go_on(&panel, status)) {
        status = run_panel(&panel);
    }
    if (panel.client_open) {
        (void)bento_mqtt_close(panel.client); /* C frees its client */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
