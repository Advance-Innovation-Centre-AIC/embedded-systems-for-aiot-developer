/*
 * 04_seg7_takes_text.c -- Seg7 รับได้ทั้งสองทาง แต่ให้ผลไม่เท่ากัน
 *
 * value(n) ส่งได้แต่จำนวนเต็ม จอจึงแสดงได้แค่จำนวนเต็ม ถ้าอยากได้ทศนิยม
 * ต้องเลือกเองว่ากี่ตำแหน่ง แล้วส่งเป็นข้อความด้วย text(s) ตัวอย่างพิสูจน์
 * ด้วย seg.value(88) ที่ขึ้น 88 ไม่ใช่ 88.0 จากนั้น Seg7 ไต่ค่าทีละ 0.5
 * จาก 0.0 ถึง 100.0 และเปลี่ยนเป็นสีแดงเมื่อถึง 60 Bar ขยับตามค่าเดียวกัน
 * กราฟวาดเส้นไต่ขึ้น แล้วจบด้วยคำว่า DONE บน Seg7 เพราะมันรับข้อความ
 *
 * value(n) carries whole numbers only, so the display can show only
 * whole numbers; for a decimal you choose the places yourself and send
 * text with text(s). The example proves it with seg.value(88), which
 * shows 88, not 88.0. Then the Seg7 climbs in steps of 0.5 from 0.0 to
 * 100.0 and turns red from 60; the Bar follows the same value, the
 * chart draws the climb, and the Seg7 ends showing the word DONE,
 * because it takes text.
 *
 * Twin of 04_seg7_takes_text.py
 * (embedded-systems-for-aiot-developer/examples/s04/04_seg7_takes_text.py),
 * from the library twin aiot-s04-04-seg7-takes-text. Style: CLEAN_C.md.
 * The climb counts in whole tenths, as the original does, so no
 * floating point is needed.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* The climb, counted in tenths so that no rounding error adds up. */
enum {
    VALUE_MAX = 100,
    WARN_AT = 60, /* the colour changes here, in the climb's units */
    PROOF_VALUE = 88,
    TENTHS = 10,
    CLIMB_END_TENTHS = 1000,
    CLIMB_STEP_TENTHS = 5,
    /* a chart keeps 50 points: one every 2.0 gives 51, one window */
    CHART_EVERY_TENTHS = 20
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    PROOF_MS = 800,  /* time to read the proof */
    /* without a pause per step, frames are lost silently */
    STEP_MS = 40
};

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t seg;
    bento_ui_id_t bar;
    bento_ui_id_t proof;
    bento_ui_id_t chart;
    uint8_t series;
    bento_ui_id_t status;
} seg_screen_t;

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 136,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CAPTION_Y = 70,
    SEG_X = 40,
    SEG_Y = 104,
    SEG_W = 240,
    SEG_H = 72,
    BAR_X = 336,
    BAR_Y = 120, /* its middle lines up with the Seg7's */
    BAR_W = 400,
    BAR_H = 40,
    PROOF_Y = 206,
    CHART_Y = 240,
    CHART_H = 96,
    STATUS_Y = 352,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. Red means "over the line" and
 * nothing else on this screen. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_BAD = 0xE5484D
};

/* ui.poll() here only keeps the screen drawn, even with nothing to tap;
 * its events are not read, and a failed poll answers no events, so its
 * status is not an error. */
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
    args.min_val = COLOR_CARD; /* the border colour: the card's own */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* The Seg7 starts as the text "0.0", in the intended format, not its
 * own "0000". */
static bento_status_t add_seg(seg_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0.0";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = SEG_X;
    args.y = SEG_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = COLOR_TEXT;
    return bento_ui_seg7_create(screen->ui, &args, &screen->seg);
}

static bento_status_t add_bar(seg_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = BAR_X;
    args.y = BAR_Y;
    args.w = BAR_W;
    args.h = BAR_H;
    args.color = COLOR_ACCENT;
    args.min_val = 0;
    args.max_val = VALUE_MAX;
    args.font_px = 0; /* the starting value */
    return bento_ui_bar_create(screen->ui, &args, &screen->bar);
}

/* The chart's own colour belongs to series 0, which is not used, so the
 * line is a new series in the accent colour. */
static bento_status_t add_chart(seg_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CHART_Y;
    args.w = CARD_W;
    args.h = CHART_H;
    args.color = COLOR_CARD;
    args.min_val = 0;
    args.max_val = VALUE_MAX;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            screen->ui, screen->chart, COLOR_ACCENT, &screen->series);
    }
    return status;
}

/* The title and the card: Seg7 on the left, the Bar on the right,
 * each with its caption. */
