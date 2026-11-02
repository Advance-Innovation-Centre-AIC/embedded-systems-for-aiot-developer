/*
 * menu.c -- ui.Menu ตัวเดียว (one Menu with child pages)
 *
 * เมนูหน้าแรก "ตั้งค่าเครื่อง" มีสองแถว แตะแถวแล้วเข้าหน้าลูก (เครือข่าย หรือ
 * เกี่ยวกับเครื่อง) กดลูกศรที่หัวเพื่อกลับ แถวที่ขอ event ไว้รายงานกลับมาที่คอนโซล
 * หน้าเกี่ยวกับเครื่องแสดงชื่อบอร์ด รันจบเองใน 20 วินาที
 *
 * A menu whose first page "settings" has two rows: tapping a row opens
 * its child page (network, or about the device), and the arrow in the
 * header goes back. The two rows ask for pressed events, so each tap
 * is printed on the console. The about page shows the board's name.
 * The example ends by itself after 20 seconds.
 *
 * Twin of menu.py
 * (embedded-systems-for-aiot-developer/examples/widgets/menu.py),
 * written in the style of the library twin aiot-widget-menu.
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - ROW_NAME (a dict from a row's handle to its name) becomes a test
 *    of the event's id against the two row ids;
 *  - "รุ่น " + name is built in a fixed buffer with snprintf;
 *  - Stop during a sleep raises KeyboardInterrupt in MicroPython; here
 *    the sleep returns and the program ends at that point.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_runtime.h"

/* Timing: 100 rounds of 200 ms (20 s). */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    ROUND_MS = 200,
    ROUNDS = 100
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22
};

/* Layout on the 792 x 398 canvas: the menu on the left, the notes on
 * the right. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    TITLE_PX = 28,
    MENU_Y = 72,
    MENU_W = 440,
    MENU_H = 248,
    MENU_MODE = 1, /* value=1 as in the original */
    NOTE_X = 496,
    NOTE_Y = 112,
    NOTE_PX = 24,
    HINT_Y = 160,
    FOOT_Y = 344,
    HINT_PX = 20,
    /* "รุ่น " and a board name: the display takes at most 126 bytes */
    MODEL_CHARS = 128
};

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

/* The menu and its pages. */
typedef struct {
    bento_ui_id_t menu;
    bento_ui_id_t root;
    bento_ui_id_t network;
    bento_ui_id_t about;
} menu_pages_t;

/* The two rows whose taps the program reports. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t row_network;
    bento_ui_id_t row_about;
} menu_screen_t;

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

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, &unused);
}

static bento_status_t add_page(bento_ui_t *ui, bento_ui_id_t menu,
                               const char *title, bento_ui_id_t *page)
{
    return bento_ui_add_page(ui, menu, title, (uint32_t)strlen(title),
                             page);
}

static bento_status_t add_row(bento_ui_t *ui, bento_ui_id_t section,
                              const char *text, bento_ui_id_t *row)
{
    return bento_ui_add_menu_row(ui, section, text,
                                 (uint32_t)strlen(text), row);
}

/* The menu and its three pages; the first page added is the one the
 * menu opens on. */
static bento_status_t add_menu(bento_ui_t *ui, menu_pages_t *pages)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = MENU_Y;
    args.w = MENU_W;
    args.h = MENU_H;
    args.color = COLOR_CARD;
    args.font_px = MENU_MODE;
    status = bento_ui_menu_create(ui, &args, &pages->menu);
    if (status == BENTO_OK) {
        status = add_page(ui, pages->menu, "ตั้งค่าเครื่อง", &pages->root);
    }
    if (status == BENTO_OK) {
        status = add_page(ui, pages->menu, "เครือข่าย", &pages->network);
    }
    if (status == BENTO_OK) {
        status =
            add_page(ui, pages->menu, "เกี่ยวกับเครื่อง", &pages->about);
    }
    return status;
}

/* The first page's two rows with a separator between them. Each row
 * opens its page and asks for pressed events: a row has its own handle,
 * so it listens like any widget. .opens() and .listen() are setters,
 * and modui.c ignores their refusal. */
