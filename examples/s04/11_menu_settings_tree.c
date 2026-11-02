/*
 * 11_menu_settings_tree.c -- หน้าตั้งค่าแบบเป็นชั้น ด้วย ui.Menu
 *
 * ui.Menu พาคนเดินดูหน้าตั้งค่าทีละชั้น หน้าราก "ตั้งค่าเครื่อง" มีสามแถว
 * (เครือข่าย การแจ้งเตือน เกี่ยวกับเครื่อง) แต่ละแถวเปิดหน้าลูกที่บอกค่าตั้ง
 * การ์ดขวาโชว์ค่าเดียวกันให้เห็นได้โดยไม่ต้องเดินเข้าไป แถวขอ pressed ไว้
 * โปรแกรมจึงรู้ว่าคนแตะแถวไหน ตัวอย่างเดิน 30 วินาที แล้วสรุปจำนวน event
 *
 * ui.Menu lets a person walk a settings tree one level at a time. The
 * root page "ตั้งค่าเครื่อง" has three rows (network, alarm, about),
 * and each row opens a child page that shows the settings; the about
 * page names the board from gpio.board_info(). A card on the right
 * shows the same values at a glance. The rows listen for "pressed",
 * so the program knows which row was touched. It runs for 30 seconds,
 * then reports how many events came from the rows.
 *
 * Twin of 11_menu_settings_tree.py
 * (embedded-systems-for-aiot-developer/examples/s04/11_menu_settings_tree.py),
 * rewritten from the library twin aiot-s04-11-menu-settings-tree.
 * Style: CLEAN_C.md. The dict "setting" becomes named constants; the
 * row texts that carry its values are built with snprintf. The course
 * file runs one poll and one 200 ms sleep per round.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_runtime.h"

/* The machine's settings live here, not in the menu: the menu is only
 * a window onto them. */
enum {
    ALARM_AT_DEGREES = 60,
    SEND_EVERY_S = 5
};
static const char SSID[] = "AIoT-Class";

/* Timing. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    ROUND_MS = 200,
    ROUNDS = 150 /* 30 seconds */
};

enum {
    AUTO = -1, /* no w= or h= given: the widget's own size */
    PAGE_COUNT = 4,
    ROOT_ROW_COUNT = 3,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF
};

/* The menu's pages: the first one added is the one the menu opens, so
 * it is the root. */
enum {
    PAGE_ROOT,
    PAGE_NETWORK,
    PAGE_ALARM,
    PAGE_ABOUT
};

/* The widgets around the menu, in the order they are made. */
enum {
    W_CARD,
    W_CARD_CAPTION,
    W_CARD_SSID,
    W_CARD_ALARM,
    W_CARD_SEND,
    W_CARD_HINT,
    W_LINE,
    W_COUNT
};

static const char *const PAGE_TITLES[PAGE_COUNT] = {
    "ตั้งค่าเครื่อง", "เครือข่าย", "การแจ้งเตือน", "เกี่ยวกับเครื่อง"};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t menu;
    bento_ui_id_t pages[PAGE_COUNT];
    bento_ui_id_t rows[ROOT_ROW_COUNT]; /* the root rows, page order */
    bento_ui_id_t ids[W_COUNT];
    int32_t events;
} menu_screen_t;

/* One widget as the original makes it: its factory, its text and its
 * keyword arguments. value= is a Label's or Button's font size and a
 * control's start value; min= and max= are a control's range (a
 * Panel's border colour and corner radius). */
typedef struct {
    uint8_t type;
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value;
    int32_t min;
    int32_t max;
} widget_spec_t;

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

static bento_status_t add_widget(bento_ui_t *ui,
                                 const widget_spec_t *spec,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    if (spec->text != NULL) {
        args.text = spec->text;
        args.text_len = (uint32_t)strlen(spec->text);
    }
    args.x = spec->x;
    args.y = spec->y;
    args.w = spec->w;
    args.h = spec->h;
    args.color = spec->color;
    args.font_px = spec->value;
    args.min_val = spec->min;
    args.max_val = spec->max;
    return bento_ui_create(ui, spec->type, &args, out_id);
}

/* Makes the widgets in table order (creation order is drawing order)
 * and keeps each id; the first refusal ends it, as a raise does in
 * MicroPython. */
static bento_status_t add_widgets(bento_ui_t *ui,
                                  const widget_spec_t *table,
                                  uint32_t count, bento_ui_id_t *ids)
{
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = add_widget(ui, &table[i], &ids[i]);
    }
    return status;
}

/* The card on the right: the same values the menu shows, seen without
 * walking in. A good settings screen answers both ways. The status
 * line is short: a create text has a ceiling and is cut silently. */
