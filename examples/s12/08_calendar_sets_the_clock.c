/*
 * 08_calendar_sets_the_clock.c -- บอร์ดไม่รู้ว่าวันนี้วันที่เท่าไร แล้วใครบอกมัน
 *
 * ทุกครั้งที่ตัดไฟ นาฬิกาของบอร์ดกลับไปที่ 1 ม.ค. 2015 ซ้ายของจอคือปฏิทินที่เปิดมา
 * ที่เดือนที่บอร์ดเชื่อ ขวาคือการ์ดเทียบวันที่ของบอร์ดกับวันที่ที่คนแตะยืนยัน
 * แตะวันในปฏิทินแล้วโปรแกรมตั้ง RTC เป็นวันนั้นเวลา 00:00:00 ทำงาน 40 วินาทีแล้วจบ
 *
 * The board's clock goes back to 1 Jan 2015 at every power cut. The left of
 * the screen is a calendar opened at the month the board believes; the right
 * is a card that compares the board's date with the date a person tapped.
 * Tapping a day sets the RTC to that day at 00:00:00. It runs 40 s.
 *
 * Twin of 08_calendar_sets_the_clock.py
 * (embedded-systems-for-aiot-developer/examples/s12/08_calendar_sets_the_clock.py),
 * from the library twin aiot-s12-08-calendar-sets-the-clock. Style: CLEAN_C.md.
 * What C changes: machine.RTC().datetime() is bento_machine_rtc_get/set; the
 * ValueError a year outside 2000..2099 raises is BENTO_E_INVALID_ARGUMENT
 * returned at the same point (the year is checked before it is narrowed into
 * the struct); BENTO_E_IO from rtc_set is ignored, as the binding ignores the
 * PDL status; lcd.print(a, b) (one space between) is snprintf into a buffer.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_machine_rtc.h"
#include "bento_runtime.h"
#include "bento_time.h"

enum { /* layout */
    MARGIN_X = 24,
    TITLE_Y = 16,
    CALENDAR_Y = 64,
    CALENDAR_W = 304,
    CALENDAR_H = 272,
    CARD_X = 360,
    CARD_W = 408,
    CARD_RADIUS = 8,
    CARD_TEXT_X = 384,
    BOARD_HEAD_Y = 88,
    BOARD_DATE_Y = 120,
    HUMAN_HEAD_Y = 184,
    HUMAN_DATE_Y = 216,
    HINT_Y = 280,
    LINE_Y = 344
};
enum { /* fonts, times, dates, sizes */
    FONT_TITLE = 28,
    FONT_SMALL = 20,
    FONT_DATE = 24,
    SETTLE_MS = 200,
    LOOP_MS = 100,
    RUN_MS = 40000,
    FIRST_DATE =
        19000101, /* the calendar answers YYYYMMDD in one number */
    YEAR_SHIFT = 10000,
    MONTH_SHIFT = 100,
    RTC_YEAR_MIN = 2000,
    RTC_YEAR_MAX = 2099,
    TEXT_BYTES = 128 /* the display carries at most 127 bytes */
};
enum { /* the course palette */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_OK = 0x30A46C
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t calendar;
    bento_ui_id_t human;
    bento_ui_id_t hint;
    bento_ui_id_t line;
    char boot_text[TEXT_BYTES];
} calendar_screen_t;

static bento_status_t add_label(const char *text, int32_t x, int32_t y,
                                uint32_t color, int32_t font_px,
                                bento_ui_id_t *id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = (int16_t)x;
    args.y = (int16_t)y;
    args.color = color;
    args.font_px = font_px;
    /* the runtime's ui core, so the helper keeps to six parameters */
    return bento_ui_label_create(bento_runtime_get_ui(), &args, id);
}

static void set_text(bento_ui_t *ui, bento_ui_id_t id, const char *text)
{
    /* a setter's refusal is ignored in MicroPython too (modui.c) */
    (void)bento_ui_set_text(ui, id, text, (uint32_t)strlen(text));
}

