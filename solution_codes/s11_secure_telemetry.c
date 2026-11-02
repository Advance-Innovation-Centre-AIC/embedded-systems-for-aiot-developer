/*
 * s11_secure_telemetry.c -- ส่ง telemetry ขึ้นแพลตฟอร์มผ่าน TLS
 *
 * ตั้งตัวตนของอุปกรณ์ (device_id, api_key, mqtt_pass, broker,
 * sni_hostname) แล้วกางหน้าจอ ซ้ายคือตารางตัวตนที่บอร์ดใช้แนะนำตัว
 * ขวาบนคือไฟสามดวงของการจับมือ TLS ขวากลางคือปุ่มต่อใหม่/ตัดสาย
 * ขวาล่างคือเวลาที่ใช้จับมือเทียบเพดาน 30 วินาที สั่งต่อแล้วรอ
 * is_connected() ไม่เกิน 30 วินาที จากนั้นส่งค่า accel_x heading
 * และ pot ขึ้นแพลตฟอร์มทุก 5 วินาที โดยเช็กสายก่อนทุกรอบส่ง ตัดสาย
 * ต้องยืนยันในกล่องก่อน สายหลุดแล้วไฟแดงติดและบอกจำนวนที่ส่งไป
 * วิธีใช้: แก้ห้าค่าบนหัวไฟล์ให้เป็นของทีม แล้วกด Program to Device
 *
 * Sets the device identity (device_id, api_key, mqtt_pass, broker,
 * sni_hostname), then draws the screen: on the left the identity
 * table the board introduces itself with, on the right three TLS
 * handshake lamps, the reconnect and disconnect buttons, and the
 * handshake time against its 30 second ceiling. It connects, waits
 * up to 30 s for is_connected(), then sends accel_x, heading and pot
 * to the platform every 5 seconds, checking the link before every
 * send. Disconnecting asks for a confirmation first. When the link
 * drops, the red lamp lights and the count of messages is printed.
 * Edit the five values at the top for your team, then Program to
 * Device.
 *
 * Twin of s11_secure_telemetry.py
 * (embedded-systems-for-aiot-developer/solution_codes/s11_secure_telemetry.py).
 * Style: CLEAN_C.md.
 *
 * Differs from the original, where C or the C API forces it:
 *   - There is no json module in C: the payload is written with
 *     snprintf as json.dumps writes it, keys in the PSoC Edge
 *     MicroPython dict order (heading, accel_x, pot: py/map.c with
 *     one hash byte), ", " and ": " between them, each real number as
 *     repr() prints it (bento_fmt_real). round(x, n) is
 *     nearbyint(x * 10^n) / 10^n in the board's float width, as
 *     py/modbuiltins.c computes it. pot.percent() is the C API's real
 *     number (percent_x10 / 10 on the Eva Kit).
 *   - tesaiot.config() builds a dict. print() of it is written key by
 *     key from the C key table (the same order as the dict), each
 *     value as text: the eight number keys unquoted, the rest as
 *     repr() of a str. The other two reads use only tls_mode, so only
 *     that field is asked for.
 *   - tesaiot.config_set(), connect(), publish() and disconnect()
 *     answer True or False, which the original ignores; here BENTO_OK
 *     and BENTO_E_FAILED are ignored the same way. Any other status
 *     of config_set() or the first connect() (no TESAIoT service in
 *     this build, where MicroPython fails at import tesaiot) ends the
 *     program.
 *   - sensors.bmi270, bmm350 and pot raise OSError on a failed read;
 *     here every failing status takes the except path, and
 *     BENTO_E_UNSUPPORTED (no such sensor in this build: an
 *     AttributeError nothing catches) ends the program.
 *   - Stop ends the program after the next sleep, where MicroPython
 *     raises KeyboardInterrupt.
 *   - In the BENTO Emulator, the TESAIoT platform from C is not
 *     available yet: the program says so (a label under the table,
 *     where the mqtt_pass row would be, which was not set, and a
 *     printed line) and then runs as when the
 *     platform cannot be reached: it waits the 30 s, lights the failed
 *     lamp and prints that 0 messages were sent.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_tesaiot.h"
#include "bento_time.h"

/* Each multiply and add is rounded on its own, as MicroPython does. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

static const char TEAM_NAME[] = "BentoBuilders";
static const char DEVICE_ID[] =
    "team03"; /* registered, under 31 chars */
