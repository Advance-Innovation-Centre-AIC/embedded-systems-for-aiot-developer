/*
 * 08_dropdown_textarea.c -- อีกสามชนิดที่รับอินพุตได้ และค่าที่ถามกลับได้จริง
 *
 * โมดูลมีของรับอินพุตหกชนิด Button Switch Slider Checkbox Dropdown Textarea
 * แต่ละชนิดส่ง event คนละแบบ แถวบนคือของให้แตะหกชิ้น แถวกลางคือ event ล่าสุด
 * แบบดิบและความหมายของมัน แถวล่างคือผลของการไล่ถาม .value() ทุกตัวพร้อมกัน
 * กับดัก: Dropdown ส่งลำดับของตัวเลือก ไม่ได้ส่งข้อความ Textarea ส่ง value 0
 * เสมอ และ .value() ที่ตอบ 0 อาจแปลว่าตอบไม่ได้ ตัวอย่างเดิน 40 วินาที
 *
 * The module has six input widgets, and each sends its own kind of
 * event. The top row holds all six to touch; the middle shows the last
 * raw event and what it means; the bottom line asks every .value() at
 * once. The traps: a Dropdown sends the index of the choice, not its
 * text; a Textarea always sends value 0; and a .value() of 0 may mean
 * "this kind cannot answer". The example runs for 40 seconds.
 *
 * Twin of 08_dropdown_textarea.py
 * (embedded-systems-for-aiot-developer/examples/s04/08_dropdown_textarea.py),
 * rewritten from the library twin aiot-s04-08-dropdown-textarea. Style:
 * CLEAN_C.md. The screen is one table of widgets in creation order;
 * the dict NAME_OF is a table of the six inputs. A .value() that
 * fails reads 0, as MicroPython's getter answers then.
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
    RUN_MS = 40000
};

enum {
    AUTO = -1, /* no w= or h= given: the widget's own size */
    CHOICE_COUNT = 3,
    ASK_COUNT = 4, /* the inputs whose .value() the CM55 answers */
    INPUT_COUNT = 6,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0x4A9EFF,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_WARN = 0xF5A623,
    COLOR_INFO = 0x4A9EFF,
    COLOR_THEME = 0 /* no color= given */
};

/* The widgets, in the order they are made. */
enum {
    W_TITLE,
    W_CARD,
    W_BUTTON,
    W_SWITCH_NAME,
    W_SWITCH,
    W_SLIDER_NAME,
    W_SLIDER,
    W_CHECKBOX,
    W_DROPDOWN_NAME,
    W_DROPDOWN,
    W_TEXTAREA_NAME,
    W_TEXTAREA,
    W_PLAIN,
    W_RAW,
    W_MEAN,
    W_POLL_LINE,
    W_INDEX_NOTE,
    W_ZERO_NOTE,
    W_COUNT
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
} form_screen_t;

/* An input widget and the name a reader understands. */
typedef struct {
    uint32_t widget;
    const char *name;
} input_t;

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

/* The name MicroPython's ui.poll() puts in ev["type"] (modui.c). */
static const char *type_text(uint8_t type)
{
    static const char *const names[] = {
        "unknown",    "clicked",      "value_changed",
        "toggled",    "pressed",      "released",
        "press_lost", "long_pressed", "long_pressed_repeat",
        "ready",      "cancel",       "focused",
        "defocused",  "scroll_begin", "scroll_end",
        "gesture"};
    const char *name = names[0];

    if ((size_t)type < (sizeof(names) / sizeof(names[0]))) {
        name = names[type];
    }
    return name;
}

