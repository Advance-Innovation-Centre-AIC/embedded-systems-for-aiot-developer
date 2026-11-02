/*
 * 07_button_to_broker.c -- ปุ่มกับไฟขึ้น broker
 * (Button and lights to the broker)
 *
 * ต่อ WiFi กับ broker สาธารณะ แล้วกดปุ่ม USER BTN1 บนบอร์ด ทุกครั้งที่
 * กด (กันเด้ง 40 ms) ส่ง event ทันที ทุก 2 วินาทีส่ง telemetry (จำนวน
 * กด สถานะปุ่ม leds และแกน z ของ BMI270) และรับคำสั่งจากหน้าเว็บ
 * (led / beep / say) มาจุดไฟ ส่งเสียง หรือขึ้นข้อความ ทำงาน 3 นาที
 * แล้วดับไฟทุกดวง ต้องแก้ WIFI_SSID WIFI_PASS และ TEAM ก่อนรัน
 * broker สาธารณะ พอร์ต 1883 ไม่เข้ารหัส ห้ามส่งของลับ
 *
 * Joins the Wi-Fi and a public broker, then watches USER BTN1. Each
 * press (debounced 40 ms) publishes an event at once; every 2 s a
 * telemetry message carries the presses, the button, the leds list
 * and the BMI270's z axis. Commands from the web page (led / beep /
 * say) light an LED, beep or show a text. It runs 3 minutes, then
 * turns every LED off. Set WIFI_SSID, WIFI_PASS and TEAM first. The
 * broker is public, port 1883, unencrypted: send nothing secret.
 *
 * Twin of 07_button_to_broker.py
 * (embedded-systems-for-aiot-developer/examples/s03/07_button_to_broker.py).
 * Style: CLEAN_C.md.
 * In the BENTO Emulator, MQTT from C is not available yet: the
 * program says so and ends.
 *
 * Differs from the original:
 *   - wifi.connect() and the mqtt calls print console lines of
 *     their own. This program prints the ones C can know ("[WiFi]
 *     Connecting...", "[WiFi] Connected! IP=...", "[MQTT]
 *     Connecting to ...", "[MQTT] Connected to ... as '...'",
 *     "[MQTT] Subscribed to '...' (QoS=0)"). No C function reports
 *     the others: each failed join attempt with the radio's code,
 *     "[WiFi] Credentials saved to QSPI", "[MQTT] Library
 *     initialized" (first use after a reset), the cy result code of
 *     a failed connect, subscribe or publish, and "[MQTT]
 *     Disconnected (reason=N)" (modwifi.c, modmqtt.c).
 *   - mqtt.connect() raises only when the Wi-Fi is down, and
 *     answers False for any broker failure. Here a client that
 *     cannot even start is the "broker ไม่ตอบ" stop, and a start
 *     that ends without a CONNACK is the "broker ปฏิเสธ" stop.
 *   - mqtt.get_message() keeps one slot that each new message
 *     overwrites (modmqtt.c:81-99). C keeps a ring of four, so each
 *     round reads every waiting message and keeps the newest, cut
 *     to the binding's 255 bytes. Only when more than four arrive
 *     within one 5 ms round does the ring keep the first four.
 *   - json.loads is a small parser here that follows the rules of
 *     extmod/modjson.c (commas and colons count as spaces, a
 *     dangling key carries over, ...). str() of a real number, a
 *     list or a dict in a command is shown as its JSON text, not
 *     as Python writes it, and a real number counts as true when a
 *     digit before its exponent is not 0.
 *   - json.dumps writes the keys in the slot order of MicroPython's
 *     dict (py/map.c, py/qstr.c), which depends on the qstr hash:
 *     1 byte on the PSoC Edge, 2 bytes on the reTerminal.
 *   - The original never calls mqtt.disconnect(): the binding keeps
 *     the link after the program ends and drops it at the next
 *     connect. A C client still open at the end (the run is over,
 *     stopped or failed) is closed then, silently: it would
 *     otherwise hold the PSoC Edge's only MQTT slot until a reset.
 *     The broker sees the device leave when the program ends, not
 *     when it runs again.
 *   - Stop ends the program quietly where MicroPython raises
 *     KeyboardInterrupt.
 */
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
/* แก้สี่บรรทัดนี้ให้ตรงกับที่ผู้สอนแจก
 * Set these four to what the teacher hands out. */
/* the team's phone hotspot: faculty Wi-Fi needs a login */
static const char WIFI_SSID[] = "bento-teamXX";
/* 8 characters or more */
static const char WIFI_PASS[] = "<รหัส Hotspot ของทีม>";
/* the spare is "test.mosquitto.org" */
static const char BROKER[] = "broker.hivemq.com";
/* team01 to team19; unset, the program will not run */
static const char TEAM[] = "teamXX";
/* สองบรรทัดนี้ห้ามแก้ (do not change these two) */
static const char ROOT[] = "bento-aiot";
/* DEVICE_ID = bento-aiot-TEAM, built in make_names() */
/* TOPIC_EVENT = ROOT/TEAM/event, TOPIC_STATE = ROOT/TEAM/telemetry,
 * built in code */
/* ---- end of settings ---- */

/* The course palette, 0xRRGGBB. */
enum {
    COL_TEXT = 0xE8EAED,
    COL_DIM = 0x9AA3AF,
    COL_CARD = 0x171B22,
    COL_ACCENT = 0x4A9EFF,
    COL_OK = 0x30A46C,
    COL_WARN = 0xF5A623,
    COL_BAD = 0xE5484D
};

/* Times in milliseconds, and other numbers. */
enum {
    SETTLE_MS = 200,
    DEBOUNCE_MS = 40, /* the pin must be still this long */
    POLL_MS = 5,
    STATE_MS = 2000,
    RUN_MS = 180000,
    MS_PER_S = 1000,
    WAIT_STEP_MS = 50, /* between two polls of a long call */
    IP_TRIES = 15,
    IP_WAIT_MS = 200,
    BROKER_PORT = 1883,
    TONE_NOTE = 69, /* a MIDI note, not a frequency */
    TONE_VELOCITY = 90,
    TONE_MS = 150,
    TEAM_CHARS = 6,
    PREFIX_CHARS = 4,    /* "team" */
    BAD_INDEX_CHARS = 8, /* how much of a bad led index is shown */
    UNKNOWN_CHARS = 16,
    SAY_CHARS = 24,
    MESSAGE_KEEP = 255 /* the binding's one-message buffer */
};

/* Buffer sizes in bytes. */
enum {
    TEXT_BYTES = 128,   /* a SET_TEXT carries at most 126 */
    CHAR_BYTES_MAX = 4, /* the longest UTF-8 character */
    LINE_BYTES = 129,   /* lcd.print looks at 128 */
    TOPIC_BYTES = 48,
    ID_BYTES = 24,
    VALUE_BYTES = 40,
    PAYLOAD_BYTES = 192,
    JSON_DEPTH_MAX = 256 /* one bracket per byte at most */
};

/* The widgets the program changes. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t status;
    bento_ui_id_t seg;
    bento_ui_id_t sent;
    bento_ui_id_t errors;
    bento_ui_id_t leds;
    bento_ui_id_t last;
    bento_ui_id_t topic;
    bento_ui_id_t az;
    bento_ui_id_t note;
} screen_t;

/* The broker link and the names it uses. */
typedef struct {
    uint32_t client;
    char device_id[ID_BYTES];
    char topic_event[TOPIC_BYTES];
    char topic_state[TOPIC_BYTES];
    char topic_cmd[TOPIC_BYTES];
} link_t;

/* The counters on the screen. */
typedef struct {
    int32_t presses;
    int32_t messages;
    int32_t events_sent;
    int32_t states_sent;
    int32_t failures;
    int32_t bad_commands;
} tally_t;

typedef struct {
    screen_t screen;
    link_t link;
    tally_t tally;
    uint32_t led_count;
    int32_t leds[BENTO_GPIO_LED_CAP]; /* what was sent, not the pins */
    uint32_t start_ms;                /* when the loop started */
    bool halted;                      /* stop_here() ended the run */
    bool lost;                        /* the broker link dropped */
} app_t;

/* The button as the loop sees it: the raw pin and the debounced
 * state, with when the pin last changed. */
typedef struct {
    bool stable;
    bool last_raw;
    uint32_t last_change_ms;
    uint32_t last_state_ms;
    int32_t last_second;
    uint32_t start_ms;
} watch_t;

/* The keys the two messages carry. */
enum {
    KEY_PRESSES = 0,
    KEY_ID = 1,
    KEY_EV = 2,
    KEY_N = 3,
    KEY_BUTTON = 4,
    KEY_LEDS = 5,
    KEY_AZ = 6,
    KEY_COUNT = 7
};

/* One message before it is written: each key's value as JSON. */
typedef struct {
    char values[KEY_COUNT][VALUE_BYTES];
    const uint8_t *order; /* the keys in json.dumps' order */
    uint32_t count;
} message_t;

/* ---------------------------------------------------------------
 * Receiving: json.loads, as extmod/modjson.c reads a text
 * ------------------------------------------------------------- */

