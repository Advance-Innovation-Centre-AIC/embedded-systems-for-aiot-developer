/*
 * 06_secure_publish_loop.c -- ส่งขึ้นแพลตฟอร์มผ่าน TLS (Secure Publish Loop)
 *
 * ต่อ WiFi ตั้งค่าแพลตฟอร์ม TESAIoT เป็นโหมด server TLS แล้วต่อสายเข้ารหัส
 * รอ is_connected() ไม่เกิน 30 วินาที จากนั้นส่ง {"seq": n, "uptime_s": t}
 * ทุกสองวินาที โดยเช็กสายก่อนส่งทุกครั้ง จอโชว์โหมดกับพอร์ตที่ใช้จริง เลขใหญ่
 * นับครั้งที่ส่งสำเร็จ บรรทัดสายเปลี่ยนสีเขียวหรือแดง และกราฟลงจังหวะระหว่าง
 * สองครั้งที่ส่ง ทำงานจนสายหลุดหรือกด Stop
 *
 * Joins Wi-Fi, sets the TESAIoT platform to server-TLS mode, starts the
 * encrypted link and waits up to 30 s for is_connected(). Then it sends
 * {"seq": n, "uptime_s": t} every two seconds, checking the link before
 * every send. The screen shows the mode and port in use, a big count of
 * messages sent, a link line that turns green or red, and a chart of the
 * gap between two sends. Runs until the link drops or Stop is pressed.
 *
 * Twin of 06_secure_publish_loop.py
 * (embedded-systems-for-aiot-developer/examples/s11/06_secure_publish_loop.py),
 * from the library twin aiot-s11-06-secure-publish-loop. Style: CLEAN_C.md.
 * What C changes:
 *   - There is no json module in C: the payload is written with snprintf
 *     as json.dumps writes it, keys in MicroPython's dict order.
 *   - tesaiot.config() builds a dict of 19 keys; only tls_mode is read,
 *     so only that field is asked for here.
 *   - tesaiot.publish() answers True or False: BENTO_OK (the message is
 *     queued) is True and any refusal is False.
 *   - tesaiot.config_set() and tesaiot.connect() answer True or False,
 *     which the original ignores; BENTO_OK and BENTO_E_FAILED are ignored
 *     the same way here. Any other status ends the program.
 *   - wifi.connect() prints console lines of its own. Here the program
 *     prints "[WiFi] Connecting..." and "[WiFi] Connected! IP=..."; the
 *     radio's per-attempt lines are not reported by the C API.
 *   - Stop ends the program quietly, where MicroPython raises
 *     KeyboardInterrupt.
 * In the BENTO Emulator, the TESAIoT platform from C is not available yet: the program says
 * so and ends.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_tesaiot.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 20,
    TITLE_Y = 12,
    TITLE_PX = 24,
    TEXT_PX = 20,
    CARD_Y = 52,
    CARD_W = 652,
    CARD_H = 120,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    INNER_X = 40,
    MODE_Y = 64,
    SEG_Y = 96,
    SEG_W = 152,
    SEG_H = 48,
    SEG_CAPTION_Y = 144, /* under the Seg7, which ends at y = 144 */
    LINK_X = 360,
    LINK_Y = 100,
    CHART_CAPTION_Y = 174,
    CHART_Y = 212,
    CHART_H = 120,
    STATUS_Y = 332,
    EMU_NOTE_W = 752 /* the Emulator note wraps on the status line */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

/* Timing and sizes. */
enum {
    SETTLE_MS = 200,       /* let the cleared screen settle */
    SEND_EVERY_MS = 2000,  /* often enough to see the rhythm */
    TICK_MS = 100,         /* short sleeps keep ui.poll() going */
    WAIT_LIMIT_MS = 30000, /* the longest wait for the link */
    POLL_MS = 20,          /* ask a pending Wi-Fi join this often */
    MS_PER_S = 1000,
    CHART_MAX_MS = SEND_EVERY_MS * 2,
    TEXT_CHARS = 128, /* a drawer line carries 127 bytes */
    PAYLOAD_CHARS = 64,
    MODE_CHARS = BENTO_TESAIOT_CONFIG_VALUE_MAX + 1U,
    /* A line with the mode in it, or a long count: the words and
     * up to 127 bytes of mode. The label and the drawer get the
     * whole line and cut it to what they carry, as for print(). */
    LINE_CHARS = TEXT_CHARS + MODE_CHARS
};

