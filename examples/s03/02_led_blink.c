/*
 * 02_led_blink.c -- ไฟกะพริบและตัวนับรอบ (A blinking LED and a round
 * counter)
 *
 * ทำให้ไฟกะพริบเป็นจังหวะ แล้วนับรอบที่กะพริบไปแล้ว กะพริบหนึ่งรอบคือ สั่งติด
 * รอ สั่งดับ รอ เวลาที่รอคือจังหวะ on() กับ off() สั่งค่าตรง ๆ ส่วน toggle()
 * สั่งกลับด้านจากค่าเดิม หลอดที่เลือกกะพริบทุก 250 ms ป้ายบนจอเขียนว่า ติด
 * หรือ ดับ ตรงกับหลอดจริง และตัวเลขนับรอบเดินขึ้นทีละหนึ่ง
 * กับดัก: จบลูปด้วย toggle() จำนวนคี่ ไฟจะค้างติด จึงต้องปิดท้ายด้วย off()
 *
 * Blink an LED in time and count the rounds. One blink is: on, wait,
 * off, wait; the waiting is the rhythm. on() and off() set the LED
 * outright; toggle() flips it from whatever it was. The chosen LED
 * blinks every 250 ms, the label says on or off in step with the real
 * LED, and the counter goes up by one. Trap: end on an odd number of
 * toggle() calls and the LED stays on, so the program ends with off().
 *
 * Twin of 02_led_blink.py
 * (embedded-systems-for-aiot-developer/examples/s03/02_led_blink.py).
 * Style: CLEAN_C.md.
 * Where C differs: an LED is its index, not an object; the names
 * board_info() lists are asked per index.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

enum {
    ROUNDS = 6, /* blinks in one set */
    TEXT_BYTES = 128
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    ON_MS = 250,     /* how long the LED stays on */
    OFF_MS = 250     /* how long it stays off */
};

/* Layout on the 792 x 398 canvas. */
enum {
    LEFT_X = 20,
    TITLE_Y = 12,
    TITLE_FONT_PX = 24,
    STATE_Y = 64,
    STATE_FONT_PX = 28,
    SEG_X = 200,
    SEG_Y = 56,
    SEG_W = 140,
    SEG_H = 56,
    HOW_X = 372,
    HOW_Y = 72,
    FONT_PX = 20,
    NOTE_Y = 152
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_INFO = 0x4A9EFF
};

typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} text_style_t;

/* The widgets the blinking writes over, and the LED it drives. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t state;
    bento_ui_id_t seg;
    bento_ui_id_t how;
    bento_ui_id_t note;
    uint32_t led;
    const char *led_name;
    int32_t rounds;
} blink_t;

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static void poll_once(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* ui.poll() answers no events when it fails; none are read */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The setters ignore a refusal in MicroPython (modui.c). */
static void set_text(const blink_t *blink, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(blink->ui, widget, text,
                            (uint32_t)strlen(text));
}

/* The LED is chosen by the name board_info() reports, not a
 * remembered number: on a board in its base LED1/LED2 may be hidden,
 * so an RGB_ LED is used when there is one, else the first. (On the
 * Eva Kit RGB_RED is really blue; only "visible" matters here.) */
static bento_status_t choose_led(blink_t *blink)
{
    const char rgb_prefix[] = "RGB_";
    const uint32_t count = bento_gpio_num_leds();
    bento_status_t status = BENTO_OK;
    bool found = false;
    uint32_t i;

    blink->led = 0U;
    for (i = 0U; (i < count) && (status == BENTO_OK) && !found; i++) {
        const char *name = NULL;

        status = bento_gpio_led_name(i, &name);
        if ((status == BENTO_OK) &&
            (strncmp(name, rgb_prefix, strlen(rgb_prefix)) == 0)) {
            blink->led = i;
            found = true;
        }
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_name(blink->led, &blink->led_name);
    }
    /* start from a state known for sure */
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(blink->led);
    }
    return status;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const text_style_t *style,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = style->x;
    args.y = style->y;
    args.color = style->color;
    args.font_px = style->font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_seg(blink_t *blink)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0";
    args.text_len = 1U;
    args.x = SEG_X;
    args.y = SEG_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = COLOR_OK;
    return bento_ui_seg7_create(blink->ui, &args, &blink->seg);
}

