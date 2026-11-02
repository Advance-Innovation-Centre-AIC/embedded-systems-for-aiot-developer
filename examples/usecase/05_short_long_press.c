/*
 * 05_short_long_press.c -- ปุ่มเดียว สองความหมาย (Short
 * and Long Press)
 *
 * แตะปุ่ม USER BTN1 สั้น ๆ เพื่อเลื่อนโหมด (ไฟ LED จริงดวงถัดไปติด)
 * หรือกดค้างเกิน 1 วินาที เพื่อล้าง (ไฟดับทุกดวง) ความหมายอยู่ที่
 * "กดนานเท่าไร" โปรแกรมจึงจับเวลาตอนกดลง แล้วตัดสิน ตอนปล่อย ปุ่ม
 * active-low ไม่มี debounce ในฮาร์ดแวร์ จึงกันเด้ง 25 ms ในโค้ด
 * กราฟเส้นฟ้าคือ ปุ่มดิบ เส้นแดงคือคำตัดสิน Seg7
 * กับแถบไต่ตามเวลาที่กดค้างอยู่ (บน Emulator กดปุ่มที่แผง HW)
 *
 * Tap USER BTN1 briefly to move to the next mode (the next real LED
 * lights), or hold it longer than 1 second to clear (every LED off).
 * The meaning is in how long the button is held, so the program times
 * the press and decides on release. The button is active-low with no
 * hardware debounce, so edges closer than 25 ms are ignored in code.
 * The blue chart line is the raw button, the red one the decision; the
 * Seg7 and the bar climb while the button is held. (On the Emulator,
 * press the button on the HW panel.)
 *
 * Twin of 05_short_long_press.py
 * (embedded-systems-for-aiot-developer/examples/usecase/05_short_long_press.py),
 * merged into one file from the library twin aiot-usecase-05-short-long-press.
 * Style: CLEAN_C.md. Stop is
 * not caught by the original: a Stop in any sleep ends the program here
 * too.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Colours, 0xRRGGBB. */
enum {
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_BAD = 0xE5484D,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22
};

/* Times, in milliseconds, and the loop's length. */
enum {
    LONG_MS = 1000, /* held longer than this is a long press */
    /* contacts bounce for a few ms; this much is enough */
    DEBOUNCE_MS = 25,
    SAMPLE_MS = 60, /* a point goes to the chart this often */
    /* the decision stays on the chart this long, so the eye catches
     * it */
    HOLD_SHOW_MS = 800,
    LOOP_MS = 5,
    ROUNDS = 6000
};

/* Levels on the chart. */
enum {
    CHART_MAX = 100,
    RAW_UP = 10,    /* drawn while released */
    RAW_DOWN = 70,  /* drawn while pressed */
    DEC_SHORT = 35, /* the "tap" decision */
    DEC_LONG = 85   /* the "hold" decision */
};

enum {
    BUTTON = 0,         /* USER BTN1 */
    RAW_SERIES = 0,     /* the series the chart comes with */
    BUTTON_PRESSED = 0, /* active-low: 0 pressed, 1 released */
    BUTTON_RELEASED = 1,
    /* the longest text built here, with room for its numbers */
    TEXT_CHARS = 128
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    uint8_t decision_series;
    bento_ui_id_t status;
    bento_ui_id_t held;
    bento_ui_id_t bar;
    bento_ui_id_t verdict;
} screen_t;

/* The button's story so far: its last accepted level and the times
 * of its edges. */
typedef struct {
    uint32_t leds;
    uint32_t mode; /* the LED that is on */
    uint32_t prev; /* the last accepted level */
    uint32_t down_ms;
    uint32_t last_edge_ms;
    uint32_t sample_ms;
    int32_t decision_level;
    uint32_t decision_ms;
} press_t;

/* Screen layout on the 792 x 398 canvas: the chart, its legend and a
 * status card on the left; the held time, the progress bar, the levels
 * and the decision card on the right. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    LEFT_W = 440,
    CHART_Y = 48,
    CHART_H = 192,
    LEGEND_RAW_Y = 252,
    LEGEND_DECISION_Y = 280,
    STATUS_CARD_Y = 316,
    STATUS_CARD_H = 60,
    STATUS_X = 40,
    STATUS_Y = 332,
    RIGHT_X = 496,
    HELD_TITLE_Y = 48,
    HELD_Y = 80,
    HELD_W = 160,
    HELD_H = 48,
    PROGRESS_TITLE_Y = 144,
    BAR_Y = 176,
    BAR_W = 256,
    BAR_H = 24,
    LEVEL_SHORT_Y = 216,
    LEVEL_LONG_Y = 244,
    /* ends at y = 340, clear of the Console button's corner */
    DECISION_CARD_Y = 284,
    DECISION_CARD_W = 272,
    DECISION_CARD_H = 56,
    DECISION_X = 512,
    DECISION_Y = 296,
    CARD_BORDER = 2
};