/* The widgets the program changes after it has drawn the screen. */
static const char WIFI_SSID[] = "AIoT-Class";

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t mode;
    bento_ui_id_t sent_digits;
    bento_ui_id_t link;
    bento_ui_id_t chart;
    uint8_t series;
    bento_ui_id_t status_line;
} tls_screen_t;

/* Where a label goes, its colour and its font size. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} spot_t;

/* The publish loop's state. */
typedef struct {
    int32_t sent;
    uint32_t last_send_ms;
    char mode[MODE_CHARS]; /* cfg["tls_mode"], read once */
} publisher_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* wifi.ip(): the board's address, "0.0.0.0" when it has none. */
static void format_ip(char *text, uint32_t cap)
{
    uint32_t ip_v4 = 0U;
    uint32_t length = 0U;

    if (bento_wifi_ip(BENTO_WIFI_IF_ANY, &ip_v4) != BENTO_OK) {
        ip_v4 = 0U;
    }
    /* a BENTO_WIFI_IPV4_TEXT_MAX buffer holds any address */
    (void)bento_wifi_ipv4_format(ip_v4, text, cap, &length);
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

/* The platform's settings, then tls_mode read back once. */
static bento_status_t configure_platform(publisher_t *publisher)
{
    static const char device_id[] = "team03";
    static const char api_key[] = "<api key ของทีม>";
    static const char mqtt_pass[] = "<รหัสผ่าน MQTT ของทีม>";
    static const char broker[] = "<ชื่อโฮสต์ของแพลตฟอร์ม>";
    uint32_t length = 0U;
    bento_status_t status = set_field("device_id", device_id);

    if (status == BENTO_OK) {
        status = set_field("api_key", api_key);
    }
    if (status == BENTO_OK) {
        status = set_field("mqtt_pass", mqtt_pass);
    }
    if (status == BENTO_OK) {
        status = set_field("broker", broker);
    }
    if (status == BENTO_OK) {
        status = set_field("sni_hostname", broker);
    }
    if (status == BENTO_OK) {
        status = set_field("tls_mode", "server_tls");
    }
    if (status == BENTO_OK) {
        status = bento_tesaiot_config_get_field(
            "tls_mode", (uint32_t)strlen("tls_mode"), 0U,
            publisher->mode, (uint32_t)sizeof(publisher->mode),
            &length);
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

/* The widget setters ignore a refusal in MicroPython (modui.c), so
 * they do here too. */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

/* A label's new text and colour in one go. */
static void show(bento_ui_t *ui, bento_ui_id_t widget, const char *text,
                 uint32_t color)
{
    set_text(ui, widget, text);
    (void)bento_ui_set_color(ui, widget, color);
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

/* The card: mode, sent count, its caption and the link line. */
static bento_status_t add_card(tls_screen_t *screen)
{
    const spot_t mode = {INNER_X, MODE_Y, COLOR_INFO, TEXT_PX};
    const spot_t caption = {INNER_X, SEG_CAPTION_Y, COLOR_DIM, TEXT_PX};
    const spot_t link = {LINK_X, LINK_Y, COLOR_WARN, TITLE_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t widget;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM;   /* the border colour */
    args.max_val = CARD_RADIUS; /* the corner radius */
    args.font_px = CARD_BORDER_PX;
    status = bento_ui_panel_create(screen->ui, &args, &widget);
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "ยังไม่ได้ตั้งโหมด", &mode, &screen->mode);
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
        status = bento_ui_seg7_create(screen->ui, &args,
                                      &screen->sent_digits);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "ส่งสำเร็จ (ครั้ง)", &caption, &widget);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "สาย: ยังไม่ได้ต่อ", &link,
                           &screen->link);
    }
    return status;
}

/* The chart of gaps between two sends, 0 to twice the period. */
static bento_status_t add_chart(tls_screen_t *screen)
{
    const spot_t caption = {MARGIN_X, CHART_CAPTION_Y, COLOR_DIM,
                            TEXT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t label;
    bento_status_t status = add_label(
        screen->ui, "จังหวะการส่ง (ms ระหว่างสองครั้ง)", &caption, &label);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = MARGIN_X;
        args.y = CHART_Y;
        args.w = CARD_W;
        args.h = CHART_H;
        args.color = COLOR_CARD;
        args.min_val = 0;
        args.max_val = CHART_MAX_MS;
        status =
            bento_ui_chart_create(screen->ui, &args, &screen->chart);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_OK, &screen->series);
    }
    return status;
}

