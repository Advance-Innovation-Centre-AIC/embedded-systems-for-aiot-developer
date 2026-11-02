/*
 * calendar.c -- ui.Calendar ตัวเดียว (Calendar)
 *
 * ปฏิทินที่แตะเลือกวันได้ สร้างด้วย min= ปี max= เดือน value= วัน (ไม่ใช่ช่วงค่าแบบ
 * Slider) ทุกแปดวินาทีโปรแกรมพลิกเดือนเอง (สิงหาคม กันยายน ตุลาคม 2026)
 * แตะวันแล้วได้ event value_changed ค่าเป็นเลขแปดหลัก YYYYMMDD ก้อนเดียว
 * ป้ายด้านขวาแสดงวันที่เป็น YYYY-MM-DD และพิมพ์ลงคอนโซล ลูกศรเปลี่ยนเดือนที่หัวไม่ส่ง
 * event ชื่อวันเป็นภาษาอังกฤษของ LVGL เสมอ รันจบเองใน 24 วินาที
 *
 * A calendar you can tap a day on, created with min= year, max= month,
 * value= day (not a range as on a Slider). Every eight seconds the
 * program turns the month itself (August, September, October 2026).
 * Tapping a day gives a value_changed event whose value is one
 * eight-digit number YYYYMMDD; the label on the right shows it as
 * YYYY-MM-DD and the console prints it. The month arrows send no event,
 * and the day names are always LVGL's English ones. It ends by itself
 * after 24 seconds.
 *
 * Twin of calendar.py
 * (embedded-systems-for-aiot-developer/examples/widgets/calendar.py),
 * merged into one file from the library twin aiot-widget-calendar.
 * Style: CLEAN_C.md. Stop is not caught
 * by the original: a Stop in any sleep ends the program here too.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Python's floor division and modulo for a positive divisor: an event's
 * value is any int32_t, and a negative one must split as MicroPython
 * splits it. */
static int32_t floor_div(int32_t value, int32_t divisor)
{
    int32_t quotient = value / divisor;

    if (((value % divisor) != 0) && (value < 0)) {
        quotient--;
    }
    return quotient;
}

static int32_t floor_mod(int32_t value, int32_t divisor)
{
    int32_t remainder = value % divisor;

    if (remainder < 0) {
        remainder += divisor;
    }
    return remainder;
}

/* True when the event is of this kind and came from this widget. */
static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* Screen layout on the 792 x 398 canvas: the calendar on the left,
 * the notes on the right. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    CALENDAR_Y = 72,
    CALENDAR_W = 320,
    CALENDAR_H = 264,
    NOTE_X = 392,
    NOTE_Y = 112,
    HINT_DIGITS_Y = 160,
    HINT_ARROWS_Y = 200,
    HINT_NAMES_Y = 344
};

/* Font sizes, in pixels. */
enum {
    FONT_TITLE = 28,
    FONT_NOTE = 24,
    FONT_HINT = 20
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_OK = 0x30A46C
};

enum {
    SHOWN_YEAR = 2026,
    AUGUST = 8,
    SEPTEMBER = 9,
    OCTOBER = 10,
    START_DAY = 15,
    PASSES = 60,
    PASSES_PER_MONTH = 20, /* 20 x 400 ms: the month turns every 8 s */
    MONTH_COUNT = 3,
    LOOP_MS = 400,
    SETTLE_MS = 200, /* let the cleared screen settle */
    /* YYYYMMDD: the year is the value floor-divided by 10000 */
    YEAR_UNIT = 10000,
    MONTH_UNIT = 100,
    /* "YYYY-MM-DD" for any int32_t, with room to spare */
    DATE_CHARS = 48
};

/* A month to turn to. */
typedef struct {
    int32_t year;
    int32_t month;
} month_t;

/* A point on the canvas. */
typedef struct {
    int16_t x;
    int16_t y;
} point_t;

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

/* min= the year, max= the month, value= the day. */
static bento_status_t add_calendar(bento_ui_t *ui,
                                   bento_ui_id_t *calendar)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CALENDAR_Y;
    args.w = CALENDAR_W;
    args.h = CALENDAR_H;
    args.color = COLOR_TEXT;
    args.min_val = SHOWN_YEAR;
    args.max_val = AUGUST;
    args.font_px = START_DAY;
    return bento_ui_calendar_create(ui, &args, calendar);
}

