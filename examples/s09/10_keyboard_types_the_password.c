/*
 * 10_keyboard_types_the_password.c -- รหัสผ่านพิมพ์บนจอ
 * (the keyboard types the password)
 *
 * ช่องซ้ายบนคือชื่อวงที่แรงที่สุดจาก wifi.scan() จริง ช่องขวาบนคือ
 * รหัสผ่านที่พิมพ์เองด้วยแป้นพิมพ์บนจอ (โหมดดาว) กดตกลงบนแป้นพิมพ์
 * หรือแตะปุ่ม "เชื่อมต่อ" แล้วโปรแกรมจะอ่านรหัสที่พิมพ์กลับมาจาก
 * ช่องจริง ๆ แล้วลองต่อวงนั้น ทำงาน 45 วินาทีแล้วจบ
 *
 * The upper left box holds the strongest network a real wifi.scan()
 * found; the upper right box is the password, typed on the on-screen
 * keyboard (shown as stars). Press OK on the keyboard or tap
 * "เชื่อมต่อ": the program reads the typed text back from the box
 * and tries to join that network. It runs for 45 seconds.
 *
 * Twin of 10_keyboard_types_the_password.py
 * (embedded-systems-for-aiot-developer/examples/s09/10_keyboard_types_the_password.py),
 * merged from the library twin aiot-s09-10-keyboard-types-the-password. Style:
 * CLEAN_C.md.
 *
 * Differs from the original, where C or the C API forces it:
 *  - wifi.scan() and wifi.connect() block in MicroPython; here they
 *    are started and polled every 50 ms, and a Stop cancels the wait.
 *    The binding's own console lines ("[WiFi] ...") are not printed:
 *    the C library logs nothing (bento_wifi/README.md). The
 *    credentials are saved on a good join, as the binding does.
 *  - every failed join is False here, as the binding answers it or
 *    the original's `except OSError` turns it; a password the C call
 *    refuses (1 to 7 bytes, or more than 64) is a failed join too.
 *  - a scan that fails is the original's `except OSError`: no
 *    network. On the reTerminal there is no wifi module (ImportError
 *    at the start); here the screen is drawn and the scan ends the
 *    program with BENTO_E_UNSUPPORTED.
 *  - ev["type"] printed on the console is the name ui.poll() gives
 *    the event (modui.c ui_event_type_name), ev["handle"] the slot.
 *  - wifi.ip() answers "0.0.0.0" when there is no address, as the
 *    binding does. ta.text() answers "" when the display does not.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"
#include "bento_wifi.h"

/* The scan's answer, at most 20 networks: too big for the 8 KB
 * program stack, so file scope, zeroed by the loader and set again at
 * the start of main() (CLEAN_C.md C9). */
extern bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];

/* Times, sizes and limits. */
enum {
    SETTLE_MS = 200,   /* let the new screen settle */
    WAIT_STEP_MS = 50, /* how often a scan or a join is polled */
    LOOP_MS = 80,
    RUN_MS = 45000,
    NO_RSSI = -200, /* below any radio's report: every network wins */
    TITLE_PX = 28,
    BUTTON_PX = 24,
    TEXT_PX = 20,    /* Thai below 20 px loses its strokes */
    TEXT_CHARS = 160 /* a text area's text: at most 127 bytes */
};

/* Colours, 0xRRGGBB: the course palette. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_OK = 0x30A46C
};

enum {
    ROLE_FIXED,
    ROLE_NETWORK,
    ROLE_PASSWORD,
    ROLE_KEYBOARD,
    ROLE_CONNECT,
    ROLE_NOTE,
    ROLE_COUNT
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t widgets[ROLE_COUNT];
    char network[BENTO_WIFI_SSID_MAX + 1U]; /* "" when none */
} keyboard_screen_t;

/* ---- Small helpers ---- */

