/*
 * menu_part1.c -- เมนูเลือกตัวอย่าง Part 1 บนจอสัมผัส (Part 1 selector)
 *
 * หน้าเมนูมีปุ่มสิบเอ็ดปุ่ม ปุ่มละตัวอย่างของ Part 1 เรียงสามคอลัมน์ แตะปุ่มแล้ว
 * เมนูพิมพ์ว่ากำลังรันไฟล์ไหน ตรวจว่าไฟล์ตัวอย่างอยู่บน filesystem ของบอร์ดหรือไม่
 * แล้วกลับมาวาดเมนูใหม่พร้อมข้อความผลลัพธ์บรรทัดบน วนไปจนกดหยุด
 *
 * A menu of eleven buttons, one per Part 1 example, in three columns.
 * Tapping a button prints which file would run, checks whether the
 * example file is on the board's filesystem, then draws the menu again
 * with the result in the status line. It repeats until Stop is pressed.
 *
 * Twin of menu_part1.py (embedded-systems-for-aiot-developer/examples/
 * lvgl_ports/part1/menu_part1.py), written new (no library twin is
 * close) in the style of the reference twins. Style: CLEAN_C.md.
 * What C cannot do: the original runs the picked example with
 * exec(open(path).read(), {"MENU_MODE": True}); a C program cannot exec
 * MicroPython source, and gc.collect() has no C counterpart (no garbage
 * collector, contract_map.json "gc.*" missing). So the C menu checks
 * the file with bento_fs_stat() (open() raising OSError gives the same
 * "missing: path" message), and a file that is there is reported as
 * "C cannot exec a .py" in the status line instead of being run. The
 * screen, the buttons, the taps and the print lines are the original's.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fs.h"
#include "bento_runtime.h"

/* Screen layout on the 792 x 398 canvas. */
enum {
    FONT_PX = 14,
    TITLE_FONT_PX = 20,
    TEXT_X = 250,
    TITLE_Y = 10,
    STATUS_Y = 40,
    COLUMNS = 3,
    GRID_X = 20,
    GRID_Y = 70,
    CELL_W = 256,
    CELL_H = 66,
    BUTTON_W = 240,
    BUTTON_H = 56,
    HINT_X = 240,
    HINT_Y = 372
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x16213E,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_STATUS = 0x00D4FF,
    COLOR_BUTTON = 0x1F4068,
    COLOR_HINT = 0x94A3B8
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 300, /* let the cleared screen settle */
    LOOP_MS = 50     /* read the touch events this often */
};

/* The examples, and the status line's text: an example's name, " -> "
 * and the reason, well inside one SET_TEXT. */
enum {
    ITEM_COUNT = 11,
    MESSAGE_CHARS = 96
};

typedef struct {
    const char *name;
    const char *path;
} menu_item_t;

static const menu_item_t ITEMS[ITEM_COUNT] = {
    {"Ex1 Hello World", "/p1ex01.py"},
    {"Ex2 Button Counter", "/p1ex02.py"},
    {"Ex3 LED Widget", "/p1ex03.py"},
    {"Ex4 Switch Control", "/p1ex04.py"},
    {"Ex5 GPIO Dashboard", "/p1ex05.py"},
    {"Ex6 HW LED Control", "/p1ex06.py"},
    {"Ex7 HW Buttons", "/p1ex07.py"},
    {"Ex8 HW ADC Display", "/p1ex08.py"},
    {"Ex9 HW GPIO Dash", "/p1ex09.py"},
    {"Ex10 CAPSENSE Mock", "/p1ex10.py"},
    {"Ex11 CAPSENSE HW", "/p1ex11.py"}};

/* The buttons of the menu on the screen, and the status line's text. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t buttons[ITEM_COUNT];
    char message[MESSAGE_CHARS];
} menu_t;

/* A position or size on the screen: the widget arguments are 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const int16_t place[2], uint32_t color,
                                int32_t font_px)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place[0];
    args.y = place[1];
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, &label);
}

static bento_status_t add_background(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;

    bento_ui_widget_args_init(&args);
    args.x = 0;
    args.y = 0;
    args.w = BENTO_RUNTIME_SCREEN_WIDTH_PX;
    args.h = BENTO_RUNTIME_SCREEN_HEIGHT_PX;
    args.color = COLOR_BACKGROUND;
    /* min= repeats the colour, as the original does */
    args.min_val = COLOR_BACKGROUND;
    args.max_val = 0;
    args.font_px = 0;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* One example's button, in its cell of the three-column grid. */
