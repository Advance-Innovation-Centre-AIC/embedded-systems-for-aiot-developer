/*
 * 04_ping_two_targets.c -- ยิงสองปลายทาง
 *
 * ต่อ WiFi ของห้องเรียน แล้ว ping สองปลายทาง 20 รอบ: เกตเวย์ (เลข .1
 * ของวงเดียวกับเรา) คือปลายในบ้าน 8.8.8.8 คือปลายนอกบ้าน กราฟเขียว
 * คือเกตเวย์ ฟ้าคืออินเทอร์เน็ต มีเลข ms ล่าสุด จำนวนรอบที่หาย และ
 * ประโยคสรุปว่าปัญหาอยู่ก่อนหรือหลังเราเตอร์
 *
 * Joins the class Wi-Fi, then pings two targets 20 times: the
 * gateway (the .1 of our own network) inside the house and 8.8.8.8
 * outside it. On the chart green is the gateway, blue the internet;
 * the screen shows the latest milliseconds, the rounds each target
 * lost, and one sentence saying whether the problem is before or
 * after the router.
 *
 * Twin of 04_ping_two_targets.py
 * (embedded-systems-for-aiot-developer/examples/s09/04_ping_two_targets.py),
 * merged from the library twin aiot-s09-04-ping-two-targets. Style: CLEAN_C.md.
 * Differs from the original:
 *   - wifi.ping() answers -1 for no answer; here the ping's
 *     BENTO_E_TIMEOUT is that -1. Any other failure, which raises
 *     OSError there and ends the program, ends it here too.
 *   - wifi.connect() prints console lines of its own. The ones C
 *     can know are printed: "[WiFi] Connecting..." and "[WiFi]
 *     Connected! IP=...". Not printed, because no C function
 *     reports them: the per-attempt and failure lines with the
 *     radio's result codes and "[WiFi] Credentials saved to QSPI"
 *     (modwifi.c:1037-1138).
 *   - Stop ends the program quietly, where MicroPython raises
 *     KeyboardInterrupt.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
static const char WIFI_SSID[] = "AIoT-Class";
static const char WIFI_PASS[] = "<รหัสผ่านของห้องเรียน>";
/* ---- end of settings ---- */

/* Rounds, times and sizes. */
enum {
    ROUNDS = 20,
    TIMEOUT_MS = 1500,
    CHART_MAX_MS = 60, /* home pings are 1-50 ms: 0-100 flattens */
    SETTLE_MS = 200,   /* let the cleared screen settle */
    PAUSE_MS = 150,    /* two short sleeps, the screen polled */
    PAUSES = 2,
    POLL_MS = 20,     /* how often a connect or ping is asked */
    NO_ANSWER = -1,   /* what wifi.ping() answers for silence */
    GATEWAY_HOST = 1, /* the router: .1 of our network */
    TEXT_BYTES = 128,
    /* An answer: "timeout" or an int32_t and " ms" (14 bytes), and
     * the NUL. */
    ANSWER_BYTES = 16
};

/* The widgets the program changes after it has drawn them. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t gateway_digits;
    bento_ui_id_t internet_digits;
    bento_ui_id_t lost_line;
    bento_ui_id_t round_line;
    bento_ui_id_t chart;
    uint8_t gateway_series;
    uint8_t internet_series;
    bento_ui_id_t verdict;
} ping_screen_t;

/* The two targets and what they answered so far. */
typedef struct {
    uint32_t gateway_ip;
    uint32_t internet_ip;
    int32_t gateway_lost;
    int32_t internet_lost;
    int32_t gateway_total_ms; /* answered rounds only: -1 would
                                 drag the average down */
    int32_t gateway_count;
} tally_t;

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 20,
    TITLE_Y = 12,
    CARD_Y = 48,
    CARD_W = 652,
    CARD_H = 92,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CAPTION_Y = 58,
    VALUE_Y = 84,
    GATEWAY_X = 40,
    INTERNET_X = 192,
    LOST_X = 352,
    ROUND_X = 532,
    DIGITS_X = 576,
    GATEWAY_DIGITS_Y = 80,
    INTERNET_DIGITS_Y = 140,
    DIGITS_W = 120,
    DIGITS_H = 52,
    CHART_Y = 200,
    CHART_H = 152,
    LEGEND_Y = 358,
    LEGEND_NET_X = 180,
    VERDICT_X = 520,
    VERDICT_Y = 360,
    TITLE_PX = 24,
    TEXT_PX = 20
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

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Dotted text of an address; a BENTO_WIFI_IPV4_TEXT_MAX buffer
 * holds any address. */