static const char BROKER[] = "<โฮสต์แพลตฟอร์ม>";

enum { /* times */
    SETTLE_MS = 200,
    WAIT_CEILING_S = 30, /* the wait loop's ceiling, on screen too */
    MS_PER_S = 1000,
    WAIT_STEP_MS = 500,
    RETRY_READ_MS = 1000,
    SEND_EVERY_MS = 5000
};

enum { /* text sizes */
    TEXT_BYTES = 128,
    VALUE_BYTES = (int32_t)BENTO_TESAIOT_CONFIG_VALUE_MAX + 1,
    KEY_BYTES = (int32_t)BENTO_TESAIOT_CONFIG_KEY_MAX + 1,
    NUMBER_BYTES = BENTO_FMT_BUF_SIZE,
    PAYLOAD_BYTES = 160
};

enum { /* layout that more than one widget shares */
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    TABLE_COLUMNS = 2,
    LEFT_COLUMN_W = 160,
    RIGHT_COLUMN_W = 224,
    SCALE_TICKS = 7,
    SCALE_MAJOR_EVERY = 3,
    ACCEL_DIGITS = 2,
    HEADING_DIGITS = 1,
    DECIMAL_BASE = 10
};

enum { /* the title, and the identity table's rows in order */
    TITLE_X = 24,
    TITLE_Y = 8,
    TITLE_PX = 20,
    ROW_DEVICE_ID = 0,
    ROW_BROKER = 1,
    ROW_TLS_MODE = 2,
    ROW_MQTT_PASS = 3
};

enum { /* colours, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_RUN = 0x4A9EFF,
    COLOR_BAD = 0xE5484D,
    COLOR_CONNECT = 0x30A46C,
    COLOR_QUIET = 0x3A4150
};

/* A widget as the original creates it; w and h -1 = automatic. */
typedef struct {
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value;
} widget_spec_t;

/* The widgets the program changes after it has drawn them. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t sent_label;
    bento_ui_id_t lamp_wait;
    bento_ui_id_t lamp_ok;
    bento_ui_id_t lamp_fail;
    bento_ui_id_t connect_button;
    bento_ui_id_t disconnect_button;
    bento_ui_id_t seconds_label;
    bento_ui_id_t seconds_bar;
    bento_ui_id_t box;
    bento_ui_id_t yes_button;
    bento_ui_id_t no_button;
    bool no_platform; /* no TESAIoT service: the BENTO Emulator */
} tls_screen_t;

/* The publish loop's own values. */
typedef struct {
    int32_t sent;
    bool asking;
} session_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* ui.poll() only keeps the queue moving here; a failed poll answers
 * no events. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
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

/* show() and hide() are setters too: a refusal is ignored. */
static void set_shown(bento_ui_t *ui, bento_ui_id_t widget, bool shown)
{
    (void)bento_ui_set_visible(ui, widget, shown ? 1U : 0U);
}

static void args_from(bento_ui_widget_args_t *args,
                      const widget_spec_t *spec)
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

static bento_status_t add_label(bento_ui_t *ui,
                                const widget_spec_t *spec,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    args_from(&args, spec);
    return bento_ui_label_create(ui, &args, out_id);
}

/* A card: drawn before what sits on it, or it paints over them. */
static bento_status_t add_card(bento_ui_t *ui,
                               const widget_spec_t *spec)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card = 0U;

    args_from(&args, spec);
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM;
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* Labels that never change, in the order the original draws them. */
static bento_status_t
add_labels(bento_ui_t *ui, const widget_spec_t *specs, size_t count)
{
    bento_ui_id_t unused = 0U;
    bento_status_t status = BENTO_OK;
    size_t i;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = add_label(ui, &specs[i], &unused);
    }
    return status;
}

/* str of a str is itself; repr() puts it in quotes with the quote,
 * the backslash and control bytes escaped (py/objstr.c). */