static bento_status_t add_item_button(menu_t *menu, int32_t item)
{
    const char *text = ITEMS[item].name;
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = px(GRID_X + ((item % COLUMNS) * CELL_W));
    args.y = px(GRID_Y + ((item / COLUMNS) * CELL_H));
    args.w = BUTTON_W;
    args.h = BUTTON_H;
    args.color = COLOR_BUTTON;
    args.font_px = FONT_PX;
    return bento_ui_button_create(menu->ui, &args,
                                  &menu->buttons[item]);
}

/* Clears the screen and draws the menu with the current message. */
static bento_status_t build_menu(menu_t *menu)
{
    const int16_t title_place[2] = {TEXT_X, TITLE_Y};
    const int16_t status_place[2] = {TEXT_X, STATUS_Y};
    const int16_t hint_place[2] = {HINT_X, HINT_Y};
    int32_t item;
    bento_status_t status =
        bento_ui_screen(menu->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_background(menu->ui);
    }
    if (status == BENTO_OK) {
        status = add_label(menu->ui, "LVGL C -> MicroPython : Part 1",
                           title_place, COLOR_TEXT, TITLE_FONT_PX);
    }
    if (status == BENTO_OK) {
        status = add_label(menu->ui, menu->message, status_place,
                           COLOR_STATUS, FONT_PX);
    }
    for (item = 0; (item < ITEM_COUNT) && (status == BENTO_OK);
         item++) {
        status = add_item_button(menu, item);
    }
    if (status == BENTO_OK) {
        status = add_label(menu->ui,
                           "example returns here when its RUN_MS ends",
                           hint_place, COLOR_HINT, FONT_PX);
    }
    return status;
}

/* True when the event is of this kind: the comparison has one type
 * on both sides. */
static bool is_kind(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

/* The example whose button this click came from, or ITEM_COUNT. */
static int32_t item_of_click(const menu_t *menu,
                             const bento_ui_event_t *event)
{
    int32_t found = ITEM_COUNT;
    int32_t item;

    for (item = ITEM_COUNT - 1;
         (item >= 0) && is_kind(event, BENTO_UI_EVENT_CLICKED);
         item--) {
        if (menu->buttons[item] == event->id) {
            found = item;
        }
    }
    return found;
}

/* Waits for the first click on an example's button. */
static bento_status_t wait_for_pick(const menu_t *menu, int32_t *picked)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    *picked = ITEM_COUNT;
    while ((status == BENTO_OK) && (*picked == ITEM_COUNT)) {
        /* ui.poll() answers no events when it fails, so a failed poll
         * is not an error here. */
        if (bento_ui_poll(menu->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                          &count, NULL) != BENTO_OK) {
            count = 0U;
        }
        for (i = 0U; (i < count) && (*picked == ITEM_COUNT); i++) {
            *picked = item_of_click(menu, &events[i]);
        }
        status = bento_time_sleep_ms(LOOP_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

/* What the original's run_example() answers, as far as C can go: the
 * file must be there, and C cannot exec MicroPython source. */
static void run_example(menu_t *menu, int32_t item)
{
    const char *path = ITEMS[item].path;
    bento_fs_stat_t info;
    bool present;

    bento_fs_stat_init(&info);
    present = (bento_fs_stat(path, (uint32_t)strlen(path), &info) ==
               BENTO_OK) &&
              (info.type == BENTO_FS_TYPE_FILE);
    if (present) {
        (void)snprintf(menu->message, sizeof(menu->message),
                       "%s -> C cannot exec a .py", ITEMS[item].name);
    } else {
        (void)snprintf(menu->message, sizeof(menu->message),
                       "%s -> missing: %s", ITEMS[item].name, path);
    }
    (void)printf("menu: %s\n", menu->message);
}

int main(void)
{
    menu_t menu;
    int32_t picked = ITEM_COUNT;
    bento_status_t status = BENTO_OK;

    (void)memset(&menu, 0, sizeof(menu));
    menu.ui = bento_runtime_get_ui();
    (void)snprintf(menu.message, sizeof(menu.message),
                   "Part 1 selector - tap an example");
    while (status == BENTO_OK) {
        status = build_menu(&menu);
        if (status == BENTO_OK) {
            status = wait_for_pick(&menu, &picked);
        }
        if (status == BENTO_OK) {
            (void)printf("menu: running %s\n", ITEMS[picked].path);
            run_example(&menu, picked);
        }
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
