/*
 * 07_platform_in_one_call.c -- คลังค่าตั้งที่บอร์ดจำเอง (The settings
 * store the board remembers)
 *
 * บอร์ดจำได้เองว่าจะต่อไปที่ไหน แม้ถอดไฟแล้วเสียบใหม่ ค่าตั้งอยู่บนแฟลชของบอร์ด
 * ตั้งครั้งเดียว ทุกโปรแกรมอ่านได้ จอแสดงหกคีย์จากคลังค่าตั้ง แล้วเวลารอต่อ
 * วันนี้ PLATFORM_BROKER ว่าง จึงอ่านอย่างเดียว เพราะ tesaiot.connect() ใช้ TLS
 * พอร์ต 8883/8884 เสมอ แต่ broker.hivemq.com รับที่ 1883 ไม่เข้ารหัส
 * กับดัก: connect() คืนค่าทันที ต้องวนถาม is_connected() และมีกำหนดเวลาเลิกรอ
 *
 * The board remembers where to connect, even after a power cycle: the
 * settings live on its flash, set once and readable by every program.
 * The screen shows six keys of the store, then how long the link took.
 * Today PLATFORM_BROKER is empty, so the program only reads: connect()
 * always uses TLS on 8883/8884, and broker.hivemq.com takes the board
 * on 1883 without TLS. Fill PLATFORM_BROKER (session 10) and it writes
 * two keys, joins Wi-Fi, connects and publishes six cards. Trap:
 * connect() returns at once; ask is_connected() in a loop, with a
 * deadline.
 *
 * Twin of 07_platform_in_one_call.py
 * (embedded-systems-for-aiot-developer/examples/s02/07_platform_in_one_call.py).
 * Style: CLEAN_C.md.
 * In the BENTO Emulator, the TESAIoT platform from C is not
 * available yet: the program says so and ends.
 * Where C differs: C has no dict. tesaiot.config() is read field by
 * field into a table (19 keys, about 2.4 KB, so at file scope), and
 * `for k in cfg` walks the keys in the order MicroPython's hash table
 * gives them on the PSoC Edge (1-byte qstr hashes). json.dumps() of
 * {"n", "knob", "uptime_s"} is one snprintf() with the keys in that
 * same hash order, which differs between the PSoC Edge and the
 * reTerminal. class Stop / raise Stop becomes a status chain that
 * ends the program normally. The OPTIGA half of tesaiot is not used.
 *
 * ตาคุณ: รันแบบ PLATFORM_BROKER ว่าง จด port กับ tls_mode แล้วตอบว่าทำไม
 * port ที่เห็นอาจไม่ใช่พอร์ตที่ connect() ใช้จริง
 * Your turn: run with PLATFORM_BROKER empty, note port and tls_mode,
 * and explain why that port may not be the one connect() really uses
 * (hint: tesaiot.config_reset() restores the factory values).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_tesaiot.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
/* แก้ให้ตรงกับที่ผู้สอนแจก
 * Set these from what the teacher hands out. */
/* the team's phone hotspot: faculty Wi-Fi needs a login */
static const char WIFI_SSID[] = "bento-teamXX";
/* 8 characters or more */
static const char WIFI_PASS[] = "<รหัส Hotspot ของทีม>";
static const char DEVICE_ID[] = "team03";
/* the platform's TLS broker, handed out in session 10; never broker.hivemq.com */
static const char PLATFORM_BROKER[] = "";
/* ---- end of settings ---- */

enum {
    CARDS = 6, /* cards sent once linked */
    MS_PER_S = 1000,
    NO_KNOB = -1, /* no potentiometer reading */
    CONFIG_KEYS = 19,
    SHOW_COUNT = 6,
    CELL_ROWS = 3,
    VALUE_BYTES = BENTO_TESAIOT_CONFIG_VALUE_MAX + 1U,
    /* "  key = value": 2 + a key of 31 + 3 + a value of 127 + NUL */
    CONFIG_LINE_BYTES = BENTO_TESAIOT_CONFIG_KEY_MAX +
                        BENTO_TESAIOT_CONFIG_VALUE_MAX + 6U,
    TEXT_BYTES = 192
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    WAIT_MS = 15000, /* the longest wait for the link */
    POLL_MS = 250,   /* ask again this often */
    CARD_MS = 1500,
    WAIT_SLICE_MS = 50, /* a Wi-Fi join or scan is asked this often */
    SNAPSHOT_WAIT_MS =
        1000 /* sensors.snapshot()'s wait after the first */
};

