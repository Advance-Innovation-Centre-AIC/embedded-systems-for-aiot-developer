/*
 * spangroup.c -- ui.SpanGroup ตัวเดียว (one SpanGroup)
 *
 * ย่อหน้าเดียวที่ประกอบจากท่อนหลายท่อน แต่ละท่อนมีขนาด สี และเส้นใต้ของตัวเอง
 * .pen(สี) ตั้งสีของท่อนที่จะเติมต่อจากนี้ .add_span(ข้อความ, ขนาด, ธง) เติมหนึ่งท่อน
 * จอแสดงย่อหน้าไทยสี่บรรทัด แล้วรันจบเองใน 20 วินาที
 *
 * One paragraph built from spans, each with its own size, colour and
 * underline: the pen sets the colour of the spans added after it. The
 * screen shows a four-line Thai paragraph; the example ends by itself
 * after 20 seconds.
 *
 * Twin of spangroup.py
 * (embedded-systems-for-aiot-developer/examples/widgets/spangroup.py),
 * merged into one file from the library twin aiot-widget-spangroup.
 * Style: CLEAN_C.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* The idle loop that keeps the screen up: 100 polls, 200 ms apart
 * (20 s). */
enum {
    SETTLE_MS = 200,
    IDLE_ROUNDS = 100,
    IDLE_MS = 200
};

/* time.sleep_ms(): BENTO_E_CANCELLED when Stop arrives, as the
 * original's KeyboardInterrupt. */
static bento_status_t pause_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    TITLE_PX = 28,
    CARD_Y = 72,
    CARD_W = 744,
    CARD_H = 248,
    CARD_RADIUS = 8,
    SPANS_X = 48,
    SPANS_Y = 96,
    SPANS_W = 696,
    SPANS_H = 200,
    NOTE_Y = 344,
    NOTE_PX = 20
};

/* One step of the paragraph: optionally a new pen colour, then one
 * span. */
typedef struct {
    bool set_pen;
    uint32_t pen;
    const char *text;
    uint8_t size_px;
    uint8_t flags;
} span_step_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                int32_t size_px)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = MARGIN_X;
    args.y = y;
    args.color = color;
    args.font_px = size_px;
    return bento_ui_label_create(ui, &args, &label);
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
    args.max_val = CARD_RADIUS;
    return bento_ui_panel_create(ui, &args, &card);
}

/* The span group and its paragraph; a pen or span refusal is
 * ignored, as modui.c does. */
static bento_status_t add_paragraph(bento_ui_t *ui)
{
    static const span_step_t span_steps[] = {
        {true, COLOR_DIM, "ขนาดต่างกัน ", 20U, 0U},
        {true, COLOR_TEXT, "ท่อนนี้ใหญ่กว่า", 28U, 0U},
        {false, 0U, "\n", 20U, 0U},
        {true, COLOR_WARN, "เฝ้าระวัง", 24U, BENTO_UI_SPAN_UNDERLINE},
        {true, COLOR_TEXT, "  ท่อนนี้มีเส้นใต้\n", 24U, 0U},
        {true, COLOR_BAD, "ยกเลิกแล้ว", 24U, BENTO_UI_SPAN_STRIKETHROUGH},
        {true, COLOR_TEXT, "  ท่อนนี้มีเส้นขีดกลาง\n", 24U, 0U},
        {true, COLOR_ACCENT, "สีมาจากปากกาที่ตั้งไว้ก่อนเติม", 24U, 0U},
    };
    bento_ui_widget_args_t args;
    bento_ui_id_t group = 0U;
    size_t i;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = SPANS_X;
    args.y = SPANS_Y;
    args.w = SPANS_W;
    args.h = SPANS_H;
    args.color = COLOR_TEXT;
    status = bento_ui_spangroup_create(ui, &args, &group);
    for (i = 0U; (i < (sizeof(span_steps) / sizeof(span_steps[0]))) &&
                 (status == BENTO_OK);
         i++) {
        const span_step_t *step = &span_steps[i];

        if (step->set_pen) {
            (void)bento_ui_set_pen(ui, group, step->pen);
        }
        (void)bento_ui_add_span(ui, group, step->text,
                                (uint32_t)strlen(step->text),
                                step->size_px, step->flags);
    }
    return status;
}

static bento_status_t build_screen(bento_ui_t *ui)
{
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(ui, "ui.SpanGroup", TITLE_Y, COLOR_TEXT,
                           TITLE_PX);
    }
    if (status == BENTO_OK) {
        status = add_card(ui);
    }
    if (status == BENTO_OK) {
        status = add_paragraph(ui);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เส้นใต้รอดจากภาพขาวดำ สีไม่รอด", NOTE_Y,
                           COLOR_DIM, NOTE_PX);
    }
    return status;
}

/* Keeps the screen up: the polls' events are not used, and a failed
 * poll is not an error. */
static bento_status_t idle(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < IDLE_ROUNDS) && (status == BENTO_OK);
         round++) {
        (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX,
                            &count, NULL);
        status = pause_ms(IDLE_MS);
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_status_t status = build_screen(ui);

    if (status == BENTO_OK) {
        status = idle(ui);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.SpanGroup: .pen() ทาท่อนถัดไป, "
                     ".add_span(ข้อความ, ขนาด, ธง), "
                     ".clear_items() ล้างทั้งย่อหน้า\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
