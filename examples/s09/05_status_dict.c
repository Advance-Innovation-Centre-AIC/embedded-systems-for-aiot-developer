/*
 * 05_status_dict.c -- อ่านสถานะครั้งเดียวต่อรอบ
 * (Read the status once a round)
 *
 * อ่าน wifi.status() ครั้งเดียวแล้วใช้ค่าชุดนั้นทั้งรอบ: ก่อนต่อ
 * หลังต่อ แล้วเทียบว่าคีย์ไหนเปลี่ยน จากนั้นเฝ้าดู 14 รอบ
 * รอบละการอ่านเดียว สามคีย์บนจอ (connected, ip, mode)
 * จึงมาจากจังหวะเวลาเดียวกันเสมอ ลองปิดเราเตอร์ระหว่างเฝ้าดู
 * แล้วดูว่าสามค่าเปลี่ยนพร้อมกัน
 * ก่อนรันแก้ชื่อวง WIFI_SSID (เหนือฟังก์ชัน join_wifi)
 * และรหัสผ่าน wifi_pass (ต้นฟังก์ชัน join_wifi)
 *
 * Reads wifi.status() once and uses that one reading for the whole
 * round: before joining, after joining, then says which keys
 * changed. Then it watches 14 rounds, one reading each, so the
 * three keys on the screen (connected, ip, mode) always come from
 * the same moment. Switch the router off while it watches: all
 * three change together. Set the network name WIFI_SSID (just above
 * the function join_wifi) and the password wifi_pass (at the top of
 * the function join_wifi) first.
 *
 * Twin of 05_status_dict.py
 * (embedded-systems-for-aiot-developer/examples/s09/05_status_dict.py),
 * merged from the library twin aiot-s09-05-status-dict. Style: CLEAN_C.md.
 *
 * Differs from the original:
 *   - wifi.status() is a dict; here it is bento_wifi_status(), one
 *     reading into a struct, and each key's text is written the
 *     way Python prints the dict's value (True, "sta", ...). The
 *     dict has no "channel" key, so st.get("channel", default)
 *     gives the default, here as there.
 *   - wifi.connect() prints console lines of its own. This program
 *     prints the two C can know, "[WiFi] Connecting..." and
 *     "[WiFi] Connected! IP=...". No C function reports the others:
 *     each failed attempt with the radio's code, "Network not seen
 *     yet...", "Connect failed after N attempts (last 0x...)" and
 *     "[WiFi] Credentials saved to QSPI (N entries)" (modwifi.c).
 *   - Stop ends the program quietly where MicroPython raises
 *     KeyboardInterrupt.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* The course palette, 0xRRGGBB. */
enum {
    COL_TEXT = 0xE8EAED,
    COL_DIM = 0x9AA3AF,
    COL_CARD = 0x171B22,
    COL_OK = 0x30A46C,
    COL_WARN = 0xF5A623,
    COL_BAD = 0xE5484D
};

/* Times in milliseconds, and counts. */
enum {
    SETTLE_MS = 200,
    BEFORE_PAUSE_MS = 1200,
    WAIT_STEP_MS = 50, /* between two polls of connect */
    WATCH_ROUNDS = 14,
    ROUND_STEPS = 3,
    STEP_MS = 160,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 */
};

/* The three keys the firmware really measures. */
enum {
    KEY_CONNECTED = 0,
    KEY_IP = 1,
    KEY_MODE = 2,
    KEY_COUNT = 3
};

/* One reading of wifi.status(), the keys this program uses. */
typedef struct {
    bool connected;
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    const char *mode;
} reading_t;

