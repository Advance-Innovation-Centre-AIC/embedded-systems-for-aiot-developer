/*
 * 13_confirm_before_acting.c -- สั่งของจริง ต้องยืนยันก่อน
 *
 * ปุ่มสั่งงานแยกเปิดกับปิดเป็นคนละใบใน ButtonMatrix แตะแล้วกล่องยืนยัน (MsgBox)
 * ด้านขวาบอกสิ่งที่จะเกิด ไม่ใช่แค่ถามว่ายืนยันไหม กดตกลงในกล่องแล้วคำสั่ง
 * จึงเกิดผล บรรทัดสถานะบอกว่าพัดลมหมุนอยู่หรือปิดอยู่ ตัวอย่างเดิน 30 วินาที
 *
 * The command buttons keep "on" and "off" apart, one each, in a
 * ButtonMatrix. Tap one and the confirmation box (MsgBox) on the right
 * says what will happen, rather than only asking "sure?". Only OK in
 * the box carries the command out, and the state line then says
 * whether the fan runs. The example runs for 30 seconds.
 *
 * Twin of 13_confirm_before_acting.py
 * (embedded-systems-for-aiot-developer/examples/s04/13_confirm_before_acting.py),
 * rewritten from the library twin aiot-s04-13-confirm-before-acting.
 * Style: CLEAN_C.md. The dict ACTION is a table indexed by the
 * button's number; "pending = None" is a has_pending flag.
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
    LOOP_MS = 50,
    RUN_MS = 30000
};

enum {
    AUTO = -1, /* no w= or h= given: the widget's own size */
    ACTION_COUNT = 2,
    ACTION_ON = 0,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_THEME = 0 /* no color= given */
};

/* The widgets, in the order they are made. */
enum {
    W_TITLE,
    W_STATE,
    W_COMMAND_CAPTION,
    W_MATRIX,
    W_BOX_CAPTION,
    W_BOX,
    W_HINT,
    W_COUNT
};

/* A ButtonMatrix sends value_changed with the button's number from 0,
 * like a Dropdown, never its text: the program keeps its own table. */
typedef struct {
    const char *what;
    const char *why;
} action_t;

static const action_t ACTIONS[ACTION_COUNT] = {
    {"เปิด", "พัดลมจะเริ่มหมุนทันที"}, {"ปิด", "พัดลมจะหยุดหมุนทันที"}};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
    bool has_pending;
    int32_t pending;
} confirm_screen_t;

/* One widget as the original makes it: its factory, its text and its
 * keyword arguments. value= is a Label's or Button's font size and a
 * control's start value; min= and max= are a control's range (a
 * Panel's border colour and corner radius). */
typedef struct {
    uint8_t type;
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value;
    int32_t min;
    int32_t max;
} widget_spec_t;

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

static bento_status_t add_widget(bento_ui_t *ui,
                                 const widget_spec_t *spec,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    if (spec->text != NULL) {
        args.text = spec->text;
        args.text_len = (uint32_t)strlen(spec->text);
    }
    args.x = spec->x;
    args.y = spec->y;
    args.w = spec->w;
    args.h = spec->h;
    args.color = spec->color;
    args.font_px = spec->value;
    args.min_val = spec->min;
    args.max_val = spec->max;
    return bento_ui_create(ui, spec->type, &args, out_id);
}

/* Makes the widgets in table order (creation order is drawing order)
 * and keeps each id; the first refusal ends it, as a raise does in
 * MicroPython. */
static bento_status_t add_widgets(bento_ui_t *ui,
                                  const widget_spec_t *table,
                                  uint32_t count, bento_ui_id_t *ids)
{
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = add_widget(ui, &table[i], &ids[i]);
    }
    return status;
}

/* The screen, in creation order. The box's title and body are split
 * by a new line. */
static const widget_spec_t SCREEN_WIDGETS[W_COUNT] = {
    {UI_WIDGET_LABEL, "สั่งของจริง ต้องยืนยันก่อน", 24, 16, AUTO, AUTO,
     COLOR_TEXT, 28, 0, 0},
    {UI_WIDGET_LABEL, "ตอนนี้: พัดลมปิดอยู่", 24, 64, AUTO, AUTO, COLOR_TEXT,
     24, 0, 0},
    {UI_WIDGET_LABEL, "สั่งงาน", 24, 112, AUTO, AUTO, COLOR_DIM, 20, 0,
     0},
    {UI_WIDGET_BTNMATRIX, NULL, 24, 144, 352, 96, COLOR_THEME, 0, 0, 0},
    {UI_WIDGET_LABEL, "กล่องยืนยัน", 416, 112, AUTO, AUTO, COLOR_DIM, 20,
     0, 0},
    {UI_WIDGET_MSGBOX, "ยืนยันคำสั่ง\nพัดลมจะเริ่มหมุนทันที", 416, 144, 352, AUTO,
     COLOR_THEME, 0, 0, 0},
    {UI_WIDGET_LABEL, "แตะปุ่มซ้าย แล้วอ่านกล่องขวาก่อนตัดสินใจ", 24, 264, AUTO,
     AUTO, COLOR_DIM, 20, 0, 0}};

