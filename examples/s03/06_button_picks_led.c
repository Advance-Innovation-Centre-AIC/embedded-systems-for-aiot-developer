/*
 * 06_button_picks_led.c -- ปุ่มเดียว ไฟทุกดวง (One button picks every
 * LED)
 *
 * รีโมทแอร์มีปุ่มไม่กี่ปุ่มแต่คุมได้หลายสถานะ เพราะปุ่มแปลว่า "เลื่อนไป
 * สถานะถัดไป" บอร์ดเปิดให้ใช้ปุ่มเดียวแต่มีไฟหลายดวง ทางออกคือตัวแปร current
 * ที่บอกว่าดวงไหนติด และปุ่มบวกหนึ่งแล้ววนกลับ การ์ดบนบอกดัชนีกับชื่อดวงที่ติด
 * ขวามือหนึ่งแถวต่อหลอด ดวงที่ติดขึ้นเขียว ตรงกับหลอดจริงทุกครั้งที่กด
 * กับดัก: value() ตอบระดับของขา ณ ตอนที่ถาม hold() จบด้วยขาต่ำ อ่านตามหลัง
 * จึงได้ 0 ทั้งที่เพิ่งเห็นหลอดสว่าง
 *
 * An air-conditioner remote has a few buttons for many states: a
 * button means "move to the next state". The board gives Python one
 * button and several LEDs, so the variable `current` remembers which
 * LED is on, and the button adds one and wraps round. The card shows
 * the lit LED's index and name, with one row per LED on the right;
 * the lit one is green, in step with the real LED on every press.
 * Trap: value() reads the pin at the moment it is asked; hold() ends
 * low, so reading after it gives 0 although the LED was just lit.
 *
 * Twin of 06_button_picks_led.py
 * (embedded-systems-for-aiot-developer/examples/s03/06_button_picks_led.py).
 * Style: CLEAN_C.md.
 * Where C differs: the rows are an array sized for the most LEDs a
 * board may declare (BENTO_GPIO_LED_CAP, 8); a board with more ends
 * the program with BENTO_E_NO_RESOURCE. An LED is its index.
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
    LED_MAX = BENTO_GPIO_LED_CAP,
    NO_LED = -1, /* no LED is on yet */
    TEXT_BYTES = 128
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    DEBOUNCE_MS = 40,
    POLL_MS = 5,
    RUN_MS = 20000
};

/* Layout, worked out from the LED count, not from a remembered
 * number: one row per LED, 32 high (20 px text), the first at y=64
 * (card 52 + 12 inside), so the card is at least 24 + n x 32 high,
 * and the text under it starts 16 below the card. */
enum {
    LEFT_X = 20,
    TITLE_Y = 12,
    TITLE_FONT_PX = 24,
    FONT_PX = 20,
    SMALL_FONT_PX = 16, /* Latin only; Thai needs 20 */
    CARD_Y = 52,
    CARD_W = 652,
    CARD_MIN_H = 124,
    CARD_PAD = 24,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    HINT_GAP = 16,
    ROW_H = 32,
    ROW_X = 400,
    FIRST_ROW_Y = 64,
    CAPTION_X = 40,
    CAPTION_Y = 62,
    SEG_Y = 92,
    SEG_W = 64,
    SEG_H = 44,
    NAME_X = 112,
    NAME_Y = 96,
    NAME_FONT_PX = 24,
    FORMULA_DROP = 38, /* lines the Latin 16 px text up with the Thai */
    WRAP_X = 328,
    WRAP_DROP = 32,
    NOTE_DROP = 64,
    NOTE_RIGHT_X = 360,
    STATUS_DROP = 100,
    STATUS_MIN_Y = 336
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
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

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t seg;
    bento_ui_id_t name;
    bento_ui_id_t status;
    bento_ui_id_t rows[LED_MAX];
    const char *names[LED_MAX];
    int32_t count;
    int32_t current; /* the program's truth, not the hardware's */
    int32_t presses;
} picker_t;

/* The debounced button. */
typedef struct {
    bool stable;
    bool last_raw;
    uint32_t last_change_ms;
} button_t;

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
static void show(const picker_t *picker, bento_ui_id_t widget,
                 const char *text, uint32_t color)
{
    (void)bento_ui_set_text(picker->ui, widget, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_color(picker->ui, widget, color);
}

static int16_t px(int32_t value)
{
    return (int16_t)value;
}

/* The LED count and names, then every LED off so the real state
 * matches the variable about to be set. */
static bento_status_t read_leds(picker_t *picker)
{
    const uint32_t count = bento_gpio_num_leds();
    bento_status_t status =
        (count <= (uint32_t)LED_MAX) ? BENTO_OK : BENTO_E_NO_RESOURCE;
    uint32_t i;

    picker->count = (int32_t)count;
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = bento_gpio_led_name(i, &picker->names[i]);
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = bento_gpio_led_off(i);
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

static int32_t card_h(const picker_t *picker)
{
    const int32_t needed = CARD_PAD + (picker->count * ROW_H);

    return (needed > CARD_MIN_H) ? needed : CARD_MIN_H;
}

static int32_t hint_y(const picker_t *picker)
{
    return CARD_Y + card_h(picker) + HINT_GAP;
}

static bento_status_t add_card(const picker_t *picker)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = LEFT_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = px(card_h(picker));
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border colour */
    args.max_val = CARD_RADIUS; /* and max= its corner radius */
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(picker->ui, &args, &card);
}

static bento_status_t add_seg(picker_t *picker)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "-";
    args.text_len = 1U;
    args.x = CAPTION_X;
    args.y = SEG_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = COLOR_DIM;
    return bento_ui_seg7_create(picker->ui, &args, &picker->seg);
}

