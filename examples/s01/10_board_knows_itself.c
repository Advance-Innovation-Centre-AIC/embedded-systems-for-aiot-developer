/*
 * 10_board_knows_itself.c -- the board says what it has
 *
 * ถามบอร์ดว่าตัวเองมีอะไร แทนที่จะเปิดคู่มือหา จอแสดงชื่อบอร์ด
 * จำนวนหลอดกับปุ่มบนเลขเจ็ดส่วน รายชื่อหลอดและปุ่มจาก
 * gpio.board_info() แล้วตารางแปดช่องที่ถาม machine ทีละชื่อว่ามีคำสั่ง
 * นั้นจริงไหม ช่องเขียวคือมี ช่องแดงคือไม่มี ลิ้นชัก Console
 * เล่าเรื่องเดียวกันทีละบรรทัด
 *
 * Asks the board what it has instead of looking it up in a manual:
 * the board's name, its LED and button counts on two Seg7 displays,
 * the LED and button names from gpio.board_info(), then a table of
 * eight cells that asks, name by name, whether machine really has
 * that call (green: it has, red: it has not). The Console drawer
 * tells the same story line by line.
 *
 * Twin of 10_board_knows_itself.py
 * (embedded-systems-for-aiot-developer/examples/s01/10_board_knows_itself.py).
 * Style: CLEAN_C.md.
 *
 * Differs from the original:
 *  - C cannot ask a module for an attribute (hasattr). The question
 *    goes to bento_machine_caps(), which says what the C machine
 *    module of this firmware offers: Pin, I2C, PDM_PCM and RTC on the
 *    PSoC Edge, no PWM, ADC, SPI or Timer. Where MicroPython's machine
 *    has a name the C layer lacks (or the other way), the cell differs.
 *    A firmware without the C machine module ends the program there
 *    with its status (hasattr never fails).
 *  - C has no dict. print(info) is written out in MicroPython's dict
 *    form, keys in the order MicroPython's hash table gives them
 *    (dict_order(), the py/map.c rules with the board's qstr hash
 *    width, as read_sensors.c does). Names are printed in single
 *    quotes without escaping: the boards' LED and button names hold
 *    no quote or backslash.
 *  - Stop during a sleep ends the program with 0 at once (MicroPython
 *    raises KeyboardInterrupt there).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_machine.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    READ_MS = 600,   /* time to read the two name rows */
    ASK_MS = 220     /* one cell at a time, so the eye can follow */
};

/* The table of questions: two columns of four cells. */
enum {
    ASK_COUNT = 8,
    CELL_ROWS = 4,
    CELL_LEFT_X = 40,
    CELL_RIGHT_X = 380,
    CELL_TOP_Y = 256,
    CELL_STEP_Y = 26,
    FONT_PX = 20,
    AUTO = -1,       /* x, y, w or h the original leaves out */
    TEXT_BYTES = 160 /* longer than SET_TEXT and lcd carry */
};

/* Colours, 0xRRGGBB. */
enum {
    COL_TEXT = 0xE8EAED,
    COL_DIM = 0x9AA3AF,
    COL_CARD = 0x171B22,
    COL_OK = 0x30A46C,
    COL_BAD = 0xE5484D,
    COL_INFO = 0x4A9EFF
};

/* Where the program keeps the ids of the widgets it changes later. */
enum {
    SLOT_LED_ROW = 0,
    SLOT_BUTTON_ROW = 1,
    SLOT_NOTE = 2,
    SLOT_CELL = 3, /* ASK_COUNT cells from here */
    SLOT_COUNT = SLOT_CELL + ASK_COUNT,
    SLOT_NONE = SLOT_COUNT /* a widget nobody changes again */
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[SLOT_COUNT + 1]; /* + the spare for SLOT_NONE */
    bento_gpio_board_info_t info;
} board_screen_t;

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

static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
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

/* One ui.<Widget>(...) call of the original: the kind, the slot that
 * keeps its id, its text (NULL: worked out at run time) and the
 * factory's x, y, w, h, color, min, max and value. */
typedef struct {
    uint8_t kind;
    uint8_t slot;
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t minimum;
    int32_t maximum;
    int32_t value;
} widget_spec_t;

/* ui.poll() here only lets the screen catch up; no event is read. */
static void poll_ui(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* a failed ui.poll() answers no events, which is all this needs */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The widget setters ignore a refusal in MicroPython (modui.c). */
static void show_text(const board_screen_t *screen, uint32_t slot,
                      const char *text)
{
    (void)bento_ui_set_text(screen->ui, screen->ids[slot], text,
                            (uint32_t)strlen(text));
}

static void show_color(const board_screen_t *screen, uint32_t slot,
                       uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, screen->ids[slot], color);
}

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void console_line(const char *text)
{
    (void)bento_lcd_print(text);
}