/* What a JSON value is. */
enum {
    KIND_NONE = 0, /* the key is absent: the default applies */
    KIND_STRING = 1,
    KIND_INT = 2,
    KIND_REAL = 3,
    KIND_TRUE = 4,
    KIND_FALSE = 5,
    KIND_NULL = 6,
    KIND_LIST = 7,
    KIND_DICT = 8
};

/* The command keys the program reads; FIELD_COUNT is any other. */
enum {
    FIELD_CMD = 0,
    FIELD_N = 1,
    FIELD_ON = 2,
    FIELD_TEXT = 3,
    FIELD_COUNT = 4
};

/* A value inside the payload. */
typedef struct {
    uint8_t kind;
    uint32_t start; /* its first byte */
    uint32_t end;   /* one past its last byte */
    bool filled;    /* a list or dict that holds an entry */
} json_value_t;

/* The parser's place in the text. A NUL ends the text, as it is
 * modjson.c's end marker. */
typedef struct {
    const uint8_t *text;
    uint32_t len;
    uint32_t index;
} json_reader_t;

/* modjson.c's state: the open containers, the one pending key
 * (it is not saved per level), and what the program wants. */
typedef struct {
    json_reader_t in;
    uint8_t levels[JSON_DEPTH_MAX];
    uint32_t depth;
    bool key_pending;
    uint32_t key_field;
    uint32_t open_field; /* the entry a second-level container is */
    uint8_t top_kind;
    bool done;
    bool failed;
    json_value_t fields[FIELD_COUNT];
} json_parse_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void lcd_line(const char *text)
{
    (void)bento_lcd_print(text);
}

/* text[:chars]: the first chars code points of a UTF-8 text, into
 * out (size holds 4 bytes a character and the NUL). */
static void copy_chars(char *out, size_t size, const char *text,
                       uint32_t chars)
{
    const uint8_t top_bits = 0xC0U;
    const uint8_t continuation = 0x80U;
    uint32_t seen = 0U;
    size_t i = 0U;
    bool full = false;

    while (!full && (text[i] != '\0') && ((i + 1U) < size)) {
        if (((uint8_t)text[i] & top_bits) != continuation) {
            seen++;
        }
        full = seen > chars;
        if (!full) {
            out[i] = text[i];
            i++;
        }
    }
    out[i] = '\0';
}

/* "[0, 1, 0]": str() of the leds list, the same as its JSON. */
static void format_list(const app_t *app, char *text, size_t size)
{
    size_t used;
    uint32_t i;

    (void)snprintf(text, size, "[");
    for (i = 0U; i < app->led_count; i++) {
        used = strlen(text);
        (void)snprintf(&text[used], size - used, "%s%d",
                       (i > 0U) ? ", " : "", (int)app->leds[i]);
    }
    used = strlen(text);
    (void)snprintf(&text[used], size - used, "]");
}

/* "leds = [0, 1, 0]" */
static void format_leds(const app_t *app, char *text, size_t size)
{
    char list[VALUE_BYTES];

    format_list(app, list, sizeof(list));
    (void)snprintf(text, size, "leds = %s", list);
}

/* {"key": value, ...} in the message's order. */
static void write_json(const message_t *message, char *text,
                       size_t size)
{
    static const char *const key_names[KEY_COUNT] = {
        "presses", "id", "ev", "n", "btn", "leds", "az"};
    size_t used;
    uint32_t i;

    (void)snprintf(text, size, "{");
    for (i = 0U; i < message->count; i++) {
        const uint8_t key = message->order[i];

        used = strlen(text);
        (void)snprintf(&text[used], size - used, "%s\"%s\": %s",
                       (i > 0U) ? ", " : "", key_names[key],
                       message->values[key]);
    }
    used = strlen(text);
    (void)snprintf(&text[used], size - used, "}");
}

/* round(value, 2): nearbyint(value * 100) / 100 in the board's
 * float width (py/modbuiltins.c), a tie going to the even one. */
static bento_real_t round_hundredths(bento_real_t value)
{
    const bento_real_t scale = (bento_real_t)100.0;
    const bento_real_t half = (bento_real_t)0.5;
    const bento_real_t scaled = value * scale;
    int32_t whole = (int32_t)scaled; /* toward zero */
    const bento_real_t rest = scaled - (bento_real_t)whole;
    const bool odd = (whole % 2) != 0;

    if ((rest > half) || ((rest == half) && odd)) {
        whole++;
    } else if ((rest < -half) || ((rest == -half) && odd)) {
        whole--;
    } else {
        /* already the nearest */
    }
    return (bento_real_t)whole / scale;
}

/* read_az(): the z acceleration in m/s2, rounded to 2 places, as
 * MicroPython writes it. false when it cannot be read this round. */
static bool read_az(char *text, uint32_t size)
{
    float ax = 0.0F;
    float ay = 0.0F;
    float az = 0.0F;
    uint32_t length = 0U;
    const bool ok =
        bento_sensors_bmi270_acceleration(&ax, &ay, &az) == BENTO_OK;

    if (ok) {
        /* a BENTO_FMT_BUF_SIZE buffer holds any number */
        (void)bento_fmt_real(
            text, size, round_hundredths((bento_real_t)az), &length);
    }
    return ok;
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

/* The order json.dumps writes the telemetry keys in (header). */
static const uint8_t *state_order(bool has_az, uint32_t *count)
{
    static const uint8_t plain_reterminal[] = {KEY_PRESSES, KEY_BUTTON,
                                               KEY_N, KEY_ID, KEY_LEDS};
    static const uint8_t with_az_reterminal[] = {
        KEY_PRESSES, KEY_BUTTON, KEY_ID, KEY_N, KEY_AZ, KEY_LEDS};
    static const uint8_t plain_psoc[] = {KEY_N, KEY_PRESSES, KEY_BUTTON,
                                         KEY_ID, KEY_LEDS};
    static const uint8_t with_az_psoc[] = {
        KEY_ID, KEY_N, KEY_PRESSES, KEY_BUTTON, KEY_AZ, KEY_LEDS};
    const uint8_t *order;

    if (on_reterminal()) {
        *count = has_az ? (uint32_t)sizeof(with_az_reterminal)
                        : (uint32_t)sizeof(plain_reterminal);
        order = has_az ? with_az_reterminal : plain_reterminal;
    } else {
        *count = has_az ? (uint32_t)sizeof(with_az_psoc)
                        : (uint32_t)sizeof(plain_psoc);
        order = has_az ? with_az_psoc : plain_psoc;
    }
    return order;
}

/* Every LED off, and the variable too, or the screen would lie. */
static bento_status_t leds_off(app_t *app)
{
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < app->led_count) && (status == BENTO_OK); i++) {
        status = bento_gpio_led_off(i);
        app->leds[i] = 0;
    }
    return status;
}

static void print_summary(const tally_t *tally)
{
    char line[LINE_BYTES];

    /* lcd.console() ignores a console that is not ready */
    (void)bento_lcd_console(
        "<span class=muted>------------------------</span>");
    (void)snprintf(line, sizeof(line),
                   "<span class=ok>กด %d ครั้ง | ส่ง %d ใบ</span>",
                   (int)tally->presses, (int)tally->messages);
    lcd_line(line);
    (void)printf("กด %d ครั้ง | event %d | telemetry %d"
                 " | ส่งไม่ผ่าน %d | คำสั่งเสีย %d\n",
                 (int)tally->presses, (int)tally->events_sent,
                 (int)tally->states_sent, (int)tally->failures,
                 (int)tally->bad_commands);
}

static uint8_t peek(const json_reader_t *in)
{
    return (in->index < in->len) ? in->text[in->index] : 0U;
}

static void advance(json_reader_t *in)
{
    if (in->index < in->len) {
        in->index++;
    }
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
    UTF8_ONE_END = 0x80,
    UTF8_TWO_END = 0x800,
    UTF8_THREE_END = 0x10000,
    UTF8_SIX_BITS = 0x3F,
    UTF8_SHIFT = 6
};

/* The bytes that follow a lead byte (0xC0 to 0xF7). */
static uint32_t follow_count(uint8_t lead)
{
    return (lead >= (uint32_t)UTF8_LEAD_FOUR)    ? (uint32_t)UTF8_THREE
           : (lead >= (uint32_t)UTF8_LEAD_THREE) ? 2U
                                                 : 1U;
}

/* MicroPython's utf8_check (py/unicode.c): the shape of each
 * sequence, as str() checks a text. */
static bool utf8_valid(const uint8_t *text, uint32_t len)
{
    uint32_t need = 0U;
    bool valid = true;
    uint32_t i;

    for (i = 0U; valid && (i < len); i++) {
        const uint8_t byte = text[i];

        if (need > 0U) {
            valid = (byte & (uint32_t)UTF8_FOLLOW_MASK) ==
                    (uint32_t)UTF8_FOLLOW;
            need--;
        } else if (byte >= (uint32_t)UTF8_LEAD_END) {
            valid = false;
        } else if (byte >= (uint32_t)UTF8_LEAD_TWO) {
            need = follow_count(byte);
        } else {
            valid = byte < (uint32_t)UTF8_FOLLOW;
        }
    }
    return valid && (need == 0U);
}

