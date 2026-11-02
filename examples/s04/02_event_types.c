/*
 * 02_event_types.c -- เหตุการณ์หน้าตาเป็นอย่างไร และใครส่งอะไร
 *
 * ตัวควบคุมแต่ละชนิดส่ง type ไม่เหมือนกัน ต้องรู้ก่อนว่าจะรออะไร แถวบนมีของจริง
 * สี่ตัวให้แตะ (Button, Switch, Checkbox, Slider) ใต้ลงมาคือคำสั่งของขั้นนี้
 * และบรรทัดของเหตุการณ์ล่าสุดที่เพิ่งเข้ามา แตะ "เดินหน้า" ไปขั้นถัดไป
 * แตะ "เริ่มใหม่" กลับขั้นแรก กับดัก: Checkbox ส่ง toggled ไม่ใช่ clicked
 *
 * Each kind of control sends a different event type, so know what to
 * wait for. Four real controls sit on the top row; below them are the
 * instruction of this step and the line of the last event that came
 * in. Each event is judged against the step and written to the screen
 * and the Console. "Next" moves to the next step, "restart" goes back
 * to the first. The trap: a Checkbox sends toggled, not clicked.
 *
 * Twin of 02_event_types.py
 * (embedded-systems-for-aiot-developer/examples/s04/02_event_types.py),
 * rewritten from the library twin aiot-s04-02-event-types. Style:
 * CLEAN_C.md. The dict NAME_OF and the list STEPS become one table
 * in the order of the four controls; an event is judged by its type
 * code and widget id, and its type name is looked up only to be shown.
 * The Python loops for ever; here Stop ends the loop.
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
    /* short: ui.poll() does not run while the program sleeps */
    LOOP_MS = 30
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    POSITION_X = 600,
    POSITION_Y = 16,
    NAME_Y = 56,
    CONTROL_Y = 96,
    CONTROL_H = 88, /* the smallest touch target */
    BUTTON_W = 160,
    SWITCH_X = 216,
    SWITCH_W = 120,
    CHECKBOX_X = 368,
    CHECKBOX_W = 176,
    CHECKBOX_TEXT_X = 396,
    CHECKBOX_TEXT_Y = 90,
    SLIDER_X = 576,
    SLIDER_W = 192,
    SLIDER_H = 24,
    SLIDER_MAX = 100,
    SLIDER_START = 30,
    ASK_Y = 192,
    WHY_Y = 232,
    LAST_Y = 264,
    NAV_Y = 304,
    NAV_W = 200,
    HOME_X = 256,
    FONT_HEADING_PX = 28,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BUTTON_ALT = 0x3A4150 /* the secondary button */
};

/* The four controls, in the order of the steps. */
enum {
    CONTROL_BUTTON = 0,
    CONTROL_SWITCH = 1,
    CONTROL_CHECKBOX = 2,
    CONTROL_SLIDER = 3,
    CONTROL_COUNT = 4
};

/* One step: the control to touch, the type it must send and why.
 * The answer is written before the touch, so the learner predicts and
 * then checks. */
typedef struct {
    const char *who;
    uint8_t want_type;
    const char *reason;
} step_t;

