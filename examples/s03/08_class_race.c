/*
 * 08_class_race.c -- เกมกดเร็วทั้งห้อง
 *
 * ผู้สอนส่งคำสั่งเริ่มรอบให้ทั้งห้องพร้อมกันทาง broker บอร์ดแต่ละทีม
 * สุ่มรอเอง 1.5-4 วินาที (ป้ายส้ม ห้ามกด) แล้วไฟทุกดวงติด (ป้ายเขียว
 * กดเลย) บอร์ดจับเวลาตอบสนองเองจากขอบแรกที่ขาปุ่มเปลี่ยน แล้วส่งผล
 * ขึ้น broker เป็น event ส่วนสถานะส่งเป็น telemetry ทุก 2 วินาทีตอน
 * ว่าง กดก่อนไฟติด = ออกตัวก่อน (แดง) เกิน 3 วินาที = พลาด กดตอนว่าง
 * = รอบซ้อม (ไม่ส่งขึ้น broker) หน้าเว็บสั่ง beep say led ได้ตอนว่าง
 * ต้องแก้ TEAM ก่อน แล้วต่อ WiFi และ broker ทีละขั้น เกมเปิด 45 นาที
 *
 * A reaction race for the whole class. The teacher starts a round
 * for every board at once through the broker. Each board waits a
 * random 1.5-4 s of its own (orange: do not press), then lights all
 * its LEDs (green: press now). The board times the reaction itself,
 * from the first edge of the button pin, and sends the result to
 * the broker as an event; its state goes out as telemetry every 2 s
 * while idle. A press before the light is a false start (red); no
 * press within 3 s is a miss. A press while idle starts a practice
 * round that is not sent. A web page may send beep, say and led
 * while idle. Set TEAM first; Wi-Fi and the broker are then joined
 * step by step. The game stays open for 45 minutes.
 *
 * Twin of 08_class_race.py
 * (embedded-systems-for-aiot-developer/examples/s03/08_class_race.py).
 * Style: CLEAN_C.md.
 * In the BENTO Emulator, MQTT from C is not available yet: the
 * program says so and ends.
 * Differs from the original:
 *   - json.dumps writes a dict in MicroPython's hash-table order,
 *     which follows the size of the qstr hash: 1 byte on the PSoC
 *     Edge, 2 bytes on the reTerminal (py/map.c, py/qstr.c). The
 *     key orders of every report are worked out for both and kept
 *     in tables below (chosen at run time: on_reterminal()).
 *   - json.loads is mirrored by a small reader that follows
 *     extmod/modjson.c (commas and colons are skipped like spaces,
 *     ] and } close either kind). A number is read with plain
 *     arithmetic, so a float with many digits can differ from
 *     MicroPython's in its last bit. A list or an object sent as
 *     "text" is shown as its JSON text, where MicroPython shows
 *     Python's str() of it. A round number sent as true counts as
 *     round 1, which MicroPython would show as True.
 *   - Text follows the board's MicroPython: on the PSoC Edge a str
 *     is bytes (no unicode str in its build), so decode() accepts
 *     any bytes, \u keeps one byte and the say filter walks bytes;
 *     on the reTerminal it is UTF-8 characters.
 *   - The binding keeps one incoming message and a new one replaces
 *     the old; bento_mqtt keeps four. The twin reads them all each
 *     round and acts on the newest, as the one slot would. A payload
 *     is cut to 255 bytes, as there.
 *   - ui.tone() is missing from a build without sound, where its
 *     first call raises AttributeError; here that call ends the
 *     program with BENTO_E_UNSUPPORTED.
 *   - wifi.connect, mqtt.connect and mqtt.subscribe print console
 *     lines of their own; the C modules print none. The program
 *     prints the ones C can know: "[WiFi] Connecting...", "[WiFi]
 *     Connected! IP=...", "[MQTT] Connecting to ...", "[MQTT]
 *     Connected to ... as '...'", "[MQTT] Subscribed to '...'
 *     (QoS=N)" and "[MQTT] subscribe rejected by broker". The radio's
 *     per-attempt lines and the cy result codes are not reported by
 *     the C API. The binding saves the Wi-Fi password on a
 *     successful join; C asks for that with BENTO_WIFI_F_SAVE_CREDS.
 *     A wait is cut short by Stop in both.
 *   - mqtt.connect raises OSError only when Wi-Fi cannot start; C
 *     takes BENTO_E_NOT_READY and BENTO_E_UNSUPPORTED as that case
 *     and every other failure as False. mqtt.subscribe without a
 *     link raises; here it is a refusal like False.
 *   - The original leaves its broker link open when it ends; a C
 *     program closes its client. Stop leaves the LEDs as they are,
 *     as KeyboardInterrupt does there.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_mqtt.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* ---- JSON: what json.loads gives this program -------------------- */

typedef enum {
    JSON_ABSENT = 0,
    JSON_STRING,
    JSON_INTEGER,
    JSON_REAL,
    JSON_TRUE,
    JSON_FALSE,
    JSON_NULL,
    JSON_OBJECT,
    JSON_ARRAY
} json_kind_t;

/* A value as a slice of the payload; a string without its quotes. */
typedef struct {
    json_kind_t kind;
    uint32_t start;
    uint32_t end;
} json_token_t;

/* The command: was it an object, its "cmd", "r", "text", "n" and
 * "on", and how many items "on" holds when it is a container. */
typedef struct {
    bool is_object;
    json_token_t action;
    json_token_t round;
    json_token_t words;
    json_token_t led;
    json_token_t on;
    uint32_t on_items;
} command_t;

/* The course palette, 0xRRGGBB (SPEC S7.13). */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* Times in milliseconds. The round's rules are the web page's too
 * (class_game.html): change them in both places. */
enum {
    SETTLE_MS = 200,      /* let the cleared screen settle */
    DEBOUNCE_MS = 40,     /* as in files 05 and 07 */
    POLL_MS = 5,          /* the game clock's grain */
    STATE_MS = 2000,      /* telemetry this often, only while idle */
    RUN_MS = 2700000,     /* 45 minutes: six rounds and the total */
    WAIT_MIN_MS = 1500,   /* after the start, at least this long */
    WAIT_SPAN_MS = 2500,  /* plus 0 to this, different per board */
    GO_TIMEOUT_MS = 3000, /* no press by then: this round is missed */
    HUMAN_MIN_MS = 100,   /* faster is a guess (the athletics rule) */
    WAIT_SLICE_MS = 50,   /* a long Wi-Fi or broker wait, in slices */
    IP_TRIES = 15,
    IP_WAIT_MS = 200,
    MS_PER_S = 1000
};

/* Ports, notes, limits and sizes. */
enum {
    BROKER_PORT = 1883,
    START_NOTE = 57, /* MIDI notes, not hertz */
    START_VELOCITY = 60,
    START_MS = 80,
    GO_NOTE = 81,
    GO_VELOCITY = 100,
    GO_MS = 120,
    BEEP_NOTE = 69,
    BEEP_VELOCITY = 90,
    BEEP_MS = 150,
    ROUND_MAX = 9999,
    INTEGER_LIMIT = 100000, /* past every range checked here */
    SAY_CHARS = 24,
    TEAM_LENGTH = 6, /* "team" and two digits */
    TEAM_PREFIX = 4,
    TEXT_BYTES = 160,
    NAME_BYTES = 64,
    /* A line with two topics in it: 70 bytes of words and two
     * names of up to 63 bytes. */
    TOPICS_LINE_BYTES = 80 + (2 * NAME_BYTES),
    /* A round's name or a time: "ซ้อม" (12 bytes) or an int32_t
     * (11), and the NUL. */
    NUMBER_BYTES = 16,
    TOPIC_BYTES = 128,   /* BENTO_MQTT_TOPIC_MAX */
    PAYLOAD_BYTES = 256, /* the binding keeps 255 bytes and a NUL */
    JSON_BYTES = 200,    /* the longest report, with any int32_t */
    SAY_BYTES = 97,      /* 24 characters of up to 4 bytes, a NUL */
    DEPTH_MAX = 256,     /* a 255-byte payload nests at most 255 */
    BITS_PER_BYTE = 8
};

/* The names built from ROOT and TEAM. */
typedef struct {
    char device_id[NAME_BYTES];
    char topic_cmd[NAME_BYTES];
    char topic_all[NAME_BYTES]; /* the teacher starts rounds here */
    char topic_event[NAME_BYTES];
    char topic_state[NAME_BYTES];
} names_t;

/* The widgets the program changes, and the board it runs on. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t headline;
    bento_ui_id_t seg;
    bento_ui_id_t round;
    bento_ui_id_t counts;
    bento_ui_id_t big;
    bento_ui_id_t icon;
    bento_ui_id_t topic;
    bento_ui_id_t note;
    uint32_t led_count; /* Eva Kit 3, Dev Kit 5 */
    bool has_sound;
    bool stopped; /* stop_here() ended the run on purpose */
    names_t names;
} race_screen_t;

/* The parts of a round. */
typedef enum {
    PHASE_IDLE = 0,
    PHASE_WAIT, /* armed: do not press */
    PHASE_GO    /* the lights are on: press */
} phase_t;

/* The game and the broker link. */
typedef struct {
    uint32_t client;
    phase_t phase;
    int32_t round;      /* 0 = practice, never sent */
    int32_t last_round; /* a repeated start does not restart */
    uint32_t arm_ms;
    int32_t wait_ms; /* how long this board waits this round */
    uint32_t go_ms;  /* the lights came on: the clock starts */
    bool has_best;
    int32_t best_ms; /* real rounds only */
    bool has_last;
    int32_t last_ms;
    int32_t rounds;
    int32_t presses;
    int32_t message_count;
    int32_t fail_count;
    int32_t bad_count;
    bool lost;
} race_t;

/* The button, debounced, with the time of its first edge. */
typedef struct {
    bool stable;
    bool last_raw;
    uint32_t last_change_ms;
    bool has_edge;
    uint32_t edge_ms; /* when the pin first read "pressed" */
} button_t;

/* How a round ended. */
typedef enum {
    END_FALSE_START = 0,
    END_MISS,
    END_TIME
} round_end_t;