/* vstr_add_char (py/vstr.c): a code point as UTF-8. */
static void add_code_point(char *out, uint32_t *used, uint32_t point)
{
    static const uint8_t leads[UTF8_MAX_BYTES] = {
        0U, UTF8_LEAD_TWO, UTF8_LEAD_THREE, UTF8_LEAD_FOUR};
    uint8_t bytes[UTF8_MAX_BYTES];
    uint32_t rest = point;
    uint32_t count = UTF8_MAX_BYTES;
    uint32_t i;

    if (point < (uint32_t)UTF8_ONE_END) {
        count = 1U;
    } else if (point < (uint32_t)UTF8_TWO_END) {
        count = 2U;
    } else if (point < (uint32_t)UTF8_THREE_END) {
        count = UTF8_THREE;
    } else {
        count = UTF8_MAX_BYTES;
    }
    for (i = count - 1U; i > 0U; i--) {
        bytes[i] = (uint8_t)((uint32_t)UTF8_FOLLOW |
                             (rest & (uint32_t)UTF8_SIX_BITS));
        rest >>= (uint32_t)UTF8_SHIFT;
    }
    bytes[0] = (uint8_t)(leads[count - 1U] | rest);
    for (i = 0U; i < count; i++) {
        out[*used] = (char)bytes[i];
        *used += 1U;
    }
}

/* \uXXXX: four characters, read the way modjson.c reads them
 * (no check that they are hex digits). */
static uint32_t read_hex4(json_reader_t *in)
{
    const uint8_t lower = 0x20U;
    const uint8_t gap = (uint8_t)('a' - ('9' + 1));
    const uint32_t hex_digits = 4U;
    const uint32_t hex_shift = 4U;
    const uint8_t digit_max = 9U;
    uint32_t point = 0U;
    uint32_t i;

    for (i = 0U; i < hex_digits; i++) {
        uint8_t digit;

        advance(in);
        digit = (uint8_t)((uint8_t)(peek(in) | lower) - (uint8_t)'0');
        if (digit > digit_max) {
            digit = (uint8_t)(digit - gap);
        }
        point = (point << hex_shift) | (uint32_t)digit;
    }
    return point;
}

/* The byte a one-letter escape stands for. */
static uint8_t escaped_byte(uint8_t letter)
{
    static const uint8_t letters[] = {'b', 'f', 'n', 'r', 't'};
    static const uint8_t bytes[] = {0x08U, 0x0CU, 0x0AU, 0x0DU, 0x09U};
    uint8_t byte = letter; /* any other letter stands for itself */
    uint32_t i;

    for (i = 0U; i < (uint32_t)sizeof(letters); i++) {
        if (letters[i] == letter) {
            byte = bytes[i];
        }
    }
    return byte;
}

/* One character of a string at in's position, added to out: an
 * escape, \u and four hex characters, or the byte itself. */
static void decode_char(json_reader_t *in, char *out, uint32_t *used)
{
    uint8_t byte = peek(in);

    if (byte == (uint8_t)'\\') {
        advance(in);
        byte = peek(in);
        if (byte == (uint8_t)'u') {
            add_code_point(out, used, read_hex4(in));
        } else {
            out[*used] = (char)escaped_byte(byte);
            *used += 1U;
        }
    } else {
        out[*used] = (char)byte;
        *used += 1U;
    }
    advance(in);
}

/* A string after its opening quote, decoded into out (which holds
 * at least len + 1 bytes). false: no closing quote, or not UTF-8. */
static bool decode_string(json_reader_t *in, char *out,
                          uint32_t *out_len)
{
    uint32_t used = 0U;
    bool closed;

    while ((peek(in) != 0U) && (peek(in) != (uint8_t)'"')) {
        decode_char(in, out, &used);
    }
    closed = peek(in) != 0U;
    advance(in);
    out[used] = '\0';
    *out_len = used;
    return closed && utf8_valid((const uint8_t *)out, used);
}

/* e or E: the case bit set, it reads as e. */
static bool is_exponent(uint8_t byte)
{
    const uint8_t lower_case = 0x20U;

    return (byte | lower_case) == (uint8_t)'e';
}

static bool is_digit(uint8_t byte)
{
    return (byte >= (uint8_t)'0') && (byte <= (uint8_t)'9');
}

/* An integer token: an optional '-' then digits only. */
static bool int_text_valid(const uint8_t *token, uint32_t len)
{
    uint32_t i = (token[0] == (uint8_t)'-') ? 1U : 0U;
    bool valid = i < len;

    for (; valid && (i < len); i++) {
        valid = is_digit(token[i]);
    }
    return valid;
}

/* After an e or E: an optional sign. */
static uint32_t skip_sign(const uint8_t *token, uint32_t len,
                          uint32_t at)
{
    const bool has_sign = (at < len) && ((token[at] == (uint8_t)'+') ||
                                         (token[at] == (uint8_t)'-'));

    return has_sign ? (at + 1U) : at;
}

/* A real-number token, as mp_parse_num_float takes it: digits, one
 * '.' before the exponent, then e, an optional sign and digits. */
static bool real_text_valid(const uint8_t *token, uint32_t len)
{
    enum {
        PART_WHOLE = 0,
        PART_FRACTION = 1,
        PART_EXPONENT = 2
    };
    uint32_t part = PART_WHOLE;
    uint32_t i = (token[0] == (uint8_t)'-') ? 1U : 0U;
    bool valid = true;

    while (valid && (i < len)) {
        const uint8_t byte = token[i];

        i++;
        if (is_digit(byte)) {
            /* a digit fits every part */
        } else if ((byte == (uint8_t)'.') &&
                   (part == (uint32_t)PART_WHOLE)) {
            part = PART_FRACTION;
        } else if (is_exponent(byte) &&
                   (part != (uint32_t)PART_EXPONENT)) {
            part = PART_EXPONENT;
            i = skip_sign(token, len, i);
            valid = i < len; /* "1e" and "1e-" are refused */
        } else {
            valid = false;
        }
    }
    return valid;
}

/* A number after its first character: the characters modjson.c
 * collects, then the integer or real rule. */
static bool scan_number(json_reader_t *in, uint8_t *kind)
{
    const uint32_t start = in->index - 1U;
    bool real = false;
    bool more = true;

    while (more) {
        const uint8_t byte = peek(in);

        if ((byte == (uint8_t)'.') || is_exponent(byte)) {
            real = true;
        } else {
            more = (byte == (uint8_t)'+') || (byte == (uint8_t)'-') ||
                   is_digit(byte);
        }
        if (more) {
            advance(in);
        }
    }
    *kind = real ? (uint8_t)KIND_REAL : (uint8_t)KIND_INT;
    return real ? real_text_valid(&in->text[start], in->index - start)
                : int_text_valid(&in->text[start], in->index - start);
}

/* null, true or false after its first letter. */
static bool scan_word(json_reader_t *in, const char *rest)
{
    bool same = true;
    size_t i;

    for (i = 0U; same && (rest[i] != '\0'); i++) {
        same = peek(in) == (uint8_t)rest[i];
        advance(in);
    }
    return same;
}

/* Which wanted key a decoded string is, or FIELD_COUNT. */
static uint32_t field_of(const char *key)
{
    static const char *const names[FIELD_COUNT] = {"cmd", "n", "on",
                                                   "text"};
    uint32_t field = FIELD_COUNT;
    uint32_t i;

    for (i = 0U; i < (uint32_t)FIELD_COUNT; i++) {
        if (strcmp(key, names[i]) == 0) {
            field = i;
        }
    }
    return field;
}

/* One token after its first character: its kind, and for a string
 * which key it would be. false: the text is not JSON. */
static bool scan_token(json_reader_t *in, uint8_t first, uint8_t *kind,
                       uint32_t *field)
{
    char decoded[MESSAGE_KEEP + 1];
    uint32_t length = 0U;
    bool valid = true;

    *field = FIELD_COUNT;
    if (first == (uint8_t)'"') {
        *kind = KIND_STRING;
        valid = decode_string(in, decoded, &length);
        *field = valid ? field_of(decoded) : (uint32_t)FIELD_COUNT;
    } else if ((first == (uint8_t)'-') || is_digit(first)) {
        valid = scan_number(in, kind);
    } else if (first == (uint8_t)'n') {
        *kind = KIND_NULL;
        valid = scan_word(in, "ull");
    } else if (first == (uint8_t)'t') {
        *kind = KIND_TRUE;
        valid = scan_word(in, "rue");
    } else if (first == (uint8_t)'f') {
        *kind = KIND_FALSE;
        valid = scan_word(in, "alse");
    } else if ((first == (uint8_t)'[') || (first == (uint8_t)'{')) {
        *kind = (first == (uint8_t)'[') ? (uint8_t)KIND_LIST
                                        : (uint8_t)KIND_DICT;
    } else {
        valid = false;
    }
    return valid;
}

/* An entry went into the container at the second level. */
static void note_entry(json_parse_t *parse)
{
    if ((parse->depth == 2U) && (parse->open_field < FIELD_COUNT)) {
        parse->fields[parse->open_field].filled = true;
    }
}

static void open_container(json_parse_t *parse, uint8_t kind)
{
    if (parse->depth < (uint32_t)JSON_DEPTH_MAX) {
        parse->levels[parse->depth] = kind;
        parse->depth++;
    } else {
        parse->failed = true;
    }
}

/* A value inside a dict: a key first, then its value. A container
 * may not be a key. */
