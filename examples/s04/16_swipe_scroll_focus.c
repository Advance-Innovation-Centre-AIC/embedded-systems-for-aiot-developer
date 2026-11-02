/*
 * 16_swipe_scroll_focus.c -- ปัดนิ้ว เลื่อนรายการ และช่องไหนกำลังถูกเลือก
 *
 * เหตุการณ์สามกลุ่มที่ไม่ได้เกิดจากการกด: ปัดนิ้วบนแผ่นพื้นด้านซ้าย (gesture)
 * แล้วป้ายบอกทิศ เลื่อนรายชื่อด้านขวา (scroll_begin / scroll_end) แล้วป้ายบอก
 * ว่ากำลังเลื่อนหรือหยุดแล้ว แตะสองปุ่มล่างหรือรายชื่อ (focused / defocused)
 * แล้วป้ายบอกว่าโฟกัสอยู่ที่ไหน ทุกเหตุการณ์ลงคอนโซล ทำงานจนกดหยุด
 *
 * Three groups of events that come from no press: swipe on the left pad
 * (gesture) and a label names the direction; scroll the list on the
 * right (scroll_begin / scroll_end) and a label says whether it moves
 * or has settled; tap the two buttons below or the list (focused /
 * defocused) and a label says where the focus is. Every event goes to
 * the console. Runs until Stop.
 *
 * Twin of 16_swipe_scroll_focus.py
 * (embedded-systems-for-aiot-developer/examples/s04/16_swipe_scroll_focus.py),
 * from the library twin aiot-s04-16-swipe-scroll-focus. Style:
 * CLEAN_C.md. The event's kind is printed by its name and compared
 * only as UI_EVENT_*. Left out on purpose: the original's `scrolling`,
 * which it sets but never reads.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 30,    /* read the events this often */
    TEXT_BYTES = 80  /* the longest text built here is 68 bytes */
};

/* The course palette, 0xRRGGBB: one value per role. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_BUTTON = 0x3A4150
};

/* The widgets the program changes, and the four whose events it
 * names. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t pad;
    bento_ui_id_t direction;
    bento_ui_id_t list;
    bento_ui_id_t scroll;
    bento_ui_id_t focus;
    bento_ui_id_t button_a;
    bento_ui_id_t button_b;
} swipe_screen_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The name MicroPython's ui.poll() gives an event's kind; it is
 * printed, never compared. */
static const char *kind_name(uint8_t kind)
{
    static const struct {
        uint8_t kind;
        const char *name;
    } names[] = {
        {BENTO_UI_EVENT_CLICKED, "clicked"},
        {BENTO_UI_EVENT_VALUE_CHANGED, "value_changed"},
        {BENTO_UI_EVENT_TOGGLED, "toggled"},
        {BENTO_UI_EVENT_PRESSED, "pressed"},
        {BENTO_UI_EVENT_RELEASED, "released"},
        {BENTO_UI_EVENT_PRESS_LOST, "press_lost"},
        {BENTO_UI_EVENT_LONG_PRESSED, "long_pressed"},
        {BENTO_UI_EVENT_LONG_PRESSED_REPEAT, "long_pressed_repeat"},
        {BENTO_UI_EVENT_READY, "ready"},
        {BENTO_UI_EVENT_CANCEL, "cancel"},
        {BENTO_UI_EVENT_FOCUSED, "focused"},
        {BENTO_UI_EVENT_DEFOCUSED, "defocused"},
        {BENTO_UI_EVENT_SCROLL_BEGIN, "scroll_begin"},
        {BENTO_UI_EVENT_SCROLL_END, "scroll_end"},
        {BENTO_UI_EVENT_GESTURE, "gesture"},
        /* the last entry answers every other kind */
        {0U, "unknown"}};
    const uint32_t last =
        (uint32_t)(sizeof(names) / sizeof(names[0])) - 1U;
    uint32_t i = 0U;

    while ((i < last) && (names[i].kind != kind)) {
        i++;
    }
    return names[i].name;
}

/* The direction a gesture carries, compared with the ui module's
 * UI_DIR_* values. */
static const char *direction_name(int32_t direction)
{
    static const struct {
        int32_t direction;
        const char *name;
    } directions[] = {{UI_DIR_LEFT, "ปัดไปทางซ้าย"},
                      {UI_DIR_RIGHT, "ปัดไปทางขวา"},
                      {UI_DIR_TOP, "ปัดขึ้น"},
                      {UI_DIR_BOTTOM, "ปัดลง"}};
    const uint32_t count =
        (uint32_t)(sizeof(directions) / sizeof(directions[0]));
    const char *name = "ทิศที่อ่านไม่ออก";
    uint32_t i;

    for (i = 0U; i < count; i++) {
        if (directions[i].direction == direction) {
            name = directions[i].name;
        }
    }
    return name;
}