static void print_repr(const char *text)
{
    const char quote =
        ((strchr(text, '\'') != NULL) && (strchr(text, '"') == NULL))
            ? '"'
            : '\'';
    const uint8_t printable_min = 0x20U;
    const uint8_t delete_byte = 0x7FU;
    size_t i;

    (void)printf("%c", quote);
    for (i = 0U; text[i] != '\0'; i++) {
        const uint8_t byte = (uint8_t)text[i];

        if ((text[i] == quote) || (text[i] == '\\')) {
            (void)printf("\\%c", text[i]);
        } else if ((byte >= printable_min) && (byte != delete_byte)) {
            (void)printf("%c", text[i]);
        } else if (text[i] == '\n') {
            (void)printf("\\n");
        } else if (text[i] == '\r') {
            (void)printf("\\r");
        } else if (text[i] == '\t') {
            (void)printf("\\t");
        } else {
            (void)printf("\\x%02x", (unsigned int)byte);
        }
    }
    (void)printf("%c", quote);
}

/* The config keys whose value is an int in the dict (modtesaiot.c). */
static bool is_number_key(const char *key)
{
    static const char *const number_keys[] = {
        "port",       "qos",         "keepalive",
        "timeout_ms", "max_retries", "retry_interval_ms",
        "api_port",   "debug_level"};
    bool found = false;
    size_t i;

    for (i = 0U; i < (sizeof(number_keys) / sizeof(number_keys[0]));
         i++) {
        found = found || (strcmp(key, number_keys[i]) == 0);
    }
    return found;
}

/* One config field as text; api_key is printed as the dict holds it. */
static bento_status_t read_field(const char *key, char *value,
                                 uint32_t cap)
{
    uint32_t length = 0U;

    return bento_tesaiot_config_get_field(
        key, (uint32_t)strlen(key), BENTO_TESAIOT_CONFIG_REVEAL_SECRET,
        value, cap, &length);
}

/* One "'key': value" of the dict's repr. */
static void print_entry(uint32_t index, const char *key,
                        const char *value)
{
    (void)printf("%s", (index > 0U) ? ", " : "");
    print_repr(key);
    (void)printf(": ");
    if (is_number_key(key)) {
        (void)printf("%s", value);
    } else {
        print_repr(value);
    }
}

/* print("config ปัจจุบัน:", tesaiot.config()) */
static bento_status_t print_config(void)
{
    char key[KEY_BYTES];
    char value[VALUE_BYTES];
    uint32_t count = 0U;
    uint32_t length = 0U;
    uint32_t i;
    bento_status_t status = bento_tesaiot_config_key_count(&count);

    if (status == BENTO_OK) {
        (void)printf("config ปัจจุบัน: {");
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = bento_tesaiot_config_key_at(
            i, key, (uint32_t)sizeof(key), &length);
        if (status == BENTO_OK) {
            status = read_field(key, value, (uint32_t)sizeof(value));
        }
        if (status == BENTO_OK) {
            print_entry(i, key, value);
        }
    }
    if (status == BENTO_OK) {
        (void)printf("}\n");
    }
    return status;
}

/* tesaiot.config_set(): its True or False is ignored, as in the
 * original; any other status means no TESAIoT service. */
static bento_status_t set_field(const char *key, const char *value)
{
    bento_status_t status = bento_tesaiot_config_set(
        key, (uint32_t)strlen(key), value, (uint32_t)strlen(value));

    if (status == BENTO_E_FAILED) {
        status = BENTO_OK;
    }
    return status;
}

/* The identity is these three together, and sni_hostname must name
 * the broker's host, or the server shows the wrong certificate and
 * the link fails without a word. */
static bento_status_t set_identity(void)
{
    /* the team's own key and MQTT password: edit both before the run */
    static const char api_key[] = "<api key ของทีม>";
    static const char mqtt_pass[] = "<รหัสผ่าน MQTT 16 ตัว>";
    bento_status_t status = set_field("device_id", DEVICE_ID);

    if (status == BENTO_OK) {
        status = set_field("api_key", api_key);
    }
    if (status == BENTO_OK) {
        status = set_field("mqtt_pass", mqtt_pass);
    }
    if (status == BENTO_OK) {
        status = set_field("broker", BROKER);
    }
    if (status == BENTO_OK) {
        status = set_field("sni_hostname", BROKER);
    }
    return status;
}