/* Shows the mode in use. *matches is false when the platform did
 * not take server TLS; the program then stops. */
static void show_mode(const tls_screen_t *screen,
                      const publisher_t *publisher, bool *matches)
{
    char text[LINE_CHARS];

    *matches = (strcmp(publisher->mode, "serverTLS") == 0);
    if (!*matches) {
        (void)snprintf(text, sizeof(text), "โหมดไม่ตรง ได้ %s",
                       publisher->mode);
        show(screen->ui, screen->mode, text, COLOR_BAD);
        poll_display(screen->ui);
        (void)snprintf(text, sizeof(text),
                       "<span class=error>โหมดไม่ตรง ได้ %s </span>",
                       publisher->mode);
    } else {
        (void)snprintf(text, sizeof(text), "โหมด %s -> พอร์ต 8884",
                       publisher->mode);
        set_text(screen->ui, screen->mode, text);
        poll_display(screen->ui); /* the drawer gets the same line */
    }
    (void)bento_lcd_print(text);
}

/* The link dropped: say so everywhere and stop sending. */
static void show_dropped(const tls_screen_t *screen,
                         const publisher_t *publisher)
{
    char text[LINE_CHARS];

    show(screen->ui, screen->link, "สาย: หลุดแล้ว", COLOR_BAD);
    (void)snprintf(text, sizeof(text),
                   "หลุดการเชื่อมต่อ ส่งไปแล้ว "
                   "%ld ครั้ง",
                   (long)publisher->sent);
    show(screen->ui, screen->status_line, text, COLOR_BAD);
    poll_display(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "<span class=warn>หลุดการเชื่อมต่อ ส่งไปแล้ว %ld"
                   " ครั้ง</span>",
                   (long)publisher->sent);
    (void)bento_lcd_print(text);
}

/* Draws the screen in the original's order. */
static bento_status_t build_screen(tls_screen_t *screen)
{
    const spot_t title = {MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX};
    const spot_t line = {MARGIN_X, STATUS_Y, COLOR_DIM, TEXT_PX};
    char text[TEXT_CHARS];
    bento_ui_id_t label;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ส่งขึ้นแพลตฟอร์มผ่าน TLS", &title,
                           &label);
    }
    if (status == BENTO_OK) {
        status = add_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "กำลังต่อ WiFi %s", WIFI_SSID);
        status =
            add_label(screen->ui, text, &line, &screen->status_line);
    }
    return status;
}

/* wifi.connect(): *joined is its True or False. A refused or
 * timed-out join is False; any other failure ends the program. */
static bento_status_t join_wifi(bool *joined)
{
    static const char wifi_pass[] = "<รหัสผ่านของห้องเรียน>";
    bento_wifi_connect_params_t params;
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
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
        format_ip(address, sizeof(address));
        (void)printf("[WiFi] Connected! IP=%s\r\n", address);
    }
    return status;
}

/* Waits for is_connected() at most WAIT_LIMIT_MS; *late is true when
 * the limit passed first. */
static bento_status_t wait_for_link(const tls_screen_t *screen,
                                    uint32_t start_ms, bool *late)
{
    bento_status_t status = BENTO_OK;

    *late = false;
    while ((status == BENTO_OK) && !*late && !link_is_up()) {
        *late = (bento_time_ticks_diff(bento_time_ticks_ms(),
                                       start_ms) > WAIT_LIMIT_MS);
        if (!*late) {
            poll_display(screen->ui);
            status = wait_ms(TICK_MS);
        }
    }
    return status;
}