static void place_in_dict(json_parse_t *parse,
                          const json_value_t *value, uint32_t field,
                          bool entering)
{
    if (!parse->key_pending) {
        parse->key_pending = true;
        parse->key_field = field;
        parse->failed = entering;
    } else {
        note_entry(parse);
        if ((parse->depth == 1U) && (parse->key_field < FIELD_COUNT)) {
            parse->fields[parse->key_field] = *value;
        }
        if (entering && (parse->depth == 1U)) {
            parse->open_field = parse->key_field;
        }
        parse->key_pending = false;
    }
}

/* Where a value goes: the document itself, a list, or a dict. */
static void place_value(json_parse_t *parse, const json_value_t *value,
                        uint32_t field)
{
    const bool entering = (value->kind == (uint8_t)KIND_LIST) ||
                          (value->kind == (uint8_t)KIND_DICT);

    if (parse->depth == 0U) {
        parse->top_kind = value->kind;
        parse->done = !entering; /* a lone value is the document */
    } else if (parse->levels[parse->depth - 1U] == (uint8_t)KIND_LIST) {
        note_entry(parse);
        if (entering && (parse->depth == 1U)) {
            parse->open_field = FIELD_COUNT;
        }
    } else {
        place_in_dict(parse, value, field, entering);
    }
    if (entering && !parse->failed) {
        open_container(parse, value->kind);
    }
}

/* '}' or ']': closes the innermost container, whichever it is. */
static void close_container(json_parse_t *parse)
{
    if (parse->depth == 0U) {
        parse->failed = true;
    } else if (parse->depth == 1U) {
        parse->done = true;
    } else {
        if ((parse->depth == 2U) && (parse->open_field < FIELD_COUNT)) {
            parse->fields[parse->open_field].end = parse->in.index;
        }
        parse->depth--;
    }
}

/* One step of modjson.c's loop. */
static void parse_step(json_parse_t *parse)
{
    const uint8_t first = peek(&parse->in);
    json_value_t value = {KIND_NONE, 0U, 0U, false};
    uint32_t field = FIELD_COUNT;

    advance(&parse->in);
    if (first == 0U) {
        parse->failed = true; /* the text ended inside a value */
    } else if ((first == (uint8_t)',') || (first == (uint8_t)':') ||
               (first == (uint8_t)' ') || (first == (uint8_t)'\t') ||
               (first == (uint8_t)'\n') || (first == (uint8_t)'\r')) {
        /* separators and spaces are all skipped */
    } else if ((first == (uint8_t)'}') || (first == (uint8_t)']')) {
        close_container(parse);
    } else {
        value.start = parse->in.index - 1U;
        parse->failed =
            !scan_token(&parse->in, first, &value.kind, &field);
        value.end = parse->in.index;
        if (!parse->failed) {
            place_value(parse, &value, field);
        }
    }
}

static bool is_space(uint8_t byte)
{
    return (byte == (uint8_t)' ') ||
           ((byte >= (uint8_t)'\t') && (byte <= (uint8_t)'\r'));
}

/* json.loads(text): true when it is JSON. text ends at its first
 * NUL (modjson.c's end marker). */
static bool parse_json(json_parse_t *parse, const uint8_t *text,
                       uint32_t len)
{
    uint32_t i;

    (void)memset(parse, 0, sizeof(*parse));
    parse->in.text = text;
    parse->in.len = len;
    for (i = 0U; i < len; i++) {
        if ((text[i] == 0U) && (parse->in.len == len)) {
            parse->in.len = i;
        }
    }
    parse->open_field = FIELD_COUNT;
    while (!parse->done && !parse->failed) {
        parse_step(parse);
    }
    while (is_space(peek(&parse->in))) {
        advance(&parse->in);
    }
    return !parse->failed && (peek(&parse->in) == 0U);
}

/* str() of an integer token: no leading zeros, and "-0" is "0". */
static void int_text(const uint8_t *token, uint32_t len, char *out,
                     uint32_t size)
{
    const bool negative = token[0] == (uint8_t)'-';
    uint32_t first = negative ? 1U : 0U;

    while ((first < (len - 1U)) && (token[first] == (uint8_t)'0')) {
        first++;
    }
    (void)snprintf(out, size, "%s%.*s",
                   (negative && (token[first] != (uint8_t)'0')) ? "-"
                                                                : "",
                   (int)len - (int)first, (const char *)&token[first]);
}

/* The value of an integer token, held at a limit far beyond any
 * LED number. */
static int32_t int_value(const uint8_t *token, uint32_t len)
{
    const int32_t limit = 1000000;
    const int32_t base = 10;
    const bool negative = token[0] == (uint8_t)'-';
    int32_t value = 0;
    uint32_t i;

    for (i = negative ? 1U : 0U; i < len; i++) {
        if (value < limit) {
            value = (value * base) + ((int32_t)token[i] - (int32_t)'0');
        }
    }
    return negative ? -value : value;
}

/* str() of a command value; "" when the key is absent. A real
 * number, list or dict is shown as its JSON text (header). */
static void value_text(const json_parse_t *parse, uint32_t field,
                       char *out, uint32_t size)
{
    const json_value_t *value = &parse->fields[field];
    const uint8_t *token = &parse->in.text[value->start];
    const uint32_t len = value->end - value->start;
    json_reader_t in = parse->in;
    uint32_t length = 0U;

    out[0] = '\0';
    if (value->kind == (uint8_t)KIND_STRING) {
        in.index = value->start + 1U;           /* after the quote */
        (void)decode_string(&in, out, &length); /* checked once */
    } else if (value->kind == (uint8_t)KIND_INT) {
        int_text(token, len, out, size);
    } else if (value->kind == (uint8_t)KIND_TRUE) {
        (void)snprintf(out, size, "True");
    } else if (value->kind == (uint8_t)KIND_FALSE) {
        (void)snprintf(out, size, "False");
    } else if (value->kind == (uint8_t)KIND_NULL) {
        (void)snprintf(out, size, "None");
    } else if (value->kind != (uint8_t)KIND_NONE) {
        (void)snprintf(out, size, "%.*s", (int)len,
                       (const char *)token);
    } else {
        /* absent: the default "" */
    }
}

/* Python's truth of a value; absent is the given default. */
static bool value_is_true(const json_parse_t *parse, uint32_t field,
                          bool absent)
{
    const json_value_t *value = &parse->fields[field];
    const uint8_t *token = &parse->in.text[value->start];
    bool truth = absent;
    uint32_t i;

    if ((value->kind == (uint8_t)KIND_INT) ||
        (value->kind == (uint8_t)KIND_REAL)) {
        truth = false; /* any digit but 0 before the exponent */
        for (i = 0U; (i < (value->end - value->start)) &&
                     !is_exponent(token[i]);
             i++) {
            truth = truth ||
                    (is_digit(token[i]) && (token[i] != (uint8_t)'0'));
        }
    } else if (value->kind == (uint8_t)KIND_STRING) {
        truth = (value->end - value->start) > 2U; /* not "" */
    } else if ((value->kind == (uint8_t)KIND_LIST) ||
               (value->kind == (uint8_t)KIND_DICT)) {
        truth = value->filled;
    } else if (value->kind != (uint8_t)KIND_NONE) {
        truth = value->kind == (uint8_t)KIND_TRUE;
    } else {
        /* absent: the default */
    }
    return truth;
}

/* A string value equal to word. */
static bool value_is(const json_parse_t *parse, uint32_t field,
                     const char *word)
{
    char text[MESSAGE_KEEP + 1];

    value_text(parse, field, text, (uint32_t)sizeof(text));
    return (parse->fields[field].kind == (uint8_t)KIND_STRING) &&
           (strcmp(text, word) == 0);
}

/* The bytes of the code point that starts at text[*at]; *at moves
 * past it. The text has passed utf8_valid. */
static uint32_t next_char(const char *text, size_t *at, size_t *bytes)
{
    static const uint8_t lead_bits[UTF8_MAX_BYTES] = {0xFFU, 0x1FU,
                                                      0x0FU, 0x07U};
    const uint8_t lead = (uint8_t)text[*at];
    const size_t count = (lead >= (uint32_t)UTF8_LEAD_TWO)
                             ? (follow_count(lead) + 1U)
                             : 1U;
    uint32_t point = (uint32_t)lead & (uint32_t)lead_bits[count - 1U];
    size_t i;

    for (i = 1U; i < count; i++) {
        const uint32_t follow = (uint32_t)(uint8_t)text[*at + i];

        point = (point << (uint32_t)UTF8_SHIFT) |
                (follow & (uint32_t)UTF8_SIX_BITS);
    }
    *bytes = count;
    *at += count;
    return point;
}

/* What the screen can draw: printable ASCII and the Thai block. */
enum {
    PRINTABLE_FIRST = 0x20,
    PRINTABLE_LAST = 0x7E,
    DELETE_CHAR = 0x7F,
    THAI_FIRST = 0x0E00,
    THAI_LAST = 0x0E7F
};

/* screen_safe(): only what the screen can draw (ASCII and Thai),
 * anything else as "?", at most chars characters. */
