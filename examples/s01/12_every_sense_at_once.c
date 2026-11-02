/*
 * 12_every_sense_at_once.c -- every sensor in one call
 *
 * คำสั่งเดียว ได้ทุกเซนเซอร์พร้อมกัน sensors.snapshot() ขอค่าจากคอร์
 * CM55 ที่อ่านเซนเซอร์อยู่แล้ว สามการ์ดบนจอขยับพร้อมกัน เอียงบอร์ดแล้ว
 * ค่า az เปลี่ยน แตะแผ่นสัมผัสแล้วการ์ดกลางเปลี่ยน หมุนลูกบิดแล้ว
 * วงแหวนขวากวาดตาม บรรทัดล่างคือตัวนับรอบ ทำงาน 30 วินาทีแล้วจบ
 * และหลังรีเซ็ต CM55 ตอบช้าได้ราว 13 วินาที ลูปจึงนับรอบที่พลาด
 *
 * One call, every sensor: sensors.snapshot() asks the CM55 core,
 * which already reads the sensors, for its latest values. Three cards
 * move together: tilt the board and az changes, touch the pads and
 * the middle card changes, turn the knob and the arc follows. The
 * bottom line shows the sequence counters. It runs for 30 seconds;
 * after a reset CM55 may take about 13 s to answer, so the loop
 * counts the rounds it missed.
 *
 * Twin of 12_every_sense_at_once.py
 * (embedded-systems-for-aiot-developer/examples/s01/12_every_sense_at_once.py).
 * Style: CLEAN_C.md.
 *
 * Differs from the original:
 *  - sensors.snapshot() waits up to 16 s for CM55's first answer in a
 *    session and 1 s after it (modsensors.c). One C call waits at most
 *    3 s, so take_snapshot() spends the same budget in calls of up to
 *    3 s; "the first answer" is this run's, not the session's.
 *  - C has no dict. The keys are listed, and the last snapshot is
 *    printed, in the order MicroPython's hash table walks them
 *    (dict_order(), the py/map.c rules with the board's qstr hash
 *    width, as read_sensors.c does).
 *  - last_seq is written but never read in the original; it is left
 *    out.
 *  - Stop during a sleep or a snapshot ends the program with 0 at
 *    once (MicroPython raises KeyboardInterrupt there).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_time.h"

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200,        /* let the cleared screen settle */
    RUN_MS = 30000,         /* how long to watch */
    TICK_MS = 200,          /* CM55 reads its sensors this often */
    FIRST_FETCH_MS = 16000, /* the binding's wait for a first answer */
    FETCH_MS = 1000         /* and for every answer after it */
};

enum {
    AZ_FLAT = 8, /* m/s^2: above this the board lies flat */
    HUNDRED = 100,
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
    COL_WARN = 0xF5A623,
    COL_INFO = 0x4A9EFF
};

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

typedef struct {
    uint32_t alloc;
    int32_t slots[DICT_SLOTS_MAX]; /* a key's index, or NO_KEY */
} hash_table_t;

/* Where the program keeps the ids of the widgets it changes later. */
enum {
    SLOT_AX,
    SLOT_AY,
    SLOT_AZ,
    SLOT_PADS,
    SLOT_SLIDE_BAR,
    SLOT_SLIDE_TEXT,
    SLOT_ARC,
    SLOT_POT_TEXT,
    SLOT_COUNTERS,
    SLOT_STATE,
    SLOT_COUNT
};

enum {
    SLOT_NONE = SLOT_COUNT /* a widget nobody changes again */
};

/* One ui.<Widget>(...) call of the original: the kind, the slot that
 * keeps its id, its text and the factory's x, y, w, h, color, min,
 * max and value. */
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

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[SLOT_COUNT + 1]; /* + the spare for SLOT_NONE */
} sense_screen_t;

/* What the loop counts. */
typedef struct {
    int32_t rounds;
    int32_t misses;
    bool ready;
} tally_t;

#if defined(BENTO_NATIVE_H)
enum {
    QSTR_HASH_MASK = 0xFF
}; /* PSoC Edge: 1-byte qstr hashes */
#else
enum {
    QSTR_HASH_MASK = 0xFFFF
}; /* reTerminal: 2-byte hashes */
#endif

static void show_color(const sense_screen_t *screen, uint32_t slot,
                       uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, screen->ids[slot], color);
}

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

