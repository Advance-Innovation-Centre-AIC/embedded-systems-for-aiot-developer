/*
 * 07_find_move_hide_delete.c -- จัดการ widget ที่สร้างไปแล้ว
 *
 * ui.list() บอกว่าบนจอมีอะไรอยู่จริง ui.get(id) เอา widget กลับคืนมาจากเลข
 * อย่างเดียว แล้วย้าย ย่อ ซ่อน แสดง หรือลบมันได้เหมือนตัวจริงทุกอย่าง จอมีกล่อง
 * สีส้มหนึ่งใบเป็นเป้าหมาย ปุ่มสามใบสั่งย้าย สั่งย่อ-ขยาย และสั่งซ่อน-แสดง
 * บรรทัดล่างรายงานว่า ui.list() นับได้กี่ตัว ของที่ซ่อนไว้ยังกินโควตา มีแต่
 * delete() ที่คืนช่อง ตอนจบเรียก ui.clear() ล้างทั้งหน้า
 *
 * ui.list() tells what is really on the screen, ui.get(id) brings a
 * widget back from its number alone, and then it can be moved,
 * resized, hidden, shown or deleted like the original. An orange
 * target box and three buttons (move, resize, hide/show) show it; the
 * bottom line reports the ui.list() count. The Console gets the
 * evidence: the list, ui.get() refusing 99 and a free number, the text
 * read back, and delete() giving its slot back. A hidden widget still
 * holds its slot. At the end ui.clear() empties the page.
 *
 * Twin of 07_find_move_hide_delete.py
 * (embedded-systems-for-aiot-developer/examples/s04/07_find_move_hide_delete.py),
 * rewritten from the library twin aiot-s04-07-find-move-hide-delete.
 * Style: CLEAN_C.md. "except ValueError" around ui.get() becomes a
 * check for BENTO_E_INVALID_ARGUMENT / BENTO_E_BAD_HANDLE, the two statuses modui.c
 * raises as ValueError; any other failure ends the program, as it
 * would in MicroPython. ui.list()'s table (64 entries, 512 bytes) is a
 * file-scope g_rows (C9).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 50,
    DELETE_SETTLE_MS = 120, /* let the CM55 finish deleting first */
    BEFORE_CLEAR_MS = 900,
    RUN_MS = 30000
};

/* The firmware's widget numbers. */
enum {
    MAX_WIDGETS = 64,     /* the firmware ceiling: ids 0-63 */
    OUT_OF_RANGE_ID = 99, /* outside 0-63 */
    FREE_ID = 31          /* in range, but nothing lives there */
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 20,
    TITLE_Y = 12,
    CARD_Y = 52,
    CARD_W = 380,
    CARD_H = 252,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    BUTTON_W = 172,
    BUTTON_H = 88,
    MOVE_X = 40,
    MOVE_Y = 72,
    SIZE_X = 220,
    SIZE_Y = 148,
    HIDE_Y = 224,
    PLACE_A_X = 430, /* the two places the target swaps between */
    PLACE_A_Y = 90,
    PLACE_B_Y = 190,
    SIZE_A_W = 220, /* and its two sizes */
    SIZE_A_H = 70,
    SIZE_B_W = 140,
    SIZE_B_H = 44,
    STATUS_X = 432,
    STATUS_Y = 264,
    CENSUS_Y = 294,
    WARNING_X = 168,
    WARNING_Y = 330,
    THROWAWAY_Y = 308,
    THROWAWAY_W = 140,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    TEXT_BYTES = 128, /* a SET_TEXT carries at most 126 bytes */
    READ_LINE_BYTES =
        192 /* the read-back sentence and up to 126 bytes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0x4A9EFF,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_INFO = 0x4A9EFF
};

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

/* The target is reached through its number only, never its id. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t move;
    bento_ui_id_t resize;
    bento_ui_id_t hide;
    int32_t target_number;
    bento_ui_id_t status;
    bento_ui_id_t census;
    bool hidden; /* the one truth about hidden or shown */
    bool at_a;   /* the target is at place A */
    bool big;    /* the target has the big size */
} census_screen_t;

/* ui.list()'s table, 64 widgets of 8 bytes: too big for the 8 KB
 * program stack, so it is file scope, zeroed by the loader and again
 * at the start of main() (CLEAN_C.md C9, BENTO-MISRA-E03/E04). */