/* One row per LED: "NAME   ดับ". */
static bento_status_t add_rows(picker_t *picker)
{
    text_style_t row = {ROW_X, FIRST_ROW_Y, COLOR_DIM, FONT_PX};
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < picker->count) && (status == BENTO_OK); i++) {
        row.y = px(FIRST_ROW_Y + (i * ROW_H));
        (void)snprintf(text, sizeof(text), "%s   ดับ", picker->names[i]);
        status = add_label(picker->ui, text, &row, &picker->rows[i]);
    }
    return status;
}

static bento_status_t build_card(picker_t *picker)
{
    const text_style_t title = {LEFT_X, TITLE_Y, COLOR_TEXT,
                                TITLE_FONT_PX};
    const text_style_t caption = {CAPTION_X, CAPTION_Y, COLOR_DIM,
                                  FONT_PX};
    const text_style_t name = {NAME_X, NAME_Y, COLOR_DIM, NAME_FONT_PX};
    char text[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "ปุ่มเดียว ไฟ %" PRId32 " ดวง",
                   picker->count);
    status = add_label(picker->ui, text, &title, &unused);
    if (status == BENTO_OK) {
        status = add_card(picker);
    }
    if (status == BENTO_OK) {
        status = add_label(picker->ui, "ดัชนีที่ติดอยู่", &caption, &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg(picker);
    }
    if (status == BENTO_OK) {
        status = add_label(picker->ui, "ยังไม่มีดวงไหนติด", &name,
                           &picker->name);
    }
    if (status == BENTO_OK) {
        status = add_rows(picker);
    }
    return status;
}

/* The two-part notes under the card: a label cuts at 126 bytes and a
 * Thai character takes 3, so each note is two labels. */
static bento_status_t build_notes(bento_ui_t *ui, int32_t top_y,
                                  const char *button_name)
{
    const text_style_t formula = {LEFT_X, px(top_y + FORMULA_DROP),
                                  COLOR_DIM, SMALL_FONT_PX};
    const text_style_t wrap = {WRAP_X, px(top_y + WRAP_DROP), COLOR_DIM,
                               FONT_PX};
    const text_style_t note = {LEFT_X, px(top_y + NOTE_DROP),
                               COLOR_WARN, FONT_PX};
    const text_style_t note_right = {
        NOTE_RIGHT_X, px(top_y + NOTE_DROP), COLOR_WARN, FONT_PX};
    const text_style_t hint = {LEFT_X, px(top_y), COLOR_DIM, FONT_PX};
    char text[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "กดปุ่ม %s เพื่อเลื่อนดวงถัดไป",
                   button_name);
    status = add_label(ui, text, &hint, &unused);
    if (status == BENTO_OK) {
        status = add_label(ui, "current = (current + 1) % N", &formula,
                           &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "ทำให้ดวงสุดท้ายวนกลับดวงแรกเอง", &wrap, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "ทุกอย่างบนจอนี้อ่านจาก current", &note, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ไม่มีบรรทัดไหนถามหลอดเลย", &note_right,
                           &unused);
    }
    return status;
}

static bento_status_t build_screen(picker_t *picker)
{
    const int32_t top_y = hint_y(picker);
    const int32_t status_y = ((top_y + STATUS_DROP) > STATUS_MIN_Y)
                                 ? (top_y + STATUS_DROP)
                                 : STATUS_MIN_Y;
    const text_style_t status_style = {LEFT_X, px(status_y), COLOR_INFO,
                                       FONT_PX};
    const char *button_name = NULL;
    bento_status_t status =
        bento_ui_screen(picker->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = build_card(picker);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_button_name(0U, &button_name);
    }
    if (status == BENTO_OK) {
        status = build_notes(picker->ui, top_y, button_name);
    }
    if (status == BENTO_OK) {
        status = add_label(picker->ui, "กดปุ่มเพื่อเลื่อนไปดวงถัดไป",
                           &status_style, &picker->status);
    }
    return status;
}

static void show_row(const picker_t *picker, int32_t led, bool on)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%s   %s", picker->names[led],
                   on ? "ติด" : "ดับ");
    show(picker, picker->rows[led], text, on ? COLOR_OK : COLOR_DIM);
}

/* The report comes from `current`, never from asking the LED: the
 * variable is the only thing that knows the truth. */
