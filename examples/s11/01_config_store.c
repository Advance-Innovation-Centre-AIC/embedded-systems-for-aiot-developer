/*
 * 01_config_store.c -- คลังค่าตั้งของแพลตฟอร์ม อ่านให้ครบก่อนจะต่ออะไร
 *
 * tesaiot.config() คืนค่าตั้ง 19 คีย์ที่บอกทั้งหมดว่าบอร์ดจะต่อไปที่ไหน ด้วยตัวตน
 * อะไร จอแสดงเลขจำนวนคีย์ตัวใหญ่ สี่คีย์ที่ต้องรู้จัก (device_id broker tls_mode
 * port) และบรรทัดสีส้มบอกว่า mqtt_pass ตั้งได้แต่อ่านกลับไม่ได้ ตารางเต็มแบ่งกลุ่ม
 * ไปอยู่ในคอนโซลและ print()
 *
 * tesaiot.config() answers the 19 settings that say where the board will
 * connect and with which identity: ask it before guessing. The screen shows
 * the key count in big digits, the four keys to know and an orange line
 * saying mqtt_pass can be set but never read back; the full grouped table
 * goes to the lcd console and to print().
 *
 * Twin of 01_config_store.py
 * (embedded-systems-for-aiot-developer/examples/s11/01_config_store.py),
 * from the library twin aiot-s11-01-config-store. Style: CLEAN_C.md.
 * What C changes: C has no dict, so the config is read once, key by key
 * (bento_tesaiot_config_key_at / _get_field, integers as decimal text,
 * api_key revealed because the original prints it) into one table that
 * every later lookup reads; a missing key, KeyError in MicroPython, ends
 * the program with BENTO_E_INVALID_ARGUMENT; sorted(keys) is an insertion
 * sort with strcmp, which is code point order on UTF-8.
 * In the BENTO Emulator, the TESAIoT platform from C is not available yet: the program says
 * so and ends (every bento_tesaiot_* call answers BENTO_E_UNSUPPORTED there).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_tesaiot.h"
#include "bento_time.h"

enum { /* sizes */
    KEYS_MAX = 32,
    KEY_BYTES = BENTO_TESAIOT_CONFIG_KEY_MAX + 1U,
    VALUE_BYTES = BENTO_TESAIOT_CONFIG_VALUE_MAX + 1U,
    TEXT_BYTES = 256,
    COUNT_BYTES = 64,
    GROUP_COUNT = 5,
    KEY_ROW_COUNT = 4,
    LINE_A_GROUPS = 3, /* groups on the first summary line */
    SETTLE_MS = 200
};
enum { /* layout */
    TITLE_X = 20,
    TITLE_Y = 12,
    CARD_X = 20,
    CARD_W = 652,
    TOP_CARD_Y = 52,
    TOP_CARD_H = 124,
    INNER_X = 40,
    CAPTION_Y = 62,
    SEG_Y = 88,
    SEG_W = 152,
    SEG_H = 72,
    KEY_ROW_X = 216,
    KEY_ROW_Y = 60,
    KEY_ROW_STEP = 26,
    PASS_Y = 188,
    HINT_Y = 214,
    HINT_RIGHT_X = 416,
    GROUP_CARD_Y = 244,
    GROUP_CARD_H = 76,
    GROUP_LINE_A_Y = 258,
    GROUP_LINE_B_Y = 288,
    TOTAL_Y = 336
};
enum { /* fonts and card shape */
    FONT_TITLE = 24,
    FONT_TEXT = 20,
    CARD_RADIUS = 12,
    CARD_BORDER = 1
};
enum { /* the course palette */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_INFO = 0x4A9EFF
};

/* The keys of each group, in the order the original lists them. */
static const char *const KEYS_IDENTITY[] = {"device_id", "factory_uid",
                                            "api_key", "tls_mode"};
static const char *const KEYS_MQTT[] = {
    "broker", "port", "sni_hostname", "qos", "keepalive"};
static const char *const KEYS_HTTPS[] = {"api_host", "api_port",
                                         "api_endpoint"};
static const char *const KEYS_RETRY[] = {
    "timeout_ms", "max_retries", "retry_interval_ms", "sntp_server",
    "sntp_timezone"};