typedef struct {
    round_end_t end;
    bool has_ms;
    int32_t ms;
} result_t;

/* ---- the game loop ----------------------------------------------- */

/* The clock for telemetry and the countdown. */
typedef struct {
    uint32_t start_ms;
    uint32_t state_ms;
    int32_t shown_s;
} game_clock_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
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

static bool go_on(const race_screen_t *screen, bento_status_t status)
{
    return (status == BENTO_OK) && !screen->stopped;
}

static uint32_t text_length(const char *text)
{
    return (uint32_t)strlen(text);
}

/* wifi.ip(): "0.0.0.0" when the interface has no address. */
static void read_ip(char *text, uint32_t cap)
{
    uint32_t address = 0U;
    uint32_t length = 0U;

    if ((bento_wifi_ip(BENTO_WIFI_IF_ANY, &address) != BENTO_OK) ||
        (bento_wifi_ipv4_format(address, text, cap, &length) !=
         BENTO_OK)) {
        (void)snprintf(text, (size_t)cap, "0.0.0.0");
    }
}

/* "0.0.0.0" is a non-empty text, so only comparing finds it. */
static bool has_address(const char *ip)
{
    return strcmp(ip, "0.0.0.0") != 0;
}

/* mqtt.subscribe(topic): its console line, and True or False. */
static bool subscribe(uint32_t client, const char *topic)
{
    uint8_t granted = 0U;
    const bento_status_t answer = bento_mqtt_subscribe(
        client, topic, text_length(topic), 0U, &granted);

    if (answer == BENTO_OK) {
        (void)printf("[MQTT] Subscribed to '%s' (QoS=%d)\r\n", topic,
                     (int)granted);
    } else if (answer == BENTO_E_FAILED) {
        (void)printf("[MQTT] subscribe rejected by broker\r\n");
    } else {
        /* the binding's own failure line carries a code C lacks */
    }
    return answer == BENTO_OK;
}

/* "ซ้อม" for the practice round, else its number. */
static void round_name(int32_t round, char *out, uint32_t cap)
{
    if (round == 0) {
        (void)snprintf(out, (size_t)cap, "ซ้อม");
    } else {
        (void)snprintf(out, (size_t)cap, "%" PRId32, round);
    }
}

static void best_text(const race_t *race, char *out, uint32_t cap)
{
    if (race->has_best) {
        (void)snprintf(out, (size_t)cap, "%" PRId32, race->best_ms);
    } else {
        (void)snprintf(out, (size_t)cap, "-");
    }
}

/* ---- the round --------------------------------------------------- */

/* Every LED at once: the bigger the signal, the sooner it is seen. */
static bento_status_t lights(const race_screen_t *screen, bool on)
{
    uint32_t led;
    bento_status_t status = BENTO_OK;

    for (led = 0U; (led < screen->led_count) && (status == BENTO_OK);
         led++) {
        status = on ? bento_gpio_led_on(led) : bento_gpio_led_off(led);
    }
    return status;
}

/* The result dict as lcd.print() shows it (its repr). */
static void result_repr(const result_t *result, char *out, uint32_t cap)
{
    if (result->end == END_MISS) {
        (void)snprintf(out, (size_t)cap, "{'miss': 1}");
    } else if (result->end == END_TIME) {
        (void)snprintf(out, (size_t)cap, "{'ms': %" PRId32 "}",
                       result->ms);
    } else if (result->has_ms) {
        (void)snprintf(out, (size_t)cap,
                       "{'false': 1, 'ms': %" PRId32 "}", result->ms);
    } else {
        (void)snprintf(out, (size_t)cap, "{'false': 1}");
    }
}

/* ---- the button -------------------------------------------------- */

/* Debounces as file 05 does, but keeps the time of the first edge:
 * debouncing makes us believe a press 40 ms late, and timing from
 * that belief would make every team slow by the same 40 ms.
 * *pressed is true in the one pass that confirms a press. */
static bento_status_t read_button(button_t *button, uint32_t now_ms,
                                  bool *pressed, uint32_t *pressed_ms)
{
    bool raw = false;
    const bento_status_t status =
        bento_gpio_button_is_pressed(0U, &raw);
    const bool settled =
        bento_time_ticks_diff(now_ms, button->last_change_ms) >=
        DEBOUNCE_MS;

    *pressed = false;
    if ((status == BENTO_OK) && (raw != button->last_raw)) {
        button->last_raw = raw;
        button->last_change_ms = now_ms;
        /* a bounce back keeps the edge, or the time would slide to
         * the bounce's late edge */
        if (raw && !button->stable && !button->has_edge) {
            button->has_edge = true; /* the finger really touched */
            button->edge_ms = now_ms;
        }
    } else if ((status == BENTO_OK) && (raw == button->stable) &&
               button->has_edge && settled) {
        button->has_edge = false; /* settled back: only a blip */
    } else if ((status == BENTO_OK) && (raw != button->stable) &&
               settled) {
        button->stable = raw;
        *pressed = raw;
        *pressed_ms = button->has_edge ? button->edge_ms : now_ms;
        button->has_edge = false;
    } else {
        /* still bouncing */
    }
    return status;
}

static bool still_linked(const race_t *race)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;

    return (bento_mqtt_state(race->client, &state) == BENTO_OK) &&
           (state == BENTO_MQTT_STATE_CONNECTED);
}

/* A container value of the command still being read. */
typedef enum {
    OPEN_NONE = 0,
    OPEN_WORDS,
    OPEN_ON
} open_field_t;

/* The reader's place, mirroring extmod/modjson.c: one pending key
 * shared by every level, and whether each open level is an object. */
typedef struct {
    const char *text;
    uint32_t length;
    uint32_t at;
    uint32_t depth;
    uint8_t is_object[DEPTH_MAX / BITS_PER_BYTE];
    bool started;
    bool done;
    bool has_key;
    json_token_t key;
    open_field_t open_field;
} json_reader_t;

/* A str of this board: characters, or bytes. */
static bool unicode_text(void)
{
    /* the reTerminal's MicroPython has unicode str; the PSoC Edge's
     * (CORE_FEATURES) keeps a str as bytes */
    return on_reterminal();
}

/* ---- the JSON reader (extmod/modjson.c's rules) ------------------ */

static bool is_digit(char letter)
{
    return (letter >= '0') && (letter <= '9');
}

/* Is the byte one of `set`? A NUL byte in a payload is in none. */
static bool in_set(char letter, const char *set)
{
    return (letter != '\0') && (strchr(set, (int)letter) != NULL);
}

static bool is_number_letter(char letter)
{
    return is_digit(letter) || in_set(letter, "+-.eE");
}

/* The quoted text: an escape takes two bytes, \u six. false when
 * the closing quote is missing. */
static bool scan_string(json_reader_t *reader, json_token_t *token)
{
    const uint32_t unicode_escape = 6U;

    token->kind = JSON_STRING;
    token->start = reader->at;
    while ((reader->at < reader->length) &&
           (reader->text[reader->at] != '"')) {
        if (reader->text[reader->at] != '\\') {
            reader->at++;
        } else if (((reader->at + 1U) < reader->length) &&
                   (reader->text[reader->at + 1U] == 'u')) {
            reader->at += unicode_escape;
        } else {
            reader->at += 2U;
        }
    }
    token->end = reader->at;
    reader->at++;
    return token->end < reader->length;
}

/* "ull", "rue" or "alse" after its first letter. */
static bool scan_word(json_reader_t *reader, const char *rest,
                      json_kind_t kind, json_token_t *token)
{
    const uint32_t length = text_length(rest);
    bool matches = (reader->at + length) <= reader->length;

    if (matches) {
        matches = strncmp(&reader->text[reader->at], rest,
                          (size_t)length) == 0;
    }
    token->kind = kind;
    reader->at += length;
    token->end = reader->at;
    return matches;
}

/* Digits, with '-' only first; *at moves past them. */
static uint32_t skip_digits(const char *text, uint32_t at, uint32_t end)
{
    uint32_t next = at;

    while ((next < end) && is_digit(text[next])) {
        next++;
    }
    return next;
}

/* -?digits for an int; a float also takes .digits and e[+-]digits. */
static bool number_is_valid(const char *text, const json_token_t *token)
{
    const uint32_t sign = (text[token->start] == '-') ? 1U : 0U;
    uint32_t at = token->start + sign;
    const uint32_t whole_end = skip_digits(text, at, token->end);
    bool valid = whole_end > at;

    at = whole_end;
    if ((token->kind == JSON_REAL) && (at < token->end) &&
        (text[at] == '.')) {
        const uint32_t part_end =
            skip_digits(text, at + 1U, token->end);

        valid = valid || (part_end > (at + 1U));
        at = part_end;
    }
    if ((token->kind == JSON_REAL) && (at < token->end) &&
        ((text[at] == 'e') || (text[at] == 'E'))) {
        at++;
        at += ((at < token->end) &&
               ((text[at] == '+') || (text[at] == '-')))
                  ? 1U
                  : 0U;
        valid = valid && (skip_digits(text, at, token->end) > at);
        at = skip_digits(text, at, token->end);
    }
    return valid && (at == token->end);
}

static bool scan_number(json_reader_t *reader, json_token_t *token)
{
    token->kind = JSON_INTEGER;
    token->start = reader->at - 1U;
    while ((reader->at < reader->length) &&
           is_number_letter(reader->text[reader->at])) {
        if (in_set(reader->text[reader->at], ".eE")) {
            token->kind = JSON_REAL;
        }
        reader->at++;
    }
    token->end = reader->at;
    return number_is_valid(reader->text, token);
}

/* The value that starts with `first`; false on a syntax error. */
static bool scan_value(json_reader_t *reader, char first,
                       json_token_t *token)
{
    bool valid = true;

    token->start = reader->at - 1U;
    token->end = reader->at;
    if (first == '{') {
        token->kind = JSON_OBJECT;
    } else if (first == '[') {
        token->kind = JSON_ARRAY;
    } else if (first == '"') {
        valid = scan_string(reader, token);
    } else if (first == 'n') {
        valid = scan_word(reader, "ull", JSON_NULL, token);
    } else if (first == 't') {
        valid = scan_word(reader, "rue", JSON_TRUE, token);
    } else if (first == 'f') {
        valid = scan_word(reader, "alse", JSON_FALSE, token);
    } else if ((first == '-') || is_digit(first)) {
        valid = scan_number(reader, token);
    } else {
        valid = false;
    }
    return valid;
}

