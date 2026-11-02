/*
 * 07_real_reading_over_tls.c -- ค่าที่วัดได้จริง ออกไปแบบที่คนกลางอ่านไม่ได้
 *
 * จอมีการ์ดสถานะ TLS (ไฟ + ข้อความ) เลขอุณหภูมิ เลขนับครั้งที่ส่งสำเร็จ แถบนับถอยหลัง
 * ถึงการส่งครั้งต่อไป และหมายเหตุ แล้วเรียก tesaiot.connect() ถ้าต่อไม่ได้ การ์ด
 * บอกให้รัน 01_config_store.py ก่อนแล้วจบ ถ้าต่อได้ ไฟเขียวติด แล้วอ่าน
 * tesaiot.device_id() มาต่อท้ายข้อความ ต้องรันไฟล์ 01 และ 02 ของคาบนี้ก่อน
 *
 * The screen holds a TLS status card (a LED and a line), the measured
 * temperature, the count of messages sent, a bar counting down to the next
 * send and a note; then it calls tesaiot.connect(). When the link cannot
 * open, the card says to run 01_config_store.py first and the program
 * ends. When it opens, the green LED lights and tesaiot.device_id() is
 * read for the card's text. Run files 01 and 02 of this session first.
 *
 * Twin of 07_real_reading_over_tls.py
 * (embedded-systems-for-aiot-developer/examples/s11/07_real_reading_over_tls.py),
 * from the library twin aiot-s11-07-real-reading-over-tls.
 * Style: CLEAN_C.md. What C changes, and what is left out on purpose:
 *  - tesaiot.device_id() returns bytes, and "text" + bytes raises
 *    TypeError in MicroPython, which nothing catches (only Stop is). So
 *    after a successful connect() the original always ends there; this
 *    program returns BENTO_E_INVALID_ARGUMENT at the same point, after the
 *    same LED write. The read_temp() helper, the 160-round publish loop,
 *    disconnect() and the closing lines are never reached with this
 *    binding, so they are not written here.
 *  - tesaiot.connect() answers True for BENTO_OK and False for
 *    BENTO_E_FAILED; any other status ends the program. A failed
 *    device_id() (OSError there) ends it with its status.
 *  - raise Stop / except Stop: pass is the not-connected path ending
 *    with 0.
 */
#include <stdint.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_tesaiot.h"
#include "bento_time.h"

enum { /* times and sizes */
    SETTLE_MS = 200,
    SEND_MS = 5000,
    UID_BYTES = 64
};
enum { /* layout */
    MARGIN_X = 20,
    TITLE_Y = 12,
    WIDE_CARD_W = 740,
    TLS_CARD_Y = 52,
    TLS_CARD_H = 64,
    LED_X = 44,
    LED_Y = 68,
    LED_SIZE = 48,
    TLS_LINE_X = 100,
    TLS_LINE_Y = 72,
    HALF_CARD_Y = 132,
    HALF_CARD_W = 360,
    HALF_CARD_H = 100,
    RIGHT_CARD_X = 400,
    LEFT_INNER_X = 36,
    RIGHT_INNER_X = 416,
    CAPTION_Y = 144,
    SEG_Y = 172,
    SEG_H = 48,
    NOW_SEG_W = 200,
    SENT_SEG_W = 160,
    BAR_CAPTION_Y = 248,
    BAR_Y = 272,
    BAR_H = 24,
    NOTE_Y = 308
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
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t led_tls;
    bento_ui_id_t tls_line;
    bento_ui_id_t seg_now;
    bento_ui_id_t seg_sent;
    bento_ui_id_t bar_next;
    bento_ui_id_t note;
} tls_screen_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

/* ui.poll() answers [] on any failure: its events are dropped. */
static void poll_screen(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const rect_t *place, uint32_t color,
                                int32_t font_px, bento_ui_id_t *id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, id);
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
                               const rect_t *place, uint32_t color,
                               bento_ui_id_t *id)
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
    return bento_ui_seg7_create(ui, &args, id);
}

/* The green LED off, and the line that says the link is not open yet. */
static bento_status_t add_tls_card(tls_screen_t *screen)
{
    const rect_t card = {MARGIN_X, TLS_CARD_Y, WIDE_CARD_W, TLS_CARD_H};
    const rect_t line = {TLS_LINE_X, TLS_LINE_Y, 0, 0};
    bento_ui_widget_args_t args;
    bento_status_t status = add_card(screen->ui, &card);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = LED_X;
        args.y = LED_Y;
        args.w = LED_SIZE;
        args.h = LED_SIZE;
        args.color = (uint32_t)COLOR_OK;
        args.font_px = 0;
        status =
            bento_ui_led_create(screen->ui, &args, &screen->led_tls);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่ได้ต่อ", &line,
                           (uint32_t)COLOR_DIM, FONT_TITLE,
                           &screen->tls_line);
    }
    return status;
}