static bento_status_t build_card(seg_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t title = {MARGIN_X, TITLE_Y};
    const point_t seg_caption = {SEG_X, CAPTION_Y};
    const point_t bar_caption = {BAR_X, CAPTION_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "Seg7 - text() คุมรูปแบบ value() ได้จำนวนเต็ม", title,
                  COLOR_TEXT, FONT_TITLE_PX, &unused);

    if (status == BENTO_OK) {
        status = add_card(ui);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "Seg7 - ใช้ text()", seg_caption,
                           COLOR_DIM, FONT_TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "Bar - ใช้ value()", bar_caption,
                           COLOR_DIM, FONT_TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_bar(screen);
    }
    return status;
}

/* The proof line starts with a text (an empty label would show
 * "Label"), then the chart and the status line. */
static bento_status_t build_lower(seg_screen_t *screen)
{
    const point_t proof = {MARGIN_X, PROOF_Y};
    const point_t status_line = {MARGIN_X, STATUS_Y};
    bento_status_t status =
        add_label(screen->ui, "ยังไม่ได้พิสูจน์อะไร", proof, COLOR_TEXT,
                  FONT_TEXT_PX, &screen->proof);

    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังไต่ค่า 0 ถึง 100", status_line,
                           COLOR_DIM, FONT_TEXT_PX, &screen->status);
    }
    return status;
}

static bento_status_t build_screen(seg_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = build_card(screen);
    }
    if (status == BENTO_OK) {
        status = build_lower(screen);
    }
    return status;
}

/* One step of the climb: the text with exactly one decimal, the Bar
 * with the whole part, a chart point every 2.0, and the colour, which
 * the eye reads before the digits. */
static void show_step(const seg_screen_t *screen, int32_t tenths)
{
    const int32_t whole = tenths / TENTHS;
    const int32_t tenth = tenths % TENTHS;
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32 ".%" PRId32, whole,
                   tenth);
    show_text(screen->ui, screen->seg, text);
    (void)bento_ui_set_value(screen->ui, screen->bar, whole);
    if ((tenths % CHART_EVERY_TENTHS) == 0) {
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->series,
                                      whole); /* fire and forget */
    }
    (void)bento_ui_set_color(screen->ui, screen->seg,
                             (whole >= WARN_AT) ? COLOR_BAD
                                                : COLOR_TEXT);
}

/* The seven-segment font has A-Z and a-z, so the Seg7 can show a
 * word. */
static void show_end(const seg_screen_t *screen)
{
    show_text(screen->ui, screen->seg, "DONE");
    (void)bento_ui_set_color(screen->ui, screen->seg, COLOR_TEXT);
    show_text(screen->ui, screen->status,
              "จบแล้ว - Seg7 ขึ้น DONE ได้ เพราะมันรับข้อความ");
    show_text(screen->ui, screen->proof,
              "จำไว้: อยากคุมรูปแบบ ต้องใช้ text()");
    poll_screen(screen->ui);
    (void)bento_lcd_print(
        "สรุป: value() ได้จำนวนเต็ม - text() ได้ทุกอย่างที่ฟอนต์มี");
}

/* The first proof: value() straight into the Seg7 shows 88, not 88.0,
 * because a whole number does not say how many decimals it wants. */
static bento_status_t prove_value(const seg_screen_t *screen)
{
    bento_status_t status;

    (void)bento_ui_set_value(screen->ui, screen->seg, PROOF_VALUE);
    poll_screen(screen->ui);
    status = pause_ms(PROOF_MS);
    if (status == BENTO_OK) {
        show_text(screen->ui, screen->proof,
                  "seg.value(88) ขึ้น 88 - ไม่ใช่ 88.0");
        (void)bento_lcd_print(
            "value() ส่งได้แต่จำนวนเต็ม - ทศนิยมต้องใช้ text()");
    }
    return status;
}

static bento_status_t climb(const seg_screen_t *screen)
{
    int32_t tenths = 0;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) && (tenths <= CLIMB_END_TENTHS)) {
        show_step(screen, tenths);
        tenths += CLIMB_STEP_TENTHS;
        poll_screen(screen->ui);
        status = pause_ms(STEP_MS);
    }
    return status;
}

int main(void)
{
    seg_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        /* lcd.print() ignores a console that is not ready (modlcd.c) */
        (void)bento_lcd_print(
            "Seg7 ใช้ text() - Bar ใช้ value() - Chart ใช้ set_next()");
        status = prove_value(&screen);
    }
    if (status == BENTO_OK) {
        status = climb(&screen);
    }
    if (status == BENTO_OK) {
        show_end(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