static bento_status_t build_screen(blink_t *blink)
{
    const text_style_t title = {LEFT_X, TITLE_Y, COLOR_TEXT,
                                TITLE_FONT_PX};
    const text_style_t state = {LEFT_X, STATE_Y, COLOR_DIM,
                                STATE_FONT_PX};
    const text_style_t how = {HOW_X, HOW_Y, COLOR_INFO, FONT_PX};
    const text_style_t note = {LEFT_X, NOTE_Y, COLOR_DIM, FONT_PX};
    char text[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(blink->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status =
            add_label(blink->ui, "ไฟกะพริบและตัวนับรอบ", &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(blink->ui, "ดับ", &state, &blink->state);
    }
    if (status == BENTO_OK) {
        status = add_seg(blink);
    }
    if (status == BENTO_OK) {
        status = add_label(blink->ui, "led.on() / led.off()", &how,
                           &blink->how);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "มองหลอด %s บนบอร์ดคู่กับป้ายบนจอ", blink->led_name);
        status = add_label(blink->ui, text, &note, &blink->note);
    }
    return status;
}

/* The label follows the LED: on in orange, off in grey; each off ends
 * a round and the counter goes up. */
static void show_state(blink_t *blink, bool on)
{
    char text[TEXT_BYTES];

    set_text(blink, blink->state, on ? "ติด" : "ดับ");
    (void)bento_ui_set_color(blink->ui, blink->state,
                             on ? COLOR_WARN : COLOR_DIM);
    if (!on) {
        blink->rounds++;
        (void)snprintf(text, sizeof(text), "%" PRId32, blink->rounds);
        set_text(blink, blink->seg, text);
    }
}

/* Set 1: on() and off() set the value outright; one line of code
 * says what the LED will be. */
static bento_status_t blink_on_off(blink_t *blink)
{
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    int32_t round;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = bento_gpio_led_on(blink->led);
        if (status == BENTO_OK) {
            set_text(blink, blink->state, "ติด");
            (void)bento_ui_set_color(blink->ui, blink->state,
                                     COLOR_WARN);
            poll_once(blink->ui);
            status = sleep_checked(ON_MS);
        }
        if (status == BENTO_OK) {
            status = bento_gpio_led_off(blink->led);
        }
        if (status == BENTO_OK) {
            show_state(blink, false);
            poll_once(blink->ui);
            status = sleep_checked(OFF_MS);
        }
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>on/off ครบ %" PRId32
                       " รอบ</span>",
                       blink->rounds);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* Set 2: toggle() flips the LED from what it was, the same rhythm in
 * one call. The price: the program must remember the state itself,
 * or the label stops matching the LED. */
static bento_status_t blink_toggle(blink_t *blink)
{
    const int32_t flips = ROUNDS * 2;
    bento_status_t status = BENTO_OK;
    bool on = false;
    int32_t flip;

    set_text(blink, blink->how, "led.toggle()");
    for (flip = 0; (flip < flips) && (status == BENTO_OK); flip++) {
        status = bento_gpio_led_toggle(blink->led);
        if (status == BENTO_OK) {
            on = !on;
            show_state(blink, on);
            poll_once(blink->ui);
            status = sleep_checked(ON_MS);
        }
    }
    return status;
}

/* End on a known value, not on the luck of an even toggle count. */
static bento_status_t finish(blink_t *blink)
{
    char text[TEXT_BYTES];
    const bento_status_t status = bento_gpio_led_off(blink->led);

    if (status == BENTO_OK) {
        set_text(blink, blink->state, "ดับ");
        (void)bento_ui_set_color(blink->ui, blink->state, COLOR_DIM);
        (void)snprintf(text, sizeof(text),
                       "จบแล้ว กะพริบไป %" PRId32 " รอบ ปิดท้ายด้วย off()",
                       blink->rounds);
        set_text(blink, blink->note, text);
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>รวม %" PRId32
                       " รอบ - จบด้วย off()</span>",
                       blink->rounds);
        (void)bento_lcd_print(text);
        poll_once(blink->ui);
    }
    return status;
}

int main(void)
{
    char text[TEXT_BYTES];
    blink_t blink;
    bento_status_t status;

    (void)memset(&blink, 0, sizeof(blink));
    blink.ui = bento_runtime_get_ui();
    status = choose_led(&blink);
    if (status == BENTO_OK) {
        status = build_screen(&blink);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ไฟกะพริบและตัวนับรอบ</h2>");
        (void)snprintf(text, sizeof(text), "หลอด %s | รอบละ %d ms",
                       blink.led_name, ON_MS + OFF_MS);
        (void)bento_lcd_print(text);
        status = blink_on_off(&blink);
    }
    if (status == BENTO_OK) {
        status = blink_toggle(&blink);
    }
    if (status == BENTO_OK) {
        status = finish(&blink);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