/* The widgets the program changes. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t rows[KEY_COUNT];
    bento_ui_id_t ssid;
    bento_ui_id_t phase;
    bento_ui_id_t same;
} screen_t;

static int16_t px(int32_t value)
{
    return (int16_t)value;
}

static const char *bool_text(bool value)
{
    return value ? "True" : "False";
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

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void lcd_line(const char *text)
{
    (void)bento_lcd_print(text);
}

/* wifi.status(): one reading, the three keys from one moment. */
static bento_status_t read_status(reading_t *reading)
{
    bento_wifi_status_t link;
    uint32_t length = 0U;
    bento_status_t status;

    bento_wifi_status_init(&link);
    status = bento_wifi_status(&link);
    if (status == BENTO_OK) {
        reading->connected = link.connected;
        /* a BENTO_WIFI_IPV4_TEXT_MAX buffer holds any address */
        (void)bento_wifi_ipv4_format(link.ip_v4, reading->ip,
                                     (uint32_t)sizeof(reading->ip),
                                     &length);
        if (link.mode == BENTO_WIFI_MODE_STA) {
            reading->mode = "sta";
        } else if (link.mode == BENTO_WIFI_MODE_IDLE) {
            reading->mode = "idle";
        } else {
            reading->mode = "off";
        }
    }
    return status;
}

/* st[key] as Python prints it. */
static const char *value_of(const reading_t *reading, int32_t key)
{
    const char *value = reading->mode;

    if (key == KEY_CONNECTED) {
        value = bool_text(reading->connected);
    } else if (key == KEY_IP) {
        value = reading->ip;
    } else {
        /* the mode */
    }
    return value;
}

/* wifi.is_connected(): it never raises, so a refusal is False. */
static bool is_connected(void)
{
    bool connected = false;

    if (bento_wifi_is_connected(&connected) != BENTO_OK) {
        connected = false;
    }
    return connected;
}

/* Layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 20,
    TITLE_Y = 12,
    CARD_X = 20,
    CARD_Y = 48,
    CARD_W = 652,
    CARD_H = 160,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    ROW_X = 40,
    ROW_Y = 64,
    ROW_STEP = 34,
    NOTE_X = 380,
    NOTE_1_Y = 58,
    NOTE_2_Y = 86,
    SOURCE_X = 316,
    SOURCE_Y = 114,
    SSID_Y = 142,
    PHASE_Y = 216,
    SAME_Y = 248,
    TITLE_PX = 24,
    TEXT_PX = 20
};

static const char *const KEYS[KEY_COUNT] = {"connected", "ip", "mode"};

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(const screen_t *screen, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(screen->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void set_color(const screen_t *screen, bento_ui_id_t widget,
                      uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, widget, color);
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

static bento_status_t add_label(const screen_t *screen,
                                const char *text, const int16_t spot[2],
                                uint32_t color, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = spot[0];
    args.y = spot[1];
    args.color = color;
    args.font_px = TEXT_PX;
    return bento_ui_label_create(screen->ui, &args, out_id);
}

/* The title and the card the three rows sit on. */
static bento_status_t add_title_and_card(const screen_t *screen)
{
    const char *title = "คาบ 9 - อ่านสถานะครั้งเดียวต่อรอบ";
    bento_ui_widget_args_t args;
    bento_ui_id_t widget;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = title;
    args.text_len = (uint32_t)strlen(title);
    args.x = TITLE_X;
    args.y = TITLE_Y;
    args.color = COL_TEXT;
    args.font_px = TITLE_PX;
    status = bento_ui_label_create(screen->ui, &args, &widget);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = CARD_X;
        args.y = CARD_Y;
        args.w = CARD_W;
        args.h = CARD_H;
        args.color = COL_CARD;
        args.min_val = COL_DIM; /* the border colour */
        args.max_val = CARD_RADIUS;
        args.font_px = CARD_BORDER;
        status = bento_ui_panel_create(screen->ui, &args, &widget);
    }
    return status;
}

/* One label a key, made once; later only its text changes (the 32
 * labels would run out in seconds otherwise). */
static bento_status_t add_rows(screen_t *screen)
{
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    int32_t key;

    for (key = 0; (key < KEY_COUNT) && (status == BENTO_OK); key++) {
        const int16_t spot[2] = {ROW_X, px(ROW_Y + (key * ROW_STEP))};

        (void)snprintf(text, sizeof(text), "%s = -", KEYS[key]);
        status =
            add_label(screen, text, spot, COL_TEXT, &screen->rows[key]);
    }
    return status;
}

