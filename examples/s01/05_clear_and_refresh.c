/*
 * 05_clear_and_refresh.c -- นับถอยหลัง (Countdown: write over, or
 * clear and redraw)
 *
 * อัปเดตซ้ำที่เดิม ต่างจากไล่พิมพ์ลงมา ท่าลิ้นชักคือ lcd.clear() แล้ววาดใหม่
 * ทั้งหน้าทุกรอบ ท่า widget คือสร้างครั้งเดียวแล้วเขียนทับค่าเดิม ตัวเลขใหญ่
 * นับถอยหลัง 5 4 3 2 1 0 อยู่ที่เดิม พร้อมวงแหวนที่ค่อย ๆ ปิดลง เหลือสอง
 * วินาทีสุดท้ายทั้งคู่เป็นสีเหลือง กับดัก: ล้างจอถี่กว่าราว 5 ครั้งต่อวินาที
 * ตาจะเห็นเป็นจอกระพริบ
 *
 * Update in place instead of printing line after line. The drawer way
 * clears and redraws the whole page every round; the widget way makes
 * a widget once and writes the new value over the old one, which is
 * far cheaper. The big number counts 5 4 3 2 1 0 in one place with a
 * ring closing beside it; both turn yellow for the last two seconds.
 * Trap: clearing more than about 5 times a second looks like flicker.
 *
 * Twin of 05_clear_and_refresh.py
 * (embedded-systems-for-aiot-developer/examples/s01/05_clear_and_refresh.py).
 * Style: CLEAN_C.md.
 * Where C differs: lcd.print(a, b, ...) joins its arguments with one
 * space; here snprintf() builds the same line first.
 *
 * ตาคุณ: ตั้ง TICK_MS = 150 และ COUNTDOWN_S = 20 แล้วดูว่าท่าไหนอ่านออก
 * ท่าไหนกลายเป็นจอกระพริบ
 * Your turn: set TICK_MS = 150 and COUNTDOWN_S = 20, run, and say
 * which way stays readable and which one flickers.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

enum {
    COUNTDOWN_S = 5, /* the countdown starts here */
    /* the last seconds turn yellow: the eye reads a colour first */
    WARN_FROM_S = 2,
    TICK_MS = 1000,  /* one step of the countdown */
    SETTLE_MS = 200, /* let the cleared screen settle */
    /* a label takes at most 126 bytes, a drawer line 127 */
    TEXT_CHARS = 128,
    NUMBER_CHARS = 12 /* the longest int32_t */
};

/* The widgets the program writes over, created once. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t digits;
    bento_ui_id_t ring;
    bento_ui_id_t note;
    bento_ui_id_t elapsed;
} countdown_screen_t;

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    FONT_PX = 20,
    TITLE_FONT_PX = 24,
    TITLE_X = 20,
    TITLE_Y = 12,
    CARD_X = 20,
    CARD_Y = 52,
    /* ends left of the board's Console button, bottom right */
    CARD_W = 652,
    CARD_H = 200,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    DIGITS_X = 60,
    DIGITS_Y = 92,
    DIGITS_W = 152,
    DIGITS_H = 112,
    RING_X = 280,
    RING_Y = 80,
    RING_SIZE = 132,
    /* short enough to stay inside the card, whose right edge is x =
     * 670 */
    NOTE_X = 440,
    NOTE_Y = 120,
    ELAPSED_X = 20,
    ELAPSED_Y = 272
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623
};

/* A widget's top-left corner. */
typedef struct {
    int16_t x;
    int16_t y;
} point_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                point_t at, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
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

/* The widget setters (text, value, colour) ignore a refusal in
 * MicroPython (modui.c), so they do here too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t widget,
                      const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

/* ui.poll() hands the display's touches over; nothing here reads them,
 * and a failed poll answers no events, so its result is not needed. */
static void poll_display(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The card: its panel, the Seg7 number, the ring and the note beside
 * them. */
static bento_status_t add_card(countdown_screen_t *screen)
{
    const int32_t start_s = COUNTDOWN_S;
    char digits[NUMBER_CHARS];
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CARD_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border colour */
    args.max_val = CARD_RADIUS; /* and max= its corner radius */
    args.font_px = CARD_BORDER_PX;
    status = bento_ui_panel_create(screen->ui, &args, &panel);
    if (status == BENTO_OK) {
        /* a Seg7 shows text */
        (void)snprintf(digits, sizeof(digits), "%" PRId32, start_s);
        bento_ui_widget_args_init(&args);
        args.text = digits;
        args.text_len = (uint32_t)strlen(digits);
        args.x = DIGITS_X;
        args.y = DIGITS_Y;
        args.w = DIGITS_W;
        args.h = DIGITS_H;
        args.color = COLOR_OK;
        status =
            bento_ui_seg7_create(screen->ui, &args, &screen->digits);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = RING_X;
        args.y = RING_Y;
        args.w = RING_SIZE;
        args.h = RING_SIZE;
        args.color = COLOR_OK;
        args.min_val = 0;
        args.max_val = COUNTDOWN_S;
        args.font_px = COUNTDOWN_S; /* the ring starts full */
        status = bento_ui_arc_create(screen->ui, &args, &screen->ring);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "อยู่ที่เดิมทุกวินาที",
                           (point_t){NOTE_X, NOTE_Y}, COLOR_DIM,
                           FONT_PX, &screen->note);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets' handles
 * follow it). The widgets are created once, here, never in the loop: a
 * widget a second would soon reach the screen's limit of 32. */
static bento_status_t build_screen(countdown_screen_t *screen)
{
    bento_ui_id_t title;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "นับถอยหลัง",
                           (point_t){TITLE_X, TITLE_Y}, COLOR_TEXT,
                           TITLE_FONT_PX, &title);
    }
    if (status == BENTO_OK) {
        status = add_card(screen);
    }
    /* never an empty label: LVGL would show "Label" until written */
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่เริ่มจับเวลา",
                           (point_t){ELAPSED_X, ELAPSED_Y}, COLOR_DIM,
                           FONT_PX, &screen->elapsed);
    }
    return status;
}

