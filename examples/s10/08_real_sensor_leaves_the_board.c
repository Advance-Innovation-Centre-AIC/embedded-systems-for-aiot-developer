/*
 * 08_real_sensor_leaves_the_board.c -- ค่าจริงออกจากบอร์ด
 *
 * ต่อ WiFi กับ broker ของห้องเรียน แล้ววัดอุณหภูมิทุก 200 ms
 * 150 รอบ (SHT40 ถ้าบอร์ดมี ไม่งั้นลูกบิด 0-100 % แทน 15-45 C)
 * แต่ส่งค่าออกไปทาง MQTT แค่ทุกสองวินาที กราฟเส้นฟ้าคือค่าที่วัด
 * เส้นเขียวคือค่าที่ส่งจริง จึงเป็นขั้นบันได ไฟมุมขวาบนบอกว่า
 * ต่อ broker อยู่หรือไม่ จบแล้วบอกว่าส่งไปกี่ครั้ง
 *
 * Joins the class Wi-Fi and broker, then measures the temperature
 * every 200 ms for 150 rounds (the SHT40 when the board has one,
 * else the knob, 0-100 % = 15-45 C), but publishes over MQTT only
 * every two seconds. On the chart the blue line is every reading,
 * the green line what was really sent, so it is a staircase. The
 * light at the top right says whether the broker is connected. At
 * the end it says how many messages went out.
 *
 * Twin of 08_real_sensor_leaves_the_board.py
 * (embedded-systems-for-aiot-developer/examples/s10/08_real_sensor_leaves_the_board.py),
 * merged from the library twin aiot-s10-08-real-sensor-leaves-the-board.
 * Style: CLEAN_C.md.
 * Differs from the original:
 *   - wifi.connect() and mqtt.* print console lines of their own.
 *     Here the program prints the ones C can know: "[WiFi]
 *     Connecting...", "[WiFi] Connected! IP=...", "[MQTT]
 *     Connecting to ...", "[MQTT] Connected to ... as '...'" and
 *     "[MQTT] Disconnected". Not printed, because no C function
 *     reports them: the per-attempt and failure lines with the
 *     radio's result codes (modwifi.c:1037-1098), "[WiFi]
 *     Credentials saved to QSPI", "[MQTT] Library initialized"
 *     (first use after a reset only), the cy result codes of a
 *     failed connect or publish and "[MQTT] Disconnected
 *     (reason=N)" when the broker drops the link (modmqtt.c).
 *   - mqtt.connect() answers False on a refused or timed-out
 *     connect, and the program goes on offline; here the same.
 *     It raises when the Wi-Fi radio is down; here the error status
 *     ends the program after the cleanup, as the exception does.
 *   - mqtt.publish() raises "MQTT not connected" once the link is
 *     gone (shown on the status line, as there) and otherwise
 *     answers False without raising, which the original counts as
 *     sent; here a failed publish is counted the same way.
 *   - The binding cuts an MQTT password to 31 bytes
 *     (modmqtt.c:39); C sends it whole. The placeholder here is 50
 *     bytes, so a real password longer than 31 bytes differs.
 *   - json.dumps writes the keys in the slot order of MicroPython's
 *     dict (py/map.c, py/qstr.c): temp_c, device, t_ms with the
 *     PSoC Edge's 1-byte qstr hash, t_ms, device, temp_c with the
 *     reTerminal's 2-byte one.
 *   - hasattr(sensors, "sht40") is a fact of the build. Here
 *     bento_sensors_sht40_temperature() is asked, and any refusal
 *     falls back to the knob, as the original's OSError does.
 *   - sensors.snapshot() waits up to 16 s right after a reset; C
 *     asks for BENTO_SENSORS_SNAPSHOT_DEFAULT_MS (1 s). A failed
 *     snapshot ends the program in both.
 *   - Stop ends the program quietly after the same cleanup, where
 *     MicroPython raises KeyboardInterrupt.
 *   - In the BENTO Emulator, MQTT from C is not available yet: the
 *     program says so and goes on offline, as the original does when
 *     the broker cannot be reached (every send then fails).
 */
#include <inttypes.h>
#include <math.h>
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

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_READ = 0x4A9EFF, /* every reading */
    COLOR_SENT = 0x30A46C, /* what really went out */
    COLOR_BAD = 0xE5484D,
    COLOR_WARN = 0xF5A623
};