static const char *const KEYS_OTHER[] = {"wifi_ssid", "debug_level"};

typedef struct {
    const char *title;
    uint32_t key_count;
    const char *const *keys;
} group_t;

/* Grouped so a reader sees which question each key answers, not just an
 * alphabetical list. */
static const group_t GROUPS[GROUP_COUNT] = {
    {"ตัวตนของอุปกรณ์", 4U, KEYS_IDENTITY},
    {"ปลายทาง MQTT", 5U, KEYS_MQTT},
    {"ปลายทาง HTTPS", 3U, KEYS_HTTPS},
    {"การลองใหม่และเวลา", 5U, KEYS_RETRY},
    {"อื่น ๆ", 2U, KEYS_OTHER}};

typedef struct {
    char key[KEYS_MAX][KEY_BYTES];
    char value[KEYS_MAX][VALUE_BYTES];
    uint32_t count;
} config_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader on
 * every load, and set again at the start of main() (CLEAN_C.md C9). */
extern config_t g_config;
config_t g_config;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

static int16_t px(int32_t value)
{
    return (int16_t)value;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const rect_t *place, uint32_t color,
                                int32_t font_px)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t id;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, &id);
}

/* The BENTO Emulator's C runtime has no TESAIoT link yet: the first
 * bento_tesaiot_* call answers BENTO_E_UNSUPPORTED there. Say so on the
 * screen and on the console; the program then ends with 0. */
static bento_status_t show_emulator_note(bento_ui_t *ui)
{
    static const char note[] = "TESAIoT จาก C ยังไม่มีใน BENTO "
                               "Emulator\nให้รันบน Dev Kit หรือ Eva Kit";
    bento_ui_widget_args_t args;
    bento_ui_id_t id;

    bento_ui_widget_args_init(&args);
    args.text = note;
    args.text_len = (uint32_t)strlen(note);
    args.x = TITLE_X;
    args.y = TITLE_Y;
    args.w = CARD_W;
    args.color = (uint32_t)COLOR_WARN;
    args.font_px = FONT_TEXT;
    (void)printf(
        "TESAIoT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva "
        "Kit / TESAIoT platform from C is not in the BENTO Emulator "
        "yet: run it on a Dev Kit or Eva Kit.\n");
    return bento_ui_label_create(ui, &args, &id);
}

static bento_status_t add_card(bento_ui_t *ui, const rect_t *place)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t id;

    bento_ui_widget_args_init(&args);
    args.x = place->x;
    args.y = place->y;
    args.w = place->w;
    args.h = place->h;
    args.color = (uint32_t)COLOR_CARD;
    args.min_val = COLOR_DIM;
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &id);
}

static bento_status_t add_seg7(bento_ui_t *ui, const char *text,
                               const rect_t *place)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t id;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.w = place->w;
    args.h = place->h;
    args.color = (uint32_t)COLOR_INFO;
    return bento_ui_seg7_create(ui, &args, &id);
}

static bool has_key(const config_t *config, const char *key,
                    uint32_t *index)
{
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < config->count) && !found; i++) {
        if (strcmp(config->key[i], key) == 0) {
            *index = i;
            found = true;
        }
    }
    return found;
}

/* A missing key raises KeyError in MicroPython; here it is a status. */
static bento_status_t config_value(const config_t *config,
                                   const char *key, const char **value)
{
    uint32_t index = 0U;
    bento_status_t status = BENTO_E_INVALID_ARGUMENT;

    *value = "";
    if (has_key(config, key, &index)) {
        *value = config->value[index];
        status = BENTO_OK;
    }
    return status;
}

static bento_status_t read_config(config_t *config)
{
    uint32_t count = 0U;
    uint32_t length = 0U;
    uint32_t i;
    bento_status_t status = bento_tesaiot_config_key_count(&count);

    if ((status == BENTO_OK) && (count > (uint32_t)KEYS_MAX)) {
        status = BENTO_E_NO_RESOURCE;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = bento_tesaiot_config_key_at(i, config->key[i],
                                             KEY_BYTES, &length);
        if (status == BENTO_OK) {
            status = bento_tesaiot_config_get_field(
                config->key[i], (uint32_t)strlen(config->key[i]),
                BENTO_TESAIOT_CONFIG_REVEAL_SECRET, config->value[i],
                VALUE_BYTES, &length);
        }
        if (status == BENTO_OK) {
            config->count = i + 1U;
        }
    }
    return status;
}