/* The screen, in creation order (creation order is drawing order). */
static bento_status_t add_screen_widgets(bento_ui_t *ui, uint32_t first,
                                         uint32_t count,
                                         bento_ui_id_t *ids)
{
    /* The Dropdown's choices, one per line, the form LVGL takes. The
     * event brings back only the index, so the texts are kept here too. */
    static const char choice_text[] = "ช้า\nกลาง\nเร็ว";
    static const widget_spec_t screen_widgets[W_COUNT] = {
        {UI_WIDGET_LABEL, "หกชนิดที่รับอินพุตได้", 20, 12, AUTO, AUTO,
         COLOR_TEXT, 24, 0, 0},
        {UI_WIDGET_PANEL, NULL, 20, 48, 752, 112, COLOR_CARD, 1,
         COLOR_DIM, 12},
        /* the three this session already used, for comparison */
        {UI_WIDGET_BUTTON, "Button", 36, 88, 120, 88, COLOR_TEXT, 16, 0,
         0},
        {UI_WIDGET_LABEL, "Switch", 176, 64, AUTO, AUTO, COLOR_DIM, 16,
         0, 0},
        {UI_WIDGET_SWITCH, NULL, 176, 92, AUTO, AUTO, COLOR_THEME, 0, 0,
         0},
        {UI_WIDGET_LABEL, "Slider", 280, 64, AUTO, AUTO, COLOR_DIM, 16,
         0, 0},
        {UI_WIDGET_SLIDER, NULL, 280, 96, 180, AUTO, COLOR_THEME, 40, 0,
         100},
        /* the three this file adds */
        {UI_WIDGET_CHECKBOX, "Checkbox", 480, 64, AUTO, AUTO,
         COLOR_TEXT, 0, 0, 0},
        {UI_WIDGET_LABEL, "Dropdown", 480, 100, AUTO, AUTO, COLOR_DIM,
         16, 0, 0},
        {UI_WIDGET_DROPDOWN, choice_text, 480, 124, 140, AUTO,
         COLOR_THEME, 20, 0, 0},
        {UI_WIDGET_LABEL, "Textarea", 636, 64, AUTO, AUTO, COLOR_DIM,
         16, 0, 0},
        {UI_WIDGET_TEXTAREA, "แตะแล้วพิมพ์", 636, 100, 120, 88,
         COLOR_THEME, 20, 0, 0},
        /* takes no input at all: proof that its .value() answers 0 */
        {UI_WIDGET_LABEL, "Label ไม่รับอินพุต", 20, 170, AUTO, AUTO,
         COLOR_DIM, 20, 0, 0},
        {UI_WIDGET_LABEL, "ยังไม่มีเหตุการณ์เข้ามา", 20, 204, AUTO, AUTO,
         COLOR_INFO, 24, 0, 0},
        {UI_WIDGET_LABEL, "แตะของแถวบนทีละชิ้น", 20, 240, AUTO, AUTO,
         COLOR_DIM, 20, 0, 0},
        {UI_WIDGET_LABEL, "ยังไม่ได้ไล่ถาม value()", 20, 272, AUTO, AUTO,
         COLOR_WARN, 20, 0, 0},
        {UI_WIDGET_LABEL, "Dropdown ส่งลำดับ ไม่ได้ส่งข้อความ", 20, 302, AUTO,
         AUTO, COLOR_DIM, 20, 0, 0},
        {UI_WIDGET_LABEL, "value() ที่ตอบ 0 อาจแปลว่าตอบไม่ได้", 20, 332,
         AUTO, AUTO, COLOR_DIM, 20, 0, 0}};

    return add_widgets(ui, &screen_widgets[first], count, &ids[first]);
}

static bento_status_t build_screen(form_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status =
            add_screen_widgets(screen->ui, 0U, W_COUNT, screen->ids);
    }
    return status;
}

/* .value() asks the CM55 across IPC; a failed ask reads 0. */
static int32_t ask_value(const form_screen_t *screen, uint32_t widget)
{
    int32_t value = 0;

    if (bento_ui_get_value(screen->ui, screen->ids[widget], &value) !=
        BENTO_OK) {
        value = 0;
    }
    return value;
}

/* Asks every .value() in one round and reports it on one line. Each
 * ask is a round trip to the CM55, so it runs only when an event came
 * in, never on every loop. Arc and Bar answer too; they come in
 * sessions 5 and 6. */
static void sweep_values(const form_screen_t *screen)
{
    static const input_t asked[ASK_COUNT] = {{W_SLIDER, "Slider"},
                                             {W_SWITCH, "Switch"},
                                             {W_CHECKBOX, "Checkbox"},
                                             {W_DROPDOWN, "Dropdown"}};
    char text[TEXT_BYTES];
    size_t used = 0U;
    uint32_t i;

    text[0] = '\0';
    for (i = 0U; i < (uint32_t)ASK_COUNT; i++) {
        (void)snprintf(&text[used], sizeof(text) - used,
                       "%s%.2s=%" PRId32, (i == 0U) ? "" : " ",
                       asked[i].name,
                       ask_value(screen, asked[i].widget));
        used = strlen(text);
    }
    (void)snprintf(&text[used], sizeof(text) - used, "  Label=%" PRId32,
                   ask_value(screen, W_PLAIN));
    show_text(screen->ui, screen->ids[W_POLL_LINE], text);
}

/* The name of the input an event came from, or "ไม่รู้จัก". */
static const char *input_name(const form_screen_t *screen,
                              bento_ui_id_t widget)
{
    static const input_t inputs[INPUT_COUNT] = {
        {W_BUTTON, "Button"},     {W_SWITCH, "Switch"},
        {W_SLIDER, "Slider"},     {W_CHECKBOX, "Checkbox"},
        {W_DROPDOWN, "Dropdown"}, {W_TEXTAREA, "Textarea"}};
    const char *name = "ไม่รู้จัก";
    uint32_t i;

    for (i = 0U; i < (uint32_t)INPUT_COUNT; i++) {
        if (screen->ids[inputs[i].widget] == widget) {
            name = inputs[i].name;
        }
    }
    return name;
}

