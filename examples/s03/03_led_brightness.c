/*
 * 03_led_brightness.c -- หรี่ไฟให้ค้างนานพอจะมองเห็น (Hold a dimmed LED
 * long enough to see it)
 *
 * ทีมหนึ่งสั่งหรี่ไฟทีละระดับแล้วไม่เห็นความต่าง จึงสรุปว่าหลอดเสีย ที่จริงคำสั่ง
 * ถูกทุกครั้ง แต่บนดวงนั้นมันจบเร็วเกินกว่าตาจะทัน brightness(pct) ค้างได้เฉพาะ
 * ดวงที่มีเส้น PWM ของฮาร์ดแวร์ ท่าที่ค้างได้แน่ทุกดวงคือ hold(pct, ms)
 * ตัวเลขบนจอคือเปอร์เซ็นต์ที่สั่งค้างอยู่ ให้มองหลอดจริงสลับระหว่าง 10 กับ 90
 * กับดัก: duty() คืนเปอร์เซ็นต์ที่สั่งล่าสุด ไม่ได้วัดจากหลอด และ toggle()
 * ไม่ได้แก้ตัวเลขนั้นเลย
 *
 * A team dimmed an LED step by step, saw no difference and decided it
 * was broken. Every command was right; on that LED it just ended too
 * fast for the eye. brightness(pct) holds a level only on an LED with
 * a hardware PWM line (the RGB LED of both boards); hold(pct, ms)
 * holds it on every LED and always ends dark. The number is the
 * percent held now: watch the real LED switch between 10 and 90.
 * Trap: duty() answers the last percent asked for, not a measurement,
 * and toggle() does not change it at all.
 *
 * Twin of 03_led_brightness.py
 * (embedded-systems-for-aiot-developer/examples/s03/03_led_brightness.py).
 * Style: CLEAN_C.md.
 * Where C differs: an LED is its index, not an object; lcd.print(a,
 * b, ...) is one snprintf() line with print's single spaces.
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
    LOW_PCT = 10, /* two levels far enough apart for the eye */
    HIGH_PCT = 90,
    ONCE_PCT = 40, /* the single brightness() of move 1 */
    LEVELS = 2,
    ROUNDS = 3, /* times the two levels are switched */
    TEXT_BYTES = 128
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    HOLD_MS = 700,   /* each level is held this long */
    LOOK_MS = 1500,  /* time to look at the LED after move 1 */
    GAP_MS = 150,    /* hold() ends dark: the LED is really off here */
    ON_LOOK_MS = 600,
    END_LOOK_MS = 1200
};

/* Layout on the 792 x 398 canvas. */
enum {
    LEFT_X = 20,
    TITLE_Y = 12,
    TITLE_FONT_PX = 24,
    FONT_PX = 20,
    SEG_Y = 60,
    SEG_W = 152,
    SEG_H = 64,
    UNIT_X = 192,
    UNIT_Y = 84,
    WHAT_Y = 144,
    BACK_Y = 184,
    NOTE_Y = 232
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_WARN = 0xF5A623,
    COLOR_INFO = 0x4A9EFF
};

typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} text_style_t;

/* The widgets the moves write over, and the LED they drive. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t seg;
    bento_ui_id_t what;
    bento_ui_id_t back;
    bento_ui_id_t note;
    uint32_t led;
    const char *led_name;
} dim_t;

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
static void set_text(const dim_t *dim, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(dim->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void set_number(const dim_t *dim, bento_ui_id_t widget,
                       int32_t number)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, number);
    set_text(dim, widget, text);
}

/* The LED is chosen by the name board_info() reports: the RGB LED is
 * visible on an assembled board and has a hardware PWM line on both
 * boards; without one, the first LED. (On the Eva Kit RGB_RED is
 * really blue; only "visible" matters here.) */