static bento_status_t add_root_rows(menu_screen_t *screen,
                                    const menu_pages_t *pages)
{
    const uint32_t pressed =
        BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_PRESSED);
    bento_ui_id_t section = 0U;
    bento_ui_id_t separator = 0U;
    bento_status_t status =
        bento_ui_add_section(screen->ui, pages->root, &section);

    if (status == BENTO_OK) {
        status = add_row(screen->ui, section, "เครือข่าย",
                         &screen->row_network);
    }
    if (status == BENTO_OK) {
        status =
            bento_ui_add_separator(screen->ui, section, &separator);
    }
    if (status == BENTO_OK) {
        status = add_row(screen->ui, section, "เกี่ยวกับเครื่อง",
                         &screen->row_about);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_opens_page(screen->ui, screen->row_network,
                                  pages->network);
        (void)bento_ui_opens_page(screen->ui, screen->row_about,
                                  pages->about);
        (void)bento_ui_listen_compat(screen->ui, screen->row_network,
                                     pressed);
        (void)bento_ui_listen_compat(screen->ui, screen->row_about,
                                     pressed);
    }
    return status;
}

/* The network page's two rows. */
static bento_status_t add_network_rows(bento_ui_t *ui,
                                       bento_ui_id_t page)
{
    bento_ui_id_t section = 0U;
    bento_ui_id_t row = 0U;
    bento_status_t status = bento_ui_add_section(ui, page, &section);

    if (status == BENTO_OK) {
        status = add_row(ui, section, "วง AIoT-Class", &row);
    }
    if (status == BENTO_OK) {
        status = add_row(ui, section, "ต่ออัตโนมัติ", &row);
    }
    return status;
}

/* The about page's one row: "รุ่น " and the board's name. */
static bento_status_t add_about_row(bento_ui_t *ui, bento_ui_id_t page)
{
    bento_gpio_board_info_t info;
    char model[MODEL_CHARS];
    bento_ui_id_t section = 0U;
    bento_ui_id_t row = 0U;
    bento_status_t status = bento_ui_add_section(ui, page, &section);

    bento_gpio_board_info_init(&info);
    if (status == BENTO_OK) {
        status = bento_gpio_board_info(&info);
    }
    if (status == BENTO_OK) {
        (void)snprintf(model, sizeof(model), "รุ่น %s", info.name);
        status = add_row(ui, section, model, &row);
    }
    return status;
}

static bento_status_t add_notes(bento_ui_t *ui)
{
    bento_status_t status =
        add_label(ui, "แตะแถวเข้าหน้าลูก",
                  (label_look_t){NOTE_X, NOTE_Y, COLOR_TEXT, NOTE_PX});

    if (status == BENTO_OK) {
        status = add_label(
            ui, "กดลูกศรที่หัวเพื่อกลับ",
            (label_look_t){NOTE_X, HINT_Y, COLOR_DIM, HINT_PX});
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "แถวที่แตะจะรายงานกลับมา",
            (label_look_t){MARGIN_X, FOOT_Y, COLOR_DIM, HINT_PX});
    }
    return status;
}

static bento_status_t build_screen(menu_screen_t *screen)
{
    menu_pages_t pages = {0U, 0U, 0U, 0U};
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            screen->ui, "ui.Menu",
            (label_look_t){MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX});
    }
    if (status == BENTO_OK) {
        status = add_menu(screen->ui, &pages);
    }
    if (status == BENTO_OK) {
        status = add_root_rows(screen, &pages);
    }
    if (status == BENTO_OK) {
        status = add_network_rows(screen->ui, pages.network);
    }
    if (status == BENTO_OK) {
        status = add_about_row(screen->ui, pages.about);
    }
    if (status == BENTO_OK) {
        status = add_notes(screen->ui);
    }
    return status;
}

/* A pressed event from one of the two rows is counted and printed with
 * the row's name. */
static int32_t print_taps(const menu_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    int32_t taps = 0;

    /* ui.poll() answers no events when it fails. */
    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        const bool pressed =
            events[i].type == (uint8_t)BENTO_UI_EVENT_PRESSED;
        const bool on_about = events[i].id == screen->row_about;

        if (pressed &&
            (on_about || (events[i].id == screen->row_network))) {
            taps++;
            (void)printf("ui.Menu: แตะแถว %s\n",
                         on_about ? "เกี่ยวกับเครื่อง" : "เครือข่าย");
        }
    }
    return taps;
}

/* ROUNDS rounds: read the taps, then sleep ROUND_MS (20 s in all). */
static bento_status_t run_rounds(const menu_screen_t *screen,
                                 int32_t *taps)
{
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        *taps += print_taps(screen);
        status = pause_ms(ROUND_MS);
    }
    return status;
}

int main(void)
{
    menu_screen_t screen = {bento_runtime_get_ui(), 0U, 0U};
    int32_t taps = 0;
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        status = run_rounds(&screen, &taps);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.Menu: การแตะแถวที่จับได้ = %ld\n", (long)taps);
        (void)printf(".add_page() -> .section() -> .row() -> .opens() "
                     "-> .listen()\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