static uint32_t grouped_key_count(void)
{
    uint32_t shown = 0U;
    uint32_t i;

    for (i = 0U; i < (uint32_t)GROUP_COUNT; i++) {
        shown += GROUPS[i].key_count;
    }
    return shown;
}

static bento_status_t add_count_card(bento_ui_t *ui,
                                     const config_t *config)
{
    const rect_t card = {CARD_X, TOP_CARD_Y, CARD_W, TOP_CARD_H};
    const rect_t caption = {INNER_X, CAPTION_Y, 0, 0};
    const rect_t digits = {INNER_X, SEG_Y, SEG_W, SEG_H};
    char text[COUNT_BYTES];
    bento_status_t status = add_card(ui, &card);

    if (status == BENTO_OK) {
        status = add_label(ui, "คีย์ที่อ่านกลับได้", &caption,
                           (uint32_t)COLOR_DIM, FONT_TEXT);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%u",
                       (unsigned int)config->count);
        status = add_seg7(ui, text, &digits);
    }
    return status;
}

/* A key not set yet must read "not set", not vanish: an unprovisioned
 * board has an empty device_id, not a missing one. */
static bento_status_t add_key_rows(bento_ui_t *ui,
                                   const config_t *config)
{
    static const char *const key_rows[KEY_ROW_COUNT] = {
        "device_id", "broker", "tls_mode", "port"};
    char text[TEXT_BYTES];
    const char *value = "";
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < (uint32_t)KEY_ROW_COUNT) && (status == BENTO_OK);
         i++) {
        const rect_t row = {KEY_ROW_X,
                            px(KEY_ROW_Y + ((int32_t)i * KEY_ROW_STEP)),
                            0, 0};

        status = config_value(config, key_rows[i], &value);
        if (status == BENTO_OK) {
            (void)snprintf(text, sizeof(text), "%s = %s", key_rows[i],
                           (value[0] != '\0') ? value : "(ยังไม่ตั้ง)");
            status = add_label(ui, text, &row, (uint32_t)COLOR_TEXT,
                               FONT_TEXT);
        }
    }
    return status;
}

/* The password can be written but never read back, so only a working
 * connection proves it was set right. */
static bento_status_t add_password_lines(bento_ui_t *ui, bool has_pass)
{
    const rect_t pass = {TITLE_X, PASS_Y, 0, 0};
    const rect_t hint_left = {TITLE_X, HINT_Y, 0, 0};
    const rect_t hint_right = {HINT_RIGHT_X, HINT_Y, 0, 0};
    char text[TEXT_BYTES];
    bento_status_t status;

    (void)snprintf(text, sizeof(text),
                   "mqtt_pass อยู่ใน config() หรือไม่: %s",
                   has_pass ? "True" : "False");
    status =
        add_label(ui, text, &pass, (uint32_t)COLOR_WARN, FONT_TEXT);
    /* two labels on one line keep each under ui.Label's 126 bytes */
    if (status == BENTO_OK) {
        status = add_label(ui, "ตั้งได้ด้วย config_set แต่อ่านตรง ๆ",
                           &hint_left, (uint32_t)COLOR_WARN, FONT_TEXT);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "ได้ KeyError -> ใช้ cfg.get()", &hint_right,
                      (uint32_t)COLOR_WARN, FONT_TEXT);
    }
    return status;
}