static void screen_safe(const char *text, uint32_t chars, char *out,
                        size_t size)
{
    size_t at = 0U;
    size_t used = 0U;
    uint32_t kept = 0U;

    while ((text[at] != '\0') && (kept < chars) &&
           ((used + (size_t)UTF8_MAX_BYTES) < size)) {
        const size_t start = at;
        size_t bytes = 0U;
        const uint32_t point = next_char(text, &at, &bytes);

        if (((point >= (uint32_t)PRINTABLE_FIRST) &&
             (point <= (uint32_t)PRINTABLE_LAST)) ||
            ((point >= (uint32_t)THAI_FIRST) &&
             (point <= (uint32_t)THAI_LAST))) {
            (void)memcpy(&out[used], &text[start], bytes);
            used += bytes;
        } else {
            out[used] = '?';
            used++;
        }
        kept++;
    }
    out[used] = '\0';
}

/* repr() of bytes, as py/objstr.c writes it: b'...' with escapes. */
static void bytes_repr(const uint8_t *data, uint32_t len, char *out,
                       size_t size)
{
    const bool has_single = memchr(data, '\'', len) != NULL;
    const bool has_double = memchr(data, '"', len) != NULL;
    const char quote = (has_single && !has_double) ? '"' : '\'';
    size_t used;
    uint32_t i;

    (void)snprintf(out, size, "b%c", quote);
    for (i = 0U; i < len; i++) {
        const uint8_t byte = data[i];

        used = strlen(out);
        if ((byte == (uint8_t)quote) || (byte == (uint8_t)'\\')) {
            (void)snprintf(&out[used], size - used, "\\%c", (char)byte);
        } else if ((byte >= (uint32_t)PRINTABLE_FIRST) &&
                   (byte < (uint32_t)DELETE_CHAR)) {
            (void)snprintf(&out[used], size - used, "%c", (char)byte);
        } else if (byte == (uint8_t)'\n') {
            (void)snprintf(&out[used], size - used, "\\n");
        } else if (byte == (uint8_t)'\r') {
            (void)snprintf(&out[used], size - used, "\\r");
        } else if (byte == (uint8_t)'\t') {
            (void)snprintf(&out[used], size - used, "\\t");
        } else {
            (void)snprintf(&out[used], size - used, "\\x%02x",
                           (unsigned int)byte);
        }
    }
    used = strlen(out);
    (void)snprintf(&out[used], size - used, "%c", quote);
}

/* The address text of wifi.ip(): "0.0.0.0" while there is none. */
static void read_ip(char *text, uint32_t size)
{
    uint32_t ip_v4 = 0U;
    uint32_t length = 0U;

    if (bento_wifi_ip(BENTO_WIFI_IF_ANY, &ip_v4) != BENTO_OK) {
        ip_v4 = 0U;
    }
    /* a BENTO_WIFI_IPV4_TEXT_MAX buffer holds any address */
    (void)bento_wifi_ipv4_format(ip_v4, text, size, &length);
}

/* mqtt.subscribe(): True only when the broker granted it. */
static bool listen_for_commands(const link_t *link)
{
    uint8_t granted = 0U;
    const bool ok =
        bento_mqtt_subscribe(link->client, link->topic_cmd,
                             (uint32_t)strlen(link->topic_cmd), 0U,
                             &granted) == BENTO_OK;

    if (ok) {
        (void)printf("[MQTT] Subscribed to '%s' (QoS=%d)\r\n",
                     link->topic_cmd, (int)granted);
    }
    return ok;
}

/* get_message(): the binding keeps only the newest message, so
 * every waiting one is read and the last is kept (header). */
static bool receive(const app_t *app, uint8_t *raw, uint32_t *len)
{
    bento_mqtt_msg_t message;
    char topic[BENTO_MQTT_TOPIC_MAX];
    uint8_t incoming[MESSAGE_KEEP];
    bool got = false;

    bento_mqtt_msg_init(&message);
    while (bento_mqtt_poll(app->link.client, &message, topic,
                           (uint32_t)sizeof(topic), incoming,
                           (uint32_t)sizeof(incoming)) == BENTO_OK) {
        (void)memcpy(raw, incoming, message.payload_len);
        *len = message.payload_len;
        got = true;
    }
    return got;
}

/* ---------------------------------------------------------------
 * Commands from the web page
 * ------------------------------------------------------------- */

static void set_color(const screen_t *screen, bento_ui_id_t widget,
                      uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, widget, color);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(const screen_t *screen, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(screen->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void show_last(const app_t *app, uint32_t color,
                      const char *text)
{
    set_color(&app->screen, app->screen.last, color);
    set_text(&app->screen, app->screen.last, text);
}

/* {"cmd": "led", "n": i, "on": 1}. *done is false when the LED
 * number is not one the board has (nothing more is said). */
static bento_status_t command_led(app_t *app, const json_parse_t *parse,
                                  bool *done)
{
    const json_value_t *index = &parse->fields[FIELD_N];
    const uint8_t *token = &parse->in.text[index->start];
    char number[MESSAGE_KEEP + 1];
    char
        shown[((int32_t)BAD_INDEX_CHARS * (int32_t)CHAR_BYTES_MAX) + 1];
    char text[TEXT_BYTES];
    int32_t led = 0; /* n defaults to 0 */
    bool is_int = true;
    bool on;
    bento_status_t status = BENTO_OK;

    value_text(parse, FIELD_N, number, (uint32_t)sizeof(number));
    if (index->kind == (uint8_t)KIND_INT) {
        led = int_value(token, index->end - index->start);
    } else if (index->kind == (uint8_t)KIND_NONE) {
        (void)snprintf(number, sizeof(number), "0");
    } else {
        /* True and False are ints in Python too */
        is_int = (index->kind == (uint8_t)KIND_TRUE) ||
                 (index->kind == (uint8_t)KIND_FALSE);
        led = (index->kind == (uint8_t)KIND_TRUE) ? 1 : 0;
    }
    *done = is_int && (led >= 0) && (led < (int32_t)app->led_count);
    if (!*done) {
        app->tally.bad_commands++;
        copy_chars(shown, sizeof(shown), number, BAD_INDEX_CHARS);
        (void)snprintf(text, sizeof(text), "led ไม่มีดวง %s", shown);
        show_last(app, COL_BAD, text);
    } else {
        on = value_is_true(parse, FIELD_ON, true);
        status = on ? bento_gpio_led_on((uint32_t)led)
                    : bento_gpio_led_off((uint32_t)led);
        app->leds[led] = on ? 1 : 0; /* the pin, then the variable */
        format_leds(app, text, sizeof(text));
        set_text(&app->screen, app->screen.leds, text);
        /* a valid number is short: 0..7, True or False */
        (void)snprintf(text, sizeof(text), "led %.8s%s", number,
                       on ? " ติด" : " ดับ");
        show_last(app, COL_OK, text);
    }
    return status;
}

/* {"cmd": "say", "text": "..."}: someone else's text, filtered and
 * cut to 24 characters before it reaches the screen. */
static void command_say(const app_t *app, const json_parse_t *parse)
{
    char text[MESSAGE_KEEP + 1];
    char safe[TEXT_BYTES];

    value_text(parse, FIELD_TEXT, text, (uint32_t)sizeof(text));
    screen_safe(text, SAY_CHARS, safe, sizeof(safe));
    show_last(app, COL_ACCENT, (safe[0] != '\0') ? safe : "say ว่าง");
}

static void command_unknown(app_t *app, const json_parse_t *parse)
{
    char action[MESSAGE_KEEP + 1];
    char shown[((int32_t)UNKNOWN_CHARS * (int32_t)CHAR_BYTES_MAX) + 1];
    char text[TEXT_BYTES];

    app->tally.bad_commands++;
    value_text(parse, FIELD_CMD, action, (uint32_t)sizeof(action));
    copy_chars(shown, sizeof(shown), action, UNKNOWN_CHARS);
    (void)snprintf(text, sizeof(text), "ไม่รู้จัก %s", shown);
    show_last(app, COL_WARN, text);
}

/* handle(): one command. Anyone can type anything, so a bad one
 * is counted and shown, never fatal. */
static bento_status_t handle(app_t *app, const uint8_t *raw,
                             uint32_t len)
{
    json_parse_t parse;
    char line[LINE_BYTES];
    bool done = true;
    bento_status_t status = BENTO_OK;

    /* raw.decode() and json.loads(), then cmd.get(): not UTF-8, not
     * JSON, or not a dict (like [1,2]) */
    if (!utf8_valid(raw, len) || !parse_json(&parse, raw, len) ||
        (parse.top_kind != (uint8_t)KIND_DICT)) {
        app->tally.bad_commands++;
        show_last(app, COL_BAD, "ไม่ใช่ JSON");
        done = false;
    } else if (value_is(&parse, FIELD_CMD, "led")) {
        status = command_led(app, &parse, &done);
    } else if (value_is(&parse, FIELD_CMD, "beep")) {
        /* ui.tone is fire-and-forget: it answers nothing to check */
        (void)bento_ui_sfx_tone(app->screen.ui, TONE_NOTE,
                                BENTO_UI_WAVE_SQUARE, TONE_VELOCITY,
                                TONE_MS);
        show_last(app, COL_OK, "beep");
    } else if (value_is(&parse, FIELD_CMD, "say")) {
        command_say(app, &parse);
    } else {
        command_unknown(app, &parse);
    }
    if ((status == BENTO_OK) && done) {
        (void)snprintf(line, sizeof(line), "cmd: ");
        bytes_repr(raw, len, &line[strlen(line)],
                   sizeof(line) - strlen(line));
        lcd_line(line);
    }
    return status;
}

static void show_counts(const app_t *app)
{
    const screen_t *screen = &app->screen;
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "event %d | telemetry %d",
                   (int)app->tally.events_sent,
                   (int)app->tally.states_sent);
    set_text(screen, screen->sent, text);
    (void)snprintf(text, sizeof(text), "ส่งไม่ผ่าน %d | คำสั่งเสีย %d",
                   (int)app->tally.failures,
                   (int)app->tally.bad_commands);
    set_text(screen, screen->errors, text);
}

/* Job 3: a command from the web page; then is the link still up?
 * The firmware never reconnects by itself. */
static bento_status_t serve_commands(app_t *app)
{
    uint8_t raw[MESSAGE_KEEP];
    uint32_t len = 0U;
    uint32_t state = BENTO_MQTT_STATE_CLOSED;
    bento_status_t status = BENTO_OK;

    if (receive(app, raw, &len)) {
        status = handle(app, raw, len);
        show_counts(app);
    }
    if ((status == BENTO_OK) &&
        ((bento_mqtt_state(app->link.client, &state) != BENTO_OK) ||
         (state != BENTO_MQTT_STATE_CONNECTED))) {
        app->lost = true;
    }
    return status;
}

/* Layout on the 792 x 398 canvas. */
enum {
    LEFT_X = 24,
    INSET_X = 40,
    TITLE_Y = 8,
    STATUS_X = 344,
    STATUS_Y = 16,
    CARD_W = 744,
    CARD_1_Y = 56,
    CARD_1_H = 120,
    CARD_2_Y = 192,
    CARD_2_H = 128,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CAPTION_1_Y = 70,
    SEG_Y = 104,
    SEG_W = 144,
    SEG_H = 56,
    COUNTS_X = 224,
    ERRORS_Y = 138,
    CAPTION_2_Y = 204,
    LEDS_Y = 232,
    RIGHT_X = 400,
    TOPIC_Y = 280,
    NOTE_Y = 334,
    TITLE_PX = 24,
    TEXT_PX = 20
};

/* ---------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------- */

static int16_t px(int32_t value)
{
    return (int16_t)value;
}

/* ui.poll() here only empties the queue; it answers no events
 * when it fails. */
static void drain_events(const screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
}

/* ---------------------------------------------------------------
 * The screen
 * ------------------------------------------------------------- */

static bento_status_t add_label(const screen_t *screen,
                                const char *text, const int16_t spot[2],
                                uint32_t color, int32_t font_px,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = spot[0];
    args.y = spot[1];
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(screen->ui, &args, out_id);
}

/* A label in 20 px that the program never changes. */
static bento_status_t add_caption(const screen_t *screen,
                                  const char *text, int32_t x,
                                  int32_t y)
{
    const int16_t spot[2] = {px(x), px(y)};
    bento_ui_id_t label;

    return add_label(screen, text, spot, COL_DIM, TEXT_PX, &label);
}

static bento_status_t add_card(const screen_t *screen, int32_t y,
                               int32_t h)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = LEFT_X;
    args.y = px(y);
    args.w = CARD_W;
    args.h = px(h);
    args.color = COL_CARD;
    args.min_val = COL_CARD; /* the border colour */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(screen->ui, &args, &card);
}

