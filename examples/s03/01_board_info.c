/*
 * 01_board_info.c -- บอร์ดตัวนี้มีอะไรบ้าง (What this board has)
 *
 * ถามบอร์ดก่อนว่ามีอะไรให้เล่นบ้าง แล้วเขียนโปรแกรมจากค่าที่ถามมา ไม่ใช่จาก
 * ตัวเลขที่จำไว้ จอแสดงชื่อบอร์ดกับจำนวนหลอด แล้วรายชื่อหลอดเรียงตามดัชนี
 * ลิ้นชัก Console เก็บรายการเดียวกันแบบเรียงบรรทัด แล้วดับหลอดครบทุกดวง
 * ด้วย range(n) กับดัก: ปุ่มผู้ใช้มีตัวเดียวคือ gpio.button(0) และชื่อที่
 * .name() คืนไม่ใช่ตัวพิมพ์บนแผ่นวงจร
 *
 * Ask the board what it has, and write the program from the answer,
 * never from a number you remember. The screen shows the board's name
 * and LED count, then the LED names by index; the Console drawer keeps
 * the same list line by line, and every LED goes off through
 * range(n). Trap: the one user button is gpio.button(0), and the name
 * .name() gives is not what the circuit board has printed on it.
 *
 * Twin of 01_board_info.py
 * (embedded-systems-for-aiot-developer/examples/s03/01_board_info.py).
 * Style: CLEAN_C.md.
 * Where C differs: C has no dict. The LED and button names are asked
 * per index, and print(info) is written out in MicroPython's dict
 * form, the keys in the order its hash table walks them (dict_order(),
 * the py/map.c rules with the board's qstr hash width). The
 * gpio.button(1) line the original keeps commented out is left out.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* Layout on the 792 x 398 canvas. The names get a line of their own
 * under the number: five LED names after x=112 would run off the
 * screen (measured in the emulator). */
enum {
    LEFT_X = 20,
    FONT_PX = 20,
    BIG_FONT_PX = 24,
    TITLE_Y = 12,
    NAME_Y = 60,
    SEG_Y = 100,
    SEG_W = 72,
    SEG_H = 48,
    ROWS_Y = 156, /* the Seg7 ends at 148 */
    TRAP_Y = 192,
    DONE_Y = 232
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_INFO = 0x4A9EFF
};

/* board_info()'s keys in the order the binding stores them, and the
 * size of the dict it asks for: modgpio.c mp_obj_new_dict(3) on the
 * PSoC Edge, rpi_gpio.c a dict of 5 on the reTerminal. */
enum {
    INFO_NAME,
    INFO_LEDS,
    INFO_BUTTONS,
    INFO_LED_NAMES,
    INFO_BUTTON_NAMES,
    INFO_KEY_COUNT
};

#if defined(BENTO_NATIVE_H)
enum {
    INFO_DICT_SIZE = 3
};
#else
enum {
    INFO_DICT_SIZE = 5
};
#endif

static const char *const INFO_KEYS[INFO_KEY_COUNT] = {
    "name", "leds", "buttons", "led_names", "btn_names"};

/* MicroPython prints a dict in the order of its hash table (py/map.c),
 * not in the order the binding stored the keys. dict_order() works
 * that order out by the same rules: the djb2 hash of each key (a
 * qstr), a table of the size the binding asked for, linear probing,
 * and a grow to the next table size when a probe comes back to where
 * it started. */
enum {
    DICT_KEYS_MAX = 8,
    DICT_SLOTS_MAX = 17, /* the next size after 12; never reached */
    HASH_SEED = 5381,
    HASH_FACTOR = 33,
    EVEN_SIZES_TO = 12, /* table sizes 2, 4 ... 12, then 17 */
    SIZE_AFTER_EVEN = 17,
    NO_KEY = -1
};

#if defined(BENTO_NATIVE_H)
enum {
    QSTR_HASH_MASK = 0xFF
}; /* PSoC Edge: 1-byte qstr hashes */
#else
enum {
    QSTR_HASH_MASK = 0xFFFF
}; /* reTerminal: 2-byte hashes */
#endif

