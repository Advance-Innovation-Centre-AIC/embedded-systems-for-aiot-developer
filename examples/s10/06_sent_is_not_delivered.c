/*
 * 06_sent_is_not_delivered.c -- ส่งแล้ว ถึงจริงไหม
 *
 * ต่อ WiFi กับ broker ของห้องเรียน subscribe topic ของตัวเอง แล้ว
 * publish สิบใบไปที่ topic นั้น ซ้ายนับครั้งที่ publish() บอกว่า
 * สำเร็จ ขวานับใบที่เดินทางกลับมาถึงเราจริง ถ้าสองข้างไม่เท่ากัน
 * ป้ายผลต่างเป็นสีแดง: "ส่งแล้ว" ฝั่งเราไม่ใช่ "ถึงแล้ว" ฝั่งโน้น
 *
 * Joins the class Wi-Fi and broker, subscribes to its own topic and
 * publishes ten messages to it. The left number counts the times
 * publish() said it succeeded, the right one the messages that
 * really came back. When they differ the difference turns red:
 * "sent" on our side is not "delivered" on the other.
 *
 * Twin of 06_sent_is_not_delivered.py
 * (embedded-systems-for-aiot-developer/examples/s10/06_sent_is_not_delivered.py),
 * merged from the library twin aiot-s10-06-sent-is-not-delivered. Style: CLEAN_C.md.
 * Differs from the original:
 *   - get_message() is one slot that a newer message overwrites
 *     (modmqtt.c:60-63). The C client keeps a ring of
 *     BENTO_MQTT_RING_SLOTS; each look takes them all and counts
 *     one, the slot's behaviour. When more than that arrive between
 *     two looks (20 ms), the ring drops the newest instead.
 *   - mqtt.publish() raises OSError once the link is gone (the
 *     "dropped" branch, as there) and otherwise answers True or
 *     False; here BENTO_OK is True and any other refusal False.
 *   - wifi.connect() and mqtt.* print console lines of their own.
 *     Here the program prints the ones C can know: "[WiFi]
 *     Connecting...", "[WiFi] Connected! IP=...", "[MQTT]
 *     Connecting to ...", "[MQTT] Connected to ... as '...'",
 *     "[MQTT] Subscribed to '...' (QoS=N)", "[MQTT] subscribe
 *     rejected by broker" and "[MQTT] Disconnected". Not printed,
 *     because no C function reports them: the radio's per-attempt
 *     and failure lines with result codes, "[WiFi] Credentials
 *     saved to QSPI", "[MQTT] Library initialized" (first use after
 *     a reset only), the cy result codes of a failed connect,
 *     subscribe or publish, and "[MQTT] Disconnected (reason=N)"
 *     when the broker drops the link (modwifi.c, modmqtt.c).
 *   - mqtt.connect() raises only when the Wi-Fi radio is down; here
 *     a client that cannot start ends the program with its status
 *     after the same cleanup.
 *   - Stop ends the program quietly after the same cleanup, where
 *     MicroPython raises KeyboardInterrupt.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_mqtt.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* Counts, times and sizes. */
enum {
    MESSAGES = 10,
    SETTLE_MS = 200, /* let the cleared screen settle */
    WINDOW_MS = 400, /* how long each message may take to return */
    LOOK_MS = 20,    /* one slot: look often, or miss some */
    POLL_MS = 20,    /* how often a connect is asked */
    BROKER_PORT = 1883,
    TICKS_LOW_MASK = 0xFFFF, /* four hex digits for the client id */
    TOPIC_BYTES = 128,
    /* The longest line is the note, "ส่ง 10 ใบไปที่ ...": 129 bytes
     * and the NUL. The label is given all of it and cuts it to what
     * SET_TEXT carries, on a character boundary, as for the
     * original. */
    TEXT_BYTES = 160,
    NAME_BYTES = 24 /* "team03-" and four hex digits */
};

/* The widgets the program changes after it has drawn them. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t link_led;
    bento_ui_id_t link_label;
    bento_ui_id_t claimed_digits;
    bento_ui_id_t claimed_bar;
    bento_ui_id_t seen_digits;
    bento_ui_id_t seen_bar;
    bento_ui_id_t difference;
    bento_ui_id_t note;
    bento_ui_id_t end_line;
} echo_screen_t;

/* The client and the two counts. */
typedef struct {
    char client_id[NAME_BYTES]; /* new each run: no clash with the
                                   run just stopped */
    uint32_t broker;            /* the MQTT client, 0 when none */
    int32_t claimed;            /* publish() said it succeeded */
    int32_t seen;               /* really came back to us */
    bool stopped;               /* the original's raise Stop */
} echo_t;

