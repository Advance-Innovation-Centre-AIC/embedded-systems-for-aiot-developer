/*
 * 02_capsense_menu_wet_hand.c -- เมนูสัมผัสสองปุ่ม และกับดักมือเปียก
 *
 * ปุ่มสัมผัส CAP1 เลื่อนแถวที่เลือกขึ้น CAP2 เลื่อนลง ในเมนูสี่แถว ทำงานตอนขอบ
 * ขาขึ้นเท่านั้น ไฟตอบรับติดตอนนิ้วแตะ และมีเสียงสั้น ๆ แถบด้านขวานับว่าแตะค้าง
 * มากี่รอบ แตะค้างครบ 40 รอบถือว่าผิดปกติ (มักมีน้ำบนแผ่น) ไฟเตือนติดและจอบอก
 * ให้เช็ดแผ่น ลูป 900 รอบ รอบละ 60 ms
 *
 * Touch button CAP1 moves the selected row up and CAP2 moves it down
 * in a four-row menu, on the rising edge only. The acknowledge LED
 * lights while a finger touches, with a short tone. The bar on the
 * right counts how many rounds a touch has lasted; 40 rounds is not
 * normal (usually water on the pad): the warning LED lights and the
 * screen says to wipe the pad. 900 rounds of 60 ms.
 *
 * Twin of 02_capsense_menu_wet_hand.py
 * (embedded-systems-for-aiot-developer/examples/s05/02_capsense_menu_wet_hand.py),
 * rewritten from the library twin aiot-s05-02-capsense-menu-wet-hand.
 * Style: CLEAN_C.md. The original calls ui.tone() without asking for a
 * sound chip; here a board without one stays silent instead of
 * raising. The LEDs are found by name as the original finds them.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

enum {
    MENU_COUNT = 4,
    STUCK_ROUNDS =
        40, /* a touch this long is not normal: water drops */
    ROUNDS = 900,
    LOOP_MS = 60,
    ROW_X = 40,
    ROW_Y = 72,
    ROW_PITCH =
        44, /* a 24 px font is about 32 tall, 12 px to part rows */
    FONT_ROW_PX = 24,
    TONE_UP = 76, /* MIDI notes */
    TONE_DOWN = 72,
    TONE_VELOCITY = 90,
    TONE_MS = 60,
    LED_WARN_FALLBACK = 0, /* Eva Kit: LED 0 red, LED 1 green */
    LED_CONFIRM_FALLBACK = 1,
    AUTO = -1,       /* no w= or h= given: the widget's own size */
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_BAD = 0xE5484D /* not normal: only for a stuck touch here */
};

/* The widgets, in the order they are made; the menu rows follow. */
enum {
    W_TITLE,
    W_MENU_CARD,
    W_TRAP_CARD,
    W_TRAP_LINE_1,
    W_TRAP_LINE_2,
    W_UP_NOTE,
    W_DOWN_NOTE,
    W_HOLD_CAPTION,
    W_HOLD_BAR,
    W_HOLD_TEXT,
    W_WARN,
    W_COUNT
};

static const char *const MENU[MENU_COUNT] = {"อุณหภูมิ", "ความเร็วพัดลม",
                                             "ตั้งเวลา", "ล้างถัง"};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
    bento_ui_id_t rows[MENU_COUNT];
    uint32_t led_warn;
    uint32_t led_confirm;
    int32_t selected;
    bool was_down[2];
    int32_t held[2];
    int32_t shown_hold;
    bool stuck;
} menu_t;

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

/* The screen on an 8 px grid: the menu card and the trap card on the
 * left, the stuck-touch gauge on the right. The trap text never
 * changes, so it is dim, not a warning colour. A Panel's value= is its
 * border width. The rows come after this table. */
