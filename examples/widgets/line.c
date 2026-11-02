/*
 * line.c -- ui.Line ตัวเดียว (one Line)
 *
 * เส้นหักเจ็ดจุดหกท่อน หนาหกพิกเซล พิกัดของจุดนับจากมุมซ้ายบนของเส้นเอง
 * รันจบเองใน 20 วินาที
 *
 * A polyline of seven points and six segments, six pixels thick; each
 * point is placed from the line's own top-left corner. The example ends
 * by itself after 20 seconds.
 *
 * Twin of line.py
 * (embedded-systems-for-aiot-developer/examples/widgets/line.py),
 * merged into one file from the library twin wref-line.
 *
 * Style: CLEAN_C.md.
 */

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
    COLOR_ACCENT = 0x4A9EFF
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
    LINE_X = 48,
    LINE_Y = 96,
    LINE_W = 696,
    LINE_H = 200,
    LINE_WIDTH = 6,
    NOTE_Y = 344,
    NOTE_PX = 20
};

/* The line's points, from its own top-left corner. */
typedef struct {
    int32_t x;
    int32_t y;
} point_t;

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = look.size_px;
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
    args.max_val = CARD_RADIUS;
    return bento_ui_panel_create(ui, &args, &card);
}

/* The line and its points; a point's refusal is ignored, as modui.c
 * does. */
static bento_status_t add_line(bento_ui_t *ui)
{
    static const point_t points[] = {
        {0, 160},  {120, 120}, {240, 152}, {360, 40},
        {480, 88}, {600, 24},  {696, 72},
    };
    bento_ui_widget_args_t args;
    bento_ui_id_t line = 0U;
    size_t i;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = LINE_X;
    args.y = LINE_Y;
    args.w = LINE_W;
    args.h = LINE_H;
    args.color = COLOR_ACCENT;
    args.font_px = LINE_WIDTH;
    status = bento_ui_line_create(ui, &args, &line);
    for (i = 0U; (i < (sizeof(points) / sizeof(points[0]))) &&
                 (status == BENTO_OK);
         i++) {
        (void)bento_ui_add_point(ui, line, points[i].x, points[i].y);
    }
    return status;
}

static bento_status_t build_screen(bento_ui_t *ui)
{
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            ui, "ui.Line",
            (label_look_t){MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(ui);
    }
    if (status == BENTO_OK) {
        status = add_line(ui);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "เจ็ดจุด หกท่อน หนาหกพิกเซล",
            (label_look_t){MARGIN_X, NOTE_Y, COLOR_DIM, NOTE_PX},
            &unused);
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
        (void)printf("ui.Line: .add_point(x, y) "
                     "นับจากมุมซ้ายบนของเส้นเอง, "
                     "value=ความหนา\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