typedef struct {
    uint32_t alloc;
    int32_t slots[DICT_SLOTS_MAX]; /* a key's index, or NO_KEY */
} hash_table_t;

static uint32_t qstr_hash(const char *key)
{
    uint32_t hash = HASH_SEED;
    size_t i;

    for (i = 0U; key[i] != '\0'; i++) {
        hash =
            (hash * (uint32_t)HASH_FACTOR) ^ (uint32_t)(uint8_t)key[i];
    }
    hash &= (uint32_t)QSTR_HASH_MASK;
    return (hash == 0U) ? 1U : hash; /* a qstr hash is never 0 */
}

static uint32_t grown_size(uint32_t alloc)
{
    const uint32_t wanted = alloc + 1U;
    uint32_t size = SIZE_AFTER_EVEN;

    if (wanted <= (uint32_t)EVEN_SIZES_TO) {
        size = wanted + (wanted % 2U); /* the next even size */
    }
    return size;
}

/* Linear probing from hash % alloc; false when every slot is taken. */
static bool table_place(hash_table_t *table, int32_t key, uint32_t hash)
{
    const uint32_t start = hash % table->alloc;
    uint32_t slot = start;
    bool placed = false;
    bool full = false;

    while (!placed && !full) {
        if (table->slots[slot] == NO_KEY) {
            table->slots[slot] = key;
            placed = true;
        } else {
            slot = (slot + 1U) % table->alloc;
            full = (slot == start);
        }
    }
    return placed;
}

/* The bigger table gets the old keys again, slot by slot. */
static void table_grow(hash_table_t *table, const uint32_t hashes[])
{
    int32_t old[DICT_SLOTS_MAX];
    const uint32_t old_alloc = table->alloc;
    uint32_t i;

    (void)memcpy(old, table->slots, sizeof(old));
    table->alloc = grown_size(old_alloc);
    for (i = 0U; i < (uint32_t)DICT_SLOTS_MAX; i++) {
        table->slots[i] = NO_KEY;
    }
    for (i = 0U; i < old_alloc; i++) {
        if (old[i] != NO_KEY) {
            (void)table_place(table, old[i], hashes[old[i]]);
        }
    }
}

/* order[] gets the indices of keys[] in the order print walks them.
 * count is at most DICT_KEYS_MAX. */
static void dict_order(const char *const keys[], uint32_t count,
                       uint32_t alloc, uint32_t order[])
{
    hash_table_t table;
    uint32_t hashes[DICT_KEYS_MAX];
    uint32_t next = 0U;
    uint32_t i;

    table.alloc = alloc;
    for (i = 0U; i < (uint32_t)DICT_SLOTS_MAX; i++) {
        table.slots[i] = NO_KEY;
    }
    for (i = 0U; i < count; i++) {
        hashes[i] = qstr_hash(keys[i]);
        if (table.alloc == 0U) {
            table_grow(&table, hashes);
        }
        if (!table_place(&table, (int32_t)i, hashes[i])) {
            table_grow(&table, hashes);
            (void)table_place(&table, (int32_t)i, hashes[i]);
        }
    }
    for (i = 0U; i < table.alloc; i++) {
        if (table.slots[i] != NO_KEY) {
            order[next] = (uint32_t)table.slots[i];
            next++;
        }
    }
}

static void append_text(char *buffer, size_t size, const char *piece)
{
    const size_t used = strlen(buffer);

    if (used < size) {
        (void)snprintf(&buffer[used], size - used, "%s", piece);
    }
}
static bento_status_t name_of(bool is_led, uint32_t index,
                              const char **name)
{
    return is_led ? bento_gpio_led_name(index, name)
                  : bento_gpio_button_name(index, name);
}

typedef struct {
    int16_t y;
    uint32_t color;
    int32_t font_px;
} text_style_t;

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* A setter's refusal is ignored, as in MicroPython (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const text_style_t *style,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = LEFT_X;
    args.y = style->y;
    args.color = style->color;
    args.font_px = style->font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A Seg7 takes text, not a number: .value(3) leaves it blank without
 * a word. */