static void address_text(uint32_t ip_v4,
                         char text[BENTO_WIFI_IPV4_TEXT_MAX])
{
    uint32_t length = 0U;

    (void)bento_wifi_ipv4_format(ip_v4, text, BENTO_WIFI_IPV4_TEXT_MAX,
                                 &length);
}

/* wifi.ping(): the round trip in ms, or NO_ANSWER. */
static bento_status_t ping_ms(uint32_t ip_v4, int32_t *answer_ms)
{
    uint32_t rtt_ms = 0U;
    bento_status_t status = bento_wifi_ping_start(ip_v4, TIMEOUT_MS);

    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(POLL_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_ping_poll(&rtt_ms);
        } else {                            /* Stop between two polls */
            (void)bento_wifi_ping_cancel(); /* nothing to report */
        }
    }
    *answer_ms = (int32_t)rtt_ms;
    if (status == BENTO_E_TIMEOUT) {
        *answer_ms = NO_ANSWER;
        status = BENTO_OK;
    }
    return status;
}

/* "timeout", or "<n> ms". */
static void answer_text(int32_t answer_ms, char *text, size_t cap)
{
    if (answer_ms < 0) {
        (void)snprintf(text, cap, "timeout");
    } else {
        (void)snprintf(text, cap, "%" PRId32 " ms", answer_ms);
    }
}

/* The digits for an answer: "--" for none. */
static void digits_text(int32_t answer_ms, char *text, size_t cap)
{
    if (answer_ms < 0) {
        (void)snprintf(text, cap, "--");
    } else {
        (void)snprintf(text, cap, "%" PRId32, answer_ms);
    }
}

/* Counts the round in; only answered gateway rounds are averaged. */
static void count_round(tally_t *tally, int32_t gateway_ms,
                        int32_t internet_ms)
{
    if (gateway_ms < 0) {
        tally->gateway_lost++;
    } else {
        tally->gateway_total_ms += gateway_ms;
        tally->gateway_count++;
    }
    if (internet_ms < 0) {
        tally->internet_lost++;
    }
}

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

/* A 24 px value under a caption. */
static bento_status_t add_value(bento_ui_t *ui, const char *text,
                                int16_t x, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = VALUE_Y;
    args.color = COLOR_TEXT;
    args.font_px = TITLE_PX;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_digits(bento_ui_t *ui, int16_t y,
                                 uint32_t color, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "--"; /* a Seg7 takes a text, not a number */
    args.text_len = (uint32_t)strlen(args.text);
    args.x = DIGITS_X;
    args.y = y;
    args.w = DIGITS_W;
    args.h = DIGITS_H;
    args.color = color;
    return bento_ui_seg7_create(ui, &args, out_id);
}

/* The title and the card behind the numbers. */
static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "คาบ 9 - ping เกตเวย์ กับ อินเทอร์เน็ต";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = MARGIN_X;
    args.y = TITLE_Y;
    args.color = COLOR_TEXT;
    args.font_px = TITLE_PX;
    status = bento_ui_label_create(ui, &args, &unused);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = MARGIN_X;
        args.y = CARD_Y;
        args.w = CARD_W;
        args.h = CARD_H;
        args.color = COLOR_CARD;
        args.min_val = COLOR_DIM; /* a panel's min= is its border */
        args.max_val = CARD_RADIUS;
        args.font_px = CARD_BORDER;
        status = bento_ui_panel_create(ui, &args, &unused);
    }
    return status;
}

