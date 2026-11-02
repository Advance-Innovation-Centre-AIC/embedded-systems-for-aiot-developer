/*
 * 04_button_active_low.c -- ปุ่มนี้ 0 คือกด ไม่ใช่ 1
 * (Active-Low Button)
 *
 * value() คืนแรงดันดิบบนขา 1 = ปล่อย 0 = กด ส่วน is_pressed()
 * คืนความหมายที่คนเข้าใจ สองตัวนี้กลับด้านกัน
 * เลขตัวใหญ่ทางซ้ายคือค่าดิบ คำทางขวาคือความหมาย กดปุ่มผู้ใช้ค้างไว้
 * แล้วเลขจะเป็น 0 คำจะเป็น กดอยู่ และเส้นกราฟตกลงพร้อมกัน
 * ลิ้นชักได้บรรทัดเฉพาะตอนค่าเปลี่ยน ตัวอย่างเดิน 15 วินาที
 * ปุ่มผู้ใช้มีตัวเดียวคือดัชนี 0 และชื่อจาก name() ไม่ใช่ป้ายบนบอร์ด
 *
 * value() answers the raw pin level (1 released, 0 pressed);
 * is_pressed() answers what it means. The two are opposite. The big
 * digit on the left is the raw value, the words on the right are its
 * meaning: hold the user button and the digit turns 0, the words say
 * pressed, and the chart line drops. The Console gets a line only when
 * the value changes. The example runs for 15 seconds. The board has one
 * user button, index 0, named by name().
 *
 * Twin of 04_button_active_low.py
 * (embedded-systems-for-aiot-developer/examples/s03/04_button_active_low.py).
 * Style: CLEAN_C.md.
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

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 20,
    TITLE_Y = 12,
    RAW_Y = 52,
    RAW_W = 92,
    RAW_H = 64,
    MEANING_X = 132,
    MEANING_Y = 76,
    CHART_Y = 136,
    CHART_W = 652,
    CHART_H = 112,
    LEGEND_Y = 256,
    NAME_Y = 292,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

enum {
    USER_BUTTON = 0,    /* the board's only user button */
    LEVEL_PRESSED = 0,  /* the chart spans exactly the two pin levels */
    LEVEL_RELEASED = 1, /* pulled up when not pressed */
    /* nothing shown yet, so the first reading counts as a change */
    LEVEL_UNKNOWN = -1
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    POLL_MS = 100,
    RUN_MS = 15000
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t raw;
    bento_ui_id_t meaning;
    bento_ui_id_t chart;
    uint8_t series;
    bento_ui_id_t legend;
    bento_ui_id_t name;
} button_screen_t;

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The Console's opening lines, with the button's name read again as
 * the original does. */
static bento_status_t print_intro(void)
{
    const char *button_name = NULL;
    char text[TEXT_BYTES];
    bento_status_t status;

    /* lcd calls ignore a console that is not ready (modlcd.c) */
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>ปุ่ม active-low</h2>");
    (void)bento_lcd_print("<span "
                          "class=warn>ปุ่มผู้ใช้ตัวเดียวของบอร์ด</span"
                          ">");
    status = bento_gpio_button_name(USER_BUTTON, &button_name);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "<span class=warn>โค้ดเรียกว่า %s ดัชนี "
                       "0</span>",
                       button_name);
        (void)bento_lcd_print(text);
        (void)bento_lcd_print(
            "<span class=muted>กดปุ่มค้างไว้แล้วดูสองฝั่ง</span>");
    }
    return status;
}

/* gpio.button(i) refuses an index the board does not have. */
static bool button_exists(uint32_t button)
{
    return button < bento_gpio_num_buttons();
}

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_INFO = 0x4A9EFF
};

static const char TEXT_RELEASED[] = "value() = 1  is_pressed() = ปล่อย";

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

/* ui.poll() here only keeps the screen drawn; its events are not read,
 * and a failed poll answers no events, so its status is not an
 * error. */
static void poll_screen(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

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

static bento_status_t add_raw(button_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "1";
    args.text_len = 1U;
    args.x = MARGIN_X;
    args.y = RAW_Y;
    args.w = RAW_W;
    args.h = RAW_H;
    args.color = COLOR_INFO;
    return bento_ui_seg7_create(screen->ui, &args, &screen->raw);
}

static bento_status_t add_chart(button_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.color = COLOR_CARD;
    args.min_val = LEVEL_PRESSED;
    args.max_val = LEVEL_RELEASED;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_WARN, &screen->series);
    }
    return status;
}

/* Raw value on the left and its meaning on the right, side by side,
 * then the chart. */