/* True when the event is of this kind (a uint8_t, so both sides of
 * == have one type). */
static bool kind_is(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

/* Layout on the 792 x 398 canvas: the pad on the left, the list on
 * the right. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    PAD_Y = 56,
    PAD_W = 392,
    PAD_H = 200,
    PAD_TEXT_X = 48,
    PAD_CAPTION_Y = 72,
    DIRECTION_Y = 112,
    PAD_HINT_Y = 168,
    LIST_X = 448,
    LIST_Y = 56,
    LIST_W = 320,
    LIST_H = 232,
    SCROLL_Y = 304,
    FOCUS_Y = 344,
    BUTTON_Y = 280,
    BUTTON_W = 200,
    BUTTON_H = 88,
    BUTTON_B_X = 240,
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
                                rect_t place, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place.x;
    args.y = place.y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 int16_t x, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = BUTTON_Y;
    args.w = BUTTON_W;
    args.h = BUTTON_H;
    args.color = COLOR_BUTTON;
    args.font_px = BUTTON_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* The swipe pad: it must not scroll itself, or LVGL takes the swipe
 * for a scroll. */
static bento_status_t add_pad(swipe_screen_t *screen)
{
    const rect_t caption = {PAD_TEXT_X, PAD_CAPTION_Y, 0, 0};
    const rect_t direction = {PAD_TEXT_X, DIRECTION_Y, 0, 0};
    const rect_t hint = {PAD_TEXT_X, PAD_HINT_Y, 0, 0};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = PAD_Y;
    args.w = PAD_W;
    args.h = PAD_H;
    args.color = COLOR_CARD;
    status = bento_ui_panel_create(screen->ui, &args, &screen->pad);
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ปัดนิ้วตรงนี้", caption, COLOR_DIM,
                           TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่ได้ปัด", direction,
                           COLOR_TEXT, BIG_PX, &screen->direction);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "ซ้าย ขวา ขึ้น ลง - "
                           "ลองให้ครบสี่ทิศ",
                           hint, COLOR_DIM, TEXT_PX, &unused);
    }
    return status;
}

/* The list scrolls itself, so it is the one widget here that sends
 * scroll_begin / scroll_end (a Roller cannot: LVGL does not count it as
 * scrollable). */
static bento_status_t add_list(swipe_screen_t *screen)
{
    static const char *const cities[] = {"กรุงเทพ", "เชียงใหม่", "ขอนแก่น",
                                         "ภูเก็ต", "สงขลา"};
    bento_ui_widget_args_t args;
    bento_status_t status;
    uint32_t i;

    bento_ui_widget_args_init(&args);
    args.x = LIST_X;
    args.y = LIST_Y;
    args.w = LIST_W;
    args.h = LIST_H;
    args.color = COLOR_TEXT;
    args.font_px = TEXT_PX;
    status = bento_ui_list_create(screen->ui, &args, &screen->list);
    for (i = 0U; (i < (uint32_t)(sizeof(cities) / sizeof(cities[0]))) &&
                 (status == BENTO_OK);
         i++) {
        bento_ui_item_t item;

        (void)memset(&item, 0, sizeof(item));
        item.struct_size = (uint32_t)sizeof(item);
        item.text = cities[i];
        item.text_len = (uint32_t)strlen(cities[i]);
        /* .add_item() sends without an answer and ignores a refusal
         * (modui.c) */
        (void)bento_ui_item_append(screen->ui, screen->list, &item);
    }
    return status;
}

/* Ask only for what is read: every kind asked for takes places in the
 * queue. The buttons ask for focused / defocused to show that a tap
 * elsewhere moves the focus, with no lv_group and no keyboard.
 * .listen() ignores the display's answer (modui.c), so the twin does
 * too. */
static void listen_to_moves(const swipe_screen_t *screen)
{
    const uint32_t focus_moves =
        BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_FOCUSED) |
        BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_DEFOCUSED);

    (void)bento_ui_listen_compat(screen->ui, screen->button_a,
                                 focus_moves);
    (void)bento_ui_listen_compat(screen->ui, screen->button_b,
                                 focus_moves);
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
/* The two buttons below, which ask for focus events. */
static bento_status_t add_buttons(swipe_screen_t *screen)
{
    bento_status_t status =
        add_button(screen->ui, "ช่อง ก", MARGIN_X, &screen->button_a);

    if (status == BENTO_OK) {
        status = add_button(screen->ui, "ช่อง ข", BUTTON_B_X,
                            &screen->button_b);
    }
    if (status == BENTO_OK) {
        listen_to_moves(screen);
    }
    return status;
}