/* Layout on the 792 x 398 canvas. */
enum {
    FONT_PX = 20,
    SMALL_FONT_PX = 16,
    BIG_FONT_PX = 24,
    LEFT_X = 20,
    TITLE_Y = 12,
    STATUS_Y = 48,
    CARD_Y = 80,
    CARD_W = 652,
    CARD_H = 120,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    CELL_LEFT_X = 36,
    CELL_RIGHT_X = 360,
    CARD_TITLE_Y = 88,
    CELL_TOP_Y = 116,
    CELL_STEP_Y = 28,
    LINK_CAPTION_Y = 208,
    LINK_Y = 236,
    WAITED_CAPTION_X = 380,
    SEG_X = 532,
    SEG_Y = 232,
    SEG_W = 152,
    SEG_H = 52,
    NOTE_Y = 300,
    WARN_Y = 332,
    FOOTER_Y = 364
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

/* tesaiot.config()'s 19 keys in the order `for k in cfg` walks them:
 * mp_obj_new_dict(24) filled from modtesaiot.c, slot = qstr hash % 24
 * with linear probing, 1-byte hashes of the PSoC Edge build. */
static const char *const CONFIG_KEY_ORDER[CONFIG_KEYS] = {
    "api_host",  "retry_interval_ms", "debug_level", "broker",
    "wifi_ssid", "sntp_server",       "keepalive",   "api_endpoint",
    "api_port",  "api_key",           "factory_uid", "device_id",
    "qos",       "sni_hostname",      "tls_mode",    "max_retries",
    "port",      "sntp_timezone",     "timeout_ms"};

/* cfg = tesaiot.config(): every value read once, as the dict is. Too
 * big for the program stack, so file scope, zeroed by the loader and
 * again at the start of main() (CLEAN_C.md C9, BENTO-MISRA-E03/E04). */
extern char g_config[CONFIG_KEYS][VALUE_BYTES];
char g_config[CONFIG_KEYS][VALUE_BYTES];
extern bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];

typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} text_style_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t status;
    bento_ui_id_t cells[SHOW_COUNT];
    bento_ui_id_t link;
    bento_ui_id_t seg;
    bento_ui_id_t note;
    bento_ui_id_t warn;
} platform_screen_t;

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The widget setters ignore a refusal in MicroPython (modui.c), so
 * they do here too. */
static void show(const platform_screen_t *screen, bento_ui_id_t widget,
                 uint32_t color, const char *text)
{
    (void)bento_ui_set_color(screen->ui, widget, color);
    (void)bento_ui_set_text(screen->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void poll_once(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* ui.poll() answers no events when it fails; none are read */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* ---- the screen ---- */

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const text_style_t *style,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = style->x;
    args.y = style->y;
    args.color = style->color;
    args.font_px = style->font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = LEFT_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border colour */
    args.max_val = CARD_RADIUS; /* and max= its corner radius */
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &card);
}

/* Six cells, two columns of three. Never an empty label: LVGL would
 * write "Label" into it and leave it there. */
static bento_status_t add_cells(platform_screen_t *screen)
{
    text_style_t cell = {CELL_LEFT_X, CELL_TOP_Y, COLOR_TEXT, FONT_PX};
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < SHOW_COUNT) && (status == BENTO_OK); i++) {
        const int32_t row = i % CELL_ROWS;

        cell.x = (i < CELL_ROWS) ? CELL_LEFT_X : CELL_RIGHT_X;
        cell.y = (int16_t)(CELL_TOP_Y + (row * CELL_STEP_Y));
        status =
            add_label(screen->ui, "รออ่าน", &cell, &screen->cells[i]);
    }
    return status;
}