static void show_current(const picker_t *picker)
{
    char text[TEXT_BYTES];

    show_row(picker, picker->current, true);
    (void)snprintf(text, sizeof(text), "%" PRId32, picker->current);
    show(picker, picker->seg, text, COLOR_OK);
    show(picker, picker->name, picker->names[picker->current],
         COLOR_OK);
    (void)snprintf(text, sizeof(text),
                   "กดไปแล้ว %" PRId32 " ครั้ง | ตอนนี้ดวง %" PRId32 " = %s",
                   picker->presses, picker->current,
                   picker->names[picker->current]);
    (void)bento_ui_set_text(picker->ui, picker->status, text,
                            (uint32_t)strlen(text));
    (void)snprintf(text, sizeof(text), "ติดอยู่ที่ดวง %" PRId32 " = %s",
                   picker->current, picker->names[picker->current]);
    (void)bento_lcd_print(text);
}

/* A press: the old LED off first, then move on. Never the other way
 * round, or the new LED just lit would be the one put out. % count
 * wraps the last LED back to the first. */
static bento_status_t next_led(picker_t *picker)
{
    bento_status_t status = BENTO_OK;

    if (picker->current >= 0) {
        status = bento_gpio_led_off((uint32_t)picker->current);
        if (status == BENTO_OK) {
            show_row(picker, picker->current, false);
        }
    }
    if (status == BENTO_OK) {
        picker->current = (picker->current + 1) % picker->count;
        picker->presses++;
        status = bento_gpio_led_on((uint32_t)picker->current);
    }
    if (status == BENTO_OK) {
        show_current(picker);
    }
    return status;
}

/* One read of the button: a change restarts the settle time; a level
 * that has held DEBOUNCE_MS becomes the stable one, and a new press
 * moves to the next LED. */
static bento_status_t read_button(picker_t *picker, button_t *button)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    bool raw = false;
    bento_status_t status = bento_gpio_button_is_pressed(0U, &raw);

    if ((status == BENTO_OK) && (raw != button->last_raw)) {
        button->last_raw = raw;
        button->last_change_ms = now_ms;
    } else if ((status == BENTO_OK) && (raw != button->stable) &&
               (bento_time_ticks_diff(now_ms, button->last_change_ms) >=
                DEBOUNCE_MS)) {
        button->stable = raw;
        status = raw ? next_led(picker) : BENTO_OK;
    } else {
        /* nothing new */
    }
    return status;
}

/* The loop reads the button every 5 ms and gives the screen its turn
 * every round, or the picture would freeze. */
static bento_status_t run_picker(picker_t *picker)
{
    button_t button = {false, false, 0U};
    uint32_t start_ms;
    bento_status_t status =
        bento_gpio_button_is_pressed(0U, &button.stable);

    button.last_raw = button.stable;
    start_ms = bento_time_ticks_ms();
    button.last_change_ms = start_ms;
    (void)bento_lcd_print(
        "<span class=muted>กดปุ่มเพื่อเลื่อนไปดวงถัดไป</span>");
    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = read_button(picker, &button);
        if (status == BENTO_OK) {
            poll_once(picker->ui);
            status = sleep_checked(POLL_MS);
        }
    }
    return status;
}

static bento_status_t finish(const picker_t *picker)
{
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < picker->count) && (status == BENTO_OK); i++) {
        status = bento_gpio_led_off((uint32_t)i);
        if (status == BENTO_OK) {
            show_row(picker, i, false);
        }
    }
    if (status == BENTO_OK) {
        show(picker, picker->seg, "-", COLOR_DIM);
        show(picker, picker->name, "ดับครบทุกดวงแล้ว", COLOR_DIM);
        show(picker, picker->status,
             "จบแล้ว สถานะจริงอยู่ในตัวแปร ไม่ใช่ในหลอด", COLOR_OK);
        poll_once(picker->ui);
        (void)bento_lcd_print(
            "<span class=ok>จบแล้ว ดับไฟครบทุกดวง</span>");
        (void)snprintf(text, sizeof(text),
                       "<span class=muted>กดไปทั้งหมด %" PRId32
                       " ครั้ง</span>",
                       picker->presses);
        (void)bento_lcd_print(text);
    }
    return status;
}

int main(void)
{
    picker_t picker;
    bento_status_t status;

    (void)memset(&picker, 0, sizeof(picker));
    picker.ui = bento_runtime_get_ui();
    picker.current = NO_LED;
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>ปุ่มเดียว ไฟทุกดวง</h2>");
    status = read_leds(&picker);
    /* gpio.button(0) raises on a board without a button */
    if ((status == BENTO_OK) && (bento_gpio_num_buttons() < 1U)) {
        status = BENTO_E_INVALID_ARGUMENT;
    }
    if (status == BENTO_OK) {
        status = build_screen(&picker);
    }
    if (status == BENTO_OK) {
        status = run_picker(&picker);
    }
    if (status == BENTO_OK) {
        status = finish(&picker);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