/* ui.poll() here only lets the screen draw; the events are not used. */
static void let_screen_draw(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* Separate on and off buttons; the box gets one OK. add_button never
 * raises in MicroPython, so its status is not checked. */
static void add_buttons(const confirm_screen_t *screen)
{
    static const char *const commands[ACTION_COUNT] = {"เปิดพัดลม",
                                                       "ปิดพัดลม"};
    uint32_t i;

    for (i = 0U; i < (uint32_t)ACTION_COUNT; i++) {
        (void)bento_ui_add_button(
            screen->ui, screen->ids[W_MATRIX], commands[i],
            (uint32_t)strlen(commands[i]), false, 0U);
    }
    (void)bento_ui_add_button(screen->ui, screen->ids[W_BOX], "ตกลง",
                              (uint32_t)strlen("ตกลง"), false, 0U);
}

static bento_status_t build_screen(confirm_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_widgets(screen->ui, SCREEN_WIDGETS, W_COUNT,
                             screen->ids);
    }
    if (status == BENTO_OK) {
        add_buttons(screen);
        let_screen_draw(screen->ui);
    }
    return status;
}

/* The action of a button number, or the "unknown" row. */
static action_t find_action(int32_t number, const char *unknown_why)
{
    action_t action;

    /* filled field by field: a local initialiser holding a text would
     * leave a template in .data on the PSoC Edge */
    action.what = "ไม่รู้จัก";
    action.why = unknown_why;
    if ((number >= 0) && (number < ACTION_COUNT)) {
        action = ACTIONS[number];
    }
    return action;
}

/* The confirmation says what will happen, not "are you sure". Text
 * over 126 bytes is cut, and a Thai letter is 3 bytes, so both lines
 * together stay under 42 letters. */
static void ask_to_confirm(confirm_screen_t *screen, int32_t number)
{
    const action_t action = find_action(number, "คำสั่งนี้ไม่มีในตาราง");
    char text[TEXT_BYTES];

    screen->has_pending = true;
    screen->pending = number;
    (void)snprintf(text, sizeof(text), "ยืนยัน %s\n%s", action.what,
                   action.why);
    show_text(screen->ui, screen->ids[W_BOX], text);
    show_text(screen->ui, screen->ids[W_HINT],
              "กำลังรอการยืนยัน - กดตกลงในกล่องขวา");
    (void)bento_ui_set_color(screen->ui, screen->ids[W_HINT],
                             COLOR_ACCENT);
    (void)snprintf(text, sizeof(text), "ขอยืนยัน: %s", action.why);
    /* lcd.print() ignores a console that is not ready */
    (void)bento_lcd_print(text);
}

static void carry_out(confirm_screen_t *screen)
{
    const action_t action = find_action(screen->pending, "");
    char text[TEXT_BYTES];

    show_text(screen->ui, screen->ids[W_STATE],
              (screen->pending == ACTION_ON) ? "ตอนนี้: พัดลมหมุนอยู่"
                                             : "ตอนนี้: พัดลมปิดอยู่");
    show_text(screen->ui, screen->ids[W_HINT],
              "ทำคำสั่งแล้ว - แตะปุ่มซ้ายเพื่อสั่งใหม่");
    (void)bento_ui_set_color(screen->ui, screen->ids[W_HINT],
                             COLOR_DIM);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ทำแล้ว: %s</span>", action.why);
    (void)bento_lcd_print(text);
    screen->has_pending = false;
}

/* Any event from the box counts as its answer, but only while a
 * command waits. */
static void handle_event(confirm_screen_t *screen,
                         const bento_ui_event_t *event)
{
    if ((event->id == screen->ids[W_MATRIX]) &&
        (event->type == BENTO_UI_EVENT_VALUE_CHANGED)) {
        ask_to_confirm(screen, event->value);
    } else if ((event->id == screen->ids[W_BOX]) &&
               screen->has_pending) {
        carry_out(screen);
    } else {
        /* nothing else changes anything */
    }
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static void handle_events(confirm_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        handle_event(screen, &events[i]);
    }
}

static bento_status_t run_loop(confirm_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        handle_events(screen);
        status = pause_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    confirm_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>MsgBox + ButtonMatrix</h2>");
        (void)printf("MsgBox + ButtonMatrix พร้อมให้ยืนยัน\n");
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_print("<span class=muted>หมดเวลา</span>");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
