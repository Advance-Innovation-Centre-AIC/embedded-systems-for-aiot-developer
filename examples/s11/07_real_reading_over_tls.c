/*
 * 07_real_reading_over_tls.c -- ค่าที่วัดได้จริง ออกไปแบบที่คนกลางอ่านไม่ได้
 *
 * จอมีการ์ดสถานะ TLS (ไฟ + ข้อความ) เลขอุณหภูมิที่วัดได้ล่าสุด เลขนับครั้งที่ส่งสำเร็จ
 * แถบนับถอยหลังถึงการส่งครั้งต่อไป และหมายเหตุ แล้วเรียก tesaiot.connect() ถ้าต่อ
 * ไม่ได้ การ์ดบอกให้รัน 01_config_store.py ก่อนแล้วจบ ถ้าต่อได้ ไฟเขียวติด แสดงรหัส
 * อุปกรณ์ แล้ววัดอุณหภูมิทุก 0.2 วินาที (SHT40 ถ้าบอร์ดมี ไม่งั้นใช้ลูกบิด 0-100 %
 * แทน 15-45 C) และส่งค่าล่าสุดผ่านช่องเข้ารหัสทุก 5 วินาที 160 รอบ แล้วตัดการเชื่อมต่อ
 * ต้องรันไฟล์ 01 และ 02 ของคาบนี้ก่อน
 *
 * The screen holds a TLS status card (a LED and a line), the latest
 * temperature, the count of messages sent, a bar counting down to the next
 * send and a note; then it calls tesaiot.connect(). When the link cannot
 * open, the card says to run 01_config_store.py first and the program ends.
 * When it opens, the green LED lights, the card shows the device id, and
 * for 160 rounds the program measures every 0.2 s (the SHT40 when the board
 * has one, else the knob, 0-100 % = 15-45 C) and publishes the latest
 * reading over the encrypted link every 5 s; then it disconnects. Run files
 * 01 and 02 of this session first.
 *
 * Twin of 07_real_reading_over_tls.py
 * (embedded-systems-for-aiot-developer/examples/s11/07_real_reading_over_tls.py),
 * from the library twin aiot-s11-07-real-reading-over-tls. Style: CLEAN_C.md.
 * What C changes:
 *  - str(tesaiot.device_id()) is the repr of the id bytes, b'...' with the
 *    quote and the escapes of py/objstr.c mp_str_print_quoted, written out
 *    by format_bytes_repr();
 *  - tesaiot.connect() answers True for BENTO_OK and False for
 *    BENTO_E_FAILED; any other status ends the program, as a failed
 *    device_id() (OSError there) or sensors.snapshot() does;
 *  - tesaiot.publish() answers True or False and never raises
 *    (modtesaiot.c:799-815): True is BENTO_OK from bento_tesaiot_publish,
 *    any other status is False, a miss; disconnect() answers True or False
 *    too, and its status is ignored as the original ignores it;
 *  - hasattr(sensors, "sht40") plus try/except OSError is one status test
 *    of bento_sensors_sht40_temperature(); the global _TEMP_SRC is a member
 *    of the link state; json.dumps of the one-key dict is snprintf;
 *  - round(x, 1) is written out (ties to the even tenth) without the maths
 *    library; "{:.1f}" and str(float) go through bento_fmt, which prints
 *    what the board's MicroPython prints;
 *  - raise Stop / except Stop: pass is the not-connected path ending with 0;
 *    Stop during a sleep ends the program there, without disconnect(), as
 *    the uncaught KeyboardInterrupt does.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_tesaiot.h"
#include "bento_time.h"

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add, or the knob temperature drifts. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

enum { /* times, counts and sizes */
    SETTLE_MS = 200,
    LOOP_MS = 200,
    SEND_MS = 5000,
    ROUNDS = 160,
    TENTHS = 10,
    TEXT_BYTES = 128, /* a SET_TEXT carries at most 126 bytes */
    REPR_LINE_BYTES =
        1024 /* the prefix, b'', and 4 bytes per id byte */
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

/* Where the temperature came from last; a new source is announced once. */
typedef enum {
    SOURCE_NONE = 0,
    SOURCE_SHT40,
    SOURCE_KNOB
} temp_source_t;

/* What the loop keeps between rounds. */
typedef struct {
    int32_t sent;
    int32_t fails;
    uint32_t last_send_ms;
    bool has_value;
    bento_real_t last_value;
    temp_source_t source;
} link_state_t;

static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(const tls_screen_t *screen, bento_ui_id_t label,
                     const char *text)
{
    (void)bento_ui_set_text(screen->ui, label, text,
                            (uint32_t)strlen(text));
}

/* round(x, 1): nearbyint(x * 10) / 10, a tie going to the even tenth. */
static bento_real_t round_tenth(bento_real_t value)
{
    const bento_real_t half = (bento_real_t)0.5;
    const bento_real_t scaled = value * (bento_real_t)TENTHS;
    int32_t whole = (int32_t)scaled;
    const bento_real_t rest = scaled - (bento_real_t)whole;
    const bool odd = (whole % 2) != 0;

    if ((rest > half) || ((rest == half) && odd)) {
        whole++;
    } else if ((rest < -half) || ((rest == -half) && odd)) {
        whole--;
    } else {
        /* already the nearest tenth */
    }
    return (bento_real_t)whole / (bento_real_t)TENTHS;
}