/* UTF-8: a lead byte tells how many bytes follow; a code point is
 * written in 1 to 4 bytes, six bits in each byte after the first. */
enum {
    UTF8_FOLLOW_MASK = 0xC0, /* 10xxxxxx: a following byte */
    UTF8_FOLLOW = 0x80,
    UTF8_LEAD_TWO = 0xC0,
    UTF8_LEAD_THREE = 0xE0,
    UTF8_LEAD_FOUR = 0xF0,
    UTF8_LEAD_END = 0xF8, /* from here: not a lead byte */
    UTF8_THREE = 3,
    UTF8_MAX_BYTES = 4,
    UTF8_SIX_BITS = 0x3F,
    UTF8_FOUR_BITS = 0x0F, /* the code bits of a three-byte lead */
    UTF8_SHIFT = 6,
    UNICODE_ESCAPE_BYTES = 6, /* \u and four hex characters */
    HEX_SHIFT = 4,
    DECIMAL_BASE = 10
};

/* The bytes of a character that starts with this lead byte (1 to 4);
 * a lead below 0xC0 stands alone. */
static uint32_t char_width(uint8_t lead)
{
    uint32_t width = 1U;

    if (lead >= (uint32_t)UTF8_LEAD_FOUR) {
        width = UTF8_MAX_BYTES;
    } else if (lead >= (uint32_t)UTF8_LEAD_THREE) {
        width = UTF8_THREE;
    } else if (lead >= (uint32_t)UTF8_LEAD_TWO) {
        width = 2U;
    } else {
        width = 1U;
    }
    return width;
}

/* \uXXXX as json.loads reads it: each letter's value is worked
 * out without a check, so a letter that is not hex gives garbage
 * there too. */
static uint32_t unicode_escape(const char *text, uint32_t at)
{
    const uint8_t hex_gap = (uint8_t)('a' - ('9' + 1));
    const uint8_t nine = 9U;
    const uint8_t lower_case = 0x20U;
    uint32_t code = 0U;
    uint32_t i;

    for (i = 2U; i < (uint32_t)UNICODE_ESCAPE_BYTES; i++) {
        uint8_t digit = (uint8_t)(((uint8_t)text[at + i] | lower_case) -
                                  (uint8_t)'0');

        digit = (digit > nine) ? (uint8_t)(digit - hex_gap) : digit;
        code = (code << (uint32_t)HEX_SHIFT) | digit;
    }
    return code;
}

/* \b \f \n \r \t, else the letter itself (\" \\ \/ and any other). */
static char simple_escape(char letter)
{
    static const char from[] = "bfnrt";
    static const char to[] = "\b\f\n\r\t";
    char result = letter;
    uint32_t i;

    for (i = 0U; from[i] != '\0'; i++) {
        if (from[i] == letter) {
            result = to[i];
        }
    }
    return result;
}

/* A code point as UTF-8 bytes (vstr_add_char); the count written. */
static uint32_t put_utf8(char *out, uint32_t code)
{
    static const uint32_t limits[UTF8_THREE] = {0x80U, 0x800U,
                                                0x10000U};
    static const uint8_t leads[UTF8_MAX_BYTES] = {
        0x00U, UTF8_LEAD_TWO, UTF8_LEAD_THREE, UTF8_LEAD_FOUR};
    uint32_t extra = 0U;
    uint32_t rest = code;
    uint32_t i;

    while ((extra < (uint32_t)UTF8_THREE) && (code >= limits[extra])) {
        extra++;
    }
    for (i = extra; i > 0U; i--) {
        out[i] = (char)(uint8_t)((uint32_t)UTF8_FOLLOW |
                                 (rest & (uint32_t)UTF8_SIX_BITS));
        rest >>= (uint32_t)UTF8_SHIFT;
    }
    out[0] = (char)(uint8_t)(leads[extra] | rest);
    return extra + 1U;
}

/* One escape at text[*at] ('\\' first) into out; answers the bytes
 * written and moves *at past it. */
static uint32_t decode_escape(const char *text, uint32_t *at, char *out)
{
    uint32_t used = 1U;

    if (text[*at + 1U] == 'u') {
        const uint32_t code = unicode_escape(text, *at);

        if (unicode_text()) {
            used = put_utf8(out, code);
        } else {
            out[0] = (char)(uint8_t)code; /* the low byte */
        }
        *at += (uint32_t)UNICODE_ESCAPE_BYTES;
    } else {
        out[0] = simple_escape(text[*at + 1U]);
        *at += 2U;
    }
    return used;
}

/* The string's text, unescaped and cut to fit; its length in
 * bytes (it may hold a NUL, from \u0000). */
static uint32_t decode_string(const char *text,
                              const json_token_t *token, char *out,
                              uint32_t cap)
{
    uint32_t at = token->start;
    uint32_t used = 0U;

    while ((at < token->end) &&
           ((used + (uint32_t)UTF8_MAX_BYTES) < cap)) {
        if (text[at] != '\\') {
            out[used] = text[at];
            used++;
            at++;
        } else {
            used += decode_escape(text, &at, &out[used]);
        }
    }
    out[used] = '\0';
    return used;
}

static bool string_equals(const char *text, const json_token_t *token,
                          const char *word)
{
    char decoded[PAYLOAD_BYTES];
    const uint32_t length =
        decode_string(text, token, decoded, sizeof(decoded));

    return (token->kind == JSON_STRING) &&
           (length == text_length(word)) &&
           (strncmp(decoded, word, (size_t)length) == 0);
}

/* A later key replaces an earlier one, as in a dict. */
static void keep_entry(json_reader_t *reader, const json_token_t *value,
                       command_t *command)
{
    const bool container =
        (value->kind == JSON_OBJECT) || (value->kind == JSON_ARRAY);
    const char *text = reader->text;

    if (string_equals(text, &reader->key, "cmd")) {
        command->action = *value;
    } else if (string_equals(text, &reader->key, "r")) {
        command->round = *value;
    } else if (string_equals(text, &reader->key, "text")) {
        command->words = *value;
        reader->open_field = container ? OPEN_WORDS : OPEN_NONE;
    } else if (string_equals(text, &reader->key, "n")) {
        command->led = *value;
    } else if (string_equals(text, &reader->key, "on")) {
        command->on = *value;
        command->on_items = 0U;
        reader->open_field = container ? OPEN_ON : OPEN_NONE;
    } else {
        /* other keys are not read */
    }
}

static bool level_is_object(const json_reader_t *reader)
{
    const uint32_t byte = reader->depth / BITS_PER_BYTE;
    const uint32_t bit = reader->depth % BITS_PER_BYTE;

    return ((reader->is_object[byte] >> bit) & 1U) != 0U;
}

static bool enter_level(json_reader_t *reader, bool is_object)
{
    const bool room = (reader->depth + 1U) < (uint32_t)DEPTH_MAX;

    if (room) {
        const uint32_t byte = (reader->depth + 1U) / BITS_PER_BYTE;
        const uint8_t mask =
            (uint8_t)(1U << ((reader->depth + 1U) % BITS_PER_BYTE));

        reader->depth++;
        reader->is_object[byte] =
            is_object ? (uint8_t)(reader->is_object[byte] | mask)
                      : (uint8_t)((uint32_t)reader->is_object[byte] &
                                  ~(uint32_t)mask);
    }
    return room;
}

/* A value inside a container: a key, a key's value, or an item.
 * Items stored straight into "on" are counted: an empty list or
 * object is false. */
static bool place_value(json_reader_t *reader,
                        const json_token_t *value, command_t *command)
{
    const bool enters =
        (value->kind == JSON_OBJECT) || (value->kind == JSON_ARRAY);
    const bool is_key = level_is_object(reader) && !reader->has_key;
    bool valid = true;

    if (is_key) {
        valid = !enters; /* a container cannot be a key */
        reader->key = *value;
        reader->has_key = true;
    } else if (level_is_object(reader)) {
        if (reader->depth == 1U) {
            keep_entry(reader, value, command);
        }
        reader->has_key = false;
    } else {
        /* an item of a list */
    }
    if (!is_key && (reader->depth == 2U) &&
        (reader->open_field == OPEN_ON)) {
        command->on_items++;
    }
    if (valid && enters) {
        valid = enter_level(reader, value->kind == JSON_OBJECT);
    }
    return valid;
}

static bool place_first(json_reader_t *reader,
                        const json_token_t *value, command_t *command)
{
    bool valid = true;

    reader->started = true;
    command->is_object = value->kind == JSON_OBJECT;
    if ((value->kind == JSON_OBJECT) || (value->kind == JSON_ARRAY)) {
        valid = enter_level(reader, command->is_object);
    } else {
        reader->done = true; /* one value alone */
    }
    return valid;
}

/* A closing bracket of either kind ends the level it is in. */
static bool close_level(json_reader_t *reader, command_t *command)
{
    if (reader->started) {
        reader->done = reader->depth == 1U;
        reader->depth--;
    }
    if ((reader->open_field != OPEN_NONE) && (reader->depth == 1U)) {
        if (reader->open_field == OPEN_WORDS) {
            command->words.end = reader->at; /* "text" ends here */
        }
        reader->open_field = OPEN_NONE;
    }
    return reader->started;
}

/* One step of the reader; false on a syntax error. */
static bool read_step(json_reader_t *reader, command_t *command)
{
    json_token_t value = {JSON_ABSENT, 0U, 0U};
    char first;
    bool valid;

    while ((reader->at < reader->length) &&
           in_set(reader->text[reader->at], ",: \t\n\r")) {
        reader->at++;
    }
    valid = reader->at < reader->length;
    if (valid) {
        first = reader->text[reader->at];
        reader->at++;
        if ((first == '}') || (first == ']')) {
            valid = close_level(reader, command);
        } else if (scan_value(reader, first, &value)) {
            valid = reader->started
                        ? place_value(reader, &value, command)
                        : place_first(reader, &value, command);
        } else {
            valid = false;
        }
    }
    return valid;
}