/* The labels around the calendar: the screen's Thai texts live here,
 * since the calendar's own day names do not go through the Thai font.
  */
static bento_status_t add_notes(bento_ui_t *ui, bento_ui_id_t *note)
{
    const point_t note_at = {NOTE_X, NOTE_Y};
    const point_t digits_at = {NOTE_X, HINT_DIGITS_Y};
    const point_t arrows_at = {NOTE_X, HINT_ARROWS_Y};
    const point_t names_at = {MARGIN_X, HINT_NAMES_Y};
    bento_ui_id_t unused;
    bento_status_t status = add_label(ui, "ยังไม่มีใครแตะวัน", note_at,
                                      COLOR_DIM, FONT_NOTE, note);

    if (status == BENTO_OK) {
        status = add_label(ui, "แตะวันแล้วได้เลขแปดหลัก", digits_at,
                           COLOR_DIM, FONT_HINT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "กดลูกศรที่หัวไม่ได้ event", arrows_at,
                           COLOR_DIM, FONT_HINT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ชื่อวันเป็นอังกฤษเสมอ", names_at, COLOR_DIM,
                           FONT_HINT, &unused);
    }
    return status;
}

/* Draws the screen in the original's order (the widgets' handles
 * follow it). */
static bento_status_t build_screen(bento_ui_t *ui,
                                   bento_ui_id_t *calendar,
                                   bento_ui_id_t *note)
{
    const point_t title_at = {MARGIN_X, TITLE_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ui.Calendar", title_at, COLOR_TEXT,
                           FONT_TITLE, &unused);
    }
    if (status == BENTO_OK) {
        status = add_calendar(ui, calendar);
    }
    if (status == BENTO_OK) {
        status = add_notes(ui, note);
    }
    return status;
}

/* A day was picked: the label shows YYYY-MM-DD in green and the console
 * the raw number. MicroPython's widget setters ignore a refusal
 * (modui.c), so the twin does too. */
static void show_day(bento_ui_t *ui, bento_ui_id_t note, int32_t ymd)
{
    char date[DATE_CHARS];

    (void)snprintf(date, sizeof(date),
                   "%04" PRId32 "-%02" PRId32 "-%02" PRId32,
                   floor_div(ymd, YEAR_UNIT),
                   floor_mod(floor_div(ymd, MONTH_UNIT), MONTH_UNIT),
                   floor_mod(ymd, MONTH_UNIT));
    (void)bento_ui_set_text(ui, note, date, (uint32_t)strlen(date));
    (void)bento_ui_set_color(ui, note, COLOR_OK);
    (void)printf("แตะวันที่ %" PRId32 "\n", ymd);
}

static void handle_taps(bento_ui_t *ui, bento_ui_id_t calendar,
                        bento_ui_id_t note)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    /* ui.poll() answers no events when it fails, so a failed poll is
     * not an error here. */
    if (bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (event_is(&events[i], BENTO_UI_EVENT_VALUE_CHANGED,
                     calendar)) {
            show_day(ui, note, events[i].value);
        }
    }
}

/* Turns the month every PASSES_PER_MONTH passes and reads the taps
 * every LOOP_MS. .month() returns nothing in MicroPython, so its status
 * is not a failure here. */
static bento_status_t run_passes(bento_ui_t *ui, bento_ui_id_t calendar,
                                 bento_ui_id_t note)
{
    static const month_t months[MONTH_COUNT] = {{SHOWN_YEAR, AUGUST},
                                                {SHOWN_YEAR, SEPTEMBER},
                                                {SHOWN_YEAR, OCTOBER}};
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < PASSES) && (status == BENTO_OK); i++) {
        if ((i % PASSES_PER_MONTH) == 0) {
            const month_t *shown =
                &months[(i / PASSES_PER_MONTH) % MONTH_COUNT];

            (void)bento_ui_set_month(ui, calendar, shown->year,
                                     shown->month);
        }
        handle_taps(ui, calendar, note);
        status = bento_time_sleep_ms(LOOP_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_ui_id_t calendar = 0U;
    bento_ui_id_t note = 0U;
    bento_status_t status = build_screen(ui, &calendar, &note);

    if (status == BENTO_OK) {
        status = run_passes(ui, calendar, note);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.Calendar: min=ปี max=เดือน value=วัน, event "
                     "คือ "
                     "YYYYMMDD ก้อนเดียว\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