static bento_status_t choose_led(dim_t *dim)
{
    const char rgb_prefix[] = "RGB_";
    const uint32_t count = bento_gpio_num_leds();
    bento_status_t status = BENTO_OK;
    bool found = false;
    uint32_t i;

    dim->led = 0U;
    for (i = 0U; (i < count) && (status == BENTO_OK) && !found; i++) {
        const char *name = NULL;

        status = bento_gpio_led_name(i, &name);
        if ((status == BENTO_OK) &&
            (strncmp(name, rgb_prefix, strlen(rgb_prefix)) == 0)) {
            dim->led = i;
            found = true;
        }
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_name(dim->led, &dim->led_name);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(dim->led);
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

static bento_status_t add_seg(dim_t *dim)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0";
    args.text_len = 1U;
    args.x = LEFT_X;
    args.y = SEG_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = COLOR_WARN;
    return bento_ui_seg7_create(dim->ui, &args, &dim->seg);
}

static bento_status_t add_lines(dim_t *dim)
{
    const text_style_t what = {LEFT_X, WHAT_Y, COLOR_INFO, FONT_PX};
    const text_style_t back = {LEFT_X, BACK_Y, COLOR_DIM, FONT_PX};
    const text_style_t note = {LEFT_X, NOTE_Y, COLOR_DIM, FONT_PX};
    char text[TEXT_BYTES];
    bento_status_t status =
        add_label(dim->ui, "กำลังจะเริ่ม", &what, &dim->what);

    if (status == BENTO_OK) {
        status =
            add_label(dim->ui, "ยังไม่ได้อ่าน duty()", &back, &dim->back);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "มองหลอด %s บนบอร์ด ไม่ใช่มองแต่จอ", dim->led_name);
        status = add_label(dim->ui, text, &note, &dim->note);
    }
    return status;
}

static bento_status_t build_screen(dim_t *dim)
{
    const text_style_t title = {LEFT_X, TITLE_Y, COLOR_TEXT,
                                TITLE_FONT_PX};
    const text_style_t unit = {UNIT_X, UNIT_Y, COLOR_DIM, FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(dim->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(dim->ui, "หรี่ไฟให้ค้างนานพอจะมองเห็น", &title,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg(dim);
    }
    if (status == BENTO_OK) {
        status = add_label(dim->ui, "% ที่สั่งค้างอยู่", &unit, &unused);
    }
    if (status == BENTO_OK) {
        status = add_lines(dim);
    }
    return status;
}

/* Move 1: one command, then look up: the board answers with the LED,
 * not with text. The label is written before the command, which may
 * block until it is done. off() afterwards gives the pin back from
 * PWM to plain GPIO, where hold() works. */
static bento_status_t move_once(const dim_t *dim)
{
    char text[TEXT_BYTES];
    uint32_t duty = 0U;
    bento_status_t status;

    set_text(dim, dim->what,
             "brightness(40) ครั้งเดียว - มองหลอด ค้างหรือวูบ");
    set_number(dim, dim->seg, ONCE_PCT);
    poll_once(dim->ui);
    status = bento_gpio_led_brightness(dim->led, ONCE_PCT);
    if (status == BENTO_OK) {
        status = bento_gpio_led_duty(dim->led, &duty);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "duty() = %" PRIu32 " - ตัวเลขไม่บอกว่าค้างหรือดับ",
                       duty);
        set_text(dim, dim->back, text);
        (void)snprintf(text, sizeof(text),
                       "brightness(40) ดวง %s - ค้างไหม ดูที่หลอด",
                       dim->led_name);
        (void)bento_lcd_print(text);
        status = sleep_checked(LOOK_MS);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(dim->led);
    }
    return status;
}

/* One level held for HOLD_MS, timed around the call to prove it held
 * the whole time. hold() ends dark, so the short gap after it is the
 * LED really off. */
static bento_status_t hold_level(const dim_t *dim, int32_t pct)
{
    char text[TEXT_BYTES];
    uint32_t duty = 0U;
    uint32_t start_ms;
    int32_t used_ms;
    bento_status_t status;

    set_number(dim, dim->seg, pct);
    (void)snprintf(text, sizeof(text),
                   "hold(%" PRId32 ", %d) กำลังค้างอยู่", pct, HOLD_MS);
    set_text(dim, dim->what, text);
    poll_once(dim->ui);
    start_ms = bento_time_ticks_ms();
    status = bento_gpio_led_hold(dim->led, pct, HOLD_MS);
    used_ms = bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_duty(dim->led, &duty);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "duty() = %" PRIu32 " ใช้เวลาจริง %" PRId32 " ms",
                       duty, used_ms);
        set_text(dim, dim->back, text);
        (void)snprintf(text, sizeof(text),
                       "hold( %" PRId32 " , %d ) ใช้เวลา %" PRId32 " ms",
                       pct, HOLD_MS, used_ms);
        (void)bento_lcd_print(text);
        poll_once(dim->ui);
        status = sleep_checked(GAP_MS);
    }
    return status;
}