/* json.loads(payload): false where it raises ValueError. */
static bool read_command(const char *payload, uint32_t length,
                         command_t *command)
{
    json_reader_t reader;
    bool valid = true;

    (void)memset(&reader, 0, sizeof(reader));
    (void)memset(command, 0, sizeof(*command));
    reader.text = payload;
    reader.length = length;
    while (valid && !reader.done) {
        valid = read_step(&reader, command);
    }
    while ((reader.at < reader.length) &&
           in_set(payload[reader.at], " \t\n\r\v\f")) {
        reader.at++;
    }
    return valid && (reader.at == reader.length);
}

/* bytes.decode(): false for text that is not UTF-8 (utf8_check),
 * on the reTerminal only. */
static bool decodes(const char *text, uint32_t length)
{
    uint32_t need = 0U;
    bool valid = true;
    uint32_t i;

    for (i = 0U; unicode_text() && valid && (i < length); i++) {
        const uint8_t byte = (uint8_t)text[i];

        if (need > 0U) {
            valid = (byte & (uint32_t)UTF8_FOLLOW_MASK) ==
                    (uint32_t)UTF8_FOLLOW;
            need--;
        } else if (byte >= (uint32_t)UTF8_LEAD_TWO) {
            valid = byte < (uint32_t)UTF8_LEAD_END;
            need = char_width(byte) - 1U;
        } else {
            valid = byte < (uint32_t)UTF8_FOLLOW;
        }
    }
    return valid && (need == 0U);
}

/* The value of a decimal digit character. */
static int32_t digit_value(char digit)
{
    return (int32_t)(uint8_t)digit - (int32_t)(uint8_t)'0';
}

/* The exponent after 'e', [+-]digits, held at a size past any
 * float's range so the loops below stay short. */
static int32_t exponent_value(const char *text, uint32_t at,
                              uint32_t end)
{
    const int32_t beyond_any_float = 400;
    const bool negative = (at < end) && (text[at] == '-');
    uint32_t next =
        at + (((at < end) && in_set(text[at], "+-")) ? 1U : 0U);
    int32_t exponent = 0;

    for (; next < end; next++) {
        exponent = (exponent * DECIMAL_BASE) + digit_value(text[next]);
        exponent =
            (exponent > beyond_any_float) ? beyond_any_float : exponent;
    }
    return negative ? -exponent : exponent;
}

/* A number as plain arithmetic in the board's float width. */
static bento_real_t real_value(const char *text,
                               const json_token_t *token)
{
    const bento_real_t ten = (bento_real_t)10.0;
    bento_real_t value = (bento_real_t)0.0;
    int32_t exponent = 0;
    bool fraction = false;
    uint32_t at = token->start;

    for (; (at < token->end) && !in_set(text[at], "eE"); at++) {
        if (text[at] == '.') {
            fraction = true;
        } else if (is_digit(text[at])) {
            value = (value * ten) + (bento_real_t)digit_value(text[at]);
            exponent -= fraction ? 1 : 0;
        } else {
            /* the sign, applied at the end */
        }
    }
    if (at < token->end) {
        exponent += exponent_value(text, at + 1U, token->end);
    }
    for (; exponent > 0; exponent--) {
        value = value * ten;
    }
    for (; exponent < 0; exponent++) {
        value = value / ten;
    }
    return (text[token->start] == '-') ? -value : value;
}

/* An int, held just past every range checked here: MicroPython's
 * int never overflows, and the checks only need the side it is on. */
static int32_t integer_value(const char *text,
                             const json_token_t *token)
{
    const int32_t beyond = INTEGER_LIMIT;
    int32_t value = 0;
    uint32_t at;

    for (at = token->start; at < token->end; at++) {
        if (is_digit(text[at])) {
            value = (value * DECIMAL_BASE) + digit_value(text[at]);
            value = (value > beyond) ? beyond : value;
        }
    }
    return (text[token->start] == '-') ? -value : value;
}

/* An int as str() prints it: no leading zeros, no "-0". */
static void integer_text(const char *text, const json_token_t *token,
                         char *out, uint32_t cap)
{
    const bool negative = text[token->start] == '-';
    uint32_t at = token->start + (negative ? 1U : 0U);
    uint32_t used = 0U;

    while (((at + 1U) < token->end) && (text[at] == '0')) {
        at++;
    }
    if (negative && (text[at] != '0')) {
        out[used] = '-';
        used++;
    }
    while ((at < token->end) && ((used + 1U) < cap)) {
        out[used] = text[at];
        used++;
        at++;
    }
    out[used] = '\0';
}

/* str() of the "text" value; its length in bytes. */
static uint32_t words_text(const char *text, const json_token_t *token,
                           char *out, uint32_t cap)
{
    static const char *const words[] = {"",     "",      "",    "",
                                        "True", "False", "None"};
    uint32_t length = 0U;

    if (token->kind == JSON_STRING) {
        length = decode_string(text, token, out, cap);
    } else if (token->kind == JSON_INTEGER) {
        integer_text(text, token, out, cap);
    } else if (token->kind == JSON_REAL) {
        if (bento_fmt_real(out, cap, real_value(text, token),
                           &length) != BENTO_OK) {
            out[0] = '\0'; /* cap is BENTO_FMT_BUF_SIZE or more */
        }
    } else if ((token->kind == JSON_OBJECT) ||
               (token->kind == JSON_ARRAY)) {
        (void)snprintf(out, (size_t)cap, "%.*s",
                       (int)token->end - (int)token->start,
                       &text[token->start]);
    } else {
        (void)snprintf(out, (size_t)cap, "%s", words[token->kind]);
    }
    return (token->kind == JSON_STRING) ? length : text_length(out);
}

/* A Thai letter's code point from its three bytes; 0 for any other
 * width, which the screen does not draw anyway. */
static uint32_t code_of(const char *text, uint32_t at, uint32_t width)
{
    uint32_t code = 0U;

    if (width == (uint32_t)UTF8_THREE) {
        code = (((uint32_t)(uint8_t)text[at] & (uint32_t)UTF8_FOUR_BITS)
                << ((uint32_t)UTF8_SHIFT * 2U)) |
               (((uint32_t)(uint8_t)text[at + 1U] &
                 (uint32_t)UTF8_SIX_BITS)
                << (uint32_t)UTF8_SHIFT);
    }
    return code;
}

/* What the screen draws: printable ASCII and the Thai block. */
static bool screen_draws(uint32_t code)
{
    const uint32_t first_printable = 0x20U;
    const uint32_t last_printable = 0x7EU;
    const uint32_t block_mask = 0xFF80U;
    const uint32_t thai_block = 0x0E00U;

    return ((code >= first_printable) && (code <= last_printable)) ||
           ((code & block_mask) == thai_block);
}

/* Keeps ASCII and Thai, the characters the screen draws, '?' for
 * the rest, at most `limit` characters: one emoji would turn the
 * whole line into empty boxes. On the PSoC Edge a character is a
 * byte, so each byte of a Thai letter becomes '?' there too. */
