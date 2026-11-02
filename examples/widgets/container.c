/*
 * container.c -- กล่อง (Container) ตัวเดียว (Container)
 *
 * กล่องเป็นชนิดเดียวที่สร้างตรง ๆ ไม่ได้ มันถูกคืนมาจาก .add_tab() .content()
 * และพวก เดียวกัน แล้วเป็น "ที่" ให้ widget อื่นไปอยู่ผ่าน parent= ซึ่งทำให้พิกัด x= y=
 * ของลูก นับจากมุมซ้ายบนของกล่อง ป้ายสี่ใบเขียนข้อความเดียวกันว่า x=24 y=56
 * แต่ไปโผล่คนละที่ ใบที่ 1 บนจอ ใบที่ 2 ในการ์ด (Panel ธรรมดาก็เป็นพ่อได้) ใบที่ 3
 * ในหน้าของแท็บ ใบที่ 4 ในตัวหน้าต่าง สั่งข้อความหรือค่าให้กล่องแล้วจอไม่ขยับ
 * บรรทัดล่างบอกแฮนเดิลที่กล่องกิน และค่าที่ถามจาก กล่องได้ (0) รันจบเองใน 20 วินาที
 * แล้วบอกว่าได้ event กี่ครั้ง (ควรเป็น 0)
 *
 * A container is the one kind that cannot be created directly:
 * .add_tab(), .content() and their kin return one, and other widgets
 * live in it through parent=, so a child's x= y= count from the
 * container's top-left corner. Four labels with the same text "x=24
 * y=56" land in four places: on the screen, in a card (a plain Panel
 * can be a parent too), in a tab's page and in a window's body. Setting
 * a container's text or value changes nothing on screen; the bottom
 * line shows the handles the containers take and the value a container
 * answers (0). It ends by itself after 20 seconds and prints how many
 * events it got (should be 0).
 *
 * Twin of container.py
 * (embedded-systems-for-aiot-developer/examples/widgets/container.py),
 * merged into one file from the library twin aiot-widget-container.
 * Style: CLEAN_C.md. Stop is not caught
 * by the original: a Stop in any sleep ends the program here too.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

enum {
    ON_SCREEN = 0,   /* parent 0 is the screen itself */
    ROUND_MS = 200,  /* read the taps, then sleep this long */
    ROUNDS = 100,    /* 100 rounds x 200 ms = 20 s, as before */
    SETTLE_MS = 200, /* let the cleared screen settle */
    /* the longest footer text, with room for its numbers */
    TEXT_CHARS = 96
};

/* Screen layout on the 792 x 398 canvas: three boxes side by side. A
 * child's position is counted from its parent's corner. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    /* the four labels' own x= y=, the same for every parent */
    SAME_X = 24,
    SAME_Y = 56,
    BOX_Y = 96,
    BOX_W = 232,
    BOX_H = 224,
    TABS_X = 280,
    WINDOW_X = 536,
    FOOTER_Y = 344,
    ANSWER_X = 440
};

/* Font sizes and box details, in pixels. */
enum {
    FONT_TITLE = 28,
    FONT_SAME = 24,
    FONT_FOOTER = 20,
    CARD_RADIUS = 8,
    TAB_BAR_H = 88
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF
};

/* The containers the program writes to and asks after it made them. */
typedef struct {
    bento_ui_id_t card;
    bento_ui_id_t page;
} boxes_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t x, int16_t y, uint32_t color,
                                int16_t font_px)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, &label);
}

/* One of the four "x=24 y=56" labels, inside parent. The text is the
 * same on every label: what differs on screen comes from the
 * coordinates alone. */
static bento_status_t
add_same_label(bento_ui_t *ui, bento_ui_id_t parent, uint32_t color)
{
    static const char same[] = "x=24 y=56";
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = same;
    args.text_len = (uint32_t)strlen(same);
    args.x = SAME_X;
    args.y = SAME_Y;
    args.color = color;
    args.font_px = FONT_SAME;
    args.parent = parent;
    return bento_ui_label_create(ui, &args, &label);
}

static void box_args(bento_ui_widget_args_t *args, int16_t x)
{
    bento_ui_widget_args_init(args);
    args->x = x;
    args->y = BOX_Y;
    args->w = BOX_W;
    args->h = BOX_H;
    args->color = COLOR_CARD;
}

/* Label 2: its parent is a plain Panel, not a container, and that
 * works too. */
static bento_status_t add_card(bento_ui_t *ui, boxes_t *boxes)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    box_args(&args, MARGIN_X);
    args.max_val = CARD_RADIUS;
    status = bento_ui_panel_create(ui, &args, &boxes->card);
    if (status == BENTO_OK) {
        status = add_same_label(ui, boxes->card, COLOR_ACCENT);
    }
    return status;
}