/* str(round(value, 1)) */
static void format_tenth(char *text, uint32_t cap, bento_real_t value)
{
    uint32_t length = 0U;

    if (bento_fmt_real(text, cap, round_tenth(value), &length) !=
        BENTO_OK) {
        text[0] = '\0'; /* BENTO_FMT_BUF_SIZE is always enough */
    }
}

/* One byte of a bytes repr, escaped as MicroPython escapes it. */
static void format_repr_byte(char *piece, uint32_t cap, uint8_t byte,
                             char quote)
{
    const uint8_t first_printable = 0x20U;
    const uint8_t delete_char = 0x7FU;

    if ((byte == (uint8_t)quote) || (byte == (uint8_t)'\\')) {
        (void)snprintf(piece, cap, "\\%c", (char)byte);
    } else if ((byte >= first_printable) && (byte < delete_char)) {
        (void)snprintf(piece, cap, "%c", (char)byte);
    } else if (byte == (uint8_t)'\n') {
        (void)snprintf(piece, cap, "\\n");
    } else if (byte == (uint8_t)'\r') {
        (void)snprintf(piece, cap, "\\r");
    } else if (byte == (uint8_t)'\t') {
        (void)snprintf(piece, cap, "\\t");
    } else {
        (void)snprintf(piece, cap, "\\x%02x", (unsigned int)byte);
    }
}

/* str(bytes): b'...' in single quotes, or in double quotes when the bytes
 * hold a single quote and no double one (py/objstr.c). */
static void format_bytes_repr(char *text, uint32_t cap,
                              const uint8_t *bytes, uint32_t count)
{
    bool has_single = false;
    bool has_double = false;
    char piece[BENTO_FMT_BUF_SIZE];
    char quote;
    uint32_t i;

    for (i = 0U; i < count; i++) {
        has_single = has_single || (bytes[i] == (uint8_t)'\'');
        has_double = has_double || (bytes[i] == (uint8_t)'"');
    }
    quote = (has_single && !has_double) ? '"' : '\'';
    (void)snprintf(text, cap, "b%c", quote);
    for (i = 0U; i < count; i++) {
        const size_t used = strlen(text);

        format_repr_byte(piece, (uint32_t)sizeof(piece), bytes[i],
                         quote);
        (void)snprintf(&text[used], (size_t)cap - used, "%s", piece);
    }
    (void)snprintf(&text[strlen(text)], (size_t)cap - strlen(text),
                   "%c", quote);
}

/* The link is open: the green LED, and the card names the device. */
static bento_status_t show_device(const tls_screen_t *screen)
{
    uint8_t uid[BENTO_TESAIOT_IPC_RESP_MAX];
    char line[REPR_LINE_BYTES];
    uint32_t uid_length = 0U;
    bento_status_t status;

    (void)bento_ui_set_value(screen->ui, screen->led_tls, 1);
    status = bento_tesaiot_device_id(uid, (uint32_t)sizeof(uid),
                                     &uid_length);
    if (status == BENTO_OK) {
        static const char prefix[] = "ช่องเข้ารหัสพร้อม - ";

        (void)memcpy(line, prefix, sizeof(prefix));
        format_bytes_repr(&line[sizeof(prefix) - 1U],
                          (uint32_t)(sizeof(line) - sizeof(prefix)),
                          uid, uid_length);
        set_text(screen, screen->tls_line, line);
        (void)bento_ui_set_color(screen->ui, screen->tls_line,
                                 (uint32_t)COLOR_OK);
        poll_screen(screen->ui);
    }
    return status;
}

/* The link uses the settings stored by files 01 and 02 of this session. */
static bento_status_t open_link(const tls_screen_t *screen)
{
    bento_status_t status = bento_tesaiot_connect_start();

    if (status == BENTO_E_FAILED) {
        set_text(screen, screen->tls_line,
                 "ต่อไม่ได้ - รัน 01_config_store.py ก่อน");
        (void)bento_ui_set_color(screen->ui, screen->tls_line,
                                 (uint32_t)COLOR_BAD);
        poll_screen(screen->ui);
        status =
            BENTO_E_CANCELLED; /* the original's Stop: a quiet end */
    } else if (status == BENTO_OK) {
        status = show_device(screen);
    } else {
        /* no TESAIoT on this firmware: the status ends the program */
    }
    return status;
}

/* The SHT40 when the board has one, else the knob (0-100 % = 15-45 C, so a
 * class can turn it across a threshold); false when neither answered. */
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
            (void)bento_lcd_print("อุณหภูมิจาก SHT40 (เซนเซอร์จริงบนบอร์ด)");
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