static void screen_safe(const char *text, uint32_t length,
                        int32_t limit, char *out)
{
    uint32_t at = 0U;
    uint32_t used = 0U;
    int32_t count = 0;

    while ((at < length) && (count < limit)) {
        const uint8_t lead = (uint8_t)text[at];
        const bool wide =
            unicode_text() && (lead >= (uint32_t)UTF8_LEAD_TWO);
        const uint32_t width = wide ? char_width(lead) : 1U;
        const uint32_t code = wide ? code_of(text, at, width) : lead;

        if (screen_draws(code)) {
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

/* ---- commands from the web page ---------------------------------- */

/* Python's truth of the "on" value; absent means on. */
static bool is_true(const char *text, const command_t *command)
{
    const json_kind_t kind = command->on.kind;
    bool truth = (kind == JSON_ABSENT) || (kind == JSON_TRUE);

    if (kind == JSON_STRING) {
        truth = command->on.end > command->on.start;
    } else if (kind == JSON_INTEGER) {
        truth = integer_value(text, &command->on) != 0;
    } else if (kind == JSON_REAL) {
        truth = real_value(text, &command->on) != (bento_real_t)0.0;
    } else if ((kind == JSON_OBJECT) || (kind == JSON_ARRAY)) {
        truth = command->on_items > 0U;
    } else {
        /* absent, true, false and null were settled above */
    }
    return truth;
}

/* isinstance(v, int): an int, or true/false (a bool is an int). */
static bool int_value(const char *text, const json_token_t *token,
                      int32_t *value)
{
    const bool is_int = (token->kind == JSON_INTEGER) ||
                        (token->kind == JSON_TRUE) ||
                        (token->kind == JSON_FALSE);

    *value = (token->kind == JSON_INTEGER) ? integer_value(text, token)
             : (token->kind == JSON_TRUE)  ? 1
                                           : 0;
    return is_int;
}

static void set_color(const race_screen_t *screen, bento_ui_id_t widget,
                      uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, widget, color);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(const race_screen_t *screen, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(screen->ui, widget, text,
                            text_length(text));
}

/* Color first, then text, the order the original sets them in. */
static void show(const race_screen_t *screen, bento_ui_id_t label,
                 uint32_t color, const char *text)
{
    set_color(screen, label, color);
    set_text(screen, label, text);
}

static void show_icon(const race_screen_t *screen, const char *name,
                      uint32_t color)
{
    (void)bento_ui_image_icon(screen->ui, screen->icon, name,
                              text_length(name));
    set_color(screen, screen->icon, color);
}

static void show_round(const race_screen_t *screen, const race_t *race)
{
    char name[NUMBER_BYTES];
    char best[NUMBER_BYTES];
    char text[TEXT_BYTES];

    round_name(race->round, name, sizeof(name));
    best_text(race, best, sizeof(best));
    (void)snprintf(text, sizeof(text), "รอบ %s | ดีที่สุด %s ms", name,
                   best);
    set_text(screen, screen->round, text);
}

/* ui.tone(): fire-and-forget in MicroPython, but missing from a
 * build without sound, where calling it raises AttributeError. */
static bento_status_t tone(const race_screen_t *screen, uint8_t note,
                           uint8_t velocity, uint16_t duration_ms)
{
    bento_status_t status = BENTO_E_UNSUPPORTED;

    if (screen->has_sound) {
        (void)bento_ui_sfx_tone(screen->ui, note, BENTO_UI_WAVE_SQUARE,
                                velocity, duration_ms);
        status = BENTO_OK;
    }
    return status;
}

/* Starts round `round`: this board's own random wait, then the part
 * where pressing is not allowed. The random part is the microsecond
 * clock when the message arrives, which differs per board; random
 * would give every board the same sequence (no seed here). */
static bento_status_t arm(const race_screen_t *screen, race_t *race,
                          int32_t round)
{
    char name[NAME_BYTES];
    char text[TEXT_BYTES];
    bento_status_t status;

    race->round = round;
    race->arm_ms = bento_time_ticks_ms();
    race->wait_ms =
        WAIT_MIN_MS + (int32_t)(bento_time_ticks_us() % WAIT_SPAN_MS);
    race->phase = PHASE_WAIT;
    status = lights(screen, false);
    if (status == BENTO_OK) {
        show(screen, screen->big, COLOR_WARN, "เตรียม... อย่าเพิ่งกด");
        show_icon(screen, "flag", COLOR_WARN);
        set_text(screen, screen->seg, "-");
        set_color(screen, screen->seg, COLOR_DIM);
        show_round(screen, race);
        /* a short low note: the round has started */
        status = tone(screen, START_NOTE, START_VELOCITY, START_MS);
    }
    if (status == BENTO_OK) {
        round_name(round, name, sizeof(name));
        (void)snprintf(text, sizeof(text), "รอบ %s สุ่มรอ %" PRId32 " ms",
                       name, race->wait_ms);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* The round number comes from someone else: a whole number from 1
 * to 9999, and a repeat of the last start does not restart. */
static bento_status_t do_race(const race_screen_t *screen, race_t *race,
                              const char *payload,
                              const command_t *command)
{
    int32_t round = 0;
    bento_status_t status = BENTO_OK;

    if (!int_value(payload, &command->round, &round) || (round < 1) ||
        (round > ROUND_MAX)) {
        race->bad_count++;
    } else if (round != race->last_round) {
        race->last_round = round;
        status = arm(screen, race, round);
    } else {
        /* the teacher sent the start twice in case one was lost */
    }
    return status;
}

/* A sound during a round could fake the start for other teams. */
static bento_status_t do_beep(const race_screen_t *screen, race_t *race)
{
    bento_status_t status = BENTO_OK;

    if (race->phase != PHASE_IDLE) {
        race->bad_count++;
    } else {
        status = tone(screen, BEEP_NOTE, BEEP_VELOCITY, BEEP_MS);
    }
    return status;
}

/* 24 characters (a label carries 126 bytes, Thai is 3 a letter). */
static void do_say(const race_screen_t *screen, const char *payload,
                   const command_t *command)
{
    char words[PAYLOAD_BYTES];
    char said[SAY_BYTES];
    const uint32_t length =
        words_text(payload, &command->words, words, sizeof(words));

    screen_safe(words, length, SAY_CHARS, said);
    show(screen, screen->note, COLOR_ACCENT, said);
}

/* Nobody switches a light during a round: it could fool a team. */
static bento_status_t do_led(const race_screen_t *screen, race_t *race,
                             const char *payload,
                             const command_t *command)
{
    int32_t led = 0;
    const bool is_int = (command->led.kind == JSON_ABSENT) ||
                        int_value(payload, &command->led, &led);
    bento_status_t status = BENTO_OK;

    if ((race->phase != PHASE_IDLE) || !is_int || (led < 0) ||
        (led >= (int32_t)screen->led_count)) {
        race->bad_count++;
    } else if (is_true(payload, command)) {
        status = bento_gpio_led_on((uint32_t)led);
    } else {
        status = bento_gpio_led_off((uint32_t)led);
    }
    return status;
}

/* One command. The sender can always type garbage; it must never
 * end the loop. */
static bento_status_t handle(const race_screen_t *screen, race_t *race,
                             const char *payload, uint32_t length)
{
    command_t command = {0};
    const bool understood = decodes(payload, length) &&
                            read_command(payload, length, &command) &&
                            command.is_object;
    const json_token_t *action = &command.action;
    bento_status_t status = BENTO_OK;

    /* not JSON, no .get(), or a command nobody knows: counted bad */
    if (understood && string_equals(payload, action, "race")) {
        status = do_race(screen, race, payload, &command);
    } else if (understood && string_equals(payload, action, "beep")) {
        status = do_beep(screen, race);
    } else if (understood && string_equals(payload, action, "say")) {
        do_say(screen, payload, &command);
    } else if (understood && string_equals(payload, action, "led")) {
        status = do_led(screen, race, payload, &command);
    } else {
        race->bad_count++;
    }
    return status;
}

static void show_counts(const race_screen_t *screen, const race_t *race)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "กด %" PRId32 " | ส่ง %" PRId32 " | ส่งไม่ผ่าน %" PRId32
                   " | คำสั่งเสีย %" PRId32,
                   race->presses, race->message_count, race->fail_count,
                   race->bad_count);
    set_text(screen, screen->counts, text);
}

/* The binding's inbox has one slot, a new message over the old, so
 * only the newest message waiting is handled. */
static bento_status_t read_inbox(const race_screen_t *screen,
                                 race_t *race)
{
    char topic[TOPIC_BYTES];
    char payload[PAYLOAD_BYTES];
    char newest[PAYLOAD_BYTES];
    bento_mqtt_msg_t message;
    bool has_message = false;
    uint32_t length = 0U;
    bento_status_t status = BENTO_OK;

    bento_mqtt_msg_init(&message);
    while (bento_mqtt_poll(race->client, &message, topic, sizeof(topic),
                           (uint8_t *)payload,
                           PAYLOAD_BYTES - 1U) == BENTO_OK) {
        length = message.payload_len;
        (void)memcpy(newest, payload, (size_t)length);
        newest[length] = '\0';
        has_message = true;
    }
    if (has_message) {
        status = handle(screen, race, newest, length);
        show_counts(screen, race);
    }
    return status;
}

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    INNER_X = 40,
    TITLE_Y = 8,
    HEADLINE_X = 344, /* the title at size 24 ends just before */
    HEADLINE_Y = 16,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 120,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    CAPTION_Y = 70,
    VALUE_Y = 104,
    SEG_W = 144, /* four digits: up to 3000 ms */
    SEG_H = 56,
    SIDE_X = 224,
    COUNTS_Y = 138,
    BIG_Y = 200,
    ICON_X = 640,
    ICON_Y = 192,
    ICON_SIZE = 64,
    TOPIC_Y = 272,
    NOTE_Y = 312,
    HINT_Y = 348,
    TITLE_PX = 24,
    BIG_PX = 28,
    TEXT_PX = 20
};

/* Where a label goes and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} look_t;

/* ---- the screen -------------------------------------------------- */

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                look_t look, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = text_length(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = look.font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_CARD; /* the border colour */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &card);
}

static bento_status_t add_seg(race_screen_t *screen)
{
    static const char dash[] = "-";
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = dash;
    args.text_len = text_length(dash);
    args.x = INNER_X;
    args.y = VALUE_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = COLOR_DIM;
    return bento_ui_seg7_create(screen->ui, &args, &screen->seg);
}

/* The top card: the reaction time, the round and the counts. */
static bento_status_t add_numbers(race_screen_t *screen)
{
    const look_t time_caption = {INNER_X, CAPTION_Y, COLOR_DIM,
                                 TEXT_PX};
    const look_t side_caption = {SIDE_X, CAPTION_Y, COLOR_DIM, TEXT_PX};
    const look_t round = {SIDE_X, VALUE_Y, COLOR_TEXT, TEXT_PX};
    const look_t counts = {SIDE_X, COUNTS_Y, COLOR_DIM, TEXT_PX};
    bento_ui_id_t unused;
    bento_status_t status = add_card(screen->ui);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "เวลาตอบสนอง (ms)", time_caption,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg(screen);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "รอบ | ดีที่สุด", side_caption, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "ยังไม่มีรอบ", round, &screen->round);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "กด 0 | ส่ง 0 | ส่งไม่ผ่าน 0 | "
                           "คำสั่งเสีย 0",
                           counts, &screen->counts);
    }
    return status;
}

/* The big label is the game's signal: players watch it and the
 * LEDs, not the numbers. */
static bento_status_t add_signal(race_screen_t *screen)
{
    static const char smiley[] = "smiley"; /* an Image takes an icon */
    const look_t big = {MARGIN_X, BIG_Y, COLOR_DIM, BIG_PX};
    bento_ui_widget_args_t args;
    bento_status_t status =
        add_label(screen->ui, "รอผู้สอนเริ่มรอบ", big, &screen->big);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = smiley;
        args.text_len = text_length(smiley);
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

static bento_status_t add_footer(race_screen_t *screen,
                                 const char *button_name)
{
    const look_t topic = {MARGIN_X, TOPIC_Y, COLOR_DIM, TEXT_PX};
    const look_t note = {MARGIN_X, NOTE_Y, COLOR_DIM, TEXT_PX};
    const look_t hint = {MARGIN_X, HINT_Y, COLOR_DIM, TEXT_PX};
    char text[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "ยังไม่ได้ต่อ broker", topic, &screen->topic);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กดปุ่มตอนว่าง = รอบซ้อม", note,
                           &screen->note);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "ไฟติดแล้วค่อยกด %s",
                       button_name);
        status = add_label(screen->ui, text, hint, &unused);
    }
    return status;
}

static bento_status_t build_screen(race_screen_t *screen,
                                   const char *button_name)
{
    const look_t title = {MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX};
    const look_t headline = {HEADLINE_X, HEADLINE_Y, COLOR_DIM,
                             TEXT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "เกมกดเร็วทั้งห้อง", title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังจะเริ่ม", headline,
                           &screen->headline);
    }
    if (status == BENTO_OK) {
        status = add_numbers(screen);
    }
    if (status == BENTO_OK) {
        status = add_signal(screen);
    }
    if (status == BENTO_OK) {
        status = add_footer(screen, button_name);
    }
    return status;
}