static const widget_spec_t SCREEN_WIDGETS[W_COUNT] = {
    {UI_WIDGET_LABEL, "เมนูสัมผัสสองปุ่ม", 24, 8, AUTO, AUTO, COLOR_TEXT, 28,
     0, 0},
    {UI_WIDGET_PANEL, NULL, 24, 56, 432, 192, COLOR_CARD, 1, COLOR_CARD,
     12},
    {UI_WIDGET_PANEL, NULL, 24, 264, 432, 96, COLOR_CARD, 1, COLOR_CARD,
     12},
    {UI_WIDGET_LABEL, "กับดัก: น้ำบนแผ่นอ่านเหมือนนิ้ว", 40, 280, AUTO, AUTO,
     COLOR_DIM, 20, 0, 0},
    {UI_WIDGET_LABEL, "จึงพังในครัวเปียก และถุงมือหนา", 40, 312, AUTO, AUTO,
     COLOR_DIM, 20, 0, 0},
    {UI_WIDGET_LABEL, "CAP1 = เลื่อนขึ้น", 480, 56, AUTO, AUTO, COLOR_DIM,
     20, 0, 0},
    {UI_WIDGET_LABEL, "CAP2 = เลื่อนลง", 480, 88, AUTO, AUTO, COLOR_DIM,
     20, 0, 0},
    {UI_WIDGET_LABEL, "แตะค้างมาแล้วกี่รอบ", 480, 136, AUTO, AUTO,
     COLOR_DIM, 20, 0, 0},
    {UI_WIDGET_BAR, NULL, 480, 168, 208, 32, COLOR_ACCENT, 0, 0,
     STUCK_ROUNDS},
    {UI_WIDGET_LABEL, "0 / 40", 480, 208, AUTO, AUTO, COLOR_DIM, 20, 0,
     0},
    /* the normal state stays quiet: dim, not green */
    {UI_WIDGET_LABEL, "สถานะ: ปกติ", 480, 256, AUTO, AUTO, COLOR_DIM, 24,
     0, 0}};

/* ui.poll() here only lets the screen draw; the events are not used. */
static void let_screen_draw(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The index of an LED by name, if the board reports it. */
static bool find_led(const char *wanted, uint32_t *led)
{
    const uint32_t count = bento_gpio_num_leds();
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < count) && !found; i++) {
        const char *name = NULL;

        if ((bento_gpio_led_name(i, &name) == BENTO_OK) &&
            (name != NULL) && (strcmp(name, wanted) == 0)) {
            *led = i;
            found = true;
        }
    }
    return found;
}

/* A board with RGB_RED, RGB_GREEN and RGB_BLUE (the Dev Kit) is asked
 * by name, as LED numbers may change; any other uses 0 and 1. */
static void pick_leds(menu_t *menu)
{
    uint32_t red = 0U;
    uint32_t green = 0U;
    uint32_t blue = 0U;
    const bool full = find_led("RGB_RED", &red) &&
                      find_led("RGB_GREEN", &green) &&
                      find_led("RGB_BLUE", &blue);

    menu->led_warn = full ? red : (uint32_t)LED_WARN_FALLBACK;
    menu->led_confirm = full ? green : (uint32_t)LED_CONFIRM_FALLBACK;
}

/* All four rows are drawn again each time, so the whole menu shows,
 * not only the chosen line. */
static void draw_menu(const menu_t *menu)
{
    char text[TEXT_BYTES];
    int32_t i;

    for (i = 0; i < MENU_COUNT; i++) {
        const bool chosen = i == menu->selected;

        (void)snprintf(text, sizeof(text), "%s%s",
                       chosen ? "> " : "   ", MENU[i]);
        show_text(menu->ui, menu->rows[i], text);
        (void)bento_ui_set_color(menu->ui, menu->rows[i],
                                 chosen ? (uint32_t)COLOR_ACCENT
                                        : (uint32_t)COLOR_DIM);
    }
}

/* A Label made with "" shows LVGL's "Label" until draw_menu() writes
 * it. Rows are 44 px apart. */
static bento_status_t add_rows(menu_t *menu)
{
    bento_ui_widget_args_t args;
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < MENU_COUNT) && (status == BENTO_OK); i++) {
        bento_ui_widget_args_init(&args);
        args.text = "";
        args.x = ROW_X;
        args.y = (int16_t)(ROW_Y + (i * ROW_PITCH));
        args.color = COLOR_DIM;
        args.font_px = FONT_ROW_PX;
        status = bento_ui_label_create(menu->ui, &args, &menu->rows[i]);
    }
    return status;
}

static bento_status_t build_screen(menu_t *menu)
{
    bento_status_t status =
        bento_ui_screen(menu->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_widgets(menu->ui, SCREEN_WIDGETS, 2U, menu->ids);
    }
    if (status == BENTO_OK) {
        status = add_rows(menu);
    }
    if (status == BENTO_OK) {
        status = add_widgets(menu->ui, &SCREEN_WIDGETS[W_TRAP_CARD],
                             (uint32_t)W_COUNT - 2U,
                             &menu->ids[W_TRAP_CARD]);
    }
    return status;
}

/* One step up or down, on the rising edge only. The tone has no
 * answer; a board without sound stays silent. */
static bento_status_t move_selection(menu_t *menu, int32_t step,
                                     uint8_t note)
{
    char text[TEXT_BYTES];
    const bento_status_t status = bento_gpio_led_on(menu->led_confirm);

    if (status == BENTO_OK) {
        menu->selected =
            (menu->selected + step + MENU_COUNT) % MENU_COUNT;
        (void)bento_ui_sfx_tone(
            menu->ui, note, (uint8_t)BENTO_UI_WAVE_SINE,
            (uint8_t)TONE_VELOCITY, (uint16_t)TONE_MS);
        draw_menu(menu);
        (void)snprintf(text, sizeof(text), "> %s",
                       MENU[menu->selected]);
        /* lcd.print() ignores a console that is not ready */
        (void)bento_lcd_print(text);
    }
    return status;
}

