/*
 * 11_lights_and_a_button.c -- ไฟจริงกับปุ่มจริง (Real lights and a
 * real button)
 *
 * เขียนโปรแกรมด้วย gpio.num_leds() ไม่ใช่ด้วยเลข 3 ที่จำมา โค้ดชุดเดียวจึง
 * วิ่งครบทุกดวงบนบอร์ดใบไหนก็ได้ ท่าที่ 1 หลอดวิ่งไล่ทีละดวง ชื่อดวงที่ติด
 * ขึ้นบนจอ ท่าที่ 2 ดวงที่เลือก (ดวง RGB ถ้ามี) ไล่ความสว่างขึ้นลง แถบบนจอ
 * เดินตาม ท่าที่ 3 กดปุ่มผู้ใช้บนบอร์ดแล้วตัวเลขเดินขึ้น
 * กับดัก: brightness() ค้างระดับได้เฉพาะดวงที่มี PWM ของฮาร์ดแวร์ ท่าที่ค้าง
 * แน่ทุกดวงคือ hold(pct, ms) และ duty() คืนเลขที่สั่งล่าสุด ไม่ได้วัดจริง
 *
 * Write the program with gpio.num_leds(), not a remembered 3, and one
 * code runs every LED on any board. Phase 1 chases the LEDs one by one
 * with the lit LED's name on the screen. Phase 2 walks the chosen LED
 * (an RGB one when there is one) up and down a brightness ladder with
 * a bar following. Phase 3 counts presses of the user button. Trap:
 * brightness() holds a level only on an LED with a hardware PWM line;
 * hold(pct, ms) holds it on every LED and ends dark, and duty() gives
 * back the last number asked for, not a measurement.
 *
 * Twin of 11_lights_and_a_button.py
 * (embedded-systems-for-aiot-developer/examples/s01/11_lights_and_a_button.py).
 * Style: CLEAN_C.md.
 * Where C differs: an LED or a button is its index, not an object;
 * class Stop / raise Stop for a board without a button becomes a test
 * that skips phase 3; lcd.print(a, b, ...) is one snprintf() line.
 *
 * ตาคุณ: ให้การกดแต่ละครั้งเลื่อนไปติดดวงถัดไป แล้ววนกลับดวงแรก โดยไม่
 * พิมพ์เลขจำนวนดวงลงไปตรง ๆ (ใบ้: led_count กับ %)
 * Your turn: make each press light the next LED and wrap round to the
 * first, without writing the LED count as a number (hint: led_count
 * and %).
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
    CHASE_ROUNDS = 2, /* rounds of the chase */
    LADDER_STEPS = 7,
    BRIGHT_PCT = 60, /* from here up the bar turns green */
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200,  /* let the CM55 core finish clearing the screen */
    STEP_MS = 220,    /* each LED stays on this long in the chase */
    HOLD_MS = 500,    /* each brightness step is held this long */
    WATCH_MS = 12000, /* the button is open for presses this long */
    POLL_MS = 60      /* often enough that a quick press is not lost */
};

/* Layout on the 792 x 398 canvas. */
enum {
    FONT_PX = 20,
    BIG_FONT_PX = 24,
    STATE_FONT_PX = 28,
    LEFT_X = 20,
    TITLE_Y = 12,
    CARD_W = 652,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    LED_CARD_Y = 52,
    LED_CARD_H = 116,
    CAPTION_Y = 58,
    INDEX_X = 40,
    DIGITS_Y = 88,
    INDEX_W = 92,
    DIGITS_H = 56,
    NAME_CAPTION_X = 204,
    NAME_X = 180,
    NAME_Y = 92,
    BAR_X = 400,
    BAR_Y = 96,
    BAR_W = 252,
    BAR_H = 28,
    BAR_MAX = 100,
    DUTY_Y = 128,
    BUTTON_CARD_Y = 180,
    BUTTON_CARD_H = 104,
    BUTTON_CAPTION_Y = 186,
    STATE_Y = 216,
    COUNT_X = 380,
    COUNT_W = 140,
    PHASE_Y = 296,
    NOTE_Y = 332,
    HINT_Y = 364
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

static const char TEXT_RELEASED[] = "ปล่อยอยู่";

typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} text_style_t;