/* The right column: why ssid and rssi are left out, and where the
 * network's name really comes from. */
static bento_status_t add_notes(screen_t *screen)
{
    const int16_t note_1[2] = {NOTE_X, NOTE_1_Y};
    const int16_t note_2[2] = {NOTE_X, NOTE_2_Y};
    const int16_t source[2] = {SOURCE_X, SOURCE_Y};
    const int16_t ssid[2] = {NOTE_X, SSID_Y};
    bento_ui_id_t widget;
    bento_status_t status = add_label(
        screen, "คีย์ ssid กับ rssi ยังไม่มีใครวัด", note_1, COL_DIM, &widget);

    if (status == BENTO_OK) {
        status = add_label(screen,
                           "ซอร์สเขียนค่าไว้ตรง ๆ เป็น \"\" "
                           "กับ 0",
                           note_2, COL_DIM, &widget);
    }
    if (status == BENTO_OK) {
        status = add_label(screen,
                           "ชื่อวงที่ต่อ = สตริงที่เราส่งเข้า "
                           "connect()",
                           source, COL_DIM, &widget);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, "-", ssid, COL_OK, &screen->ssid);
    }
    return status;
}

static bento_status_t build_screen(screen_t *screen)
{
    const int16_t phase[2] = {TITLE_X, PHASE_Y};
    const int16_t same[2] = {TITLE_X, SAME_Y};
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_title_and_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_rows(screen);
    }
    if (status == BENTO_OK) {
        status = add_notes(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, "รอบที่ 1 - ก่อนต่อ", phase, COL_DIM,
                           &screen->phase);
    }
    if (status == BENTO_OK) {
        status = add_label(screen, "-", same, COL_DIM, &screen->same);
    }
    if (status == BENTO_OK) {
        set_text(screen, screen->same, "ทุกค่าบนจอมาจากการอ่านครั้งเดียวกัน");
        drain_events(screen);
    }
    return status;
}

/* show(): writes over the same labels, so the screen is one page
 * whose values change, not a list that scrolls. */
static void show(const screen_t *screen, const reading_t *reading,
                 const char *phase)
{
    char text[TEXT_BYTES];
    int32_t key;

    for (key = 0; key < KEY_COUNT; key++) {
        (void)snprintf(text, sizeof(text), "%s = %s", KEYS[key],
                       value_of(reading, key));
        set_text(screen, screen->rows[key], text);
    }
    set_color(screen, screen->rows[KEY_CONNECTED],
              reading->connected ? (uint32_t)COL_OK
                                 : (uint32_t)COL_BAD);
    set_text(screen, screen->phase, phase);
    drain_events(screen);
}

/* dump(): one reading, written to the screen's console and to the
 * serial console, plus a key the dict does not have. */
static bento_status_t dump(const char *title, reading_t *reading)
{
    const char *missing = "ไม่มีคีย์นี้"; /* st.get()'s default */
    char line[TEXT_BYTES];
    int32_t key;
    bento_status_t status = read_status(reading);

    if (status == BENTO_OK) {
        (void)snprintf(line, sizeof(line), "<b>--- %s ---</b>", title);
        lcd_line(line);
        (void)printf("--- %s ---\n", title);
        for (key = 0; key < KEY_COUNT; key++) {
            (void)snprintf(line, sizeof(line), "  %s = %s", KEYS[key],
                           value_of(reading, key));
            lcd_line(line);
            (void)printf("  %-10s = %s\n", KEYS[key],
                         value_of(reading, key));
        }
        (void)snprintf(line, sizeof(line),
                       "  channel = %s "
                       "(ไม่มีคีย์นี้จริง)",
                       missing);
        lcd_line(line);
        (void)printf("  %-10s = %s  (คีย์นี้ไม่มีจริง"
                     " จึงได้ค่าที่เราตั้งไว้)\n",
                     "channel", missing);
    }
    return status;
}