static bento_status_t add_group_card(bento_ui_t *ui,
                                     const config_t *config)
{
    const rect_t card = {CARD_X, GROUP_CARD_Y, CARD_W, GROUP_CARD_H};
    const rect_t line_a = {INNER_X, GROUP_LINE_A_Y, 0, 0};
    const rect_t line_b = {INNER_X, GROUP_LINE_B_Y, 0, 0};
    const rect_t total = {TITLE_X, TOTAL_Y, 0, 0};
    const uint32_t shown = grouped_key_count();
    char counts[GROUP_COUNT][COUNT_BYTES];
    char text[TEXT_BYTES];
    uint32_t i;
    bento_status_t status = add_card(ui, &card);

    for (i = 0U; i < (uint32_t)GROUP_COUNT; i++) {
        (void)snprintf(counts[i], sizeof(counts[i]), "%s %u",
                       GROUPS[i].title,
                       (unsigned int)GROUPS[i].key_count);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%s - %s - %s", counts[0],
                       counts[1], counts[2]);
        status = add_label(ui, text, &line_a, (uint32_t)COLOR_DIM,
                           FONT_TEXT);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%s - %s",
                       counts[LINE_A_GROUPS],
                       counts[LINE_A_GROUPS + 1]);
        status = add_label(ui, text, &line_b, (uint32_t)COLOR_DIM,
                           FONT_TEXT);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "ไล่ครบ %u จาก %u คีย์ - รายละเอียดอยู่ในคอนโซล",
                       (unsigned int)shown,
                       (unsigned int)config->count);
        status =
            add_label(ui, text, &total,
                      (shown == config->count) ? (uint32_t)COLOR_OK
                                               : (uint32_t)COLOR_WARN,
                      FONT_TEXT);
    }
    return status;
}

static bento_status_t
build_screen(bento_ui_t *ui, const config_t *config, bool has_pass)
{
    const rect_t title = {TITLE_X, TITLE_Y, 0, 0};
    bento_status_t status;

    /* MicroPython ignores a refused ui.screen(): the runtime already set it up */
    (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = bento_time_sleep_ms((uint32_t)SETTLE_MS);
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "คลังค่าตั้ง tesaiot.config()", &title,
                           (uint32_t)COLOR_TEXT, FONT_TITLE);
    }
    if (status == BENTO_OK) {
        status = add_count_card(ui, config);
    }
    if (status == BENTO_OK) {
        status = add_key_rows(ui, config);
    }
    if (status == BENTO_OK) {
        status = add_password_lines(ui, has_pass);
    }
    if (status == BENTO_OK) {
        status = add_group_card(ui, config);
    }
    return status;
}

static bento_status_t print_lcd_group(const config_t *config,
                                      const group_t *group)
{
    char text[TEXT_BYTES];
    const char *value = "";
    uint32_t i;
    bento_status_t status = BENTO_OK;

    (void)snprintf(text, sizeof(text), "<b>%s</b>", group->title);
    (void)bento_lcd_print(text);
    for (i = 0U; (i < group->key_count) && (status == BENTO_OK); i++) {
        status = config_value(config, group->keys[i], &value);
        if (status == BENTO_OK) {
            (void)snprintf(text, sizeof(text), "   %s = %s",
                           group->keys[i], value);
            (void)bento_lcd_print(text);
        }
    }
    return status;
}

/* The console takes the grouped list; lcd.print() ignores a console that
 * is not ready (modlcd.c), so its status is ignored too. */
static bento_status_t print_lcd_groups(const config_t *config)
{
    char text[TEXT_BYTES];
    uint32_t i;
    bento_status_t status = BENTO_OK;

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>คาบ 11 - คลังค่าตั้ง</h2>");
    (void)snprintf(text, sizeof(text), "config() คืนมา %u คีย์",
                   (unsigned int)config->count);
    (void)bento_lcd_print(text);
    for (i = 0U; (i < (uint32_t)GROUP_COUNT) && (status == BENTO_OK);
         i++) {
        status = print_lcd_group(config, &GROUPS[i]);
    }
    return status;
}

static bento_status_t print_table_group(const config_t *config,
                                        const group_t *group)
{
    const char *value = "";
    uint32_t i;
    bento_status_t status = BENTO_OK;

    (void)printf("%s\n", group->title);
    for (i = 0U; (i < group->key_count) && (status == BENTO_OK); i++) {
        status = config_value(config, group->keys[i], &value);
        if (status == BENTO_OK) {
            (void)printf("  %-18s = %s\n", group->keys[i], value);
        }
    }
    if (status == BENTO_OK) {
        (void)printf("\n");
    }
    return status;
}

