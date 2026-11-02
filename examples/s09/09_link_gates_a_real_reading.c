/*
 * 09_link_gates_a_real_reading.c -- ค่ารออยู่
 * ลิงก์เป็นคนเปิดประตู (The Link Gates a Real Reading)
 *
 * ทุกรอบโปรแกรมวัดอุณหภูมิก่อนเสมอ ไม่ถามเน็ต (SHT40 ถ้าบอร์ดมี
 * ไม่งั้นลูกบิด POTEN 0-100 % แทน 15-45 C) แล้วเก็บค่าใส่คิว
 * จากนั้นจึงถามว่า WiFi ต่ออยู่ไหม ถ้าต่ออยู่ปล่อยคิวทีละค่า
 * แถบบนบอกสถานะลิงก์ เลขซ้ายคือค่าล่าสุด เลขขวาคือค่าที่ค้างในคิว
 * ถอด WiFi ที่เราเตอร์แล้วดูเลขขวาเดินขึ้น คิวเก็บได้ 60 ค่า
 * เต็มแล้วทิ้งของเก่าสุด ทำงาน 150 รอบ รอบละ 200 ms แล้วสรุปผล
 *
 * Each round the program measures first and never asks the network
 * (the SHT40 when the board has one, else the POTEN knob, 0-100 % =
 * 15-45 C), and puts the value in a queue. Only then does it ask
 * whether Wi-Fi is up; when it is, the queue lets one value go. The
 * top strip shows the link, the left number the last reading, the
 * right number what waits in the queue. Unplug Wi-Fi at the router
 * and watch the right number climb. The queue holds 60 values and
 * drops the oldest when full. 150 rounds of 200 ms, then a summary.
 *
 * Twin of 09_link_gates_a_real_reading.py
 * (embedded-systems-for-aiot-developer/examples/s09/09_link_gates_a_real_reading.py),
 * merged from the library twin aiot-s09-09-link-gates-a-real-reading
 * (the course file names fewer constants; it runs the same). Style:
 * CLEAN_C.md.
 *
 * Differs from the original:
 *   - The queue's values are never read again, only its length, so
 *     the queue is a count here: append, drop the oldest and send
 *     the oldest change that count the same way.
 *   - wifi.connect() is bento_wifi_connect_start and its poll. The
 *     binding's own console lines are written by the program here
 *     ("[WiFi] Connecting...", "[WiFi] Connected! IP=..."); its
 *     per-attempt failure lines are not, the C API does not report
 *     them. A failed join is ignored, as the original ignores the
 *     False it returns. The credentials are saved on success, as
 *     the binding does (BENTO_WIFI_F_SAVE_CREDS).
 *   - hasattr(sensors, "sht40") is a fact of the MicroPython build.
 *     Here the SHT40 is asked, and any refusal (no SHT40 on this
 *     board, or a read error, the original's OSError) falls back to
 *     the knob, as there.
 *   - sensors.snapshot() waits up to 16 s for the display core in
 *     the first seconds after a reset; C asks for
 *     BENTO_SENSORS_SNAPSHOT_DEFAULT_MS (1 s). A failed snapshot
 *     ends the program in both. A build whose C runtime has no
 *     snapshot path (the BENTO Emulator answers
 *     BENTO_E_UNSUPPORTED) reads it as a snapshot without the "pot"
 *     key: the SHT40 is asked, and a round with neither has no
 *     reading, the original's (None, "") path.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
static const char WIFI_SSID[] = "AIoT-Class";
static const char WIFI_PASS[] = "<รหัสผ่านของห้องเรียน>";
/* ---- end of settings ---- */

/* MicroPython rounds a*b and +c separately: the compiler must not
 * fuse them, or the knob's temperature differs in the last digit. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

/* The queue and the pace. */
enum {
    QUEUE_MAX = 60,                   /* then the oldest is dropped */
    QUEUE_WARN = (QUEUE_MAX * 3) / 4, /* above: the bar is red */
    ROUNDS = 150,
    LOOP_MS = 200,
    LOG_EVERY = 10,    /* a console line every this many sent */
    SETTLE_MS = 200,   /* let the cleared screen settle */
    JOIN_POLL_MS = 20, /* ask the radio this often while joining */
    TEXT_CHARS = 128   /* a SET_TEXT carries at most 126 bytes */
};