/* The title, the status line and the counters card. */
static bento_status_t add_top(screen_t *screen)
{
    const int16_t title_spot[2] = {LEFT_X, TITLE_Y};
    const int16_t status_spot[2] = {STATUS_X, STATUS_Y};
    const int16_t sent_spot[2] = {COUNTS_X, SEG_Y};
    const int16_t errors_spot[2] = {COUNTS_X, ERRORS_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t title;
    bento_status_t status =
        add_label(screen, "ปุ่มกับไฟขึ้น broker", title_spot, COL_TEXT,
                  TITLE_PX, &title);

    if (status == BENTO_OK) {
        status = add_label(screen, "กำลังจะเริ่ม", status_spot, COL_DIM,
                           TEXT_PX, &screen->status);
    }
    if (status == BENTO_OK) {
        status = add_card(screen, CARD_1_Y, CARD_1_H);
    }
    if (status == BENTO_OK) {
        status =
            add_caption(screen, "กดไปแล้ว (ครั้ง)", INSET_X, CAPTION_1_Y);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "0";
        args.text_len = 1U;
        args.x = INSET_X;
        args.y = SEG_Y;
        args.w = SEG_W;
        args.h = SEG_H;
        args.color = COL_ACCENT;
        args.font_px = 0;
        status = bento_ui_seg7_create(screen->ui, &args, &screen->seg);
    }
    if (status == BENTO_OK) {
        status =
            add_caption(screen, "ส่งออกไปแล้ว", COUNTS_X, CAPTION_1_Y);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, "event 0 | telemetry 0", sent_spot,
                           COL_TEXT, TEXT_PX, &screen->sent);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, "ส่งไม่ผ่าน 0 | คำสั่งเสีย 0", errors_spot,
                           COL_DIM, TEXT_PX, &screen->errors);
    }
    return status;
}

/* The second card: leds, last command, topic and az. */
static bento_status_t add_bottom(screen_t *screen, const char *leds)
{
    const int16_t leds_spot[2] = {INSET_X, LEDS_Y};
    const int16_t last_spot[2] = {RIGHT_X, LEDS_Y};
    const int16_t topic_spot[2] = {INSET_X, TOPIC_Y};
    const int16_t az_spot[2] = {RIGHT_X, TOPIC_Y};
    bento_status_t status = add_card(screen, CARD_2_Y, CARD_2_H);

    if (status == BENTO_OK) {
        status = add_caption(screen, "leds ที่ส่งออก (จากตัวแปร)", INSET_X,
                             CAPTION_2_Y);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, leds, leds_spot, COL_TEXT, TITLE_PX,
                           &screen->leds);
    }
    if (status == BENTO_OK) {
        status =
            add_caption(screen, "คำสั่งล่าสุดจากเว็บ", RIGHT_X, CAPTION_2_Y);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, "ยังไม่มี", last_spot, COL_DIM,
                           TITLE_PX, &screen->last);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, "ยังไม่ได้ต่อ broker", topic_spot,
                           COL_DIM, TEXT_PX, &screen->topic);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, "az ยังไม่ได้อ่าน", az_spot, COL_DIM,
                           TEXT_PX, &screen->az);
    }
    return status;
}

static bento_status_t build_screen(app_t *app)
{
    const int16_t note_spot[2] = {LEFT_X, NOTE_Y};
    screen_t *screen = &app->screen;
    char leds[TEXT_BYTES];
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_top(screen);
    }
    if (status == BENTO_OK) {
        format_leds(app, leds, sizeof(leds));
        status = add_bottom(screen, leds);
    }
    if (status == BENTO_OK) {
        /* a new label takes 95 bytes; .text() takes 126 */
        status = add_label(screen, "ห้ามส่งของลับ", note_spot, COL_WARN,
                           TEXT_PX, &screen->note);
    }
    if (status == BENTO_OK) {
        set_text(screen, screen->note,
                 "broker สาธารณะ พอร์ต 1883 ไม่เข้ารหัส ห้ามส่งของลับ");
        drain_events(screen);
    }
    return status;
}

/* ---------------------------------------------------------------
 * Leaving early, politely
 * ------------------------------------------------------------- */

/* Says on the screen where the run stopped, and ends it. */
static void stop_here(app_t *app, const char *screen_text,
                      const char *log_text)
{
    const screen_t *screen = &app->screen;
    char line[LINE_BYTES];

    set_color(screen, screen->status, COL_BAD);
    set_text(screen, screen->status, screen_text);
    set_color(screen, screen->note, COL_BAD);
    set_text(screen, screen->note, log_text);
    drain_events(screen);
    (void)snprintf(line, sizeof(line), "<span class=error>%s</span>",
                   log_text);
    lcd_line(line);
    (void)printf("หยุดที่: %s\n", log_text);
    app->halted = true;
}

/* The time left, written once a second: every 5 ms would be 200
 * messages a second to the display core. */
static void show_time_left(const app_t *app, watch_t *watch,
                           uint32_t now_ms)
{
    char text[TEXT_BYTES];
    const int32_t second =
        bento_time_ticks_diff(now_ms, watch->start_ms) / MS_PER_S;

    if (second != watch->last_second) {
        watch->last_second = second;
        (void)snprintf(text, sizeof(text),
                       "ส่งอยู่ - เหลืออีก %d "
                       "วินาที",
                       (int)((RUN_MS / MS_PER_S) - second));
        set_text(&app->screen, app->screen.status, text);
    }
}