/* The widgets the program changes, and what the board said it has. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t led_digits;
    bento_ui_id_t led_name;
    bento_ui_id_t bar;
    bento_ui_id_t duty;
    bento_ui_id_t state;
    bento_ui_id_t presses;
    bento_ui_id_t phase;
    bento_ui_id_t note;
    uint32_t led_count;
    uint32_t button_count;
    uint32_t show; /* the LED phases 2 and 3 use */
} lights_t;

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The widget setters ignore a refusal in MicroPython (modui.c), so
 * they do here too. */
static void set_text(const lights_t *lights, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(lights->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void set_color(const lights_t *lights, bento_ui_id_t widget,
                      uint32_t color)
{
    (void)bento_ui_set_color(lights->ui, widget, color);
}

static void poll_once(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* ui.poll() answers no events when it fails; none are read */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
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

static bento_status_t add_card(bento_ui_t *ui, int16_t y, int16_t h)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = LEFT_X;
    args.y = y;
    args.w = CARD_W;
    args.h = h;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border colour */
    args.max_val = CARD_RADIUS; /* and max= its corner radius */
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &card);
}

static bento_status_t add_digits(bento_ui_t *ui, const char *text,
                                 const text_style_t *style, int16_t w,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = style->x;
    args.y = style->y;
    args.w = w;
    args.h = DIGITS_H;
    args.color = style->color;
    return bento_ui_seg7_create(ui, &args, out_id);
}

/* A Bar takes no color= when it is made: its constructor ignores it,
 * so the colour is set afterwards (ui_widget_mgr.c). */
static bento_status_t add_bar(lights_t *lights)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = BAR_X;
    args.y = BAR_Y;
    args.w = BAR_W;
    args.h = BAR_H;
    args.min_val = 0;
    args.max_val = BAR_MAX;
    args.font_px = 0;
    status = bento_ui_bar_create(lights->ui, &args, &lights->bar);
    if (status == BENTO_OK) {
        set_color(lights, lights->bar, COLOR_WARN);
    }
    return status;
}