static bento_status_t build_screen(swipe_screen_t *screen)
{
    const rect_t title = {MARGIN_X, TITLE_Y, 0, 0};
    const rect_t scroll = {LIST_X, SCROLL_Y, 0, 0};
    const rect_t focus = {LIST_X, FOCUS_Y, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "ปัดนิ้ว เลื่อนรายการ "
                           "และช่องที่ถูกเลือก",
                           title, COLOR_TEXT, BIG_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_pad(screen);
    }
    if (status == BENTO_OK) {
        status = add_list(screen);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_listen_compat(
            screen->ui, screen->pad,
            BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_GESTURE));
        (void)bento_ui_listen_compat(
            screen->ui, screen->list,
            BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_SCROLL_BEGIN) |
                BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_SCROLL_END) |
                BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_FOCUSED));
        status = add_label(screen->ui, "รายชื่อ: ยังไม่ได้เลื่อน", scroll,
                           COLOR_DIM, TEXT_PX, &screen->scroll);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "โฟกัส: ยังไม่มี", focus, COLOR_DIM,
                           TEXT_PX, &screen->focus);
    }
    if (status == BENTO_OK) {
        status = add_buttons(screen);
    }
    return status;
}

static void show(const swipe_screen_t *screen, bento_ui_id_t label,
                 const char *text, uint32_t color)
{
    (void)bento_ui_set_text(screen->ui, label, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_color(screen->ui, label, color);
}

/* Which of the four named widgets sent the event; the original reads
 * only ev["handle"] here, so only the id is compared. */
static const char *widget_name(const swipe_screen_t *screen,
                               bento_ui_id_t widget)
{
    const char *name = "?";

    if (widget == screen->pad) {
        name = "แผ่นปัด";
    } else if (widget == screen->list) {
        name = "รายชื่อ";
    } else if (widget == screen->button_a) {
        name = "ช่อง ก";
    } else if (widget == screen->button_b) {
        name = "ช่อง ข";
    } else {
        /* not one of the four */
    }
    return name;
}

/* One event: a label follows its kind, and the console gets every
 * event. MicroPython's widget setters ignore a refusal (modui.c), so
 * the twin does too. scroll_end may come more than once per swipe (LVGL
 * reports each axis, and again when the animation stops): it means
 * "settled", and only then is the chosen value worth sending anywhere.
 */
static void handle_event(const swipe_screen_t *screen,
                         const bento_ui_event_t *event)
{
    const char *who = widget_name(screen, event->id);
    char text[TEXT_BYTES];

    if (kind_is(event, BENTO_UI_EVENT_GESTURE)) {
        show(screen, screen->direction, direction_name(event->value),
             COLOR_ACCENT);
    } else if (kind_is(event, BENTO_UI_EVENT_SCROLL_BEGIN)) {
        show(screen, screen->scroll, "รายชื่อ: กำลังเลื่อน", COLOR_ACCENT);
    } else if (kind_is(event, BENTO_UI_EVENT_SCROLL_END)) {
        show(screen, screen->scroll, "รายชื่อ: หยุดแล้ว", COLOR_OK);
    } else if (kind_is(event, BENTO_UI_EVENT_FOCUSED)) {
        (void)snprintf(text, sizeof(text), "โฟกัส: %s", who);
        show(screen, screen->focus, text, COLOR_TEXT);
    } else if (kind_is(event, BENTO_UI_EVENT_DEFOCUSED)) {
        (void)snprintf(text, sizeof(text), "โฟกัสออกจาก %s แล้ว", who);
        show(screen, screen->focus, text, COLOR_DIM);
    } else {
        /* any other kind is only printed */
    }
    (void)snprintf(text, sizeof(text), "%s %s value=%" PRId32, who,
                   kind_name(event->type), event->value);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
}

/* Reads the events every LOOP_MS until Stop is pressed. */
static bento_status_t run_event_loop(const swipe_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
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
            handle_event(screen, &events[i]);
        }
        status = wait_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    swipe_screen_t screen = {
        bento_runtime_get_ui(), 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>gesture / scroll / focus</h2>");
        (void)bento_lcd_print(
            "gesture ไม่เกิดบนของที่เลื่อนได้ - ปัดบน Panel เท่านั้น");
        (void)bento_lcd_print("Roller ไม่ส่ง scroll เลย - "
                              "มันไม่ใช่ของที่เลื่อนได้ในสายตา LVGL");
        status = run_event_loop(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
