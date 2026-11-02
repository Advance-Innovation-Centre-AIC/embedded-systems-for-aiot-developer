/*
 * menu_sections.c -- หน้าเลือก "ตอน" ของคอร์ส AIoT in Action (Sections menu)
 *
 * หน้าแรกของชุดตัวอย่าง: ปุ่มใหญ่สามปุ่ม ตอนที่ 2 UI-to-Hardware, ตอนที่ 3 Sensor
 * Visualization และเมนู Part 1 แบบเดิม แตะปุ่มแล้วหน้าเมนูพิมพ์ว่ากำลังรันไฟล์ไหน
 * ตรวจว่าไฟล์เมนูของตอนนั้นอยู่บน filesystem ของบอร์ดหรือไม่ แล้ววาดหน้าใหม่
 * พร้อมข้อความผลลัพธ์ วนไปจนกดหยุด
 *
 * The first page of the examples: three large buttons, Section 2
 * UI-to-Hardware, Section 3 Sensor Visualization and the legacy Part 1
 * menu. Tapping one prints which file would run, checks whether that
 * section's menu file is on the board's filesystem, then draws the page
 * again with the result in the status line. It repeats until Stop.
 *
 * Twin of menu_sections.py (embedded-systems-for-aiot-developer/
 * examples/lvgl_ports/boot/menu_sections.py), written new (no library
 * twin is close) in the style of the reference twins. Style:
 * CLEAN_C.md. What C cannot do: the original runs the picked menu with
 * exec(open(path).read()); a C program cannot exec MicroPython source,
 * and gc.collect() has no C counterpart (no garbage collector,
 * contract_map.json "gc.*" missing). So the C page checks the file with
 * bento_fs_stat() (open() raising OSError gives the same "not on this
 * board yet" message), and a file that is there is reported as "C
 * cannot exec a .py" in the status line instead of being run. The
 * original's branch for a section without a path is left out: every
 * section has one, so it never runs. The screen, the buttons, the taps
 * and the print lines are the original's.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fs.h"
#include "bento_runtime.h"

/* Screen layout on the 792 x 398 canvas. */
enum {
    FONT_PX = 16,
    TITLE_FONT_PX = 24,
    TEXT_X = 280,
    TITLE_Y = 24,
    STATUS_Y = 64,
    BUTTON_X = 176,
    BUTTON_Y = 120,
    ROW_H = 76,
    BUTTON_W = 440,
    BUTTON_H = 64
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x16213E,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_STATUS = 0x00D4FF,
    COLOR_BUTTON = 0x1F4068
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 300, /* let the cleared screen settle */
    LOOP_MS = 50     /* read the touch events this often */
};

/* The sections, and the status line's text: a section's name, " -> "
 * and the reason, well inside one SET_TEXT. */
enum {
    ITEM_COUNT = 3,
    MESSAGE_CHARS = 96
};

typedef struct {
    const char *name;
    const char *path;
} menu_item_t;

static const menu_item_t ITEMS[ITEM_COUNT] = {
    {"ตอนที่ 2 - UI-to-Hardware", "/s2menu.py"},
    {"ตอนที่ 3 - Sensor Visualization", "/s3menu.py"},
    {"Part 1 (legacy C-port menu)", "/p1menu.py"}};

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

/* One section's button, in its row. */
static bento_status_t add_item_button(menu_t *menu, int32_t item)
{
    const char *text = ITEMS[item].name;
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = BUTTON_X;
    args.y = px(BUTTON_Y + (item * ROW_H));
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
        status = add_label(menu->ui, "LVGL C -> MicroPython",
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
    return status;
}

/* The section whose button this click came from, else the pick so
 * far: like the original, a later click in the same poll replaces an
 * earlier one. */
static int32_t item_of_click(const menu_t *menu,
                             const bento_ui_event_t *event,
                             int32_t picked)
{
    int32_t found = picked;
    int32_t item;

    for (item = ITEM_COUNT - 1;
         (item >= 0) && (event->type == BENTO_UI_EVENT_CLICKED);
         item--) {
        if (menu->buttons[item] == event->id) {
            found = item;
        }
    }
    return found;
}

/* Waits for a click on a section's button. */
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
        for (i = 0U; i < count; i++) {
            *picked = item_of_click(menu, &events[i], *picked);
        }
        status = bento_time_sleep_ms(LOOP_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

/* What the original's run() answers, as far as C can go: the file
 * must be there, and C cannot exec MicroPython source. */
static void run_section(menu_t *menu, int32_t item)
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
                       "%s -> %s not on this board yet",
                       ITEMS[item].name, path);
    }
}

int main(void)
{
    menu_t menu;
    int32_t picked = ITEM_COUNT;
    bento_status_t status = BENTO_OK;

    (void)memset(&menu, 0, sizeof(menu));
    menu.ui = bento_runtime_get_ui();
    (void)snprintf(menu.message, sizeof(menu.message),
                   "เลือกตอนที่จะเรียน");
    while (status == BENTO_OK) {
        status = build_menu(&menu);
        if (status == BENTO_OK) {
            status = wait_for_pick(&menu, &picked);
        }
        if (status == BENTO_OK) {
            (void)printf("sections: running %s\n", ITEMS[picked].path);
            run_section(&menu, picked);
        }
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
