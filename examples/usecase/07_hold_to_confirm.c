/*
 * 07_hold_to_confirm.c -- กดค้างเพื่อยืนยันคำสั่งที่ย้อนกลับไม่ได้
 * (hold to confirm a command that cannot be undone)
 *
 * ต้องกดปุ่มค้างครบสามวินาทีถึงจะนับว่ายืนยัน ปล่อยก่อนครบ = ยกเลิก ไม่มีผลใด ๆ
 * หน้าปัดวงกลมและ Seg7 บอกความคืบหน้า 0 ถึง 100% กราฟเก็บรูปการกดไว้ เส้นแดง
 * คือเส้นครบ 100% หลอดน้ำเงินสว่างขึ้นตามเวลาที่ค้างและดับทันทีที่ปล่อย ครบแล้ว
 * หลอดแดงกะพริบยืนยัน โปรแกรมวนอ่านปุ่มตลอดเวลาแล้วเทียบเวลาเอง ไม่ใช้ sleep
 * นับถอยหลัง
 *
 * Hold the button for three seconds to confirm; releasing earlier
 * cancels with no effect. The dial and the Seg7 show the progress from
 * 0 to 100 %, the chart keeps the shape of each press against the red
 * 100 % line, the blue LED brightens while the button is held and goes
 * dark on release, and the red LED blinks once the command has run.
 * The program reads the button all the time and compares the clock
 * itself; it never sleeps through a countdown.
 *
 * Twin of 07_hold_to_confirm.py
 * (embedded-systems-for-aiot-developer/examples/usecase/
 *  07_hold_to_confirm.py), rewritten clean from the library twin
 * aiot-usecase-07-hold-to-confirm.
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - gpio.button(0) and the LEDs are indexes; led_named() searches the
 *    board's LED names with bento_gpio_led_name(), and an index the
 *    board lacks ends the program (ValueError in MicroPython);
 *  - floor division on a negative number (shown starts at -1) needs a
 *    helper, since C's / rounds toward zero; held * 100 is computed in
 *    64 bits, as Python's int does not overflow;
 *  - this course file has no try/finally: as in the .py, both LEDs go
 *    off after the 8000 rounds only, not when Stop or an error ends
 *    the program early;
 *  - Stop during a sleep or a hold() raises KeyboardInterrupt in
 *    MicroPython; here the wait returns and the program ends there.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_runtime.h"

/* The hold, the debounce and the rhythm of the loop. */
enum {
    HOLD_MS = 3000,   /* held this long counts as confirmed */
    DEBOUNCE_MS = 25, /* the button contact's bounce */
    SAMPLE_MS = 60,   /* a chart point this often */
    LOOP_MS = 10,     /* one round; the progress LED's hold spends it */
    BLINK_MS = 80,
    BLINKS = 6,
    ROUNDS = 8000,
    CHART_POINTS = 50,
    FULL_PCT = 100,
    DECILE_PCT = 10, /* an LCD line every 10 % */
    NOT_SHOWN = -1,  /* no percentage shown yet */
    PRESSED = 0,     /* value(): the button pulls the pin low */
    BUTTON = 0,
    LED_FALLBACK = 0,
    NAME_PAIR = 2,
    TEXT_CHARS = 128
};

/* Colours, 0xRRGGBB; 0 leaves the widget's own colour. */
enum {
    COLOR_DEFAULT = 0,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22
};

/* Layout on the 792 x 398 canvas: the chart, its legend and the
 * verdict card on the left, the dial, the Seg7 and the two rules on
 * the right; the rules end at y=331, clear of the Console button. */
enum {
    LEFT_X = 24,
    CARD_TEXT_X = 40,
    RIGHT_X = 496,
    TITLE_Y = 8,
    CHART_Y = 48,
    CHART_W = 440,
    CHART_H = 176,
    CHART_MIN = -5,
    CHART_MAX = 105,
    LEGEND_BLUE_Y = 236,
    LEGEND_RED_Y = 264,
    CARD_Y = 300,
    CARD_H = 76,
    VERDICT_Y = 308,
    HINT_Y = 344,
    ARC_X = 548,
    ARC_Y = 40,
    ARC_SIZE = 160,
    SEG_Y = 216,
    SEG_W = 120,
    SEG_H = 48,
    PERCENT_X = 624,
    PERCENT_Y = 228,
    RULE_TIME_Y = 276,
    RULE_CANCEL_Y = 304,
    CARD_BORDER = 2,
    HEAD_PX = 24,
    TEXT_PX = 20
};

