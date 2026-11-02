/*
 * 09_your_level_rule.c -- เขียนกฎตัดระดับเอง
 * แล้วให้บอร์ดตรวจให้ (Your Level Rule)
 *
 * แบบฝึกคู่กับ 08 ฟังก์ชัน level_name() ยังตอบ "ปกติ" ทุกค่า
 * งานของคุณคือแก้มันตามโจทย์ ของทีมซ่อมบำรุง: ต่ำกว่า 50 "ปกติ", 50 ถึง
 * 79 "เริ่มสูง", 80 ขึ้นไป "ต้องรีบดู" โปรแกรมลองกฎกับหกค่า
 * แถวเขียวคือตอบถูก แถวแดงคือยังผิด รันครั้งแรกได้เขียวสองแดงสี่
 * ลำดับของ if สำคัญ: เช็กเงื่อนไขที่เข้มที่สุด (80) ก่อน
 * แล้วค่อยไล่ลงมา
 *
 * A practice pair for 08: level_name() still answers "normal" for every
 * value, and your job is to fix it to the maintenance team's rule
 * (below 50 normal, 50 to 79 high, 80 and up alarm). The program tries
 * the rule on six values: a green row is right, a red row is still
 * wrong. The first run gives two green rows and four red ones. Check
 * the strictest condition (80) first, then work down.
 *
 * Twin of 09_your_level_rule.py
 * (embedded-systems-for-aiot-developer/examples/s01/09_your_level_rule.py).
 * Style: CLEAN_C.md.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 20,
    TITLE_Y = 12,
    CARD_Y = 52,
    CARD_W = 652,
    CARD_H = 96,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CAPTION_Y = 58,
    PASSED_X = 40,
    FAILED_X = 232,
    COUNTER_Y = 88,
    COUNTER_W = 152,
    COUNTER_H = 52,
    BAR_X = 412,
    BAR_Y = 100,
    BAR_W = 232,
    BAR_H = 28,
    ROW_Y = 172,
    ROW_STEP_Y = 28,
    FOOTER_Y = 348,
    HINT_X = 300,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_BAD = 0xE5484D
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    /* one checked value at a time, slow enough to watch */
    CASE_MS = 300
};

enum {
    CASE_COUNT = 6
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t passed;
    bento_ui_id_t failed;
    bento_ui_id_t bar;
    bento_ui_id_t rows[CASE_COUNT];
    bento_ui_id_t result;
} check_screen_t;

/* ----- Your part: fill this in. ----- */
/* The level's name for a value, by the rule in the header. It answers
 * "ปกติ" for every value now, so only the first two rows pass. Hint:
 * test the strictest condition first and end with the return of the
 * remaining case, without an if. */
static const char *level_name(int32_t value)
{
    (void)value; /* the starting rule does not look at the value yet */
    return "ปกติ";
}

/* ----- End of your part. ----- */

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

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

static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* the border colour */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* A captioned counter: the caption above, the big digits below. */
static bento_status_t add_counter(bento_ui_t *ui, const char *caption,
                                  int16_t x, uint32_t color,
                                  bento_ui_id_t *out_id)
{
    const point_t at = {x, CAPTION_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, caption, at, COLOR_DIM, FONT_TEXT_PX, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "0";
        args.text_len = 1U;
        args.x = x;
        args.y = COUNTER_Y;
        args.w = COUNTER_W;
        args.h = COUNTER_H;
        args.color = color;
        status = bento_ui_seg7_create(ui, &args, out_id);
    }
    return status;
}

static bento_status_t add_bar(check_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = BAR_X;
    args.y = BAR_Y;
    args.w = BAR_W;
    args.h = BAR_H;
    args.min_val = 0;
    args.max_val = CASE_COUNT;
    args.font_px = 0; /* the starting value */
    return bento_ui_bar_create(screen->ui, &args, &screen->bar);
}

/* One row per case, made now and written over by the check; never an
 * empty label, which LVGL would fill with the word "Label". */
static bento_status_t add_rows(check_screen_t *screen)
{
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < CASE_COUNT) && (status == BENTO_OK); i++) {
        const point_t at = {MARGIN_X,
                            (int16_t)(ROW_Y + (i * ROW_STEP_Y))};

        status = add_label(screen->ui, "รอตรวจ", at, COLOR_DIM,
                           FONT_TEXT_PX, &screen->rows[i]);
    }
    return status;
}

static bento_status_t build_screen(check_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t title = {MARGIN_X, TITLE_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เขียนกฎตัดระดับเอง แล้วให้บอร์ดตรวจให้", title,
                           COLOR_TEXT, FONT_TITLE_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(ui);
    }
    if (status == BENTO_OK) {
        status = add_counter(ui, "ผ่านแล้ว", PASSED_X, COLOR_OK,
                             &screen->passed);
    }
    if (status == BENTO_OK) {
        status = add_counter(ui, "ยังไม่ผ่าน", FAILED_X, COLOR_BAD,
                             &screen->failed);
    }
    if (status == BENTO_OK) {
        status = add_bar(screen);
    }
    if (status == BENTO_OK) {
        status = add_rows(screen);
    }
    return status;
}