/* Measure often, send seldom: both rhythms side by side. */
static bento_status_t add_number_cards(tls_screen_t *screen)
{
    const rect_t left = {MARGIN_X, HALF_CARD_Y, HALF_CARD_W,
                         HALF_CARD_H};
    const rect_t left_caption = {LEFT_INNER_X, CAPTION_Y, 0, 0};
    const rect_t now = {LEFT_INNER_X, SEG_Y, NOW_SEG_W, SEG_H};
    const rect_t right = {RIGHT_CARD_X, HALF_CARD_Y, HALF_CARD_W,
                          HALF_CARD_H};
    const rect_t right_caption = {RIGHT_INNER_X, CAPTION_Y, 0, 0};
    const rect_t sent = {RIGHT_INNER_X, SEG_Y, SENT_SEG_W, SEG_H};
    bento_ui_id_t unused;
    bento_status_t status = add_card(screen->ui, &left);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "อุณหภูมิที่วัดได้", &left_caption,
                           (uint32_t)COLOR_DIM, FONT_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg7(screen->ui, "--", &now, (uint32_t)COLOR_INFO,
                          &screen->seg_now);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui, &right);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ส่งสำเร็จ (ครั้ง)", &right_caption,
                           (uint32_t)COLOR_DIM, FONT_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg7(screen->ui, "0", &sent, (uint32_t)COLOR_OK,
                          &screen->seg_sent);
    }
    return status;
}

static bento_status_t add_countdown(tls_screen_t *screen)
{
    const rect_t caption = {MARGIN_X, BAR_CAPTION_Y, 0, 0};
    const rect_t note = {MARGIN_X, NOTE_Y, 0, 0};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "อีกกี่วินาทีถึงส่งครั้งต่อไป", &caption,
                  (uint32_t)COLOR_DIM, FONT_TEXT, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = MARGIN_X;
        args.y = BAR_Y;
        args.w = WIDE_CARD_W;
        args.h = BAR_H;
        args.color = (uint32_t)COLOR_INFO;
        args.min_val = 0;
        args.max_val = SEND_MS;
        args.font_px = 0;
        status =
            bento_ui_bar_create(screen->ui, &args, &screen->bar_next);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "เข้ารหัสกันคนกลางอ่าน ไม่ได้กันค่าผิด", &note,
                      (uint32_t)COLOR_DIM, FONT_TEXT, &screen->note);
    }
    return status;
}

static bento_status_t build_screen(tls_screen_t *screen)
{
    const rect_t title = {MARGIN_X, TITLE_Y, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status;

    /* MicroPython ignores a refused ui.screen(): the runtime already set it up */
    (void)bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = bento_time_sleep_ms((uint32_t)SETTLE_MS);
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ค่าจริง ส่งแบบเข้ารหัส", &title,
                           (uint32_t)COLOR_TEXT, FONT_TITLE, &unused);
    }
    if (status == BENTO_OK) {
        status = add_tls_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_number_cards(screen);
    }
    if (status == BENTO_OK) {
        status = add_countdown(screen);
    }
    return status;
}

/* The link uses the settings stored by files 01 and 02 of this session. */
static bento_status_t open_link(const tls_screen_t *screen)
{
    static const char not_open[] = "ต่อไม่ได้ - รัน 01_config_store.py ก่อน";
    uint8_t uid[UID_BYTES];
    uint32_t uid_length = 0U;
    bento_status_t status = bento_tesaiot_connect_start();

    if (status == BENTO_E_FAILED) {
        (void)bento_ui_set_text(screen->ui, screen->tls_line, not_open,
                                (uint32_t)strlen(not_open));
        (void)bento_ui_set_color(screen->ui, screen->tls_line,
                                 (uint32_t)COLOR_BAD);
        poll_screen(screen->ui);
        status =
            BENTO_E_CANCELLED; /* the original's Stop: a quiet end */
    } else if (status == BENTO_OK) {
        (void)bento_ui_set_value(screen->ui, screen->led_tls, 1);
        status = bento_tesaiot_device_id(uid, (uint32_t)sizeof(uid),
                                         &uid_length);
        if (status == BENTO_OK) {
            /* text + bytes: the TypeError that ends the original here */
            status = BENTO_E_INVALID_ARGUMENT;
        }
    } else {
        /* no TESAIoT on this firmware: the status ends the program */
    }
    return status;
}

int main(void)
{
    tls_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        poll_screen(screen.ui);
        (void)
            bento_lcd_clear(); /* lcd calls ignore a console not ready (modlcd.c) */
        (void)bento_lcd_console("<h2>ค่าจริงผ่านช่องเข้ารหัส</h2>");
        status = open_link(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