/* Times, rounds and sizes. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    READ_MS = 200,   /* measuring often bothers nobody */
    SEND_MS = 2000,  /* sending often bothers the broker */
    ROUNDS = 150,
    POLL_MS = 20, /* how often a connect is asked */
    BROKER_PORT = 1883,
    READ_SERIES = 0,  /* the series every Chart starts with */
    TENTHS = 10,      /* round(x, 1) */
    TEXT_BYTES = 128, /* a label or console line; 126 at most */
    PAYLOAD_BYTES = 96
};

/* Where the temperature came from last; announced on change. */
typedef enum {
    SOURCE_NONE = 0,
    SOURCE_SHT40,
    SOURCE_KNOB
} temp_source_t;

/* The widgets the program changes after it has drawn them. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t link_led;
    bento_ui_id_t link_label;
    bento_ui_id_t now_digits;
    bento_ui_id_t sent_digits;
    bento_ui_id_t chart;
    uint8_t sent_series;
    bento_ui_id_t status_line;
} link_screen_t;

/* What the measuring loop keeps between rounds. */
typedef struct {
    uint32_t broker; /* the MQTT client, 0 when none */
    temp_source_t source;
    bool has_value;
    bento_real_t last_value;
    uint32_t last_send_ms;
    int32_t sent;
} telemetry_t;

/* MicroPython rounds a*b and +c separately: the compiler must not
 * fuse them, or the knob's temperature differs in the last digit. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

/* A position or size on the screen: the widget arguments are
 * 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

/* int() of a temperature: truncates toward zero. */
static int32_t whole_degrees(bento_real_t temp_c)
{
    return (int32_t)temp_c;
}

/* round(x, 1): nearbyint(x * 10) / 10 in the board's float width
 * (py/modbuiltins.c:491-496). */
static bento_real_t round_tenth(bento_real_t value)
{
    const bento_real_t scale = (bento_real_t)TENTHS;
    const bento_real_t scaled = value * scale;

    return (bento_real_t)nearbyint((double)scaled) / scale;
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

/* The SHT40 when the board has one, else the knob (0-100 % =
 * 15-45 C). false when this round has neither. */
static bool read_temp(const bento_sensors_snapshot_t *snap,
                      temp_source_t *source, bento_real_t *temp_c)
{
    const bento_real_t knob_base_c = (bento_real_t)15.0;
    const bento_real_t c_per_percent = (bento_real_t)0.3;
    float sht40_c = 0.0F;
    bool found = true;

    if (bento_sensors_sht40_temperature(&sht40_c) == BENTO_OK) {
        if (*source != SOURCE_SHT40) {
            *source = SOURCE_SHT40;
            (void)bento_lcd_print("อุณหภูมิจาก SHT40 "
                                  "(เซนเซอร์จริงบนบอร์ด)");
        }
        *temp_c = (bento_real_t)sht40_c;
    } else if (snap->has_pot != 0U) {
        const bento_real_t scaled =
            (bento_real_t)snap->pot_percent * c_per_percent;

        if (*source != SOURCE_KNOB) {
            *source = SOURCE_KNOB;
            (void)bento_lcd_print(
                "<span class=warn>ไม่มีเซนเซอร์อุณหภูมิ</span>");
            (void)bento_lcd_print("ใช้ลูกบิดแทน: 0-100 % = 15-45 C");
        }
        *temp_c = knob_base_c + scaled;
    } else {
        found = false;
    }
    return found;
}

static const char DEVICE_ID[] = "team03";

/* Prints the line wifi.connect() prints on success. */
static void print_wifi_address(void)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    uint32_t ip_v4 = 0U;
    uint32_t length = 0U;

    /* an address that cannot be read prints as 0.0.0.0 */
    if (bento_wifi_ip(BENTO_WIFI_IF_STA, &ip_v4) != BENTO_OK) {
        ip_v4 = 0U;
    }
    /* a BENTO_WIFI_IPV4_TEXT_MAX buffer holds any address */
    (void)bento_wifi_ipv4_format(ip_v4, address,
                                 (uint32_t)sizeof(address), &length);
    (void)printf("[WiFi] Connected! IP=%s\r\n", address);
}