/* ui.poll() whose events are not wanted: they are dropped. */
static void drop_events(const race_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* a failed poll answers no events in MicroPython too */
    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
}

/* Ends politely: the screen says where it stopped, nothing hangs. */
static void stop_here(race_screen_t *screen, const char *screen_text,
                      const char *log_text)
{
    char text[TEXT_BYTES];

    show(screen, screen->headline, COLOR_BAD, screen_text);
    show(screen, screen->note, COLOR_BAD, log_text);
    drop_events(screen);
    (void)snprintf(text, sizeof(text), "<span class=error>%s</span>",
                   log_text);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
    (void)printf("หยุดที่: %s\n", log_text);
    screen->stopped = true;
}

static void announce_ready(const race_screen_t *screen)
{
    char text[TOPICS_LINE_BYTES];

    show(screen, screen->headline, COLOR_OK, "ต่อแล้ว รอผู้สอนเริ่มรอบ");
    (void)snprintf(text, sizeof(text), "ฟัง %s กับ %s",
                   screen->names.topic_cmd, screen->names.topic_all);
    show(screen, screen->topic, COLOR_TEXT, text);
    drop_events(screen);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>3) ต่อแล้ว ฟัง %s และ %s </span>",
                   screen->names.topic_cmd, screen->names.topic_all);
    (void)bento_lcd_print(text);
}

/* The lights come on; the clock starts after them, not before. */
static bento_status_t go(const race_screen_t *screen, race_t *race)
{
    bento_status_t status = lights(screen, true);

    if (status == BENTO_OK) {
        race->go_ms = bento_time_ticks_ms();
        race->phase = PHASE_GO;
        show(screen, screen->big, COLOR_OK, "กดเลย");
        show_icon(screen, "check", COLOR_OK);
        status = tone(screen, GO_NOTE, GO_VELOCITY, GO_MS);
    }
    return status;
}

/* The round's result on screen. */
static void show_result(const race_screen_t *screen, race_t *race,
                        const result_t *result)
{
    char text[TEXT_BYTES];

    if (result->end == END_FALSE_START) {
        show(screen, screen->big, COLOR_BAD, "ออกตัวก่อนไฟติด");
        show_icon(screen, "cross", COLOR_BAD);
        /* a dash as when idle: seven segments draw no capital */
        set_text(screen, screen->seg, "-");
        set_color(screen, screen->seg, COLOR_BAD);
    } else if (result->end == END_MISS) {
        show(screen, screen->big, COLOR_WARN, "ช้าเกิน 3 วินาที");
        show_icon(screen, "cross", COLOR_WARN);
        set_text(screen, screen->seg, "-");
        set_color(screen, screen->seg, COLOR_DIM);
    } else {
        race->has_last = true;
        race->last_ms = result->ms;
        if ((race->round != 0) &&
            (!race->has_best || (result->ms < race->best_ms))) {
            race->has_best = true;
            race->best_ms = result->ms;
        }
        (void)snprintf(text, sizeof(text), "%" PRId32 " ms",
                       result->ms);
        show(screen, screen->big, COLOR_OK, text);
        show_icon(screen, "star", COLOR_OK);
        (void)snprintf(text, sizeof(text), "%" PRId32, result->ms);
        set_text(screen, screen->seg, text);
        set_color(screen, screen->seg, COLOR_OK);
    }
}

static void show_countdown(const race_screen_t *screen,
                           const race_t *race, game_clock_t *clock,
                           uint32_t now_ms)
{
    const int32_t second =
        bento_time_ticks_diff(now_ms, clock->start_ms) / MS_PER_S;
    char text[TEXT_BYTES];

    if ((race->phase == PHASE_IDLE) && (second != clock->shown_s)) {
        clock->shown_s = second;
        (void)snprintf(text, sizeof(text), "เล่นได้อีก %" PRId32 " วินาที",
                       (RUN_MS / MS_PER_S) - second);
        set_text(screen, screen->headline, text);
    }
}

/* The end: lost link or time up, then the day's numbers. */
static void report(const race_screen_t *screen, const race_t *race,
                   uint32_t start_ms)
{
    char best[NUMBER_BYTES];
    char text[TEXT_BYTES];

    if (race->lost) {
        const int32_t lost_s =
            bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) /
            MS_PER_S;

        show(screen, screen->headline, COLOR_BAD, "สายหลุด หยุดเกมแล้ว");
        set_color(screen, screen->note, COLOR_BAD);
        (void)snprintf(text, sizeof(text),
                       "หลุดวินาทีที่ %" PRId32 " กดรันใหม่ได้เลย", lost_s);
        set_text(screen, screen->note, text);
        drop_events(screen);
        (void)bento_lcd_print(
            "<span class=error>สายหลุด ต้องรันใหม่</span>");
    } else {
        show(screen, screen->headline, COLOR_DIM, "หมดเวลาเกมแล้ว");
        drop_events(screen);
        (void)bento_lcd_print("<span class=ok>จบเกม</span>");
    }
    best_text(race, best, sizeof(best));
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>เล่น %" PRId32
                   " รอบ | ดีที่สุด %s ms | กด %" PRId32 " ครั้ง</span>",
                   race->rounds, best, race->presses);
    (void)bento_lcd_print(text);
    (void)printf("เล่น %" PRId32 " รอบ | ดีที่สุด %s ms\n", race->rounds,
                 best);
}

static bool has_sound(const bento_ui_t *ui)
{
    bento_ui_caps_t caps;

    return (bento_ui_get_capabilities(ui, &caps) == BENTO_OK) &&
           ((caps.feature_bits & (uint32_t)BENTO_UI_FEATURE_SFX) != 0U);
}

/* Change these to what the teacher hands out. NETWORK.ssid is the
 * team's phone hotspot (the faculty Wi-Fi needs a login the board
 * cannot do); TEAM is team01 to team19. The program refuses to run
 * until TEAM is set. */
typedef struct {
    const char *ssid;
    const char *pass;
} wifi_network_t;

static const wifi_network_t NETWORK = {"bento-teamXX",
                                       "<รหัส Hotspot ของทีม>"};
static const char BROKER[] = "broker.hivemq.com";
static const char TEAM[] = "teamXX";

/* ---- the reports' keys, in json.dumps order ---------------------- */

typedef enum {
    FIELD_FALSE = 0,
    FIELD_MISS,
    FIELD_MS,
    FIELD_EV,
    FIELD_R,
    FIELD_ID,
    FIELD_N,
    FIELD_PRESSES,
    FIELD_BEST,
    FIELD_LAST,
    FIELD_ROUNDS,
    FIELD_END /* ends a list below */
} field_t;

typedef struct {
    const field_t *false_start;
    const field_t *early;
    const field_t *miss;
    const field_t *time;
    const field_t *state;
} orders_t;

/* ---- before the three steps: the team name must be ours ---------- */

/* The client id comes from TEAM: another team's name makes the
 * broker throw that team's board out, and this firmware does not
 * reconnect by itself. team00 is the teacher's board. */
static bool team_is_valid(void)
{
    bool valid = (strlen(TEAM) == (size_t)TEAM_LENGTH) &&
                 (strncmp(TEAM, "team", (size_t)TEAM_PREFIX) == 0) &&
                 (strcmp(TEAM, "team00") != 0);
    size_t i;

    for (i = (size_t)TEAM_PREFIX; valid && (TEAM[i] != '\0'); i++) {
        valid = (TEAM[i] >= '0') && (TEAM[i] <= '9');
    }
    return valid;
}

/* ---- the three steps of lesson 2: Wi-Fi, IP, broker -------------- */

/* Up to BENTO_WIFI_SCAN_MAX networks: too big for the 8 KB stack,
 * so at file scope and cleared at the start of main (C9). */
extern bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];

/* Waits for the join in slices, so Stop ends it as in MicroPython.
 * *joined is wifi.connect()'s True or False. */
static bento_status_t join_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_status_t join = BENTO_E_IN_PROGRESS;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = NETWORK.ssid;
    params.ssid_len = text_length(NETWORK.ssid);
    params.pass = NETWORK.pass;
    params.pass_len = text_length(NETWORK.pass);
    params.flags = BENTO_WIFI_F_SAVE_CREDS; /* the binding saves too */
    (void)printf("[WiFi] Connecting...\r\n");
    status = bento_wifi_connect_start(&params);
    if (status == BENTO_OK) {
        join = bento_wifi_connect_poll();
    }
    while ((status == BENTO_OK) && (join == BENTO_E_IN_PROGRESS)) {
        status = wait_ms(WAIT_SLICE_MS);
        join = (status == BENTO_OK) ? bento_wifi_connect_poll() : join;
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

/* Did a scan hear our network? A scan that fails says "no": the
 * screen must still tell why, not die with a traceback. */
static bento_status_t heard_network(bool *heard)
{
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t scan = bento_wifi_scan_start();
    bento_status_t status = BENTO_OK;

    if (scan == BENTO_OK) {
        scan = bento_wifi_scan_poll(g_networks, BENTO_WIFI_SCAN_MAX,
                                    &count);
    }
    while ((status == BENTO_OK) && (scan == BENTO_E_IN_PROGRESS)) {
        status = wait_ms(WAIT_SLICE_MS);
        scan = (status == BENTO_OK)
                   ? bento_wifi_scan_poll(g_networks,
                                          BENTO_WIFI_SCAN_MAX, &count)
                   : scan;
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_scan_cancel(); /* Stop wins either way */
    }
    *heard = false;
    for (i = 0U; (scan == BENTO_OK) && (i < count); i++) {
        *heard =
            *heard || (strcmp(g_networks[i].ssid, NETWORK.ssid) == 0);
    }
    return status;
}

/* The join failed: say whether the network was heard at all. These
 * texts go to a label (126 bytes, a Thai letter is 3), so short. */
static bento_status_t explain_no_wifi(race_screen_t *screen)
{
    char text[TEXT_BYTES];
    bool heard = false;
    bento_status_t status = heard_network(&heard);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       heard ? "ได้ยินวง %s แต่ต่อไม่ผ่าน ตรวจรหัสผ่าน"
                             : "ไม่ได้ยินวง %s ตรวจชื่อวง",
                       NETWORK.ssid);
        stop_here(screen, "ต่อ WiFi ไม่ติด", text);
    }
    return status;
}