/* One table row: two cells, each ended by its NUL. */
static void add_row(bento_ui_t *ui, bento_ui_id_t table, uint8_t row,
                    const char *left, const char *right)
{
    char cells[TEXT_BYTES];
    const size_t left_bytes = strlen(left) + 1U;
    const size_t right_bytes = strlen(right) + 1U;

    if ((left_bytes + right_bytes) <= sizeof(cells)) {
        (void)memcpy(cells, left, left_bytes);
        (void)memcpy(&cells[left_bytes], right, right_bytes);
        /* a row is sent and forgotten, as modui.c does */
        (void)bento_ui_add_row(ui, table, row, cells,
                               (uint32_t)(left_bytes + right_bytes));
    }
}

/* The BENTO Emulator has no TESAIoT platform from C yet: say so on the screen
 * and in the Console. */
static bento_status_t add_emulator_notice(bento_ui_t *ui)
{
    /* The line breaks are in the text: the Emulator does not wrap a
     * label at a width, so all of it stays on the screen. */
    static const widget_spec_t notice = {
        "TESAIoT จาก C ยังไม่มีใน BENTO Emulator\nให้รันบน Dev Kit หรือ Eva "
        "Kit",
        40,
        250,
        -1,
        -1,
        COLOR_TEXT,
        20};
    bento_ui_id_t unused = 0U;

    (void)printf(
        "TESAIoT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva "
        "Kit / TESAIoT platform from C is not in the BENTO Emulator "
        "yet: run it on a Dev Kit or Eva Kit.\n");
    return add_label(ui, &notice, &unused);
}

/* The left card: the identity, with mqtt_pass written as set but
 * unreadable, because an empty cell would read as "not set". */
static bento_status_t add_identity(bento_ui_t *ui, bool no_platform)
{
    static const widget_spec_t card = {NULL, 24,         48, 440,
                                       344,  COLOR_CARD, 1};
    static const widget_spec_t title = {
        "ตัวตนที่บอร์ดใช้แนะนำตัว", 40, 60, -1, -1, COLOR_DIM, 20};
    static const widget_spec_t table_spec = {NULL, 40, 96, 408,
                                             288,  0U, 0};
    char mode[VALUE_BYTES];
    bento_ui_widget_args_t args;
    bento_ui_id_t table = 0U;
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_card(ui, &card);

    if (status == BENTO_OK) {
        status = add_label(ui, &title, &unused);
    }
    mode[0] = '\0'; /* nothing to read back without the platform */
    if ((status == BENTO_OK) && !no_platform) {
        status = read_field("tls_mode", mode, (uint32_t)sizeof(mode));
    }
    if (status == BENTO_OK) {
        args_from(&args, &table_spec);
        args.min_val = TABLE_COLUMNS;
        status = bento_ui_table_create(ui, &args, &table);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_col_width(ui, table, 0, LEFT_COLUMN_W);
        (void)bento_ui_set_col_width(ui, table, 1, RIGHT_COLUMN_W);
        add_row(ui, table, ROW_DEVICE_ID, "device_id", DEVICE_ID);
        add_row(ui, table, ROW_BROKER, "broker", BROKER);
        add_row(ui, table, ROW_TLS_MODE, "tls_mode", mode);
    }
    if ((status == BENTO_OK) && no_platform) {
        status = add_emulator_notice(ui);
    } else if (status == BENTO_OK) {
        add_row(ui, table, ROW_MQTT_PASS, "mqtt_pass", "ตั้งแล้ว ดูไม่ได้");
    } else {
        /* the card or the table failed: the status ends the program */
    }
    return status;
}