/* The statuses that are MicroPython's OSError from snapshot(). */
static bool is_miss(bento_status_t status)
{
    return (status == BENTO_E_NOT_READY) ||
           (status == BENTO_E_TRANSPORT) || (status == BENTO_E_TIMEOUT);
}

/* No reading this round: a miss, or a runtime without the snapshot
 * (the BENTO Emulator answers BENTO_E_UNSUPPORTED), which takes the
 * original's except OSError path too. Only a miss is asked again. */
static bool no_reading(bento_status_t status)
{
    return is_miss(status) || (status == BENTO_E_UNSUPPORTED);
}

/* sensors.snapshot(): the binding's budget, spent in calls of at most
 * BENTO_SENSORS_WAIT_MAX_MS each so that Stop is seen in between. */
static bento_status_t take_snapshot(bool answered_before,
                                    bento_sensors_snapshot_t *snapshot)
{
    const uint32_t budget_ms =
        answered_before ? (uint32_t)FETCH_MS : (uint32_t)FIRST_FETCH_MS;
    const uint32_t start_ms = bento_time_ticks_ms();
    uint32_t spent_ms = 0U;
    bento_status_t status = BENTO_E_TIMEOUT;

    while (is_miss(status) && (spent_ms < budget_ms)) {
        uint32_t wait_ms = budget_ms - spent_ms;

        if (wait_ms > BENTO_SENSORS_WAIT_MAX_MS) {
            wait_ms = BENTO_SENSORS_WAIT_MAX_MS;
        }
        bento_sensors_snapshot_init(snapshot);
        status = bento_sensors_snapshot(wait_ms, snapshot);
        spent_ms = (uint32_t)bento_time_ticks_diff(
            bento_time_ticks_ms(), start_ms);
        if (is_miss(status) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

/* round(value, 2) as MicroPython does it: value * 100 rounded half
 * to even (nearbyint), then divided by 100. */
static bento_real_t round_to_hundredths(float value)
{
    const bento_real_t hundred = (bento_real_t)HUNDRED;
    const bento_real_t half = (bento_real_t)0.5;
    const bento_real_t scaled = (bento_real_t)value * hundred;
    int32_t whole = (int32_t)scaled; /* toward zero */
    const bento_real_t rest = scaled - (bento_real_t)whole;
    const bool odd = (whole % 2) != 0;
    bento_real_t rounded;

    if ((rest > half) || ((rest == half) && odd)) {
        whole++;
    } else if ((rest < -half) || ((rest == -half) && odd)) {
        whole--;
    } else {
        /* already the nearest whole number */
    }
    rounded = (bento_real_t)whole / hundred;
    if ((whole == 0) && (scaled < (bento_real_t)0)) {
        rounded = -rounded; /* nearbyint keeps the sign: "-0.0" */
    }
    return rounded;
}

/* The snapshot dict (modsensors.c sensors_snapshot): its parts in the
 * order the binding stores them, and the keys of each part. */
enum {
    PART_MOTION,
    PART_TOUCH,
    PART_KNOB,
    PART_COUNT
};

enum {
    SNAPSHOT_DICT_SIZE = 3,
    AXIS_COUNT = 6,
    MOTION_KEY_COUNT = 7,
    TOUCH_PAD_COUNT = 2,
    TOUCH_KEY_COUNT = 4,
    KNOB_RAW = 0,
    KNOB_PERCENT = 1,
    KNOB_VOLTAGE = 2,
    KNOB_KEY_COUNT = 4
};

static const char *const PART_KEYS[PART_COUNT] = {"bmi270", "capsense",
                                                  "pot"};

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void console_line(const char *text)
{
    (void)bento_lcd_print(text);
}

static void console_html(const char *html)
{
    (void)bento_lcd_console(html);
}

/* The parts this snapshot holds (PART_*), in the order its dict
 * walks them; returns how many there are. */
static uint32_t snapshot_parts(const bento_sensors_snapshot_t *snapshot,
                               uint32_t parts[])
{
    const bool has[PART_COUNT] = {snapshot->has_bmi270 != 0U,
                                  snapshot->has_capsense != 0U,
                                  snapshot->has_pot != 0U};
    const char *keys[PART_COUNT];
    uint32_t stored[PART_COUNT];
    uint32_t order[PART_COUNT];
    uint32_t count = 0U;
    uint32_t part;

    for (part = 0U; part < (uint32_t)PART_COUNT; part++) {
        if (has[part]) {
            keys[count] = PART_KEYS[part];
            stored[count] = part;
            count++;
        }
    }
    dict_order(keys, count, SNAPSHOT_DICT_SIZE, order);
    for (part = 0U; part < count; part++) {
        parts[part] = stored[order[part]];
    }
    return count;
}

/* The first answer: say so, and list the keys CM55 filled in. A key
 * is there only when CM55 has that sensor, so it is asked, not read
 * blindly. */
static void tell_first_answer(const sense_screen_t *screen,
                              const bento_sensors_snapshot_t *snapshot,
                              int32_t rounds)
{
    char line[TEXT_BYTES];
    uint32_t parts[PART_COUNT];
    const uint32_t count = snapshot_parts(snapshot, parts);
    uint32_t i;

    show_color(screen, SLOT_STATE, COL_OK);
    (void)snprintf(line, sizeof(line),
                   "<span class=ok>ได้ค่าชุดแรกแล้วที่รอบ %ld </span>",
                   (long)rounds);
    console_line(line);
    for (i = 0U; i < count; i++) {
        (void)snprintf(line, sizeof(line), "  ได้คีย์ %s",
                       PART_KEYS[parts[i]]);
        console_line(line);
    }
}

/* print() of a float: str(v) as this board's MicroPython writes it. */
static void print_real(float value)
{
    char number[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;

    /* the buffer is BENTO_FMT_BUF_SIZE: always enough */
    (void)bento_fmt_real(number, sizeof(number), (bento_real_t)value,
                         &length);
    (void)printf("%s", number);
}

static void print_key(uint32_t place, const char *key)
{
    (void)printf("%s'%s': ", (place > 0U) ? ", " : "", key);
}

/* {'ax': .., 'ay': .., 'az': .., 'gx': .., 'gy': .., 'gz': ..,
 * 'sequence': n}, in its dict's walk order. */
static void print_motion(const bento_sensors_snapshot_t *snapshot)
{
    static const char *const keys[MOTION_KEY_COUNT] = {
        "ax", "ay", "az", "gx", "gy", "gz", "sequence"};
    const float axes[AXIS_COUNT] = {snapshot->ax, snapshot->ay,
                                    snapshot->az, snapshot->gx,
                                    snapshot->gy, snapshot->gz};
    uint32_t order[MOTION_KEY_COUNT];
    uint32_t i;

    dict_order(keys, MOTION_KEY_COUNT, MOTION_KEY_COUNT, order);
    (void)printf("{");
    for (i = 0U; i < (uint32_t)MOTION_KEY_COUNT; i++) {
        print_key(i, keys[order[i]]);
        if (order[i] < (uint32_t)AXIS_COUNT) {
            print_real(axes[order[i]]);
        } else {
            (void)printf("%u", (unsigned)snapshot->bmi270_sequence);
        }
    }
    (void)printf("}");
}

static void print_touch(const bento_sensors_snapshot_t *snapshot)
{
    static const char *const keys[TOUCH_KEY_COUNT] = {
        "btn0", "btn1", "slider", "sequence"};
    const uint32_t numbers[TOUCH_KEY_COUNT] = {
        snapshot->btn0, snapshot->btn1, snapshot->slider,
        snapshot->capsense_sequence};
    uint32_t order[TOUCH_KEY_COUNT];
    uint32_t i;

    dict_order(keys, TOUCH_KEY_COUNT, TOUCH_KEY_COUNT, order);
    (void)printf("{");
    for (i = 0U; i < (uint32_t)TOUCH_KEY_COUNT; i++) {
        const uint32_t key = order[i];

        print_key(i, keys[key]);
        if (key < (uint32_t)TOUCH_PAD_COUNT) { /* the pads are bools */
            (void)printf("%s", (numbers[key] != 0U) ? "True" : "False");
        } else {
            (void)printf("%lu", (unsigned long)numbers[key]);
        }
    }
    (void)printf("}");
}

static void print_knob(const bento_sensors_snapshot_t *snapshot)
{
    static const char *const keys[KNOB_KEY_COUNT] = {
        "raw", "percent", "voltage", "sequence"};
    uint32_t order[KNOB_KEY_COUNT];
    uint32_t i;

    dict_order(keys, KNOB_KEY_COUNT, KNOB_KEY_COUNT, order);
    (void)printf("{");
    for (i = 0U; i < (uint32_t)KNOB_KEY_COUNT; i++) {
        print_key(i, keys[order[i]]);
        if (order[i] == (uint32_t)KNOB_RAW) {
            (void)printf("%u", (unsigned)snapshot->pot_raw);
        } else if (order[i] == (uint32_t)KNOB_PERCENT) {
            print_real(snapshot->pot_percent);
        } else if (order[i] == (uint32_t)KNOB_VOLTAGE) {
            print_real(snapshot->pot_voltage);
        } else {
            (void)printf("%u", (unsigned)snapshot->pot_sequence);
        }
    }
    (void)printf("}");
}

/* The whole dict is too long for the 4.3 inch screen: it goes to the
 * computer's console. */
static void print_snapshot(const bento_sensors_snapshot_t *snapshot)
{
    uint32_t parts[PART_COUNT];
    const uint32_t count = snapshot_parts(snapshot, parts);
    uint32_t i;

    (void)printf("snapshot() ก้อนสุดท้าย = {");
    for (i = 0U; i < count; i++) {
        print_key(i, PART_KEYS[parts[i]]);
        if (parts[i] == (uint32_t)PART_MOTION) {
            print_motion(snapshot);
        } else if (parts[i] == (uint32_t)PART_TOUCH) {
            print_touch(snapshot);
        } else {
            print_knob(snapshot);
        }
    }
    (void)printf("}\n");
}

/* sensors.init() prints a line per sensor that failed, or that all
 * started (modsensors.c), before it answers. */
static void print_init_lines(uint32_t failed)
{
    static const char *const names[] = {"BMI270",   "DPS368",
                                        "SHT40",    "BMM350",
                                        "CapSense", "Potentiometer"};
    static const uint32_t bits[] = {
        BENTO_SENSORS_BMI270,   BENTO_SENSORS_DPS368,
        BENTO_SENSORS_SHT40,    BENTO_SENSORS_BMM350,
        BENTO_SENSORS_CAPSENSE, BENTO_SENSORS_POT};
    uint32_t i;

    for (i = 0U; i < (sizeof(bits) / sizeof(bits[0])); i++) {
        if ((failed & bits[i]) != 0U) {
            (void)printf("sensors: %s init failed\n", names[i]);
        }
    }
    if (failed == 0U) {
        (void)printf("sensors: all sensors initialized OK\n");
    }
}

/* The last snapshot, then sensors.init(): whatever this board answers
 * is printed, not what a comment guesses. */
static bento_status_t print_answers(bool answered_before)
{
    bento_sensors_snapshot_t snapshot;
    uint32_t failed = 0U;
    bento_status_t status = take_snapshot(answered_before, &snapshot);

    if (status == BENTO_OK) {
        print_snapshot(&snapshot);
    } else if (no_reading(status)) {
        (void)printf("snapshot() ปฏิเสธ: sensors.snapshot(): no "
                     "reply from the display core. It answers sensor "
                     "requests about 13 s after a reset -- if this "
                     "fired at boot, ask again in a moment\n");
        status = BENTO_OK;
    } else {
        /* any other status ends the program */
    }
    if (status == BENTO_OK) {
        status = bento_sensors_init(&failed);
    }
    if (status == BENTO_OK) {
        print_init_lines(failed);
        (void)printf("sensors.init() ตอบว่า: %s\n",
                     (failed == 0U) ? "True" : "False");
    } else if (status == BENTO_E_UNSUPPORTED) {
        (void)printf("sensors.init() ปฏิเสธว่า: sensors.init() is "
                     "unavailable on the Eva Kit: CM55 owns SCB0 "
                     "(display touch + sensors). Driving it from "
                     "MicroPython hangs the board. Use "
                     "sensors.snapshot() instead, which asks CM55 over "
                     "IPC.\n");
        status = BENTO_OK;
    } else if (status == BENTO_E_IO) {
        (void)printf("sensors.init() ปฏิเสธว่า: Sensor I2C bus init "
                     "failed\n");
        status = BENTO_OK;
    } else {
        /* any other status ends the program */
    }
    return status;
}

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
static void show_text(const sense_screen_t *screen, uint32_t slot,
                      const char *text)
{
    (void)bento_ui_set_text(screen->ui, screen->ids[slot], text,
                            (uint32_t)strlen(text));
}

static void show_value(const sense_screen_t *screen, uint32_t slot,
                       int32_t value)
{
    (void)bento_ui_set_value(screen->ui, screen->ids[slot], value);
}

static bento_status_t add_widget(sense_screen_t *screen,
                                 const widget_spec_t *spec)
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
    args.min_val = spec->minimum;
    args.max_val = spec->maximum;
    args.font_px = spec->value;
    return bento_ui_create(screen->ui, spec->kind, &args,
                           &screen->ids[spec->slot]);
}

static bento_status_t add_widgets(sense_screen_t *screen,
                                  const widget_spec_t *specs,
                                  uint32_t count)
{
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = add_widget(screen, &specs[i]);
    }
    return status;
}

/* Title, the motion card and the touch card up to its bar. */
static bento_status_t add_left_cards(sense_screen_t *screen)
{
    static const widget_spec_t rows[] = {
        {UI_WIDGET_LABEL, SLOT_NONE, "อ่านทุกเซนเซอร์ด้วยคำสั่งเดียว", 20, 12,
         AUTO, AUTO, COL_TEXT, 0, 0, 24},
        {UI_WIDGET_LABEL, SLOT_NONE,
         "sensors.snapshot() คืน dict ก้อนเดียว", 20, 48, AUTO, AUTO,
         COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_PANEL, SLOT_NONE, NULL, 20, 80, 240, 180, COL_CARD,
         COL_DIM, 12, 1},
        {UI_WIDGET_LABEL, SLOT_NONE, "IMU  BMI270", 36, 88, AUTO, AUTO,
         COL_INFO, 0, 0, 16},
        {UI_WIDGET_LABEL, SLOT_AX, "ax  รอค่า", 36, 116, AUTO, AUTO,
         COL_TEXT, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_AY, "ay  รอค่า", 36, 144, AUTO, AUTO,
         COL_TEXT, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_AZ, "az  รอค่า", 36, 172, AUTO, AUTO,
         COL_TEXT, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_NONE, "เอียงบอร์ดแล้วดู az", 36, 208, AUTO,
         AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_PANEL, SLOT_NONE, NULL, 276, 80, 240, 180, COL_CARD,
         COL_DIM, 12, 1},
        {UI_WIDGET_LABEL, SLOT_NONE, "CapSense", 292, 88, AUTO, AUTO,
         COL_INFO, 0, 0, 16},
        {UI_WIDGET_LABEL, SLOT_PADS, "ปุ่ม  รอค่า", 292, 116, AUTO, AUTO,
         COL_TEXT, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_NONE, "แถบเลื่อน 0-100", 292, 146, AUTO,
         AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_BAR, SLOT_SLIDE_BAR, NULL, 292, 176, 200, 24, 0, 0,
         HUNDRED, 0}};

    return add_widgets(screen, rows, sizeof(rows) / sizeof(rows[0]));
}

/* The slider line, the knob card up to its arc. */
static bento_status_t add_knob_card(sense_screen_t *screen)
{
    static const widget_spec_t rows[] = {
        {UI_WIDGET_LABEL, SLOT_SLIDE_TEXT, "slider  รอค่า", 292, 208,
         AUTO, AUTO, COL_TEXT, 0, 0, FONT_PX},
        {UI_WIDGET_PANEL, SLOT_NONE, NULL, 532, 80, 240, 180, COL_CARD,
         COL_DIM, 12, 1},
        {UI_WIDGET_LABEL, SLOT_NONE, "ลูกบิด  pot", 548, 84, AUTO, AUTO,
         COL_INFO, 0, 0, FONT_PX},
        {UI_WIDGET_ARC, SLOT_ARC, NULL, 556, 112, 104, 104, 0, 0,
         HUNDRED, 0}};

    return add_widgets(screen, rows, sizeof(rows) / sizeof(rows[0]));
}

static bento_status_t add_bottom_lines(sense_screen_t *screen)
{
    static const widget_spec_t rows[] = {
        {UI_WIDGET_LABEL, SLOT_POT_TEXT, "pot  รอค่า", 548, 224, AUTO,
         AUTO, COL_TEXT, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_COUNTERS, "ตัวนับรอบ  ยังไม่ได้อ่าน", 20, 276,
         AUTO, AUTO, COL_DIM, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_STATE, "กำลังขอค่าชุดแรกจาก CM55", 20, 308,
         AUTO, AUTO, COL_WARN, 0, 0, FONT_PX},
        {UI_WIDGET_LABEL, SLOT_NONE,
         "เอียง แตะ หมุน "
         "แล้วดูสามการ์ดขยับพร้อมกัน",
         20, 340, AUTO, AUTO, COL_DIM, 0, 0, FONT_PX}};

    return add_widgets(screen, rows, sizeof(rows) / sizeof(rows[0]));
}

/* Bar and Arc take no color= when they are made: each gets its
 * colour right after, as in the original. */
static bento_status_t build_screen(sense_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_left_cards(screen);
    }
    if (status == BENTO_OK) {
        show_color(screen, SLOT_SLIDE_BAR, COL_INFO);
        status = add_knob_card(screen);
    }
    if (status == BENTO_OK) {
        show_color(screen, SLOT_ARC, COL_OK);
        status = add_bottom_lines(screen);
    }
    if (status == BENTO_OK) {
        poll_ui(screen->ui);
    }
    return status;
}

/* "<name>  <str(round(value, 2))>" */
static void show_axis(const sense_screen_t *screen, uint32_t slot,
                      const char *name, float value)
{
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint32_t length = 0U;

    /* the buffer is BENTO_FMT_BUF_SIZE: always enough */
    (void)bento_fmt_real(number, sizeof(number),
                         round_to_hundredths(value), &length);
    (void)snprintf(text, sizeof(text), "%s  %s", name, number);
    show_text(screen, slot, text);
}

static void show_motion(const sense_screen_t *screen,
                        const bento_sensors_snapshot_t *snapshot,
                        char *counters, size_t size)
{
    char piece[TEXT_BYTES];
    const float flat = (float)AZ_FLAT;

    show_axis(screen, SLOT_AX, "ax", snapshot->ax);
    show_axis(screen, SLOT_AY, "ay", snapshot->ay);
    show_axis(screen, SLOT_AZ, "az", snapshot->az);
    show_color(screen, SLOT_AZ,
               (snapshot->az > flat) ? COL_OK : COL_WARN);
    (void)snprintf(piece, sizeof(piece), "imu %u",
                   (unsigned)snapshot->bmi270_sequence);
    append_text(counters, size, piece);
}

static void show_touch(const sense_screen_t *screen,
                       const bento_sensors_snapshot_t *snapshot,
                       char *counters, size_t size)
{
    const bool left = snapshot->btn0 != 0U;
    const bool right = snapshot->btn1 != 0U;
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "ปุ่ม  %s / %s",
                   left ? "แตะ" : "ว่าง", right ? "แตะ" : "ว่าง");
    show_text(screen, SLOT_PADS, text);
    show_color(screen, SLOT_PADS, (left || right) ? COL_OK : COL_TEXT);
    show_value(screen, SLOT_SLIDE_BAR, (int32_t)snapshot->slider);
    (void)snprintf(text, sizeof(text), "slider  %u",
                   (unsigned)snapshot->slider);
    show_text(screen, SLOT_SLIDE_TEXT, text);
    (void)snprintf(text, sizeof(text), "  touch %u",
                   (unsigned)snapshot->capsense_sequence);
    append_text(counters, size, text);
}