static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    /* an event may carry a compat id: compare the slot, as ev["handle"] does */
    return (event->type == kind) && (bento_ui_id_to_handle(event->id) ==
                                     bento_ui_id_to_handle(widget));
}

/* The calendar opens at the month the board believes, not the real one:
 * the point of the file is what the board believes. min= is the year,
 * max= the month and value= the day. */
static bento_status_t add_calendar(calendar_screen_t *screen,
                                   const bento_machine_datetime_t *boot)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = (int16_t)MARGIN_X;
    args.y = (int16_t)CALENDAR_Y;
    args.w = (int16_t)CALENDAR_W;
    args.h = (int16_t)CALENDAR_H;
    args.color = (uint32_t)COLOR_TEXT;
    args.min_val = (int32_t)boot->year;
    args.max_val = (int32_t)boot->month;
    args.font_px = (int32_t)boot->day;
    return bento_ui_calendar_create(screen->ui, &args,
                                    &screen->calendar);
}

static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t id;

    bento_ui_widget_args_init(&args);
    args.x = (int16_t)CARD_X;
    args.y = (int16_t)CALENDAR_Y;
    args.w = (int16_t)CARD_W;
    args.h = (int16_t)CALENDAR_H;
    args.color = (uint32_t)COLOR_CARD;
    args.max_val = CARD_RADIUS;
    return bento_ui_panel_create(ui, &args, &id);
}

static bento_status_t add_card_texts(calendar_screen_t *screen)
{
    bento_ui_id_t id;
    bento_status_t status =
        add_label("นาฬิกาของบอร์ดหลังเปิดเครื่อง", CARD_TEXT_X, BOARD_HEAD_Y,
                  (uint32_t)COLOR_DIM, FONT_SMALL, &id);

    if (status == BENTO_OK) {
        status = add_label(screen->boot_text, CARD_TEXT_X, BOARD_DATE_Y,
                           (uint32_t)COLOR_WARN, FONT_DATE, &id);
    }
    if (status == BENTO_OK) {
        status = add_label("วันที่ที่คนยืนยันบนจอ", CARD_TEXT_X, HUMAN_HEAD_Y,
                           (uint32_t)COLOR_DIM, FONT_SMALL, &id);
    }
    if (status == BENTO_OK) {
        status =
            add_label("ยังไม่มีใครยืนยัน", CARD_TEXT_X, HUMAN_DATE_Y,
                      (uint32_t)COLOR_DIM, FONT_DATE, &screen->human);
    }
    if (status == BENTO_OK) {
        status =
            add_label("แตะวันในปฏิทินเพื่อตั้ง", CARD_TEXT_X, HINT_Y,
                      (uint32_t)COLOR_DIM, FONT_SMALL, &screen->hint);
    }
    return status;
}

static bento_status_t build_screen(calendar_screen_t *screen,
                                   const bento_machine_datetime_t *boot)
{
    bento_ui_id_t id;
    bento_status_t status =
        add_label("วันนี้วันที่เท่าไร ใครบอก", MARGIN_X, TITLE_Y,
                  (uint32_t)COLOR_TEXT, FONT_TITLE, &id);

    if (status == BENTO_OK) {
        status = add_calendar(screen, boot);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_card_texts(screen);
    }
    if (status == BENTO_OK) {
        status =
            add_label("t ใน payload คือ ticks ไม่ใช่วันที่", MARGIN_X, LINE_Y,
                      (uint32_t)COLOR_DIM, FONT_SMALL, &screen->line);
    }
    return status;
}

/* lcd.print() ignores a console that is not ready (modlcd.c) */
static void print_intro(const char *boot_text)
{
    char text[TEXT_BYTES];

    (void)bento_lcd_clear();
    (void)bento_lcd_console(
        "<h2>Calendar - ใครบอกบอร์ดว่าวันนี้วันที่เท่าไร</h2>");
    (void)snprintf(text, sizeof(text), "นาฬิกาบอร์ดหลังเปิดเครื่อง: %s",
                   boot_text);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print("ปี 2015 คือลายเซ็นของนาฬิกาที่ไม่เคยถูกตั้ง");
}

