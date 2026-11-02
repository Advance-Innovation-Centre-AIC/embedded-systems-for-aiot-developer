/*
 * win.c -- ui.Win ตัวเดียว (one Win)
 *
 * หน้าต่างมีแถบหัว (text=) ของข้างในใส่ใน .content() ซึ่งคืนตัวหน้าต่างเดิมทุกครั้ง
 * ทุก event ที่แตะได้จะพิมพ์ชนิดกับแฮนเดิลออกคอนโซล รันจบเองใน 20 วินาที
 *
 * A window with a title bar (text=); what goes inside is placed in
 * content(), which returns the same body every time. Every touch event
 * is printed with its type and handle. The example ends by itself after
 * 20 seconds.
 *
 * Twin of win.py
 * (embedded-systems-for-aiot-developer/examples/widgets/win.py),
 * merged into one file from the library twin aiot-widget-win.
 * Style: CLEAN_C.md.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Timing: 100 rounds of 200 ms (20 s). */
enum {
    SETTLE_MS = 200,
    ROUND_MS = 200, /* read the taps, then sleep this long */
    ROUNDS = 100
};

static bento_status_t build_screen(bento_ui_t *ui);

static const char *event_name(uint8_t type);

static bento_status_t pause_ms(uint32_t duration_ms);

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF
};

/* Layout on the 792 x 398 canvas; the body's children are placed
 * from the body's corner. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    TITLE_PX = 28,
    WIN_Y = 72,
    WIN_W = 744,
    WIN_H = 248,
    LINE_PX = 24,
    BUTTON_X = 432,
    BUTTON_Y = 40,
    BUTTON_W = 232,
    BUTTON_H = 88,
    FOOT_Y = 344,
    FOOT_PX = 20
};

/* The report lines inside the window, top to bottom. */
typedef struct {
    const char *text;
    int16_t y;
} line_t;

/* parent 0 means the screen, as a widget made without parent=. */
static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                int32_t size_px, bento_ui_id_t parent)
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
    args.parent = parent;
    return bento_ui_label_create(ui, &args, &label);
}

static bento_status_t add_button(bento_ui_t *ui, bento_ui_id_t parent)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t button;

    bento_ui_widget_args_init(&args);
    args.text = "รับทราบ";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = BUTTON_X;
    args.y = BUTTON_Y;
    args.w = BUTTON_W;
    args.h = BUTTON_H;
    args.color = COLOR_ACCENT;
    args.font_px = LINE_PX;
    args.parent = parent;
    return bento_ui_button_create(ui, &args, &button);
}

/* The window, its body, the report lines and the button inside it. */
static bento_status_t add_window(bento_ui_t *ui)
{
    static const line_t report_lines[] = {
        {"เดินเครื่อง 7 ชั่วโมง 20 นาที", 16},
        {"หยุดฉุกเฉิน 0 ครั้ง", 56},
        {"แจ้งเตือน 3 ครั้ง", 96},
    };
    bento_ui_widget_args_t args;
    bento_ui_id_t window = 0U;
    bento_ui_id_t body = 0U;
    size_t i;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "รายงานประจำกะ";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = MARGIN_X;
    args.y = WIN_Y;
    args.w = WIN_W;
    args.h = WIN_H;
    args.color = COLOR_CARD;
    status = bento_ui_win_create(ui, &args, &window);
    if (status == BENTO_OK) {
        status = bento_ui_win_content(ui, window, &body);
    }
    for (i = 0U;
         (i < (sizeof(report_lines) / sizeof(report_lines[0]))) &&
         (status == BENTO_OK);
         i++) {
        status = add_label(ui, report_lines[i].text, report_lines[i].y,
                           COLOR_TEXT, LINE_PX, body);
    }
    if (status == BENTO_OK) {
        status = add_button(ui, body);
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
        status =
            add_label(ui, "ui.Win", TITLE_Y, COLOR_TEXT, TITLE_PX, 0U);
    }
    if (status == BENTO_OK) {
        status = add_window(ui);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "content() คือที่ที่ของข้างในไปอยู่", FOOT_Y,
                           COLOR_DIM, FOOT_PX, 0U);
    }
    return status;
}

/* The name ui.poll() gives each event type (modui.c), for the
 * printed line only. */
typedef struct {
    uint8_t type;
    const char *name;
} event_name_t;

static const char *event_name(uint8_t type)
{
    static const event_name_t event_names[] = {
        {BENTO_UI_EVENT_CLICKED, "clicked"},
        {BENTO_UI_EVENT_VALUE_CHANGED, "value_changed"},
        {BENTO_UI_EVENT_TOGGLED, "toggled"},
        {BENTO_UI_EVENT_PRESSED, "pressed"},
        {BENTO_UI_EVENT_RELEASED, "released"},
        {BENTO_UI_EVENT_PRESS_LOST, "press_lost"},
        {BENTO_UI_EVENT_LONG_PRESSED, "long_pressed"},
        {BENTO_UI_EVENT_LONG_PRESSED_REPEAT, "long_pressed_repeat"},
        {BENTO_UI_EVENT_READY, "ready"},
        {BENTO_UI_EVENT_CANCEL, "cancel"},
        {BENTO_UI_EVENT_FOCUSED, "focused"},
        {BENTO_UI_EVENT_DEFOCUSED, "defocused"},
    };
    const char *name = "unknown";
    size_t i;

    for (i = 0U; i < (sizeof(event_names) / sizeof(event_names[0]));
         i++) {
        if (event_names[i].type == type) {
            name = event_names[i].name;
        }
    }
    return name;
}

/* time.sleep_ms(): BENTO_E_CANCELLED when Stop arrives, as the
 * original's KeyboardInterrupt. */
bento_status_t pause_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Prints every event of one poll; a failed poll has no events. */
static void print_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        (void)printf("event %s handle %ld\n",
                     event_name(events[i].type),
                     (long)bento_ui_id_to_handle(events[i].id));
    }
}

/* ROUNDS rounds: read the taps, then sleep ROUND_MS (20 s in all). */
static bento_status_t run_rounds(bento_ui_t *ui)
{
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        print_events(ui);
        status = pause_ms(ROUND_MS);
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_status_t status = build_screen(ui);

    if (status == BENTO_OK) {
        status = run_rounds(ui);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.Win: text=แถบหัว, .content() คืนตัวหน้าต่าง "
                     "เรียกซ้ำได้แฮนเดิลเดิม\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