/* The numbers: both latest pings, the losses and the round. */
static bento_status_t add_numbers(ping_screen_t *screen)
{
    char rounds[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "เกตเวย์ ms", GATEWAY_X, CAPTION_Y,
                  COLOR_DIM, &unused);

    if (status == BENTO_OK) {
        status = add_digits(screen->ui, GATEWAY_DIGITS_Y, COLOR_OK,
                            &screen->gateway_digits);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "อินเทอร์เน็ต ms", INTERNET_X,
                           CAPTION_Y, COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        status = add_digits(screen->ui, INTERNET_DIGITS_Y, COLOR_INFO,
                            &screen->internet_digits);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "หายไป gw / net", LOST_X,
                           CAPTION_Y, COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_value(screen->ui, "0 / 0", LOST_X, &screen->lost_line);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "รอบ", ROUND_X, CAPTION_Y,
                           COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        (void)snprintf(rounds, sizeof(rounds), "0/%d", ROUNDS);
        status =
            add_value(screen->ui, rounds, ROUND_X, &screen->round_line);
    }
    return status;
}

/* The chart with its two lines, and the legend. */
static bento_status_t add_chart(ping_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CHART_Y;
    args.w = CARD_W;
    args.h = CHART_H;
    args.color = COLOR_CARD;
    args.min_val = 0;
    args.max_val = CHART_MAX_MS;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_OK,
                                           &screen->gateway_series);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_INFO,
                                           &screen->internet_series);
    }
    if (status == BENTO_OK) { /* two labels: each under 126 bytes */
        status = add_label(screen->ui, "เขียว = เกตเวย์", MARGIN_X,
                           LEGEND_Y, COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ฟ้า = อินเทอร์เน็ต (ms ต่อรอบ)",
                           LEGEND_NET_X, LEGEND_Y, COLOR_DIM, &unused);
    }
    return status;
}

/* A lost round feeds the chart 0, not -1, or the line would dive
 * under the axis. A chart's set_next() is a setter, ignored on
 * refusal (modui.c). */
static void feed_chart(const ping_screen_t *screen, int32_t gateway_ms,
                       int32_t internet_ms)
{
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  screen->gateway_series,
                                  (gateway_ms > 0) ? gateway_ms : 0);
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  screen->internet_series,
                                  (internet_ms > 0) ? internet_ms : 0);
}

/* Both digits: the texts first, then the colours, as the original
 * sets them. */
static void show_digits(const ping_screen_t *screen, int32_t gateway_ms,
                        int32_t internet_ms)
{
    char text[TEXT_BYTES];

    digits_text(gateway_ms, text, sizeof(text));
    set_text(screen->ui, screen->gateway_digits, text);
    digits_text(internet_ms, text, sizeof(text));
    set_text(screen->ui, screen->internet_digits, text);
    (void)bento_ui_set_color(screen->ui, screen->gateway_digits,
                             (gateway_ms < 0) ? COLOR_BAD : COLOR_OK);
    (void)bento_ui_set_color(screen->ui, screen->internet_digits,
                             (internet_ms < 0) ? COLOR_BAD
                                               : COLOR_INFO);
}

