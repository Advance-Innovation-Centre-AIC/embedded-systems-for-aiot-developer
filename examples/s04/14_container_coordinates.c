/*
 * 14_container_coordinates.c -- กล่องที่คอนเทนเนอร์คืนมา: parent= เปลี่ยนพิกัด
 *
 * ป้ายสามใบ ข้อความเดียวกัน "x=24 y=56" พิกัดเดียวกัน แต่อยู่คนละที่ ใบแรกเป็น
 * ลูกของจอ ใบที่สองอยู่ในการ์ด Panel ใบที่สามอยู่ในหน้าของแท็บ พิกัดของลูกนับ
 * จากมุมของพ่อ กล่องเป็น "ที่" ไม่ใช่ของที่ตอบได้ ถามค่าได้ 0 เสมอ และไม่ส่ง
 * event โปรแกรมนับ event 60 รอบ รอบละ 200 ms แล้วสรุป
 *
 * Three labels with the same text "x=24 y=56" and the same coordinates
 * land in three places: one is a child of the screen, one sits in a
 * Panel card, one in a tab page. A child's coordinates count from its
 * parent's corner. A box is a place, not something that answers: its
 * value reads 0 and it sends no events. The program counts events for
 * 60 rounds of 200 ms, then reports.
 *
 * Twin of 14_container_coordinates.py
 * (embedded-systems-for-aiot-developer/examples/s04/14_container_coordinates.py),
 * from the library twin aiot-s04-14-container-coordinates. Style:
 * CLEAN_C.md. The course file polls once per 200 ms round, so the
 * twin's separate 20 ms tap clock is gone.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Timing. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    ROUNDS = 60,
    /* one round lasts 200 ms: 60 rounds are 12 seconds */
    STEP_MS = 200,
    TEXT_BYTES = 128 /* the longest text built here is 115 bytes */
};

/* The widgets the program reads or changes after it has drawn them. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t tabs;
    bento_ui_id_t page;
    bento_ui_id_t line;
    int32_t answer; /* what the box answered when asked for its value */
} box_screen_t;

/* Layout on the 792 x 398 canvas. Inside a parent, x and y count
 * from the parent's corner. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    SAME_X = 24, /* the x and the y every "x=24 y=56" label is given */
    SAME_Y = 56,
    CAPTION_Y = 16,
    BOX_Y = 96,
    CARD_W = 352,
    BOX_H = 224,
    CARD_RADIUS = 8,
    TABS_X = 400,
    TABS_W = 368,
    TAB_BAR_H = 88,
    BOTTOM_Y = 336,
    TITLE_PX = 28,
    SAME_PX = 24,
    TEXT_PX = 20
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF
};

/* The very same text on all three labels: what differs on screen is
 * the coordinates alone. */
static const char TEXT_SAME[] = "x=24 y=56";

/* Where a widget goes: x and y inside its parent (0 = the screen). */
typedef struct {
    int16_t x;
    int16_t y;
    bento_ui_id_t parent;
} spot_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                spot_t spot, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = spot.x;
    args.y = spot.y;
    args.color = color;
    args.font_px = font_px;
    args.parent = spot.parent;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A caption, then the "x=24 y=56" label, both inside the parent. */
static bento_status_t add_example(bento_ui_t *ui, bento_ui_id_t parent,
                                  const char *caption)
{
    const spot_t caption_spot = {MARGIN_X, CAPTION_Y, parent};
    const spot_t same_spot = {SAME_X, SAME_Y, parent};
    bento_ui_id_t unused;
    bento_status_t status = add_label(ui, caption, caption_spot,
                                      COLOR_DIM, TEXT_PX, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, TEXT_SAME, same_spot, COLOR_ACCENT,
                           SAME_PX, &unused);
    }
    return status;
}

/* Label 2: its parent is a plain Panel, not a box a container hands
 * back, and that works. */
static bento_status_t add_card_example(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = BOX_Y;
    args.w = CARD_W;
    args.h = BOX_H;
    args.color = COLOR_CARD;
    args.max_val = CARD_RADIUS;
    status = bento_ui_panel_create(ui, &args, &card);
    if (status == BENTO_OK) {
        status = add_example(ui, card, "ป้ายใบนี้อยู่ในการ์ด");
    }
    return status;
}

/* Label 3: its parent is a tab page, a real box, made by .add_tab(). */
static bento_status_t add_tab_example(box_screen_t *screen)
{
    const char *name = "หน้าในแท็บ";
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = TABS_X;
    args.y = BOX_Y;
    args.w = TABS_W;
    args.h = BOX_H;
    args.color = COLOR_CARD;
    /* value= of a Tabview is its tab bar's height */
    args.font_px = TAB_BAR_H;
    status = bento_ui_tabview_create(screen->ui, &args, &screen->tabs);
    if (status == BENTO_OK) {
        status =
            bento_ui_add_tab(screen->ui, screen->tabs, name,
                             (uint32_t)strlen(name), &screen->page);
    }
    if (status == BENTO_OK) {
        status =
            add_example(screen->ui, screen->page, "ป้ายใบนี้อยู่ในหน้าแท็บ");
    }
    return status;
}