/* wifi.connect(): *joined is its True or False. A refused or
 * timed-out join is False; any other failure ends the program. */
static bento_status_t join_wifi(bool *joined)
{
    /* The class network, handed out by the teacher. */
    static const char wifi_ssid[] = "AIoT-Class";
    static const char wifi_pass[] = "<รหัสผ่านของห้องเรียน>";
    bento_wifi_connect_params_t params;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = wifi_ssid;
    params.ssid_len = (uint32_t)strlen(wifi_ssid);
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
        } else { /* Stop between two polls: leave the radio alone */
            (void)bento_wifi_connect_cancel(); /* nothing to report */
        }
    }
    *joined = (status == BENTO_OK);
    if ((status == BENTO_E_FAILED) || (status == BENTO_E_TIMEOUT)) {
        status = BENTO_OK;
    }
    if (*joined) {
        print_wifi_address();
    }
    return status;
}

/* mqtt.connect(): a refused or timed-out connect is MicroPython's
 * False, and the program goes on offline. A client that could not
 * even start (no radio) ends the program, as the binding's raise. */
static bento_status_t open_broker(uint32_t *broker)
{
    static const char broker_host[] = "192.168.1.50";
    static const char mqtt_user[] = "team03";
    static const char mqtt_pass[] = "<รหัสผ่าน MQTT ของทีม>";
    bento_mqtt_config_t config;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = broker_host;
    config.host_len = (uint32_t)strlen(broker_host);
    config.port = (uint16_t)BROKER_PORT;
    config.client_id = DEVICE_ID;
    config.client_id_len = (uint32_t)strlen(DEVICE_ID);
    config.user = mqtt_user;
    config.user_len = (uint32_t)strlen(mqtt_user);
    config.pass = mqtt_pass;
    config.pass_len = (uint32_t)strlen(mqtt_pass);
    status = bento_mqtt_open_start(&config, broker);
    if (status == BENTO_OK) { /* the binding raises before it prints */
        (void)printf("[MQTT] Connecting to %s:%d ...\r\n", broker_host,
                     BROKER_PORT);
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(POLL_MS);
        if (status == BENTO_OK) {
            status = bento_mqtt_open_poll(*broker);
        }
    }
    if (status == BENTO_OK) {
        (void)printf("[MQTT] Connected to %s:%d as '%s'\r\n",
                     broker_host, BROKER_PORT, DEVICE_ID);
    } else if ((status != BENTO_E_CANCELLED) && (*broker != 0U)) {
        status = BENTO_OK;
    } else {
        /* no client, or Stop: the status ends the program */
    }
    return status;
}

/* mqtt.is_connected(): an unknown state counts as offline. */
static bool broker_connected(uint32_t broker)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;

    if (bento_mqtt_state(broker, &state) != BENTO_OK) {
        state = BENTO_MQTT_STATE_CLOSED;
    }
    return state == BENTO_MQTT_STATE_CONNECTED;
}

/* mqtt.disconnect() never fails and always says so; a second call
 * finds no client and says it again. */
static void close_broker(uint32_t *broker)
{
    if (*broker != 0U) {
        (void)bento_mqtt_close(*broker); /* the client is freed */
        *broker = 0U;
    }
    (void)printf("[MQTT] Disconnected\r\n");
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

/* json.dumps of {"device", "temp_c", "t_ms"}, keys in the slot
 * order of the board's MicroPython dict (header). */
static void write_payload(char *payload, size_t cap,
                          const char *temp_text, uint32_t now_ms)
{
    if (on_reterminal()) {
        (void)snprintf(payload, cap,
                       "{\"t_ms\": %" PRIu32 ", \"device\": \"%s\","
                       " \"temp_c\": %s}",
                       now_ms, DEVICE_ID, temp_text);
    } else {
        (void)snprintf(payload, cap,
                       "{\"temp_c\": %s, \"device\": \"%s\","
                       " \"t_ms\": %" PRIu32 "}",
                       temp_text, DEVICE_ID, now_ms);
    }
}

/* MicroPython rounds a*b and +c separately: the compiler must not
 * fuse them, or the knob's temperature differs in the last digit. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

/* Layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 20,
    TITLE_Y = 12,
    LED_X = 606,
    LED_Y = 12,
    LED_SIZE = 18,
    LINK_X = 628,
    LINK_Y = 8,
    CARD_Y = 52,
    CARD_H = 96,
    NOW_CARD_X = 20,
    NOW_CARD_W = 332,
    SENT_CARD_X = 368,
    SENT_CARD_W = 304,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CARD_INSET_X = 16, /* caption and digits inside a card */
    CAPTION_Y = 64,
    DIGITS_Y = 92,
    DIGITS_H = 48,
    NOW_DIGITS_W = 200,
    SENT_DIGITS_W = 140,
    CHART_X = 20,
    CHART_Y = 164,
    CHART_W = 548,
    CHART_H = 192,
    CHART_MIN_C = 20, /* a narrow scale makes the line move */
    CHART_MAX_C = 35,
    LEGEND_X = 580,
    LEGEND_READ_Y = 172,
    LEGEND_SENT_Y = 196,
    EMULATOR_NOTE_Y = 232, /* under the legend, beside the chart */
    EMULATOR_NOTE_W = 204,
    STATUS_X = 20,
    STATUS_Y = 364,
    TITLE_PX = 24,
    TEXT_PX = 20
};

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

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t x, int16_t y, uint32_t color,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.color = color;
    args.font_px = TEXT_PX;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A card: a rounded panel with a dim one-pixel border. */