static bento_status_t stop_raised_in(bento_status_t status)
{
    const bool stopped = !bento_runtime_is_running();

    return ((status == BENTO_OK) && stopped) ? BENTO_E_CANCELLED
                                             : status;
}

static bento_status_t wait_step(void)
{
    return stop_raised_in(bento_time_sleep_ms(WAIT_STEP_MS));
}

/* Python's len() of a str: characters, not bytes. */
static int32_t character_count(const char *text)
{
    const uint8_t top_bits = 0xC0U;
    const uint8_t continuation = 0x80U;
    int32_t count = 0;
    size_t i;

    for (i = 0U; text[i] != '\0'; i++) {
        if (((uint8_t)text[i] & top_bits) != continuation) {
            count++;
        }
    }
    return count;
}

/* The name ui.poll() gives an event type (modui.c). */
static const char *event_name(uint8_t type)
{
    static const char *const names[] = {
        "unknown",    "clicked",      "value_changed",
        "toggled",    "pressed",      "released",
        "press_lost", "long_pressed", "long_pressed_repeat",
        "ready",      "cancel",       "focused",
        "defocused",  "scroll_begin", "scroll_end",
        "gesture"};
    const size_t count = sizeof(names) / sizeof(names[0]);

    return ((size_t)type < count) ? names[type] : names[0];
}

/* wifi.connect(ssid, password): true when joined. */
static bento_status_t join_wifi(const char *ssid, const char *password,
                                bool *joined)
{
    bento_wifi_connect_params_t params;
    bento_status_t answer = BENTO_E_IN_PROGRESS;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = ssid;
    params.ssid_len = (uint32_t)strlen(ssid);
    params.pass = password;
    params.pass_len = (uint32_t)strlen(password);
    params.flags = BENTO_WIFI_F_SAVE_CREDS; /* as the binding does */
    status = bento_wifi_connect_start(&params);
    while ((status == BENTO_OK) && (answer == BENTO_E_IN_PROGRESS)) {
        answer = bento_wifi_connect_poll();
        if (answer == BENTO_E_IN_PROGRESS) {
            status = wait_step();
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* Stop: give up */
    }
    *joined = (status == BENTO_OK) && (answer == BENTO_OK);
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* wifi.ip(): the address, or "0.0.0.0" when there is none. */
static void read_ip(char text[BENTO_WIFI_IPV4_TEXT_MAX])
{
    uint32_t address = 0U;
    uint32_t length = 0U;

    if ((bento_wifi_ip(BENTO_WIFI_IF_ANY, &address) != BENTO_OK) ||
        (bento_wifi_ipv4_format(address, text, BENTO_WIFI_IPV4_TEXT_MAX,
                                &length) != BENTO_OK)) {
        (void)snprintf(text, BENTO_WIFI_IPV4_TEXT_MAX, "0.0.0.0");
    }
}

/* The kinds of widget the layout creates, and their roles. */
enum {
    WIDGET_LABEL,
    WIDGET_TEXTAREA,
    WIDGET_KEYBOARD,
    WIDGET_BUTTON,
    WIDGET_KIND_COUNT
};

typedef struct {
    int32_t kind;
    int32_t role;
    const char *text; /* NULL: the widget has none */
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t size_px;
} widget_spec_t;

typedef bento_ui_status_t (*widget_factory_t)(
    bento_ui_t *ui, const bento_ui_widget_args_t *args,
    bento_ui_id_t *out_id);

/* The widget setters: MicroPython's ignore a refusal (modui.c). */
static void show_text(const keyboard_screen_t *screen, int32_t role,
                      const char *text)
{
    (void)bento_ui_set_text(screen->ui, screen->widgets[role], text,
                            (uint32_t)strlen(text));
}

static void show_color(const keyboard_screen_t *screen, int32_t role,
                       uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, screen->widgets[role], color);
}

/* lcd.print() ignores a refused message (modlcd.c). */
static void lcd_line(const char *text)
{
    (void)bento_lcd_print(text);
}

/* ---- The screen ---- */

static bento_status_t add_widget(keyboard_screen_t *screen,
                                 const widget_spec_t *spec)
{
    static const widget_factory_t factories[WIDGET_KIND_COUNT] = {
        bento_ui_label_create, bento_ui_textarea_create,
        bento_ui_keyboard_create, bento_ui_button_create};
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
    args.font_px = spec->size_px;
    return stop_raised_in(factories[spec->kind](
        screen->ui, &args, &screen->widgets[spec->role]));
}

static bento_status_t add_widgets(keyboard_screen_t *screen,
                                  const widget_spec_t *specs,
                                  size_t count)
{
    size_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = add_widget(screen, &specs[i]);
    }
    return status;
}