static bento_status_t build_store_card(platform_screen_t *screen)
{
    const text_style_t title = {LEFT_X, TITLE_Y, COLOR_TEXT,
                                BIG_FONT_PX};
    const text_style_t status_style = {LEFT_X, STATUS_Y, COLOR_DIM,
                                       FONT_PX};
    const text_style_t card_title = {CELL_LEFT_X, CARD_TITLE_Y,
                                     COLOR_INFO, SMALL_FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "คลังค่าตั้งที่บอร์ดจำเอง", &title, &unused);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังอ่านคลังค่าตั้ง", &status_style,
                           &screen->status);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "tesaiot.config()", &card_title,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_cells(screen);
    }
    return status;
}

static bento_status_t add_seg(platform_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0";
    args.text_len = 1U;
    args.x = SEG_X;
    args.y = SEG_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = COLOR_INFO;
    return bento_ui_seg7_create(screen->ui, &args, &screen->seg);
}

static bento_status_t build_link_rows(platform_screen_t *screen)
{
    const text_style_t caption = {LEFT_X, LINK_CAPTION_Y, COLOR_DIM,
                                  FONT_PX};
    const text_style_t link = {LEFT_X, LINK_Y, COLOR_DIM, BIG_FONT_PX};
    const text_style_t waited = {WAITED_CAPTION_X, LINK_CAPTION_Y,
                                 COLOR_DIM, FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "สถานะการต่อ", &caption, &unused);

    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "ยังไม่ได้สั่งต่อ", &link, &screen->link);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "รอมาแล้ว (ms)", &waited, &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg(screen);
    }
    return status;
}

static bento_status_t build_notes(platform_screen_t *screen)
{
    const text_style_t note = {LEFT_X, NOTE_Y, COLOR_DIM, FONT_PX};
    const text_style_t warn = {LEFT_X, WARN_Y, COLOR_WARN, FONT_PX};
    const text_style_t footer = {LEFT_X, FOOTER_Y, COLOR_DIM, FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "กำลังเริ่ม", &note, &screen->note);

    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "ครึ่ง OPTIGA ของโมดูลนี้ปิดอยู่บนบอร์ดชุดนี้",
                      &warn, &screen->warn);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "ตั้งครั้งเดียว อยู่บนแฟลช ถอดไฟแล้วยังอยู่",
                      &footer, &unused);
    }
    return status;
}

/* The BENTO Emulator has no TESAIoT platform from C yet: every
 * bento_tesaiot_* call, the settings store included, answers
 * BENTO_E_UNSUPPORTED there. The program takes
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
    static const char note[] = "TESAIoT จาก C ยังไม่มีใน BENTO Emulator\n"
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
    (void)printf(
        "TESAIoT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit "
        "หรือ Eva Kit / TESAIoT platform from C is not in the BENTO "
        "Emulator yet: run it on a Dev Kit or Eva Kit.\n");
}

static bento_status_t build_screen(platform_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = build_store_card(screen);
    }
    if (status == BENTO_OK) {
        status = build_link_rows(screen);
    }
    if (status == BENTO_OK) {
        status = build_notes(screen);
    }
    return status;
}

/* ---- the settings store ---- */

/* tesaiot.config(): one snapshot of the store, read without a network
 * (it lives on flash). The dict carries api_key in plain, so the
 * secret is read too. */
static bento_status_t read_config(void)
{
    bento_status_t status = BENTO_OK;
    uint32_t length = 0U;
    uint32_t i;

    for (i = 0U; (i < (uint32_t)CONFIG_KEYS) && (status == BENTO_OK);
         i++) {
        status = bento_tesaiot_config_get_field(
            CONFIG_KEY_ORDER[i], (uint32_t)strlen(CONFIG_KEY_ORDER[i]),
            BENTO_TESAIOT_CONFIG_REVEAL_SECRET, g_config[i],
            VALUE_BYTES, &length);
    }
    return status;
}