static bento_status_t add_count(bento_ui_t *ui, uint32_t leds)
{
    char text[TEXT_BYTES];
    bento_ui_widget_args_t args;
    bento_ui_id_t seg;

    (void)snprintf(text, sizeof(text), "%" PRIu32, leds);
    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = LEFT_X;
    args.y = SEG_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = COLOR_OK;
    return bento_ui_seg7_create(ui, &args, &seg);
}

/* The title, the board's name, the LED count and the row the names
 * go to. */
static bento_status_t build_header(bento_ui_t *ui,
                                   const bento_gpio_board_info_t *info,
                                   bento_ui_id_t *rows)
{
    const text_style_t title = {TITLE_Y, COLOR_TEXT, BIG_FONT_PX};
    const text_style_t name = {NAME_Y, COLOR_INFO, BIG_FONT_PX};
    const text_style_t row = {ROWS_Y, COLOR_TEXT, FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "บอร์ดตัวนี้มีอะไรบ้าง", &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, info->name, &name, &unused);
    }
    if (status == BENTO_OK) {
        status = add_count(ui, info->leds);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "กำลังอ่านรายชื่อ", &row, rows);
    }
    return status;
}

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void print_counts(const bento_gpio_board_info_t *info)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "ชื่อ: %s", info->name);
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "ไฟ %" PRIu32 " ดวง | ปุ่ม %" PRIu32 " ปุ่ม", info->leds,
                   info->buttons);
    (void)bento_lcd_print(text);
}

/* The drawer takes every name on its own line; the 4.3-inch screen
 * gets them in one row. */
static bento_status_t list_leds(bento_ui_t *ui, uint32_t count,
                                bento_ui_id_t rows)
{
    char row[TEXT_BYTES];
    char piece[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    uint32_t i;

    (void)bento_lcd_console("<span class=muted>--- หลอดไฟ ---</span>");
    (void)snprintf(row, sizeof(row), "gpio.led(i) -> ");
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        const char *name = NULL;

        status = bento_gpio_led_name(i, &name);
        if (status == BENTO_OK) {
            (void)snprintf(piece, sizeof(piece),
                           "  gpio.led(%" PRIu32 ") = %s", i, name);
            (void)bento_lcd_print(piece);
            (void)snprintf(piece, sizeof(piece), "%" PRIu32 "=%s  ", i,
                           name);
            append_text(row, sizeof(row), piece);
        }
    }
    if (status == BENTO_OK) {
        set_text(ui, rows, row);
    }
    return status;
}

static bento_status_t list_buttons(uint32_t count)
{
    char piece[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    uint32_t i;

    (void)bento_lcd_console("<span class=muted>--- ปุ่ม ---</span>");
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        const char *name = NULL;

        status = bento_gpio_button_name(i, &name);
        if (status == BENTO_OK) {
            (void)snprintf(piece, sizeof(piece),
                           "  gpio.button(%" PRIu32 ") = %s", i, name);
            (void)bento_lcd_print(piece);
        }
    }
    return status;
}

/* The trap goes on the screen: the person looking at the board and
 * the one reading the code call the same button by two names. The
 * full text goes through set_text(), which carries more than a
 * create does (122 bytes with the name, under the 126 of SET_TEXT).
 * btn_names[0] on a board without a button is an IndexError. */
static bento_status_t show_trap(bento_ui_t *ui,
                                const bento_gpio_board_info_t *info)
{
    const text_style_t trap_style = {TRAP_Y, COLOR_WARN, FONT_PX};
    char text[TEXT_BYTES];
    const char *name = NULL;
    bento_ui_id_t trap = 0U;
    bento_status_t status =
        add_label(ui, "ปุ่มผู้ใช้ตัวเดียวของบอร์ด", &trap_style, &trap);

    if (status == BENTO_OK) {
        status = (info->buttons > 0U)
                     ? bento_gpio_button_name(0U, &name)
                     : BENTO_E_INVALID_ARGUMENT;
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "โค้ดเรียกว่า %s ดัชนี 0 - ไม่ใช่ป้ายบนบอร์ด", name);
        set_text(ui, trap, text);
    }
    return status;
}