static bento_status_t build_led_card(lights_t *lights)
{
    const text_style_t index_caption = {INDEX_X, CAPTION_Y, COLOR_DIM,
                                        FONT_PX};
    const text_style_t index = {INDEX_X, DIGITS_Y, COLOR_OK, 0};
    const text_style_t name_caption = {NAME_CAPTION_X, CAPTION_Y,
                                       COLOR_DIM, FONT_PX};
    const text_style_t name = {NAME_X, NAME_Y, COLOR_INFO, BIG_FONT_PX};
    const text_style_t bar_caption = {BAR_X, CAPTION_Y, COLOR_DIM,
                                      FONT_PX};
    const text_style_t duty = {BAR_X, DUTY_Y, COLOR_DIM, FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        add_card(lights->ui, LED_CARD_Y, LED_CARD_H);

    if (status == BENTO_OK) {
        status =
            add_label(lights->ui, "ดวงที่สั่งอยู่", &index_caption, &unused);
    }
    if (status == BENTO_OK) {
        status = add_digits(lights->ui, "-", &index, INDEX_W,
                            &lights->led_digits);
    }
    if (status == BENTO_OK) {
        status =
            add_label(lights->ui, "ชื่อของมัน", &name_caption, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(lights->ui, "ยังไม่ได้สั่ง", &name, &lights->led_name);
    }
    if (status == BENTO_OK) {
        status =
            add_label(lights->ui, "ความสว่างที่สั่ง", &bar_caption, &unused);
    }
    if (status == BENTO_OK) {
        status = add_bar(lights);
    }
    if (status == BENTO_OK) {
        status =
            add_label(lights->ui, "duty() = 0", &duty, &lights->duty);
    }
    return status;
}

static bento_status_t build_button_card(lights_t *lights)
{
    const text_style_t caption = {INDEX_X, BUTTON_CAPTION_Y, COLOR_DIM,
                                  FONT_PX};
    const text_style_t state = {INDEX_X, STATE_Y, COLOR_DIM,
                                STATE_FONT_PX};
    const text_style_t count_caption = {COUNT_X, BUTTON_CAPTION_Y,
                                        COLOR_DIM, FONT_PX};
    const text_style_t count = {COUNT_X, STATE_Y, COLOR_INFO, 0};
    bento_ui_id_t unused;
    bento_status_t status =
        add_card(lights->ui, BUTTON_CARD_Y, BUTTON_CARD_H);

    if (status == BENTO_OK) {
        status = add_label(lights->ui, "ปุ่มบนบอร์ด", &caption, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(lights->ui, "ยังไม่ถึงคิวปุ่ม", &state, &lights->state);
    }
    if (status == BENTO_OK) {
        status = add_label(lights->ui, "กดไปแล้ว (ครั้ง)", &count_caption,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_digits(lights->ui, "0", &count, COUNT_W,
                            &lights->presses);
    }
    return status;
}

static bento_status_t build_footer(lights_t *lights)
{
    const text_style_t phase = {LEFT_X, PHASE_Y, COLOR_WARN, FONT_PX};
    const text_style_t note = {LEFT_X, NOTE_Y, COLOR_DIM, FONT_PX};
    const text_style_t hint = {LEFT_X, HINT_Y, COLOR_DIM, FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status = add_label(lights->ui, "ท่าที่ 1 - ไล่ทีละดวง",
                                      &phase, &lights->phase);

    if (status == BENTO_OK) {
        status = add_label(lights->ui, "ยังไม่เริ่ม", &note, &lights->note);
    }
    if (status == BENTO_OK) {
        status = add_label(lights->ui, "มองที่บอร์ดด้วย ไม่ใช่มองแต่จอ", &hint,
                           &unused);
    }
    return status;
}

static bento_status_t build_screen(lights_t *lights)
{
    const text_style_t title = {LEFT_X, TITLE_Y, COLOR_TEXT,
                                BIG_FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(lights->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(lights->ui, "ไฟจริงกับปุ่มจริง", &title, &unused);
    }
    if (status == BENTO_OK) {
        status = build_led_card(lights);
    }
    if (status == BENTO_OK) {
        status = build_button_card(lights);
    }
    if (status == BENTO_OK) {
        status = build_footer(lights);
    }
    return status;
}

/* Every LED off before the show; then the LED to show is found by its
 * name, not a remembered number: on a board in its base LED1/LED2 may
 * be hidden, so an RGB_ LED is used when there is one. (On the Eva Kit
 * the LED named RGB_RED is really blue; only "visible" matters here.) */
static bento_status_t prepare_leds(lights_t *lights)
{
    const char rgb_prefix[] = "RGB_";
    bento_status_t status = BENTO_OK;
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < lights->led_count) && (status == BENTO_OK); i++) {
        status = bento_gpio_led_off(i);
    }
    for (i = 0U;
         (i < lights->led_count) && (status == BENTO_OK) && !found;
         i++) {
        const char *name = NULL;

        status = bento_gpio_led_name(i, &name);
        if ((status == BENTO_OK) &&
            (strncmp(name, rgb_prefix, strlen(rgb_prefix)) == 0)) {
            lights->show = i;
            found = true;
        }
    }
    return status;
}

/* One LED of the chase: on, its number and name, then off. */
static bento_status_t chase_one(const lights_t *lights, uint32_t led)
{
    char text[TEXT_BYTES];
    const char *name = NULL;
    bento_status_t status = bento_gpio_led_on(led);

    if (status == BENTO_OK) {
        status = bento_gpio_led_name(led, &name);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%" PRIu32, led);
        set_text(lights, lights->led_digits, text);
        set_text(lights, lights->led_name, name);
        (void)snprintf(text, sizeof(text), "ติดดวงที่ %" PRIu32 " - %s",
                       led, name);
        (void)bento_lcd_print(text);
        poll_once(lights->ui);
        status = sleep_checked(STEP_MS);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(led);
    }
    return status;
}

/* Phase 1: led_count, not 3, so a board with five LEDs runs all five. */
static bento_status_t run_chase(const lights_t *lights)
{
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    int32_t round;
    uint32_t led;

    (void)snprintf(text, sizeof(text), "ไล่ทีละดวง %d รอบ - มองที่บอร์ด",
                   CHASE_ROUNDS);
    set_text(lights, lights->note, text);
    poll_once(lights->ui);
    for (round = 0; (round < CHASE_ROUNDS) && (status == BENTO_OK);
         round++) {
        for (led = 0U;
             (led < lights->led_count) && (status == BENTO_OK); led++) {
            status = chase_one(lights, led);
        }
    }
    return status;
}

/* One step of the ladder. hold() repeats the pulse until ms is over,
 * so it holds the level on every LED and also stands in for a sleep.
 * duty() then still answers pct although the LED is dark: it is the
 * last number asked for, not a measurement. */
static bento_status_t ladder_step(const lights_t *lights, int32_t pct)
{
    char text[TEXT_BYTES];
    uint32_t duty = 0U;
    bento_status_t status;

    (void)bento_ui_set_value(lights->ui, lights->bar, pct);
    set_color(lights, lights->bar,
              (pct >= BRIGHT_PCT) ? COLOR_OK : COLOR_WARN);
    (void)snprintf(text, sizeof(text), "กำลังสั่ง %d %%", (int)pct);
    set_text(lights, lights->duty, text);
    poll_once(lights->ui);
    status = bento_gpio_led_hold(lights->show, pct, HOLD_MS);
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_duty(lights->show, &duty);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "duty() = %" PRIu32 " แต่หลอดดับแล้ว", duty);
        set_text(lights, lights->duty, text);
        (void)snprintf(text, sizeof(text),
                       "hold( %d , %d ) -> duty() %" PRIu32, (int)pct,
                       HOLD_MS, duty);
        (void)bento_lcd_print(text);
        poll_once(lights->ui);
    }
    return status;
}

/* Phase 2: the chosen LED up and down the brightness ladder. */
static bento_status_t run_ladder(const lights_t *lights)
{
    static const int32_t ladder[LADDER_STEPS] = {10, 35, 60, 85,
                                                 60, 35, 10};
    char text[TEXT_BYTES];
    const char *name = NULL;
    bento_status_t status = bento_gpio_led_name(lights->show, &name);
    uint32_t i;

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "ท่าที่ 2 - ไล่ความสว่างดวงที่ %" PRIu32, lights->show);
        set_text(lights, lights->phase, text);
        set_color(lights, lights->phase, COLOR_INFO);
        (void)snprintf(text, sizeof(text), "%" PRIu32, lights->show);
        set_text(lights, lights->led_digits, text);
        set_text(lights, lights->led_name, name);
        set_text(lights, lights->note,
                 "hold() ย้ำพัลส์ให้เอง ค้างระดับได้ทุกดวง");
        poll_once(lights->ui);
        (void)bento_lcd_console(
            "<span class=muted>--- ไล่ความสว่าง ---</span>");
    }
    for (i = 0U; (i < (uint32_t)LADDER_STEPS) && (status == BENTO_OK);
         i++) {
        status = ladder_step(lights, ladder[i]);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_value(lights->ui, lights->bar, 0);
        status = bento_gpio_led_off(lights->show);
    }
    return status;
}