/* A position on the screen: the widget arguments are 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
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

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 20,
    TITLE_Y = 12,
    LED_X = 606,
    LED_Y = 12,
    LED_SIZE = 18,
    LINK_X = 628,
    LINK_Y = 8,
    CARD_Y = 52,
    CARD_H = 152,
    CLAIM_CARD_X = 20,
    CLAIM_CARD_W = 328,
    SEEN_CARD_X = 356,
    SEEN_CARD_W = 316,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CARD_INSET_X = 16,
    CAPTION_Y = 62,
    DIGITS_Y = 96,
    DIGITS_W = 180,
    DIGITS_H = 76,
    BAR_Y = 180,
    CLAIM_BAR_W = 292,
    SEEN_BAR_W = 288,
    BAR_H = 20,
    DIFF_Y = 220,
    NOTE_Y = 268,
    END_Y = 304,
    END_TAIL_X = 216,
    TITLE_PX = 24,
    DIFF_PX = 28,
    TEXT_PX = 20
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_TRACK = 0x171B22, /* a Bar's track; the moving part has
                               the theme's colour */
    COLOR_OK = 0x30A46C,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

/* One of the two cards: where it is and what it shows. */
typedef struct {
    int16_t card_x;
    int16_t card_w;
    uint32_t border;
    const char *caption;
    uint32_t digits_color;
    int16_t bar_w;
} card_place_t;

/* ui.poll() here only keeps the queue empty; it answers no events
 * when it fails. */
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

static void paint(bento_ui_t *ui, bento_ui_id_t widget, uint32_t color,
                  const char *text)
{
    (void)bento_ui_set_color(ui, widget, color);
    set_text(ui, widget, text);
}

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

/* The title and the MQTT light with its label, top right. */
static bento_status_t add_header(echo_screen_t *screen)
{
    const label_at_t title_at = {MARGIN_X, TITLE_Y, COLOR_TEXT,
                                 TITLE_PX};
    const label_at_t link_at = {LINK_X, LINK_Y, COLOR_DIM, TEXT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(
        screen->ui, "ที่บอกว่าส่ง กับ ที่กลับมาจริง", &title_at, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = LED_X;
        args.y = LED_Y;
        args.w = LED_SIZE;
        args.h = LED_SIZE;
        args.color = COLOR_OK;
        args.font_px = 0; /* dim: offline */
        status =
            bento_ui_led_create(screen->ui, &args, &screen->link_led);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "MQTT: ออฟไลน์", &link_at,
                           &screen->link_label);
    }
    return status;
}

/* A card: its panel, caption, big digits and a bar from 0 to
 * MESSAGES. */
static bento_status_t add_card(bento_ui_t *ui,
                               const card_place_t *place,
                               bento_ui_id_t *digits,
                               bento_ui_id_t *bar)
{
    const int16_t inner_x = px(place->card_x + CARD_INSET_X);
    const label_at_t caption_at = {inner_x, CAPTION_Y, COLOR_DIM,
                                   TEXT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = place->card_x;
    args.y = CARD_Y;
    args.w = place->card_w;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = (int32_t)place->border; /* the border colour */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    status = bento_ui_panel_create(ui, &args, &unused);
    if (status == BENTO_OK) {
        status = add_label(ui, place->caption, &caption_at, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "0"; /* a Seg7 takes a text, not a number */
        args.text_len = (uint32_t)strlen(args.text);
        args.x = inner_x;
        args.y = DIGITS_Y;
        args.w = DIGITS_W;
        args.h = DIGITS_H;
        args.color = place->digits_color;
        status = bento_ui_seg7_create(ui, &args, digits);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = inner_x;
        args.y = BAR_Y;
        args.w = place->bar_w;
        args.h = BAR_H;
        args.color = COLOR_TRACK;
        args.min_val = 0;
        args.max_val = MESSAGES;
        args.font_px = 0; /* empty */
        status = bento_ui_bar_create(ui, &args, bar);
    }
    return status;
}

/* Left what our program claims, right what was measured. */
static bento_status_t add_cards(echo_screen_t *screen)
{
    static const card_place_t claim_card = {
        CLAIM_CARD_X,           CLAIM_CARD_W, COLOR_INFO,
        "publish() บอกว่าสำเร็จ", COLOR_INFO,   CLAIM_BAR_W};
    static const card_place_t seen_card = {
        SEEN_CARD_X,        SEEN_CARD_W, COLOR_OK,
        "เดินทางกลับมาถึงเรา", COLOR_OK,    SEEN_BAR_W};
    bento_status_t status =
        add_card(screen->ui, &claim_card, &screen->claimed_digits,
                 &screen->claimed_bar);

    if (status == BENTO_OK) {
        status = add_card(screen->ui, &seen_card, &screen->seen_digits,
                          &screen->seen_bar);
    }
    return status;
}

/* The difference, the note and the closing line (two labels: each
 * under 126 bytes, a Thai letter takes 3). */
static bento_status_t add_footer(echo_screen_t *screen)
{
    const label_at_t difference_at = {MARGIN_X, DIFF_Y, COLOR_DIM,
                                      DIFF_PX};
    const label_at_t note_at = {MARGIN_X, NOTE_Y, COLOR_DIM, TEXT_PX};
    const label_at_t end_at = {MARGIN_X, END_Y, COLOR_DIM, TEXT_PX};
    const label_at_t tail_at = {END_TAIL_X, END_Y, COLOR_DIM, TEXT_PX};
    bento_ui_id_t unused;
    bento_status_t status = add_label(
        screen->ui, "ต่างกัน 0 ใบ", &difference_at, &screen->difference);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังต่อเน็ต...", &note_at,
                           &screen->note);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "หลักฐานว่าถึงจริง", &end_at,
                           &screen->end_line);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ต้องนับที่ปลายทาง ไม่ใช่ต้นทาง",
                           &tail_at, &unused);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(echo_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_header(screen);
    }
    if (status == BENTO_OK) {
        status = add_cards(screen);
    }
    if (status == BENTO_OK) {
        status = add_footer(screen);
    }
    if (status == BENTO_OK) {
        drain_events(screen->ui);
    }
    return status;
}

