/*
 * 01_first_widgets.c -- widget ตัวแรก และเหตุผลที่ต้องใส่ x กับ y ทุกครั้ง
 *
 * เรียกสร้าง widget หนึ่งครั้งได้ widget หนึ่งตัว ที่ค้างบนจอจนกว่าจะมีใคร
 * ล้างจอใหม่ จอมีการ์ดหนึ่งใบที่มีตัวนับ "แตะไปกี่ครั้ง" กับชื่อปุ่มที่แตะล่าสุด
 * ปุ่มสองใบ และบรรทัดสถานะ แตะปุ่มแล้วตัวนับเดินขึ้นทันที และลิ้นชัก Console
 * ได้บรรทัดบันทึกทุกครั้ง ตัวอย่างเดิน 25 วินาที ทุก widget ระบุ x กับ y เสมอ
 *
 * One create call makes one widget, which stays on the screen until
 * someone clears it. The screen has a card with a tap counter and the
 * name of the last button tapped, two buttons and a status line. Tap a
 * button and the counter goes up at once, and the Console drawer logs
 * every tap. The example runs for 25 seconds. Every widget gets its x
 * and y, never the screen's own 200 px grid.
 *
 * Twin of 01_first_widgets.py
 * (embedded-systems-for-aiot-developer/examples/s04/01_first_widgets.py),
 * from the library twin aiot-s04-01-first-widgets. Style: CLEAN_C.md.
 * The Python writes the loop pause (50 ms) and the second button's
 * colour inline; here they are the named LOOP_MS and COLOR_BUTTON_ALT.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    /* at least this long per round, so the screen has time to draw */
    LOOP_MS = 50,
    RUN_MS = 25000
};

/* The widgets the program changes or listens to after it has drawn
 * the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t taps;
    bento_ui_id_t last;
    bento_ui_id_t hello;
    bento_ui_id_t bye;
    bento_ui_id_t status;
} first_screen_t;

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 136, /* a 20 px caption and the big value under it */
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CAPTION_Y = 70,
    TAPS_X = 40,
    TAPS_Y = 104,
    TAPS_W = 192,
    TAPS_H = 72,
    LAST_X = 264, /* the counter ends at 232, then a 32 px gap */
    LAST_Y = 104,
    RULE_Y = 208,
    BUTTON_Y = 240,
    BUTTON_W = 200,
    BUTTON_H = 88, /* the smallest touch target */
    BYE_X = 256,   /* 32 px from the first button, against mistaps */
    HINT_X = 488,
    HINT_Y = 272,
    /* ends before x = 690, the Console button's corner */
    STATUS_Y = 350,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. The counter is not a state, so it is
 * accent, not green. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    /* the secondary button, not the main path */
    COLOR_BUTTON_ALT = 0x3A4150
};

static const char TEXT_HELLO[] = "ทักทาย";
static const char TEXT_BYE[] = "ลาก่อน";

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t widget,
                      const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                point_t at, uint32_t color,
                                int16_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = at.x;
    args.y = at.y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* The card's frame first, then what sits on it: creation order is
 * drawing order. */
static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_CARD; /* the border colour: the card's own */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* Seg7 shows text only, so a number is always turned into text
 * first. */