static bento_status_t add_card(bento_ui_t *ui, int16_t x, int16_t w)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = x;
    args.y = CARD_Y;
    args.w = w;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM;
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* One of the two cards: where it is, its caption and digits. */
typedef struct {
    int16_t card_x;
    int16_t card_w;
    const char *caption;
    int16_t digits_w;
    const char *start_text; /* a Seg7 takes a text, not a number */
    uint32_t color;
} counter_place_t;

/* A card with a caption and seven-segment digits under it. */
static bento_status_t add_counter(bento_ui_t *ui,
                                  const counter_place_t *place,
                                  bento_ui_id_t *out_id)
{
    const int16_t inner_x = px(place->card_x + CARD_INSET_X);
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_card(ui, place->card_x, place->card_w);

    if (status == BENTO_OK) {
        status = add_label(ui, place->caption, inner_x, CAPTION_Y,
                           COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = place->start_text;
        args.text_len = (uint32_t)strlen(args.text);
        args.x = inner_x;
        args.y = DIGITS_Y;
        args.w = place->digits_w;
        args.h = DIGITS_H;
        args.color = place->color;
        status = bento_ui_seg7_create(ui, &args, out_id);
    }
    return status;
}

/* The two cards: the reading now, and the messages sent. */
static bento_status_t add_counters(link_screen_t *screen)
{
    static const counter_place_t now_card = {
        NOW_CARD_X,   NOW_CARD_W, "อุณหภูมิที่วัดได้ตอนนี้",
        NOW_DIGITS_W, "--",       COLOR_READ};
    static const counter_place_t sent_card = {
        SENT_CARD_X,   SENT_CARD_W, "ส่งออกไปแล้ว (ครั้ง)",
        SENT_DIGITS_W, "0",         COLOR_SENT};
    bento_status_t status =
        add_counter(screen->ui, &now_card, &screen->now_digits);

    if (status == BENTO_OK) {
        status =
            add_counter(screen->ui, &sent_card, &screen->sent_digits);
    }
    return status;
}

/* The title and the MQTT light with its label, top right. */
static bento_status_t add_header(link_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "ค่าจริงจากโต๊ะนี้ ออกไปหาคนอื่น";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = TITLE_X;
    args.y = TITLE_Y;
    args.color = COLOR_TEXT;
    args.font_px = TITLE_PX;
    status = bento_ui_label_create(screen->ui, &args, &unused);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = LED_X;
        args.y = LED_Y;
        args.w = LED_SIZE;
        args.h = LED_SIZE;
        args.color = COLOR_SENT;
        args.font_px = 0; /* dim: offline */
        status =
            bento_ui_led_create(screen->ui, &args, &screen->link_led);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "MQTT: ออฟไลน์", LINK_X, LINK_Y,
                           COLOR_DIM, &screen->link_label);
    }
    return status;
}

/* The chart, 20-35 C, with the green series added to the blue. */
static bento_status_t add_chart(link_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CHART_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.min_val = CHART_MIN_C;
    args.max_val = CHART_MAX_C;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status =
            bento_ui_chart_add_series(screen->ui, screen->chart,
                                      COLOR_SENT, &screen->sent_series);
    }
    return status;
}

