/*
 * 05_debounce_count.c -- นับการกดให้ตรง ด้วยการรอให้ปุ่มนิ่งก่อน (Debounce Count)
 *
 * การกันเด้งคือกฎข้อเดียว: ค่าต้องนิ่งครบเวลาหนึ่ง (DEBOUNCE_MS) ก่อนเราจะเชื่อ
 * เลขสองตัววิ่งคู่กัน ส้มคือนับดิบทุกขอบกด เขียวคือนับหลังกันเด้ง และบรรทัดส่วนต่าง
 * กดสิบครั้งช้า ๆ แล้วกดรัว ๆ อีกสิบครั้ง เทียบว่าส่วนต่างต่างกันไหม ตัวอย่างเดิน 20 วินาที
 * นับเฉพาะขอบ "เริ่มกด" ถ้าลืม if ข้อนี้ การปล่อยจะถูกนับด้วย และเลขจะขึ้นทีละสอง
 *
 * Debouncing is one rule: a value must stay still for DEBOUNCE_MS
 * before we believe it. Two counters run side by side: orange counts
 * every raw press edge, green counts presses after debouncing, and a
 * line shows the difference. Press ten times slowly, then ten times
 * fast, and compare the differences. The example runs for 20 seconds.
 * Only the "press" edge is counted; without that check a release counts
 * too and the number climbs by two.
 *
 * Twin of 05_debounce_count.py
 * (embedded-systems-for-aiot-developer/examples/s03/05_debounce_count.py).
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
    RAW_X = 40,
    CLEAN_X = 380,
    COUNTER_Y = 60,
    COUNTER_W = 140,
    COUNTER_H = 56,
    DIFFERENCE_X = 40,
    DIFFERENCE_Y = 136,
    SETTINGS_Y = 188,
    STATE_Y = 228,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* Timing, in milliseconds. */
enum {
    DEBOUNCE_MS = 40, /* still this long before we believe it */
    POLL_MS = 5,      /* how often the button is read */
    SETTLE_MS = 200,  /* let the cleared screen settle */
    RUN_MS = 20000
};

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t raw;
    bento_ui_id_t clean;
    bento_ui_id_t difference;
    bento_ui_id_t settings;
    bento_ui_id_t state;
} debounce_screen_t;

/* The two counts and what the debouncer believes. */
typedef struct {
    int32_t raw_count;   /* every press edge, nothing filtered */
    int32_t clean_count; /* only presses that passed the debouncer */
    bool last_raw;       /* the raw reading of the round before */
    bool stable;         /* the value believed so far */
    uint32_t last_change_ms;
} debounce_t;

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
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
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_INFO = 0x4A9EFF
};

/* ui.poll() here only keeps the screen drawn; its events are not read,
 * and a failed poll answers no events, so its status is not an error.
  */
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

static void show_number(bento_ui_t *ui, bento_ui_id_t widget,
                        int32_t number)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, number);
    show_text(ui, widget, text);
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

static bento_status_t add_counter(bento_ui_t *ui, int16_t x,
                                  uint32_t color, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0";
    args.text_len = 1U;
    args.x = x;
    args.y = COUNTER_Y;
    args.w = COUNTER_W;
    args.h = COUNTER_H;
    args.color = color;
    return bento_ui_seg7_create(ui, &args, out_id);
}

/* The two counters side by side, orange raw on the left, green
 * debounced on the right, and one line that says which is which. */
static bento_status_t build_counters(debounce_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t title = {MARGIN_X, TITLE_Y};
    const point_t difference = {DIFFERENCE_X, DIFFERENCE_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "นับดิบ เทียบ นับกันเด้ง", title, COLOR_TEXT,
                           FONT_TITLE_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_counter(ui, RAW_X, COLOR_WARN, &screen->raw);
    }
    if (status == BENTO_OK) {
        status = add_counter(ui, CLEAN_X, COLOR_OK, &screen->clean);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "ส้ม = ดิบ | เขียว = กันเด้ง", difference,
                      COLOR_INFO, FONT_TEXT_PX, &screen->difference);
    }
    return status;
}

/* The settings are written on the screen, not in a comment: the person
 * pressing the button is not reading the code. */
static bento_status_t build_notes(debounce_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t settings = {MARGIN_X, SETTINGS_Y};
    const point_t state = {MARGIN_X, STATE_Y};
    char text[TEXT_BYTES];
    bento_status_t status =
        add_label(ui, "DEBOUNCE_MS กับ POLL_MS", settings, COLOR_DIM,
                  FONT_TEXT_PX, &screen->settings);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "DEBOUNCE_MS = %d ms | POLL_MS = %d ms",
                       DEBOUNCE_MS, POLL_MS);
        show_text(ui, screen->settings, text);
        status = add_label(ui, "กดช้า ๆ สิบครั้ง", state, COLOR_INFO,
                           FONT_TEXT_PX, &screen->state);
    }
    if (status == BENTO_OK) {
        show_text(ui, screen->state,
                  "กดช้า ๆ สิบครั้ง แล้วกดรัว ๆ "
                  "อีกสิบครั้ง");
    }
    return status;
}