static const step_t STEPS[CONTROL_COUNT] = {
    {"Button", BENTO_UI_EVENT_CLICKED,
     "ปุ่มส่ง clicked ค่า value เป็น 1 เสมอ"},
    {"Switch", BENTO_UI_EVENT_TOGGLED,
     "สวิตช์ส่ง toggled ค่า 1 ตอนเปิด 0 ตอนปิด"},
    {"Checkbox", BENTO_UI_EVENT_TOGGLED,
     "กล่องติ๊กส่ง toggled ไม่ใช่ clicked ทั้งที่หน้าตาเหมือนของกด"},
    {"Slider", BENTO_UI_EVENT_VALUE_CHANGED,
     "แถบเลื่อนส่ง value_changed และส่งถี่มากระหว่างที่นิ้วยังลากอยู่"},
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

/* The widgets the program changes or listens to, and the step. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t position;
    bento_ui_id_t ask;
    bento_ui_id_t why;
    bento_ui_id_t last;
    bento_ui_id_t controls[CONTROL_COUNT];
    bento_ui_id_t next;
    bento_ui_id_t home;
    int32_t step;
} events_screen_t;

/* The name the course shows for an event type, the one MicroPython's
 * ui.poll() puts in ev["type"] (modui.c). Shown only, never compared. */
static const char *type_text(uint8_t type)
{
    static const char *const NAMES[] = {
        "unknown",    "clicked",      "value_changed",
        "toggled",    "pressed",      "released",
        "press_lost", "long_pressed", "long_pressed_repeat",
        "ready",      "cancel",       "focused",
        "defocused",  "scroll_begin", "scroll_end",
        "gesture"};
    const char *name = NAMES[0];

    if ((size_t)type < (sizeof(NAMES) / sizeof(NAMES[0]))) {
        name = NAMES[type];
    }
    return name;
}

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

static void show_color(bento_ui_t *ui, bento_ui_id_t widget,
                       uint32_t color)
{
    (void)bento_ui_set_color(ui, widget, color);
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

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 rect_t at, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = at.x;
    args.y = at.y;
    args.w = at.w;
    args.h = at.h;
    args.color = color;
    args.font_px = FONT_TITLE_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* The four type names above the controls: the instruction names the
 * type, so the learner finds each control without guessing. */
static bento_status_t add_names(bento_ui_t *ui)
{
    static const int16_t NAME_X[CONTROL_COUNT] = {MARGIN_X, SWITCH_X,
                                                  CHECKBOX_X, SLIDER_X};
    bento_status_t status = BENTO_OK;
    bento_ui_id_t unused;
    uint32_t i;

    for (i = 0U; (i < (uint32_t)CONTROL_COUNT) && (status == BENTO_OK);
         i++) {
        const label_at_t at = {NAME_X[i], NAME_Y, FONT_TEXT_PX};

        status = add_label(ui, STEPS[i].who, at, COLOR_DIM, &unused);
    }
    return status;
}

/* The Checkbox's own text is always 16 px, too small for Thai on the
 * board, so the box is empty and a 20 px Thai label sits beside it. */
static bento_status_t add_checkbox(events_screen_t *screen)
{
    const label_at_t text_at = {CHECKBOX_TEXT_X, CHECKBOX_TEXT_Y,
                                FONT_TEXT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = " ";
    args.text_len = 1U;
    args.x = CHECKBOX_X;
    args.y = CONTROL_Y;
    args.w = CHECKBOX_W;
    args.h = CONTROL_H;
    args.color = COLOR_TEXT;
    status = bento_ui_checkbox_create(
        screen->ui, &args, &screen->controls[CONTROL_CHECKBOX]);
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "ติ๊กฉัน", text_at, COLOR_TEXT, &unused);
    }
    return status;
}

static bento_status_t add_switch_and_slider(events_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = SWITCH_X;
    args.y = CONTROL_Y;
    args.w = SWITCH_W;
    args.h = CONTROL_H;
    args.color = COLOR_ACCENT;
    status = bento_ui_switch_create(screen->ui, &args,
                                    &screen->controls[CONTROL_SWITCH]);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = SLIDER_X;
        args.y = CONTROL_Y;
        args.w = SLIDER_W;
        args.h = SLIDER_H;
        args.color = COLOR_ACCENT;
        args.min_val = 0;
        args.max_val = SLIDER_MAX;
        args.font_px = SLIDER_START;
        status = bento_ui_slider_create(
            screen->ui, &args, &screen->controls[CONTROL_SLIDER]);
    }
    return status;
}

/* One row, all 88 high and 32 apart:
 * 24 + 160 + 32 + 120 + 32 + 176 + 32 + 192 = 768, the right edge. */
static bento_status_t add_controls(events_screen_t *screen)
{
    const rect_t button = {MARGIN_X, CONTROL_Y, BUTTON_W, CONTROL_H};
    bento_status_t status = add_names(screen->ui);

    if (status == BENTO_OK) {
        status = add_button(screen->ui, "แตะฉัน", button, COLOR_ACCENT,
                            &screen->controls[CONTROL_BUTTON]);
    }
    if (status == BENTO_OK) {
        status = add_switch_and_slider(screen);
    }
    if (status == BENTO_OK) {
        status = add_checkbox(screen);
    }
    return status;
}

/* The instruction, the reason, the last event and the two navigation
 * buttons on the bottom row, which ends before the Console button. */
static bento_status_t add_lesson(events_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const label_at_t ask = {MARGIN_X, ASK_Y, FONT_TITLE_PX};
    const label_at_t why = {MARGIN_X, WHY_Y, FONT_TEXT_PX};
    const label_at_t last = {MARGIN_X, LAST_Y, FONT_TEXT_PX};
    const rect_t next = {MARGIN_X, NAV_Y, NAV_W, CONTROL_H};
    const rect_t home = {HOME_X, NAV_Y, NAV_W, CONTROL_H};
    bento_status_t status = add_label(ui, "แตะ Button แล้วต้องได้ clicked",
                                      ask, COLOR_ACCENT, &screen->ask);

    if (status == BENTO_OK) {
        status = add_label(ui, STEPS[0].reason, why, COLOR_DIM,
                           &screen->why);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ยังไม่มีเหตุการณ์เข้ามา", last, COLOR_DIM,
                           &screen->last);
    }
    if (status == BENTO_OK) {
        status =
            add_button(ui, "เดินหน้า", next, COLOR_ACCENT, &screen->next);
    }
    if (status == BENTO_OK) {
        status = add_button(ui, "เริ่มใหม่", home, COLOR_BUTTON_ALT,
                            &screen->home);
    }
    return status;
}

