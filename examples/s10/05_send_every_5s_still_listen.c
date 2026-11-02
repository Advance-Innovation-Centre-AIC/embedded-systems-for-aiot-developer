/*
 * 05_send_every_5s_still_listen.c -- ส่งทุก 5 วินาที แต่ยังรับคำสั่งได้ทุก 100 ms
 *
 * นาฬิกาสองเรือนในลูปเดียว: เลขซ้ายนับรอบลูปทุก 100 ms เลขขวานับ
 * "ถึงกำหนดส่ง" ทุก 5000 ms โดยไม่มี sleep ยาวที่ไหนเลย
 * เก็บเวลาที่ส่งครั้งล่าสุดไว้ แล้วถามด้วย ticks_diff ทุกรอบว่าครบ
 * หรือยัง แท่งกลางคือเวลาที่ผ่านไปตั้งแต่ใบล่าสุด ระหว่างรอ
 * กดปุ่มผู้ใช้บนบอร์ด (แทนคำสั่งที่ วิ่งเข้ามาจาก broker)
 * แล้วบอร์ดรู้ตัวทันที ทำงานราวหนึ่งนาที ไม่ต่อ WiFi ไม่ต่อ broker
 *
 * Two clocks in one loop: the left number counts loop rounds every 100
 * ms, the right one counts "time to send" every 5000 ms, and nothing
 * sleeps long. The time of the last send is kept, and every round asks
 * ticks_diff whether the period has passed. The middle bar is the time
 * since the last send. Meanwhile, press the board's user button
 * (standing in for a command from a broker) and the board notices at
 * once. Runs for about a minute; no WiFi, no broker.
 *
 * Twin of 05_send_every_5s_still_listen.py
 * (embedded-systems-for-aiot-developer/examples/s10/05_send_every_5s_still_listen.py),
 * merged from the library twin aiot-s10-05-send-every-5s-still-listen. Style: CLEAN_C.md.
 * The button's name comes from the firmware (btn.name()), not from the
 * circuit board.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* The course palette, 0xRRGGBB: one value per role. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    /* also the bar's track; the moving part has the theme colour */
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_INFO = 0x4A9EFF
};

enum {
    SEND_EVERY_MS = 5000, /* the sending period */
    LOOP_MS = 100,        /* the loop's period: much shorter */
    ROUNDS = 600,         /* 600 rounds of 100 ms: about a minute */
    SETTLE_MS = 200,      /* let the cleared screen settle */
    USER_BUTTON = 0,
    /* 148 bytes at most here, with a 16-byte button name */
    TEXT_BYTES = 160
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    const char *button_name;
    bento_ui_id_t loops_digits;
    bento_ui_id_t sent_digits;
    bento_ui_id_t waited_bar;
    bento_ui_id_t received_line;
    bento_ui_id_t note;
} clocks_screen_t;

/* The two clocks and the button, as the loop leaves them. */
typedef struct {
    int32_t loops;
    int32_t sent;
    int32_t received;
    uint32_t last_send_ms;
    uint32_t previous_button; /* the raw pin: 0 while pressed */
} clocks_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The console's opening lines. */
static void print_intro(const clocks_screen_t *screen)
{
    char text[TEXT_BYTES];

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>คาบ 10 - "
                            "นาฬิกาสองเรือนในลูปเดียว</h2>");
    (void)snprintf(text, sizeof(text), "ลูปทุก %d ms | ส่งทุก %d ms",
                   LOOP_MS, SEND_EVERY_MS);
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "กด %s ระหว่างรอ แล้วดูว่าบอร์ดรู้ตัวทันทีหรือไม่",
                   screen->button_name);
    (void)bento_lcd_print(text);
}

/* Two cards stacked as bands: 24 px outside, 16 px between them and
 * inside. */