/* Where a widget sits and how big it is (-1: the widget's default). */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} place_t;

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

/* The LEDs, the widgets the rounds change, and the state of a hold. */
typedef struct {
    bento_ui_t *ui;
    uint32_t led_progress;
    uint32_t led_done;
    bento_ui_id_t chart;
    uint8_t goal_series;
    bento_ui_id_t verdict;
    bento_ui_id_t hint;
    bento_ui_id_t arc;
    bento_ui_id_t seg;
    uint32_t previous; /* the last accepted button value */
    uint32_t last_edge_ms;
    uint32_t sample_at_ms;
    uint32_t down_at_ms;
    bool fired;
    int32_t shown;
    int32_t pct;
} confirm_t;

/* BENTO_E_CANCELLED once Stop has been pressed (KeyboardInterrupt in
 * MicroPython). */
static bento_status_t stop_check(bento_status_t status)
{
    return ((status == BENTO_OK) && !bento_runtime_is_running())
               ? BENTO_E_CANCELLED
               : status;
}

/* Floor division by a positive divisor, as Python's floor division. */
static int64_t floor_div(int64_t value, int64_t divisor)
{
    int64_t quotient = value / divisor;

    if (((value % divisor) != 0) && (value < 0)) {
        quotient--;
    }
    return quotient;
}

/* The index of the LED with this name, as table.index(name). */
static bool led_index(const char *name, uint32_t *index)
{
    const uint32_t led_count = bento_gpio_num_leds();
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < led_count) && !found; i++) {
        const char *led_name = "";

        if ((bento_gpio_led_name(i, &led_name) == BENTO_OK) &&
            (strcmp(led_name, name) == 0)) {
            found = true;
            *index = i;
        }
    }
    return found;
}

/* The LED whose name comes first, else the fallback: the index differs
 * from board to board, the name does not. */
static bento_status_t led_named(const char *const names[NAME_PAIR],
                                uint32_t *led)
{
    bento_gpio_board_info_t info;
    bool found = false;
    uint32_t name;
    bento_status_t status;

    bento_gpio_board_info_init(&info);
    status = bento_gpio_board_info(&info);
    for (name = 0U;
         (name < (uint32_t)NAME_PAIR) && !found && (status == BENTO_OK);
         name++) {
        found = led_index(names[name], led);
    }
    if ((status == BENTO_OK) && !found) {
        *led = LED_FALLBACK;
        /* gpio.led() raises ValueError for an LED the board lacks */
        status = (*led < bento_gpio_num_leds())
                     ? BENTO_OK
                     : BENTO_E_INVALID_ARGUMENT;
    }
    return status;
}

/* Blue is the progress and red the confirmation on both boards. The
 * Eva Kit's RGB_RED shines blue, and a board without RGB_GREEN (the Eva
 * Kit) has its red LED at index 0. */
static bento_status_t find_leds(confirm_t *confirm)
{
    static const char *const progress_names[NAME_PAIR] = {"RGB_BLUE",
                                                          "RGB_RED"};
    static const char *const done_names[NAME_PAIR] = {"RGB_RED",
                                                      "LED1"};
    uint32_t unused = 0U;
    bento_status_t status =
        led_named(progress_names, &confirm->led_progress);

    if (status == BENTO_OK) {
        if (led_index("RGB_GREEN", &unused)) {
            status = led_named(done_names, &confirm->led_done);
        } else {
            confirm->led_done = 0U;
            status = (bento_gpio_num_leds() > 0U)
                         ? BENTO_OK
                         : BENTO_E_INVALID_ARGUMENT;
        }
    }
    return status;
}