/* Set the real clock to the day the person tapped. Hours, minutes and
 * seconds are zero: the calendar gives only a day, and a value we do not
 * know must not be made up. */
static bento_status_t set_clock(int32_t year, int32_t month,
                                int32_t day)
{
    bento_machine_datetime_t date;
    bento_status_t status = BENTO_E_INVALID_ARGUMENT;

    if ((year >= RTC_YEAR_MIN) && (year <= RTC_YEAR_MAX)) {
        bento_machine_datetime_init(&date);
        date.year = (uint16_t)year;
        date.month = (uint8_t)month;
        date.day = (uint8_t)day;
        status = bento_machine_rtc_set(&date);
        if (status == BENTO_E_IO) {
            status =
                BENTO_OK; /* the binding ignores the PDL's refusal */
        }
    }
    return status;
}

/* One eight-digit number, split by division and modulo; no tuple here.
 * ymd is positive, so C's / and % give floor division and modulo. */
static bento_status_t take_date(const calendar_screen_t *screen,
                                int32_t ymd)
{
    const int32_t year = ymd / YEAR_SHIFT;
    const int32_t month = (ymd / MONTH_SHIFT) % MONTH_SHIFT;
    const int32_t day = ymd % MONTH_SHIFT;
    char picked[TEXT_BYTES];
    char text[TEXT_BYTES];
    bento_status_t status;

    (void)snprintf(picked, sizeof(picked), "%04d-%02d-%02d", (int)year,
                   (int)month, (int)day);
    status = set_clock(year, month, day);
    if (status == BENTO_OK) {
        set_text(screen->ui, screen->human, picked);
        (void)bento_ui_set_color(screen->ui, screen->human,
                                 (uint32_t)COLOR_OK);
        set_text(screen->ui, screen->hint, "ตั้งนาฬิกาแล้ว");
        set_text(screen->ui, screen->line, "ตั้งแล้วอยู่จนกว่าจะตัดไฟ");
        (void)bento_ui_set_color(screen->ui, screen->line,
                                 (uint32_t)COLOR_ACCENT);
        (void)snprintf(text, sizeof(text), "คนยืนยันวันที่ %s - ตั้ง RTC แล้ว",
                       picked);
        (void)bento_lcd_print(text);
    }
    return status;
}

static bento_status_t handle_events(const calendar_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U; /* ui.poll() answers [] on every failure */
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        if (event_is(&events[i], BENTO_UI_EVENT_VALUE_CHANGED,
                     screen->calendar) &&
            (events[i].value >= FIRST_DATE)) {
            status = take_date(screen, events[i].value);
        }
    }
    return status;
}

static bento_status_t run_event_loop(const calendar_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = handle_events(screen);
        if (status == BENTO_OK) {
            status = bento_time_sleep_ms((uint32_t)LOOP_MS);
        }
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    calendar_screen_t screen;
    bento_machine_datetime_t boot;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    /* eight fields: year month day weekday hour minute second subsecond */
    bento_machine_datetime_init(&boot);
    status = bento_machine_rtc_get(&boot);
    if (status == BENTO_OK) {
        (void)snprintf(screen.boot_text, sizeof(screen.boot_text),
                       "%04d-%02d-%02d", (int)boot.year,
                       (int)boot.month, (int)boot.day);
        /* MicroPython ignores a refused ui.screen(): the runtime already set it up */
        (void)bento_ui_screen(screen.ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                              BENTO_RUNTIME_SCREEN_HEIGHT_PX);
        status = bento_time_sleep_ms((uint32_t)SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = build_screen(&screen, &boot);
    }
    if (status == BENTO_OK) {
        print_intro(screen.boot_text);
        status = run_event_loop(&screen);
    }
    if (status == BENTO_OK) {
        set_text(screen.ui, screen.hint, "หมดเวลา");
        (void)bento_lcd_print(
            "<span class=warn>ตัดไฟแล้วกลับไป 2015 เหมือนเดิม</span>");
        (void)printf("Calendar: min=ปี max=เดือน value=วัน และ event คือ "
                     "YYYYMMDD ก้อนเดียว\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