/* A box is a place, not something that answers: these two calls leave
 * the screen as it is, and asking for its value answers 0. */
static void probe_box(box_screen_t *screen)
{
    const char *text = "ชื่อใหม่ที่ไม่มีวันขึ้นจอ";

    /* MicroPython's widget setters ignore a refusal (modui.c), so
     * the twin does too. */
    (void)bento_ui_set_text(screen->ui, screen->page, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_value(screen->ui, screen->page, 1);
    /* MicroPython answers 0 when the display does not; get_value has
     * cleared answer then */
    (void)bento_ui_get_value(screen->ui, screen->page, &screen->answer);
}

/* The bottom row: the real handle numbers, and what the box
 * answered. */
static bento_status_t add_findings(box_screen_t *screen)
{
    const spot_t budget_spot = {TABS_X, BOTTOM_Y, 0U};
    const spot_t line_spot = {MARGIN_X, BOTTOM_Y, 0U};
    char text[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_status_t status;

    (void)snprintf(text, sizeof(text),
                   "แท็บ id %" PRIu32 " + หน้าของมัน id %" PRIu32,
                   bento_ui_id_to_handle(screen->tabs),
                   bento_ui_id_to_handle(screen->page));
    status = add_label(screen->ui, text, budget_spot, COLOR_DIM,
                       TEXT_PX, &unused);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "ถามค่าจากกล่อง ได้ %" PRId32,
                       screen->answer);
        status = add_label(screen->ui, text, line_spot, COLOR_DIM,
                           TEXT_PX, &screen->line);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(box_screen_t *screen)
{
    const spot_t title = {MARGIN_X, TITLE_Y, 0U};
    const spot_t same_on_screen = {SAME_X, SAME_Y, 0U};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กล่องที่คอนเทนเนอร์คืนมา", title,
                           COLOR_TEXT, TITLE_PX, &unused);
    }
    /* label 1: no parent, so it counts from the screen's corner */
    if (status == BENTO_OK) {
        status = add_label(screen->ui, TEXT_SAME, same_on_screen,
                           COLOR_DIM, SAME_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card_example(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_tab_example(screen);
    }
    if (status == BENTO_OK) {
        probe_box(screen);
        status = add_findings(screen);
    }
    return status;
}

/* lcd.print() ignores a console that is not ready (modlcd.c), so the
 * twin does too. */
static void print_findings(const box_screen_t *screen)
{
    char text[TEXT_BYTES];

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>กล่อง - ที่อยู่ "
                            "ไม่ใช่ของที่ตอบได้</h2>");
    (void)bento_lcd_print("ป้ายสามใบ ข้อความเดียวกัน พิกัดเดียวกัน "
                          "แต่คนละที่");
    (void)snprintf(text, sizeof(text),
                   "Tabview id %" PRIu32 " + หน้าของมัน id %" PRIu32
                   " = 2 แฮนเดิล",
                   bento_ui_id_to_handle(screen->tabs),
                   bento_ui_id_to_handle(screen->page));
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "ถามค่าจากกล่องได้ %" PRId32 " - ทุกครั้ง ทุกกล่อง",
                   screen->answer);
    (void)bento_lcd_print(text);
}

/* Counts every event of every widget for ROUNDS rounds of STEP_MS. A
 * box registers no callback, so the count stays at zero. */
static bento_status_t count_events(const box_screen_t *screen,
                                   int32_t *events_seen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    bento_status_t status = BENTO_OK;
    int32_t round;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        uint32_t count = 0U;

        /* ui.poll() answers no events when it fails, so a failed
         * poll is not an error here. */
        if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                          &count, NULL) != BENTO_OK) {
            count = 0U;
        }
        *events_seen += (int32_t)count;
        status = wait_ms(STEP_MS);
    }
    return status;
}

int main(void)
{
    box_screen_t screen = {bento_runtime_get_ui(), 0U, 0U, 0U, 0};
    int32_t events_seen = 0;
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        print_findings(&screen);
        status = count_events(&screen, &events_seen);
    }
    if (status == BENTO_OK) {
        char text[TEXT_BYTES];

        (void)snprintf(text, sizeof(text),
                       "กล่องไม่ส่ง event - นับได้ %" PRId32 " ครั้ง",
                       events_seen);
        /* MicroPython's widget setters ignore a refusal (modui.c),
         * so the twin does too. */
        (void)bento_ui_set_text(screen.ui, screen.line, text,
                                (uint32_t)strlen(text));
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>event จากกล่อง = %" PRId32
                       " </span>",
                       events_seen);
        (void)bento_lcd_print(text);
        (void)printf("Container: parent= เปลี่ยนระบบพิกัดของลูก "
                     "และกล่องกินแฮนเดิลใบละหนึ่ง\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