static void place_args(bento_ui_widget_args_t *args, place_t place)
{
    bento_ui_widget_args_init(args);
    args->x = place.x;
    args->y = place.y;
    args->w = place.w;
    args->h = place.h;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    place_args(&args, (place_t){look.x, look.y, -1, -1});
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static void set_text_color(bento_ui_t *ui, bento_ui_id_t widget,
                           const char *text, uint32_t color)
{
    set_text(ui, widget, text);
    (void)bento_ui_set_color(ui, widget, color);
}

/* A ui.poll() whose events the original throws away. */
static void poll_drop(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL); /* the events are not read, as before */
}

/* The title, the chart with its red goal series, and the legend. */
static bento_status_t add_chart(confirm_t *confirm)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        confirm->ui, "กดค้างเพื่อยืนยัน",
        (label_look_t){LEFT_X, TITLE_Y, COLOR_DEFAULT, HEAD_PX},
        &unused);

    if (status == BENTO_OK) {
        place_args(&args, (place_t){LEFT_X, CHART_Y, CHART_W, CHART_H});
        args.color = COLOR_ACCENT;
        args.min_val = CHART_MIN;
        args.max_val = CHART_MAX;
        status =
            bento_ui_chart_create(confirm->ui, &args, &confirm->chart);
    }
    if (status == BENTO_OK) {
        status =
            bento_ui_chart_add_series(confirm->ui, confirm->chart,
                                      COLOR_BAD, &confirm->goal_series);
    }
    if (status == BENTO_OK) {
        status = add_label(confirm->ui, "เส้นฟ้า = เปอร์เซ็นต์ที่ค้างอยู่",
                           (label_look_t){LEFT_X, LEGEND_BLUE_Y,
                                          COLOR_ACCENT, TEXT_PX},
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            confirm->ui, "เส้นแดง = เส้นครบ 100%",
            (label_look_t){LEFT_X, LEGEND_RED_Y, COLOR_BAD, TEXT_PX},
            &unused);
    }
    return status;
}

/* The verdict card is made before its two lines: LVGL draws in the
 * order of creation. The hint names the board's own button. */
static bento_status_t add_verdict_card(confirm_t *confirm)
{
    bento_ui_widget_args_t args;
    char hint[TEXT_CHARS];
    const char *button_name = "";
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    place_args(&args, (place_t){LEFT_X, CARD_Y, CHART_W, CARD_H});
    args.color = COLOR_CARD;
    args.min_val = COLOR_CARD;
    args.font_px = CARD_BORDER;
    status = bento_ui_panel_create(confirm->ui, &args, &unused);
    if (status == BENTO_OK) {
        status = add_label(
            confirm->ui, "รอการกด",
            (label_look_t){CARD_TEXT_X, VERDICT_Y, COLOR_DIM, HEAD_PX},
            &confirm->verdict);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_button_name(BUTTON, &button_name);
    }
    if (status == BENTO_OK) {
        (void)snprintf(hint, sizeof(hint), "กดค้างที่ปุ่ม %s บนบอร์ด",
                       button_name);
        status = add_label(
            confirm->ui, hint,
            (label_look_t){CARD_TEXT_X, HINT_Y, COLOR_DEFAULT, TEXT_PX},
            &confirm->hint);
    }
    return status;
}

/* The progress dial and the Seg7; the Seg7 gets "0" at once, or it
 * would show its own 0000. */
static bento_status_t add_dial(confirm_t *confirm)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        confirm->ui, "หน้าปัดความคืบหน้า",
        (label_look_t){RIGHT_X, TITLE_Y, COLOR_DEFAULT, TEXT_PX},
        &unused);

    if (status == BENTO_OK) {
        place_args(&args, (place_t){ARC_X, ARC_Y, ARC_SIZE, ARC_SIZE});
        args.min_val = 0;
        args.max_val = FULL_PCT;
        args.font_px = 0;
        status = bento_ui_arc_create(confirm->ui, &args, &confirm->arc);
    }
    if (status == BENTO_OK) {
        place_args(&args, (place_t){RIGHT_X, SEG_Y, SEG_W, SEG_H});
        status =
            bento_ui_seg7_create(confirm->ui, &args, &confirm->seg);
    }
    if (status == BENTO_OK) {
        set_text(confirm->ui, confirm->seg, "0");
        status = add_label(confirm->ui, "%",
                           (label_look_t){PERCENT_X, PERCENT_Y,
                                          COLOR_DEFAULT, TEXT_PX},
                           &unused);
    }
    return status;
}