/* The three handshake lamps: one lit at a time. */
static bento_status_t add_lamps(tls_screen_t *screen)
{
    static const widget_spec_t lamps[] = {
        {NULL, 496, 96, 48, 48, COLOR_RUN, 1},
        {NULL, 592, 96, 48, 48, COLOR_OK, 0},
        {NULL, 688, 96, 48, 48, COLOR_BAD, 0},
    };
    static const widget_spec_t names[] = {
        {"กำลังต่อ", 496, 148, -1, -1, COLOR_DIM, 20},
        {"สำเร็จ", 592, 148, -1, -1, COLOR_DIM, 20},
        {"ล้มเหลว", 672, 148, -1, -1, COLOR_DIM, 20},
    };
    bento_ui_id_t *const ids[] = {&screen->lamp_wait, &screen->lamp_ok,
                                  &screen->lamp_fail};
    bento_ui_widget_args_t args;
    bento_status_t status = BENTO_OK;
    size_t i;

    for (i = 0U; (i < (sizeof(lamps) / sizeof(lamps[0]))) &&
                 (status == BENTO_OK);
         i++) {
        args_from(&args, &lamps[i]);
        status = bento_ui_led_create(screen->ui, &args, ids[i]);
    }
    if (status == BENTO_OK) {
        status = add_labels(screen->ui, names,
                            sizeof(names) / sizeof(names[0]));
    }
    return status;
}

/* Two separate buttons above the meter: lower down they would cover
 * the corner the firmware keeps for its Console button. */
static bento_status_t add_buttons(tls_screen_t *screen)
{
    static const widget_spec_t connect = {
        "ต่อใหม่", 496, 184, 88, 88, COLOR_CONNECT, 20};
    static const widget_spec_t disconnect = {
        "ตัดสาย", 616, 184, 88, 88, COLOR_QUIET, 20};
    bento_ui_widget_args_t args;
    bento_status_t status;

    args_from(&args, &connect);
    status = bento_ui_button_create(screen->ui, &args,
                                    &screen->connect_button);
    if (status == BENTO_OK) {
        args_from(&args, &disconnect);
        status = bento_ui_button_create(screen->ui, &args,
                                        &screen->disconnect_button);
    }
    return status;
}

/* The handshake time against the ceiling the code really uses. The
 * Scale is only a ruler; the Bar above it moves. */
static bento_status_t add_meter(tls_screen_t *screen)
{
    static const widget_spec_t seconds = {"0 วิ", 496,        284, -1,
                                          -1,    COLOR_TEXT, 20};
    static const widget_spec_t ceiling = {
        "เพดาน 30 วิ", 592, 284, -1, -1, COLOR_DIM, 20};
    static const widget_spec_t bar = {NULL, 496,       320, 192,
                                      12,   COLOR_RUN, 0};
    static const widget_spec_t ruler = {NULL, 496,        340, 192,
                                        44,   COLOR_TEXT, 0};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status =
        add_label(screen->ui, &seconds, &screen->seconds_label);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, &ceiling, &unused);
    }
    if (status == BENTO_OK) {
        args_from(&args, &bar);
        args.min_val = 0;
        args.max_val = WAIT_CEILING_S;
        status = bento_ui_bar_create(screen->ui, &args,
                                     &screen->seconds_bar);
    }
    if (status == BENTO_OK) {
        args_from(&args, &ruler);
        args.min_val = 0;
        args.max_val = WAIT_CEILING_S;
        status = bento_ui_scale_create(screen->ui, &args, &unused);
    }
    if (status == BENTO_OK) { /* room for three numbers: 0 15 30 */
        (void)bento_ui_set_ticks(screen->ui, unused, SCALE_TICKS,
                                 SCALE_MAJOR_EVERY);
    }
    return status;
}

/* The confirmation says what will happen. The MsgBox's own buttons
 * send no events yet, so two real buttons stand in; all three are
 * made now and hidden. */
