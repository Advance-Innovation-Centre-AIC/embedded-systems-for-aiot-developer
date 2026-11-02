/*
 * 15_press_and_hold.c -- กดค้างไว้: เหตุการณ์ที่ clicked บอกไม่ได้
 *
 * ปุ่มเพิ่มกับปุ่มลด (jog button) ขอรับเหตุการณ์ห้าชนิดด้วย .listen():
 * pressed, released, press_lost, long_pressed และ long_pressed_repeat
 * กดค้างแล้วค่าที่ตั้งไต่ขึ้นหรือลงเอง (อยู่ใน 0-100 เสมอ) ปล่อยหรือลากนิ้ว
 * ออกนอกปุ่มแล้วหยุด แถวล่างบอกเหตุการณ์ล่าสุดและความหมาย ปุ่ม
 * "ตั้งค่าใหม่เป็น 50" คืนค่าเริ่มต้น ทำงานจนกดหยุด
 *
 * The "more" and "less" jog buttons ask for five events with .listen():
 * pressed, released, press_lost, long_pressed and long_pressed_repeat.
 * Hold one and the set point climbs or falls by itself (always within
 * 0-100); lift the finger, or slide it off, and it stops. The rows
 * below show the latest event and what it means. "Reset to 50" restores
 * the start value. Runs until Stop.
 *
 * Twin of 15_press_and_hold.py
 * (embedded-systems-for-aiot-developer/examples/s04/15_press_and_hold.py),
 * from the library twin aiot-s04-15-press-and-hold. Style: CLEAN_C.md.
 * The Python writes 50, 0, 100, 30 ms and the second button's colour
 * inline; here they are named constants. The event's kind is shown by
 * its name and compared only as UI_EVENT_*. Left out on purpose: the
 * original's `holding`, which it sets but never reads.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* The course palette, 0xRRGGBB: one value per role. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_BUTTON_ALT = 0x3A4150 /* a second button, not the main way */
};

/* The set point, and the pace. */
enum {
    SET_START = 50, /* the start value, and the value after a reset */
    SET_MIN = 0,
    SET_MAX = 100,
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 30,   /* short, because long_pressed_repeat comes often */
    TEXT_BYTES = 64 /* the longest text built here is 52 bytes */
};

/* The widgets the program changes or reads after it has drawn the
 * screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t up;
    bento_ui_id_t down;
    bento_ui_id_t reset;
    bento_ui_id_t set_point;
    bento_ui_id_t last;
    bento_ui_id_t why;
    bento_ui_id_t finger;
} jog_screen_t;

/* What the screen shows for one kind of event. */
typedef struct {
    uint8_t kind;        /* UI_EVENT_* */
    const char *name;    /* the name MicroPython's ui.poll() gives it */
    const char *meaning; /* NULL: the original has no words for it */
} event_kind_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The name and the meaning of an event's kind; written down first, so a
 * student can predict, then check. */
static const event_kind_t *find_kind(uint8_t kind)
{
    static const event_kind_t kinds[] = {
        {BENTO_UI_EVENT_CLICKED, "clicked",
         "กดแล้วปล่อยบนปุ่มเดิม - มาถึงตอนปล่อยแล้วเสมอ"},
        {BENTO_UI_EVENT_VALUE_CHANGED, "value_changed", NULL},
        {BENTO_UI_EVENT_TOGGLED, "toggled", NULL},
        {BENTO_UI_EVENT_PRESSED, "pressed",
         "นิ้วลงบนปุ่ม - เริ่มทำงานได้ตรงนี้"},
        {BENTO_UI_EVENT_RELEASED, "released",
         "นิ้วยกขึ้น - "
         "จุดที่ต้องสั่งหยุด"},
        {BENTO_UI_EVENT_PRESS_LOST, "press_lost",
         "นิ้วยังกดอยู่ แต่เลื่อนออกนอกปุ่มแล้ว"},
        {BENTO_UI_EVENT_LONG_PRESSED, "long_pressed",
         "กดค้างครบเวลา - เริ่มโหมดกดค้าง"},
        {BENTO_UI_EVENT_LONG_PRESSED_REPEAT, "long_pressed_repeat",
         "ยังกดค้างอยู่ - value คือจำนวนครั้งที่ย้ำ"},
        {BENTO_UI_EVENT_READY, "ready", NULL},
        {BENTO_UI_EVENT_CANCEL, "cancel", NULL},
        {BENTO_UI_EVENT_FOCUSED, "focused", NULL},
        {BENTO_UI_EVENT_DEFOCUSED, "defocused", NULL},
        {BENTO_UI_EVENT_SCROLL_BEGIN, "scroll_begin", NULL},
        {BENTO_UI_EVENT_SCROLL_END, "scroll_end", NULL},
        {BENTO_UI_EVENT_GESTURE, "gesture", NULL},
        /* the last entry answers every other kind */
        {0U, "unknown", NULL}};
    const uint32_t last =
        (uint32_t)(sizeof(kinds) / sizeof(kinds[0])) - 1U;
    uint32_t i = 0U;

    while ((i < last) && (kinds[i].kind != kind)) {
        i++;
    }
    return &kinds[i];
}