/* The light and its label always change together. */
static void show_link(const echo_screen_t *screen, bool connected)
{
    (void)bento_ui_set_value(screen->ui, screen->link_led,
                             connected ? 1 : 0);
    set_text(screen->ui, screen->link_label,
             connected ? "MQTT: เชื่อมต่อแล้ว" : "MQTT: ออฟไลน์");
}

/* A step that cannot go on: the note, the console, then the end
 * (the original's raise Stop). */
static void give_up(const echo_screen_t *screen, echo_t *echo,
                    const char *text)
{
    char line[TEXT_BYTES];

    paint(screen->ui, screen->note, COLOR_BAD, text);
    drain_events(screen->ui);
    (void)snprintf(line, sizeof(line), "<span class=err>%s</span>",
                   text);
    (void)bento_lcd_print(line); /* lcd.print() ignores a console
                                    that is not ready (modlcd.c) */
    echo->stopped = true;
}

/* A number on a Seg7 and its bar. */
static void show_count(bento_ui_t *ui, bento_ui_id_t digits,
                       bento_ui_id_t bar, int32_t count)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, count);
    set_text(ui, digits, text);
    (void)bento_ui_set_value(ui, bar, count);
}

/* The answer of the whole file: the two numbers side by side. */
static void show_results(const echo_screen_t *screen,
                         const echo_t *echo)
{
    const int32_t difference = echo->claimed - echo->seen;
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "ต่างกัน %" PRId32 " ใบ",
                   difference);
    set_text(screen->ui, screen->difference, text);
    if (echo->seen < echo->claimed) {
        /* not always a bug: messages faster than our looks overwrite
         * each other in the one-slot inbox */
        (void)bento_ui_set_color(screen->ui, screen->difference,
                                 COLOR_BAD);
        paint(screen->ui, screen->note, COLOR_BAD,
              "เลขสองข้างไม่ตรงกัน ฝั่งเราเชื่อไม่ได้");
        (void)snprintf(text, sizeof(text),
                       "<span class=warn>ต่างกัน %" PRId32 " ใบ</span>",
                       difference);
        (void)bento_lcd_print(text);
        (void)bento_lcd_print("ใบที่มาเร็วกว่าที่เราถามทัน");
        (void)bento_lcd_print("จึงทับกันในบัฟเฟอร์ช่องเดียว");
    } else {
        (void)bento_ui_set_color(screen->ui, screen->difference,
                                 COLOR_OK);
        paint(screen->ui, screen->note, COLOR_OK,
              "รอบนี้ครบ แต่ QoS 0 ไม่ประกันว่ารอบหน้าจะครบ");
        (void)bento_lcd_print("<span class=ok>ครบทุกใบในรอบนี้</span>");
    }
    (void)bento_ui_set_color(screen->ui, screen->end_line, COLOR_TEXT);
    drain_events(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "publish() บอกว่าสำเร็จ %" PRId32 " ครั้ง",
                   echo->claimed);
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text), "วิ่งกลับมาถึงเราจริง %" PRId32 " ใบ",
                   echo->seen);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print("ถ้าต้องการหลักฐานว่าถึงจริง");
    (void)bento_lcd_print("ต้องนับที่ปลายทาง ไม่ใช่ที่ต้นทาง");
}