/* Font sizes, in pixels. */
enum {
    FONT_TITLE = 24,
    FONT_LABEL = 20
};

/* A point on the canvas. */
typedef struct {
    int16_t x;
    int16_t y;
} point_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                point_t at, int16_t font_px,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = at.x;
    args.y = at.y;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_colored_label(bento_ui_t *ui,
                                        const char *text, point_t at,
                                        uint32_t color,
                                        bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = at.x;
    args.y = at.y;
    args.font_px = FONT_LABEL;
    args.color = color;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A card: the original gives min= the colour again and no max=. */
static bento_status_t add_card(bento_ui_t *ui, point_t at, point_t size)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = at.x;
    args.y = at.y;
    args.w = size.x;
    args.h = size.y;
    args.color = COLOR_CARD;
    args.min_val = COLOR_CARD;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* The left column: the chart (raw button in blue, decision in red), its
 * legend and the status card. */
static bento_status_t add_left_column(screen_t *screen)
{
    const point_t raw_at = {MARGIN_X, LEGEND_RAW_Y};
    const point_t decision_at = {MARGIN_X, LEGEND_DECISION_Y};
    const point_t card_at = {MARGIN_X, STATUS_CARD_Y};
    const point_t card_size = {LEFT_W, STATUS_CARD_H};
    const point_t status_at = {STATUS_X, STATUS_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CHART_Y;
    args.w = LEFT_W;
    args.h = CHART_H;
    args.min_val = 0;
    args.max_val = CHART_MAX;
    args.color = COLOR_ACCENT;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_BAD,
                                           &screen->decision_series);
    }
    if (status == BENTO_OK) {
        status = add_colored_label(screen->ui,
                                   "เส้นฟ้า = ระดับปุ่มดิบ "
                                   "(กด=สูง)",
                                   raw_at, COLOR_ACCENT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_colored_label(screen->ui,
                                   "เส้นแดง = "
                                   "คำตัดสินตอนปล่อย",
                                   decision_at, COLOR_BAD, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui, card_at, card_size);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "แตะสั้น = เลื่อนโหมด | กดค้าง "
                           "= ล้าง",
                           status_at, FONT_LABEL, &screen->status);
    }
    return status;
}