static bento_status_t build_top(button_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t title = {MARGIN_X, TITLE_Y};
    const point_t meaning = {MEANING_X, MEANING_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ปุ่ม active-low", title, COLOR_TEXT,
                           FONT_TITLE_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_raw(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, TEXT_RELEASED, meaning, COLOR_DIM,
                           FONT_TITLE_PX, &screen->meaning);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    return status;
}

/* The legend and the name line. Their full texts go through set_text,
 * whose room is wider than a create call's; the name line names the
 * button as the board does, not the print on the circuit board. */
static bento_status_t build_bottom(button_screen_t *screen)
{
    const point_t legend = {MARGIN_X, LEGEND_Y};
    const point_t name = {MARGIN_X, NAME_Y};
    const char *button_name = NULL;
    char text[TEXT_BYTES];
    bento_status_t status =
        add_label(screen->ui, "เส้นบน 1 = ปล่อย", legend, COLOR_DIM,
                  FONT_TEXT_PX, &screen->legend);

    if (status == BENTO_OK) {
        show_text(screen->ui, screen->legend,
                  "เส้นบน 1 = ปล่อย (pull-up) เส้นล่าง 0 = กด "
                  "(ลัดลงกราวด์)");
        status = add_label(screen->ui, "ปุ่มผู้ใช้ตัวเดียวของบอร์ด", name,
                           COLOR_WARN, FONT_TEXT_PX, &screen->name);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_button_name(USER_BUTTON, &button_name);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "โค้ดเรียกว่า %s ดัชนี 0 - ไม่ใช่ป้ายบนบอร์ด",
                       button_name);
        show_text(screen->ui, screen->name, text);
    }
    return status;
}

/* The value changed: the digit, the meaning, the legend and one
 * Console line follow. */
static void show_change(const button_screen_t *screen, int32_t raw,
                        bool pressed, int32_t edges)
{
    bento_ui_t *ui = screen->ui;
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, raw);
    show_text(ui, screen->raw, text);
    (void)snprintf(text, sizeof(text), "ค่าเปลี่ยนไปแล้ว %" PRId32 " ครั้ง",
                   edges);
    show_text(ui, screen->legend, text);
    if (pressed) {
        (void)bento_ui_set_color(ui, screen->raw, COLOR_WARN);
        show_text(ui, screen->meaning,
                  "value() = 0  is_pressed() = กดอยู่");
        (void)bento_ui_set_color(ui, screen->meaning, COLOR_OK);
        (void)snprintf(
            text, sizeof(text),
            "value() = %" PRId32 " -> <span class=ok>กดอยู่</span>", raw);
    } else {
        (void)bento_ui_set_color(ui, screen->raw, COLOR_INFO);
        show_text(ui, screen->meaning, TEXT_RELEASED);
        (void)bento_ui_set_color(ui, screen->meaning, COLOR_DIM);
        (void)snprintf(text, sizeof(text),
                       "value() = %" PRId32
                       " -> <span class=muted>ปล่อย</span>",
                       raw);
    }
    (void)bento_lcd_print(text);
}

/* Why the circuit is inverted: a pull-up holds the pin at 1, and the
 * button shorts it to ground, so the pin never floats. */
static void show_end(const button_screen_t *screen, int32_t edges)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "หมดเวลา - ค่าเปลี่ยนไปแล้ว %" PRId32 " ครั้ง", edges);
    show_text(screen->ui, screen->legend, text);
    show_text(screen->ui, screen->name,
              "เข้าใจวงจรด้วย value() เขียนตรรกะด้วย is_pressed()");
    (void)bento_ui_set_color(screen->ui, screen->name, COLOR_DIM);
    (void)bento_lcd_print("<span class=muted>1 = pull-up ดึงไว้</span>");
    (void)bento_lcd_print("<span class=muted>0 = "
                          "ปุ่มลัดลงกราวด์</span>");
    poll_screen(screen->ui);
}

/* The value shown last, so the Console gets a line only when it
 * changes. */
typedef struct {
    int32_t last_shown;
    int32_t edges;
} button_tally_t;

/* Reads both methods in the same round, so they can really be compared.
 * The chart gets a point every round: a steady line shows the program
 * is still running. */
static bento_status_t read_button(const button_screen_t *screen,
                                  button_tally_t *tally)
{
    uint32_t raw = 0U;
    bool pressed = false;
    bento_status_t status = bento_gpio_button_value(USER_BUTTON, &raw);

    if (status == BENTO_OK) {
        status = bento_gpio_button_is_pressed(USER_BUTTON, &pressed);
    }
    if (status == BENTO_OK) {
        const int32_t level = (int32_t)raw;

        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->series,
                                      level); /* fire and forget */
        if (level != tally->last_shown) {
            tally->last_shown = level;
            tally->edges++;
            show_change(screen, level, pressed, tally->edges);
        }
        poll_screen(screen->ui);
        status = pause_ms(POLL_MS);
    }
    return status;
}

static bento_status_t run_loop(const button_screen_t *screen,
                               button_tally_t *tally)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = read_button(screen, tally);
    }
    return status;
}

int main(void)
{
    button_screen_t screen;
    button_tally_t tally = {LEVEL_UNKNOWN, 0};
    bento_status_t status = button_exists(USER_BUTTON)
                                ? BENTO_OK
                                : BENTO_E_INVALID_ARGUMENT;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    if (status == BENTO_OK) {
        status = build_top(&screen);
    }
    if (status == BENTO_OK) {
        status = build_bottom(&screen);
    }
    if (status == BENTO_OK) {
        status = print_intro();
    }
    if (status == BENTO_OK) {
        status = run_loop(&screen, &tally);
    }
    if (status == BENTO_OK) {
        show_end(&screen, tally.edges);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
