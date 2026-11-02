/*
 * msgbox.c -- กล่องข้อความ ui.MsgBox (Widget: ui.MsgBox)
 *
 * กล่องข้อความสำหรับถามยืนยัน บรรทัดแรกของ text= คือหัวเรื่อง บรรทัดที่เหลือคือเนื้อความ
 * value=1 เพิ่มปุ่มกากบาทปิดที่มุมขวาบน ปุ่มท้ายกล่องเติมด้วย .add_button(ข้อความ)
 * แต่ปุ่มท้ายกล่องไม่ส่ง event กลับมา จึงวางปุ่ม ui.Button จริงสองปุ่มไว้ข้างนอกกล่อง
 * แตะปุ่มจริงแล้วโปรแกรมพิมพ์ว่าแตะปุ่มไหน ตัวอย่างจบเองใน 20 วินาที
 *
 * A message box that asks for a confirmation: the first line of text=
 * is its title and the rest its body; value=1 adds a close cross at the
 * top right, and .add_button(text) adds the buttons along its bottom.
 * Those buttons send no event back, so two real ui.Button widgets sit
 * outside the box, and a tap on one prints which one it was. The
 * example ends by itself after 20 seconds and needs no network and no
 * sensor.
 *
 * Twin of msgbox.py
 * (embedded-systems-for-aiot-developer/examples/widgets/msgbox.py),
 * merged into one file from the library twin aiot-widget-msgbox.
 * Style: CLEAN_C.md.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Timing. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    ROUND_MS = 200,  /* read the taps, then sleep this long */
    ROUNDS = 100     /* 100 rounds of ROUND_MS: 20 seconds */
};

/* The two real buttons, the only widgets whose taps the program
 * answers. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t confirm;
    bento_ui_id_t cancel;
} confirm_screen_t;

enum {
    EVENT_NAME_COUNT = 15
};

/* The name ui.poll() gives an event type (modui.c,
 * ui_event_type_name). */
typedef struct {
    uint8_t kind;
    const char *name;
} event_name_t;

static const char *event_name(uint8_t kind)
{
    static const event_name_t names[EVENT_NAME_COUNT] = {
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
        {BENTO_UI_EVENT_SCROLL_BEGIN, "scroll_begin"},
        {BENTO_UI_EVENT_SCROLL_END, "scroll_end"},
        {BENTO_UI_EVENT_GESTURE, "gesture"},
    };
    const char *name = "unknown";
    int32_t i;

    for (i = 0; i < EVENT_NAME_COUNT; i++) {
        if (names[i].kind == kind) {
            name = names[i].name;
        }
    }
    return name;
}

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    BOX_Y = 72,
    BOX_W = 440,
    BOX_H = 240,
    BUTTON_X = 496,
    CONFIRM_Y = 72,
    CANCEL_Y = 192,
    BUTTON_W = 272,
    BUTTON_H = 88, /* the touch-target height */
    FOOTER_Y = 344
};

/* Font sizes, in pixels. */
enum {
    TITLE_PX = 28,
    BUTTON_PX = 24,
    CAPTION_PX = 20
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF
};

static const char TEXT_CONFIRM[] = "ยืนยัน";
static const char TEXT_CANCEL[] = "ยกเลิก";

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                int32_t font_px)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = MARGIN_X;
    args.y = y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, &label);
}

/* The box, then the two buttons along its bottom. The whole text, title
 * and body together, has a Label's limit of 126 bytes: a longer one is
 * cut short without a word. */
static bento_status_t add_message_box(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t box;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "ยืนยันคำสั่ง\nสั่งปั๊มเดิน 30 วินาที";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = MARGIN_X;
    args.y = BOX_Y;
    args.w = BOX_W;
    args.h = BOX_H;
    args.color = COLOR_TEXT;
    args.font_px = 1; /* value=1: the close cross */
    status = bento_ui_msgbox_create(ui, &args, &box);
    if (status == BENTO_OK) {
        /* .add_button() is sent without waiting for an answer
         * (modui.c), so a refusal is never seen there and is not seen
         * here either. */
        (void)bento_ui_add_button(ui, box, TEXT_CONFIRM,
                                  (uint32_t)strlen(TEXT_CONFIRM), false,
                                  0U);
        (void)bento_ui_add_button(ui, box, TEXT_CANCEL,
                                  (uint32_t)strlen(TEXT_CANCEL), false,
                                  0U);
    }
    return status;
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 int16_t y, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = BUTTON_X;
    args.y = y;
    args.w = BUTTON_W;
    args.h = BUTTON_H;
    args.color = color;
    args.font_px = BUTTON_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(confirm_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
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
        status =
            add_label(ui, "ui.MsgBox", TITLE_Y, COLOR_TEXT, TITLE_PX);
    }
    if (status == BENTO_OK) {
        status = add_message_box(ui);
    }
    if (status == BENTO_OK) {
        status = add_button(ui, TEXT_CONFIRM, CONFIRM_Y, COLOR_ACCENT,
                            &screen->confirm);
    }
    if (status == BENTO_OK) {
        status = add_button(ui, TEXT_CANCEL, CANCEL_Y, COLOR_DIM,
                            &screen->cancel);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ปุ่มในกล่องเป็นของประดับ", FOOTER_Y,
                           COLOR_DIM, CAPTION_PX);
    }
    return status;
}

/* Any kind of event on a real button counts: the original reads only
 * the handle. */
static void report_event(const confirm_screen_t *screen,
                         const bento_ui_event_t *event)
{
    if (event->id == screen->confirm) {
        (void)printf("กดยืนยันจากปุ่มจริง\n");
    } else if (event->id == screen->cancel) {
        (void)printf("กดยกเลิกจากปุ่มจริง\n");
    } else {
        (void)printf("event จากที่อื่น handle %" PRIu32 " %s\n",
                     bento_ui_id_to_handle(event->id),
                     event_name(event->type));
    }
}

static void handle_events(const confirm_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    /* ui.poll() answers no events when it fails, so a failed poll is
     * not an error here. */
    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        report_event(screen, &events[i]);
    }
}

/* ROUNDS rounds: read the taps, then sleep ROUND_MS (20 s in all). */
static bento_status_t run_rounds(const confirm_screen_t *screen)
{
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        handle_events(screen);
        status = bento_time_sleep_ms(ROUND_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    confirm_screen_t screen = {0};
    bento_status_t status;

    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        status = run_rounds(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.MsgBox: บรรทัดแรก=หัวเรื่อง, value=1 "
                     "เพิ่มกากบาท, "
                     "ปุ่มท้ายกล่องไม่ส่ง event\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