/* Up to 15 short waits for DHCP to give an address. */
static bento_status_t wait_for_address(race_screen_t *screen, char *ip,
                                       uint32_t cap)
{
    int32_t tries = 0;
    bento_status_t status = BENTO_OK;

    read_ip(ip, cap);
    while ((status == BENTO_OK) && (tries < IP_TRIES) &&
           !has_address(ip)) {
        drop_events(screen);
        status = wait_ms(IP_WAIT_MS);
        read_ip(ip, cap);
        tries++;
    }
    if ((status == BENTO_OK) && !has_address(ip)) {
        stop_here(screen, "ไม่มีเลข IP",
                  "ได้ลิงก์แต่ DHCP ไม่ให้เลข "
                  "IP");
    }
    return status;
}

static bento_status_t step_wifi(race_screen_t *screen, char *ip,
                                uint32_t cap)
{
    char text[TEXT_BYTES];
    bool joined = false;
    bento_status_t status;

    /* the label must show before connect(), which can block ~85 s */
    show(screen, screen->headline, COLOR_WARN,
         "กำลังต่อ WiFi จอจะนิ่งสักครู่");
    drop_events(screen);
    (void)snprintf(text, sizeof(text), "1) กำลังต่อ WiFi %s",
                   NETWORK.ssid);
    (void)bento_lcd_print(text);
    status = join_wifi(&joined);
    if ((status == BENTO_OK) && !joined) {
        status = explain_no_wifi(screen);
    }
    if (go_on(screen, status)) {
        status = wait_for_address(screen, ip, cap);
    }
    return status;
}

/* The lines mqtt.connect() prints around its connect (modmqtt.c);
 * it prints none when Wi-Fi cannot start (its OSError). */
static void print_connect_lines(bento_status_t answer,
                                const char *client_id)
{
    if ((answer != BENTO_E_NOT_READY) &&
        (answer != BENTO_E_UNSUPPORTED)) {
        (void)printf("[MQTT] Connecting to %s:%d ...\r\n", BROKER,
                     BROKER_PORT);
    }
    if (answer == BENTO_OK) {
        (void)printf("[MQTT] Connected to %s:%d as '%s'\r\n", BROKER,
                     BROKER_PORT, client_id);
    }
}

/* Opens the client and waits for the broker's answer in slices.
 * *answer is the open's result once it is no longer in progress. */
static bento_status_t open_broker(const race_screen_t *screen,
                                  race_t *race, bento_status_t *answer)
{
    bento_mqtt_config_t config;
    bento_status_t status = BENTO_OK;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = text_length(BROKER);
    config.port = BROKER_PORT;
    config.client_id = screen->names.device_id;
    config.client_id_len = text_length(screen->names.device_id);
    *answer = bento_mqtt_open_start(&config, &race->client);
    if (*answer == BENTO_OK) {
        *answer = bento_mqtt_open_poll(race->client);
    }
    while ((status == BENTO_OK) && (*answer == BENTO_E_IN_PROGRESS)) {
        status = wait_ms(WAIT_SLICE_MS);
        *answer = (status == BENTO_OK)
                      ? bento_mqtt_open_poll(race->client)
                      : *answer;
    }
    if (status == BENTO_OK) {
        print_connect_lines(*answer, screen->names.device_id);
    }
    return status;
}

/* The BENTO Emulator has no MQTT from C yet: bento_mqtt_* and the
 * broker calls answer BENTO_E_UNSUPPORTED there. The program takes
 * the original's own "broker failed" way out: the step turns red with
 * the original's words, the reason line and the drawer say why, the
 * console gets the sentence in Thai and English, and the program
 * ends normally. The label stays within the 126 bytes of a
 * SET_TEXT. */
static void show_emulator_note(bento_ui_t *ui, bento_ui_id_t step,
                               const char *step_text,
                               bento_ui_id_t reason)
{
    /* the Emulator does not wrap a label by its width: \n does */
    static const char note[] = "MQTT จาก C ยังไม่มีใน BENTO Emulator\n"
                               "ให้รันบน Dev Kit หรือ Eva Kit";
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* the setters ignore a refusal, as in MicroPython (modui.c) */
    (void)bento_ui_set_color(ui, step, COLOR_BAD);
    (void)bento_ui_set_text(ui, step, step_text,
                            (uint32_t)strlen(step_text));
    (void)bento_ui_set_color(ui, reason, COLOR_BAD);
    (void)bento_ui_set_text(ui, reason, note, (uint32_t)strlen(note));
    /* ui.poll() answers no events when it fails; none are read */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
    (void)bento_lcd_print(note); /* ignores a console not ready */
    (void)printf("MQTT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit "
                 "หรือ Eva Kit / MQTT from C is not in the BENTO "
                 "Emulator yet: run it on a Dev Kit or Eva Kit.\n");
}

/* What the original's except OSError and "if not linked" catch. */
static void explain_no_broker(race_screen_t *screen,
                              bento_status_t answer)
{
    char text[TEXT_BYTES];

    if (answer == BENTO_E_UNSUPPORTED) {
        show_emulator_note(screen->ui, screen->headline, "broker ไม่ตอบ",
                           screen->note);
        screen->stopped = true;
    } else if (answer == BENTO_E_NOT_READY) {
        (void)snprintf(text, sizeof(text), "ต่อ %s ไม่ได้ ลองตัวสำรอง",
                       BROKER);
        stop_here(screen, "broker ไม่ตอบ", text);
    } else {
        stop_here(screen, "broker ปฏิเสธ",
                  "ตรวจชื่อ broker กับพอร์ต "
                  "1883");
    }
}

/* One topic to listen to; a refusal stops the run politely. */
static void listen_to(race_screen_t *screen, const race_t *race,
                      const char *topic)
{
    char text[TEXT_BYTES];

    if (!screen->stopped && !subscribe(race->client, topic)) {
        (void)snprintf(text, sizeof(text), "broker ไม่ยอมให้ฟัง %s",
                       topic);
        stop_here(screen, "subscribe ไม่ผ่าน", text);
    }
}

static bento_status_t step_broker(race_screen_t *screen, race_t *race,
                                  const char *ip)
{
    char text[TEXT_BYTES];
    bento_status_t answer = BENTO_OK;
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "IP %s กำลังต่อ broker", ip);
    set_text(screen, screen->headline, text);
    drop_events(screen);
    (void)snprintf(text, sizeof(text), "2) ได้ IP %s กำลังต่อ %s", ip,
                   BROKER);
    (void)bento_lcd_print(text);
    status = open_broker(screen, race, &answer);
    if ((status == BENTO_OK) && (answer != BENTO_OK)) {
        explain_no_broker(screen, answer);
    }
    /* our team's topic (the team's web page) and the whole class's
     * (the teacher starts rounds); both fill the same inbox */
    if (go_on(screen, status)) {
        listen_to(screen, race, screen->names.topic_cmd);
        listen_to(screen, race, screen->names.topic_all);
    }
    return status;
}

/* ---- the reports ------------------------------------------------- */

/* One key's value as json.dumps writes it. */
static void write_field(field_t field, const race_t *race,
                        const result_t *result, char *out, uint32_t cap)
{
    const int32_t numbers[FIELD_END] = {1,
                                        1,
                                        result->ms,
                                        0,
                                        race->round,
                                        0,
                                        race->message_count,
                                        race->presses,
                                        race->best_ms,
                                        race->last_ms,
                                        race->rounds};
    const bool is_null = ((field == FIELD_BEST) && !race->has_best) ||
                         ((field == FIELD_LAST) && !race->has_last);

    if (field == FIELD_EV) {
        (void)snprintf(out, (size_t)cap, "\"race\"");
    } else if (field == FIELD_ID) {
        (void)snprintf(out, (size_t)cap, "\"%s\"", TEAM);
    } else if (is_null) {
        (void)snprintf(out, (size_t)cap, "null");
    } else {
        (void)snprintf(out, (size_t)cap, "%" PRId32, numbers[field]);
    }
}

/* json.dumps of a dict whose keys come in `order`. */
static void write_json(const field_t *order, const race_t *race,
                       const result_t *result, char *json, uint32_t cap)
{
    static const char *const field_names[FIELD_END] = {
        "false", "miss",    "ms",   "ev",   "r",     "id",
        "n",     "presses", "best", "last", "rounds"};
    char value[NAME_BYTES];
    uint32_t used = 0U;
    uint32_t i;

    json[0] = '\0';
    for (i = 0U; (order[i] != FIELD_END) && (used < cap); i++) {
        int written;

        write_field(order[i], race, result, value, sizeof(value));
        written = snprintf(&json[used], cap - used, "%s\"%s\": %s",
                           (i == 0U) ? "{" : ", ",
                           field_names[order[i]], value);
        used += (written > 0) ? (uint32_t)written : 0U;
    }
    if (used < cap) {
        (void)snprintf(&json[used], cap - used, "}");
    }
}

/* One message out. false only when the link is gone (the binding's
 * OSError); a refused message is lost, counted on screen, and the
 * game goes on: the team must see why its result is missing. */
static bool send_json(const race_screen_t *screen, race_t *race,
                      const char *topic, const field_t *order,
                      const result_t *result)
{
    char json[JSON_BYTES];
    uint32_t state = BENTO_MQTT_STATE_CLOSED;
    bool linked;

    race->message_count++;
    linked = (bento_mqtt_state(race->client, &state) == BENTO_OK) &&
             (state == BENTO_MQTT_STATE_CONNECTED);
    write_json(order, race, result, json, sizeof(json));
    if (linked &&
        (bento_mqtt_publish(race->client, topic, text_length(topic),
                            (const uint8_t *)json, text_length(json),
                            0U, 0U) != BENTO_OK)) {
        race->fail_count++;
        show_counts(screen, race);
    }
    return linked;
}