/* What the screen and the console say when the run is over. */
static void show_summary(const app_t *app)
{
    const screen_t *screen = &app->screen;
    const tally_t *tally = &app->tally;
    const int32_t lost_s =
        bento_time_ticks_diff(bento_time_ticks_ms(), app->start_ms) /
        MS_PER_S;
    char text[TEXT_BYTES];

    if (app->lost) {
        set_color(screen, screen->status, COL_BAD);
        set_text(screen, screen->status, "สายหลุด หยุดส่งแล้ว");
        set_color(screen, screen->note, COL_BAD);
        (void)snprintf(text, sizeof(text),
                       "หลุดวินาทีที่ %d กด %d ครั้ง "
                       "รันใหม่",
                       (int)lost_s, (int)tally->presses);
        set_text(screen, screen->note, text);
        lcd_line("<span class=error>สายหลุด ต้องรันใหม่</span>");
    } else {
        set_color(screen, screen->status, COL_OK);
        set_text(screen, screen->status, "ครบเวลาแล้ว - ดับไฟครบทุกดวง");
        set_color(screen, screen->note, COL_DIM);
        (void)snprintf(text, sizeof(text),
                       "กด %d ครั้ง | ส่ง %d ใบ | ส่งไม่ผ่าน %d",
                       (int)tally->presses, (int)tally->messages,
                       (int)tally->failures);
        set_text(screen, screen->note, text);
    }
    drain_events(screen);
}

/* The networks a scan heard: too big for the 8 KB program stack,
 * so file scope, zeroed by the loader and again in main()
 * (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern bento_wifi_ap_t g_heard[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_heard[BENTO_WIFI_SCAN_MAX];

/* A team name is "team" and two digits, and never team00: someone
 * else's name would knock that team off the broker. */
static bool team_is_valid(void)
{
    bool valid = (strlen(TEAM) == (size_t)TEAM_CHARS) &&
                 (strncmp(TEAM, "team", (size_t)PREFIX_CHARS) == 0) &&
                 (strcmp(TEAM, "team00") != 0);
    size_t i;

    for (i = (size_t)PREFIX_CHARS; valid && (i < (size_t)TEAM_CHARS);
         i++) {
        valid = (TEAM[i] >= '0') && (TEAM[i] <= '9');
    }
    return valid;
}

/* ---------------------------------------------------------------
 * The three steps: Wi-Fi, an address, the broker
 * ------------------------------------------------------------- */

/* wifi.connect(): *joined is its True or False. A refused or
 * timed-out join is False; any other failure ends the program,
 * as the binding's OSError does. */
static bento_status_t join_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
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
        status = wait_ms(WAIT_STEP_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_connect_poll();
        } else { /* Stop: leave the radio alone */
            (void)bento_wifi_connect_cancel(); /* nothing to report */
        }
    }
    *joined = (status == BENTO_OK);
    if ((status == BENTO_E_FAILED) || (status == BENTO_E_TIMEOUT)) {
        status = BENTO_OK;
    }
    if (*joined) {
        read_ip(address, (uint32_t)sizeof(address));
        (void)printf("[WiFi] Connected! IP=%s\r\n", address);
    }
    return status;
}

/* Did a scan hear our network? A scan that fails says no, as the
 * original's except OSError: pass does. */
static bento_status_t heard_network(bool *heard)
{
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = bento_wifi_scan_start();

    *heard = false;
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(WAIT_STEP_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_scan_poll(g_heard, BENTO_WIFI_SCAN_MAX,
                                          &count);
        } else {
            (void)bento_wifi_scan_cancel(); /* Stop: nothing to say */
        }
    }
    for (i = 0U; (status == BENTO_OK) && (i < count); i++) {
        if (strcmp(g_heard[i].ssid, WIFI_SSID) == 0) {
            *heard = true;
        }
    }
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* Step 1: the Wi-Fi. A failed join stops with the reason. */
static bento_status_t step_wifi(app_t *app)
{
    const screen_t *screen = &app->screen;
    char line[LINE_BYTES];
    bool joined = false;
    bool heard = false;
    bento_status_t status;

    /* the line must show before connect() blocks for up to 85 s */
    set_color(screen, screen->status, COL_WARN);
    set_text(screen, screen->status, "กำลังต่อ WiFi จอจะนิ่งสักครู่");
    drain_events(screen);
    (void)snprintf(line, sizeof(line), "1) กำลังต่อ WiFi %s", WIFI_SSID);
    lcd_line(line);
    status = join_wifi(&joined);
    if ((status == BENTO_OK) && !joined) {
        status = heard_network(&heard);
    }
    if ((status == BENTO_OK) && !joined) {
        /* .text() takes 126 bytes and a Thai letter is 3 */
        (void)snprintf(line, sizeof(line),
                       heard ? "ได้ยินวง %s แต่ต่อไม่ผ่าน ตรวจรหัสผ่าน"
                             : "ไม่ได้ยินวง %s ตรวจชื่อวง",
                       WIFI_SSID);
        stop_here(app, "ต่อ WiFi ไม่ติด", line);
    }
    return status;
}

/* Step 2: an address. "0.0.0.0" is a text, not an empty one, so
 * the original compares it; DHCP gets 15 x 200 ms. */
static bento_status_t step_address(app_t *app, char *ip, uint32_t size)
{
    int32_t tries = 0;
    bento_status_t status = BENTO_OK;

    read_ip(ip, size);
    while ((status == BENTO_OK) && (tries < IP_TRIES) &&
           (strcmp(ip, "0.0.0.0") == 0)) {
        drain_events(&app->screen);
        status = wait_ms(IP_WAIT_MS);
        read_ip(ip, size);
        tries++;
    }
    if ((status == BENTO_OK) && (strcmp(ip, "0.0.0.0") == 0)) {
        stop_here(app, "ไม่มีเลข IP", "ได้ลิงก์แต่ DHCP ไม่ให้เลข IP");
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
    (void)bento_ui_set_color(ui, step, COL_BAD);
    (void)bento_ui_set_text(ui, step, step_text,
                            (uint32_t)strlen(step_text));
    (void)bento_ui_set_color(ui, reason, COL_BAD);
    (void)bento_ui_set_text(ui, reason, note, (uint32_t)strlen(note));
    /* ui.poll() answers no events when it fails; none are read */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
    (void)bento_lcd_print(note); /* ignores a console not ready */
    (void)printf("MQTT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit "
                 "หรือ Eva Kit / MQTT from C is not in the BENTO "
                 "Emulator yet: run it on a Dev Kit or Eva Kit.\n");
}

/* mqtt.connect(): *started is false when no client could even
 * start (the binding's OSError), *linked is its True or False. */
static bento_status_t open_broker(link_t *link, bool *started,
                                  bool *linked)
{
    bento_mqtt_config_t config;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = (uint16_t)BROKER_PORT;
    config.client_id = link->device_id;
    config.client_id_len = (uint32_t)strlen(link->device_id);
    (void)printf("[MQTT] Connecting to %s:%d ...\r\n", BROKER,
                 BROKER_PORT);
    status = bento_mqtt_open_start(&config, &link->client);
    *started = (status == BENTO_OK);
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(WAIT_STEP_MS);
        if (status == BENTO_OK) {
            status = bento_mqtt_open_poll(link->client);
        }
    }
    *linked = (status == BENTO_OK);
    if (*linked) {
        (void)printf("[MQTT] Connected to %s:%d as '%s'\r\n", BROKER,
                     BROKER_PORT, link->device_id);
    } else if (link->client != 0U) {
        /* the binding deletes a failed client at once */
        (void)bento_mqtt_close(link->client); /* freed either way */
        link->client = 0U;
    } else {
        /* no client started */
    }
    /* Stop, and the Emulator's missing MQTT, go back to the caller */
    return ((status == BENTO_E_CANCELLED) ||
            (status == BENTO_E_UNSUPPORTED))
               ? status
               : BENTO_OK;
}

/* Step 3: the broker, then the command topic. */
static bento_status_t step_broker(app_t *app)
{
    char line[LINE_BYTES];
    bool started = false;
    bool linked = false;
    bento_status_t status = open_broker(&app->link, &started, &linked);

    if (status == BENTO_E_UNSUPPORTED) {
        show_emulator_note(app->screen.ui, app->screen.status,
                           "broker ไม่ตอบ", app->screen.note);
        app->halted = true;
        status = BENTO_OK;
    } else if ((status == BENTO_OK) && !started) {
        (void)snprintf(line, sizeof(line), "ต่อ %s ไม่ได้ ลองตัวสำรอง",
                       BROKER);
        stop_here(app, "broker ไม่ตอบ", line);
    } else if ((status == BENTO_OK) && !linked) {
        stop_here(app, "broker ปฏิเสธ", "ตรวจชื่อ broker กับพอร์ต 1883");
    } else if ((status == BENTO_OK) &&
               !listen_for_commands(&app->link)) {
        (void)snprintf(line, sizeof(line), "broker ไม่ยอมให้ฟัง %s",
                       app->link.topic_cmd);
        stop_here(app, "subscribe ไม่ผ่าน", line);
    } else {
        /* linked and listening, or Stop */
    }
    return status;
}