static bento_status_t add_taps(first_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0";
    args.text_len = 1U;
    args.x = TAPS_X;
    args.y = TAPS_Y;
    args.w = TAPS_W;
    args.h = TAPS_H;
    args.color = COLOR_ACCENT;
    return bento_ui_seg7_create(screen->ui, &args, &screen->taps);
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 int16_t x, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = BUTTON_Y;
    args.w = BUTTON_W;
    args.h = BUTTON_H;
    args.color = color;
    args.font_px = FONT_TITLE_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* The title and the report card: the tap counter and the last
 * button's name. */
static bento_status_t build_card(first_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t title = {MARGIN_X, TITLE_Y};
    const point_t taps_caption = {TAPS_X, CAPTION_Y};
    const point_t last_caption = {LAST_X, CAPTION_Y};
    const point_t last = {LAST_X, LAST_Y};
    bento_ui_id_t unused;
    bento_status_t status = add_label(
        ui, "widget ตัวแรก", title, COLOR_TEXT, FONT_TITLE_PX, &unused);

    if (status == BENTO_OK) {
        status = add_card(ui);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "แตะไปแล้ว (ครั้ง)", taps_caption,
                           COLOR_DIM, FONT_TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_taps(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ปุ่มที่แตะล่าสุด", last_caption, COLOR_DIM,
                           FONT_TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ยังไม่มี", last, COLOR_TEXT, FONT_TITLE_PX,
                           &screen->last);
    }
    return status;
}

/* The rule line, the two buttons and the status line. */
static bento_status_t build_buttons(first_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t rule = {MARGIN_X, RULE_Y};
    const point_t hint = {HINT_X, HINT_Y};
    const point_t status_line = {MARGIN_X, STATUS_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "ทุก widget ต้องระบุ x= y= เสมอ", rule, COLOR_DIM,
                  FONT_TEXT_PX, &unused);

    if (status == BENTO_OK) {
        status = add_button(ui, TEXT_HELLO, MARGIN_X, COLOR_ACCENT,
                            &screen->hello);
    }
    if (status == BENTO_OK) {
        status = add_button(ui, TEXT_BYE, BYE_X, COLOR_BUTTON_ALT,
                            &screen->bye);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "แตะปุ่มสองใบนี้", hint, COLOR_DIM,
                           FONT_TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ยังไม่ได้แตะอะไร", status_line, COLOR_DIM,
                           FONT_TEXT_PX, &screen->status);
    }
    return status;
}

static bento_status_t build_screen(first_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = build_card(screen);
    }
    if (status == BENTO_OK) {
        status = build_buttons(screen);
    }
    return status;
}

/* True when the event is of this kind and came from this widget. */
static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* The name of the button a click came from, or NULL for any other
 * event: every button sends the same kind, so the widget decides. */
static const char *clicked_name(const first_screen_t *screen,
                                const bento_ui_event_t *event)
{
    const char *name = NULL;

    if (event_is(event, BENTO_UI_EVENT_CLICKED, screen->hello)) {
        name = TEXT_HELLO;
    } else if (event_is(event, BENTO_UI_EVENT_CLICKED, screen->bye)) {
        name = TEXT_BYE;
    } else {
        /* another widget, or another kind of event: not counted */
    }
    return name;
}

static void show_tap(const first_screen_t *screen, const char *name,
                     int32_t taps)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, taps);
    show_text(screen->ui, screen->taps, text);
    show_text(screen->ui, screen->last, name);
    (void)snprintf(text, sizeof(text), "แตะปุ่ม %s แล้ว", name);
    show_text(screen->ui, screen->status, text);
    (void)snprintf(text, sizeof(text),
                   "clicked -> %s (ครั้งที่ %" PRId32 ")", name, taps);
    /* lcd.print() ignores a console that is not ready */
    (void)bento_lcd_print(text);
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static void handle_events(const first_screen_t *screen, int32_t *taps)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        const char *name = clicked_name(screen, &events[i]);

        if (name != NULL) {
            (*taps)++;
            show_tap(screen, name, *taps);
        }
    }
}

/* The screen says it has ended first, for whoever watches only the
 * screen; the widgets stay until someone clears the screen. */
static void show_end(const first_screen_t *screen, int32_t taps)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "หมดเวลา - แตะทั้งหมด %" PRId32 " ครั้ง", taps);
    show_text(screen->ui, screen->status, text);
    show_text(screen->ui, screen->last, "จบแล้ว");
    (void)snprintf(text, sizeof(text), "สรุป: แตะทั้งหมด %" PRId32 " ครั้ง",
                   taps);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print(
        "widget ยังค้างบนจอ จนกว่าจะมีใครเรียก ui.screen()");
}

/* Reads the taps every LOOP_MS until RUN_MS has passed or Stop is
 * pressed. */
static bento_status_t run_loop(const first_screen_t *screen,
                               int32_t *taps)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        handle_events(screen, taps);
        status = pause_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    first_screen_t screen;
    int32_t taps = 0;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        /* lcd calls ignore a console that is not ready (modlcd.c) */
        (void)bento_lcd_print("widget ตัวแรก - "
                              "แตะปุ่มแล้วตัวนับบนจอจะเดิน");
        (void)bento_lcd_print("เหตุการณ์แต่ละครั้งจะถูกบันทึกไว้ตรงนี้");
        status = run_loop(&screen, &taps);
    }
    if (status == BENTO_OK) {
        show_end(&screen, taps);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