/* A board that gives Python no button says so instead of ending
 * silently. */
static void skip_buttons(const lights_t *lights)
{
    set_text(lights, lights->phase, "ท่าที่ 3 - บอร์ดนี้ไม่มีปุ่มให้ Python");
    set_color(lights, lights->phase, COLOR_DIM);
    set_text(lights, lights->state, "ข้ามท่านี้");
    poll_once(lights->ui);
    (void)bento_lcd_print(
        "<span class=warn>ไม่มีปุ่มให้ Python ข้ามท่าที่ 3</span>");
}

/* The silkscreen and .name() differ, and boards differ, so the code
 * always calls the button by .name(). */
static bento_status_t open_buttons(const lights_t *lights)
{
    char text[TEXT_BYTES];
    const char *name = NULL;
    bento_status_t status = bento_gpio_button_name(0U, &name);

    if (status == BENTO_OK) {
        set_text(lights, lights->phase, "ท่าที่ 3 - กดปุ่มบนบอร์ดได้เลย");
        set_color(lights, lights->phase, COLOR_OK);
        set_text(lights, lights->state, TEXT_RELEASED);
        set_color(lights, lights->state, COLOR_DIM);
        (void)snprintf(text, sizeof(text),
                       "ปุ่มผู้ใช้ตัวเดียว - โค้ดเรียก %s ดัชนี 0", name);
        set_text(lights, lights->note, text);
        poll_once(lights->ui);
        (void)bento_lcd_console(
            "<span class=muted>--- รอปุ่ม ---</span>");
        (void)snprintf(text, sizeof(text),
                       "กดปุ่มผู้ใช้บนบอร์ด - โค้ดเรียกมันว่า %s", name);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* The button changed. Only the falling edge counts, the round where
 * it went from released to pressed: counting every round where it is
 * down would count one long press a hundred times. The LED answers
 * every press so the presser knows the board heard. */
static bento_status_t show_change(const lights_t *lights, bool down,
                                  int32_t *presses)
{
    char text[TEXT_BYTES];
    bento_status_t status;

    if (down) {
        *presses += 1;
        (void)snprintf(text, sizeof(text), "%d", (int)*presses);
        set_text(lights, lights->presses, text);
        set_text(lights, lights->state, "กดอยู่");
        set_color(lights, lights->state, COLOR_OK);
        status = bento_gpio_led_on(lights->show);
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>กดครั้งที่ %d </span>",
                       (int)*presses);
        (void)bento_lcd_print(text);
    } else {
        set_text(lights, lights->state, TEXT_RELEASED);
        set_color(lights, lights->state, COLOR_DIM);
        status = bento_gpio_led_off(lights->show);
    }
    return status;
}

/* Reads the button every POLL_MS, minus the time the work took, until
 * WATCH_MS has passed. */
static bento_status_t watch_button(const lights_t *lights,
                                   int32_t *presses)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    uint32_t work_start_ms = start_ms;
    bool was_down = false;
    bento_status_t status = BENTO_OK;

    while (
        (status == BENTO_OK) &&
        (bento_time_ticks_diff(work_start_ms, start_ms) < WATCH_MS)) {
        bool down = false;

        status = bento_gpio_button_is_pressed(0U, &down);
        if ((status == BENTO_OK) && (down != was_down)) {
            was_down = down;
            status = show_change(lights, down, presses);
        }
        if (status == BENTO_OK) {
            const int32_t left_ms =
                POLL_MS - bento_time_ticks_diff(bento_time_ticks_ms(),
                                                work_start_ms);

            poll_once(lights->ui);
            status = (left_ms > 0) ? sleep_checked((uint32_t)left_ms)
                                   : BENTO_OK;
        }
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
        work_start_ms = bento_time_ticks_ms();
    }
    return status;
}