/* Move 2: hold, switching the two levels. */
static bento_status_t move_hold(const dim_t *dim)
{
    static const int32_t levels[LEVELS] = {LOW_PCT, HIGH_PCT};
    bento_status_t status = BENTO_OK;
    int32_t round;
    int32_t i;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        for (i = 0; (i < LEVELS) && (status == BENTO_OK); i++) {
            status = hold_level(dim, levels[i]);
        }
    }
    return status;
}

/* Move 3: duty() answers "what was asked last", not "what the LED is
 * now". on(), off() and value(n) keep it in step with the pin, but
 * toggle() does not touch it: the one place they disagree, with no
 * error to catch. */
static bento_status_t move_toggle(const dim_t *dim)
{
    char text[TEXT_BYTES];
    uint32_t duty_on = 0U;
    uint32_t duty_after = 0U;
    bento_status_t status;

    set_text(dim, dim->what,
             "ท่าที่ 3 - on() แล้ว toggle() ดูตัวเลขของ duty()");
    poll_once(dim->ui);
    status = bento_gpio_led_on(dim->led);
    if (status == BENTO_OK) {
        status = bento_gpio_led_duty(dim->led, &duty_on);
    }
    if (status == BENTO_OK) {
        status = sleep_checked(ON_LOOK_MS);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_toggle(dim->led);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_duty(dim->led, &duty_after);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "on() duty=%" PRIu32 "  toggle() duty=%" PRIu32,
                       duty_on, duty_after);
        set_text(dim, dim->back, text);
        (void)snprintf(text, sizeof(text),
                       "toggle() ดับหลอด แต่ duty() ยังตอบ %" PRIu32,
                       duty_after);
        (void)bento_lcd_print(text);
        (void)bento_lcd_print(
            "<span class=muted>duty() = สิ่งที่สั่ง ไม่ใช่สิ่งที่วัด</span>");
        poll_once(dim->ui);
        status = sleep_checked(END_LOOK_MS);
    }
    return status;
}

/* Nine times the value is not nine times as bright to the eye: the
 * eye does not answer in a straight line. */
static bento_status_t finish(const dim_t *dim)
{
    const bento_status_t status = bento_gpio_led_off(dim->led);

    if (status == BENTO_OK) {
        set_text(dim, dim->what, "จบแล้ว - เทียบ 10 กับ 90 ที่เพิ่งเห็นเมื่อกี้");
        set_text(dim, dim->note, "ส่งห่างกัน 9 เท่า ตาไม่ได้เห็นต่างกัน 9 เท่า");
        (void)bento_lcd_print(
            "<span class=ok>ระดับจะเห็นได้ ต่อเมื่อสั่งค้างไว้</span>");
        poll_once(dim->ui);
    }
    return status;
}

int main(void)
{
    dim_t dim;
    bento_status_t status;

    (void)memset(&dim, 0, sizeof(dim));
    dim.ui = bento_runtime_get_ui();
    status = choose_led(&dim);
    if (status == BENTO_OK) {
        status = build_screen(&dim);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>หรี่ไฟให้ค้างนานพอจะมองเห็น</h2>");
        status = move_once(&dim);
    }
    if (status == BENTO_OK) {
        status = move_hold(&dim);
    }
    if (status == BENTO_OK) {
        status = move_toggle(&dim);
    }
    if (status == BENTO_OK) {
        status = finish(&dim);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