static bento_status_t add_rules(bento_ui_t *ui)
{
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        ui, "ต้องกดค้างครบ 3.0 วินาที",
        (label_look_t){RIGHT_X, RULE_TIME_Y, COLOR_DEFAULT, TEXT_PX},
        &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "ปล่อยก่อนครบ = ยกเลิก",
                           (label_look_t){RIGHT_X, RULE_CANCEL_Y,
                                          COLOR_DEFAULT, TEXT_PX},
                           &unused);
    }
    return status;
}

static bento_status_t build_screen(confirm_t *confirm)
{
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(confirm->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = add_chart(confirm);
    if (status == BENTO_OK) {
        status = add_verdict_card(confirm);
    }
    if (status == BENTO_OK) {
        status = add_dial(confirm);
    }
    if (status == BENTO_OK) {
        status = add_rules(confirm->ui);
    }
    if (status == BENTO_OK) {
        poll_drop(confirm->ui);
    }
    return status;
}

static void show_percent(const confirm_t *confirm, int32_t pct)
{
    char text[TEXT_CHARS];

    (void)bento_ui_set_value(confirm->ui, confirm->arc, pct);
    (void)snprintf(text, sizeof(text), "%" PRId32, pct);
    set_text(confirm->ui, confirm->seg, text);
}

/* A release before the command fired: nothing happens, and the screen
 * and the LCD say where it was let go. */
static void show_cancel(const confirm_t *confirm)
{
    char text[TEXT_CHARS];

    (void)snprintf(text, sizeof(text), "ยกเลิก - ปล่อยที่ %" PRId32 " %%",
                   confirm->shown);
    (void)bento_lcd_print(text); /* modlcd.c ignores a refusal */
    set_text_color(confirm->ui, confirm->verdict, "ยกเลิก", COLOR_DIM);
    (void)snprintf(text, sizeof(text),
                   "ปล่อยที่ %" PRId32 "%% - ไม่มีผลใด ๆ", confirm->shown);
    set_text(confirm->ui, confirm->hint, text);
}

/* A debounced edge: a press starts the hold, a release ends it. */
static bento_status_t on_edge(confirm_t *confirm, uint32_t value,
                              uint32_t now_ms)
{
    bento_status_t status = BENTO_OK;

    confirm->last_edge_ms = now_ms;
    if (value == (uint32_t)PRESSED) {
        confirm->down_at_ms = now_ms;
        confirm->fired = false;
        set_text_color(confirm->ui, confirm->verdict, "กำลังค้าง...",
                       COLOR_WARN);
    } else {
        if (!confirm->fired) {
            show_cancel(confirm);
        }
        status = bento_gpio_led_off(confirm->led_progress);
        confirm->shown = NOT_SHOWN;
        confirm->pct = 0;
        show_percent(confirm, 0);
    }
    confirm->previous = value;
    return status;
}

/* The hold completed: the command runs and the done LED blinks. */
static bento_status_t fire(confirm_t *confirm)
{
    bento_status_t status = bento_gpio_led_off(confirm->led_progress);
    int32_t blink;

    confirm->fired = true;
    if (status == BENTO_OK) {
        set_text_color(confirm->ui, confirm->verdict, "ยืนยันแล้ว",
                       COLOR_OK);
        set_text(confirm->ui, confirm->hint, "คำสั่งถูกดำเนินการเรียบร้อย");
    }
    for (blink = 0; (blink < BLINKS) && (status == BENTO_OK); blink++) {
        status = bento_gpio_led_toggle(confirm->led_done);
        if (status == BENTO_OK) {
            poll_drop(confirm->ui);
            status = stop_check(bento_time_sleep_ms(BLINK_MS));
        }
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(confirm->led_done);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_print(
            "<span class=ok>ยืนยันแล้ว - คำสั่งถูกดำเนินการ</span>");
    }
    return status;
}

/* Held and not fired yet: the percentage, an LCD line per tenth, and
 * the command at HOLD_MS. */
static bento_status_t on_held(confirm_t *confirm, uint32_t now_ms)
{
    char text[TEXT_CHARS];
    const int32_t held_ms =
        bento_time_ticks_diff(now_ms, confirm->down_at_ms);
    const int64_t pct = floor_div((int64_t)held_ms * FULL_PCT, HOLD_MS);
    bento_status_t status = BENTO_OK;

    confirm->pct = (pct < FULL_PCT) ? (int32_t)pct : FULL_PCT;
    show_percent(confirm, confirm->pct);
    if (floor_div(confirm->pct, DECILE_PCT) !=
        floor_div(confirm->shown, DECILE_PCT)) {
        (void)snprintf(text, sizeof(text), "ค้างอยู่ %" PRId32 " %%",
                       confirm->pct);
        (void)bento_lcd_print(text);
    }
    confirm->shown = confirm->pct;
    if (held_ms >= HOLD_MS) {
        status = fire(confirm);
    }
    return status;
}

/* One round: read the button, take an edge, follow the hold, draw,
 * poll, and the progress LED's hold, which is the wait. Never a
 * countdown sleep: a release halfway must be seen. */
static bento_status_t run_round(confirm_t *confirm)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    uint32_t value = 0U;
    bento_status_t status = bento_gpio_button_value(BUTTON, &value);

    if ((status == BENTO_OK) && (value != confirm->previous) &&
        (bento_time_ticks_diff(now_ms, confirm->last_edge_ms) >
         DEBOUNCE_MS)) {
        status = on_edge(confirm, value, now_ms);
    }
    if ((status == BENTO_OK) && (value == (uint32_t)PRESSED) &&
        !confirm->fired) {
        status = on_held(confirm, now_ms);
    }
    if ((status == BENTO_OK) &&
        (bento_time_ticks_diff(now_ms, confirm->sample_at_ms) >=
         SAMPLE_MS)) {
        confirm->sample_at_ms = now_ms;
        (void)bento_ui_chart_set_next(confirm->ui, confirm->chart, 0U,
                                      confirm->pct);
        (void)bento_ui_chart_set_next(confirm->ui, confirm->chart,
                                      confirm->goal_series, FULL_PCT);
    }
    if (status == BENTO_OK) {
        poll_drop(confirm->ui);
        /* dark once fired: this LED tells how much is left, not done */
        status = stop_check(bento_gpio_led_hold(
            confirm->led_progress, confirm->fired ? 0 : confirm->pct,
            LOOP_MS));
    }
    return status;
}