/* The widget way: write the new value over the old one, nothing is
 * cleared. */
static void show_left_on_screen(const countdown_screen_t *screen,
                                int32_t left_s)
{
    char digits[NUMBER_CHARS];

    (void)snprintf(digits, sizeof(digits), "%" PRId32, left_s);
    show_text(screen->ui, screen->digits, digits);
    (void)bento_ui_set_value(screen->ui, screen->ring, left_s);
    if (left_s <= WARN_FROM_S) {
        (void)bento_ui_set_color(screen->ui, screen->digits,
                                 COLOR_WARN);
        (void)bento_ui_set_color(screen->ui, screen->ring, COLOR_WARN);
    }
}

/* The drawer way: clear it and draw the whole page again, or the
 * numbers pile up. The drawer's calls ignore a console that is not
 * ready (modlcd.c), here as in MicroPython. */
static void show_left_in_drawer(int32_t left_s)
{
    char line[TEXT_CHARS];

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>นับถอยหลัง</h2>");
    if (left_s == 0) {
        (void)bento_lcd_print("<span class=ok>หมดเวลา</span>");
    } else if (left_s <= WARN_FROM_S) {
        (void)snprintf(
            line, sizeof(line),
            "<span class=warn>เหลืออีก %" PRId32 " วินาที</span>", left_s);
        (void)bento_lcd_print(line);
    } else {
        (void)snprintf(line, sizeof(line), "เหลืออีก %" PRId32 " วินาที",
                       left_s);
        (void)bento_lcd_print(line);
    }
}

/* The time really passed since start_ms: drawing takes time too, so the
 * loop's clock runs behind the real one. */
static void show_elapsed(const countdown_screen_t *screen,
                         uint32_t start_ms)
{
    const int32_t elapsed_ms =
        bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);
    char text[TEXT_CHARS];

    (void)snprintf(text, sizeof(text), "ผ่านไปแล้ว %" PRId32 " ms",
                   elapsed_ms);
    show_text(screen->ui, screen->elapsed, text);
    (void)snprintf(text, sizeof(text),
                   "<span class=muted>ผ่านไปแล้ว %" PRId32 " ms</span>",
                   elapsed_ms);
    (void)bento_lcd_print(text);
}

/* The last page stays up to be read: a program that clears the screen
 * last leaves the reader with nothing. */
static void show_total(const countdown_screen_t *screen,
                       uint32_t start_ms)
{
    const int32_t total_ms =
        bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);
    const int32_t planned_ms = COUNTDOWN_S * TICK_MS;
    char text[TEXT_CHARS];

    show_text(screen->ui, screen->digits, "0");
    show_text(screen->ui, screen->note, "จบแล้ว ค่าค้างไว้ให้อ่าน");
    (void)snprintf(text, sizeof(text),
                   "รวมทั้งหมด %" PRId32 " ms - ตั้งใจไว้ %" PRId32 " ms",
                   total_ms, planned_ms);
    show_text(screen->ui, screen->elapsed, text);
    (void)snprintf(text, sizeof(text),
                   "<span class=muted>รวมทั้งหมด %" PRId32 " ms</span>",
                   total_ms);
    (void)bento_lcd_print(text);
    poll_display(screen->ui);
}

/* One step a TICK_MS from COUNTDOWN_S down to 0, until Stop. */
static bento_status_t run_countdown(const countdown_screen_t *screen,
                                    uint32_t start_ms)
{
    int32_t left_s;
    bento_status_t status = BENTO_OK;

    for (left_s = COUNTDOWN_S; (left_s >= 0) && (status == BENTO_OK);
         left_s--) {
        show_left_on_screen(screen, left_s);
        show_left_in_drawer(left_s);
        show_elapsed(screen, start_ms);
        poll_display(screen->ui);
        status = bento_time_sleep_ms(TICK_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    countdown_screen_t screen = {bento_runtime_get_ui(), 0U, 0U, 0U,
                                 0U};
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        /* ticks, not the calendar: milliseconds since the board
         * started */
        const uint32_t start_ms = bento_time_ticks_ms();

        status = run_countdown(&screen, start_ms);
        if (status == BENTO_OK) {
            show_total(&screen, start_ms);
        }
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