static void console_html(const char *html)
{
    (void)bento_lcd_console(html);
}

static bento_status_t add_widget(board_screen_t *screen,
                                 const widget_spec_t *spec,
                                 const char *text)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    if (text != NULL) {
        args.text = text;
        args.text_len = (uint32_t)strlen(text);
    }
    args.x = spec->x;
    args.y = spec->y;
    args.w = spec->w;
    args.h = spec->h;
    args.color = spec->color;
    args.min_val = spec->minimum;
    args.max_val = spec->maximum;
    args.font_px = spec->value;
    return bento_ui_create(screen->ui, spec->kind, &args,
                           &screen->ids[spec->slot]);
}

/* The card: name, LED count and button count. A row whose text is
 * NULL takes the next of the board's own answers. */
static bento_status_t add_card(board_screen_t *screen)
{
    static const widget_spec_t card_widgets[] = {
        {UI_WIDGET_LABEL, SLOT_NONE, "บอร์ดตอบเองว่ามีอะไร", 20, 12, AUTO,
         AUTO, COL_TEXT, 0, 0, 24},
        {UI_WIDGET_PANEL, SLOT_NONE, NULL, 20, 52, 652, 104, COL_CARD,
         COL_DIM, 12, 1},
        {UI_WIDGET_LABEL, SLOT_NONE, "ชื่อบอร์ด", 40, 58, AUTO, AUTO,
         COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_NONE, NULL, 40, 92, AUTO, AUTO, COL_INFO,
         0, 0, 24},
        {UI_WIDGET_LABEL, SLOT_NONE, "หลอด", 440, 58, AUTO, AUTO,
         COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_SEG7, SLOT_NONE, NULL, 440, 88, 72, 56, COL_OK, 0, 0,
         0},
        {UI_WIDGET_LABEL, SLOT_NONE, "ปุ่ม", 540, 58, AUTO, AUTO, COL_DIM,
         0, 0, FONT_PX},
        {UI_WIDGET_SEG7, SLOT_NONE, NULL, 540, 88, 72, 56, COL_OK, 0, 0,
         0}};
    char leds[TEXT_BYTES];
    char buttons[TEXT_BYTES];
    const char *answers[] = {screen->info.name, leds, buttons};
    uint32_t next = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    (void)snprintf(leds, sizeof(leds), "%lu",
                   (unsigned long)screen->info.leds);
    (void)snprintf(buttons, sizeof(buttons), "%lu",
                   (unsigned long)screen->info.buttons);
    for (i = 0U;
         (i < (sizeof(card_widgets) / sizeof(card_widgets[0]))) &&
         (status == BENTO_OK);
         i++) {
        const char *text = card_widgets[i].text;

        if (text == NULL) {
            text = answers[next];
            next++;
        }
        status = add_widget(screen, &card_widgets[i], text);
    }
    return status;
}

/* The eight cells, two columns of four. A cell is never created
 * empty: LVGL would write "Label" into it. */
static bento_status_t add_cells(board_screen_t *screen)
{
    static const widget_spec_t first_cell = {UI_WIDGET_LABEL,
                                             SLOT_CELL,
                                             "รอถาม",
                                             CELL_LEFT_X,
                                             CELL_TOP_Y,
                                             AUTO,
                                             AUTO,
                                             COL_DIM,
                                             0,
                                             0,
                                             FONT_PX};
    widget_spec_t cell = first_cell;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < ASK_COUNT) && (status == BENTO_OK); i++) {
        const int32_t row = (int32_t)(i % (uint32_t)CELL_ROWS);

        cell.slot = (uint8_t)(SLOT_CELL + i);
        cell.x = (i < (uint32_t)CELL_ROWS) ? CELL_LEFT_X : CELL_RIGHT_X;
        cell.y = (int16_t)(CELL_TOP_Y + (row * CELL_STEP_Y));
        status = add_widget(screen, &cell, cell.text);
    }
    return status;
}

/* The two name rows, the eight empty cells and the note. A cell is
 * never created empty: LVGL would write "Label" into it. */