/* The two boxes. The password box is one line (which overrides the
 * h= asked for) and shows stars. */
static bento_status_t build_boxes(keyboard_screen_t *screen)
{
    static const widget_spec_t boxes[] = {
        {WIDGET_LABEL, ROLE_FIXED, "รหัสผ่านพิมพ์บนจอ", 24, 16, -1, -1,
         COLOR_TEXT, TITLE_PX},
        {WIDGET_LABEL, ROLE_FIXED, "ชื่อวง - จาก wifi.scan()", 24, 64, -1,
         -1, COLOR_DIM, TEXT_PX},
        {WIDGET_TEXTAREA, ROLE_NETWORK, "กำลังสแกน", 24, 96, 328, 88,
         COLOR_TEXT, TEXT_PX},
        {WIDGET_LABEL, ROLE_FIXED, "รหัสผ่าน - พิมพ์เอง", 384, 64, -1, -1,
         COLOR_DIM, TEXT_PX},
        {WIDGET_TEXTAREA, ROLE_PASSWORD, "", 384, 96, 304, 88,
         COLOR_TEXT, 0}};
    const bento_status_t status =
        add_widgets(screen, boxes, sizeof(boxes) / sizeof(boxes[0]));

    if (status == BENTO_OK) { /* .prop() is a setter (modui.c) */
        (void)bento_ui_set_prop(screen->ui,
                                screen->widgets[ROLE_PASSWORD],
                                BENTO_UI_PROP_ONE_LINE, 1);
        (void)bento_ui_set_prop(screen->ui,
                                screen->widgets[ROLE_PASSWORD],
                                BENTO_UI_PROP_PASSWORD, 1);
    }
    return status;
}

/* The keyboard, bound to the password box after it exists, and
 * asked for its ready and cancel events (none are sent unasked). */
static bento_status_t build_keyboard(keyboard_screen_t *screen)
{
    static const widget_spec_t keyboard = {WIDGET_KEYBOARD,
                                           ROLE_KEYBOARD,
                                           NULL,
                                           24,
                                           200,
                                           440,
                                           168,
                                           COLOR_TEXT,
                                           0};
    static const widget_spec_t controls[] = {
        {WIDGET_BUTTON, ROLE_CONNECT, "เชื่อมต่อ", 496, 200, 192, 88,
         COLOR_ACCENT, BUTTON_PX},
        {WIDGET_LABEL, ROLE_NOTE, "แตะเพื่อลองต่อ", 496, 304, -1, -1,
         COLOR_DIM, TEXT_PX}};
    const uint32_t endings = BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_READY) |
                             BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_CANCEL);
    bento_status_t status = add_widget(screen, &keyboard);

    /* .bind() and .listen() are setters: modui.c ignores a refusal */
    if (status == BENTO_OK) {
        (void)bento_ui_bind_textarea(screen->ui,
                                     screen->widgets[ROLE_KEYBOARD],
                                     screen->widgets[ROLE_PASSWORD]);
        (void)bento_ui_listen_compat(
            screen->ui, screen->widgets[ROLE_KEYBOARD], endings);
        status = add_widgets(screen, controls,
                             sizeof(controls) / sizeof(controls[0]));
    }
    return status;
}