/* cfg[key], or NULL when `key in cfg` is false. */
static const char *config_value(const char *key)
{
    const char *value = NULL;
    uint32_t i;

    for (i = 0U; (i < (uint32_t)CONFIG_KEYS) && (value == NULL); i++) {
        if (strcmp(CONFIG_KEY_ORDER[i], key) == 0) {
            value = g_config[i];
        }
    }
    return value;
}

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void print_config(void)
{
    char text[CONFIG_LINE_BYTES];
    uint32_t i;

    (void)snprintf(text, sizeof(text), "คลังค่าตั้งมีทั้งหมด %d คีย์",
                   CONFIG_KEYS);
    (void)bento_lcd_print(text);
    for (i = 0U; i < (uint32_t)CONFIG_KEYS; i++) {
        /* a stored value is at most VALUE_BYTES - 1 bytes */
        (void)snprintf(text, sizeof(text), "  %s = %.*s",
                       CONFIG_KEY_ORDER[i], VALUE_BYTES - 1,
                       g_config[i]);
        (void)bento_lcd_print(text);
    }
}

/* One cell; recolor puts a found key back in the text colour. */
static void show_cell(const platform_screen_t *screen, uint32_t index,
                      bool recolor)
{
    static const char *const show_keys[SHOW_COUNT] = {
        "device_id", "broker", "port", "tls_mode", "qos", "keepalive"};
    char text[TEXT_BYTES];
    const char *value = config_value(show_keys[index]);

    if (value == NULL) {
        (void)snprintf(text, sizeof(text), "%s = ไม่มีคีย์นี้",
                       show_keys[index]);
        show(screen, screen->cells[index], COLOR_DIM, text);
    } else {
        if (recolor) {
            (void)bento_ui_set_color(screen->ui, screen->cells[index],
                                     COLOR_TEXT);
        }
        (void)snprintf(text, sizeof(text), "%s = %s", show_keys[index],
                       value);
        (void)bento_ui_set_text(screen->ui, screen->cells[index], text,
                                (uint32_t)strlen(text));
    }
}

static void show_cells(const platform_screen_t *screen, bool recolor)
{
    uint32_t i;

    for (i = 0U; i < (uint32_t)SHOW_COUNT; i++) {
        show_cell(screen, i, recolor);
    }
}

/* No platform broker yet: show what is there, write nothing. The port
 * in the store is not the one connect() really uses. */
static void show_read_only(const platform_screen_t *screen)
{
    char text[TEXT_BYTES];

    show_cells(screen, false);
    (void)snprintf(text, sizeof(text),
                   "อ่านได้ %d คีย์ | ไม่ได้เขียนอะไรลงแฟลช", CONFIG_KEYS);
    show(screen, screen->status, COLOR_WARN, text);
    show(screen, screen->link, COLOR_DIM, "ยังไม่สั่งต่อ");
    show(screen, screen->note, COLOR_WARN,
         "ต่อได้เฉพาะแบบ TLS รอคาบ 10 กับ 11");
    poll_once(screen->ui);
    (void)bento_lcd_print(
        "<span class=warn>PLATFORM_BROKER ว่าง อ่านอย่างเดียว</span>");
    (void)bento_lcd_print(
        "tesaiot.connect() ใช้ TLS พอร์ต 8883/8884 เสมอ");
    (void)bento_lcd_print("broker.hivemq.com รับบอร์ดที่ 1883 ไม่เข้ารหัส");
    (void)bento_lcd_print("สองทางนี้จึงต่อกันไม่ได้");
    (void)printf("อ่านอย่างเดียว | คลังค่าตั้งมี %d คีย์\n", CONFIG_KEYS);
}

/* Only the keys needed are written; "port" is not, because connect()
 * takes its port from tls_mode. The store is read again afterwards to
 * prove the values landed. */
