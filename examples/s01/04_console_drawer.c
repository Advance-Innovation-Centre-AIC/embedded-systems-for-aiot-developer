/*
 * 04_console_drawer.c -- ลิ้นชัก Console กับการ์ดสรุป (The Console
 * drawer and a summary card)
 *
 * รายงานผลตรวจยาวสิบบรรทัด คนดูหน้างานอยากรู้บรรทัดเดียวว่า "ผ่านกี่ข้อ
 * จากกี่ข้อ" การ์ดบนสุดนับขึ้นจาก 0/4 พร้อมแถบความคืบหน้า รายการสี่ข้อ
 * ไล่โผล่ทีละข้อ เขียวคือผ่าน แดงคือไม่ผ่าน รายละเอียดอยู่ในลิ้นชัก Console
 * กับดัก: มุมขวาล่างราว 100x58 พิกเซลเป็นของปุ่ม Console ห้ามวาง widget ทับ
 *
 * A ten-line check report is more than the person on site wants: they
 * want one line, "how many passed of how many". The card on top counts
 * up from 0/4 with a progress bar, and the four items appear one by
 * one, green for a pass and red for a failure. lcd.console() writes
 * headings and rules (layout), lcd.print() the data. Trap: the bottom
 * right 100 x 58 px belong to the board's Console button; a widget
 * there stops the drawer from opening.
 *
 * Twin of 04_console_drawer.py
 * (embedded-systems-for-aiot-developer/examples/s01/04_console_drawer.py).
 * Style: CLEAN_C.md.
 * Where C differs: lcd.print(a, b, ...) joins its arguments with one
 * space; here snprintf() builds the same line first.
 *
 * ตาคุณ: แก้ CHECKS ให้ผ่านครบ แล้วเพิ่มรายการที่ห้า ดูว่าต้องแก้อะไรอีก
 * Your turn: make every CHECKS item pass, then add a fifth item and
 * see what else must change (hint: is the last row still inside the
 * 398 px screen?).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

enum {
    CHECK_COUNT = 4,
    TEXT_BYTES = 128 /* a label takes at most 126 bytes */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    CHECK_MS = 500   /* one check appears this often */
};

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    FONT_PX = 20,
    TITLE_FONT_PX = 24,
    TITLE_X = 20,
    TITLE_Y = 12,
    CARD_X = 20,
    CARD_Y = 52,
    CARD_W = 652, /* ends left of the board's Console button */
    CARD_H = 112,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    CAPTION_X = 40,
    CAPTION_Y = 62,
    SCORE_X = 40,
    SCORE_Y = 96,
    SCORE_W = 160,
    SCORE_H = 48,
    PROGRESS_X = 220,
    PROGRESS_Y = 104,
    PROGRESS_W = 432,
    PROGRESS_H = 24,
    ROW_X = 40,
    FIRST_ROW_Y = 180,
    ROW_STEP = 34,
    SUMMARY_X = 20,
    SUMMARY_Y = 324,
    DETAILS_Y = 352
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* One item of the report and its result. */
typedef struct {
    const char *name;
    bool ok;
} check_t;

/* A widget's top-left corner. */
typedef struct {
    int16_t x;
    int16_t y;
} point_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t score;
    bento_ui_id_t progress;
    bento_ui_id_t summary;
    bento_ui_id_t details;
} report_screen_t;

/* A position on the screen: the widget arguments are 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

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

/* Nothing here reads the touches, and a failed poll answers no
 * events, so its result is not needed. */
static void poll_display(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t add_panel(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;

    bento_ui_widget_args_init(&args);
    args.x = CARD_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border colour */
    args.max_val = CARD_RADIUS; /* and max= its corner radius */
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* A Seg7 shows text, so "0/4" can be written as it is. */
static bento_status_t add_score(report_screen_t *screen)
{
    char score_text[TEXT_BYTES];
    bento_ui_widget_args_t args;

    (void)snprintf(score_text, sizeof(score_text), "0/%d", CHECK_COUNT);
    bento_ui_widget_args_init(&args);
    args.text = score_text;
    args.text_len = (uint32_t)strlen(score_text);
    args.x = SCORE_X;
    args.y = SCORE_Y;
    args.w = SCORE_W;
    args.h = SCORE_H;
    args.color = COLOR_OK;
    return bento_ui_seg7_create(screen->ui, &args, &screen->score);
}

static bento_status_t add_progress(report_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = PROGRESS_X;
    args.y = PROGRESS_Y;
    args.w = PROGRESS_W;
    args.h = PROGRESS_H;
    args.min_val = 0;
    args.max_val = CHECK_COUNT;
    args.font_px = 0;
    return bento_ui_bar_create(screen->ui, &args, &screen->progress);
}

/* The card: its panel, the caption, the passed-total score and the
 * progress bar. */
static bento_status_t add_card(report_screen_t *screen)
{
    const point_t caption = {CAPTION_X, CAPTION_Y};
    bento_ui_id_t unused;
    bento_status_t status = add_panel(screen->ui);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ผ่านแล้ว", caption, COLOR_DIM,
                           FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_score(screen);
    }
    if (status == BENTO_OK) {
        status = add_progress(screen);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(report_screen_t *screen)
{
    const point_t title_at = {TITLE_X, TITLE_Y};
    const point_t summary_at = {SUMMARY_X, SUMMARY_Y};
    const point_t details_at = {SUMMARY_X, DETAILS_Y};
    bento_ui_id_t title;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "รายงานตรวจสอบบอร์ด", title_at,
                           COLOR_TEXT, TITLE_FONT_PX, &title);
    }
    if (status == BENTO_OK) {
        status = add_card(screen);
    }
    /* both summary labels exist from the start and are written over at
     * the end: labels made later in the if / else would stack at the
     * same place */
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังตรวจไม่จบ", summary_at,
                           COLOR_DIM, FONT_PX, &screen->summary);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "รายละเอียดอยู่ในลิ้นชัก Console",
                      details_at, COLOR_DIM, FONT_PX, &screen->details);
    }
    return status;
}