static const char WIFI_SSID[] = "AIoT-Class";
static const char WIFI_PASS[] = "<รหัสผ่านของห้องเรียน>";
static const char BROKER[] = "192.168.1.50";
/* Our own topic, subscribed too: each message goes round through
 * the broker and back to us, the cheapest proof of delivery a
 * classroom has. */
static const char TOPIC_ECHO[] = "bento/team03/echo";

/* wifi.connect(): *joined is its True or False. A refused or
 * timed-out join is False; any other failure ends the program. */
static bento_status_t join_wifi(bool *joined)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_wifi_connect_params_t params;
    uint32_t ip_v4 = 0U;
    uint32_t length = 0U;
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
        status = wait_ms(POLL_MS);
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
    if (*joined) { /* an unreadable address prints as 0.0.0.0 */
        if (bento_wifi_ip(BENTO_WIFI_IF_STA, &ip_v4) != BENTO_OK) {
            ip_v4 = 0U;
        }
        /* a BENTO_WIFI_IPV4_TEXT_MAX buffer holds any address */
        (void)bento_wifi_ipv4_format(
            ip_v4, address, (uint32_t)sizeof(address), &length);
        (void)printf("[WiFi] Connected! IP=%s\r\n", address);
    }
    return status;
}

/* mqtt.connect(): *linked is its True or False. A client that
 * cannot even start (no radio) ends the program, as the binding's
 * raise does. */
static bento_status_t open_broker(echo_t *echo, bool *linked)
{
    bento_mqtt_config_t config;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = (uint16_t)BROKER_PORT;
    config.client_id = echo->client_id;
    config.client_id_len = (uint32_t)strlen(echo->client_id);
    status = bento_mqtt_open_start(&config, &echo->broker);
    if (status == BENTO_OK) { /* the binding raises before it prints */
        (void)printf("[MQTT] Connecting to %s:%d ...\r\n", BROKER,
                     BROKER_PORT);
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(POLL_MS);
        if (status == BENTO_OK) {
            status = bento_mqtt_open_poll(echo->broker);
        }
    }
    *linked = (status == BENTO_OK);
    if (*linked) {
        (void)printf("[MQTT] Connected to %s:%d as '%s'\r\n", BROKER,
                     BROKER_PORT, echo->client_id);
    } else if ((status != BENTO_E_CANCELLED) && (echo->broker != 0U)) {
        status = BENTO_OK;
    } else {
        /* no client, or Stop: the status ends the program */
    }
    return status;
}

/* mqtt.subscribe(): the original ignores its answer. A lost link
 * raises "MQTT not connected" there, which nothing catches. */
static bento_status_t subscribe_echo(const echo_t *echo)
{
    uint8_t granted_qos = 0U;
    bento_status_t status = bento_mqtt_subscribe(
        echo->broker, TOPIC_ECHO, (uint32_t)strlen(TOPIC_ECHO), 0U,
        &granted_qos);

    if (status == BENTO_OK) {
        (void)printf("[MQTT] Subscribed to '%s' (QoS=%d)\r\n",
                     TOPIC_ECHO, (int)granted_qos);
    } else if (status == BENTO_E_FAILED) {
        (void)printf("[MQTT] subscribe rejected by broker\r\n");
    } else {
        /* a failure the binding prints with a cy code */
    }
    if (status != BENTO_E_SESSION) {
        status = BENTO_OK;
    }
    return status;
}

/* mqtt.disconnect() never fails and always says so. */
static void close_broker(echo_t *echo)
{
    if (echo->broker != 0U) {
        (void)bento_mqtt_close(echo->broker); /* the client is freed */
        echo->broker = 0U;
    }
    (void)printf("[MQTT] Disconnected\r\n");
}

/* get_message() is one slot a newer message overwrites: everything
 * waiting counts as one message. */
static bool message_arrived(uint32_t broker)
{
    char topic[TOPIC_BYTES]; /* the program never reads it */
    bento_mqtt_msg_t info;
    bool found = false;
    bool more = true;

    while (more) {
        bento_mqtt_msg_init(&info);
        more = bento_mqtt_poll(broker, &info, topic, sizeof(topic),
                               NULL, 0U) == BENTO_OK;
        found = found || more;
    }
    return found;
}

/* After each message, a short window to collect what comes back,
 * looking often: the inbox has one slot. */