static bento_status_t build_screen(events_screen_t *screen)
{
    const label_at_t title = {MARGIN_X, TITLE_Y, FONT_HEADING_PX};
    /* the step counter is not a state, so it is dim, not green */
    const label_at_t position = {POSITION_X, POSITION_Y, FONT_TEXT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ใครส่งเหตุการณ์ชนิดไหน", title,
                           COLOR_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "1 / 4  Button", position,
                           COLOR_DIM, &screen->position);
    }
    if (status == BENTO_OK) {
        status = add_controls(screen);
    }
    if (status == BENTO_OK) {
        status = add_lesson(screen);
    }
    return status;
}

/* Draws the instruction of the current step, without judging yet. */
static void show_step(const events_screen_t *screen)
{
    const step_t *step = &STEPS[screen->step];
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32 " / %d  %s",
                   screen->step + 1, (int)CONTROL_COUNT, step->who);
    show_text(screen->ui, screen->position, text);
    (void)snprintf(text, sizeof(text), "แตะ %s แล้วต้องได้ %s", step->who,
                   type_text(step->want_type));
    show_text(screen->ui, screen->ask, text);
    show_color(screen->ui, screen->ask, COLOR_ACCENT);
    show_text(screen->ui, screen->why, step->reason);
}

/* The name of the control an event came from, or "?" for any other
 * widget. */
static const char *control_name(const events_screen_t *screen,
                                bento_ui_id_t widget)
{
    const char *name = "?";
    uint32_t i;

    for (i = 0U; i < (uint32_t)CONTROL_COUNT; i++) {
        if (screen->controls[i] == widget) {
            name = STEPS[i].who;
        }
    }
    return name;
}

/* Writes the raw event: handle, type and value are always there. */
static void show_event(const events_screen_t *screen,
                       const bento_ui_event_t *event, const char *got)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "handle=%" PRIu32 "  type=%s  value=%" PRId32,
                   bento_ui_id_to_handle(event->id),
                   type_text(event->type), event->value);
    show_text(screen->ui, screen->last, text);
    (void)snprintf(text, sizeof(text), "%s %s value=%" PRId32, got,
                   type_text(event->type), event->value);
    /* lcd.print() ignores a console that is not ready */
    (void)bento_lcd_print(text);
}

/* Compares the event with what this step said, then reports the
 * verdict. */
static void judge(const events_screen_t *screen,
                  const bento_ui_event_t *event)
{
    const step_t *step = &STEPS[screen->step];
    const bento_ui_id_t wanted = screen->controls[screen->step];
    const char *got = control_name(screen, event->id);
    char text[TEXT_BYTES];
    uint32_t color = COLOR_WARN;

    show_event(screen, event, got);
    if ((event->id == wanted) && (event->type == step->want_type)) {
        (void)snprintf(text, sizeof(text), "%s ส่ง %s ถูกต้องตามที่บอกไว้",
                       step->who, type_text(step->want_type));
        color = COLOR_OK;
    } else if (event->id == wanted) {
        (void)snprintf(text, sizeof(text), "%s ส่ง %s ไม่ใช่ %s",
                       step->who, type_text(event->type),
                       type_text(step->want_type));
    } else {
        (void)snprintf(text, sizeof(text), "นั่นคือ %s - ขั้นนี้ให้แตะ %s", got,
                       step->who);
    }
    show_text(screen->ui, screen->ask, text);
    show_color(screen->ui, screen->ask, color);
}

/* True when the event is of this kind and came from this widget. */
static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* The navigation buttons send clicked too, so they are handled first
 * and never judged: "next" would otherwise answer the Button step. */
static void handle_event(events_screen_t *screen,
                         const bento_ui_event_t *event)
{
    if (event_is(event, BENTO_UI_EVENT_CLICKED, screen->next)) {
        screen->step = (screen->step + 1) % CONTROL_COUNT;
        show_step(screen);
    } else if (event_is(event, BENTO_UI_EVENT_CLICKED, screen->home)) {
        screen->step = 0;
        show_step(screen);
    } else {
        judge(screen, event);
    }
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static bento_status_t run_loop(events_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        uint32_t count = 0U;
        uint32_t i;

        if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                          &count, NULL) != BENTO_OK) {
            count = 0U;
        }
        for (i = 0U; i < count; i++) {
            handle_event(screen, &events[i]);
        }
        status = pause_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    events_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        /* lcd calls ignore a console that is not ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ใครส่งเหตุการณ์ชนิดไหน</h2>");
        (void)bento_lcd_print(
            "Button=clicked - Switch/Checkbox=toggled - "
            "Slider=value_changed");
        show_step(&screen);
        status = run_loop(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