/* What a value_changed means depends on who sent it: a Dropdown's
 * value is an index into the choices, a Textarea's is always 0. */
static void explain_change(const form_screen_t *screen,
                           const bento_ui_event_t *event, char *text,
                           size_t text_bytes)
{
    static const char *const choices[CHOICE_COUNT] = {"ช้า", "กลาง",
                                                      "เร็ว"};
    const int32_t value = event->value;

    if (event->id == screen->ids[W_DROPDOWN]) {
        const char *pick = "?";

        if ((value >= 0) && (value < CHOICE_COUNT)) {
            pick = choices[value];
        }
        (void)snprintf(text, text_bytes,
                       "Dropdown เลือกลำดับ %" PRId32 " = %s", value,
                       pick);
    } else if (event->id == screen->ids[W_TEXTAREA]) {
        (void)snprintf(text, text_bytes,
                       "Textarea มีคนพิมพ์ แต่ value เป็น %" PRId32, value);
    } else {
        (void)snprintf(text, text_bytes, "ค่าใหม่คือ %" PRId32, value);
    }
}

/* True when the event is of this kind; the kind passes through a
 * uint8_t, so both sides of the comparison have one type. */
static bool type_is(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

/* One branch per known kind and a last one for the rest: an else that
 * swallowed everything would hide a strange event from everyone. */
static void explain_event(const form_screen_t *screen,
                          const bento_ui_event_t *event)
{
    char text[TEXT_BYTES];
    const bento_ui_id_t mean = screen->ids[W_MEAN];

    if (type_is(event, BENTO_UI_EVENT_CLICKED)) {
        show_text(screen->ui, mean, "กดแล้วปล่อย - value ไม่ได้ใช้");
    } else if (type_is(event, BENTO_UI_EVENT_TOGGLED)) {
        (void)snprintf(text, sizeof(text), "สลับเป็น %s",
                       (event->value == 1) ? "เปิด" : "ปิด");
        show_text(screen->ui, mean, text);
    } else if (type_is(event, BENTO_UI_EVENT_VALUE_CHANGED)) {
        explain_change(screen, event, text, sizeof(text));
        show_text(screen->ui, mean, text);
    } else {
        (void)snprintf(text, sizeof(text), "ชนิด %s - จดไว้ อย่าเดาแทนมัน",
                       type_text(event->type));
        show_text(screen->ui, mean, text);
        (void)bento_ui_set_color(screen->ui, mean, COLOR_WARN);
    }
}

/* The three fields are always there: print them raw first, then say
 * what they mean. */
static void handle_event(const form_screen_t *screen,
                         const bento_ui_event_t *event)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%s %s value=%" PRId32,
                   input_name(screen, event->id),
                   type_text(event->type), event->value);
    show_text(screen->ui, screen->ids[W_RAW], text);
    explain_event(screen, event);
    /* lcd.print() ignores a console that is not ready */
    (void)bento_lcd_print(text);
    sweep_values(screen);
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static void handle_events(const form_screen_t *screen)
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

static bento_status_t run_loop(const form_screen_t *screen)
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

/* The first evidence: a Label and a Textarea answer 0 with nothing to
 * answer, a 0 that means something else than a Slider's 0. */
static void show_zeros(const form_screen_t *screen)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "Label.value() = %" PRId32 " - ตอบ 0 เพราะตอบไม่ได้",
                   ask_value(screen, W_PLAIN));
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "Textarea.value() = %" PRId32 " - ตอบ 0 เหมือนกัน",
                   ask_value(screen, W_TEXTAREA));
    (void)bento_lcd_print(text);
    (void)bento_lcd_print("<span class=muted>0 สองตัวนี้คนละความหมายกับ 0 "
                          "ของ Slider</span>");
}

int main(void)
{
    form_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        /* lcd calls ignore a console that is not ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>หกชนิดที่รับอินพุตได้</h2>");
        (void)bento_lcd_print(
            "Button=clicked - Switch/Checkbox=toggled");
        (void)bento_lcd_print("Slider/Dropdown/Textarea=value_changed");
        sweep_values(&screen);
        show_zeros(&screen);
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        show_text(screen.ui, screen.ids[W_RAW], "หมดเวลาแล้ว");
        show_text(screen.ui, screen.ids[W_MEAN],
                  "จบ - หกชนิด สามแบบของ event");
        sweep_values(&screen);
        (void)bento_lcd_print("<span class=ok>สรุป: ต้องรู้ว่าแต่ละชนิดส่งอะไร "
                              "ก่อนไปรอมัน</span>");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