static bento_status_t react_to_touch(menu_t *menu, bool up, bool down)
{
    bento_status_t status = BENTO_OK;

    if (up && !menu->was_down[0]) {
        status = move_selection(menu, -1, (uint8_t)TONE_UP);
    } else if (down && !menu->was_down[1]) {
        status = move_selection(menu, 1, (uint8_t)TONE_DOWN);
    } else if (!up && !down) {
        status = bento_gpio_led_off(menu->led_confirm);
    } else {
        /* still held: nothing new */
    }
    return status;
}

/* The bar answers "how long has it been touched", which a finger
 * cannot tell. */
static void show_hold(menu_t *menu)
{
    char text[TEXT_BYTES];
    const int32_t held =
        (menu->held[0] > menu->held[1]) ? menu->held[0] : menu->held[1];

    if (held != menu->shown_hold) {
        menu->shown_hold = held;
        (void)bento_ui_set_value(menu->ui, menu->ids[W_HOLD_BAR],
                                 (held < STUCK_ROUNDS) ? held
                                                       : STUCK_ROUNDS);
        (void)snprintf(text, sizeof(text), "%" PRId32 " / %d", held,
                       STUCK_ROUNDS);
        show_text(menu->ui, menu->ids[W_HOLD_TEXT], text);
    }
}

/* A touch that never ends usually means water on the pad. */
static bento_status_t watch_stuck(menu_t *menu, bool up, bool down)
{
    const bento_ui_id_t warn = menu->ids[W_WARN];
    bento_status_t status = BENTO_OK;

    if ((menu->held[0] == STUCK_ROUNDS) ||
        (menu->held[1] == STUCK_ROUNDS)) {
        menu->stuck = true;
        status = bento_gpio_led_on(menu->led_warn);
        show_text(menu->ui, warn, "แตะค้างผิดปกติ\nเช็ดแผ่นสัมผัส");
        (void)bento_ui_set_color(menu->ui, warn, COLOR_BAD);
        (void)bento_lcd_print(
            "<span class=muted>แตะค้างผิดปกติ - เช็ดแผ่นสัมผัส</span>");
    } else if (!up && !down) {
        status = bento_gpio_led_off(menu->led_warn);
        if (menu->stuck) {
            menu->stuck = false;
            show_text(menu->ui, warn, "สถานะ: ปกติ");
            (void)bento_ui_set_color(menu->ui, warn, COLOR_DIM);
        }
    } else {
        /* touched, but not long enough to worry */
    }
    return status;
}

/* One round. A failed read raises in the original, so it ends here. */
static bento_status_t run_round(menu_t *menu)
{
    uint8_t up = 0U;
    uint8_t down = 0U;
    bento_status_t status = bento_sensors_capsense_buttons(&up, &down);

    if (status == BENTO_OK) {
        menu->held[0] = (up != 0U) ? (menu->held[0] + 1) : 0;
        menu->held[1] = (down != 0U) ? (menu->held[1] + 1) : 0;
        status = react_to_touch(menu, up != 0U, down != 0U);
    }
    if (status == BENTO_OK) {
        show_hold(menu);
        status = watch_stuck(menu, up != 0U, down != 0U);
    }
    if (status == BENTO_OK) {
        menu->was_down[0] = up != 0U;
        menu->was_down[1] = down != 0U;
        let_screen_draw(menu->ui);
        status = pause_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    menu_t menu;
    char text[TEXT_BYTES];
    bento_status_t status;
    int32_t round;

    (void)memset(&menu, 0, sizeof(menu));
    menu.ui = bento_runtime_get_ui();
    menu.shown_hold = -1;
    pick_leds(&menu);
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>เมนูสัมผัสสองปุ่ม</h2>");
    (void)snprintf(text, sizeof(text),
                   "ซ้าย = ขึ้น | ขวา = ลง | ทั้งหมด %d รายการ", MENU_COUNT);
    (void)bento_lcd_print(text);
    status = build_screen(&menu);
    if (status == BENTO_OK) {
        draw_menu(&menu);
        let_screen_draw(menu.ui);
        (void)snprintf(text, sizeof(text), "> %s", MENU[0]);
        (void)bento_lcd_print(text);
    }
    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = run_round(&menu);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(menu.led_warn);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(menu.led_confirm);
    }
    if (status == BENTO_OK) {
        (void)printf("เลือกไว้ที่: %s\n", MENU[menu.selected]);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