/* Both counts and their difference on one line, to compare at a
 * glance. */
static void show_counts(const debounce_screen_t *screen,
                        const debounce_t *counts)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "ส้ม = ดิบ %" PRId32 " | เขียว = กันเด้ง %" PRId32
                   " | ส่วนต่าง %" PRId32,
                   counts->raw_count, counts->clean_count,
                   counts->raw_count - counts->clean_count);
    show_text(screen->ui, screen->difference, text);
}

/* A difference of zero does not mean the button never bounced, only
 * that it was pressed slowly enough this time. Below 10 ms bounces get
 * through; above 200 ms the button feels slow. */
static void show_end(const debounce_screen_t *screen,
                     const debounce_t *counts)
{
    char text[TEXT_BYTES];

    (void)bento_lcd_console("<span class=muted>---</span>");
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>สรุป ดิบ %" PRId32 " | กันเด้ง %" PRId32
                   " </span>",
                   counts->raw_count, counts->clean_count);
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "<span class=muted>ส่วนต่าง %" PRId32 " ครั้ง</span>",
                   counts->raw_count - counts->clean_count);
    (void)bento_lcd_print(text);
    show_text(screen->ui, screen->settings,
              "ต่ำกว่า 10 ยังเด้งหลุด | สูงกว่า 200 ปุ่มจะหน่วง");
    (void)snprintf(text, sizeof(text),
                   "หมดเวลา - ดิบ %" PRId32 " | กันเด้ง %" PRId32,
                   counts->raw_count, counts->clean_count);
    show_text(screen->ui, screen->state, text);
    (void)bento_ui_set_color(screen->ui, screen->state, COLOR_OK);
    poll_screen(screen->ui);
}

enum {
    USER_BUTTON = 0 /* the board's only user button */
};

/* The rule and the counters it updates are one function, kept whole:
 * it lives in main.c with the reading loop. */
/* One reading. A raw change is counted at once if it is a press, as a
 * naive program does, and restarts the stillness timer; a value still
 * for DEBOUNCE_MS becomes the believed one, and only a press edge of
 * that is counted. */
static void debounce(const debounce_screen_t *screen,
                     debounce_t *counts, bool raw, uint32_t now_ms)
{
    char text[TEXT_BYTES];

    if (raw != counts->last_raw) {
        if (raw) {
            counts->raw_count++;
            show_number(screen->ui, screen->raw, counts->raw_count);
            show_counts(screen, counts);
        }
        counts->last_raw = raw;
        counts->last_change_ms = now_ms;
    } else if ((raw != counts->stable) &&
               (bento_time_ticks_diff(now_ms, counts->last_change_ms) >=
                DEBOUNCE_MS)) {
        counts->stable = raw;
        if (counts->stable) {
            counts->clean_count++;
            show_number(screen->ui, screen->clean, counts->clean_count);
            show_counts(screen, counts);
            (void)snprintf(text, sizeof(text),
                           "ดิบ %" PRId32 " | กันเด้ง %" PRId32,
                           counts->raw_count, counts->clean_count);
            /* lcd.print() ignores a console that is not ready */
            (void)bento_lcd_print(text);
        }
    } else {
        /* no change, or not still for long enough yet */
    }
}

/* Reads the button every POLL_MS; the screen needs a poll on every
 * round too. */
static bento_status_t run_loop(const debounce_screen_t *screen,
                               debounce_t *counts)
{
    bento_status_t status =
        bento_gpio_button_is_pressed(USER_BUTTON, &counts->last_raw);
    const uint32_t start_ms = bento_time_ticks_ms();

    counts->stable = counts->last_raw;
    counts->last_change_ms = start_ms;
    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        /* one clock read, shared by both counts */
        const uint32_t now_ms = bento_time_ticks_ms();
        bool raw = false;

        status = bento_gpio_button_is_pressed(USER_BUTTON, &raw);
        if (status == BENTO_OK) {
            debounce(screen, counts, raw, now_ms);
            poll_screen(screen->ui);
            status = pause_ms(POLL_MS);
        }
    }
    return status;
}

int main(void)
{
    debounce_screen_t screen;
    debounce_t counts = {0, 0, false, false, 0U};
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    /* lcd calls ignore a console that is not ready (modlcd.c) */
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>นับดิบ เทียบ นับกันเด้ง</h2>");
    (void)bento_lcd_print(
        "<span class=muted>กดปุ่มบนบอร์ดสิบครั้งช้า ๆ</span>");
    status = button_exists(USER_BUTTON) ? BENTO_OK
                                        : BENTO_E_INVALID_ARGUMENT;
    if (status == BENTO_OK) {
        status = build_counters(&screen);
    }
    if (status == BENTO_OK) {
        status = build_notes(&screen);
    }
    if (status == BENTO_OK) {
        status = run_loop(&screen, &counts);
    }
    if (status == BENTO_OK) {
        show_end(&screen, &counts);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