static bento_status_t add_confirm_box(tls_screen_t *screen)
{
    static const widget_spec_t box = {
        "ตัดสาย\nต้องจับมือ TLS ใหม่ทั้งชุด", 112, 96, 568, 136, COLOR_CARD, 0};
    static const widget_spec_t yes = {"ตัดสาย", 144,         248, 200,
                                      88,      COLOR_QUIET, 20};
    static const widget_spec_t no = {"ไม่ตัด", 376,         248, 200,
                                     88,     COLOR_QUIET, 20};
    bento_ui_widget_args_t args;
    bento_status_t status;

    args_from(&args, &box);
    status = bento_ui_msgbox_create(screen->ui, &args, &screen->box);
    if (status == BENTO_OK) {
        args_from(&args, &yes);
        status = bento_ui_button_create(screen->ui, &args,
                                        &screen->yes_button);
    }
    if (status == BENTO_OK) {
        args_from(&args, &no);
        status = bento_ui_button_create(screen->ui, &args,
                                        &screen->no_button);
    }
    if (status == BENTO_OK) {
        set_shown(screen->ui, screen->box, false);
        set_shown(screen->ui, screen->yes_button, false);
        set_shown(screen->ui, screen->no_button, false);
        drain_events(screen->ui);
    }
    return status;
}

/* The title bar, the left card, then the right card. */
static bento_status_t build_screen(tls_screen_t *screen)
{
    static const widget_spec_t sent = {"ส่งแล้ว 0 ใบ", 560,       8, -1,
                                       -1,           COLOR_DIM, 20};
    static const widget_spec_t right_card = {NULL, 480,        48, 288,
                                             344,  COLOR_CARD, 1};
    static const widget_spec_t handshake = {
        "การจับมือ TLS", 496, 60, -1, -1, COLOR_DIM, 20};
    char title_text[TEXT_BYTES];
    widget_spec_t title = {NULL, TITLE_X,    TITLE_Y, -1,
                           -1,   COLOR_TEXT, TITLE_PX};
    bento_ui_id_t unused = 0U;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    (void)snprintf(title_text, sizeof(title_text), "MQTTs - ทีม %s",
                   TEAM_NAME);
    title.text = title_text;
    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, &sent, &screen->sent_label);
    }
    if (status == BENTO_OK) {
        status = add_identity(screen->ui, screen->no_platform);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui, &right_card);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, &handshake, &unused);
    }
    if (status == BENTO_OK) {
        status = add_lamps(screen);
    }
    if (status == BENTO_OK) {
        status = add_buttons(screen);
    }
    if (status == BENTO_OK) {
        status = add_meter(screen);
    }
    if (status == BENTO_OK) {
        status = add_confirm_box(screen);
    }
    return status;
}

/* tesaiot.is_connected(): a refused question counts as "no". */
static bool link_is_up(void)
{
    bool connected = false;

    if (bento_tesaiot_is_connected(&connected) != BENTO_OK) {
        connected = false;
    }
    return connected;
}

/* tesaiot.connect() returns at once: its answer is not the link's
 * state, only is_connected() is. */
static bento_status_t start_connect(void)
{
    bento_status_t status = bento_tesaiot_connect_start();

    if (status == BENTO_E_FAILED) {
        status = BENTO_OK;
    }
    return status;
}

/* The bar moves every half second; the number is written again only
 * when the second changes. */
static void show_seconds(const tls_screen_t *screen, int32_t waited_ms,
                         int32_t *last_second)
{
    char text[TEXT_BYTES];

    if ((waited_ms / MS_PER_S) != *last_second) {
        *last_second = waited_ms / MS_PER_S;
        set_value(screen->ui, screen->seconds_bar, *last_second);
        (void)snprintf(text, sizeof(text), "%ld วิ", (long)*last_second);
        set_text(screen->ui, screen->seconds_label, text);
    }
}

/* Every wait has a way out by time, or the program would hang here
 * the day the network is down. */
static bento_status_t wait_connected(const tls_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    int32_t last_second = -1;
    bool waiting = true;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) && waiting && !link_is_up()) {
        const int32_t waited_ms =
            bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);

        if (waited_ms > (WAIT_CEILING_S * MS_PER_S)) {
            (void)printf("ต่อแพลตฟอร์มไม่สำเร็จใน 30 วินาที\n");
            waiting = false;
        } else {
            show_seconds(screen, waited_ms, &last_second);
            drain_events(screen->ui);
            status = wait_ms(WAIT_STEP_MS);
        }
    }
    return status;
}

/* A lamp that goes off dims, it does not vanish: the state stays
 * visible. */