/* The legend and the status line under the chart. */
static bento_status_t add_footer(link_screen_t *screen)
{
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "ฟ้า = วัดทุก 0.2 วิ", LEGEND_X,
                  LEGEND_READ_Y, COLOR_READ, &unused);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "เขียว = ส่งทุก 2 วิ", LEGEND_X,
                           LEGEND_SENT_Y, COLOR_SENT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังต่อ...", STATUS_X, STATUS_Y,
                           COLOR_DIM, &screen->status_line);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(link_screen_t *screen)
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
        status = add_counters(screen);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
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
static void show_link(const link_screen_t *screen, bool connected)
{
    (void)bento_ui_set_value(screen->ui, screen->link_led,
                             connected ? 1 : 0);
    set_text(screen->ui, screen->link_label,
             connected ? "MQTT: เชื่อมต่อแล้ว" : "MQTT: ออฟไลน์");
}

static void show_status(const link_screen_t *screen, const char *text,
                        uint32_t color)
{
    set_text(screen->ui, screen->status_line, text);
    (void)bento_ui_set_color(screen->ui, screen->status_line, color);
}

/* The fresh reading: digits and the blue line. A chart's
 * set_next() is a setter, ignored on refusal (modui.c). */
static void show_reading(const link_screen_t *screen,
                         bento_real_t temp_c)
{
    char number[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;

    /* a BENTO_FMT_BUF_SIZE buffer holds any number */
    (void)bento_fmt_real_precision(number, (uint32_t)sizeof(number),
                                   temp_c, 1, &length);
    set_text(screen->ui, screen->now_digits, number);
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  READ_SERIES, whole_degrees(temp_c));
}

static void show_summary(const link_screen_t *screen,
                         const telemetry_t *tele)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "จบรอบ - ส่งทั้งหมด %" PRId32 " ครั้ง", tele->sent);
    set_text(screen->ui, screen->status_line, text);
    drain_events(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>จบ - วัด %d รอบ ส่ง %" PRId32
                   " ครั้ง</span>",
                   ROUNDS, tele->sent);
    (void)bento_lcd_print(text);
    (void)printf("วัดกี่ครั้งกับส่งกี่ครั้งไม่เท่ากัน"
                 " และไม่ควรเท่ากัน\n");
}

/* MicroPython rounds a*b and +c separately: the compiler must not
 * fuse them, or the knob's temperature differs in the last digit. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

/* One message: the last value, rounded to a tenth. */
static void send_reading(const link_screen_t *screen, telemetry_t *tele,
                         uint32_t now_ms)
{
    static const char topic[] = "bento/team03/telemetry";
    char number[BENTO_FMT_BUF_SIZE];
    char payload[PAYLOAD_BYTES];
    char text[TEXT_BYTES];
    uint32_t length = 0U;
    bento_status_t sent_status;

    /* a BENTO_FMT_BUF_SIZE buffer holds any number */
    (void)bento_fmt_real(number, (uint32_t)sizeof(number),
                         round_tenth(tele->last_value), &length);
    write_payload(payload, sizeof(payload), number, now_ms);
    sent_status = bento_mqtt_publish(
        tele->broker, topic, (uint32_t)strlen(topic),
        (const uint8_t *)payload, (uint32_t)strlen(payload), 0U, 0U);
    if ((sent_status == BENTO_E_SESSION) ||
        (sent_status == BENTO_E_UNSUPPORTED)) {
        /* the link dropped: the screen says so instead of going
         * quiet (the original's OSError) */
        show_status(screen, "ส่งไม่ออก: MQTT not connected", COLOR_BAD);
        show_link(screen, false);
    } else {
        tele->sent++;
        (void)snprintf(text, sizeof(text), "%" PRId32, tele->sent);
        set_text(screen->ui, screen->sent_digits, text);
        (void)snprintf(text, sizeof(text),
                       "ส่งแล้ว %" PRId32 " ครั้ง | ล่าสุด %s C", tele->sent,
                       number);
        (void)bento_lcd_print(text);
    }
}

/* One round: measure, send when SEND_MS has passed, and feed the
 * green line the last value sent, so it becomes a staircase. */
