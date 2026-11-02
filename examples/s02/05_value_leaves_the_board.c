/*
 * 05_value_leaves_the_board.c -- ค่าที่วัดได้ ออกจากบอร์ด
 * (A value leaves the board)
 *
 * บันไดสามขั้น: WiFi ได้ IP ก่อน แล้วจึงต่อ MQTT broker แล้วจึงส่ง
 * ป้ายสามขั้นเปลี่ยนจากเทาเป็นเขียวทีละขั้น ขั้นที่ไม่ผ่านเป็นแดง
 * พร้อมเหตุผลแล้วจบ ผ่านครบแล้วส่งค่าลูกบิดและค่าเอียง (az) เป็น JSON
 * 60 ใบ ห่างกันใบละ 2 วินาที เลขใบที่ส่งเดินขึ้นบนจอ แก้ WIFI_SSID
 * WIFI_PASS และ TEAM ก่อนรัน (ยังไม่แก้ TEAM โปรแกรมบอกบนจอแล้วจบ)
 * พอร์ต 1883 ไม่เข้ารหัส ห้ามส่งของลับ
 *
 * A three-step ladder: Wi-Fi gets an address first, then the MQTT
 * broker, then the sending. The three step labels turn from grey to
 * green one by one; a step that fails turns red with its reason and
 * the program ends. Then 60 JSON messages with the knob and the tilt
 * (az) go out, 2 seconds apart, and the count climbs on screen. Set
 * WIFI_SSID, WIFI_PASS and TEAM first (with TEAM unset the program
 * says so and ends). Port 1883 is not encrypted: send no secrets.
 *
 * Twin of 05_value_leaves_the_board.py
 * (embedded-systems-for-aiot-developer/examples/s02/05_value_leaves_the_board.py).
 * Style: CLEAN_C.md.
 * In the BENTO Emulator, MQTT from C is not available yet: the
 * program says so and ends.
 * Differs from the original:
 *   - wifi.connect() and mqtt.connect() print console lines of their
 *     own. Here the program prints the ones C can know: "[WiFi]
 *     Connecting...", "[WiFi] Connected! IP=...", "[MQTT] Connecting
 *     to ...", "[MQTT] Connected to ... as '...'". The per-attempt
 *     and failure lines with the radio's result codes are not
 *     printed: no C function reports them.
 *   - mqtt.connect() raised OSError when the board's Wi-Fi layer was
 *     not ready; here that is a client that could not even start. A
 *     refused or timed-out connect is the original's False.
 *   - json.dumps writes the keys in the slot order of MicroPython's
 *     dict (py/map.c, py/qstr.c): id, uptime_s, az, n, knob with the
 *     PSoC Edge's 1-byte qstr hash; knob, id, az, uptime_s, n with
 *     the reTerminal's 2-byte one.
 *   - sensors.snapshot() waits up to 16 s right after a reset; C asks
 *     for BENTO_SENSORS_SNAPSHOT_DEFAULT_MS (1 s). A failed snapshot
 *     only leaves knob -1 and az -99.0 in both.
 *   - The MQTT client is closed when the program ends. The original
 *     leaves it open and its next mqtt.connect() cleans it up; a C
 *     client would outlive the program and hold one of the module's
 *     two clients.
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

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
/* แก้สามบรรทัดนี้ตามที่ผู้สอนแจก
 * Set these three from what the teacher hands out. */
/* the team's phone hotspot: faculty Wi-Fi needs a login */
static const char WIFI_SSID[] = "bento-teamXX";
/* 8 characters or more */
static const char WIFI_PASS[] = "<รหัส Hotspot ของทีม>";
/* team01 to team19; unset, the program will not run */
static const char TEAM[] = "teamXX";
/* ---- end of settings ---- */

