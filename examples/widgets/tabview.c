/*
 * tabview.c -- แท็บสามหน้าในพื้นที่เดียว (Tabview Widget)
 *
 * คอนเทนเนอร์ที่แบ่งพื้นที่เดิมเป็นสามหน้า มีแถบแท็บภาษาไทยด้านบน ใต้ลงมาคือของประจำแท็บ
 * ทุกหกวินาทีโปรแกรมสลับแท็บเอง ให้เห็นว่าทั้งสามหน้ามีของจริง ถ้าแตะเปลี่ยนแท็บ
 * โปรแกรมพิมพ์ลำดับแท็บออกมา รัน 24 วินาทีแล้วจบเอง ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์
 *
 * A container that splits one area into three pages, with a bar of Thai
 * tab names on top and each tab's contents below. Every six seconds the
 * program switches the tab itself, to show that all three pages hold
 * something; a tap that changes the tab prints its index. It runs 24
 * seconds and ends by itself; it needs no network and no sensor.
 *
 * Twin of tabview.py
 * (embedded-systems-for-aiot-developer/examples/widgets/tabview.py),
 * merged into one file from the library twin wref-tabview.
 * Style: CLEAN_C.md. What differs from
 * MicroPython only because the language forces it:
 *  - the note's text is built in a fixed buffer with snprintf instead
 *    of string +.
 *  - Stop during a sleep raises KeyboardInterrupt in MicroPython; here
 *    the sleep returns and the program ends at that point.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Timing and the tab switching. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 400,
    LOOPS = 60, /* 60 x 400 ms = 24 seconds */
    /* the program switches the tab every 15 rounds, 6 seconds */
    LOOPS_PER_TAB = 15,
    TAB_COUNT = 3
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t tabview;
    bento_ui_id_t note;
} tab_screen_t;

/* Stop is a status: a sleep that Stop ended answers
 * BENTO_E_CANCELLED. */
static bento_status_t sleep_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* True when the event is of this kind and came from this widget. */
static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* Layout on the 792 x 398 canvas; a page's children count from the
 * page's top-left corner. */
enum {
    SCREEN_TEXT_X = 24,
    TITLE_Y = 16,
    TITLE_FONT_PX = 28,
    TABVIEW_X = 24,
    TABVIEW_Y = 72,
    TABVIEW_W = 744,
    TABVIEW_H = 248,
    /* the default 40 px is below the 88 px touch target */
    TAB_BAR_H = 88,
    PAGE_TEXT_X = 16,
    PAGE_FIRST_Y = 16,
    PAGE_SECOND_Y = 56,
    HEADING_FONT_PX = 24,
    NOTE_FONT_PX = 20,
    FOOTER_Y = 344
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF
};

/* The note's text: its prefix and the longest tab name, with the
 * NUL. */
enum {
    NOTE_TEXT_BYTES = 96
};

static const char *const TAB_NAMES[TAB_COUNT] = {"ตอนนี้", "ย้อนหลัง",
                                                 "ตั้งค่า"};

/* The three pages, each the parent of its own labels. */
typedef struct {
    bento_ui_id_t now;
    bento_ui_id_t history;
    bento_ui_id_t settings;
} pages_t;

/* A label inside `parent` (0: the screen). */
static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                int16_t font_px, bento_ui_id_t parent)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x =
        (parent == BENTO_UI_ID_INVALID) ? SCREEN_TEXT_X : PAGE_TEXT_X;
    args.y = y;
    args.color = color;
    args.font_px = font_px;
    args.parent = parent;
    return bento_ui_label_create(ui, &args, &label);
}

static bento_status_t add_tabview(tab_screen_t *screen, pages_t *pages)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = TABVIEW_X;
    args.y = TABVIEW_Y;
    args.w = TABVIEW_W;
    args.h = TABVIEW_H;
    args.color = COLOR_CARD;
    args.font_px = TAB_BAR_H;
    status =
        bento_ui_tabview_create(screen->ui, &args, &screen->tabview);
    if (status == BENTO_OK) {
        status = bento_ui_add_tab(
            screen->ui, screen->tabview, TAB_NAMES[0],
            (uint32_t)strlen(TAB_NAMES[0]), &pages->now);
    }
    if (status == BENTO_OK) {
        status = bento_ui_add_tab(
            screen->ui, screen->tabview, TAB_NAMES[1],
            (uint32_t)strlen(TAB_NAMES[1]), &pages->history);
    }
    if (status == BENTO_OK) {
        status = bento_ui_add_tab(
            screen->ui, screen->tabview, TAB_NAMES[2],
            (uint32_t)strlen(TAB_NAMES[2]), &pages->settings);
    }
    return status;
}