/* The arc takes whole numbers only: int() cuts the fraction off. */
static void show_knob(const sense_screen_t *screen,
                      const bento_sensors_snapshot_t *snapshot,
                      char *counters, size_t size)
{
    const int32_t percent = (int32_t)snapshot->pot_percent;
    char text[TEXT_BYTES];

    show_value(screen, SLOT_ARC, percent);
    (void)snprintf(text, sizeof(text), "pot  %ld %%", (long)percent);
    show_text(screen, SLOT_POT_TEXT, text);
    (void)snprintf(text, sizeof(text), "  pot %u",
                   (unsigned)snapshot->pot_sequence);
    append_text(counters, size, text);
}

static void show_snapshot(const sense_screen_t *screen,
                          const bento_sensors_snapshot_t *snapshot,
                          const tally_t *tally)
{
    char counters[TEXT_BYTES];
    char state[TEXT_BYTES];

    (void)snprintf(counters, sizeof(counters), "%s", "ตัวนับรอบ  ");
    if (snapshot->has_bmi270 != 0U) {
        show_motion(screen, snapshot, counters, sizeof(counters));
    }
    if (snapshot->has_capsense != 0U) {
        show_touch(screen, snapshot, counters, sizeof(counters));
    }
    if (snapshot->has_pot != 0U) {
        show_knob(screen, snapshot, counters, sizeof(counters));
    }
    show_text(screen, SLOT_COUNTERS, counters);
    (void)snprintf(state, sizeof(state),
                   "อ่านไปแล้ว %ld รอบ - พลาด "
                   "%ld",
                   (long)tally->rounds, (long)tally->misses);
    show_text(screen, SLOT_STATE, state);
}