extern bento_ui_widget_info_t g_rows[MAX_WIDGETS];
bento_ui_widget_info_t g_rows[MAX_WIDGETS];

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t widget,
                      const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void print_line(const char *text)
{
    (void)bento_lcd_print(text);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                point_t at, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
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

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 rect_t at, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = at.x;
    args.y = at.y;
    args.w = at.w;
    args.h = at.h;
    args.color = color;
    args.font_px = FONT_TEXT_PX;
    return bento_ui_button_create(ui, &args, out_id);
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
    args.min_val = COLOR_DIM; /* the border colour */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* The name modui.c's ui.list() gives each widget type, by its code. */
static const char *type_name(uint8_t widget_type)
{
    static const char *const names[] = {
        "Unknown",      "Button",   "Label",    "Slider",
        "Switch",       "Checkbox", "Arc",      "Bar",
        "Spinner",      "Dropdown", "Textarea", "Seg7",
        "DotMatrix",    "Chart",    "Image",    "Panel",
        "Compass",      "Unknown",  "Led",      "Scale",
        "Spinbox",      "Table",    "List",     "Roller",
        "ButtonMatrix", "MsgBox",   "Keyboard", "Tabview",
        "Tileview",     "Win",      "Line",     "Picture",
        "Container",    "Menu",     "MenuPage", "SpanGroup",
        "Calendar"};
    const char *name = names[0];

    if ((size_t)widget_type < (sizeof(names) / sizeof(names[0]))) {
        name = names[widget_type];
    }
    return name;
}

/* Counted again after every change, and written to the report line.
 * ui.list() raises when the display does not answer; so does this. */
static bento_status_t count_widgets(const census_screen_t *screen,
                                    uint32_t *count)
{
    char text[TEXT_BYTES];
    bento_status_t status =
        bento_ui_list(screen->ui, g_rows, MAX_WIDGETS, count);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "ui.list() = %" PRIu32 " ตัว",
                       *count);
        show_text(screen->ui, screen->census, text);
    }
    return status;
}

static bento_status_t add_buttons(census_screen_t *screen)
{
    const rect_t move = {MOVE_X, MOVE_Y, BUTTON_W, BUTTON_H};
    const rect_t resize = {SIZE_X, SIZE_Y, BUTTON_W, BUTTON_H};
    const rect_t hide = {MOVE_X, HIDE_Y, BUTTON_W, BUTTON_H};
    const rect_t target = {PLACE_A_X, PLACE_A_Y, SIZE_A_W, SIZE_A_H};
    bento_ui_id_t target_id;
    bento_status_t status =
        add_button(screen->ui, "ย้ายที่", move, COLOR_TEXT, &screen->move);

    if (status == BENTO_OK) {
        status = add_button(screen->ui, "ย่อ ขยาย", resize, COLOR_OK,
                            &screen->resize);
    }
    if (status == BENTO_OK) {
        status = add_button(screen->ui, "ซ่อน แสดง", hide, COLOR_TEXT,
                            &screen->hide);
    }
    if (status == BENTO_OK) {
        /* made once and never used through its id again: every command
         * finds it with ui.get(), to show the number alone is enough */
        status = add_button(screen->ui, "เป้าหมาย", target, COLOR_WARN,
                            &target_id);
    }
    if (status == BENTO_OK) {
        screen->target_number =
            (int32_t)bento_ui_id_to_handle(target_id);
    }
    return status;
}

static bento_status_t build_screen(census_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t title = {MARGIN_X, TITLE_Y};
    const point_t status_line = {STATUS_X, STATUS_Y};
    const point_t census = {STATUS_X, CENSUS_Y};
    const point_t warning = {WARNING_X, WARNING_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ตามหา ย้าย ซ่อน แล้วลบ", title, COLOR_TEXT,
                           FONT_TITLE_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(ui);
    }
    if (status == BENTO_OK) {
        status = add_buttons(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ยังไม่ได้สั่งอะไร", status_line, COLOR_INFO,
                           FONT_TEXT_PX, &screen->status);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ยังไม่ได้นับ", census, COLOR_DIM,
                           FONT_TEXT_PX, &screen->census);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ui.list() ใช้คีย์ id ส่วน poll ใช้ handle",
                           warning, COLOR_WARN, FONT_TEXT_PX, &unused);
    }
    return status;
}

/* Evidence 1: what ui.list() sees. Its key is "id", not "handle":
 * the same value under another word. */