static void show_link_up(const tls_screen_t *screen, uint32_t start_ms)
{
    const int32_t up_ms =
        bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);
    char text[TEXT_CHARS];

    show(screen->ui, screen->link, "สาย: ต่ออยู่", COLOR_OK);
    (void)snprintf(text, sizeof(text),
                   "TLS สำเร็จใน %ld ms - เช็ก is_connected()"
                   " ก่อนส่งทุกครั้ง",
                   (long)up_ms);
    show(screen->ui, screen->status_line, text, COLOR_OK);
    poll_display(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>TLS สำเร็จใน %ld ms</span>",
                   (long)up_ms);
    (void)bento_lcd_print(text);
}

static void show_link_late(const tls_screen_t *screen)
{
    show(screen->ui, screen->status_line, "ต่อไม่สำเร็จใน 30 วินาที",
         COLOR_BAD);
    poll_display(screen->ui);
    (void)bento_lcd_print(
        "<span class=error>ต่อไม่สำเร็จใน 30 วินาที</span>");
}

/* connect(), then wait for is_connected() at most WAIT_LIMIT_MS.
 * *up is false when the link never came. */
static bento_status_t open_link(const tls_screen_t *screen, bool *up)
{
    uint32_t start_ms;
    bool late = false;
    bento_status_t status = bento_tesaiot_connect_start();

    if (status == BENTO_E_FAILED) {
        status = BENTO_OK; /* connect()'s False, ignored there too */
    }
    start_ms = bento_time_ticks_ms();
    if (status == BENTO_OK) {
        show(screen->ui, screen->status_line,
             "connect() คืนค่าแล้ว กำลังรอ is_connected()", COLOR_WARN);
        poll_display(screen->ui);
        status = wait_for_link(screen, start_ms, &late);
    }
    *up = (status == BENTO_OK) && !late;
    if (late) {
        show_link_late(screen);
    } else if (*up) {
        show_link_up(screen, start_ms);
    } else {
        /* Stop, or no TESAIoT service: the status says which */
    }
    return status;
}

/* One flat message: the platform's bridge wraps it under data and
 * adds the device id from the topic, so wrapping it here too would
 * prefix every name with data_. No topic either: the firmware
 * builds it from device_id. */
static void send_one(const tls_screen_t *screen, publisher_t *publisher)
{
    const uint32_t uptime_s =
        bento_time_ticks_ms() / (uint32_t)MS_PER_S;
    const uint32_t now_ms = bento_time_ticks_ms();
    const int32_t gap_ms =
        bento_time_ticks_diff(now_ms, publisher->last_send_ms);
    char payload[PAYLOAD_CHARS];
    char text[LINE_CHARS];

    const int32_t message_number = publisher->sent + 1;

    (void)snprintf(payload, sizeof(payload),
                   "{\"seq\": %ld, \"uptime_s\": %lu}",
                   (long)message_number, (unsigned long)uptime_s);
    publisher->last_send_ms = now_ms;
    if (bento_tesaiot_publish(NULL, 0U, (const uint8_t *)payload,
                              (uint32_t)strlen(payload), 0U,
                              NULL) == BENTO_OK) {
        publisher->sent++;
        (void)snprintf(text, sizeof(text), "%ld",
                       (long)publisher->sent);
        set_text(screen->ui, screen->sent_digits, text);
        if (publisher->sent > 1) { /* the first has no gap yet */
            (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                          screen->series, gap_ms);
        }
        (void)snprintf(text, sizeof(text),
                       "ส่งครั้งที่ %ld | โหมด %s | พอร์ต 8884",
                       (long)publisher->sent, publisher->mode);
        (void)bento_lcd_print(text);
    } else {
        show(screen->ui, screen->link, "สาย: publish ถูกปฏิเสธ",
             COLOR_WARN);
        (void)bento_lcd_print(
            "<span class=warn>publish ถูกปฏิเสธ</span>");
    }
    poll_display(screen->ui);
}

/* Short sleeps instead of one long one keep the screen alive. */
static bento_status_t wait_period(const tls_screen_t *screen)
{
    int32_t waited_ms = 0;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) && (waited_ms < SEND_EVERY_MS)) {
        poll_display(screen->ui);
        status = wait_ms(TICK_MS);
        waited_ms += TICK_MS;
    }
    return status;
}

/* Check the link before every send: a link that was fine five
 * seconds ago is no proof of now. */
