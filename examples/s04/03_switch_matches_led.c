/*
 * 03_switch_matches_led.c -- สวิตช์บนจอ กับ ไฟจริง ต้องพูดตรงกันเสมอ
 *
 * มีฟังก์ชันเดียว set_led() ที่มีสิทธิ์เปลี่ยนสถานะไฟ ทุกเส้นทาง (สวิตช์บนจอ
 * ปุ่ม ALL OFF และตอนจบ) ต้องผ่านประตูนั้น จอมีแถวสวิตช์ตามจำนวนไฟบนบอร์ด
 * (ไม่เกินห้าอัน) และ Seg7 ตัวโตบอกว่าตอนนี้ติดอยู่กี่ดวง กด ALL OFF แล้วดู
 * สวิตช์เด้งกลับพร้อมกับเลขลงเป็น 0 ตัวอย่างเดิน 30 วินาที
 *
 * The screen and the real LEDs must always agree. One function,
 * set_led(), is the only gate that changes an LED: it remembers the
 * state, drives the LED, pulls the switch on screen to match and
 * reports the count. There is one switch per LED (at most five fit),
 * a Seg7 with how many are lit, and an ALL OFF button that turns every
 * LED off through the same gate, so the switches spring back by
 * themselves. The example runs for 30 seconds.
 *
 * Twin of 03_switch_matches_led.py
 * (embedded-systems-for-aiot-developer/examples/s04/03_switch_matches_led.py),
 * rewritten from the library twin aiot-s04-03-switch-matches-led. Style:
 * CLEAN_C.md. The LED names come from bento_gpio_led_name(), one per
 * index, in the order gpio.board_info() lists them. As in the Python
 * (no try/finally), the LEDs are turned off at the end only when the
 * 30 s run out; Stop leaves them as they are.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_runtime.h"

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 50,
    RUN_MS = 30000
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    CARD_W = 744,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    SWITCH_CARD_Y = 56,
    SWITCH_CARD_H = 152,
    SWITCH_FIRST_X = 40,
    SWITCH_RIGHT_EDGE = 768, /* the card's right edge */
    SWITCH_NAME_Y = 72,
    SWITCH_Y = 104,
    SWITCH_W = 120,
    SWITCH_H = 88,   /* the smallest touch target */
    PITCH_MIN = 152, /* a 32 px gap: the least that stops mistaps */
    PITCH_MAX = 200,
    SUMMARY_Y = 224,
    SUMMARY_W = 360,
    SUMMARY_H = 112,
    CAPTION_X = 40,
    CAPTION_Y = 238,
    COUNT_X = 40,
    COUNT_Y = 272,
    COUNT_W = 104,
    COUNT_H = 48, /* a Seg7's digit size comes from h, not value */
    STATUS_X = 168,
    STATUS_Y = 280,
    OFF_X = 416, /* 32 px right of the summary card */
    OFF_W = 200,
    OFF_H = 88,
    OFF_HINT_Y = 318,
    LESSON_Y =
        350, /* both bottom lines end before the Console button */
    WARNING_X = 400,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    FONT_NAME_PX = 16,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_BUTTON_ALT = 0x3A4150 /* the secondary button */
};

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

/* Where a label goes and its font size (a Label's value=). */
typedef struct {
    int16_t x;
    int16_t y;
    int32_t font_px;
} label_at_t;

/* The program's truth lives in led_on only: an LED's value() reads
 * the pin, not what was asked, and after brightness() or hold() the
 * pin is left low. */
typedef struct {
    bento_ui_t *ui;
    uint32_t led_count;
    const char *names[BENTO_GPIO_LED_CAP];
    bool led_on[BENTO_GPIO_LED_CAP];
    bento_ui_id_t switches[BENTO_GPIO_LED_CAP];
    uint32_t switch_count;
    bento_ui_id_t count;
    bento_ui_id_t status;
    bento_ui_id_t all_off;
    bento_ui_id_t lesson;
} switch_panel_t;

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t widget,
                      const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_at_t at, uint32_t color,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = at.x;
    args.y = at.y;
    args.color = color;
    args.font_px = at.font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_card(bento_ui_t *ui, rect_t at)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = at.x;
    args.y = at.y;
    args.w = at.w;
    args.h = at.h;
    args.color = COLOR_CARD;
    args.min_val = COLOR_CARD; /* the border colour: the card's own */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* The switch pitch follows the LED count: 3 LEDs get 200 (an 80 px
 * gap), 5 get 152 (the last switch ends at 40 + 4 * 152 + 120 = 768). */
static int32_t switch_pitch(uint32_t led_count)
{
    int32_t pitch = PITCH_MAX;

    if (led_count > 1U) {
        pitch = (SWITCH_RIGHT_EDGE - SWITCH_FIRST_X - SWITCH_W) /
                ((int32_t)led_count - 1);
        if (pitch > PITCH_MAX) {
            pitch = PITCH_MAX;
        }
        if (pitch < PITCH_MIN) {
            pitch = PITCH_MIN;
        }
    }
    return pitch;
}