static void show_handshake(const tls_screen_t *screen)
{
    const bool connected = link_is_up();

    set_value(screen->ui, screen->lamp_wait, 0);
    if (connected) {
        (void)bento_lcd_console(
            "<span class=ok>เชื่อมต่อ TLS สำเร็จ</span>");
    } else {
        (void)bento_lcd_console(
            "<span class=muted>ต่อไม่สำเร็จใน 30 วินาที</span>");
    }
    set_value(screen->ui, screen->lamp_ok, connected ? 1 : 0);
    set_value(screen->ui, screen->lamp_fail, connected ? 0 : 1);
    drain_events(screen->ui);
}

/* round(value, digits) as py/modbuiltins.c: nearbyint(value * mult) /
 * mult, in the board's float width, then repr() of the result. */
static void round_text(bento_real_t value, int32_t digits, char *text)
{
    bento_real_t mult = (bento_real_t)1;
    bento_real_t scaled;
    bento_real_t rounded;
    uint32_t length = 0U;
    int32_t i;

    for (i = 0; i < digits; i++) {
        mult *= (bento_real_t)DECIMAL_BASE;
    }
    scaled = value * mult;
#if BENTO_FMT_REAL_DOUBLE
    rounded = nearbyint(scaled) / mult;
#else
    rounded = nearbyintf(scaled) / mult;
#endif
    if (bento_fmt_real(text, NUMBER_BYTES, rounded, &length) !=
        BENTO_OK) {
        text[0] = '\0';
    }
}

/* Three questions to the hardware. *read false is the original's
 * except OSError; BENTO_E_UNSUPPORTED (no such sensor) ends the
 * program. Flat, numbers as numbers, the platform's own key names. */
static bento_status_t read_payload(char *payload, size_t cap,
                                   bool *read)
{
    bento_sensors_imu6_t motion;
    float heading = 0.0F;
    float pot = 0.0F;
    char accel_text[NUMBER_BYTES];
    char heading_text[NUMBER_BYTES];
    char pot_text[NUMBER_BYTES];
    uint32_t length = 0U;
    bento_status_t status;

    bento_sensors_imu6_init(&motion);
    status = bento_sensors_bmi270_motion(&motion);
    if (status == BENTO_OK) {
        status = bento_sensors_bmm350_heading(&heading);
    }
    if (status == BENTO_OK) {
        status = bento_sensors_pot_percent(&pot);
    }
    *read = status == BENTO_OK;
    if (*read) {
        round_text((bento_real_t)motion.ax, ACCEL_DIGITS, accel_text);
        round_text((bento_real_t)heading, HEADING_DIGITS, heading_text);
        if (bento_fmt_real(pot_text, (uint32_t)sizeof(pot_text),
                           (bento_real_t)pot, &length) != BENTO_OK) {
            pot_text[0] = '\0';
        }
        (void)snprintf(
            payload, cap,
            "{\"heading\": %s, \"accel_x\": %s, \"pot\": %s}",
            heading_text, accel_text, pot_text);
    }
    return (status == BENTO_E_UNSUPPORTED) ? status : BENTO_OK;
}

/* No topic: the firmware builds it from device_id. tls_mode is the
 * answer to "how is this not session 10": it sets port 8884. */
static bento_status_t send_payload(const tls_screen_t *screen,
                                   session_t *session,
                                   const char *payload)
{
    char mode[VALUE_BYTES];
    char text[TEXT_BYTES];
    bento_status_t status;

    /* answers True or False, which the original ignores */
    (void)bento_tesaiot_publish(NULL, 0U, (const uint8_t *)payload,
                                (uint32_t)strlen(payload), 0U, NULL);
    session->sent++;
    status = read_field("tls_mode", mode, (uint32_t)sizeof(mode));
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "ส่งครั้งที่ %ld | โหมด %s -> 8884",
                       (long)session->sent, mode);
        (void)bento_lcd_print(text);
        (void)snprintf(text, sizeof(text), "ส่งแล้ว %ld ใบ",
                       (long)session->sent);
        set_text(screen->ui, screen->sent_label, text);
    }
    return status;
}

static void show_box(const tls_screen_t *screen, bool shown)
{
    set_shown(screen->ui, screen->box, shown);
    set_shown(screen->ui, screen->yes_button, shown);
    set_shown(screen->ui, screen->no_button, shown);
}