static bento_status_t write_keys(const platform_screen_t *screen)
{
    char text[TEXT_BYTES];
    bento_status_t status = bento_tesaiot_config_set(
        "device_id", (uint32_t)strlen("device_id"), DEVICE_ID,
        (uint32_t)strlen(DEVICE_ID));

    if (status == BENTO_OK) {
        status = bento_tesaiot_config_set(
            "broker", (uint32_t)strlen("broker"), PLATFORM_BROKER,
            (uint32_t)strlen(PLATFORM_BROKER));
    }
    if (status == BENTO_OK) {
        status = read_config();
    }
    if (status == BENTO_OK) {
        show_cells(screen, true);
        (void)snprintf(text, sizeof(text),
                       "อ่านได้ %d คีย์ | เขียนทับไป 2 คีย์", CONFIG_KEYS);
        show(screen, screen->status, COLOR_OK, text);
        poll_once(screen->ui);
        (void)bento_lcd_print(
            "<span class=ok>เขียนทับ device_id, broker แล้ว</span>");
        (void)bento_lcd_console(
            "<span class=muted>ค่าพวกนี้อยู่บนแฟลช ถอดไฟแล้วยังอยู่</span>");
    }
    return status;
}

/* ---- Wi-Fi ---- */

/* wifi.connect(): waits for the join in slices so Stop can end it.
 * *joined is its True or False. */
static bento_status_t join_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
    bento_status_t join = BENTO_E_IN_PROGRESS;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = WIFI_PASS;
    params.pass_len = (uint32_t)strlen(WIFI_PASS);
    params.flags = BENTO_WIFI_F_SAVE_CREDS; /* the binding saves too */
    status = bento_wifi_connect_start(&params);
    if (status == BENTO_OK) {
        join = bento_wifi_connect_poll();
    }
    while ((status == BENTO_OK) && (join == BENTO_E_IN_PROGRESS)) {
        status = sleep_checked(WAIT_SLICE_MS);
        join = (status == BENTO_OK) ? bento_wifi_connect_poll() : join;
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* Stop wins either way */
    }
    *joined = (status == BENTO_OK) && (join == BENTO_OK);
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* wifi.scan(), asked in slices so Stop ends it; *heard is whether the
 * team's network is among the answers. */