static bento_status_t build_footer(check_screen_t *screen)
{
    const point_t result = {MARGIN_X, FOOTER_Y};
    const point_t hint = {HINT_X, FOOTER_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "ยังไม่ได้ตรวจ", result, COLOR_DIM,
                  FONT_TEXT_PX, &screen->result);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "แก้แล้วรันใหม่ได้เรื่อย ๆ", hint,
                           COLOR_DIM, FONT_TEXT_PX, &unused);
    }
    return status;
}

static void show_counts(const check_screen_t *screen, int32_t checked,
                        int32_t passed)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, passed);
    show_text(screen->ui, screen->passed, text);
    (void)snprintf(text, sizeof(text), "%" PRId32, checked - passed);
    show_text(screen->ui, screen->failed, text);
    (void)bento_ui_set_value(screen->ui, screen->bar, passed);
}

static void show_result(const check_screen_t *screen, int32_t passed)
{
    char text[TEXT_BYTES];

    if (passed == CASE_COUNT) {
        (void)snprintf(text, sizeof(text),
                       "ผ่านครบทั้ง %d ข้อ - "
                       "กฎใช้ได้แล้ว",
                       CASE_COUNT);
        show_text(screen->ui, screen->result, text);
        (void)bento_ui_set_color(screen->ui, screen->result, COLOR_OK);
        (void)bento_lcd_print(
            "<span class=ok>ผ่านครบ - กฎนี้ตรงตามโจทย์แล้ว</span>");
    } else {
        (void)snprintf(text, sizeof(text),
                       "ยังตกอยู่ %" PRId32 " ข้อ - ดูแถวแดง",
                       CASE_COUNT - passed);
        show_text(screen->ui, screen->result, text);
        (void)bento_ui_set_color(screen->ui, screen->result, COLOR_BAD);
        (void)snprintf(text, sizeof(text),
                       "<span class=warn>ยังตก %" PRId32 " ข้อ</span>",
                       CASE_COUNT - passed);
        (void)bento_lcd_print(text);
    }
    poll_screen(screen->ui);
}

/* A value to try and the level the maintenance team wants for it. */
typedef struct {
    int32_t value;
    const char *want;
} check_case_t;

/* Tries the rule on one case against the team's answer, not against
 * a guess; true when right. */
static bool check_case(const check_screen_t *screen, int32_t i)
{
    /* All three levels, and the values right on each line, where >=
     * and > differ. */
    static const check_case_t cases[CASE_COUNT] = {
        {12, "ปกติ"},   {49, "ปกติ"},    {50, "เริ่มสูง"},
        {79, "เริ่มสูง"}, {80, "ต้องรีบดู"}, {92, "ต้องรีบดู"},
    };
    const check_case_t *check = &cases[i];
    const char *got = level_name(check->value);
    const bool right = (strcmp(got, check->want) == 0);
    char text[TEXT_BYTES];

    if (right) {
        (void)bento_ui_set_color(screen->ui, screen->rows[i], COLOR_OK);
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>%" PRId32 " -> %s</span>",
                       check->value, got);
    } else {
        (void)bento_ui_set_color(screen->ui, screen->rows[i],
                                 COLOR_BAD);
        (void)snprintf(text, sizeof(text),
                       "<span class=error>%" PRId32
                       " ควรเป็น %s แต่ได้ %s</span>",
                       check->value, check->want, got);
    }
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "%" PRId32 "   ควรเป็น %s   ได้ %s", check->value,
                   check->want, got);
    show_text(screen->ui, screen->rows[i], text);
    return right;
}

/* Checks every case in turn; *passed counts the right answers. */
static bento_status_t check_all(const check_screen_t *screen,
                                int32_t *passed)
{
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < CASE_COUNT) && (status == BENTO_OK); i++) {
        if (check_case(screen, i)) {
            (*passed)++;
        }
        show_counts(screen, i + 1, *passed);
        poll_screen(screen->ui);
        status = pause_ms(CASE_MS);
    }
    return status;
}

int main(void)
{
    check_screen_t screen;
    int32_t passed = 0;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        status = build_footer(&screen);
    }
    if (status == BENTO_OK) {
        poll_screen(screen.ui);
        /* lcd calls ignore a console that is not ready (modlcd.c) */
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ตรวจกฎตัดระดับ</h2>");
        status = check_all(&screen, &passed);
    }
    if (status == BENTO_OK) {
        show_result(&screen, passed);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