/* Measure often: a new reading goes on the left number at once. */
static bento_status_t measure(const tls_screen_t *screen,
                              link_state_t *state)
{
    bento_sensors_snapshot_t snap;
    bento_real_t temp_c = (bento_real_t)0.0;
    bento_status_t status;

    bento_sensors_snapshot_init(&snap);
    status = bento_sensors_snapshot(BENTO_SENSORS_SNAPSHOT_DEFAULT_MS,
                                    &snap);
    if ((status == BENTO_OK) &&
        read_temp(&snap, &state->source, &temp_c)) {
        char digits[BENTO_FMT_BUF_SIZE];
        uint32_t length = 0U;

        state->last_value = temp_c;
        state->has_value = true;
        if (bento_fmt_real_precision(digits, sizeof(digits), temp_c, 1,
                                     &length) != BENTO_OK) {
            digits[0] = '\0'; /* BENTO_FMT_BUF_SIZE is always enough */
        }
        set_text(screen, screen->seg_now, digits);
    }
    return status;
}

/* Send seldom. publish(payload, topic): the payload comes first and must
 * be a JSON text; publish("temp_c", 25.3) would send "temp_c" to topic
 * 25.3. */
static void send_reading(const tls_screen_t *screen,
                         link_state_t *state)
{
    char rounded[BENTO_FMT_BUF_SIZE];
    char payload[TEXT_BYTES];
    char text[TEXT_BYTES];
    bento_status_t status;

    format_tenth(rounded, (uint32_t)sizeof(rounded), state->last_value);
    (void)snprintf(payload, sizeof(payload), "{\"temperature\": %s}",
                   rounded);
    status = bento_tesaiot_publish(NULL, 0U, (const uint8_t *)payload,
                                   (uint32_t)strlen(payload), 0U, NULL);
    if (status == BENTO_OK) {
        state->sent = state->sent + 1;
        (void)snprintf(text, sizeof(text), "%d", (int)state->sent);
        set_text(screen, screen->seg_sent, text);
        (void)snprintf(text, sizeof(text), "ส่งแล้ว %d ครั้ง | ล่าสุด %s C",
                       (int)state->sent, rounded);
        (void)bento_lcd_print(text);
    } else {
        /* the encrypted link dropped: the screen must say so, not stay quiet */
        state->fails = state->fails + 1;
        (void)bento_ui_set_value(screen->ui, screen->led_tls, 0);
        set_text(screen, screen->tls_line,
                 "ส่งไม่ออก: publish() คืน False");
        (void)bento_ui_set_color(screen->ui, screen->tls_line,
                                 (uint32_t)COLOR_BAD);
    }
}

/* One round: measure, move the countdown bar, send when it is full. */
static bento_status_t run_round(const tls_screen_t *screen,
                                link_state_t *state)
{
    bento_status_t status = measure(screen, state);

    if (status == BENTO_OK) {
        const uint32_t now_ms = bento_time_ticks_ms();
        const int32_t waited_ms =
            bento_time_ticks_diff(now_ms, state->last_send_ms);

        (void)bento_ui_set_value(screen->ui, screen->bar_next,
                                 (waited_ms < SEND_MS) ? waited_ms
                                                       : SEND_MS);
        if (state->has_value && (waited_ms >= SEND_MS)) {
            state->last_send_ms = now_ms;
            send_reading(screen, state);
        }
        poll_screen(screen->ui);
        status = wait_ms((uint32_t)LOOP_MS);
    }
    return status;
}

static bento_status_t run_rounds(const tls_screen_t *screen,
                                 link_state_t *state)
{
    int32_t i;
    bento_status_t status = BENTO_OK;

    state->last_send_ms = bento_time_ticks_ms();
    for (i = 0; (i < ROUNDS) && (status == BENTO_OK); i++) {
        status = run_round(screen, state);
    }
    return status;
}

/* Encryption protects the path, not whether the value is right. */
static void finish(const tls_screen_t *screen,
                   const link_state_t *state)
{
    char text[TEXT_BYTES];

    (void)bento_tesaiot_disconnect(); /* True or False, never a raise */
    (void)bento_ui_set_value(screen->ui, screen->led_tls, 0);
    (void)snprintf(text, sizeof(text), "จบ - ส่งสำเร็จ %d ครั้ง พลาด %d",
                   (int)state->sent, (int)state->fails);
    set_text(screen, screen->note, text);
    poll_screen(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>จบ - สำเร็จ %d | พลาด %d </span>",
                   (int)state->sent, (int)state->fails);
    (void)bento_lcd_print(text);
    (void)printf("การเข้ารหัสปกป้องเส้นทาง ไม่ได้ปกป้องความถูกต้องของค่า\n");
}

int main(void)
{
    tls_screen_t screen;
    link_state_t state;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(&state, 0, sizeof(state));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        poll_screen(screen.ui);
        /* lcd calls ignore a console that is not ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ค่าจริงผ่านช่องเข้ารหัส</h2>");
        status = open_link(&screen);
    }
    if (status == BENTO_OK) {
        status = run_rounds(&screen, &state);
    }
    if (status == BENTO_OK) {
        finish(&screen, &state);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