/* One click. Disconnecting cannot be taken back at once (a whole new
 * TLS handshake), so it asks first. */
static void on_click(const tls_screen_t *screen, session_t *session,
                     bento_ui_id_t widget)
{
    if ((widget == screen->disconnect_button) && !session->asking) {
        session->asking = true;
        show_box(screen, true);
    } else if ((widget == screen->yes_button) && session->asking) {
        session->asking = false;
        show_box(screen, false);
        (void)
            bento_tesaiot_disconnect(); /* answers True or False, ignored */
    } else if ((widget == screen->no_button) && session->asking) {
        session->asking = false;
        show_box(screen, false);
    } else if (widget == screen->connect_button) {
        (void)
            bento_tesaiot_connect_start(); /* answers True or False, ignored */
    } else {
        /* another widget, or a button that is not live now */
    }
}

/* The buttons are asked once per round: no finger is faster. */
static void read_buttons(const tls_screen_t *screen, session_t *session)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U; /* a failed poll answers no events */
    }
    for (i = 0U; i < count; i++) {
        if (events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) {
            on_click(screen, session, events[i].id);
        }
    }
}

/* One round: read, send, show, the buttons, then the rest. A failed
 * read waits a second and tries again, without sending. */
static bento_status_t publish_round(const tls_screen_t *screen,
                                    session_t *session)
{
    char payload[PAYLOAD_BYTES];
    bool read = false;
    bento_status_t status =
        read_payload(payload, sizeof(payload), &read);

    if ((status == BENTO_OK) && !read) {
        status = wait_ms(RETRY_READ_MS);
    } else if (status == BENTO_OK) {
        status = send_payload(screen, session, payload);
        if (status == BENTO_OK) {
            read_buttons(screen, session);
            status = wait_ms(SEND_EVERY_MS);
        }
    } else {
        /* no such sensor: the status ends the program */
    }
    return status;
}

/* The link is checked before every send, not once at the start: it
 * can drop at any time, and a program that sends on blindly loses an
 * hour of data in silence. */
static bento_status_t publish_loop(const tls_screen_t *screen)
{
    char text[TEXT_BYTES];
    session_t session = {0, false};
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) && link_is_up()) {
        status = publish_round(screen, &session);
    }
    if (status == BENTO_OK) { /* the screen must say the link is gone */
        set_value(screen->ui, screen->lamp_ok, 0);
        set_value(screen->ui, screen->lamp_fail, 1);
        drain_events(screen->ui);
        (void)snprintf(
            text, sizeof(text),
            "<span class=muted>หลุดการเชื่อมต่อ ส่งไป %ld ครั้ง</span>",
            (long)session.sent);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* Warm up once before the TLS handshake: on the Eva Kit the display
 * core answers sensor questions only some 13 s after a reset. */
static bento_status_t warm_up_sensors(void)
{
    bento_sensors_imu6_t motion;
    bento_status_t status;

    bento_sensors_imu6_init(&motion);
    status = bento_sensors_bmi270_motion(&motion);
    if ((status != BENTO_OK) && (status != BENTO_E_UNSUPPORTED)) {
        (void)printf("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ตอนส่ง\n");
        status = BENTO_OK;
    }
    return status;
}

int main(void)
{
    char text[TEXT_BYTES];
    tls_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = warm_up_sensors();
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)snprintf(text, sizeof(text), "<h2>MQTTs - ทีม %s</h2>",
                       TEAM_NAME);
        (void)bento_lcd_console(text);
        status = set_identity();
    }
    if (status == BENTO_E_UNSUPPORTED) { /* the BENTO Emulator */
        screen.no_platform = true;
        status = BENTO_OK;
    } else if (status == BENTO_OK) {
        status = print_config();
    } else {
        /* any other failure ends the program */
    }
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    if ((status == BENTO_OK) && !screen.no_platform) {
        status = start_connect();
    }
    if (status == BENTO_OK) {
        status = wait_connected(&screen);
    }
    if (status == BENTO_OK) {
        show_handshake(&screen);
        status = publish_loop(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