static bento_status_t hear_network(bool *heard)
{
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t scan = BENTO_E_IN_PROGRESS;
    bento_status_t status = bento_wifi_scan_start();

    while ((status == BENTO_OK) && (scan == BENTO_E_IN_PROGRESS)) {
        scan = bento_wifi_scan_poll(g_networks, BENTO_WIFI_SCAN_MAX,
                                    &count);
        if (scan == BENTO_E_IN_PROGRESS) {
            status = sleep_checked(WAIT_SLICE_MS);
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_scan_cancel(); /* Stop wins either way */
    }
    status = (status == BENTO_OK) ? scan : status;
    *heard = false;
    for (i = 0U; (status == BENTO_OK) && (i < count); i++) {
        *heard = *heard || (strcmp(g_networks[i].ssid, WIFI_SSID) == 0);
    }
    return status;
}

static void show_no_wifi(const platform_screen_t *screen, bool heard)
{
    char why[TEXT_BYTES];
    char text[TEXT_BYTES];

    if (heard) {
        (void)snprintf(why, sizeof(why),
                       "ได้ยินวง %s แต่ต่อไม่ผ่าน ตรวจรหัสผ่าน", WIFI_SSID);
    } else {
        (void)snprintf(why, sizeof(why),
                       "ไม่ได้ยินวง %s เลย ตรวจชื่อวงหรือย้ายที่", WIFI_SSID);
    }
    show(screen, screen->link, COLOR_BAD, "ไม่มีเน็ต");
    show(screen, screen->status, COLOR_BAD,
         "อ่านเขียนคลังค่าตั้งได้ แต่ต่อเน็ตไม่ได้");
    show(screen, screen->note, COLOR_BAD, why);
    poll_once(screen->ui);
    (void)snprintf(text, sizeof(text), "<span class=error>%s</span>",
                   why);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print(
        "<span class=ok>ส่วนคลังค่าตั้งทำงานครบแล้ว ไม่ต้องใช้เน็ต</span>");
    (void)printf("หยุดที่: %s\n", why);
}

/* wifi.ip(): always a text, "0.0.0.0" while there is no address. */
static void print_ip(void)
{
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    char text[TEXT_BYTES];
    uint32_t address = 0U;
    uint32_t length = 0U;

    if ((bento_wifi_ip(BENTO_WIFI_IF_ANY, &address) != BENTO_OK) ||
        (bento_wifi_ipv4_format(address, ip, sizeof(ip), &length) !=
         BENTO_OK)) {
        (void)snprintf(ip, sizeof(ip), "0.0.0.0");
    }
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ได้ IP %s </span>", ip);
    (void)bento_lcd_print(text);
}

/* tesaiot.connect() does not join Wi-Fi: an IP must come first.
 * *online is false when the program stops here. */
static bento_status_t go_online(const platform_screen_t *screen,
                                bool *online)
{
    char text[TEXT_BYTES];
    bool heard = false;
    bento_status_t status;

    show(screen, screen->status, COLOR_WARN, "กำลังต่อ WiFi จอจะนิ่งสักครู่");
    poll_once(screen->ui);
    (void)snprintf(text, sizeof(text), "กำลังต่อ WiFi %s", WIFI_SSID);
    (void)bento_lcd_print(text);
    status = join_wifi(online);
    if ((status == BENTO_OK) && !*online) {
        status = hear_network(&heard);
        if (status == BENTO_OK) {
            show_no_wifi(screen, heard);
        }
    }
    if ((status == BENTO_OK) && *online) {
        print_ip();
    }
    return status;
}

/* ---- the platform link ---- */

/* connect() returns at once: its answer means "order taken", not
 * "linked". Sending straight away would be an OSError. */
static bento_status_t start_link(const platform_screen_t *screen)
{
    char text[TEXT_BYTES];

    show(screen, screen->link, COLOR_WARN, "สั่งต่อแล้ว กำลังรอ");
    (void)snprintf(text, sizeof(text),
                   "รอสูงสุด %" PRId32 " วินาที แล้วเลิกรอ",
                   (int32_t)WAIT_MS / (int32_t)MS_PER_S);
    show(screen, screen->note, COLOR_DIM, text);
    poll_once(screen->ui);
    (void)bento_lcd_print("สั่ง tesaiot.connect() แล้ว กำลังรอสาย");
    return bento_tesaiot_connect_start();
}

/* Asks is_connected() every POLL_MS until linked or WAIT_MS: the
 * deadline is what tells a program that fails politely from one that
 * hangs until the cable is pulled. */
static bento_status_t wait_for_link(const platform_screen_t *screen,
                                    uint32_t start_ms, bool *linked)
{
    char text[TEXT_BYTES];
    bool waiting = true;
    bento_status_t status = BENTO_OK;

    *linked = false;
    while ((status == BENTO_OK) && waiting) {
        const int32_t waited_ms =
            bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);

        (void)snprintf(text, sizeof(text), "%" PRId32, waited_ms);
        (void)bento_ui_set_text(screen->ui, screen->seg, text,
                                (uint32_t)strlen(text));
        status = bento_tesaiot_is_connected(linked);
        waiting =
            (status == BENTO_OK) && !*linked && (waited_ms < WAIT_MS);
        if (waiting) {
            poll_once(screen->ui);
            status = sleep_checked(POLL_MS);
        }
    }
    return status;
}