/* The key orders of this board's MicroPython (header). */
static const orders_t *board_orders(void)
{
    /* A round's dict starts with one or two keys, then gets ev, r, id
     * and n; the state dict starts with four and gets id and n. */
    static const field_t false_order_reterminal[] = {
        FIELD_EV, FIELD_FALSE, FIELD_R, FIELD_ID, FIELD_N, FIELD_END};
    static const field_t early_order_reterminal[] = {
        FIELD_EV, FIELD_N,  FIELD_FALSE, FIELD_R,
        FIELD_MS, FIELD_ID, FIELD_END};
    static const field_t miss_order_reterminal[] = {
        FIELD_EV, FIELD_N, FIELD_R, FIELD_ID, FIELD_MISS, FIELD_END};
    static const field_t time_order_reterminal[] = {
        FIELD_EV, FIELD_N, FIELD_R, FIELD_ID, FIELD_MS, FIELD_END};
    static const field_t state_order_reterminal[] = {
        FIELD_PRESSES, FIELD_ID,   FIELD_N,  FIELD_BEST,
        FIELD_ROUNDS,  FIELD_LAST, FIELD_END};

    static const field_t false_order_psoc[] = {
        FIELD_FALSE, FIELD_N, FIELD_EV, FIELD_ID, FIELD_R, FIELD_END};
    static const field_t early_order_psoc[] = {
        FIELD_FALSE, FIELD_MS, FIELD_EV, FIELD_N,
        FIELD_ID,    FIELD_R,  FIELD_END};
    static const field_t miss_order_psoc[] = {
        FIELD_N, FIELD_EV, FIELD_MISS, FIELD_ID, FIELD_R, FIELD_END};
    static const field_t time_order_psoc[] = {
        FIELD_N, FIELD_MS, FIELD_EV, FIELD_ID, FIELD_R, FIELD_END};
    static const field_t state_order_psoc[] = {
        FIELD_ROUNDS, FIELD_BEST, FIELD_PRESSES, FIELD_N,
        FIELD_ID,     FIELD_LAST, FIELD_END};

    /* the report key orders of one board's MicroPython */
    static const orders_t orders_reterminal = {
        false_order_reterminal, early_order_reterminal,
        miss_order_reterminal, time_order_reterminal,
        state_order_reterminal};
    static const orders_t orders_psoc = {
        false_order_psoc, early_order_psoc, miss_order_psoc,
        time_order_psoc, state_order_psoc};

    return on_reterminal() ? &orders_reterminal : &orders_psoc;
}

static const field_t *event_order(const result_t *result)
{
    const orders_t *orders = board_orders();
    const field_t *order = orders->time;

    if (result->end == END_MISS) {
        order = orders->miss;
    } else if (result->end == END_FALSE_START) {
        order = result->has_ms ? orders->early : orders->false_start;
    } else {
        /* a timed round */
    }
    return order;
}

/* Ends the round: the screen, the console, and the broker for a
 * real round. false when the link is gone. */
static bento_status_t finish(const race_screen_t *screen, race_t *race,
                             const result_t *result)
{
    char name[NAME_BYTES];
    char shown[NAME_BYTES];
    char text[TEXT_BYTES];
    bento_status_t status;

    race->phase = PHASE_IDLE;
    status = lights(screen, false);
    if (status == BENTO_OK) {
        show_result(screen, race, result);
        show_round(screen, race);
        round_name(race->round, name, sizeof(name));
        result_repr(result, shown, sizeof(shown));
        (void)snprintf(text, sizeof(text), "รอบ %s -> %s", name, shown);
        (void)bento_lcd_print(text);
    }
    /* a practice round stays on our desk, off the class's table */
    if ((status == BENTO_OK) && (race->round != 0)) {
        race->rounds++;
        race->lost = !send_json(screen, race, screen->names.topic_event,
                                event_order(result), result);
    }
    return status;
}

/* The round's part after this loop pass's button reading; pressed
 * says a press was confirmed in this pass, from pressed_ms. */
static bento_status_t run_phase(const race_screen_t *screen,
                                race_t *race, bool pressed,
                                uint32_t pressed_ms, uint32_t now_ms)
{
    result_t result = {END_FALSE_START, false, 0};
    bento_status_t status = BENTO_OK;

    if ((race->phase == PHASE_WAIT) && pressed) {
        status = finish(screen, race, &result);
    } else if (race->phase == PHASE_WAIT) {
        if (bento_time_ticks_diff(now_ms, race->arm_ms) >=
            race->wait_ms) {
            status = go(screen, race);
        }
    } else if ((race->phase == PHASE_GO) && pressed) {
        result.ms = bento_time_ticks_diff(pressed_ms, race->go_ms);
        result.has_ms = true;
        /* faster than a human: the press was a guess */
        result.end =
            (result.ms < HUMAN_MIN_MS) ? END_FALSE_START : END_TIME;
        status = finish(screen, race, &result);
    } else if (race->phase == PHASE_GO) {
        if (bento_time_ticks_diff(now_ms, race->go_ms) >=
            GO_TIMEOUT_MS) {
            result.end = END_MISS;
            status = finish(screen, race, &result);
        }
    } else if (pressed) {
        status = arm(screen, race, 0); /* idle press: practice */
    } else {
        /* idle, nothing pressed */
    }
    return status;
}

/* Telemetry only while idle: a publish takes network time, and in a
 * round it would slow the button reading and skew the time. */
static void send_state(const race_screen_t *screen, race_t *race,
                       game_clock_t *clock, uint32_t now_ms)
{
    const result_t none = {END_TIME, false, 0};

    if ((race->phase == PHASE_IDLE) &&
        (bento_time_ticks_diff(now_ms, clock->state_ms) >= STATE_MS)) {
        clock->state_ms = now_ms;
        race->lost = !send_json(screen, race, screen->names.topic_state,
                                board_orders()->state, &none);
        if (!race->lost) {
            show_counts(screen, race);
        }
    }
}

/* One pass: the button, the round, the inbox, then telemetry. A lost
 * link ends the pass at once, as the original's break. */
static bento_status_t run_pass(const race_screen_t *screen,
                               race_t *race, button_t *button,
                               game_clock_t *clock)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    uint32_t pressed_ms = now_ms;
    bool pressed = false;
    bento_status_t status =
        read_button(button, now_ms, &pressed, &pressed_ms);

    if (pressed) {
        race->presses++;
        show_counts(screen, race);
    }
    if (status == BENTO_OK) {
        status = run_phase(screen, race, pressed, pressed_ms, now_ms);
    }
    if ((status == BENTO_OK) && !race->lost) {
        status = read_inbox(screen, race);
    }
    if ((status == BENTO_OK) && !race->lost) {
        send_state(screen, race, clock, now_ms);
    }
    race->lost = race->lost || !still_linked(race);
    if ((status == BENTO_OK) && !race->lost) {
        show_countdown(screen, race, clock, now_ms);
        drop_events(screen);
        status = wait_ms(POLL_MS);
    }
    return status;
}

/* 45 minutes of passes, or until the link is lost. */
static bento_status_t run_game(const race_screen_t *screen,
                               race_t *race)
{
    button_t button = {0};
    game_clock_t clock = {0};
    bento_status_t status =
        bento_gpio_button_is_pressed(0U, &button.stable);

    button.last_raw = button.stable;
    clock.start_ms = bento_time_ticks_ms();
    button.last_change_ms = clock.start_ms;
    clock.state_ms = bento_time_ticks_add(clock.start_ms, -STATE_MS);
    clock.shown_s = -1;
    while ((status == BENTO_OK) && !race->lost &&
           (bento_time_ticks_diff(bento_time_ticks_ms(),
                                  clock.start_ms) < RUN_MS)) {
        status = run_pass(screen, race, &button, &clock);
    }
    if (status == BENTO_OK) {
        status = lights(screen, false);
    }
    if (status == BENTO_OK) {
        report(screen, race, clock.start_ms);
    }
    return status;
}

static void build_names(names_t *names)
{
    /* leave it: the whole class uses the same root, so the web page
     * finds every board */
    static const char root[] = "bento-aiot";

    (void)snprintf(names->device_id, sizeof(names->device_id),
                   "bento-aiot-%s", TEAM);
    (void)snprintf(names->topic_cmd, sizeof(names->topic_cmd),
                   "%s/%s/cmd", root, TEAM);
    (void)snprintf(names->topic_all, sizeof(names->topic_all),
                   "%s/all/cmd", root);
    (void)snprintf(names->topic_event, sizeof(names->topic_event),
                   "%s/%s/event", root, TEAM);
    (void)snprintf(names->topic_state, sizeof(names->topic_state),
                   "%s/%s/telemetry", root, TEAM);
}

/* The team check, the three steps, then the game. */
static bento_status_t run_steps(race_screen_t *screen, race_t *race)
{
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_status_t status = BENTO_OK;

    if (!team_is_valid()) {
        stop_here(screen, "ยังไม่ได้ตั้งชื่อทีม",
                  "แก้ TEAM เป็นเลขทีมของคุณก่อน เช่น team03");
    }
    if (go_on(screen, status)) {
        status = step_wifi(screen, ip, sizeof(ip));
    }
    if (go_on(screen, status)) {
        status = step_broker(screen, race, ip);
    }
    if (go_on(screen, status)) {
        announce_ready(screen);
        status = run_game(screen, race);
    }
    return status;
}

/* Every LED off before the screen is drawn; gpio.button(0) refuses
 * a board without a button. */
static bento_status_t prepare_board(race_screen_t *screen,
                                    const char **button_name)
{
    bento_status_t status = bento_gpio_button_name(0U, button_name);

    screen->led_count = bento_gpio_num_leds();
    if (status == BENTO_OK) {
        status = lights(screen, false);
    }
    return status;
}

int main(void)
{
    race_screen_t screen = {0};
    race_t race = {0};
    const char *button_name = "";
    bento_status_t status;

    (void)memset(g_networks, 0, sizeof(g_networks));
    race.last_round = -1;
    screen.ui = bento_runtime_get_ui();
    screen.has_sound = has_sound(screen.ui);
    build_names(&screen.names);
    status = prepare_board(&screen, &button_name);
    if (status == BENTO_OK) {
        status = build_screen(&screen, button_name);
    }
    if (status == BENTO_OK) {
        drop_events(&screen);
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>เกมกดเร็วทั้งห้อง</h2>");
        status = run_steps(&screen, &race);
    }
    if (race.client != 0U) {
        (void)bento_mqtt_close(race.client); /* nothing left to tell */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