static const widget_spec_t SIDE_WIDGETS[W_COUNT] = {
    {UI_WIDGET_PANEL, NULL, 496, 64, 272, 272, COLOR_CARD, 0, 0, 8},
    {UI_WIDGET_LABEL, "ค่าที่ใช้อยู่ตอนนี้", 520, 88, AUTO, AUTO, COLOR_DIM, 20,
     0, 0},
    {UI_WIDGET_LABEL, SSID, 520, 136, AUTO, AUTO, COLOR_TEXT, 24, 0, 0},
    {UI_WIDGET_LABEL, "เตือนที่ 60 องศา", 520, 184, AUTO, AUTO, COLOR_TEXT,
     24, 0, 0},
    {UI_WIDGET_LABEL, "ส่งทุก 5 วินาที", 520, 232, AUTO, AUTO, COLOR_TEXT,
     24, 0, 0},
    {UI_WIDGET_LABEL, "แตะเมนูซ้ายดูเพิ่ม", 520, 280, AUTO, AUTO, COLOR_DIM,
     20, 0, 0},
    {UI_WIDGET_LABEL, "เมนูพาคนเดินดู แต่ไม่บอกโปรแกรม", 24, 344, AUTO, AUTO,
     COLOR_DIM, 20, 0, 0}};

/* The name MicroPython's ui.poll() puts in ev["type"] (modui.c). */
static const char *type_text(uint8_t type)
{
    static const char *const names[] = {
        "unknown",    "clicked",      "value_changed",
        "toggled",    "pressed",      "released",
        "press_lost", "long_pressed", "long_pressed_repeat",
        "ready",      "cancel",       "focused",
        "defocused",  "scroll_begin", "scroll_end",
        "gesture"};
    const char *name = names[0];

    if ((size_t)type < (sizeof(names) / sizeof(names[0]))) {
        name = names[type];
    }
    return name;
}

static bento_status_t add_row(bento_ui_t *ui, bento_ui_id_t section,
                              const char *text, bento_ui_id_t *out_id)
{
    return bento_ui_add_menu_row(ui, section, text,
                                 (uint32_t)strlen(text), out_id);
}

/* value=1 turns the root page's back button on, so the header always
 * has its arrow and nobody has to guess how to leave. */
static bento_status_t add_menu(menu_screen_t *screen)
{
    static const widget_spec_t menu = {
        UI_WIDGET_MENU, NULL, 24, 64, 440, 272, COLOR_CARD, 1, 0, 0};
    bento_status_t status =
        add_widget(screen->ui, &menu, &screen->menu);
    uint32_t i;

    for (i = 0U; (i < (uint32_t)PAGE_COUNT) && (status == BENTO_OK);
         i++) {
        status = bento_ui_add_page(
            screen->ui, screen->menu, PAGE_TITLES[i],
            (uint32_t)strlen(PAGE_TITLES[i]), &screen->pages[i]);
    }
    return status;
}

/* The root page: one section of three rows. A section groups rows; it
 * is not a page. Without the opens() links the rows could be touched
 * but would go nowhere; without listen() the program would not hear
 * them, since a widget is silent by default. */
static bento_status_t add_root_rows(menu_screen_t *screen)
{
    bento_ui_id_t section;
    bento_status_t status = bento_ui_add_section(
        screen->ui, screen->pages[PAGE_ROOT], &section);
    uint32_t i;

    for (i = 0U; (i < (uint32_t)ROOT_ROW_COUNT) && (status == BENTO_OK);
         i++) {
        status = add_row(screen->ui, section, PAGE_TITLES[i + 1U],
                         &screen->rows[i]);
    }
    for (i = 0U; (i < (uint32_t)ROOT_ROW_COUNT) && (status == BENTO_OK);
         i++) {
        /* .listen() and .opens() never raise in MicroPython */
        (void)bento_ui_listen_compat(
            screen->ui, screen->rows[i],
            BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_PRESSED));
    }
    for (i = 0U; (i < (uint32_t)ROOT_ROW_COUNT) && (status == BENTO_OK);
         i++) {
        (void)bento_ui_opens_page(screen->ui, screen->rows[i],
                                  screen->pages[i + 1U]);
    }
    return status;
}

/* Two rows in a new section of a child page. */
static bento_status_t add_two_rows(bento_ui_t *ui, bento_ui_id_t page,
                                   const char *first,
                                   const char *second)
{
    bento_ui_id_t section;
    bento_ui_id_t unused;
    bento_status_t status = bento_ui_add_section(ui, page, &section);

    if (status == BENTO_OK) {
        status = add_row(ui, section, first, &unused);
    }
    if (status == BENTO_OK) {
        status = add_row(ui, section, second, &unused);
    }
    return status;
}