/* Which keys the join changed: the question to ask on site. */
static void print_changes(const reading_t *before,
                          const reading_t *after)
{
    char line[TEXT_BYTES];
    int32_t key;

    lcd_line("<b>คีย์ที่เปลี่ยนไปหลังต่อ</b>");
    (void)printf("คีย์ที่เปลี่ยนไปหลังต่อ:\n");
    for (key = 0; key < KEY_COUNT; key++) {
        const char *was = value_of(before, key);
        const char *now = value_of(after, key);

        if (strcmp(was, now) != 0) {
            (void)snprintf(line, sizeof(line), "  %s: %s -> %s",
                           KEYS[key], was, now);
            lcd_line(line);
            (void)printf("  %-10s %s -> %s\n", KEYS[key], was, now);
        }
    }
}

/* is_connected() and status()["connected"] answer the same
 * question; each print asks is_connected() again, as there. */
static void compare_connected(const reading_t *after)
{
    char line[TEXT_BYTES];

    (void)snprintf(line, sizeof(line),
                   "is_connected() = %s - [\"connected\"] = %s",
                   bool_text(is_connected()),
                   bool_text(after->connected));
    lcd_line(line);
    (void)printf("is_connected() = %s\n", bool_text(is_connected()));
    (void)printf("status()[\"connected\"] = %s\n",
                 bool_text(after->connected));
}

/* Not joined: say so and end without the watch. */
static void skip_watch(const screen_t *screen)
{
    set_color(screen, screen->same, COL_BAD);
    set_color(screen, screen->phase, COL_BAD);
    set_text(screen, screen->phase, "ยังไม่ได้ต่อ ข้ามรอบเฝ้าดู");
    set_text(screen, screen->same,
             "ตรวจ WIFI_SSID กับรหัส Wi-Fi ในโค้ดไฟล์นี้");
    drain_events(screen);
    lcd_line("<span class=err>ยังไม่ได้ต่อ ข้ามรอบเฝ้าดู</span>");
}

/* The classroom network the teacher hands out (its password is set
 * where the program joins). */
static const char WIFI_SSID[] = "AIoT-Class";

/* wifi.connect(): *joined is its True or False. A refused or
 * timed-out join is False; any other failure ends the program,
 * as the binding's OSError does. */
static bento_status_t join_wifi(bool *joined)
{
    /* the classroom password, WIFI_PASS in the .py: set it before you
     * run */
    static const char wifi_pass[] = "<รหัสผ่านของห้องเรียน>";
    bento_wifi_connect_params_t params;
    reading_t reading;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = wifi_pass;
    params.pass_len = (uint32_t)strlen(wifi_pass);
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
        status = read_status(&reading);
    }
    if (*joined && (status == BENTO_OK)) {
        (void)printf("[WiFi] Connected! IP=%s\r\n", reading.ip);
    }
    return status;
}

/* The join, between the two readings. The network's name is known
 * because the program sent it, not because the board says it. */
static bento_status_t join(const screen_t *screen, bool *joined)
{
    char text[TEXT_BYTES];
    bento_status_t status;

    set_color(screen, screen->phase, COL_WARN);
    (void)snprintf(text, sizeof(text),
                   "กำลังต่อ %s ... "
                   "จอจะนิ่งครู่หนึ่ง",
                   WIFI_SSID);
    set_text(screen, screen->phase, text);
    drain_events(screen);
    (void)snprintf(text, sizeof(text), "กำลังต่อ %s - อาจรอนาน",
                   WIFI_SSID);
    lcd_line(text);
    status = join_wifi(joined);
    if (status == BENTO_OK) {
        (void)printf("connect() คืนค่า %s\n", bool_text(*joined));
        set_text(screen, screen->ssid,
                 *joined ? WIFI_SSID : "ยังไม่ได้ต่อวงไหน");
        set_color(screen, screen->ssid,
                  *joined ? (uint32_t)COL_OK : (uint32_t)COL_BAD);
    }
    return status;
}

/* One watch round: one reading, and the log only when the address
 * changed, so the important lines are not pushed off the console. */