/* Label 3: its parent is a tab's page, a real container. */
static bento_status_t add_tabs(bento_ui_t *ui, boxes_t *boxes)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t tabs;
    bento_status_t status;

    box_args(&args, TABS_X);
    args.font_px = TAB_BAR_H;
    status = bento_ui_tabview_create(ui, &args, &tabs);
    if (status == BENTO_OK) {
        status = bento_ui_add_tab(
            ui, tabs, "แท็บ", (uint32_t)strlen("แท็บ"), &boxes->page);
    }
    if (status == BENTO_OK) {
        status = add_same_label(ui, boxes->page, COLOR_ACCENT);
    }
    return status;
}

/* Label 4: its parent is a window's body, a container as well. */
static bento_status_t add_window(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t window;
    bento_ui_id_t body;
    bento_status_t status;

    box_args(&args, WINDOW_X);
    args.text = "หน้าต่าง";
    args.text_len = (uint32_t)strlen(args.text);
    status = bento_ui_win_create(ui, &args, &window);
    if (status == BENTO_OK) {
        status = bento_ui_win_content(ui, window, &body);
    }
    if (status == BENTO_OK) {
        status = add_same_label(ui, body, COLOR_ACCENT);
    }
    return status;
}

/* A container answers neither a text nor a value (ui_widget_mgr.c has
 * no case for it), so these two change nothing on screen and report no
 * error; the setters' status is ignored as MicroPython ignores it
 * (modui.c). The footer then shows the handles the boxes took and what
 * the page answers when asked for its value (0 when the display does
 * not answer). */
static bento_status_t add_footer(bento_ui_t *ui, const boxes_t *boxes)
{
    char text[TEXT_CHARS];
    int32_t value = 0;
    bento_status_t status;

    (void)bento_ui_set_text(ui, boxes->page, "ชื่อใหม่ที่ไม่มีวันขึ้นจอ",
                            (uint32_t)strlen("ชื่อใหม่ที่ไม่มีวันขึ้นจอ"));
    (void)bento_ui_set_value(ui, boxes->page, 1);
    (void)snprintf(text, sizeof(text),
                   "การ์ด id %" PRIu32 " หน้าแท็บ id %" PRIu32,
                   bento_ui_id_to_handle(boxes->card),
                   bento_ui_id_to_handle(boxes->page));
    status =
        add_label(ui, text, MARGIN_X, FOOTER_Y, COLOR_DIM, FONT_FOOTER);
    if (status == BENTO_OK) {
        if (bento_ui_get_value(ui, boxes->page, &value) != BENTO_OK) {
            value = 0;
        }
        (void)snprintf(text, sizeof(text), "ถามค่าจากกล่องได้ %" PRId32,
                       value);
        status = add_label(ui, text, ANSWER_X, FOOTER_Y, COLOR_DIM,
                           FONT_FOOTER);
    }
    return status;
}

/* Draws the screen in the original's order (the widgets' handles
 * follow it). */
static bento_status_t build_screen(bento_ui_t *ui)
{
    boxes_t boxes = {0U, 0U};
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
        status = add_label(ui, "กล่อง (Container)", MARGIN_X, TITLE_Y,
                           COLOR_TEXT, FONT_TITLE);
    }
    /* label 1: no parent=, a child of the screen */
    if (status == BENTO_OK) {
        status = add_same_label(ui, ON_SCREEN, COLOR_DIM);
    }
    if (status == BENTO_OK) {
        status = add_card(ui, &boxes);
    }
    if (status == BENTO_OK) {
        status = add_tabs(ui, &boxes);
    }
    if (status == BENTO_OK) {
        status = add_window(ui);
    }
    if (status == BENTO_OK) {
        status = add_footer(ui, &boxes);
    }
    return status;
}

/* Counts every event of the round, of any kind from any widget. */
static uint32_t count_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* ui.poll() answers no events when it fails, so a failed poll is
     * not an error here. */
    if (bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    return count;
}

/* ROUNDS rounds: read the taps, then sleep ROUND_MS (20 s in all). */
static bento_status_t run_rounds(bento_ui_t *ui, uint32_t *events)
{
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        *events += count_events(ui);
        status = bento_time_sleep_ms(ROUND_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    uint32_t events = 0U;
    bento_status_t status = build_screen(ui);

    if (status == BENTO_OK) {
        status = run_rounds(ui, &events);
    }
    if (status == BENTO_OK) {
        /* 0 expected: register_object() binds only LV_EVENT_DELETE,
         * to free the handle */
        (void)printf("Container: event ที่ได้รับตลอดการรัน = %" PRIu32
                     " (ควรเป็น 0)\n",
                     events);
        (void)printf("parent= เปลี่ยนระบบพิกัดของลูก "
                     "และกล่องกินแฮนเดิลใบละหนึ่ง\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