/* The screen, the console and the serial table for one round. */
static void show_round(const ping_screen_t *screen,
                       const tally_t *tally, int32_t round,
                       int32_t gateway_ms, int32_t internet_ms)
{
    char gateway[ANSWER_BYTES];
    char internet[ANSWER_BYTES];
    char text[TEXT_BYTES];
    uint32_t lost_color;

    feed_chart(screen, gateway_ms, internet_ms);
    show_digits(screen, gateway_ms, internet_ms);
    (void)snprintf(text, sizeof(text), "%" PRId32 " / %" PRId32,
                   tally->gateway_lost, tally->internet_lost);
    set_text(screen->ui, screen->lost_line, text);
    lost_color = COLOR_TEXT;
    if ((tally->gateway_lost + tally->internet_lost) > 0) {
        lost_color = COLOR_BAD;
    }
    (void)bento_ui_set_color(screen->ui, screen->lost_line, lost_color);
    (void)snprintf(text, sizeof(text), "%" PRId32 "/%d", round + 1,
                   ROUNDS);
    set_text(screen->ui, screen->round_line, text);
    answer_text(gateway_ms, gateway, sizeof(gateway));
    answer_text(internet_ms, internet, sizeof(internet));
    (void)snprintf(text, sizeof(text), "รอบ %" PRId32 " gw %s net %s",
                   round + 1, gateway, internet);
    (void)bento_lcd_print(text); /* lcd.print() ignores a console
                                    that is not ready (modlcd.c) */
    (void)printf("  %-4" PRId32 " %-12s %s\n", round + 1, gateway,
                 internet);
}

/* The losses, the gateway's average, and the heart of the file: the
 * two lines read together say where the problem is. */
static void show_verdict(const ping_screen_t *screen,
                         const tally_t *tally)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "เสีย gw %" PRId32 " net %" PRId32 " จาก %d รอบ",
                   tally->gateway_lost, tally->internet_lost, ROUNDS);
    (void)bento_lcd_print(text);
    (void)printf("เกตเวย์เสีย %" PRId32 " จาก %d"
                 "  อินเทอร์เน็ตเสีย %" PRId32 "\n",
                 tally->gateway_lost, ROUNDS, tally->internet_lost);
    if (tally->gateway_count > 0) {
        const int32_t average_ms =
            tally->gateway_total_ms / tally->gateway_count;

        (void)snprintf(text, sizeof(text),
                       "เกตเวย์ตอบเฉลี่ย %" PRId32 " ms จาก %" PRId32
                       " รอบ",
                       average_ms, tally->gateway_count);
        (void)bento_lcd_print(text);
        (void)printf("เกตเวย์ตอบเฉลี่ย %" PRId32 " ms จาก %" PRId32
                     " รอบที่ตอบ\n",
                     average_ms, tally->gateway_count);
    }
    if ((tally->gateway_lost == 0) && (tally->internet_lost > 0)) {
        paint(screen->ui, screen->verdict, COLOR_WARN,
              "ในบ้านปกติ ปัญหาอยู่เลยเราเตอร์ออกไป");
    } else if (tally->gateway_lost > 0) {
        paint(screen->ui, screen->verdict, COLOR_BAD,
              "เราเตอร์ยังไม่รอด ปัญหาที่คลื่นหรือวงแลน");
    } else {
        paint(screen->ui, screen->verdict, COLOR_OK,
              "เส้นทางโล่งทั้งสองช่วง");
    }
    drain_events(screen->ui);
    (void)bento_lcd_print("สรุป: ดูป้ายล่างสุดบนจอ");
}

/* Most class and home networks have their router at .1 of the same
 * network; if this room does not, put the real number here. */
static void pick_targets(const ping_screen_t *screen, tally_t *tally)
{
    static const char internet_ip[] = "8.8.8.8"; /* Google Public DNS */
    const uint32_t host_mask = 0xFFU;
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    char gateway[BENTO_WIFI_IPV4_TEXT_MAX];
    char text[TEXT_BYTES];
    uint32_t ip_v4 = 0U;

    if (bento_wifi_ip(BENTO_WIFI_IF_ANY, &ip_v4) != BENTO_OK) {
        ip_v4 = 0U; /* wifi.ip() answers "0.0.0.0" then */
    }
    tally->gateway_ip = (ip_v4 & ~host_mask) | (uint32_t)GATEWAY_HOST;
    /* a fixed, valid dotted quad: parsing it cannot fail */
    (void)bento_wifi_ipv4_parse(internet_ip,
                                (uint32_t)strlen(internet_ip),
                                &tally->internet_ip);
    address_text(ip_v4, address);
    address_text(tally->gateway_ip, gateway);
    (void)snprintf(text, sizeof(text), "IP เรา %s - ยิงเกตเวย์ %s",
                   address, gateway);
    paint(screen->ui, screen->verdict, COLOR_DIM, text);
    drain_events(screen->ui);
    (void)snprintf(text, sizeof(text), "IP %s เกตเวย์ %s", address,
                   gateway);
    (void)bento_lcd_print(text);
    (void)printf("รอบ   เกตเวย์      อินเทอร์เน็ต\n");
}

