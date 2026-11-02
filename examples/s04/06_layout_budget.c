/*
 * 06_layout_budget.c -- พื้นที่ 792x398 กับงบ widget: งบของคอร์ส 32 ตัว
 * (เพดานเฟิร์มแวร์ 64)
 *
 * หน้าจอนี้มีเส้นอยู่สามเส้น พื้นที่ 792x398 พิกเซล เพดานเฟิร์มแวร์ 64 widget
 * ต่อหน้า และงบที่คอร์สขีดเอง 32 ตัว จอมีตาราง 5x5 ปุ่ม แถบ Bar บอกว่าใช้งบ
 * ไปแล้วกี่ใน 32 และสามบรรทัดล่างสรุปผลเทียบกับ ui.list() ผลตรวจเขตห้ามวาง
 * (มุมขวาล่างของปุ่ม Console) และผลลองสร้างตัวที่ 33 ทุกช่องถูกตรวจก่อนสร้าง
 *
 * The screen has three lines: the 792 x 398 area, the firmware ceiling
 * of 64 widgets a page, and the course's own budget of 32. A 5 x 5
 * grid of buttons fills the budget, each cell checked before it is
 * made (inside the area, not under the Console button, within budget);
 * a Bar and a counter show the budget spent. Three lines compare the
 * count with ui.list(), test a point in the no-go corner, and report
 * the 33rd widget, which can be made: 32 is the course's line.
 *
 * Twin of 06_layout_budget.py
 * (embedded-systems-for-aiot-developer/examples/s04/06_layout_budget.py),
 * rewritten from the library twin aiot-s04-06-layout-budget. Style:
 * CLEAN_C.md. The try/except RuntimeError around the 33rd button
 * becomes a check of its create status. ui.list()'s table (64 entries,
 * 512 bytes) is a file-scope g_alive (C9).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* The field's numbers, so nobody has to remember them. */
enum {
    AREA_W = 792,
    AREA_H = 398,
    MAX_WIDGETS = 64,   /* the firmware ceiling (UI_MAX_WIDGETS) */
    COURSE_BUDGET = 32, /* the course's own line, half the ceiling */
    RESERVED_X = 690, /* right of this and below RESERVED_Y: Console */
    RESERVED_Y = 340,
    PROBE_X = 700,
    PROBE_Y = 350
};

/* The grid. */
enum {
    COLS = 5,
    ROWS = 5,
    CELL_W = 140,
    CELL_H = 36,
    GAP_X = 10,
    GAP_Y = 4,
    LEFT = 20,
    TOP = 72, /* below the counter label at y = 44 */
    /* the three summary labels come out of the budget too */
    RESERVE_FOR_TEXT = 3,
    HEADER_WIDGETS = 4,  /* title, caption, counter and bar */
    CELL_TEXT_BYTES = 24 /* "r,c", two int32_t: 2 x 11 and the NUL */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 50,
    RUN_MS = 15000
};

/* Screen layout. */
enum {
    TITLE_Y = 8,
    CAPTION_X = 432,
    CAPTION_Y = 6,
    COUNTER_Y = 42,
    BAR_X = 580,
    BAR_Y = 32,
    BAR_W = 192,
    BAR_H = 16,
    SUMMARY_Y = 284,
    PROBE_LINE_Y = 302,
    CEILING_Y = 332,
    EXTRA_X =
        580, /* the empty bottom-right corner, clear of the lines */
    EXTRA_Y = 308,
    EXTRA_SIDE = 88,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    FONT_CELL_PX = 14,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0x4A9EFF,
    COLOR_DIM = 0x9AA3AF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_CELL = 0x3A4150,
    COLOR_EXTRA = 0xE5484D
};

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

typedef struct {
    bento_ui_t *ui;
    int32_t
        used; /* counted by the program itself, from the first one */
    int32_t skipped;
    bento_ui_id_t counter;
    bento_ui_id_t bar;
    bento_ui_id_t summary;
    bento_ui_id_t probe;
    bento_ui_id_t ceiling;
} budget_t;

/* ui.list()'s table, 64 widgets of 8 bytes: too big for the 8 KB
 * program stack, so it is file scope, zeroed by the loader and again
 * at the start of main() (CLEAN_C.md C9, BENTO-MISRA-E03/E04). */
extern bento_ui_widget_info_t g_alive[MAX_WIDGETS];
bento_ui_widget_info_t g_alive[MAX_WIDGETS];

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

/* Records the budget spent, and moves the number and the bar together
 * in one place. */