static void show_not_linked(const platform_screen_t *screen)
{
    char text[TEXT_BYTES];
    const char *broker = config_value("broker");

    (void)bento_ui_set_color(screen->ui, screen->seg, COLOR_BAD);
    (void)snprintf(text, sizeof(text), "ต่อไม่ติดใน %" PRId32 " วินาที",
                   (int32_t)WAIT_MS / (int32_t)MS_PER_S);
    show(screen, screen->link, COLOR_BAD, text);
    show(screen, screen->status, COLOR_BAD,
         "มีเน็ตแล้ว แต่ไปไม่ถึงแพลตฟอร์มที่ตั้งไว้");
    show(screen, screen->note, COLOR_BAD,
         "ตรวจชื่อ broker ใบรับรอง และพอร์ต TLS");
    poll_once(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "<span class=error>รอครบ %d ms แล้วยังไม่ติด</span>",
                   WAIT_MS);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print(
        "<span class=muted>ค่าที่ตั้งไว้ยังอยู่ ไม่ต้องตั้งใหม่รอบหน้า</span>");
    (void)printf("ต่อไม่ติด | broker = %s\n",
                 (broker != NULL) ? broker : "?");
}

static void show_linked(const platform_screen_t *screen,
                        uint32_t start_ms)
{
    char text[TEXT_BYTES];

    (void)bento_ui_set_color(screen->ui, screen->seg, COLOR_OK);
    (void)snprintf(
        text, sizeof(text), "ต่อติดใน %" PRId32 " ms",
        bento_time_ticks_diff(bento_time_ticks_ms(), start_ms));
    show(screen, screen->link, COLOR_OK, text);
    show(screen, screen->status, COLOR_OK, "พร้อมส่งแล้ว");
    poll_once(screen->ui);
    (void)bento_lcd_print("<span class=ok>ต่อติดแล้ว</span>");
}

/* ---- the cards ---- */

/* The knob in percent, or -1 when the snapshot fails or has no pot. */
static int32_t read_knob(void)
{
    bento_sensors_snapshot_t snapshot;
    int32_t knob = NO_KNOB;

    bento_sensors_snapshot_init(&snapshot);
    if ((bento_sensors_snapshot(SNAPSHOT_WAIT_MS, &snapshot) ==
         BENTO_OK) &&
        (snapshot.has_pot != 0U)) {
        knob = (int32_t)snapshot.pot_percent;
    }
    return knob;
}

/* json.dumps({"n": n, "knob": knob, "uptime_s": s}): the keys come out
 * in the order of MicroPython's hash table for a dict of 3, which the
 * qstr hash width decides. */
static void build_body(int32_t number, int32_t knob, char *out,
                       uint32_t cap)
{
    const int32_t uptime_s =
        (int32_t)(bento_time_ticks_ms() / MS_PER_S);

#if defined(BENTO_NATIVE_H)
    (void)snprintf(out, (size_t)cap,
                   "{\"uptime_s\": %" PRId32 ", \"knob\": %" PRId32
                   ", \"n\": %" PRId32 "}",
                   uptime_s, knob, number);
#else
    (void)snprintf(out, (size_t)cap,
                   "{\"n\": %" PRId32 ", \"knob\": %" PRId32
                   ", \"uptime_s\": %" PRId32 "}",
                   number, knob, uptime_s);
#endif
}

/* One card. publish() needs no topic: the firmware builds it from the
 * store's device_id. *dropped is true when the link was gone. */
static void send_card(const platform_screen_t *screen, int32_t number,
                      int32_t *sent, bool *dropped)
{
    char body[TEXT_BYTES];
    char text[TEXT_BYTES];
    const int32_t knob = read_knob();
    bool connected = false;

    build_body(number, knob, body, sizeof(body));
    *dropped = (bento_tesaiot_is_connected(&connected) != BENTO_OK) ||
               !connected;
    if (*dropped) {
        (void)snprintf(text, sizeof(text), "สายหลุดที่ใบที่ %" PRId32,
                       number);
        show(screen, screen->link, COLOR_BAD, text);
        poll_once(screen->ui);
        (void)snprintf(
            text, sizeof(text),
            "<span class=error>สายหลุดที่ใบที่ %" PRId32 " </span>", number);
        (void)bento_lcd_print(text);
    } else if (bento_tesaiot_publish(NULL, 0U, (const uint8_t *)body,
                                     (uint32_t)strlen(body), 0U,
                                     NULL) == BENTO_OK) {
        *sent = number;
        (void)snprintf(text, sizeof(text),
                       "ส่งแล้ว %" PRId32 " ใบ | knob = %" PRId32, *sent,
                       knob);
        show(screen, screen->note, COLOR_OK, text);
        (void)snprintf(text, sizeof(text), "ใบที่ %" PRId32 " -> %s",
                       number, body);
        (void)bento_lcd_print(text);
    } else {
        (void)snprintf(text, sizeof(text), "ใบที่ %" PRId32 " ถูกปฏิเสธ",
                       number);
        show(screen, screen->note, COLOR_WARN, text);
        (void)snprintf(
            text, sizeof(text),
            "<span class=warn>ใบที่ %" PRId32 " ถูกปฏิเสธ</span>", number);
        (void)bento_lcd_print(text);
    }
}