/* Where the temperature came from last; it is announced on the
 * console only when it changes. */
typedef enum {
    SOURCE_NONE = 0,
    SOURCE_SHT40,
    SOURCE_KNOB
} temp_source_t;

/* The widgets the loop changes. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t link_text;
    bento_ui_id_t link_led;
    bento_ui_id_t temp_digits;
    bento_ui_id_t queue_digits;
    bento_ui_id_t queue_bar;
    bento_ui_id_t note;
} gate_screen_t;

/* What the loop counts. */
typedef struct {
    int32_t queued;
    int32_t sent;
    int32_t dropped;
    temp_source_t source;
} gate_tally_t;

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

/* The board's address as text, "0.0.0.0" when it has none. */
static void format_ip(char *text, uint32_t cap)
{
    uint32_t ip_v4 = 0U;
    uint32_t length = 0U;

    if (bento_wifi_ip(BENTO_WIFI_IF_ANY, &ip_v4) != BENTO_OK) {
        ip_v4 = 0U;
    }
    if (bento_wifi_ipv4_format(ip_v4, text, cap, &length) != BENTO_OK) {
        text[0] = '\0'; /* only a too-small buffer fails */
    }
}

/* Joins the class network and waits for the answer. A refusal is
 * not an error here: the original ignores connect()'s False and
 * the loop shows the link as it is. Only Stop ends the program. */
static bento_status_t join_wifi(void)
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
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(JOIN_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_connect_poll();
        }
    }
    if (status == BENTO_OK) {
        format_ip(address, sizeof(address));
        (void)printf("[WiFi] Connected! IP=%s\n", address);
    } else if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* Stop wins */
    } else {
        status = BENTO_OK; /* connect() answered False */
    }
    return status;
}

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 20,
    TITLE_Y = 12,
    TITLE_PX = 24,
    TEXT_PX = 20,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    LINK_Y = 52,
    LINK_W = 740,
    LINK_H = 64,
    LINK_TEXT_X = 40,
    LINK_TEXT_Y = 72,
    LED_X = 692,
    LED_Y = 68,
    LED_SIZE = 48,
    CARD_Y = 132,
    CARD_W = 360,
    CARD_H = 100,
    CAPTION_Y = 144,
    SEG_Y = 172,
    SEG_H = 48,
    TEMP_CARD_X = 20,
    TEMP_X = 36,
    TEMP_W = 200,
    QUEUE_CARD_X = 400,
    QUEUE_X = 416,
    QUEUE_W = 160,
    BAR_Y = 252,
    BAR_W = 740,
    BAR_H = 24,
    NOTE_Y = 292
};

/* Where a label goes and its font size. */
typedef struct {
    int16_t x;
    int16_t y;
    int32_t font_px;
} spot_t;

/* A rectangle on the screen. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

/* The widget setters ignore a refusal in MicroPython (modui.c), so
 * they do here too. */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const spot_t *at, uint32_t color,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = at->x;
    args.y = at->y;
    args.color = color;
    args.font_px = at->font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A rounded card with a dim one-pixel border. */
static bento_status_t add_card(bento_ui_t *ui, const rect_t *place)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;

    bento_ui_widget_args_init(&args);
    args.x = place->x;
    args.y = place->y;
    args.w = place->w;
    args.h = place->h;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM;   /* the border colour */
    args.max_val = CARD_RADIUS; /* the corner radius */
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &panel);
}

static bento_status_t add_digits(bento_ui_t *ui, const char *text,
                                 const rect_t *place, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.w = place->w;
    args.h = place->h;
    args.color = color;
    return bento_ui_seg7_create(ui, &args, out_id);
}

/* The link strip: its card, the status text and the LED. */
static bento_status_t add_link_strip(gate_screen_t *screen)
{
    const rect_t card = {MARGIN_X, LINK_Y, LINK_W, LINK_H};
    const spot_t text = {LINK_TEXT_X, LINK_TEXT_Y, TITLE_PX};
    bento_ui_widget_args_t args;
    bento_status_t status = add_card(screen->ui, &card);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังต่อ...", &text, COLOR_DIM,
                           &screen->link_text);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = LED_X;
        args.y = LED_Y;
        args.w = LED_SIZE;
        args.h = LED_SIZE;
        args.color = COLOR_OK;
        args.font_px = 0;
        status =
            bento_ui_led_create(screen->ui, &args, &screen->link_led);
    }
    return status;
}