static bento_status_t measure_once(const link_screen_t *screen,
                                   telemetry_t *tele)
{
    bento_sensors_snapshot_t snap;
    bento_real_t temp_c = (bento_real_t)0.0;
    bento_status_t status;

    bento_sensors_snapshot_init(&snap);
    status = bento_sensors_snapshot(BENTO_SENSORS_SNAPSHOT_DEFAULT_MS,
                                    &snap);
    if ((status == BENTO_OK) &&
        read_temp(&snap, &tele->source, &temp_c)) {
        tele->last_value = temp_c;
        tele->has_value = true;
        show_reading(screen, temp_c);
    }
    if (status == BENTO_OK) {
        const uint32_t now_ms = bento_time_ticks_ms();

        if (tele->has_value &&
            (bento_time_ticks_diff(now_ms, tele->last_send_ms) >=
             SEND_MS)) {
            tele->last_send_ms = now_ms;
            send_reading(screen, tele, now_ms);
        }
        if (tele->has_value) {
            (void)bento_ui_chart_set_next(
                screen->ui, screen->chart, screen->sent_series,
                whole_degrees(tele->last_value));
        }
        drain_events(screen->ui);
        status = wait_ms(READ_MS);
    }
    return status;
}

static bento_status_t measure_rounds(const link_screen_t *screen,
                                     telemetry_t *tele)
{
    bento_status_t status = BENTO_OK;
    int32_t i;

    tele->sent = 0;
    tele->has_value = false;
    tele->last_send_ms = bento_time_ticks_ms();
    for (i = 0; (i < ROUNDS) && (status == BENTO_OK); i++) {
        status = measure_once(screen, tele);
    }
    return status;
}

/* The BENTO Emulator's C runtime has no MQTT yet (BENTO_E_UNSUPPORTED):
 * a note beside the chart says so, and the console too. */
static bento_status_t show_emulator_note(const link_screen_t *screen)
{
    static const char note[] =
        "MQTT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";
    bento_ui_widget_args_t args;
    bento_ui_id_t label;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = note;
    args.text_len = (uint32_t)strlen(note);
    args.x = LEGEND_X;
    args.y = EMULATOR_NOTE_Y;
    args.w = EMULATOR_NOTE_W;
    args.color = (uint32_t)COLOR_WARN;
    args.font_px = TEXT_PX;
    status = bento_ui_label_create(screen->ui, &args, &label);
    (void)printf("%s / MQTT from C is not in the BENTO Emulator yet: "
                 "run it on a Dev Kit or Eva "
                 "Kit.\n",
                 note);
    return status;
}

/* From the broker connect to the end of the loop. Whatever ends it,
 * the cleanup says goodbye to the broker, so the name does not
 * linger there for another minute. */
static bento_status_t run_with_broker(const link_screen_t *screen,
                                      telemetry_t *tele)
{
    bento_status_t status = open_broker(&tele->broker);

    if (status == BENTO_E_UNSUPPORTED) {
        /* then on, as with a broker that cannot be reached */
        status = show_emulator_note(screen);
    }
    if (status == BENTO_OK) {
        show_link(screen, broker_connected(tele->broker));
        show_status(screen, "ต่อแล้ว ส่งทุก 2 วินาที", COLOR_SENT);
        drain_events(screen->ui);
        status = measure_rounds(screen, tele);
    }
    if (status == BENTO_OK) {
        close_broker(&tele->broker);
    }
    close_broker(&tele->broker);
    show_link(screen, false);
    return status;
}

int main(void)
{
    link_screen_t screen = {0};
    telemetry_t tele = {0};
    bool joined = false;
    bento_status_t status;

    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) { /* lcd.* ignore a console that is not
                                 ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ค่าจริงออกจากบอร์ด</h2>");
        status = join_wifi(&joined);
    }
    if ((status == BENTO_OK) && !joined) {
        show_status(&screen, "ต่อ WiFi ไม่ได้ - ตรวจชื่อวงกับรหัสผ่าน",
                    COLOR_BAD);
        drain_events(screen.ui);
    }
    if ((status == BENTO_OK) && joined) {
        status = run_with_broker(&screen, &tele);
    }
    if ((status == BENTO_OK) && joined) {
        show_summary(&screen, &tele);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