/* One item: its row on the screen, its line in the drawer, and the
 * card's new score. lcd.print() ignores a console that is not ready
 * (modlcd.c). */
static bento_status_t report_check(const report_screen_t *screen,
                                   const check_t *check, int32_t passed,
                                   int32_t row_y)
{
    const point_t row_at = {ROW_X, px(row_y)};
    char text[TEXT_BYTES];
    bento_ui_id_t row;
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "%s  %s", check->name,
                   check->ok ? "ผ่าน" : "ไม่ผ่าน");
    status = add_label(screen->ui, text, row_at,
                       check->ok ? COLOR_OK : COLOR_BAD, FONT_PX, &row);
    if (status == BENTO_OK) {
        /* a line for a failure too: a report that speaks only on
         * success is silent exactly when the reader most wants to
         * know */
        (void)snprintf(text, sizeof(text), "%s %s", check->name,
                       check->ok ? "<span class=ok>ผ่าน</span>"
                                 : "<span class=error>ไม่ผ่าน</span>");
        (void)bento_lcd_print(text);
        (void)snprintf(text, sizeof(text), "%d-%d", (int)passed,
                       CHECK_COUNT);
        show_text(screen->ui, screen->score, text);
        (void)bento_ui_set_value(screen->ui, screen->progress, passed);
    }
    return status;
}

/* The items one by one, CHECK_MS apart; answers how many passed. The
 * data sits in one table, so a new item changes only the table. */
static bento_status_t run_checks(const report_screen_t *screen,
                                 int32_t *out_passed)
{
    static const check_t checks[CHECK_COUNT] = {
        {"จอแสดงผล", true},
        {"ปุ่มบนบอร์ด", true},
        {"หลอด LED", true},
        {"การ์ด SD", false},
    };
    int32_t passed = 0;
    int32_t row_y = FIRST_ROW_Y;
    int32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0; (i < CHECK_COUNT) && (status == BENTO_OK); i++) {
        passed += checks[i].ok ? 1 : 0;
        status = report_check(screen, &checks[i], passed, row_y);
        row_y += ROW_STEP;
        if (status == BENTO_OK) {
            poll_display(screen->ui);
            status = sleep_checked(CHECK_MS);
        }
    }
    *out_passed = passed;
    return status;
}

/* The summary's colour comes from the real result, never painted
 * green first: the card and the drawer tell the same story. The
 * summary is two labels because one carries 126 bytes and a Thai
 * character takes 3. */
static void show_summary(const report_screen_t *screen, int32_t passed)
{
    const bool all_passed = (passed == CHECK_COUNT);
    const uint32_t color = all_passed ? COLOR_OK : COLOR_WARN;
    char text[TEXT_BYTES];

    (void)bento_lcd_console(
        "<span class=muted>------------------------</span>");
    (void)bento_ui_set_color(screen->ui, screen->score, color);
    show_text(screen->ui, screen->summary,
              all_passed ? "ผ่านครบทุกรายการ" : "ยังมีรายการที่ตก");
    (void)bento_ui_set_color(screen->ui, screen->summary, color);
    (void)bento_ui_set_color(screen->ui, screen->details,
                             all_passed ? COLOR_DIM : COLOR_WARN);
    if (all_passed) {
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>ผ่านครบ %d รายการ</span>",
                       (int)passed);
    } else {
        (void)snprintf(text, sizeof(text),
                       "<span class=warn>ผ่าน %d จาก %d </span>",
                       (int)passed, CHECK_COUNT);
    }
    (void)bento_lcd_print(text);
}

int main(void)
{
    report_screen_t screen = {bento_runtime_get_ui(), 0U, 0U, 0U, 0U};
    int32_t passed = 0;
    bento_status_t status = build_screen(&screen);

    /* the headings go through lcd.console(): they are layout, not
     * data; the drawer ignores a console that is not ready */
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>รายงานตรวจสอบบอร์ด</h2>");
        (void)bento_lcd_console("<span class=muted>เริ่มตรวจ</span>");
        status = run_checks(&screen, &passed);
    }
    if (status == BENTO_OK) {
        show_summary(&screen, passed);
        poll_display(screen.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