static bento_status_t watch_round(const screen_t *screen, int32_t round,
                                  char *last_ip, size_t ip_size)
{
    reading_t reading;
    char text[TEXT_BYTES];
    int32_t step;
    bento_status_t status = read_status(&reading);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "เฝ้าดูรอบที่ %d/%d",
                       (int)(round + 1), WATCH_ROUNDS);
        show(screen, &reading, text);
        (void)snprintf(text, sizeof(text),
                       "รอบนี้อ่าน status() ไป 1 ครั้ง ใช้ค่า %d ตัว",
                       KEY_COUNT);
        set_text(screen, screen->same, text);
        if (strcmp(reading.ip, last_ip) != 0) {
            (void)snprintf(text, sizeof(text),
                           "  รอบ %d IP เปลี่ยน %s -> %s",
                           (int)(round + 1), last_ip, reading.ip);
            lcd_line(text);
            (void)snprintf(last_ip, ip_size, "%s", reading.ip);
        }
        (void)printf("  รอบ %d connected = %s ip = %s\n",
                     (int)(round + 1), bool_text(reading.connected),
                     reading.ip);
    }
    for (step = 0; (step < ROUND_STEPS) && (status == BENTO_OK);
         step++) {
        drain_events(screen);
        status = wait_ms(STEP_MS);
    }
    return status;
}

/* The watch: 14 rounds, one reading each. If the link drops, all
 * three values change in the same round, never one by one. */
static bento_status_t watch(const screen_t *screen,
                            const reading_t *after)
{
    char last_ip[BENTO_WIFI_IPV4_TEXT_MAX];
    int32_t round;
    bento_status_t status = BENTO_OK;

    set_color(screen, screen->same, COL_OK);
    lcd_line("<b>เฝ้าดู</b> "
             "ลองปิดเราเตอร์แล้วดูว่าสามค่าเปลี่ยนพร้อมกันไหม");
    (void)snprintf(last_ip, sizeof(last_ip), "%s", after->ip);
    for (round = 0; (round < WATCH_ROUNDS) && (status == BENTO_OK);
         round++) {
        status = watch_round(screen, round, last_ip, sizeof(last_ip));
    }
    if (status == BENTO_OK) {
        set_text(screen, screen->phase, "จบการเฝ้าดู");
        set_text(screen, screen->same,
                 "ทุกแถวบนจอมาจากการอ่าน status() ครั้งเดียวกัน");
        drain_events(screen);
        (void)printf("กติกาข้อเดียวของไฟล์นี้: หนึ่งรอบ"
                     " อ่าน status() หนึ่งครั้ง\n");
    }
    return status;
}

/* Before, the join, after, and what changed. */
static bento_status_t before_and_after(const screen_t *screen,
                                       reading_t *after)
{
    reading_t before;
    bool joined = false;
    bento_status_t status = dump("ก่อนต่อ", &before);

    if (status == BENTO_OK) {
        show(screen, &before, "รอบที่ 1 - ก่อนต่อ");
        status = wait_ms(BEFORE_PAUSE_MS);
    }
    if (status == BENTO_OK) {
        status = join(screen, &joined);
    }
    if (status == BENTO_OK) {
        status = dump("หลังต่อ", after);
    }
    if (status == BENTO_OK) {
        set_color(screen, screen->phase,
                  joined ? (uint32_t)COL_OK : (uint32_t)COL_BAD);
        show(screen, after, "รอบที่ 2 - หลังต่อ");
        print_changes(&before, after);
        compare_connected(after);
    }
    return status;
}

int main(void)
{
    screen_t screen;
    reading_t after;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    /* the lcd calls ignore a console that is not ready (modlcd.c) */
    (void)bento_lcd_clear();
    (void)bento_lcd_console(
        "<h2>คาบ 9 - อ่าน status() ครั้งเดียวต่อรอบ</h2>");
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        status = before_and_after(&screen, &after);
    }
    if ((status == BENTO_OK) && !after.connected) {
        skip_watch(&screen);
    } else if (status == BENTO_OK) {
        status = watch(&screen, &after);
    } else {
        /* an error or Stop ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