/* Draws the whole screen in the original's order. The verdict line
 * comes before connect(), which freezes the screen: the screen must
 * speak before it goes quiet. */
static bento_status_t build_screen(ping_screen_t *screen)
{
    char text[TEXT_BYTES];
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_numbers(screen);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "กำลังต่อ %s ... "
                       "จอจะนิ่งไปครู่หนึ่ง",
                       WIFI_SSID);
        status = add_label(screen->ui, text, VERDICT_X, VERDICT_Y,
                           COLOR_WARN, &screen->verdict);
    }
    if (status == BENTO_OK) {
        drain_events(screen->ui);
    }
    return status;
}

/* wifi.connect(): *joined is its True or False. A refused or
 * timed-out join is False; any other failure ends the program. */
static bento_status_t join_wifi(bool *joined)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_wifi_connect_params_t params;
    uint32_t ip_v4 = 0U;
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
        address_text(ip_v4, address);
        (void)printf("[WiFi] Connected! IP=%s\r\n", address);
    }
    return status;
}

/* ROUNDS rounds: the gateway first, always; that order tells which
 * part of the path broke. Between rounds two short sleeps with the
 * screen polled, so it keeps moving. */
static bento_status_t ping_rounds(const ping_screen_t *screen,
                                  tally_t *tally)
{
    bento_status_t status = BENTO_OK;
    int32_t round;
    int32_t pause;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        int32_t gateway_ms = NO_ANSWER;
        int32_t internet_ms = NO_ANSWER;

        status = ping_ms(tally->gateway_ip, &gateway_ms);
        if (status == BENTO_OK) {
            status = ping_ms(tally->internet_ip, &internet_ms);
        }
        if (status == BENTO_OK) {
            count_round(tally, gateway_ms, internet_ms);
            show_round(screen, tally, round, gateway_ms, internet_ms);
        }
        for (pause = 0; (pause < PAUSES) && (status == BENTO_OK);
             pause++) {
            drain_events(screen->ui);
            status = wait_ms(PAUSE_MS);
        }
    }
    return status;
}

int main(void)
{
    ping_screen_t screen = {0};
    tally_t tally = {0U, 0U, 0, 0, 0, 0};
    char text[TEXT_BYTES];
    bool joined = false;
    bento_status_t status;

    screen.ui = bento_runtime_get_ui();
    (void)bento_lcd_clear(); /* lcd.* ignore a console that is not
                                ready (modlcd.c) */
    (void)bento_lcd_console("<h2>คาบ 9 - ยิงสองปลายทาง</h2>");
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "กำลังต่อ %s - อาจรอนาน",
                       WIFI_SSID);
        (void)bento_lcd_print(text);
        status = join_wifi(&joined);
    }
    if ((status == BENTO_OK) && !joined) {
        paint(screen.ui, screen.verdict, COLOR_BAD,
              "ต่อไม่สำเร็จ หยุดตรงนี้");
        drain_events(screen.ui);
        (void)bento_lcd_print(
            "<span class=err>ต่อไม่สำเร็จ หยุดตรงนี้</span>");
    }
    if ((status == BENTO_OK) && joined) {
        pick_targets(&screen, &tally);
        status = ping_rounds(&screen, &tally);
    }
    if ((status == BENTO_OK) && joined) {
        show_verdict(&screen, &tally);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