static bento_status_t send_cards(const platform_screen_t *screen)
{
    char text[TEXT_BYTES];
    int32_t sent = 0;
    int32_t number;
    bool dropped = false;
    bento_status_t status = BENTO_OK;

    for (number = 1;
         (number <= CARDS) && !dropped && (status == BENTO_OK);
         number++) {
        send_card(screen, number, &sent, &dropped);
        if (!dropped) {
            poll_once(screen->ui);
            status = sleep_checked(CARD_MS);
        }
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_color(screen->ui, screen->warn, COLOR_DIM);
        (void)snprintf(text, sizeof(text),
                       "จบแล้ว - ส่งได้ %" PRId32 " ใบ จาก %d", sent,
                       CARDS);
        show(screen, screen->status, COLOR_OK, text);
        poll_once(screen->ui);
        (void)bento_lcd_console(
            "<span class=muted>------------------------</span>");
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>ส่งได้ %" PRId32
                       " ใบ จาก %d </span>",
                       sent, CARDS);
        (void)bento_lcd_print(text);
        (void)printf("ส่งได้ %" PRId32 " ใบ | คลังค่าตั้งมี %d คีย์\n", sent,
                     CONFIG_KEYS);
    }
    return status;
}

/* Write, join, connect, send: each step ends the program normally
 * when it cannot go on, as the original's raise Stop does. */
static bento_status_t connect_and_send(const platform_screen_t *screen)
{
    bool online = false;
    bool linked = false;
    uint32_t start_ms = 0U;
    bento_status_t status = write_keys(screen);

    if (status == BENTO_OK) {
        status = go_online(screen, &online);
    }
    if ((status == BENTO_OK) && online) {
        status = start_link(screen);
        start_ms = bento_time_ticks_ms();
    }
    if ((status == BENTO_OK) && online) {
        status = wait_for_link(screen, start_ms, &linked);
    }
    if ((status == BENTO_OK) && online && !linked) {
        show_not_linked(screen);
    }
    if ((status == BENTO_OK) && linked) {
        show_linked(screen, start_ms);
        status = send_cards(screen);
    }
    return status;
}

int main(void)
{
    platform_screen_t screen;
    bool done = false;
    bento_status_t status;

    (void)memset(g_config, 0, sizeof(g_config));
    (void)memset(g_networks, 0, sizeof(g_networks));
    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        poll_once(screen.ui);
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>คลังค่าตั้งที่บอร์ดจำเอง</h2>");
        status = read_config();
        /* the Emulator answers every TESAIoT call, the store
         * included, with BENTO_E_UNSUPPORTED; the program ends here,
         * normally, with nothing written */
        if (status == BENTO_E_UNSUPPORTED) {
            show_emulator_note(screen.ui, screen.link, "ยังไม่สั่งต่อ",
                               screen.note);
            done = true;
            status = BENTO_OK;
        }
    }
    if ((status == BENTO_OK) && !done) {
        print_config();
        if (PLATFORM_BROKER[0] == '\0') {
            show_read_only(&screen);
        } else {
            status = connect_and_send(&screen);
        }
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