static void spend(budget_t *budget, int32_t used)
{
    char text[TEXT_BYTES];

    budget->used = used;
    (void)snprintf(text, sizeof(text),
                   "ใช้ไป %" PRId32 " / งบ %d (เพดาน %d)", budget->used,
                   COURSE_BUDGET, MAX_WIDGETS);
    show_text(budget->ui, budget->counter, text);
    (void)bento_ui_set_value(budget->ui, budget->bar, budget->used);
}

/* The Bar turns the budget into an amount you can see; full means the
 * course's budget, not the board's ceiling. */
static bento_status_t add_header(budget_t *budget)
{
    bento_ui_t *ui = budget->ui;
    const point_t title = {LEFT, TITLE_Y};
    const point_t caption = {CAPTION_X, CAPTION_Y};
    const point_t counter = {CAPTION_X, COUNTER_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "งบประมาณของหน้าจอ", title, COLOR_TEXT,
                  FONT_TITLE_PX, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "งบ widget ต่อหนึ่งหน้า", caption, COLOR_DIM,
                           FONT_TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ใช้ไป 4 / งบ 32 (เพดาน 64)", counter,
                           COLOR_OK, FONT_TEXT_PX, &budget->counter);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = BAR_X;
        args.y = BAR_Y;
        args.w = BAR_W;
        args.h = BAR_H;
        args.min_val = 0;
        args.max_val = COURSE_BUDGET;
        args.font_px = 0;
        status = bento_ui_bar_create(ui, &args, &budget->bar);
    }
    if (status == BENTO_OK) {
        /* made at 0 and set after: a Bar made with a non-zero value
         * blanks the whole page */
        (void)bento_ui_set_value(ui, budget->bar, HEADER_WIDGETS);
        budget->used = HEADER_WIDGETS;
    }
    return status;
}

/* Past the right or bottom edge of the area. */
static bool off_area(int32_t right, int32_t bottom)
{
    return (right > AREA_W) || (bottom > AREA_H);
}

/* Under the Console button in the bottom-right corner. */
static bool in_reserved(int32_t x, int32_t y)
{
    return (x > RESERVED_X) && (y > RESERVED_Y);
}

static bento_status_t add_cell(budget_t *budget, int32_t row,
                               int32_t col)
{
    char text[CELL_TEXT_BYTES];
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "%" PRId32 ",%" PRId32, row,
                   col);
    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = (int16_t)(LEFT + (col * (CELL_W + GAP_X)));
    args.y = (int16_t)(TOP + (row * (CELL_H + GAP_Y)));
    args.w = CELL_W;
    args.h = CELL_H;
    args.color = COLOR_CELL;
    args.font_px = FONT_CELL_PX;
    status = bento_ui_button_create(budget->ui, &args, &unused);
    if (status == BENTO_OK) {
        spend(budget, budget->used + 1);
    }
    return status;
}

/* One row. Each cell is checked before it is made, never made first
 * and looked at after: inside the area, clear of the Console button,
 * and within the budget (the row stops there). */
static bento_status_t add_row(budget_t *budget, int32_t row)
{
    const int32_t y = TOP + (row * (CELL_H + GAP_Y));
    bento_status_t status = BENTO_OK;
    bool full = false;
    int32_t col;

    for (col = 0; (col < COLS) && !full && (status == BENTO_OK);
         col++) {
        const int32_t x = LEFT + (col * (CELL_W + GAP_X));

        if (off_area(x + CELL_W, y + CELL_H) ||
            in_reserved(x + CELL_W, y + CELL_H)) {
            budget->skipped++;
        } else if (budget->used >= ((int32_t)COURSE_BUDGET -
                                    (int32_t)RESERVE_FOR_TEXT)) {
            full = true;
        } else {
            status = add_cell(budget, row, col);
        }
    }
    return status;
}

/* A Label made with no text shows "Label" until its first write, so
 * every label gets a start text. */
static bento_status_t add_summary(budget_t *budget)
{
    const point_t summary = {LEFT, SUMMARY_Y};
    const point_t probe = {LEFT, PROBE_LINE_Y};
    const point_t ceiling = {LEFT, CEILING_Y};
    bento_status_t status =
        add_label(budget->ui, "ยังไม่ได้เทียบกับ ui.list()", summary,
                  COLOR_TEXT, FONT_TEXT_PX, &budget->summary);

    if (status == BENTO_OK) {
        status = add_label(budget->ui, "ยังไม่ได้ตรวจเขตห้ามวาง", probe,
                           COLOR_WARN, FONT_TEXT_PX, &budget->probe);
    }
    if (status == BENTO_OK) {
        status = add_label(budget->ui, "ยังไม่ได้ลองสร้างตัวที่ 33", ceiling,
                           COLOR_DIM, FONT_TEXT_PX, &budget->ceiling);
    }
    if (status == BENTO_OK) {
        spend(budget, budget->used + RESERVE_FOR_TEXT);
    }
    return status;
}