/* The three steps; each runs only while none has stopped. */
static bento_status_t connect_all(app_t *app)
{
    const screen_t *screen = &app->screen;
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    char line[LINE_BYTES];
    bento_status_t status = step_wifi(app);

    if ((status == BENTO_OK) && !app->halted) {
        status = step_address(app, ip, (uint32_t)sizeof(ip));
    }
    if ((status == BENTO_OK) && !app->halted) {
        (void)snprintf(line, sizeof(line), "IP %s กำลังต่อ broker", ip);
        set_text(screen, screen->status, line);
        drain_events(screen);
        (void)snprintf(line, sizeof(line), "2) ได้ IP %s กำลังต่อ %s", ip,
                       BROKER);
        lcd_line(line);
        status = step_broker(app);
    }
    if ((status == BENTO_OK) && !app->halted) {
        set_color(screen, screen->status, COL_OK);
        set_text(screen, screen->status, "ต่อแล้ว กดปุ่มบนบอร์ดได้เลย");
        set_color(screen, screen->topic, COL_TEXT);
        (void)snprintf(line, sizeof(line), "%s/%s/#", ROOT, TEAM);
        set_text(screen, screen->topic, line);
        drain_events(screen);
        (void)snprintf(line, sizeof(line), "%s %s </span>",
                       "<span class=ok>3) ต่อ broker แล้ว ฟัง",
                       app->link.topic_cmd);
        lcd_line(line);
    }
    return status;
}

/* ---------------------------------------------------------------
 * Sending: json.dumps and publish
 * ------------------------------------------------------------- */

/* send(): adds "id" and "n", then publishes. false when the link
 * is gone (publish raised OSError); a publish the network refused
 * is counted as a failure and the run goes on. */
static bool send(app_t *app, const char *topic, message_t *message)
{
    char payload[PAYLOAD_BYTES];
    bento_status_t status;

    app->tally.messages++;
    (void)snprintf(message->values[KEY_ID], VALUE_BYTES, "\"%s\"",
                   TEAM);
    (void)snprintf(message->values[KEY_N], VALUE_BYTES, "%d",
                   (int)app->tally.messages);
    write_json(message, payload, sizeof(payload));
    status = bento_mqtt_publish(
        app->link.client, topic, (uint32_t)strlen(topic),
        (const uint8_t *)payload, (uint32_t)strlen(payload), 0U, 0U);
    if ((status != BENTO_OK) && (status != BENTO_E_SESSION)) {
        app->tally.failures++;
    }
    return status != BENTO_E_SESSION;
}

/* The event message for a press. The order is the dict's slot
 * order (header); 1-byte and 2-byte hashes agree here. */
static bool send_press(app_t *app)
{
    static const uint8_t order[] = {KEY_PRESSES, KEY_ID, KEY_EV, KEY_N};
    message_t message;

    message.order = order;
    message.count = (uint32_t)sizeof(order);
    (void)snprintf(message.values[KEY_EV], VALUE_BYTES, "\"press\"");
    (void)snprintf(message.values[KEY_PRESSES], VALUE_BYTES, "%d",
                   (int)app->tally.presses);
    return send(app, app->link.topic_event, &message);
}

/* The telemetry message: presses, the debounced button, the leds
 * and, when it could be read, az (also shown on the screen). */
static bool send_state(app_t *app, bool pressed)
{
    const screen_t *screen = &app->screen;
    char az[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    message_t message;
    const bool has_az = read_az(az, (uint32_t)sizeof(az));

    message.order = state_order(has_az, &message.count);
    (void)snprintf(message.values[KEY_PRESSES], VALUE_BYTES, "%d",
                   (int)app->tally.presses);
    (void)snprintf(message.values[KEY_BUTTON], VALUE_BYTES, "%d",
                   pressed ? 1 : 0);
    format_list(app, message.values[KEY_LEDS], VALUE_BYTES);
    if (has_az) {
        (void)snprintf(message.values[KEY_AZ], VALUE_BYTES, "%s", az);
        (void)snprintf(text, sizeof(text), "az = %s m/s2", az);
        set_text(screen, screen->az, text);
    }
    return send(app, app->link.topic_state, &message);
}

/* ---------------------------------------------------------------
 * The run: three jobs every 5 ms for 3 minutes
 * ------------------------------------------------------------- */

/* Job 1: the button. Only the edge where a press starts sends. */
static bento_status_t watch_button(app_t *app, watch_t *watch,
                                   uint32_t now_ms)
{
    char text[TEXT_BYTES];
    bool raw = false;
    bento_status_t status = bento_gpio_button_is_pressed(0U, &raw);

    if ((status == BENTO_OK) && (raw != watch->last_raw)) {
        watch->last_raw = raw;
        watch->last_change_ms = now_ms;
    } else if ((status == BENTO_OK) && (raw != watch->stable) &&
               (bento_time_ticks_diff(now_ms, watch->last_change_ms) >=
                DEBOUNCE_MS)) {
        watch->stable = raw;
        if (raw) {
            app->tally.presses++;
            (void)snprintf(text, sizeof(text), "%d",
                           (int)app->tally.presses);
            set_text(&app->screen, app->screen.seg, text);
            app->lost = !send_press(app);
        }
        if (raw && !app->lost) {
            app->tally.events_sent++;
            show_counts(app);
            (void)snprintf(text, sizeof(text), "กดครั้งที่ %d -> event",
                           (int)app->tally.presses);
            lcd_line(text);
        }
    } else {
        /* no change, or the pin is still settling */
    }
    return status;
}

/* Job 2: telemetry by the clock. Without retain, a page opened
 * just now waits for the next one. */
static void send_telemetry(app_t *app, watch_t *watch, uint32_t now_ms)
{
    if (bento_time_ticks_diff(now_ms, watch->last_state_ms) >=
        STATE_MS) {
        watch->last_state_ms = now_ms;
        app->lost = !send_state(app, watch->stable);
        if (!app->lost) {
            app->tally.states_sent++;
            show_counts(app);
        }
    }
}

/* One round of the loop; app->lost ends the run. */
static bento_status_t run_round(app_t *app, watch_t *watch)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    bento_status_t status = watch_button(app, watch, now_ms);

    if ((status == BENTO_OK) && !app->lost) {
        send_telemetry(app, watch, now_ms);
    }
    if ((status == BENTO_OK) && !app->lost) {
        status = serve_commands(app);
    }
    if ((status == BENTO_OK) && !app->lost) {
        show_time_left(app, watch, now_ms);
        drain_events(&app->screen);
        status = wait_ms(POLL_MS);
    }
    return status;
}

static bento_status_t run_loop(app_t *app)
{
    watch_t watch;
    bool pressed = false;
    bento_status_t status = bento_gpio_button_is_pressed(0U, &pressed);

    watch.stable = pressed;
    watch.last_raw = pressed;
    watch.start_ms = bento_time_ticks_ms();
    watch.last_change_ms = watch.start_ms;
    watch.last_state_ms =
        bento_time_ticks_add(watch.start_ms, -STATE_MS);
    watch.last_second = -1;
    while ((status == BENTO_OK) && !app->lost &&
           (bento_time_ticks_diff(bento_time_ticks_ms(),
                                  watch.start_ms) < RUN_MS)) {
        status = run_round(app, &watch);
    }
    app->start_ms = watch.start_ms;
    return status;
}

/* ---------------------------------------------------------------
 * The end, and the start
 * ------------------------------------------------------------- */

/* The run after the three steps: the loop, then lights out. */
static bento_status_t run_and_finish(app_t *app)
{
    char text[TEXT_BYTES];
    bento_status_t status = run_loop(app);

    if (status == BENTO_OK) {
        status = leds_off(app);
    }
    if (status == BENTO_OK) {
        format_leds(app, text, sizeof(text));
        set_text(&app->screen, app->screen.leds, text);
        show_summary(app);
        print_summary(&app->tally);
    }
    return status;
}

/* The names the team uses on the broker. */
static void make_names(link_t *link)
{
    /* the firmware cuts a client_id at 31 */
    (void)snprintf(link->device_id, sizeof(link->device_id),
                   "bento-aiot-%s", TEAM);
    (void)snprintf(link->topic_event, sizeof(link->topic_event),
                   "%s/%s/event", ROOT, TEAM);
    (void)snprintf(link->topic_state, sizeof(link->topic_state),
                   "%s/%s/telemetry", ROOT, TEAM);
    (void)snprintf(link->topic_cmd, sizeof(link->topic_cmd),
                   "%s/%s/cmd", ROOT, TEAM);
}

int main(void)
{
    app_t app;
    bento_status_t status;

    (void)memset(&app, 0, sizeof(app));
    (void)memset(g_heard, 0, sizeof(g_heard));
    app.screen.ui = bento_runtime_get_ui();
    app.led_count = bento_gpio_num_leds();
    make_names(&app.link);
    /* gpio.button(0) refuses a board without a button */
    status = (bento_gpio_num_buttons() > 0U) ? BENTO_OK
                                             : BENTO_E_INVALID_ARGUMENT;
    if (status == BENTO_OK) { /* the real LEDs match the variable */
        status = leds_off(&app);
    }
    if (status == BENTO_OK) {
        status = build_screen(&app);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_clear(); /* ignored when not ready */
        (void)bento_lcd_console("<h2>ปุ่มกับไฟขึ้น broker</h2>");
        if (!team_is_valid()) {
            stop_here(&app, "ยังไม่ได้ตั้งชื่อทีม",
                      "แก้ TEAM เป็นเลขทีมของคุณก่อน เช่น team03");
        } else {
            status = connect_all(&app);
        }
    }
    if ((status == BENTO_OK) && !app.halted) {
        status = run_and_finish(&app);
    }
    if (app.link.client != 0U) {
        /* the binding keeps the link until the next connect; a C
         * client left open would hold the only slot until a reset */
        (void)bento_mqtt_close(app.link.client); /* nothing to say */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