static bento_status_t publish_loop(const tls_screen_t *screen,
                                   publisher_t *publisher)
{
    bool dropped = false;
    bento_status_t status = BENTO_OK;

    publisher->last_send_ms = bento_time_ticks_ms();
    while ((status == BENTO_OK) && !dropped) {
        dropped = !link_is_up();
        if (dropped) {
            show_dropped(screen, publisher);
        } else {
            send_one(screen, publisher);
            status = wait_period(screen);
        }
    }
    return status;
}

/* The BENTO Emulator's C runtime has no TESAIoT link yet (every
 * bento_tesaiot_* call answers BENTO_E_UNSUPPORTED there): the note takes
 * the status line, the console says it too, and the program ends quietly.
 * Answers BENTO_E_CANCELLED, the quiet end, once the note is up. */
static bento_status_t show_emulator_note(const tls_screen_t *screen)
{
    static const char note[] = "TESAIoT จาก C ยังไม่มีใน BENTO "
                               "Emulator\nให้รันบน Dev Kit หรือ Eva Kit";
    bento_ui_widget_args_t args;
    bento_ui_id_t label;
    bento_status_t status;

    set_text(screen->ui, screen->status_line, "");
    bento_ui_widget_args_init(&args);
    args.text = note;
    args.text_len = (uint32_t)strlen(note);
    args.x = MARGIN_X;
    args.y = STATUS_Y;
    args.w = EMU_NOTE_W;
    args.color = (uint32_t)COLOR_WARN;
    args.font_px = TEXT_PX;
    status = bento_ui_label_create(screen->ui, &args, &label);
    poll_display(screen->ui);
    (void)printf(
        "TESAIoT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva "
        "Kit / TESAIoT platform from C is not in the BENTO Emulator "
        "yet: run it on a Dev Kit or Eva Kit.\n");
    return (status == BENTO_OK) ? BENTO_E_CANCELLED : status;
}

/* After Wi-Fi: the platform, the link, the loop. */
static bento_status_t run_platform(const tls_screen_t *screen,
                                   publisher_t *publisher)
{
    bool matches = false;
    bool up = false;
    bento_status_t status = configure_platform(publisher);

    if (status == BENTO_E_UNSUPPORTED) {
        status = show_emulator_note(screen);
    }
    if (status == BENTO_OK) {
        show_mode(screen, publisher, &matches);
    }
    if ((status == BENTO_OK) && matches) {
        status = open_link(screen, &up);
    }
    if ((status == BENTO_OK) && up) {
        status = publish_loop(screen, publisher);
    }
    return status;
}

/* Wi-Fi first; a failed join ends the program normally. */
static bento_status_t run_session(const tls_screen_t *screen,
                                  publisher_t *publisher)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    char text[TEXT_CHARS];
    bool joined = false;
    bento_status_t status = join_wifi(&joined);

    if ((status == BENTO_OK) && !joined) {
        show(screen->ui, screen->status_line,
             "ต่อ WiFi ไม่ได้ "
             "จบตรงนี้",
             COLOR_BAD);
        poll_display(screen->ui);
        (void)bento_lcd_print("<span class=error>ต่อ WiFi "
                              "ไม่ได้</span>");
    }
    if ((status == BENTO_OK) && joined) {
        format_ip(address, sizeof(address));
        (void)snprintf(text, sizeof(text),
                       "ได้ IP %s - กำลังตั้งค่าแพลตฟอร์ม", address);
        set_text(screen->ui, screen->status_line, text);
        poll_display(screen->ui);
        format_ip(address, sizeof(address));
        (void)snprintf(text, sizeof(text), "ได้ IP %s", address);
        (void)bento_lcd_print(text);
        status = run_platform(screen, publisher);
    }
    return status;
}

int main(void)
{
    tls_screen_t screen;
    publisher_t publisher;
    char text[TEXT_CHARS];
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(&publisher, 0, sizeof(publisher));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        poll_display(screen.ui);
        /* lcd calls ignore a console that is not ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>คาบ 11 - MQTTs</h2>");
        (void)snprintf(text, sizeof(text), "กำลังต่อ WiFi %s", WIFI_SSID);
        (void)bento_lcd_print(text);
        status = run_session(&screen, &publisher);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