/* The link and the sending. */
enum {
    PORT = 1883,      /* MQTT, not encrypted */
    KEEPALIVE_S = 60, /* silent longer, the broker may drop us */
    MESSAGES = 60,    /* no retain: repeat for two minutes */
    GAP_MS = 2000,
    DHCP_TRIES = 15, /* ask for the address again this often */
    DHCP_WAIT_MS = 200,
    SETTLE_MS = 200, /* let the cleared screen settle */
    WIFI_SLICE_MS = 50,
    MQTT_SLICE_MS = 20
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* Sizes and the readings' stand-ins. */
enum {
    LINE_BYTES = 192,    /* a label or console line, cut at 126/127 */
    NAME_BYTES = 64,     /* the device id or the topic */
    PAYLOAD_BYTES = 160, /* the JSON body with any numbers */
    TEAM_CHARS = 6,      /* "team" and two digits */
    TEAM_PREFIX = 4,
    NO_KNOB = -1,
    HUNDREDTHS = 100, /* round(x, 2) */
    MS_PER_S = 1000
};

/* The widgets the program changes after it has drawn them. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t wifi;
    bento_ui_id_t broker;
    bento_ui_id_t publish;
    bento_ui_id_t count;
    bento_ui_id_t bar;
    bento_ui_id_t payload;
    bento_ui_id_t note;
} panel_t;

/* The names built from TEAM. */
typedef struct {
    char device_id[NAME_BYTES];
    char topic[NAME_BYTES];
} names_t;

/* One round's readings: -1 and -99.0 when they cannot be read. */
typedef struct {
    int32_t knob;
    bento_real_t az;
} reading_t;

static bento_status_t check_running(bento_status_t status)
{
    bento_status_t result = status;

    if ((result == BENTO_OK) && !bento_runtime_is_running()) {
        result = BENTO_E_CANCELLED;
    }
    return result;
}

/* wifi.ip(): "0.0.0.0" when there is no address. */
static void read_ip(char *text, uint32_t capacity)
{
    uint32_t address = 0U;
    uint32_t length = 0U;

    if ((bento_wifi_ip(BENTO_WIFI_IF_ANY, &address) != BENTO_OK) ||
        (bento_wifi_ipv4_format(address, text, capacity, &length) !=
         BENTO_OK)) {
        (void)snprintf(text, capacity, "0.0.0.0");
    }
}

static bool has_no_ip(const char *ip)
{
    return strcmp(ip, "0.0.0.0") == 0;
}

/* Asks the started join every WIFI_SLICE_MS until it ends. The
 * poll itself answers BENTO_E_CANCELLED on Stop. */
static bento_status_t wait_connect(void)
{
    bento_status_t status = BENTO_OK;
    bool waiting = true;

    while (waiting) {
        status = bento_wifi_connect_poll();
        waiting = status == BENTO_E_IN_PROGRESS;
        if (waiting) {
            status = check_running(bento_time_sleep_ms(WIFI_SLICE_MS));
            waiting = status == BENTO_OK;
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* answers BENTO_OK always */
    }
    return status;
}

/* Asks the started connect every MQTT_SLICE_MS until it ends. */
static bento_status_t wait_broker(uint32_t broker)
{
    bento_status_t status = BENTO_OK;
    bool waiting = true;

    while (waiting) {
        status = check_running(bento_time_sleep_ms(MQTT_SLICE_MS));
        if (status == BENTO_OK) {
            status = bento_mqtt_open_poll(broker);
        }
        waiting = status == BENTO_E_IN_PROGRESS;
    }
    return status;
}

/* round(x, 2): nearbyint(x * 100) / 100 in the board's float width
 * (py/modbuiltins.c, mp_builtin_round). */
static bento_real_t round_hundredth(bento_real_t value)
{
    const bento_real_t scale = (bento_real_t)HUNDREDTHS;
    const bento_real_t scaled = value * scale;

    return (bento_real_t)nearbyint((double)scaled) / scale;
}

/* Real values from the board, never random ones. A round that
 * cannot read keeps -1 and -99.0 and still sends. */
static reading_t read_board(void)
{
    const bento_real_t no_tilt = (bento_real_t)-99.0;
    bento_sensors_snapshot_t snapshot;
    reading_t reading = {NO_KNOB, no_tilt};

    bento_sensors_snapshot_init(&snapshot);
    if (bento_sensors_snapshot(BENTO_SENSORS_SNAPSHOT_DEFAULT_MS,
                               &snapshot) == BENTO_OK) {
        if (snapshot.has_pot != 0U) {
            reading.knob = (int32_t)snapshot.pot_percent;
        }
        if (snapshot.has_bmi270 != 0U) { /* lying flat: about 9.8 */
            reading.az = round_hundredth((bento_real_t)snapshot.az);
        }
    }
    return reading;
}

/* The same for the whole class. The device id and the topic are
 * built from TEAM in main(), so no two boards share a client id. */
/* The spare broker is test.mosquitto.org. */
static const char BROKER[] = "broker.hivemq.com";

/* mqtt.connect(BROKER, port=, client_id=, keepalive=). *started is
 * false when no client could even start (the original's OSError),
 * *linked false when the broker refused or never answered (its
 * False). Only Stop is an error. */
static bento_status_t open_broker(const names_t *names,
                                  uint32_t *broker, bool *started,
                                  bool *linked)
{
    bento_mqtt_config_t config;
    bento_status_t status;

    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = (uint16_t)PORT;
    config.keepalive_s = (uint16_t)KEEPALIVE_S;
    config.client_id = names->device_id;
    config.client_id_len = (uint32_t)strlen(names->device_id);
    (void)printf("[MQTT] Connecting to %s:%d ...\n", BROKER, PORT);
    status = bento_mqtt_open_start(&config, broker);
    *started = status == BENTO_OK;
    if (*started) {
        status = wait_broker(*broker);
    }
    *linked = status == BENTO_OK;
    if (*linked) {
        (void)printf("[MQTT] Connected to %s:%d as '%s'\n", BROKER,
                     PORT, names->device_id);
    }
    /* Stop, and the Emulator's missing MQTT, go back to the caller */
    return ((status == BENTO_E_CANCELLED) ||
            (status == BENTO_E_UNSUPPORTED))
               ? status
               : BENTO_OK;
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show_text(const panel_t *panel, bento_ui_id_t label,
                      const char *text)
{
    (void)bento_ui_set_text(panel->ui, label, text,
                            (uint32_t)strlen(text));
}

/* Colour first, as the original sets it. */
static void show(const panel_t *panel, bento_ui_id_t label,
                 uint32_t color, const char *text)
{
    (void)bento_ui_set_color(panel->ui, label, color);
    show_text(panel, label, text);
}

/* ui.poll() here only lets the labels reach the screen before a
 * call that blocks; its events are not used. */
static void flush_screen(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void console_line(const char *text)
{
    (void)bento_lcd_print(text);
}

static void show_linked(const panel_t *panel, const names_t *names)
{
    char line[LINE_BYTES];

    (void)snprintf(line, sizeof(line), "2) broker    ต่อแล้ว %s", BROKER);
    show(panel, panel->broker, COLOR_OK, line);
    show(panel, panel->publish, COLOR_WARN, "3) publish   กำลังส่ง");
    (void)snprintf(line, sizeof(line), "หัวข้อ %s", names->topic);
    show(panel, panel->note, COLOR_DIM, line);
    flush_screen(panel->ui);
    console_line("<span class=ok>ต่อ broker แล้ว</span>");
    (void)snprintf(line, sizeof(line), "หัวข้อที่ส่ง: %s", names->topic);
    console_line(line);
}

/* Ends politely: the screen says where it stopped, never silent,
 * so nobody at the bench has to guess. */
static void stop_here(const panel_t *panel, bento_ui_id_t label,
                      const char *screen_text, const char *reason)
{
    char line[LINE_BYTES];

    show(panel, label, COLOR_BAD, screen_text);
    show(panel, panel->note, COLOR_BAD, reason);
    flush_screen(panel->ui);
    (void)snprintf(line, sizeof(line), "<span class=error>%s</span>",
                   reason);
    console_line(line);
    (void)printf("หยุดที่: %s\n", reason);
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

/* Step 2: introduce the board to the broker. A client id another
 * board uses makes the broker kick the older one, which is why the
 * id carries the team. */
static bento_status_t step_broker(const panel_t *panel,
                                  const names_t *names,
                                  uint32_t *broker, bool *passed)
{
    char line[LINE_BYTES];
    bool started = false;
    bento_status_t status;

    (void)snprintf(line, sizeof(line), "2) broker    กำลังต่อ %s",
                   BROKER);
    show(panel, panel->broker, COLOR_WARN, line);
    show_text(panel, panel->note,
              "ถ้าเน็ตของห้องกันพอร์ต 1883 "
              "ขั้นนี้จะไม่ผ่าน");
    flush_screen(panel->ui);
    (void)snprintf(line, sizeof(line),
                   "2) กำลังต่อ broker %s พอร์ต "
                   "1883",
                   BROKER);
    console_line(line);
    status = open_broker(names, broker, &started, passed);
    if (status == BENTO_E_UNSUPPORTED) {
        show_emulator_note(panel->ui, panel->broker,
                           "2) broker    ต่อไม่ได้", panel->note);
        *passed = false; /* the ladder stops here, normally */
        status = BENTO_OK;
    } else if ((status == BENTO_OK) && !started) {
        stop_here(panel, panel->broker, "2) broker    ต่อไม่ได้",
                  "ชั้น WiFi ของบอร์ดไม่พร้อม ลองรันใหม่");
    } else if ((status == BENTO_OK) && !*passed) {
        (void)snprintf(line, sizeof(line),
                       "%s ไม่ตอบ: เน็ตกันพอร์ต 1883 "
                       "หรือชื่อผิด",
                       BROKER);
        stop_here(panel, panel->broker, "2) broker    ต่อไม่ได้", line);
    } else if (status == BENTO_OK) {
        show_linked(panel, names);
    } else {
        /* Stop ends the run */
    }
    return status;
}

/* Layout on the 792 x 398 canvas. */
enum {
    FONT_PX = 20,
    TITLE_PX = 24,
    TITLE_X = 24,
    TITLE_Y = 8,
    WARNING_X = 384,
    WARNING_Y = 16,
    CARD_X = 24,
    CARD_W = 744,
    STEPS_Y = 56,
    STEPS_H = 144,
    COUNT_CARD_Y = 216,
    COUNT_CARD_H = 120,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    INSET_X = 40,
    WIFI_Y = 72,
    BROKER_Y = 112,
    PUBLISH_Y = 152,
    CAPTION_Y = 232,
    SEG_Y = 264,
    SEG_W = 160,
    SEG_H = 56,
    PROGRESS_X = 248,
    BAR_W = 328,
    BAR_H = 24,
    PAYLOAD_Y = 296,
    NOTE_X = 24,
    NOTE_Y = 348
};

static void place(bento_ui_widget_args_t *args, int16_t x, int16_t y,
                  uint32_t color)
{
    bento_ui_widget_args_init(args);
    args->x = x;
    args->y = y;
    args->color = color;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const bento_ui_widget_args_t *where,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args = *where;

    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_text(bento_ui_t *ui, const char *text,
                               int16_t x, int16_t y, uint32_t color,
                               bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    place(&args, x, y, color);
    return add_label(ui, text, &args, FONT_PX, out_id);
}

/* A card: a rounded panel, its border in its own colour. */
static bento_status_t add_card(bento_ui_t *ui, int16_t y, int16_t h)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    place(&args, CARD_X, y, COLOR_CARD);
    args.w = CARD_W;
    args.h = h;
    args.min_val = COLOR_CARD;
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &card);
}

static bento_status_t add_heading(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    place(&args, TITLE_X, TITLE_Y, COLOR_TEXT);
    status = add_label(ui, "ส่งค่าออกจากบอร์ด", &args, TITLE_PX, &unused);
    if (status == BENTO_OK) {
        status = add_text(ui, "พอร์ต 1883 ไม่เข้ารหัส ห้ามส่งของลับ",
                          WARNING_X, WARNING_Y, COLOR_WARN, &unused);
    }
    return status;
}

static bento_status_t add_steps(panel_t *panel)
{
    bento_status_t status = add_card(panel->ui, STEPS_Y, STEPS_H);

    if (status == BENTO_OK) {
        status = add_text(panel->ui, "1) WiFi      ยังไม่ถึงคิว", INSET_X,
                          WIFI_Y, COLOR_DIM, &panel->wifi);
    }
    if (status == BENTO_OK) {
        status = add_text(panel->ui, "2) broker    ยังไม่ถึงคิว", INSET_X,
                          BROKER_Y, COLOR_DIM, &panel->broker);
    }
    if (status == BENTO_OK) {
        status = add_text(panel->ui, "3) publish   ยังไม่ถึงคิว", INSET_X,
                          PUBLISH_Y, COLOR_DIM, &panel->publish);
    }
    return status;
}

/* The count is a 7-segment text, and the bar is coloured after it
 * is made, as in the original. */
static bento_status_t add_counter(panel_t *panel)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_card(panel->ui, COUNT_CARD_Y, COUNT_CARD_H);

    if (status == BENTO_OK) {
        status = add_text(panel->ui, "ส่งไปแล้ว (ใบ)", INSET_X, CAPTION_Y,
                          COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        place(&args, INSET_X, SEG_Y, COLOR_ACCENT);
        args.text = "0";
        args.text_len = 1U;
        args.w = SEG_W;
        args.h = SEG_H;
        status = bento_ui_seg7_create(panel->ui, &args, &panel->count);
    }
    if (status == BENTO_OK) {
        status = add_text(panel->ui, "ความคืบหน้า", PROGRESS_X, CAPTION_Y,
                          COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        place(&args, PROGRESS_X, SEG_Y, 0U);
        args.w = BAR_W;
        args.h = BAR_H;
        args.max_val = MESSAGES;
        status = bento_ui_bar_create(panel->ui, &args, &panel->bar);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_color(panel->ui, panel->bar, COLOR_ACCENT);
        status = add_text(panel->ui, "ยังไม่ได้ประกอบ payload", PROGRESS_X,
                          PAYLOAD_Y, COLOR_DIM, &panel->payload);
    }
    return status;
}

static bento_status_t build_screen(panel_t *panel)
{
    bento_status_t status =
        bento_ui_screen(panel->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = check_running(bento_time_sleep_ms(SETTLE_MS));
    }
    if (status == BENTO_OK) {
        status = add_heading(panel->ui);
    }
    if (status == BENTO_OK) {
        status = add_steps(panel);
    }
    if (status == BENTO_OK) {
        status = add_counter(panel);
    }
    if (status == BENTO_OK) {
        status = add_text(panel->ui, "กำลังเริ่ม", NOTE_X, NOTE_Y,
                          COLOR_DIM, &panel->note);
    }
    if (status == BENTO_OK) {
        flush_screen(panel->ui);
        (void)bento_lcd_clear(); /* lcd calls ignore their result */
        (void)bento_lcd_console("<h2>ส่งค่าออกจากบอร์ด</h2>");
    }
    return status;
}

/* "0.0.0.0" is a text that is not empty, so the address must be
 * compared; DHCP gets DHCP_TRIES more chances. */
static bento_status_t wait_for_ip(const panel_t *panel, char *ip,
                                  uint32_t capacity)
{
    int32_t attempt;
    bento_status_t status = BENTO_OK;

    read_ip(ip, capacity);
    for (attempt = 0; (attempt < DHCP_TRIES) && has_no_ip(ip) &&
                      (status == BENTO_OK);
         attempt++) {
        flush_screen(panel->ui);
        status = check_running(bento_time_sleep_ms(DHCP_WAIT_MS));
        if (status == BENTO_OK) {
            read_ip(ip, capacity);
        }
    }
    return status;
}

/* The address is shown only once it is real. */
static bento_status_t show_address(const panel_t *panel, bool *passed)
{
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    char line[LINE_BYTES];
    const bento_status_t status = wait_for_ip(panel, ip, sizeof(ip));

    *passed = (status == BENTO_OK) && !has_no_ip(ip);
    if ((status == BENTO_OK) && !*passed) {
        stop_here(panel, panel->wifi,
                  "1) WiFi      ลิงก์ขึ้นแต่ไม่มี "
                  "IP",
                  "ลิงก์ขึ้นแต่ DHCP ไม่ให้เลข ส่งอะไรออกไม่ได้");
    }
    if (*passed) {
        (void)snprintf(line, sizeof(line), "1) WiFi      IP %s", ip);
        show(panel, panel->wifi, COLOR_OK, line);
        flush_screen(panel->ui);
        (void)snprintf(line, sizeof(line),
                       "<span class=ok>ได้ IP %s </span>", ip);
        console_line(line);
    }
    return status;
}

/* A message that went out: count, bar, what it carried. */
static void show_sent(const panel_t *panel, int32_t n,
                      const reading_t *reading, const char *az,
                      const char *payload)
{
    char line[LINE_BYTES];

    (void)snprintf(line, sizeof(line), "%" PRId32, n);
    show_text(panel, panel->count, line); /* Seg7 takes a text */
    (void)bento_ui_set_value(panel->ui, panel->bar, n);
    (void)snprintf(line, sizeof(line),
                   "ใบที่ %" PRId32 " knob=%" PRId32 " az=%s", n,
                   reading->knob, az);
    show(panel, panel->payload, COLOR_TEXT, line);
    flush_screen(panel->ui);
    (void)snprintf(line, sizeof(line), "ใบที่ %" PRId32 " -> %s", n,
                   payload);
    console_line(line);
    (void)printf("ส่ง: %s\n", payload);
}

/* A lost link makes publish() raise OSError, it does not answer
 * False, so both ways are told apart. */
static void show_not_sent(const panel_t *panel, bento_status_t result,
                          int32_t n, int32_t sent)
{
    char line[LINE_BYTES];

    if (result == BENTO_E_SESSION) {
        (void)snprintf(line, sizeof(line),
                       "3) publish   สายหลุดที่ใบที่ %" PRId32, n);
        show(panel, panel->publish, COLOR_BAD, line);
        (void)snprintf(line, sizeof(line),
                       "ส่งไปได้ %" PRId32 " ใบก่อนสายหลุด", sent);
        show(panel, panel->note, COLOR_BAD, line);
        flush_screen(panel->ui);
        (void)snprintf(
            line, sizeof(line),
            "<span class=error>สายหลุดที่ใบที่ %" PRId32 " </span>", n);
    } else {
        (void)snprintf(line, sizeof(line),
                       "3) publish   ถูกปฏิเสธที่ใบที่ %" PRId32, n);
        show(panel, panel->publish, COLOR_BAD, line);
        flush_screen(panel->ui);
        (void)snprintf(
            line, sizeof(line),
            "<span class=error>ใบที่ %" PRId32 " ถูกปฏิเสธ</span>", n);
    }
    console_line(line);
}

/* is_connected() says whether the link is up now; connect()'s
 * answer said only that it was up then. */
static void summarize(const panel_t *panel, uint32_t broker,
                      int32_t sent)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;
    char line[LINE_BYTES];
    bool still;

    /* an unknown state counts as not connected */
    if (bento_mqtt_state(broker, &state) != BENTO_OK) {
        state = BENTO_MQTT_STATE_CLOSED;
    }
    still = state == BENTO_MQTT_STATE_CONNECTED;
    if (sent == MESSAGES) {
        (void)snprintf(line, sizeof(line), "3) publish   ส่งครบ %d ใบ",
                       MESSAGES);
        show(panel, panel->publish, COLOR_OK, line);
    }
    (void)snprintf(line, sizeof(line),
                   "ส่งได้ %" PRId32 " ใบ | is_connected() = %s", sent,
                   still ? "True" : "False");
    show(panel, panel->note, still ? COLOR_OK : COLOR_WARN, line);
    flush_screen(panel->ui);
    (void)bento_lcd_console(
        "<span class=muted>------------------------</span>");
    (void)snprintf(line, sizeof(line),
                   "<span class=ok>ส่งได้ %" PRId32 " ใบ จาก %d "
                   "</span>",
                   sent, MESSAGES);
    console_line(line);
    (void)printf("ส่งได้ %" PRId32 " ใบ | ยังต่ออยู่: %s\n", sent,
                 still ? "True" : "False");
}

/* The scan's answers: too big for the 8 KB program stack, so at
 * file scope, zeroed by the loader and set again in main()
 * (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];

/* team01 to team19 style: "team", two digits, not team00. Another
 * team's name would make the broker kick that team's board off. */
static bool team_is_set(void)
{
    const char *digits = &TEAM[TEAM_PREFIX];
    bool good = (strlen(TEAM) == (size_t)TEAM_CHARS) &&
                (strncmp(TEAM, "team", (size_t)TEAM_PREFIX) == 0);

    good = good && (digits[0] >= '0') && (digits[0] <= '9') &&
           (digits[1] >= '0') && (digits[1] <= '9');
    return good && (strcmp(TEAM, "team00") != 0);
}

/* wifi.connect(): *joined is false when the network was not found
 * or refused us, the original's False; other failures end the run,
 * as its OSError does. The binding saves the network after a join,
 * so the twin asks for that too. */
static bento_status_t connect_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = WIFI_PASS;
    params.pass_len = (uint32_t)strlen(WIFI_PASS);
    params.flags = BENTO_WIFI_F_SAVE_CREDS;
    (void)printf("[WiFi] Connecting...\n");
    status = bento_wifi_connect_start(&params);
    if (status == BENTO_OK) {
        status = wait_connect();
    }
    *joined = status == BENTO_OK;
    if (*joined) {
        read_ip(address, sizeof(address));
        (void)printf("[WiFi] Connected! IP=%s\n", address);
    }
    if ((status == BENTO_E_TIMEOUT) || (status == BENTO_E_FAILED)) {
        status = BENTO_OK;
    }
    return status;
}

/* Whether a scan hears our network: "not heard" and "wrong
 * password" are fixed in different ways. A failed scan only means
 * not heard; Stop is the one error. */
static bento_status_t network_heard(bool *heard)
{
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = bento_wifi_scan_start();
    bool waiting = status == BENTO_OK;

    *heard = false;
    while (waiting) {
        status = bento_wifi_scan_poll(g_networks, BENTO_WIFI_SCAN_MAX,
                                      &count);
        waiting = status == BENTO_E_IN_PROGRESS;
        if (waiting) {
            status = check_running(bento_time_sleep_ms(WIFI_SLICE_MS));
            waiting = status == BENTO_OK;
        }
    }
    for (i = 0U; (status == BENTO_OK) && (i < count); i++) {
        *heard = *heard || (strcmp(g_networks[i].ssid, WIFI_SSID) == 0);
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_scan_cancel(); /* answers BENTO_OK always */
    }
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

static bento_status_t explain_no_join(const panel_t *panel)
{
    char reason[LINE_BYTES];
    bool heard = false;
    const bento_status_t status = network_heard(&heard);

    if (status == BENTO_OK) {
        if (heard) {
            (void)snprintf(reason, sizeof(reason),
                           "ได้ยินวง %s แต่ต่อไม่ผ่าน "
                           "ตรวจรหัสผ่าน",
                           WIFI_SSID);
        } else {
            (void)snprintf(reason, sizeof(reason),
                           "ไม่ได้ยินวง %s เลย ตรวจชื่อวงหรือย้ายที่",
                           WIFI_SSID);
        }
        stop_here(panel, panel->wifi, "1) WiFi      ต่อไม่ติด", reason);
    }
    return status;
}

/* Step 1: Wi-Fi with an address. The label is up before connect()
 * because the screen freezes while it runs (up to about 85 s). */
static bento_status_t step_wifi(const panel_t *panel, bool *passed)
{
    char line[LINE_BYTES];
    bool joined = false;
    uint32_t start_ms;
    bento_status_t status;

    (void)snprintf(line, sizeof(line), "1) WiFi      กำลังต่อ %s",
                   WIFI_SSID);
    show(panel, panel->wifi, COLOR_WARN, line);
    show(panel, panel->note, COLOR_WARN,
         "ครั้งแรกอาจรอนาน จอจะนิ่ง อย่ากดรีเซ็ต");
    flush_screen(panel->ui);
    (void)snprintf(line, sizeof(line), "1) กำลังต่อ WiFi %s", WIFI_SSID);
    console_line(line);
    start_ms = bento_time_ticks_ms();
    status = connect_wifi(&joined);
    if (status == BENTO_OK) {
        (void)snprintf(
            line, sizeof(line),
            "connect() ใช้เวลา %" PRId32 " ms คืนค่า %s",
            bento_time_ticks_diff(bento_time_ticks_ms(), start_ms),
            joined ? "True" : "False");
        console_line(line);
    }
    *passed = false;
    if ((status == BENTO_OK) && !joined) {
        status = explain_no_join(panel);
    } else if (status == BENTO_OK) {
        status = show_address(panel, passed);
    } else {
        /* the join failed beyond False: its status ends the run */
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

/* json.dumps of {"id", "n", "knob", "az", "uptime_s"}, keys in the
 * slot order of the board's MicroPython dict (header). */
static void write_payload(char *payload, size_t capacity, int32_t n,
                          const reading_t *reading, const char *az)
{
    const uint32_t uptime_s =
        bento_time_ticks_ms() / (uint32_t)MS_PER_S;

    if (on_reterminal()) {
        (void)snprintf(payload, capacity,
                       "{\"knob\": %" PRId32 ", \"id\": \"%s\","
                       " \"az\": %s, \"uptime_s\": %" PRIu32
                       ", \"n\": %" PRId32 "}",
                       reading->knob, TEAM, az, uptime_s, n);
    } else {
        (void)snprintf(payload, capacity,
                       "{\"id\": \"%s\", \"uptime_s\": %" PRIu32
                       ", \"az\": %s, \"n\": %" PRId32
                       ", \"knob\": %" PRId32 "}",
                       TEAM, uptime_s, az, n, reading->knob);
    }
}

/* One message: true when it went out. */
static bool send_one(const panel_t *panel, const names_t *names,
                     uint32_t broker, int32_t n, int32_t sent)
{
    char az[BENTO_FMT_BUF_SIZE];
    char payload[PAYLOAD_BYTES];
    uint32_t length = 0U;
    const reading_t reading = read_board();
    bento_status_t result;

    /* a BENTO_FMT_BUF_SIZE buffer holds any number */
    (void)bento_fmt_real(az, sizeof(az), reading.az, &length);
    write_payload(payload, sizeof(payload), n, &reading, az);
    result = bento_mqtt_publish(
        broker, names->topic, (uint32_t)strlen(names->topic),
        (const uint8_t *)payload, (uint32_t)strlen(payload), 0U, 0U);
    if (result == BENTO_OK) {
        show_sent(panel, n, &reading, az, payload);
    } else {
        show_not_sent(panel, result, n, sent);
    }
    return result == BENTO_OK;
}

/* Step 3: MESSAGES messages, GAP_MS apart, until one fails. */
static bento_status_t step_publish(const panel_t *panel,
                                   const names_t *names,
                                   uint32_t broker, int32_t *sent)
{
    bool going = true;
    int32_t n;
    bento_status_t status = BENTO_OK;

    *sent = 0;
    for (n = 1; (n <= MESSAGES) && going && (status == BENTO_OK); n++) {
        going = send_one(panel, names, broker, n, *sent);
        if (going) {
            *sent = n;
            status = check_running(bento_time_sleep_ms(GAP_MS));
        }
    }
    return status;
}

/* The three steps in order: each runs only when the one before
 * passed. */
static bento_status_t run_ladder(const panel_t *panel,
                                 const names_t *names, uint32_t *broker)
{
    bool passed = team_is_set();
    int32_t sent = 0;
    bento_status_t status = BENTO_OK;

    if (!passed) {
        stop_here(panel, panel->wifi,
                  "1) WiFi      "
                  "ยังไม่ได้ตั้งชื่อทีม",
                  "แก้ TEAM เป็นเลขทีมของคุณก่อน เช่น team03");
    }
    if (passed) {
        status = step_wifi(panel, &passed);
    }
    if ((status == BENTO_OK) && passed) {
        status = step_broker(panel, names, broker, &passed);
    }
    if ((status == BENTO_OK) && passed) {
        status = step_publish(panel, names, *broker, &sent);
    }
    if ((status == BENTO_OK) && passed) {
        summarize(panel, *broker, sent);
    }
    return status;
}

int main(void)
{
    /* the whole class uses the same root; no need to change it */
    static const char root[] = "bento-aiot";
    panel_t panel = {0};
    names_t names;
    uint32_t broker = 0U;
    bento_status_t status;

    (void)memset(g_networks, 0, sizeof(g_networks));
    (void)snprintf(names.device_id, sizeof(names.device_id), "%s-%s",
                   root, TEAM);
    (void)snprintf(names.topic, sizeof(names.topic), "%s/%s/telemetry",
                   root, TEAM);
    panel.ui = bento_runtime_get_ui();
    status = build_screen(&panel);
    if (status == BENTO_OK) {
        status = run_ladder(&panel, &names, &broker);
    }
    if (broker != 0U) { /* the client would outlive the program */
        (void)bento_mqtt_close(broker); /* frees it, whatever state */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