/* The two number cards: the last reading and the queue. */
static bento_status_t add_number_cards(gate_screen_t *screen)
{
    const rect_t temp_card = {TEMP_CARD_X, CARD_Y, CARD_W, CARD_H};
    const spot_t temp_caption = {TEMP_X, CAPTION_Y, TEXT_PX};
    const rect_t temp_digits = {TEMP_X, SEG_Y, TEMP_W, SEG_H};
    const rect_t queue_card = {QUEUE_CARD_X, CARD_Y, CARD_W, CARD_H};
    const spot_t queue_caption = {QUEUE_X, CAPTION_Y, TEXT_PX};
    const rect_t queue_digits = {QUEUE_X, SEG_Y, QUEUE_W, SEG_H};
    bento_ui_id_t caption;
    bento_status_t status = add_card(screen->ui, &temp_card);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "อุณหภูมิที่วัดได้", &temp_caption,
                           COLOR_DIM, &caption);
    }
    if (status == BENTO_OK) {
        status = add_digits(screen->ui, "--", &temp_digits, COLOR_INFO,
                            &screen->temp_digits);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui, &queue_card);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ค้างในคิว (ค่า)", &queue_caption,
                           COLOR_DIM, &caption);
    }
    if (status == BENTO_OK) {
        status = add_digits(screen->ui, "0", &queue_digits, COLOR_OK,
                            &screen->queue_digits);
    }
    return status;
}

/* The queue bar and the hint under it. */
static bento_status_t add_queue_bar(gate_screen_t *screen)
{
    const spot_t note = {MARGIN_X, NOTE_Y, TEXT_PX};
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = BAR_Y;
    args.w = BAR_W;
    args.h = BAR_H;
    args.color = COLOR_OK;
    args.min_val = 0;
    args.max_val = QUEUE_MAX;
    args.font_px = 0;
    status = bento_ui_bar_create(screen->ui, &args, &screen->queue_bar);
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "ถอด WiFi ที่เราเตอร์ "
                           "แล้วดูเลขขวาเดินขึ้น",
                           &note, COLOR_DIM, &screen->note);
    }
    return status;
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

/* Draws the screen in the original's order. */
static bento_status_t build_screen(gate_screen_t *screen)
{
    const spot_t title = {MARGIN_X, TITLE_Y, TITLE_PX};
    bento_ui_id_t title_label;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ค่ารออยู่ ลิงก์เป็นคนเปิดประตู", &title,
                           COLOR_TEXT, &title_label);
    }
    if (status == BENTO_OK) {
        status = add_link_strip(screen);
    }
    if (status == BENTO_OK) {
        status = add_number_cards(screen);
    }
    if (status == BENTO_OK) {
        status = add_queue_bar(screen);
    }
    return status;
}

static void show_queue(const gate_screen_t *screen,
                       const gate_tally_t *tally)
{
    char digits[TEXT_CHARS];

    (void)snprintf(digits, sizeof(digits), "%" PRId32, tally->queued);
    set_text(screen->ui, screen->queue_digits, digits);
    (void)bento_ui_set_value(screen->ui, screen->queue_bar,
                             tally->queued);
    (void)bento_ui_set_color(screen->ui, screen->queue_bar,
                             (tally->queued > QUEUE_WARN) ? COLOR_BAD
                                                          : COLOR_OK);
}

/* The summary on the screen, in the drawer and on the console. */
static void show_summary(const gate_screen_t *screen,
                         const gate_tally_t *tally)
{
    char text[TEXT_CHARS];

    (void)snprintf(text, sizeof(text),
                   "จบ - ปล่อยไป %" PRId32 " ค่า ทิ้งเพราะคิวเต็ม %" PRId32,
                   tally->sent, tally->dropped);
    set_text(screen->ui, screen->note, text);
    poll_display(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ปล่อย %" PRId32 " | ค้าง %" PRId32
                   " | ทิ้ง %" PRId32 " </span>",
                   tally->sent, tally->queued, tally->dropped);
    (void)bento_lcd_print(text);
    (void)printf("เซนเซอร์ไม่ได้พังตอนเน็ตหลุด"
                 " จึงไม่มีเหตุผลให้หยุดวัด\n");
}