/* The set point stays within its range, so the number never runs
 * past the real one. */
static int32_t clamp(int32_t value)
{
    int32_t clamped = value;

    if (value < SET_MIN) {
        clamped = SET_MIN;
    } else if (value > SET_MAX) {
        clamped = SET_MAX;
    } else {
        /* within the range already */
    }
    return clamped;
}

/* True when the event is of this kind (a uint8_t, so both sides of
 * == have one type). */
static bool kind_is(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

/* Layout on the 792 x 398 canvas: 88 px touch targets, 32 px apart. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    HINT_Y = 56,
    JOG_Y = 96,
    JOG_W = 232,
    JOG_H = 88,
    DOWN_X = 288,
    CARD_X = 552,
    CARD_W = 216,
    VALUE_X = 576,
    VALUE_CAPTION_Y = 104,
    VALUE_Y = 136,
    LAST_Y = 200,
    WHY_Y = 240,
    STATE_Y = 272,
    RESET_Y = 304,
    RESET_W = 304,
    BIG_PX = 28,
    BUTTON_PX = 24,
    TEXT_PX = 20
};

/* Where a widget goes, and how big it is. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = MARGIN_X;
    args.y = y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 rect_t place, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place.x;
    args.y = place.y;
    args.w = place.w;
    args.h = place.h;
    args.color = color;
    args.font_px = BUTTON_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* The card that holds the set point, so the number has a ground of
 * its own. */
static bento_status_t add_set_point_card(jog_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CARD_X;
    args.y = JOG_Y;
    args.w = CARD_W;
    args.h = JOG_H;
    args.color = COLOR_CARD;
    status = bento_ui_panel_create(screen->ui, &args, &unused);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "ค่าที่ตั้งไว้";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = VALUE_X;
        args.y = VALUE_CAPTION_Y;
        args.color = COLOR_DIM;
        args.font_px = TEXT_PX;
        status = bento_ui_label_create(screen->ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "50";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = VALUE_X;
        args.y = VALUE_Y;
        args.color = COLOR_TEXT;
        args.font_px = BIG_PX;
        status = bento_ui_label_create(screen->ui, &args,
                                       &screen->set_point);
    }
    return status;
}

/* The two jog buttons send nothing beyond clicked until they ask: the
 * queue has 16 places, and a widget that sent everything would fill it
 * with what nobody reads. */
static void listen_to_jog(const jog_screen_t *screen)
{
    const uint32_t mask =
        BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_PRESSED) |
        BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_RELEASED) |
        BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_PRESS_LOST) |
        BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_LONG_PRESSED) |
        BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_LONG_PRESSED_REPEAT);

    /* .listen() ignores the display's answer (modui.c
     * widget_listen), so the twin does too. */
    (void)bento_ui_listen_compat(screen->ui, screen->up, mask);
    (void)bento_ui_listen_compat(screen->ui, screen->down, mask);
}

/* The top half: title, hint, the two jog buttons and the set point
 * card. */
static bento_status_t add_controls(jog_screen_t *screen)
{
    const rect_t up_place = {MARGIN_X, JOG_Y, JOG_W, JOG_H};
    const rect_t down_place = {DOWN_X, JOG_Y, JOG_W, JOG_H};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui,
                  "กดค้างไว้ - เหตุการณ์ที่ clicked "
                  "บอกไม่ได้",
                  TITLE_Y, COLOR_TEXT, BIG_PX, &unused);

    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "กดค้างที่ปุ่มเพิ่มหรือลด "
                           "แล้วดูแถวล่าง",
                           HINT_Y, COLOR_DIM, TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_button(screen->ui, "เพิ่ม", up_place, COLOR_ACCENT,
                            &screen->up);
    }
    if (status == BENTO_OK) {
        status = add_button(screen->ui, "ลด", down_place,
                            COLOR_BUTTON_ALT, &screen->down);
    }
    if (status == BENTO_OK) {
        status = add_set_point_card(screen);
    }
    if (status == BENTO_OK) {
        listen_to_jog(screen);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(jog_screen_t *screen)
{
    const rect_t reset_place = {MARGIN_X, RESET_Y, RESET_W, JOG_H};
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_controls(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่มีเหตุการณ์เข้ามา", LAST_Y,
                           COLOR_DIM, BUTTON_PX, &screen->last);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "เหตุการณ์แต่ละชนิดหมายถึงอะไร "
                           "จะขึ้นตรงนี้",
                           WHY_Y, COLOR_DIM, TEXT_PX, &screen->why);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "นิ้ว: ไม่ได้แตะ", STATE_Y,
                           COLOR_DIM, TEXT_PX, &screen->finger);
    }
    if (status == BENTO_OK) {
        status = add_button(screen->ui, "ตั้งค่าใหม่เป็น 50", reset_place,
                            COLOR_BUTTON_ALT, &screen->reset);
    }
    return status;
}