static bento_status_t build_screen(keyboard_screen_t *screen)
{
    bento_status_t status =
        stop_raised_in(bento_ui_screen_default(screen->ui));

    if (status == BENTO_OK) {
        status = stop_raised_in(bento_time_sleep_ms(SETTLE_MS));
    }
    if (status == BENTO_OK) {
        status = build_boxes(screen);
    }
    if (status == BENTO_OK) {
        status = build_keyboard(screen);
    }
    return status;
}

static void show_network(const keyboard_screen_t *screen)
{
    char line[TEXT_CHARS];

    if (screen->network[0] != '\0') {
        show_text(screen, ROLE_NETWORK, screen->network);
        (void)snprintf(line, sizeof(line), "วงที่แรงที่สุดตอนนี้: %s",
                       screen->network);
        lcd_line(line);
    } else {
        show_text(screen, ROLE_NETWORK, "สแกนไม่เจอวงไหนเลย");
        lcd_line("<span class=warn>สแกนไม่เจอวง - ตรวจสายอากาศ</span>");
    }
}

/* ---- Trying the typed password ---- */

/* ta.text(): what the box holds, "" when the display does not say. */
static void read_password(const keyboard_screen_t *screen, char *text,
                          uint32_t size)
{
    if (bento_ui_get_text(screen->ui, screen->widgets[ROLE_PASSWORD],
                          text, size, NULL) != BENTO_OK) {
        text[0] = '\0';
    }
}

static void show_attempt(const keyboard_screen_t *screen, bool joined,
                         const char *password)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    char line[TEXT_CHARS];

    if (joined) {
        show_color(screen, ROLE_NOTE, COLOR_OK);
        read_ip(address);
        (void)snprintf(line, sizeof(line), "ต่อได้ ip %s", address);
        show_text(screen, ROLE_NOTE, line);
        read_ip(address);
        (void)snprintf(line, sizeof(line), "ต่อสำเร็จ %s", address);
        lcd_line(line);
    } else {
        show_color(screen, ROLE_NOTE, COLOR_DIM);
        show_text(screen, ROLE_NOTE, "ต่อไม่ได้ - ลองพิมพ์รหัสใหม่");
        /* the length, never the password: the whole class sees it */
        (void)snprintf(line, sizeof(line),
                       "ต่อไม่สำเร็จ - รหัสที่อ่านกลับได้ยาว %d ตัว",
                       (int)character_count(password));
        lcd_line(line);
    }
}

/* ---- The network ---- */

/* wifi.scan() into g_networks; *count is 0 when it failed. */
static bento_status_t scan_networks(uint32_t *count)
{
    bool finished = false;
    bento_status_t status = bento_wifi_scan_start();

    *count = 0U;
    while ((status == BENTO_OK) && !finished) {
        const bento_status_t answer = bento_wifi_scan_poll(
            g_networks, BENTO_WIFI_SCAN_MAX, count);

        finished = answer != BENTO_E_IN_PROGRESS;
        status = finished ? answer : wait_step();
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_scan_cancel(); /* Stop: give up */
    }
    return status;
}

/* The strongest network's name; a failed scan is the original's
 * `except OSError`: no network. */
static bento_status_t find_strongest(keyboard_screen_t *screen)
{
    int32_t best_rssi = NO_RSSI;
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = scan_networks(&count);

    screen->network[0] = '\0';
    for (i = 0U; (status == BENTO_OK) && (i < count); i++) {
        if ((int32_t)g_networks[i].rssi_dbm > best_rssi) {
            best_rssi = (int32_t)g_networks[i].rssi_dbm;
            (void)snprintf(screen->network, sizeof(screen->network),
                           "%s", g_networks[i].ssid);
        }
    }
    if ((status != BENTO_E_CANCELLED) &&
        (status != BENTO_E_UNSUPPORTED)) {
        status = BENTO_OK;
    }
    return status;
}