static bento_status_t fill_pages(bento_ui_t *ui, const pages_t *pages)
{
    bento_status_t status =
        add_label(ui, "อุณหภูมิ 25.4 องศา", PAGE_FIRST_Y, COLOR_TEXT,
                  HEADING_FONT_PX, pages->now);

    if (status == BENTO_OK) {
        status = add_label(ui, "อยู่ในพิสัยปกติ", PAGE_SECOND_Y, COLOR_DIM,
                           NOTE_FONT_PX, pages->now);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "สูงสุดวันนี้ 31.2 องศา", PAGE_FIRST_Y,
                           COLOR_TEXT, HEADING_FONT_PX, pages->history);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ต่ำสุดวันนี้ 22.8 องศา", PAGE_SECOND_Y,
                           COLOR_TEXT, HEADING_FONT_PX, pages->history);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "แจ้งเตือนที่ 60 องศา", PAGE_FIRST_Y,
                      COLOR_ACCENT, HEADING_FONT_PX, pages->settings);
    }
    return status;
}

static bento_status_t add_note(tab_screen_t *screen)
{
    char text[NOTE_TEXT_BYTES];
    bento_ui_widget_args_t args;

    (void)snprintf(text, sizeof(text), "แท็บที่เปิดอยู่: %s", TAB_NAMES[0]);
    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = SCREEN_TEXT_X;
    args.y = FOOTER_Y;
    args.color = COLOR_DIM;
    args.font_px = NOTE_FONT_PX;
    return bento_ui_label_create(screen->ui, &args, &screen->note);
}

static bento_status_t build_screen(tab_screen_t *screen)
{
    pages_t pages = {0U, 0U, 0U};
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "ui.Tabview", TITLE_Y, COLOR_TEXT,
                      TITLE_FONT_PX, BENTO_UI_ID_INVALID);
    }
    if (status == BENTO_OK) {
        status = add_tabview(screen, &pages);
    }
    if (status == BENTO_OK) {
        status = fill_pages(screen->ui, &pages);
    }
    if (status == BENTO_OK) {
        status = add_note(screen);
    }
    return status;
}

/* Opens tab `pick` and names it in the note. The tab is set through its
 * property, which switches at once; .value() would slide, and a picture
 * taken mid-slide shows the old tab beside the new note. */
static void show_tab(const tab_screen_t *screen, int32_t pick)
{
    char text[NOTE_TEXT_BYTES];

    /* MicroPython's widget setters ignore a refusal (modui.c), so
     * the twin does too. */
    (void)bento_ui_set_prop(screen->ui, screen->tabview,
                            BENTO_UI_PROP_ACTIVE_TAB, pick);
    (void)snprintf(text, sizeof(text), "แท็บที่เปิดอยู่: %s",
                   TAB_NAMES[pick]);
    (void)bento_ui_set_text(screen->ui, screen->note, text,
                            (uint32_t)strlen(text));
}

static void handle_events(const tab_screen_t *screen)
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
        if (event_is(&events[i], BENTO_UI_EVENT_VALUE_CHANGED,
                     screen->tabview)) {
            (void)printf("สลับไปแท็บลำดับที่ %d\n", (int)events[i].value);
        }
    }
}

static bento_status_t run_event_loop(const tab_screen_t *screen)
{
    int32_t loop;
    bento_status_t status = BENTO_OK;

    for (loop = 0; (loop < LOOPS) && (status == BENTO_OK); loop++) {
        if ((loop % LOOPS_PER_TAB) == 0) {
            show_tab(screen, (loop / LOOPS_PER_TAB) % TAB_COUNT);
        }
        handle_events(screen);
        status = sleep_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    tab_screen_t screen = {bento_runtime_get_ui(), 0U, 0U};
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        status = run_event_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.Tabview: .add_tab() คืนหน้า, "
                     "value=ความสูงแถบ, "
                     ".value(n) เลือกแท็บ\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