static void show_number(const jog_screen_t *screen, int32_t set_point)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, set_point);
    (void)bento_ui_set_text(screen->ui, screen->set_point, text,
                            (uint32_t)strlen(text));
}

static void show_finger(const jog_screen_t *screen, const char *text,
                        uint32_t color)
{
    (void)bento_ui_set_text(screen->ui, screen->finger, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_color(screen->ui, screen->finger, color);
}

/* The rows below the buttons: the set point, the event, what it
 * means; and the console. */
static void show_event(const jog_screen_t *screen,
                       const bento_ui_event_t *event, int32_t set_point)
{
    const event_kind_t *kind = find_kind(event->type);
    const char *meaning =
        (kind->meaning != NULL) ? kind->meaning : "ชนิดที่ยังไม่รู้จัก";
    char text[TEXT_BYTES];

    show_number(screen, set_point);
    (void)snprintf(text, sizeof(text), "%s  value=%" PRId32, kind->name,
                   event->value);
    (void)bento_ui_set_text(screen->ui, screen->last, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_color(screen->ui, screen->last,
                             kind_is(event, BENTO_UI_EVENT_RELEASED)
                                 ? COLOR_OK
                                 : COLOR_TEXT);
    (void)bento_ui_set_text(screen->ui, screen->why, meaning,
                            (uint32_t)strlen(meaning));
    (void)snprintf(text, sizeof(text),
                   "%s value=%" PRId32 " -> %" PRId32, kind->name,
                   event->value, set_point);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
}

/* A jog event moves the set point: pressed by one step (this is where a
 * real machine starts, not at clicked, which comes too late), a repeat
 * by its count; released and press_lost both mean stop, or a finger
 * slid off the button would leave the machine running. */
static int32_t jog(const jog_screen_t *screen,
                   const bento_ui_event_t *event, int32_t set_point)
{
    const int32_t step = (event->id == screen->up) ? 1 : -1;
    int32_t moved = set_point;

    if (kind_is(event, BENTO_UI_EVENT_PRESSED)) {
        moved = clamp(set_point + step);
        show_finger(screen, "นิ้ว: กดอยู่", COLOR_ACCENT);
    } else if (kind_is(event, BENTO_UI_EVENT_LONG_PRESSED_REPEAT)) {
        /* the firmware folds the repeats into one queue place and
         * counts them in value */
        moved = clamp(set_point + (step * event->value));
    } else if (kind_is(event, BENTO_UI_EVENT_RELEASED) ||
               kind_is(event, BENTO_UI_EVENT_PRESS_LOST)) {
        show_finger(screen, "นิ้ว: ไม่ได้แตะ", COLOR_DIM);
    } else {
        /* any other kind only shows what it is */
    }
    return moved;
}

/* One event: the reset button, a jog button, or something this program
 * does not handle. Returns the set point after it. MicroPython's widget
 * setters ignore a refusal (modui.c), so the twin does too. */
static int32_t handle_event(const jog_screen_t *screen,
                            const bento_ui_event_t *event,
                            int32_t set_point)
{
    int32_t moved = set_point;

    if ((event->id == screen->reset) &&
        kind_is(event, BENTO_UI_EVENT_CLICKED)) {
        moved = SET_START;
        show_number(screen, moved);
    } else if ((event->id == screen->up) ||
               (event->id == screen->down)) {
        moved = jog(screen, event, set_point);
        show_event(screen, event, moved);
    } else {
        /* not a jog button: nothing to do */
    }
    return moved;
}

/* Reads the taps every LOOP_MS until Stop is pressed. */
static bento_status_t run_event_loop(const jog_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    int32_t set_point = SET_START;
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        uint32_t count = 0U;
        uint32_t i;

        /* ui.poll() answers no events when it fails, so a failed
         * poll is not an error here. */
        if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                          &count, NULL) != BENTO_OK) {
            count = 0U;
        }
        for (i = 0U; i < count; i++) {
            set_point = handle_event(screen, &events[i], set_point);
        }
        status = wait_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    jog_screen_t screen = {
        bento_runtime_get_ui(), 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ปุ่มที่ต้องกดค้าง</h2>");
        /* one text is one print node in Flow; split in two, Flow
         * would show an expression */
        (void)bento_lcd_print("pressed / released / press_lost / "
                              "long_pressed / long_pressed_repeat");
        (void)bento_lcd_print("ทั้งห้าชนิดนี้ต้องขอด้วย .listen() ก่อน "
                              "ไม่งั้นไม่มีอะไรมาถึงเลย");
        status = run_event_loop(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