static void show_miss(const sense_screen_t *screen, int32_t misses)
{
    char text[TEXT_BYTES];

    show_color(screen, SLOT_STATE, COL_WARN);
    (void)snprintf(text, sizeof(text),
                   "CM55 ยังไม่ตอบ - พลาดไป %ld "
                   "รอบ",
                   (long)misses);
    show_text(screen, SLOT_STATE, text);
    poll_ui(screen->ui);
}

/* The final values stay on the screen so they can be read. */
static void tell_end(const sense_screen_t *screen, const tally_t *tally)
{
    char text[TEXT_BYTES];

    show_color(screen, SLOT_STATE, COL_DIM);
    (void)snprintf(text, sizeof(text), "จบแล้ว - อ่าน %ld รอบ พลาด %ld",
                   (long)tally->rounds, (long)tally->misses);
    show_text(screen, SLOT_STATE, text);
    poll_ui(screen->ui);
    console_html("<span class=muted>------------------------</span>");
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>อ่าน %ld รอบ | พลาด %ld รอบ</span>",
                   (long)tally->rounds, (long)tally->misses);
    console_line(text);
}

/* One round: ask, show, then sleep what is left of TICK_MS. A miss
 * sleeps the whole tick, as the original's continue does. */
static bento_status_t read_round(const sense_screen_t *screen,
                                 tally_t *tally, uint32_t work_start_ms)
{
    bento_sensors_snapshot_t snapshot;
    bento_status_t status;
    int32_t left_ms = TICK_MS;

    tally->rounds++;
    status = take_snapshot(tally->ready, &snapshot);
    if (no_reading(status)) {
        tally->misses++;
        show_miss(screen, tally->misses);
        status = BENTO_OK;
    } else if (status == BENTO_OK) {
        if (!tally->ready) {
            tally->ready = true;
            tell_first_answer(screen, &snapshot, tally->rounds);
        }
        show_snapshot(screen, &snapshot, tally);
        poll_ui(screen->ui);
        left_ms = TICK_MS - bento_time_ticks_diff(bento_time_ticks_ms(),
                                                  work_start_ms);
    } else {
        /* any other status ends the program, as an uncaught error */
    }
    if ((status == BENTO_OK) && (left_ms > 0)) {
        status = pause_ms((uint32_t)left_ms);
    }
    return status;
}

static bento_status_t watch(const sense_screen_t *screen,
                            tally_t *tally)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;
    bool watching = true;

    while ((status == BENTO_OK) && watching) {
        const uint32_t work_start_ms = bento_time_ticks_ms();

        if (bento_time_ticks_diff(work_start_ms, start_ms) >= RUN_MS) {
            watching = false;
        } else {
            status = read_round(screen, tally, work_start_ms);
        }
    }
    return status;
}

int main(void)
{
    sense_screen_t screen;
    tally_t tally = {0};
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear(); /* refused like print: ignored */
        console_html("<h2>อ่านทุกเซนเซอร์ด้วยคำสั่งเดียว</h2>");
        console_html("<span class=muted>ไม่ต้องเรียก sensors.init() "
                     "ก่อน</span>");
        console_line("CM55 อ่านเซนเซอร์อยู่แล้ว เราขอค่าจากมันแทน");
        status = watch(&screen, &tally);
    }
    if (status == BENTO_OK) {
        tell_end(&screen, &tally);
        status = print_answers(tally.ready);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