static bento_status_t add_table(board_screen_t *screen)
{
    static const widget_spec_t row_widgets[] = {
        {UI_WIDGET_LABEL, SLOT_LED_ROW, "กำลังอ่านรายชื่อหลอด", 20, 168,
         AUTO, AUTO, COL_TEXT, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_BUTTON_ROW, "กำลังอ่านรายชื่อปุ่ม", 20, 196,
         AUTO, AUTO, COL_TEXT, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_NONE, "ถาม machine ว่ามีคำสั่งไหนจริง", 20,
         228, AUTO, AUTO, COL_TEXT, 0, 0, FONT_PX}};
    static const widget_spec_t note_widget = {UI_WIDGET_LABEL,
                                              SLOT_NOTE,
                                              "ยังไม่ได้ถามอะไรเลย",
                                              20,
                                              366,
                                              AUTO,
                                              AUTO,
                                              COL_DIM,
                                              0,
                                              0,
                                              FONT_PX};
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < (sizeof(row_widgets) / sizeof(row_widgets[0]))) &&
                 (status == BENTO_OK);
         i++) {
        status =
            add_widget(screen, &row_widgets[i], row_widgets[i].text);
    }
    if (status == BENTO_OK) {
        status = add_cells(screen);
    }
    if (status == BENTO_OK) {
        status = add_widget(screen, &note_widget, note_widget.text);
    }
    return status;
}

static bento_status_t build_screen(board_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_table(screen);
    }
    if (status == BENTO_OK) {
        poll_ui(screen->ui);
    }
    return status;
}

static void tell_counts(const bento_gpio_board_info_t *info)
{
    char line[TEXT_BYTES];

    (void)bento_lcd_clear(); /* a refused clear is ignored, as print */
    console_html("<h2>บอร์ดตอบเองว่ามีอะไร</h2>");
    (void)snprintf(line, sizeof(line), "ชื่อ: %s", info->name);
    console_line(line);
    (void)snprintf(line, sizeof(line), "หลอด %lu ดวง | ปุ่ม %lu ปุ่ม",
                   (unsigned long)info->leds,
                   (unsigned long)info->buttons);
    console_line(line);
}

/* One console line per LED (or button), then the same names in one
 * row on the screen: "gpio.led(i) -> 0=NAME  1=NAME  ". */
static bento_status_t list_names(const board_screen_t *screen,
                                 bool is_led)
{
    const char *call = is_led ? "gpio.led" : "gpio.button";
    const uint32_t count =
        is_led ? screen->info.leds : screen->info.buttons;
    char row[TEXT_BYTES];
    char piece[TEXT_BYTES];
    uint32_t i;
    bento_status_t status = BENTO_OK;

    (void)snprintf(row, sizeof(row), "%s(i) -> ", call);
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        const char *name = NULL;

        status = name_of(is_led, i, &name);
        if (status == BENTO_OK) {
            (void)snprintf(piece, sizeof(piece), "  %s(%lu) = %s", call,
                           (unsigned long)i, name);
            console_line(piece);
            (void)snprintf(piece, sizeof(piece), "%lu=%s  ",
                           (unsigned long)i, name);
            append_text(row, sizeof(row), piece);
        }
    }
    if (status == BENTO_OK) {
        show_text(screen, is_led ? SLOT_LED_ROW : SLOT_BUTTON_ROW, row);
    }
    return status;
}

static bento_status_t tell_names(const board_screen_t *screen)
{
    bento_status_t status;

    console_html("<span class=muted>--- หลอด ---</span>");
    status = list_names(screen, true);
    if (status == BENTO_OK) {
        console_html("<span class=muted>--- ปุ่ม ---</span>");
        status = list_names(screen, false);
    }
    if (status == BENTO_OK) {
        poll_ui(screen->ui);
        status = pause_ms(READ_MS);
    }
    return status;
}

static void tell_result(const board_screen_t *screen, uint32_t have)
{
    char text[TEXT_BYTES];

    show_color(screen, SLOT_NOTE, COL_TEXT);
    (void)snprintf(text, sizeof(text), "ถาม %d ชื่อ มีจริง %lu ชื่อ",
                   ASK_COUNT, (unsigned long)have);
    show_text(screen, SLOT_NOTE, text);
    poll_ui(screen->ui);
    console_html("<span class=muted>------------------------</span>");
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>มีจริง %lu จาก %d ชื่อ</span>",
                   (unsigned long)have, ASK_COUNT);
    console_line(text);
    console_line("<span class=info>ไม่มี machine.PWM ก็หรี่ไฟได้ ด้วย "
                 "gpio.led().brightness()</span>");
}

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

/* One name asked of machine, and the capability bit that answers. */
typedef struct {
    const char *name;
    uint32_t cap;
} question_t;