/* The held time and the progress bar towards LONG_MS. */
static bento_status_t add_progress(screen_t *screen)
{
    const point_t title_at = {RIGHT_X, HELD_TITLE_Y};
    const point_t progress_at = {RIGHT_X, PROGRESS_TITLE_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(screen->ui, "กดค้างมาแล้ว (ms)",
                                      title_at, FONT_LABEL, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = RIGHT_X;
        args.y = HELD_Y;
        args.w = HELD_W;
        args.h = HELD_H;
        status = bento_ui_seg7_create(screen->ui, &args, &screen->held);
    }
    /* the widget's own start would show 0000 */
    if (status == BENTO_OK) {
        (void)bento_ui_set_text(screen->ui, screen->held, "0", 1U);
        status = add_label(screen->ui, "ความคืบหน้าสู่ 1000 ms",
                           progress_at, FONT_LABEL, &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = RIGHT_X;
        args.y = BAR_Y;
        args.w = BAR_W;
        args.h = BAR_H;
        args.min_val = 0;
        args.max_val = LONG_MS;
        args.font_px = 0;
        status = bento_ui_bar_create(screen->ui, &args, &screen->bar);
    }
    return status;
}

/* The two levels' key and the decision card. */
static bento_status_t add_decision(screen_t *screen)
{
    const point_t short_at = {RIGHT_X, LEVEL_SHORT_Y};
    const point_t long_at = {RIGHT_X, LEVEL_LONG_Y};
    const point_t card_at = {RIGHT_X, DECISION_CARD_Y};
    const point_t card_size = {DECISION_CARD_W, DECISION_CARD_H};
    const point_t verdict_at = {DECISION_X, DECISION_Y};
    bento_ui_id_t unused;
    bento_status_t status = add_label(screen->ui, "แตะสั้น = ระดับกลาง",
                                      short_at, FONT_LABEL, &unused);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กดค้าง = ระดับสูง", long_at,
                           FONT_LABEL, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui, card_at, card_size);
    }
    if (status == BENTO_OK) {
        status = add_colored_label(screen->ui, "ยังไม่มีการกด", verdict_at,
                                   COLOR_DIM, &screen->verdict);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(screen_t *screen)
{
    const point_t title_at = {MARGIN_X, TITLE_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ปุ่มเดียว สองความหมาย", title_at,
                           FONT_TITLE, &unused);
    }
    if (status == BENTO_OK) {
        status = add_left_column(screen);
    }
    if (status == BENTO_OK) {
        status = add_progress(screen);
    }
    if (status == BENTO_OK) {
        status = add_decision(screen);
    }
    return status;
}

/* ui.poll() here only keeps the display's queue moving: its events are
 * not used, and a failed poll answers none in MicroPython, so its
 * status is not a failure. */
static void poll_display(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* While the button is held, the Seg7 and the bar climb, so the user
 * sees how far the long press still is; after a decision has been shown
 * long enough the bar goes back to 0. */
static void show_held(const screen_t *screen, const press_t *press,
                      uint32_t level, uint32_t now_ms)
{
    char text[TEXT_CHARS];

    if (level == BUTTON_PRESSED) {
        const int32_t held_ms =
            bento_time_ticks_diff(now_ms, press->down_ms);

        (void)snprintf(text, sizeof(text), "%" PRId32, held_ms);
        (void)bento_ui_set_text(screen->ui, screen->held, text,
                                (uint32_t)strlen(text));
        (void)bento_ui_set_value(screen->ui, screen->bar,
                                 (held_ms < LONG_MS) ? held_ms
                                                     : LONG_MS);
    } else if (bento_time_ticks_diff(now_ms, press->decision_ms) >=
               HOLD_SHOW_MS) {
        (void)bento_ui_set_value(screen->ui, screen->bar, 0);
    } else {
        /* a press just decided: its bar stays up a moment to be read */
    }
}

/* A point on each line at a fixed pace, so both lines always have as
 * many points. */
static void sample_chart(const screen_t *screen, press_t *press,
                         uint32_t level, uint32_t now_ms)
{
    if (bento_time_ticks_diff(now_ms, press->sample_ms) >= SAMPLE_MS) {
        press->sample_ms = now_ms;
        (void)bento_ui_chart_set_next(
            screen->ui, screen->chart, RAW_SERIES,
            (level == BUTTON_PRESSED) ? RAW_DOWN : RAW_UP);
        if (bento_time_ticks_diff(now_ms, press->decision_ms) >=
            HOLD_SHOW_MS) {
            press->decision_level = 0;
        }
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->decision_series,
                                      press->decision_level);
    }
}

/* The console's first lines: the title, then the button's name and
 * the rule. */
static bento_status_t greet(void)
{
    const char *name = NULL;
    char line[TEXT_CHARS];
    bento_status_t status;

    /* the lcd calls ignore a console that is not ready (modlcd.c) */
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>ปุ่มเดียว สองความหมาย</h2>");
    status = bento_gpio_button_name(BUTTON, &name);
    if (status == BENTO_OK) {
        (void)snprintf(line, sizeof(line),
                       "ปุ่ม %s | กดค้าง > %d ms = "
                       "ล้าง",
                       name, LONG_MS);
        (void)bento_lcd_print(line);
    }
    return status;
}

/* Held at least LONG_MS: every LED off. */
static bento_status_t decide_long(const screen_t *screen,
                                  press_t *press, int32_t held_ms)
{
    char line[TEXT_CHARS];
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < press->leds) && (status == BENTO_OK); i++) {
        status = bento_gpio_led_off(i);
    }
    if (status == BENTO_OK) {
        press->decision_level = DEC_LONG;
        (void)bento_ui_set_text(screen->ui, screen->verdict,
                                "กดค้าง -> ล้างทั้งหมด",
                                (uint32_t)strlen("กดค้าง -> "
                                                 "ล้างทั้งหมด"));
        (void)bento_ui_set_color(screen->ui, screen->verdict,
                                 COLOR_BAD);
        (void)snprintf(line, sizeof(line),
                       "กดค้าง %" PRId32 " ms -> ล้างทั้งหมด", held_ms);
        (void)bento_ui_set_text(screen->ui, screen->status, line,
                                (uint32_t)strlen(line));
        (void)bento_lcd_print(line);
    }
    return status;
}