static void close_buttons(const lights_t *lights, int32_t presses)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "จบแล้ว - กดไป %d ครั้ง",
                   (int)presses);
    set_text(lights, lights->phase, text);
    set_color(lights, lights->phase, COLOR_DIM);
    set_text(lights, lights->state, "ปิดรับแล้ว");
    set_color(lights, lights->state, COLOR_DIM);
    (void)snprintf(text, sizeof(text),
                   "หลอด %" PRIu32 " ดวง ปุ่ม %" PRIu32 " ปุ่ม",
                   lights->led_count, lights->button_count);
    set_text(lights, lights->note, text);
    poll_once(lights->ui);
    (void)bento_lcd_console(
        "<span class=muted>------------------------</span>");
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>กดทั้งหมด %d ครั้ง</span>",
                   (int)presses);
    (void)bento_lcd_print(text);
}

/* Phase 3: wait for the button. */
static bento_status_t run_buttons(const lights_t *lights)
{
    int32_t presses = 0;
    bento_status_t status = BENTO_OK;

    if (lights->button_count < 1U) {
        skip_buttons(lights);
    } else {
        status = open_buttons(lights);
        if (status == BENTO_OK) {
            status = watch_button(lights, &presses);
        }
        if (status == BENTO_OK) {
            status = bento_gpio_led_off(lights->show);
        }
        if (status == BENTO_OK) {
            close_buttons(lights, presses);
        }
    }
    return status;
}

int main(void)
{
    char text[TEXT_BYTES];
    lights_t lights;
    bento_status_t status;

    (void)memset(&lights, 0, sizeof(lights));
    lights.ui = bento_runtime_get_ui();
    lights.led_count = bento_gpio_num_leds();
    lights.button_count = bento_gpio_num_buttons();
    status = build_screen(&lights);
    if (status == BENTO_OK) {
        poll_once(lights.ui);
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ไฟจริงกับปุ่มจริง</h2>");
        (void)snprintf(text, sizeof(text),
                       "บอร์ดนี้มีหลอด %" PRIu32 " ดวง | ปุ่ม %" PRIu32 " ปุ่ม",
                       lights.led_count, lights.button_count);
        (void)bento_lcd_print(text);
        status = prepare_leds(&lights);
    }
    if (status == BENTO_OK) {
        status = run_chase(&lights);
    }
    if (status == BENTO_OK) {
        status = run_ladder(&lights);
    }
    if (status == BENTO_OK) {
        status = run_buttons(&lights);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