/* print() takes the full table, too long for the 4.3 inch screen. */
static bento_status_t print_table(const config_t *config)
{
    uint32_t i;
    bento_status_t status = BENTO_OK;

    (void)printf("config() คืนมาทั้งหมด %u คีย์\n",
                 (unsigned int)config->count);
    (void)printf("\n");
    for (i = 0U; (i < (uint32_t)GROUP_COUNT) && (status == BENTO_OK);
         i++) {
        status = print_table_group(config, &GROUPS[i]);
    }
    return status;
}
static bool is_grouped(const char *key)
{
    bool found = false;
    uint32_t i;
    uint32_t j;

    for (i = 0U; i < (uint32_t)GROUP_COUNT; i++) {
        for (j = 0U; j < GROUPS[i].key_count; j++) {
            found = found || (strcmp(key, GROUPS[i].keys[j]) == 0);
        }
    }
    return found;
}

static void sort_key_order(const config_t *config, uint32_t *order)
{
    uint32_t i;

    for (i = 0U; i < config->count; i++) {
        order[i] = i;
    }
    for (i = 1U; i < config->count; i++) {
        uint32_t j = i;

        while ((j > 0U) && (strcmp(config->key[order[j - 1U]],
                                   config->key[order[j]]) > 0)) {
            const uint32_t moved = order[j];

            order[j] = order[j - 1U];
            order[j - 1U] = moved;
            j--;
        }
    }
}

static void print_unlisted_keys(const config_t *config,
                                const uint32_t *order)
{
    uint32_t i;

    for (i = 0U; i < config->count; i++) {
        if (!is_grouped(config->key[order[i]])) {
            (void)printf("   %s = %s\n", config->key[order[i]],
                         config->value[order[i]]);
        }
    }
}

/* Make sure no key was forgotten outside the groups. */
static void print_ungrouped(const config_t *config)
{
    uint32_t order[KEYS_MAX];

    if (grouped_key_count() != config->count) {
        (void)printf("มีคีย์ที่ยังไม่ได้จัดกลุ่ม:\n");
        sort_key_order(config, order);
        print_unlisted_keys(config, order);
    }
}
/* A secret has no way out of the device through a plain read: that is
 * as it should be. */
static bento_status_t print_secret_lines(const config_t *config,
                                         bool has_pass)
{
    char text[TEXT_BYTES];
    const char *tls_mode = "";
    const char *port = "";
    uint32_t index = 0U;
    bento_status_t status;

    (void)snprintf(
        text, sizeof(text),
        "<span class=warn>mqtt_pass อยู่ใน config(): %s </span>",
        has_pass ? "True" : "False");
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text), "อ่านด้วย .get(): %s",
                   has_key(config, "mqtt_pass", &index)
                       ? config->value[index]
                       : "อ่านกลับไม่ได้");
    (void)bento_lcd_print(text);
    status = config_value(config, "tls_mode", &tls_mode);
    if (status == BENTO_OK) {
        status = config_value(config, "port", &port);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "tls_mode = %s | port = %s",
                       tls_mode, port);
        (void)bento_lcd_print(text);
        (void)printf("อ่านค่าครบแล้ว ขั้นถัดไปอยู่ที่ "
                     "examples/s11/05_wait_for_connected.py\n");
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    config_t *config = &g_config;
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t index = 0U;
    bool has_pass = false;
    bento_status_t status;

    (void)memset(config, 0, sizeof(*config));
    status = read_config(config);
    if (status == BENTO_E_UNSUPPORTED) {
        status = show_emulator_note(ui);
        if (status == BENTO_OK) {
            status =
                BENTO_E_CANCELLED; /* nothing else can run: a quiet end */
        }
    }
    if (status == BENTO_OK) {
        has_pass = has_key(config, "mqtt_pass", &index);
        status = build_screen(ui, config, has_pass);
    }
    if (status == BENTO_OK) {
        /* the poll's events are thrown away, as in the original */
        (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX,
                            &count, NULL);
        status = print_lcd_groups(config);
    }
    if (status == BENTO_OK) {
        status = print_table(config);
    }
    if (status == BENTO_OK) {
        print_ungrouped(config);
        status = print_secret_lines(config, has_pass);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