enum {
    MARGIN_X = 24,
    INNER_X = 40,
    RIGHT_X = 400,
    TITLE_Y = 8,
    CARD_W = 744,
    CARD_H = 128,
    TOP_CARD_Y = 48,
    BOTTOM_CARD_Y = 192,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    COUNTER_CAPTION_Y = 64,
    COUNTER_Y = 96,
    COUNTER_W = 232,
    /* the box, not the digits: those are fixed at 28 px */
    COUNTER_H = 64,
    WAIT_CAPTION_Y = 208,
    BAR_Y = 244,
    BAR_W = 712,
    BAR_H = 24,
    RECEIVED_Y = 280,
    NOTE_Y = 336,
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
static bento_status_t add_card(bento_ui_t *ui, int16_t y)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM;
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* A caption and a seven-segment counter under it, starting at "0". */
static bento_status_t add_counter(bento_ui_t *ui, const char *caption,
                                  int16_t x, uint32_t color,
                                  bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(ui, caption, x, COUNTER_CAPTION_Y,
                                      COLOR_DIM, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "0"; /* a Seg7 takes a text, not a number */
        args.text_len = (uint32_t)strlen(args.text);
        args.x = x;
        args.y = COUNTER_Y;
        args.w = COUNTER_W;
        args.h = COUNTER_H;
        args.color = color;
        status = bento_ui_seg7_create(ui, &args, out_id);
    }
    return status;
}

/* The top card: the fast clock on the left, the slow one on the
 * right. */
static bento_status_t add_clocks(clocks_screen_t *screen)
{
    bento_status_t status = add_card(screen->ui, TOP_CARD_Y);

    if (status == BENTO_OK) {
        status = add_counter(screen->ui, "รอบลูป (ทุก 100 ms)", INNER_X,
                             COLOR_INFO, &screen->loops_digits);
    }
    if (status == BENTO_OK) {
        status = add_counter(screen->ui, "ส่งแล้ว (ทุก 5 วินาที)", RIGHT_X,
                             COLOR_OK, &screen->sent_digits);
    }
    return status;
}

/* The bottom card: the time since the last send, and the button
 * presses received. */
static bento_status_t add_waiting(clocks_screen_t *screen)
{
    char text[TEXT_BYTES];
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_card(screen->ui, BOTTOM_CARD_Y);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "เวลาที่ผ่านไปตั้งแต่ใบล่าสุด", INNER_X,
                           WAIT_CAPTION_Y, COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = INNER_X;
        args.y = BAR_Y;
        args.w = BAR_W;
        args.h = BAR_H;
        args.color = COLOR_CARD;
        args.min_val = 0;
        args.max_val = SEND_EVERY_MS;
        args.font_px = 0; /* the bar starts empty */
        status =
            bento_ui_bar_create(screen->ui, &args, &screen->waited_bar);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "กด %s ระหว่างรอ - รับได้: 0 ครั้ง",
                       screen->button_name);
        status = add_label(screen->ui, text, INNER_X, RECEIVED_Y,
                           COLOR_TEXT, &screen->received_line);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(clocks_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    /* 110 bytes: under the Label's 126, so one label */
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "ส่งทุก 5 วินาที แต่ยังรับคำสั่งได้ทุก 100 ms";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = MARGIN_X;
        args.y = TITLE_Y;
        args.color = COLOR_TEXT;
        args.font_px = TITLE_PX;
        status = bento_ui_label_create(screen->ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        status = add_clocks(screen);
    }
    if (status == BENTO_OK) {
        status = add_waiting(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "ลูปทุก 100 ms  ส่งทุก 5000 ms  ไม่มี "
                           "sleep ยาว",
                           MARGIN_X, NOTE_Y, COLOR_DIM, &screen->note);
    }
    return status;
}

/* The end: the totals on the screen, on the console and on the
 * serial line. */
static void show_totals(const clocks_screen_t *screen,
                        const clocks_t *clocks)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "จบ - ลูป %" PRId32 " รอบ ส่ง %" PRId32 " ใบ",
                   clocks->loops, clocks->sent);
    (void)bento_ui_set_text(screen->ui, screen->note, text,
                            (uint32_t)strlen(text));
    drain_events(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "จบ - ลูป %" PRId32 " รอบ | ถึงกำหนดส่ง %" PRId32
                   " ใบ | รับ %" PRId32,
                   clocks->loops, clocks->sent, clocks->received);
    (void)bento_lcd_print(text);
    (void)printf("ลูปเดินไป %" PRId32 " รอบ แต่ถึงกำหนดส่งแค่ %" PRId32
                 " ครั้ง\n",
                 clocks->loops, clocks->sent);
    (void)printf("อัตราส่วนนี้คือ SEND_EVERY_MS / LOOP_MS - "
                 "ลองแก้สองค่านั้นแล้วรันซ้ำ\n");
}

/* The slow clock: time to send. A real program calls mqtt.publish()
 * here (examples/s10/03_connect_and_publish.py). Returns the time
 * waited, 0 right after a send. */