/* A short tap: the next LED is the mode. */
static bento_status_t decide_short(const screen_t *screen,
                                   press_t *press, int32_t held_ms)
{
    char line[TEXT_CHARS];
    bento_status_t status = bento_gpio_led_off(press->mode);

    if ((status == BENTO_OK) && (press->leds == 0U)) {
        /* the modulo below: ZeroDivisionError in MicroPython */
        status = BENTO_E_INVALID_ARGUMENT;
    }
    if (status == BENTO_OK) {
        press->mode = (press->mode + 1U) % press->leds;
        status = bento_gpio_led_on(press->mode);
    }
    if (status == BENTO_OK) {
        press->decision_level = DEC_SHORT;
        (void)snprintf(line, sizeof(line), "แตะสั้น -> โหมด %" PRIu32,
                       press->mode);
        (void)bento_ui_set_text(screen->ui, screen->verdict, line,
                                (uint32_t)strlen(line));
        (void)bento_ui_set_color(screen->ui, screen->verdict, COLOR_OK);
        (void)snprintf(line, sizeof(line),
                       "แตะ %" PRId32 " ms -> โหมด %" PRIu32, held_ms,
                       press->mode);
        (void)bento_ui_set_text(screen->ui, screen->status, line,
                                (uint32_t)strlen(line));
        (void)bento_lcd_print(line);
    }
    return status;
}

/* An edge counts only once the debounce window has passed. Pressing
 * starts the timer; releasing decides. */
static bento_status_t follow_edge(const screen_t *screen,
                                  press_t *press, uint32_t level,
                                  uint32_t now_ms)
{
    bento_status_t status = BENTO_OK;

    if ((level != press->prev) &&
        (bento_time_ticks_diff(now_ms, press->last_edge_ms) >
         DEBOUNCE_MS)) {
        press->last_edge_ms = now_ms;
        if (level == BUTTON_PRESSED) {
            press->down_ms = now_ms;
        } else {
            const int32_t held_ms =
                bento_time_ticks_diff(now_ms, press->down_ms);

            press->decision_ms = now_ms;
            status = (held_ms >= LONG_MS)
                         ? decide_long(screen, press, held_ms)
                         : decide_short(screen, press, held_ms);
        }
        press->prev = level;
    }
    return status;
}

static bento_status_t run_round(const screen_t *screen, press_t *press)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    uint32_t level = BUTTON_RELEASED;
    bento_status_t status = bento_gpio_button_value(BUTTON, &level);

    if (status == BENTO_OK) {
        status = follow_edge(screen, press, level, now_ms);
    }
    if (status == BENTO_OK) {
        show_held(screen, press, level, now_ms);
        sample_chart(screen, press, level, now_ms);
        poll_display(screen->ui);
        status = bento_time_sleep_ms(LOOP_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Mode 0's LED on, the button's first level, the clocks, then ROUNDS
 * rounds. */
static bento_status_t run_rounds(const screen_t *screen, uint32_t leds)
{
    press_t press;
    bento_status_t status;
    int32_t i;

    (void)memset(&press, 0, sizeof(press));
    press.leds = leds;
    status = bento_gpio_led_on(press.mode);
    if (status == BENTO_OK) {
        status = bento_gpio_button_value(BUTTON, &press.prev);
    }
    if (status == BENTO_OK) {
        press.last_edge_ms = bento_time_ticks_ms();
        press.sample_ms = bento_time_ticks_ms();
        press.decision_ms = bento_time_ticks_ms();
    }
    for (i = 0; (i < ROUNDS) && (status == BENTO_OK); i++) {
        status = run_round(screen, &press);
    }
    return status;
}

int main(void)
{
    screen_t screen;
    /* gpio.button(0) refuses a button the board does not have */
    bento_status_t status = (BUTTON < bento_gpio_num_buttons())
                                ? BENTO_OK
                                : BENTO_E_INVALID_ARGUMENT;
    const uint32_t leds = bento_gpio_num_leds();

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    if (status == BENTO_OK) {
        status = greet();
    }
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    if (status == BENTO_OK) {
        poll_display(screen.ui);
        status = run_rounds(&screen, leds);
    }
    if (status == BENTO_OK) {
        const char *closing = "จบ - คำตัดสินเกิดตอนปล่อย ไม่ใช่ตอนกด";

        (void)bento_ui_set_text(screen.ui, screen.status, closing,
                                (uint32_t)strlen(closing));
        (void)printf("จบ ลองสังเกตว่าคำตัดสินเกิดตอนปล่อย "
                     "ไม่ใช่ตอนกด\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