/* The child pages: a row's text carries the value, as there is no
 * other way to put a value into the menu. The model name comes from
 * the firmware, so one file names both the Eva Kit and the Dev Kit. */
static bento_status_t add_child_rows(const menu_screen_t *screen)
{
    char first[TEXT_BYTES];
    char second[TEXT_BYTES];
    bento_gpio_board_info_t info;
    bento_status_t status;

    (void)snprintf(first, sizeof(first), "วง %s", SSID);
    status = add_two_rows(screen->ui, screen->pages[PAGE_NETWORK],
                          first, "ต่ออัตโนมัติเมื่อเปิดเครื่อง");
    if (status == BENTO_OK) {
        (void)snprintf(first, sizeof(first), "เตือนเมื่อเกิน %d องศา",
                       ALARM_AT_DEGREES);
        (void)snprintf(second, sizeof(second), "ส่งซ้ำทุก %d วินาที",
                       SEND_EVERY_S);
        status = add_two_rows(screen->ui, screen->pages[PAGE_ALARM],
                              first, second);
    }
    if (status == BENTO_OK) {
        bento_gpio_board_info_init(&info);
        status = bento_gpio_board_info(&info);
    }
    if (status == BENTO_OK) {
        (void)snprintf(first, sizeof(first), "รุ่น %s",
                       (info.name != NULL) ? info.name : "");
        status = add_two_rows(screen->ui, screen->pages[PAGE_ABOUT],
                              first, "เฟิร์มแวร์ BENTO");
    }
    return status;
}

static bento_status_t build_screen(menu_screen_t *screen)
{
    static const widget_spec_t title = {UI_WIDGET_LABEL,
                                        "หน้าตั้งค่าแบบเป็นชั้น",
                                        24,
                                        16,
                                        AUTO,
                                        AUTO,
                                        COLOR_TEXT,
                                        28,
                                        0,
                                        0};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_widget(screen->ui, &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_menu(screen);
    }
    if (status == BENTO_OK) {
        status = add_root_rows(screen);
    }
    if (status == BENTO_OK) {
        status = add_child_rows(screen);
    }
    if (status == BENTO_OK) {
        status =
            add_widgets(screen->ui, SIDE_WIDGETS, W_COUNT, screen->ids);
    }
    return status;
}

/* The row an event came from, or the menu itself for anything else. */
static const char *row_name(const menu_screen_t *screen,
                            bento_ui_id_t widget)
{
    const char *name = "ตัวเมนูเอง";
    uint32_t i;

    for (i = 0U; i < (uint32_t)ROOT_ROW_COUNT; i++) {
        if (screen->rows[i] == widget) {
            name = PAGE_TITLES[i + 1U];
        }
    }
    return name;
}

/* Every event is counted: the menu itself sends nothing, the rows send
 * pressed once they have asked for it. */
static void handle_events(menu_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    char text[TEXT_BYTES];
    uint32_t count = 0U;
    uint32_t i;

    /* a failed poll answers no events, so it is not an error here */
    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        const char *who = row_name(screen, events[i].id);

        screen->events++;
        (void)snprintf(text, sizeof(text), "แตะแถว %s", who);
        show_text(screen->ui, screen->ids[W_LINE], text);
        (void)snprintf(text, sizeof(text), "event: %s จาก %s",
                       type_text(events[i].type), who);
        /* lcd.print() ignores a console that is not ready */
        (void)bento_lcd_print(text);
    }
}

static bento_status_t run_loop(menu_screen_t *screen)
{
    bento_status_t status = BENTO_OK;
    int32_t round;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        handle_events(screen);
        status = pause_ms(ROUND_MS);
    }
    return status;
}

int main(void)
{
    menu_screen_t screen;
    char text[TEXT_BYTES];
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>Menu - หน้าตั้งค่าแบบเป็นชั้น</h2>");
        (void)bento_lcd_print(
            "Menu 1 + หน้า 4 + section 4 + แถว 9 = 18 แฮนเดิล");
        (void)bento_lcd_print("จากเพดาน 64 - โครงลึกกินโควตาเร็วกว่าที่คิด");
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>event จากแถวเมนู = %" PRId32
                       " </span>",
                       screen.events);
        (void)bento_lcd_print(text);
        (void)bento_lcd_print("แตะแล้วยังศูนย์ = .listen() ไปไม่ถึงคอร์จอ");
        (void)printf("Menu: .add_page() -> .section() -> .row() -> "
                     ".opens()\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