/* range(n), not range(3): the course's two boards have different LED
 * counts, and this runs all of them on both without a change. */
static bento_status_t leds_off(bento_ui_t *ui)
{
    const text_style_t done = {DONE_Y, COLOR_OK, FONT_PX};
    const uint32_t count = bento_gpio_num_leds();
    char text[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_status_t status = BENTO_OK;
    uint32_t i;

    (void)snprintf(text, sizeof(text),
                   "<span class=muted>num_leds() = %" PRIu32 " </span>",
                   count);
    (void)bento_lcd_print(text);
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = bento_gpio_led_off(i);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "range(n) ดับครบ %" PRIu32 " ดวง ไม่ต้องรู้เลขก่อน",
                       count);
        status = add_label(ui, text, &done, &unused);
    }
    if (status == BENTO_OK) {
        (void)snprintf(
            text, sizeof(text),
            "<span class=ok>ดับไฟครบ %" PRIu32 " ดวงแล้ว</span>", count);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* "['A', 'B']": the LED names or the button names, as print shows a
 * list of str. */
static bento_status_t print_names(bool is_led, uint32_t count)
{
    uint32_t i;
    bento_status_t status = BENTO_OK;

    (void)printf("[");
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        const char *name = NULL;

        status = name_of(is_led, i, &name);
        if (status == BENTO_OK) {
            (void)printf("%s'%s'", (i > 0U) ? ", " : "", name);
        }
    }
    (void)printf("]");
    return status;
}

/* One value of the board_info() dict. */
static bento_status_t
print_info_value(const bento_gpio_board_info_t *info, uint32_t key)
{
    bento_status_t status = BENTO_OK;

    (void)printf("'%s': ", INFO_KEYS[key]);
    if (key == (uint32_t)INFO_NAME) {
        (void)printf("'%s'", info->name);
    } else if (key == (uint32_t)INFO_LEDS) {
        (void)printf("%" PRIu32, info->leds);
    } else if (key == (uint32_t)INFO_BUTTONS) {
        (void)printf("%" PRIu32, info->buttons);
    } else if (key == (uint32_t)INFO_LED_NAMES) {
        status = print_names(true, info->leds);
    } else {
        status = print_names(false, info->buttons);
    }
    return status;
}

/* The raw dict is too long for the 4.3-inch screen: it goes to the
 * computer's console. Everything on the screen came from it. */
static bento_status_t print_info(const bento_gpio_board_info_t *info)
{
    uint32_t order[INFO_KEY_COUNT];
    bento_status_t status = BENTO_OK;
    uint32_t i;

    dict_order(INFO_KEYS, INFO_KEY_COUNT, INFO_DICT_SIZE, order);
    (void)printf("board_info() = {");
    for (i = 0U; (i < (uint32_t)INFO_KEY_COUNT) && (status == BENTO_OK);
         i++) {
        (void)printf("%s", (i > 0U) ? ", " : "");
        status = print_info_value(info, order[i]);
    }
    (void)printf("}\n");
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_gpio_board_info_t info;
    bento_ui_id_t rows = 0U;
    bento_status_t status;

    /* asked once; the whole program reads the answer from here */
    bento_gpio_board_info_init(&info);
    status = bento_gpio_board_info(&info);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>บอร์ดตัวนี้มีอะไรบ้าง</h2>");
        status = build_header(ui, &info, &rows);
    }
    if (status == BENTO_OK) {
        print_counts(&info);
        status = list_leds(ui, info.leds, rows);
    }
    if (status == BENTO_OK) {
        status = list_buttons(info.buttons);
    }
    if (status == BENTO_OK) {
        status = show_trap(ui, &info);
    }
    if (status == BENTO_OK) {
        status = leds_off(ui);
    }
    if (status == BENTO_OK) {
        status = print_info(&info);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