/* Reads the typed password back and tries it on the network. */
static bento_status_t try_password(const keyboard_screen_t *screen)
{
    bento_ui_event_t dropped[BENTO_UI_POLL_EVENTS_MAX];
    char password[TEXT_CHARS];
    uint32_t count = 0U;
    bool joined = false;
    bento_status_t status;

    read_password(screen, password, sizeof(password));
    show_color(screen, ROLE_NOTE, COLOR_WARN);
    show_text(screen, ROLE_NOTE, "กำลังต่อด้วยรหัสที่พิมพ์");
    /* the original polls here and never reads those events */
    (void)bento_ui_poll(screen->ui, dropped, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
    status = stop_raised_in(BENTO_OK);
    if ((status == BENTO_OK) && (screen->network[0] != '\0')) {
        status = join_wifi(screen->network, password, &joined);
    }
    if (status == BENTO_OK) {
        show_attempt(screen, joined, password);
    }
    return status;
}

/* cancel: a note; ready, or a tap on "เชื่อมต่อ": a try; anything
 * else is written on the console. */
static bento_status_t handle_event(const keyboard_screen_t *screen,
                                   const bento_ui_event_t *event)
{
    const bool tap_connect =
        (event->id == screen->widgets[ROLE_CONNECT]) &&
        (event->type == (uint8_t)BENTO_UI_EVENT_CLICKED);
    char line[TEXT_CHARS];
    bento_status_t status = BENTO_OK;

    if (event->type == (uint8_t)BENTO_UI_EVENT_CANCEL) {
        show_color(screen, ROLE_NOTE, COLOR_DIM);
        show_text(screen, ROLE_NOTE, "ปิดแป้นพิมพ์ - ยังไม่ได้ต่อ");
    } else if ((event->type == (uint8_t)BENTO_UI_EVENT_READY) ||
               tap_connect) {
        status = try_password(screen);
    } else {
        (void)snprintf(line, sizeof(line), "event: %s handle %d",
                       event_name(event->type),
                       (int)bento_ui_id_to_handle(event->id));
        lcd_line(line);
    }
    return status;
}

/* Reads the events every LOOP_MS for RUN_MS. */
static bento_status_t run_event_loop(const keyboard_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        /* a failed poll answers no events */
        if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                          &count, NULL) != BENTO_OK) {
            count = 0U;
        }
        status = stop_raised_in(BENTO_OK);
        for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
            status = handle_event(screen, &events[i]);
        }
        if (status == BENTO_OK) {
            status = stop_raised_in(bento_time_sleep_ms(LOOP_MS));
        }
    }
    return status;
}

int main(void)
{
    keyboard_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(g_networks, 0, sizeof(g_networks));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear(); /* ignored refusal (modlcd.c) */
        (void)bento_lcd_console("<h2>Keyboard - แป้นพิมพ์บนจอ</h2>");
        lcd_line("พิมพ์รหัสลงช่องขวา แล้วกดตกลงบนแป้นพิมพ์ "
                 "หรือกดปุ่มเชื่อมต่อ");
        lcd_line("ta.text() ที่ไม่ใส่อาร์กิวเมนต์ "
                 "อ่านสิ่งที่พิมพ์กลับมาได้");
        status = find_strongest(&screen);
    }
    if (status == BENTO_OK) {
        show_network(&screen);
        status = run_event_loop(&screen);
    }
    if (status == BENTO_OK) {
        show_text(&screen, ROLE_NOTE, "หมดเวลา");
        lcd_line("<span class=ok>จบ - รหัสที่พิมพ์ถูกใช้ต่อจริง"
                 "</span>");
        (void)printf("Keyboard วาดได้ ผูกช่องได้ และส่งค่ากลับ "
                     "MicroPython ได้แล้ว\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