static bento_status_t collect_echoes(const echo_screen_t *screen,
                                     echo_t *echo)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            WINDOW_MS)) {
        if (message_arrived(echo->broker)) {
            echo->seen++;
            show_count(screen->ui, screen->seen_digits,
                       screen->seen_bar, echo->seen);
        }
        drain_events(screen->ui);
        status = wait_ms(LOOK_MS);
    }
    return status;
}

/* send_one() also marks the link as dropped on the screen; most of
 * its work is the publish, so it lives here. */
/* One message, {"seq": n} as json.dumps writes it. *dropped: the
 * link is gone (the original's OSError) and sending ends. */
static void send_one(const echo_screen_t *screen, echo_t *echo,
                     int32_t sequence, bool *dropped)
{
    char payload[TEXT_BYTES];
    char line[TEXT_BYTES];
    bento_status_t sent_status;

    (void)snprintf(payload, sizeof(payload), "{\"seq\": %" PRId32 "}",
                   sequence);
    sent_status = bento_mqtt_publish(
        echo->broker, TOPIC_ECHO, (uint32_t)strlen(TOPIC_ECHO),
        (const uint8_t *)payload, (uint32_t)strlen(payload), 0U, 0U);
    *dropped = sent_status == BENTO_E_SESSION;
    if (sent_status == BENTO_OK) {
        echo->claimed++;
    } else if (*dropped) {
        show_link(screen, false);
        (void)snprintf(line, sizeof(line),
                       "<span class=err>สายหลุดที่ใบที่ %" PRId32 " </span>",
                       sequence);
        (void)bento_lcd_print(line);
    } else {
        /* publish() answered False: not counted */
    }
}

/* MESSAGES messages to our own topic, counting what returns. */
static bento_status_t send_and_count(const echo_screen_t *screen,
                                     echo_t *echo)
{
    bento_status_t status = BENTO_OK;
    bool dropped = false;
    int32_t sequence;

    for (sequence = 1;
         (sequence <= MESSAGES) && (status == BENTO_OK) && !dropped;
         sequence++) {
        send_one(screen, echo, sequence, &dropped);
        if (!dropped) {
            show_count(screen->ui, screen->claimed_digits,
                       screen->claimed_bar, echo->claimed);
            status = collect_echoes(screen, echo);
        }
    }
    return status;
}

/* run_echo() also writes the link and the count on the screen; most
 * of its work is the broker, so it lives here. */
/* From the broker connect to the end of sending. Whatever ends it,
 * the cleanup says goodbye to the broker, so the name does not
 * linger there for another minute. */
static bento_status_t run_echo(const echo_screen_t *screen,
                               echo_t *echo)
{
    char text[TEXT_BYTES];
    bool linked = false;
    bento_status_t status = open_broker(echo, &linked);

    if ((status == BENTO_OK) && !linked) {
        show_link(screen, false);
        give_up(screen, echo, "ต่อ broker ไม่ได้");
    }
    if ((status == BENTO_OK) && linked) {
        show_link(screen, true);
        status = subscribe_echo(echo);
    }
    if ((status == BENTO_OK) && linked) {
        (void)snprintf(text, sizeof(text),
                       "ส่ง %d ใบไปที่ topic ของตัวเอง"
                       " แล้วนั่งนับที่กลับมา",
                       MESSAGES);
        set_text(screen->ui, screen->note, text);
        drain_events(screen->ui);
        (void)snprintf(text, sizeof(text), "subscribe %s แล้ว",
                       TOPIC_ECHO);
        (void)bento_lcd_print(text);
        status = send_and_count(screen, echo);
    }
    close_broker(echo);
    show_link(screen, false);
    return status;
}

static const char DEVICE_ID[] = "team03";

int main(void)
{
    echo_screen_t screen = {0};
    echo_t echo = {0};
    bool joined = false;
    bento_status_t status;

    /* a new id each run: a run right after Stop does not clash */
    (void)snprintf(echo.client_id, sizeof(echo.client_id), "%s-%04x",
                   DEVICE_ID,
                   (unsigned int)(bento_time_ticks_ms() &
                                  (uint32_t)TICKS_LOW_MASK));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) { /* lcd.* ignore a console that is not
                                 ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>คาบ 10 - ส่งแล้วถึงจริงไหม</h2>");
        status = join_wifi(&joined);
    }
    if ((status == BENTO_OK) && !joined) {
        give_up(&screen, &echo, "ต่อ WiFi ไม่ได้");
    }
    if ((status == BENTO_OK) && joined) {
        status = run_echo(&screen, &echo);
    }
    if ((status == BENTO_OK) && !echo.stopped) {
        show_results(&screen, &echo);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