/* Measure first, whatever the network does: the sensor did not
 * break when the link went down. A full queue drops its oldest. */
static bento_status_t measure(const gate_screen_t *screen,
                              gate_tally_t *tally)
{
    bento_sensors_snapshot_t snap;
    bento_real_t temp_c = (bento_real_t)0.0;
    char digits[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;
    bento_status_t status;

    bento_sensors_snapshot_init(&snap);
    status = bento_sensors_snapshot(BENTO_SENSORS_SNAPSHOT_DEFAULT_MS,
                                    &snap);
    if (status == BENTO_E_UNSUPPORTED) {
        /* a build with no snapshot path (the BENTO Emulator): a
         * snapshot without the "pot" key, so read_temp() takes the
         * SHT40, or has no reading this round, as the original does */
        bento_sensors_snapshot_init(&snap);
        status = BENTO_OK;
    }
    if ((status == BENTO_OK) &&
        read_temp(&snap, &tally->source, &temp_c)) {
        if (bento_fmt_real_precision(digits, sizeof(digits), temp_c, 1,
                                     &length) != BENTO_OK) {
            digits[0] = '\0'; /* the buffer is always large enough */
        }
        set_text(screen->ui, screen->temp_digits, digits);
        tally->queued++;
        if (tally->queued > QUEUE_MAX) {
            tally->queued--;
            tally->dropped++;
        }
    }
    return status;
}

/* The oldest value leaves; every LOG_EVERY values the console says
 * how many have gone. lcd.print() is sent without an answer. */
static void send_one(gate_tally_t *tally)
{
    char text[TEXT_CHARS];

    if (tally->queued > 0) {
        tally->queued--;
        tally->sent++;
        if ((tally->sent % LOG_EVERY) == 0) {
            (void)snprintf(text, sizeof(text),
                           "ปล่อยไปแล้ว %" PRId32 " ค่า | ค้าง %" PRId32,
                           tally->sent, tally->queued);
            (void)bento_lcd_print(text);
        }
    }
}

/* Then ask the link. One value leaves per round, never the whole
 * queue at once: that would be firing a burst at the receiver. */
static void send_if_linked(const gate_screen_t *screen,
                           gate_tally_t *tally)
{
    char text[TEXT_CHARS];
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    bool up = false;

    if (bento_wifi_is_connected(&up) != BENTO_OK) {
        up = false; /* no radio answers as "not connected" */
    }
    (void)bento_ui_set_value(screen->ui, screen->link_led, up ? 1 : 0);
    if (up) {
        format_ip(address, sizeof(address));
        (void)snprintf(text, sizeof(text), "ออนไลน์ - %s", address);
        set_text(screen->ui, screen->link_text, text);
        (void)bento_ui_set_color(screen->ui, screen->link_text,
                                 COLOR_OK);
        send_one(tally);
    } else {
        set_text(screen->ui, screen->link_text,
                 "ออฟไลน์ - "
                 "ค่ายังถูกเก็บไว้");
        (void)bento_ui_set_color(screen->ui, screen->link_text,
                                 COLOR_BAD);
    }
}

static bento_status_t run_rounds(const gate_screen_t *screen,
                                 gate_tally_t *tally)
{
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = measure(screen, tally);
        if (status == BENTO_OK) {
            send_if_linked(screen, tally);
            show_queue(screen, tally);
            poll_display(screen->ui);
            status = wait_ms(LOOP_MS);
        }
    }
    return status;
}

int main(void)
{
    gate_screen_t screen;
    gate_tally_t tally;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(&tally, 0, sizeof(tally));
    screen.ui = bento_runtime_get_ui();
    tally.source = SOURCE_NONE;
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        poll_display(screen.ui);
        /* lcd calls ignore a console that is not ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ลิงก์เปิดประตูให้ค่าที่รออยู่</h2"
                                ">");
        status = join_wifi();
    }
    if (status == BENTO_OK) {
        status = run_rounds(&screen, &tally);
    }
    if (status == BENTO_OK) {
        show_summary(&screen, &tally);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