static bento_status_t add_switch(switch_panel_t *panel, int16_t x)
{
    const label_at_t name = {x, SWITCH_NAME_Y, FONT_NAME_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(panel->ui, panel->names[panel->switch_count], name,
                  COLOR_DIM, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = x;
        args.y = SWITCH_Y;
        args.w = SWITCH_W;
        args.h = SWITCH_H;
        args.color = COLOR_ACCENT;
        status = bento_ui_switch_create(
            panel->ui, &args, &panel->switches[panel->switch_count]);
    }
    if (status == BENTO_OK) {
        panel->switch_count++;
    }
    return status;
}

/* One name and one switch per LED. A board with more than five LEDs
 * gets switches for the first five; set_led() still drives the rest. */
static bento_status_t add_switches(switch_panel_t *panel)
{
    const rect_t card = {MARGIN_X, SWITCH_CARD_Y, CARD_W,
                         SWITCH_CARD_H};
    const int32_t pitch = switch_pitch(panel->led_count);
    bento_status_t status = add_card(panel->ui, card);
    int32_t x = SWITCH_FIRST_X;
    uint32_t i;

    for (i = 0U; (i < panel->led_count) && (status == BENTO_OK) &&
                 ((x + SWITCH_W) <= SWITCH_RIGHT_EDGE);
         i++) {
        status = add_switch(panel, (int16_t)x);
        x += pitch;
    }
    return status;
}

/* The big number reads from afar; the line beside it explains it. A
 * Label made with no text shows "Label", so every label gets a start
 * text. */
static bento_status_t add_summary(switch_panel_t *panel)
{
    const rect_t card = {MARGIN_X, SUMMARY_Y, SUMMARY_W, SUMMARY_H};
    const label_at_t caption = {CAPTION_X, CAPTION_Y, FONT_TEXT_PX};
    const label_at_t status_line = {STATUS_X, STATUS_Y, FONT_TEXT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_card(panel->ui, card);

    if (status == BENTO_OK) {
        status = add_label(panel->ui, "ไฟที่ติดอยู่ (ดวง)", caption,
                           COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "0";
        args.text_len = 1U;
        args.x = COUNT_X;
        args.y = COUNT_Y;
        args.w = COUNT_W;
        args.h = COUNT_H;
        args.color = COLOR_ACCENT;
        status = bento_ui_seg7_create(panel->ui, &args, &panel->count);
    }
    if (status == BENTO_OK) {
        status = add_label(panel->ui, "ยังไม่ได้ตั้งค่าไฟ", status_line,
                           COLOR_DIM, &panel->status);
    }
    return status;
}

/* The button and its hint stack as the right column; then the two
 * lesson lines at the bottom. */
static bento_status_t add_controls(switch_panel_t *panel)
{
    const label_at_t hint = {OFF_X, OFF_HINT_Y, FONT_TEXT_PX};
    const label_at_t lesson = {MARGIN_X, LESSON_Y, FONT_TEXT_PX};
    const label_at_t warning = {WARNING_X, LESSON_Y, FONT_TEXT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "ALL OFF";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = OFF_X;
    args.y = SUMMARY_Y;
    args.w = OFF_W;
    args.h = OFF_H;
    args.color = COLOR_BUTTON_ALT;
    args.font_px = FONT_TITLE_PX;
    status = bento_ui_button_create(panel->ui, &args, &panel->all_off);
    if (status == BENTO_OK) {
        status = add_label(panel->ui, "สวิตช์จะเด้งกลับเอง", hint,
                           COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(panel->ui, "ทุกเส้นทางผ่าน set_led ทางเดียว",
                           lesson, COLOR_DIM, &panel->lesson);
    }
    if (status == BENTO_OK) {
        status = add_label(panel->ui, "สั่ง gpio ตรง ๆ จอจะโกหก", warning,
                           COLOR_DIM, &unused);
    }
    return status;
}

/* The LED count and names; an LED the board cannot name raises in
 * MicroPython, so its status ends the program here. */
static bento_status_t read_board(switch_panel_t *panel)
{
    bento_status_t status = BENTO_OK;
    uint32_t i;

    panel->led_count = bento_gpio_num_leds();
    if (panel->led_count > BENTO_GPIO_LED_CAP) {
        panel->led_count = BENTO_GPIO_LED_CAP;
    }
    for (i = 0U; (i < panel->led_count) && (status == BENTO_OK); i++) {
        status = bento_gpio_led_name(i, &panel->names[i]);
    }
    return status;
}

static bento_status_t build_screen(switch_panel_t *panel)
{
    const label_at_t title = {MARGIN_X, TITLE_Y, FONT_TITLE_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(panel->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = read_board(panel);
    }
    if (status == BENTO_OK) {
        status = add_label(panel->ui, "สวิตช์บนจอ กับ ไฟจริง", title,
                           COLOR_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_switches(panel);
    }
    if (status == BENTO_OK) {
        status = add_summary(panel);
    }
    if (status == BENTO_OK) {
        status = add_controls(panel);
    }
    return status;
}

/* Counted from led_on only; the hardware is never asked. "How many are
 * lit" is neither good nor bad, so no status colour: the accent for a
 * number that changes, the dim colour for zero. */
static void show_count(const switch_panel_t *panel)
{
    char text[TEXT_BYTES];
    uint32_t lit = 0U;
    uint32_t i;

    for (i = 0U; i < panel->led_count; i++) {
        if (panel->led_on[i]) {
            lit++;
        }
    }
    (void)snprintf(text, sizeof(text), "%" PRIu32, lit);
    show_text(panel->ui, panel->count, text);
    (void)bento_ui_set_color(panel->ui, panel->count,
                             (lit != 0U) ? (uint32_t)COLOR_ACCENT
                                         : (uint32_t)COLOR_DIM);
    (void)snprintf(text, sizeof(text), "จาก %" PRIu32 " ดวง",
                   panel->led_count);
    show_text(panel->ui, panel->status, text);
}

/* The only gate that changes an LED: remember, drive, pull the switch
 * to match, report. Every path goes through here. */
static bento_status_t set_led(switch_panel_t *panel, uint32_t led,
                              bool on)
{
    bento_status_t status;

    panel->led_on[led] = on;
    status = on ? bento_gpio_led_on(led) : bento_gpio_led_off(led);
    if (status == BENTO_OK) {
        if (led < panel->switch_count) {
            (void)bento_ui_set_value(panel->ui, panel->switches[led],
                                     on ? 1 : 0);
        }
        show_count(panel);
    }
    return status;
}

static bento_status_t all_off(switch_panel_t *panel)
{
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < panel->led_count) && (status == BENTO_OK); i++) {
        status = set_led(panel, i, false);
    }
    return status;
}

/* The switch an event came from, or switch_count for any other
 * widget. */
static uint32_t switch_index(const switch_panel_t *panel,
                             bento_ui_id_t widget)
{
    uint32_t found = panel->switch_count;
    uint32_t i;

    for (i = panel->switch_count; i > 0U; i--) {
        if (panel->switches[i - 1U] == widget) {
            found = i - 1U;
        }
    }
    return found;
}

/* True when the event is of this kind; the kind passes through a
 * uint8_t, so both sides of the comparison have one type. */
static bool type_is(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

static bento_status_t handle_event(switch_panel_t *panel,
                                   const bento_ui_event_t *event)
{
    const uint32_t led = switch_index(panel, event->id);
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;

    if (type_is(event, BENTO_UI_EVENT_TOGGLED) &&
        (led < panel->switch_count)) {
        status = set_led(panel, led, event->value == 1);
        if (status == BENTO_OK) {
            (void)snprintf(text, sizeof(text), "%s -> %s",
                           panel->names[led],
                           panel->led_on[led] ? "ON" : "OFF");
            (void)bento_lcd_print(text);
        }
    } else if (type_is(event, BENTO_UI_EVENT_CLICKED) &&
               (event->id == panel->all_off)) {
        /* through the same gate, so the switches spring back by
         * themselves */
        status = all_off(panel);
        if (status == BENTO_OK) {
            (void)bento_lcd_print("ALL OFF -> ดับทุกดวงผ่านประตูเดิม");
        }
    } else {
        /* no other event changes anything */
    }
    return status;
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static bento_status_t run_loop(switch_panel_t *panel)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        uint32_t count = 0U;
        uint32_t i;

        if (bento_ui_poll(panel->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                          &count, NULL) != BENTO_OK) {
            count = 0U;
        }
        for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
            status = handle_event(panel, &events[i]);
        }
        if (status == BENTO_OK) {
            status = pause_ms(LOOP_MS);
        }
    }
    return status;
}

int main(void)
{
    switch_panel_t panel;
    bento_status_t status;

    (void)memset(&panel, 0, sizeof(panel));
    panel.ui = bento_runtime_get_ui();
    status = build_screen(&panel);
    if (status == BENTO_OK) {
        /* start from a state we know: ordered off, not assumed off */
        status = all_off(&panel);
    }
    if (status == BENTO_OK) {
        /* lcd calls ignore a console that is not ready (modlcd.c) */
        (void)bento_lcd_print("สลับสวิตช์บนจอ แล้วมองหลอดไฟจริงด้วย");
        (void)bento_lcd_print("กด ALL OFF แล้วดูว่าสวิตช์บนจอเด้งกลับด้วยไหม");
        status = run_loop(&panel);
    }
    if (status == BENTO_OK) {
        status = all_off(&panel);
    }
    if (status == BENTO_OK) {
        show_text(panel.ui, panel.lesson, "จบแล้ว - ดับทุกดวงผ่าน set_led");
        (void)bento_lcd_print("สรุป: จอกับไฟตรงกันได้ เพราะมีประตูเดียว");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