static bento_status_t list_on_console(const census_screen_t *screen)
{
    char text[TEXT_BYTES];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status =
        bento_ui_list(screen->ui, g_rows, MAX_WIDGETS, &count);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "ui.list() นับได้ %" PRIu32 " ตัว", count);
        print_line(text);
        for (i = 0U; i < count; i++) {
            (void)snprintf(text, sizeof(text),
                           "  id=%" PRIu32 " type=%s",
                           bento_ui_id_to_handle(g_rows[i].id),
                           type_name(g_rows[i].widget_type));
            print_line(text);
        }
        status = count_widgets(screen, &count);
    }
    return status;
}

/* Evidence 2: ui.get() refuses a number outside 0-63, and a free one,
 * with ValueError. Tried for real once, rather than believed. */
static bento_status_t try_get(bento_ui_t *ui, int32_t number,
                              const char *found, const char *refused)
{
    bento_ui_widget_info_t info;
    bento_status_t status = bento_ui_get(ui, number, &info);

    if (status == BENTO_OK) {
        print_line(found);
    } else if ((status == BENTO_E_INVALID_ARGUMENT) ||
               (status == BENTO_E_BAD_HANDLE)) {
        print_line(refused);
        status = BENTO_OK;
    } else {
        /* any other failure is not a ValueError: it ends the program */
    }
    return status;
}

/* Evidence 3: .text() with no argument reads the display's text back,
 * since the firmware got GET_TEXT (0x6B) on 16 Aug 2026. */
static void read_back(const census_screen_t *screen)
{
    bento_ui_widget_info_t target;
    char got[TEXT_BYTES];
    char line[READ_LINE_BYTES];
    uint32_t length = 0U;

    got[0] = '\0';
    if ((bento_ui_get(screen->ui, screen->target_number, &target) ==
         BENTO_OK) &&
        (bento_ui_get_text(screen->ui, target.id, got, sizeof(got),
                           &length) != BENTO_OK)) {
        got[0] = '\0';
    }
    (void)snprintf(line, sizeof(line), "target.text() แบบอ่าน คืน %s",
                   got);
    print_line(line);
    print_line("จอบอกได้เองแล้วว่าเขียนอะไรอยู่ ไม่ต้องจำฝั่งเรา");
}

/* Evidence 4: delete() gives the slot back; hide() does not. A
 * throwaway button is counted before and after, then deleted. */
static bento_status_t delete_returns_slot(census_screen_t *screen)
{
    const rect_t place = {MARGIN_X, THROWAWAY_Y, THROWAWAY_W, BUTTON_H};
    char text[TEXT_BYTES];
    bento_ui_id_t throwaway;
    uint32_t before = 0U;
    uint32_t middle = 0U;
    uint32_t after = 0U;
    bento_status_t status = count_widgets(screen, &before);

    if (status == BENTO_OK) {
        status = add_button(screen->ui, "ชั่วคราว", place, COLOR_TEXT,
                            &throwaway);
    }
    if (status == BENTO_OK) {
        status =
            bento_ui_list(screen->ui, g_rows, MAX_WIDGETS, &middle);
    }
    if (status == BENTO_OK) {
        /* .delete() ignores a refusal in modui.c */
        (void)bento_ui_delete(screen->ui, throwaway);
        status = pause_ms(DELETE_SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = count_widgets(screen, &after);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "นับได้ %" PRIu32 " -> %" PRIu32 " -> %" PRIu32,
                       before, middle, after);
        print_line(text);
        print_line("delete() คืนช่องในโควตา 64 ให้จริง");
    }
    return status;
}

static void move_target(census_screen_t *screen, bento_ui_id_t target)
{
    char text[TEXT_BYTES];
    const int16_t y =
        screen->at_a ? (int16_t)PLACE_B_Y : (int16_t)PLACE_A_Y;

    screen->at_a = !screen->at_a;
    (void)bento_ui_set_pos(screen->ui, target, PLACE_A_X, y);
    (void)snprintf(text, sizeof(text), "pos(%d, %d)", PLACE_A_X,
                   (int)y);
    show_text(screen->ui, screen->status, text);
    (void)snprintf(text, sizeof(text), "pos -> %d %d", PLACE_A_X,
                   (int)y);
    print_line(text);
}

static void resize_target(census_screen_t *screen, bento_ui_id_t target)
{
    char text[TEXT_BYTES];
    const int16_t width =
        screen->big ? (int16_t)SIZE_B_W : (int16_t)SIZE_A_W;
    const int16_t height =
        screen->big ? (int16_t)SIZE_B_H : (int16_t)SIZE_A_H;

    screen->big = !screen->big;
    (void)bento_ui_set_size(screen->ui, target, width, height);
    (void)snprintf(text, sizeof(text), "size(%d, %d)", (int)width,
                   (int)height);
    show_text(screen->ui, screen->status, text);
    (void)snprintf(text, sizeof(text), "size -> %d %d", (int)width,
                   (int)height);
    print_line(text);
}