/* The goal line fills the chart first, so the blue line has something
 * to compare with; then the button's first value and the clocks. */
static bento_status_t start_rounds(confirm_t *confirm)
{
    int32_t point;

    for (point = 0; point < CHART_POINTS; point++) {
        (void)bento_ui_chart_set_next(confirm->ui, confirm->chart,
                                      confirm->goal_series, FULL_PCT);
    }
    confirm->last_edge_ms = bento_time_ticks_ms();
    confirm->sample_at_ms = bento_time_ticks_ms();
    confirm->down_at_ms = 0U;
    confirm->fired = false;
    confirm->shown = NOT_SHOWN;
    confirm->pct = 0;
    return bento_gpio_button_value(BUTTON, &confirm->previous);
}

static bento_status_t run_rounds(confirm_t *confirm)
{
    bento_status_t status = start_rounds(confirm);
    int32_t round;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = run_round(confirm);
    }
    return status;
}

int main(void)
{
    confirm_t confirm;
    bento_status_t status;

    (void)memset(&confirm, 0, sizeof(confirm));
    confirm.ui = bento_runtime_get_ui();
    status = find_leds(&confirm);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear(); /* lcd calls ignore a refusal */
        (void)bento_lcd_console("<h2>กดค้าง 3 วินาทีเพื่อยืนยัน</h2>");
        (void)bento_lcd_print("ปล่อยมือก่อนครบ = ยกเลิก ไม่มีผล");
        status = build_screen(&confirm);
    }
    if (status == BENTO_OK) {
        status = run_rounds(&confirm);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(confirm.led_progress);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(confirm.led_done);
    }
    if (status == BENTO_OK) {
        set_text(confirm.ui, confirm.hint, "จบรอบทดสอบ");
        (void)printf("จบ\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