/* The first four are expected; the last four are what code from the
 * internet often reaches for. */
static const question_t QUESTIONS[ASK_COUNT] = {
    {"Pin", BENTO_MACHINE_CAP_PIN},
    {"I2C", BENTO_MACHINE_CAP_I2C},
    {"PDM_PCM", BENTO_MACHINE_CAP_PDM},
    {"RTC", BENTO_MACHINE_CAP_RTC},
    {"PWM", BENTO_MACHINE_CAP_PWM},
    {"ADC", BENTO_MACHINE_CAP_ADC},
    {"SPI", BENTO_MACHINE_CAP_SPI},
    {"Timer", BENTO_MACHINE_CAP_TIMER}};

static bool machine_has(uint32_t caps, uint32_t question)
{
    return (caps & QUESTIONS[question].cap) != 0U;
}

/* One cell: green "มี" or red "ไม่มี", and the same on the console. */
static void show_answer(const board_screen_t *screen, uint32_t question,
                        bool has)
{
    const char *name = QUESTIONS[question].name;
    char text[TEXT_BYTES];

    show_color(screen, SLOT_CELL + question, has ? COL_OK : COL_BAD);
    (void)snprintf(text, sizeof(text), "%s   %s", name,
                   has ? "มี" : "ไม่มี");
    show_text(screen, SLOT_CELL + question, text);
    (void)snprintf(text, sizeof(text),
                   "<span class=%s>machine.%s %s</span>",
                   has ? "ok" : "error", name, has ? "มี" : "ไม่มี");
    console_line(text);
}

/* Asks machine name by name and answers how many it really has. */
static bento_status_t ask_machine(const board_screen_t *screen,
                                  uint32_t caps, uint32_t *have)
{
    uint32_t i;
    bento_status_t status = BENTO_OK;

    *have = 0U;
    console_html("<span class=muted>--- machine ---</span>");
    for (i = 0U; (i < ASK_COUNT) && (status == BENTO_OK); i++) {
        const bool has = machine_has(caps, i);

        if (has) {
            *have = *have + 1U;
        }
        show_answer(screen, i, has);
        poll_ui(screen->ui);
        status = pause_ms(ASK_MS);
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
        (void)printf("%lu", (unsigned long)info->leds);
    } else if (key == (uint32_t)INFO_BUTTONS) {
        (void)printf("%lu", (unsigned long)info->buttons);
    } else if (key == (uint32_t)INFO_LED_NAMES) {
        status = print_names(true, info->leds);
    } else {
        status = print_names(false, info->buttons);
    }
    return status;
}

/* The whole dict goes to the computer's console: it is too long to
 * read on a 4.3 inch screen. */
static bento_status_t print_summary(const bento_gpio_board_info_t *info,
                                    uint32_t caps)
{
    uint32_t order[INFO_KEY_COUNT];
    bento_status_t status = BENTO_OK;
    uint32_t i;
    bool first = true;

    dict_order(INFO_KEYS, INFO_KEY_COUNT, INFO_DICT_SIZE, order);
    (void)printf("board_info() = {");
    for (i = 0U; (i < (uint32_t)INFO_KEY_COUNT) && (status == BENTO_OK);
         i++) {
        (void)printf("%s", (i > 0U) ? ", " : "");
        status = print_info_value(info, order[i]);
    }
    (void)printf("}\n");
    (void)printf("machine ที่มีจริง: [");
    for (i = 0U; i < (uint32_t)ASK_COUNT; i++) {
        if (machine_has(caps, i)) {
            (void)printf("%s'%s'", first ? "" : ", ",
                         QUESTIONS[i].name);
            first = false;
        }
    }
    (void)printf("]\n");
    return status;
}

static bento_status_t run(board_screen_t *screen)
{
    uint32_t caps = 0U;
    uint32_t have = 0U;
    bento_status_t status = build_screen(screen);

    if (status == BENTO_OK) {
        tell_counts(&screen->info);
        status = tell_names(screen);
    }
    if (status == BENTO_OK) {
        status = bento_machine_caps(&caps);
    }
    if (status == BENTO_OK) {
        status = ask_machine(screen, caps, &have);
    }
    if (status == BENTO_OK) {
        tell_result(screen, have);
        status = print_summary(&screen->info, caps);
    }
    return status;
}

int main(void)
{
    board_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    /* asked once; the whole program reads the answer from here */
    bento_gpio_board_info_init(&screen.info);
    status = bento_gpio_board_info(&screen.info);
    if (status == BENTO_OK) {
        status = run(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