/* A hidden widget is still on the books, so the count does not
 * drop. */
static bento_status_t hide_target(census_screen_t *screen,
                                  bento_ui_id_t target)
{
    char text[TEXT_BYTES];
    uint32_t count = 0U;
    bento_status_t status;

    screen->hidden = !screen->hidden;
    if (screen->hidden) {
        (void)bento_ui_set_visible(screen->ui, target, 0U);
        show_text(screen->ui, screen->status, "hide() - ยังอยู่ในโควตา");
    } else {
        (void)bento_ui_set_visible(screen->ui, target, 1U);
        show_text(screen->ui, screen->status, "show() - กลับมาแล้ว");
    }
    status = bento_ui_list(screen->ui, g_rows, MAX_WIDGETS, &count);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "hidden = %s - ui.list() = %" PRIu32,
                       screen->hidden ? "True" : "False", count);
        print_line(text);
        status = count_widgets(screen, &count);
    }
    return status;
}

/* Every branch first gets the widget back from its number, the way a
 * big program does when its layout comes from a data file. */
static bento_status_t handle_click(census_screen_t *screen,
                                   bento_ui_id_t widget)
{
    bento_ui_widget_info_t target;
    bento_status_t status = BENTO_OK;

    if ((widget == screen->move) || (widget == screen->resize) ||
        (widget == screen->hide)) {
        status =
            bento_ui_get(screen->ui, screen->target_number, &target);
    }
    if (status == BENTO_OK) {
        if (widget == screen->move) {
            move_target(screen, target.id);
        } else if (widget == screen->resize) {
            resize_target(screen, target.id);
        } else if (widget == screen->hide) {
            status = hide_target(screen, target.id);
        } else {
            /* another widget: nothing to do */
        }
    }
    return status;
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static bento_status_t handle_events(census_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    bento_status_t status = BENTO_OK;
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        if (events[i].type == BENTO_UI_EVENT_CLICKED) {
            status = handle_click(screen, events[i].id);
        }
    }
    return status;
}

static bento_status_t run_loop(census_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = handle_events(screen);
        if (status == BENTO_OK) {
            status = pause_ms(LOOP_MS);
        }
    }
    return status;
}

/* ui.clear() deletes everything and gives all 64 slots back; unlike
 * ui.screen() it does not set the drawing area again. */
static bento_status_t finish(const census_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    char text[TEXT_BYTES];
    uint32_t count = 0U;
    bento_status_t status;

    show_text(screen->ui, screen->status, "กำลังจะเรียก ui.clear()");
    /* the poll only lets the screen draw; its events are not used */
    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
    status = pause_ms(BEFORE_CLEAR_MS);
    if (status == BENTO_OK) {
        status = bento_ui_clear(screen->ui);
    }
    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = bento_ui_list(screen->ui, g_rows, MAX_WIDGETS, &count);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "ui.clear() แล้ว - ui.list() = %" PRIu32, count);
        print_line(text);
        print_line("<span class=ok>จอว่าง โควตากลับมาครบ 64 ช่อง</span>");
    }
    return status;
}

/* The evidence on the Console, in the original's order. */
static bento_status_t show_evidence(census_screen_t *screen)
{
    bento_status_t status = list_on_console(screen);

    if (status == BENTO_OK) {
        status = try_get(
            screen->ui, OUT_OF_RANGE_ID,
            "ui.get(99) ผ่านได้ - ผิดจากที่เอกสารบอก",
            "<span class=ok>ui.get(99) โยน ValueError ตามคาด</span>");
    }
    if (status == BENTO_OK) {
        status = try_get(
            screen->ui, FREE_ID, "id 31 มีตัวตนอยู่จริง",
            "<span class=ok>id 31 ยังว่าง - ValueError เช่นกัน</span>");
    }
    if (status == BENTO_OK) {
        read_back(screen);
        status = delete_returns_slot(screen);
    }
    return status;
}

int main(void)
{
    census_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(g_rows, 0, sizeof(g_rows));
    screen.ui = bento_runtime_get_ui();
    screen.at_a = true;
    screen.big = true;
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>จัดการ widget ที่สร้างไปแล้ว</h2>");
        status = show_evidence(&screen);
    }
    if (status == BENTO_OK) {
        print_line("แตะปุ่มซ้ายสามใบ แล้วมองกล่องสีส้ม");
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        status = finish(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