static int32_t check_send(const clocks_screen_t *screen,
                          clocks_t *clocks, uint32_t now_ms)
{
    int32_t waited_ms =
        bento_time_ticks_diff(now_ms, clocks->last_send_ms);

    if (waited_ms >= SEND_EVERY_MS) {
        char text[TEXT_BYTES];

        clocks->sent++;
        (void)snprintf(text, sizeof(text), "%" PRId32, clocks->sent);
        (void)bento_ui_set_text(screen->ui, screen->sent_digits, text,
                                (uint32_t)strlen(text));
        (void)snprintf(text, sizeof(text),
                       "ถึงกำหนดใบที่ %" PRId32 " | ลูปไปแล้ว %" PRId32
                       " รอบ",
                       clocks->sent, clocks->loops);
        (void)bento_lcd_print(text);
        clocks->last_send_ms = now_ms;
        waited_ms = 0;
    }
    return waited_ms;
}

/* The button stands in for a command arriving from the broker: a loop
 * that slept would miss it. A falling edge is a press (this board's
 * button reads 0 while pressed). */
static bento_status_t check_button(const clocks_screen_t *screen,
                                   clocks_t *clocks)
{
    uint32_t level = 1U;
    bento_status_t status =
        bento_gpio_button_value(USER_BUTTON, &level);

    if ((status == BENTO_OK) && (level == 0U) &&
        (clocks->previous_button == 1U)) {
        char text[TEXT_BYTES];

        clocks->received++;
        (void)bento_ui_set_color(screen->ui, screen->received_line,
                                 COLOR_INFO);
        (void)snprintf(text, sizeof(text),
                       "กด %s ระหว่างรอ - รับได้: %" PRId32 " ครั้ง",
                       screen->button_name, clocks->received);
        (void)bento_ui_set_text(screen->ui, screen->received_line, text,
                                (uint32_t)strlen(text));
        (void)snprintf(text, sizeof(text),
                       "รับคำสั่งระหว่างรอบที่ %" PRId32 " - ครั้งที่ "
                       "%" PRId32,
                       clocks->loops, clocks->received);
        (void)bento_lcd_print(text);
    }
    clocks->previous_button = level;
    return status;
}

/* ROUNDS rounds of LOOP_MS. The left number and the bar move every
 * round, the right number every five seconds: that difference is the
 * lesson. MicroPython's widget setters and lcd.print() ignore a refusal
 * (modui.c, modlcd.c), so the twin does too. */
static bento_status_t run_clocks(const clocks_screen_t *screen,
                                 clocks_t *clocks)
{
    bento_status_t status = BENTO_OK;
    int32_t round;

    clocks->last_send_ms = bento_time_ticks_ms();
    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        char text[TEXT_BYTES];
        int32_t waited_ms;

        clocks->loops++;
        waited_ms = check_send(screen, clocks, bento_time_ticks_ms());
        status = check_button(screen, clocks);
        if (status == BENTO_OK) {
            (void)snprintf(text, sizeof(text), "%" PRId32,
                           clocks->loops);
            (void)bento_ui_set_text(screen->ui, screen->loops_digits,
                                    text, (uint32_t)strlen(text));
            (void)bento_ui_set_value(screen->ui, screen->waited_bar,
                                     waited_ms);
            drain_events(screen->ui);
            status = wait_ms(LOOP_MS);
        }
    }
    return status;
}

int main(void)
{
    clocks_screen_t screen = {
        bento_runtime_get_ui(), "", 0U, 0U, 0U, 0U, 0U};
    clocks_t clocks = {0, 0, 0, 0U, 1U};
    /* gpio.button(0) refuses a button the board does not have */
    bento_status_t status =
        (bento_gpio_num_buttons() > (uint32_t)USER_BUTTON)
            ? BENTO_OK
            : BENTO_E_INVALID_ARGUMENT;

    if (status == BENTO_OK) {
        status =
            bento_gpio_button_name(USER_BUTTON, &screen.button_name);
    }
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    if (status == BENTO_OK) {
        drain_events(screen.ui);
        print_intro(&screen);
        status = bento_gpio_button_value(USER_BUTTON,
                                         &clocks.previous_button);
    }
    if (status == BENTO_OK) {
        status = run_clocks(&screen, &clocks);
    }
    if (status == BENTO_OK) {
        show_totals(&screen, &clocks);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