static bento_status_t build_screen(budget_t *budget)
{
    bento_status_t status =
        bento_ui_screen(budget->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    int32_t row;

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_header(budget);
    }
    for (row = 0; (row < ROWS) && (status == BENTO_OK); row++) {
        status = add_row(budget, row);
    }
    if (status == BENTO_OK) {
        status = add_summary(budget);
    }
    return status;
}

/* One real try at a 33rd widget: 32 is the course's line, which the
 * board does not know, so it is made. The board's line is 64, which
 * this file never reaches. */
static void try_widget_33(const budget_t *budget, char *over,
                          size_t over_bytes)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t extra;

    if (budget->used < COURSE_BUDGET) {
        (void)snprintf(over, over_bytes, "ยังเหลืองบ %" PRId32 " ตัว",
                       COURSE_BUDGET - budget->used);
    } else {
        bento_ui_widget_args_init(&args);
        args.text = "33";
        args.text_len = 2U;
        args.x = EXTRA_X;
        args.y = EXTRA_Y;
        args.w = EXTRA_SIDE;
        args.h = EXTRA_SIDE;
        args.color = COLOR_EXTRA;
        if (bento_ui_button_create(budget->ui, &args, &extra) ==
            BENTO_OK) {
            /* .delete() ignores a refusal in modui.c */
            (void)bento_ui_delete(budget->ui, extra);
            (void)snprintf(over, over_bytes, "%s",
                           "ตัวที่ 33 สร้างได้ - งบ 32 ไม่ใช่เพดาน 64");
        } else {
            (void)snprintf(over, over_bytes, "%s",
                           "ตัวที่ 33 ถูกปฏิเสธ - เพดานต่ำกว่า 64");
        }
    }
}

/* ui.list() reports the widgets really on the screen, to check the
 * program's own count. Its field is 'id', ui.poll()'s is 'handle':
 * two words, one value. */
static void show_report(const budget_t *budget, uint32_t alive,
                        const char *over)
{
    char text[TEXT_BYTES];

    (void)snprintf(
        text, sizeof(text),
        "นับเอง %" PRId32 " - ui.list() %" PRIu32 " - %s", budget->used,
        alive, (alive == (uint32_t)budget->used) ? "ตรงกัน" : "ไม่ตรง");
    show_text(budget->ui, budget->summary, text);
    if (in_reserved(PROBE_X, PROBE_Y)) {
        show_text(budget->ui, budget->probe,
                  "จุด x=700 y=350 อยู่ใต้ปุ่ม Console จึงข้าม");
    } else {
        show_text(budget->ui, budget->probe, "จุด x=700 y=350 วางได้");
    }
    show_text(budget->ui, budget->ceiling, over);
    (void)snprintf(text, sizeof(text),
                   "งบคอร์ส 32 (เพดาน 64) - พื้นที่ 792x398 - ข้ามไป "
                   "%" PRId32 " ช่อง",
                   budget->skipped);
    /* lcd.print() ignores a console that is not ready */
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "นับเอง %" PRId32 " - ui.list() รายงาน %" PRIu32,
                   budget->used, alive);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print(over);
}

/* Nothing on this screen reacts; the loop only keeps ui.poll() going
 * so the screen stays live. */
static bento_status_t run_loop(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        uint32_t count = 0U;

        (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX,
                            &count, NULL);
        status = pause_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    budget_t budget;
    char over[TEXT_BYTES];
    char text[TEXT_BYTES];
    uint32_t alive = 0U;
    bento_status_t status;

    (void)memset(&budget, 0, sizeof(budget));
    (void)memset(g_alive, 0, sizeof(g_alive));
    budget.ui = bento_runtime_get_ui();
    status = build_screen(&budget);
    if (status == BENTO_OK) {
        try_widget_33(&budget, over, sizeof(over));
        status = bento_ui_list(budget.ui, g_alive, MAX_WIDGETS, &alive);
    }
    if (status == BENTO_OK) {
        show_report(&budget, alive, over);
        status = run_loop(budget.ui);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "จบแล้ว - นับเอง %" PRId32 " - บนจอจริง %" PRIu32,
                       budget.used, alive);
        show_text(budget.ui, budget.summary, text);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
