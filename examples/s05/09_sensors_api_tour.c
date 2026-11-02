/*
 * 09_sensors_api_tour.c -- sensors: ใครตอบ ใครปฏิเสธ
 *
 * ทัวร์ชื่อในโมดูล sensors เก้าชื่อ เรียกจริงทีละชื่อแล้วรายงานสิ่งที่บอร์ดตอบ
 * แถวเขียวคือตอบได้ แถวส้มคือบอร์ดปฏิเสธ (บน Eva Kit คอร์จอถือบัส SCB0 ไว้
 * init scan push live_push auto จึงถูกปิด) ปฏิเสธเสียงดังดีกว่าบอร์ดค้างเงียบ
 * บนบอร์ดอื่นสามชื่อสุดท้ายถูกข้าม เพราะจะส่งค่าจริง วนไม่รู้จบ หรือเปิด task ค้าง
 * ปิดท้ายด้วยค่าลูกบิดและ CapSense ปุ่มทดสอบอีกครั้ง และปุ่มออก
 *
 * A tour of nine names in the sensors module: each is really called
 * and what the board answers is reported. Green rows answered; orange
 * rows were refused (on the Eva Kit the display core holds bus SCB0,
 * so init, scan, push, live_push and auto are closed). A loud refusal
 * beats a silently stuck board. On other boards the last three are
 * skipped: they would send real data, loop for ever or leave a task
 * running. It ends with the knob and CapSense readings. Buttons: test
 * again, and exit.
 *
 * Twin of 09_sensors_api_tour.py
 * (embedded-systems-for-aiot-developer/examples/s05/09_sensors_api_tour.py),
 * rewritten from the library twin aiot-s05-09-sensors-api-tour. Style:
 * CLEAN_C.md. Where C must differ: an exception's text is the binding's
 * own; here the console gets the status name (and, for snapshot(), the
 * reason bento_sensors_snapshot_why() gives). A dict's keys come from
 * the has_* fields of the C struct, sorted as sorted() sorts them;
 * capsense.read() is printed in its keys' insertion order. snapshot()
 * waits at most BENTO_SENSORS_WAIT_MAX_MS (3 s) per call, where the
 * binding may wait longer at first boot.
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_gpio.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add, or a value differs in the last digit. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

enum {
    LOOP_MS = 200,
    ROW_COUNT = 9,
    ROW_X = 24,
    ROW_Y = 60,
    ROW_PITCH = 27, /* fits a 20 px Thai line */
    FONT_ROW_PX = 20,
    KEY_COUNT = 8,
    KEYS_SHOWN =
        24, /* characters of the key list that fit the column */
    CONSOLE_PIECE = 100,
    PERCENT_DIGITS = 1,
    VOLT_DIGITS = 3,
    AUTO_RATE_MS = 200,
    AUTO = -1,       /* no w= or h= given: the widget's own size */
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The nine names, in the order of the rows. */
enum {
    ROW_SNAPSHOT,
    ROW_READ_ALL,
    ROW_AUTO_STATUS,
    ROW_AUTO_RATE,
    ROW_INIT,
    ROW_SCAN,
    ROW_PUSH,
    ROW_LIVE_PUSH,
    ROW_AUTO
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_NO = 0xF5A623,
    COLOR_THEME = 0 /* no color= given */
};

/* The widgets beside the rows, in the order they are made. */
enum {
    W_TITLE,
    W_KEYS,
    W_SAME,
    W_AUTO_STATUS,
    W_AGAIN,
    W_EXIT,
    W_CARD,
    W_CARD_LINE_1,
    W_CARD_LINE_2,
    W_COUNT
};

/* The bits of the dict keys, in sorted() order of their names. */
enum {
    KEY_BMI270 = 0x01,
    KEY_BMM350 = 0x02,
    KEY_CAPSENSE = 0x04,
    KEY_DPS368 = 0x08,
    KEY_LIS3DH = 0x10,
    KEY_LTR303 = 0x20,
    KEY_POT = 0x40,
    KEY_SHT40 = 0x80
};

static const char *const NAMES[ROW_COUNT] = {
    "snapshot()",     "read_all()",  "auto_status()",
    "auto_rate(200)", "init()",      "scan()",
    "push()",         "live_push()", "auto()"};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
    bento_ui_id_t rows[ROW_COUNT];
    bool is_eva;
} tour_t;

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

/* The evidence column, the two buttons and the card under the table.
 * The Panel comes before the two lines on it. */
static bento_status_t add_side(tour_t *tour)
{
    static const widget_spec_t side_widgets[W_COUNT] = {
        {UI_WIDGET_LABEL, "sensors: ใครตอบ ใครปฏิเสธ", 24, 24, AUTO,
         AUTO, COLOR_THEME, 24, 0, 0},
        {UI_WIDGET_LABEL, "คีย์ที่ snapshot คืนมา: ...", 456, 62, AUTO, AUTO,
         COLOR_ACCENT, 20, 0, 0},
        {UI_WIDGET_LABEL, "read_all = snapshot: ...", 456, 88, AUTO,
         AUTO, COLOR_ACCENT, 16, 0, 0},
        {UI_WIDGET_LABEL, "auto_status: ...", 456, 112, AUTO, AUTO,
         COLOR_ACCENT, 16, 0, 0},
        {UI_WIDGET_BUTTON, "ทดสอบอีกครั้ง", 456, 152, 232, 88,
         COLOR_ACCENT, 20, 0, 0},
        {UI_WIDGET_BUTTON, "ออก", 456, 272, 232, 88, COLOR_DIM, 20, 0,
         0},
        {UI_WIDGET_PANEL, NULL, 24, 306, 408, 72, COLOR_THEME, 0, 0, 0},
        {UI_WIDGET_LABEL, "แถวส้มไม่ใช่ความผิดพลาดของเรา", 40, 310, AUTO,
         AUTO, COLOR_NO, 20, 0, 0},
        {UI_WIDGET_LABEL, "ปฏิเสธเสียงดัง ดีกว่าบอร์ดค้างเงียบ", 40, 338, AUTO,
         AUTO, COLOR_NO, 20, 0, 0}};
    bento_status_t status = add_widget(tour->ui, &side_widgets[W_TITLE],
                                       &tour->ids[W_TITLE]);
    bento_ui_widget_args_t args;
    char text[TEXT_BYTES];
    int32_t i;

    /* nine rows close together; the column ends at x = 440, so the
     * verdicts are short and the full sentences go to the console */
    for (i = 0; (i < ROW_COUNT) && (status == BENTO_OK); i++) {
        (void)snprintf(text, sizeof(text), "%s  ...", NAMES[i]);
        bento_ui_widget_args_init(&args);
        args.text = text;
        args.text_len = (uint32_t)strlen(text);
        args.x = ROW_X;
        args.y = (int16_t)(ROW_Y + (i * ROW_PITCH));
        args.color = COLOR_DIM;
        args.font_px = FONT_ROW_PX;
        status = bento_ui_label_create(tour->ui, &args, &tour->rows[i]);
    }
    if (status == BENTO_OK) {
        status =
            add_widgets(tour->ui, &side_widgets[W_KEYS],
                        (uint32_t)W_COUNT - 1U, &tour->ids[W_KEYS]);
    }
    return status;
}

static void verdict(const tour_t *tour, int32_t row, bool ok,
                    const char *note)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%s  %s", NAMES[row], note);
    show_text(tour->ui, tour->rows[row], text);
    (void)bento_ui_set_color(tour->ui, tour->rows[row],
                             ok ? (uint32_t)COLOR_OK
                                : (uint32_t)COLOR_NO);
}

/* The name of a status, for the console. */
static const char *status_name(bento_status_t status)
{
    static const char *const names[] = {"OK",
                                        "E_INVALID_ARGUMENT",
                                        "E_NO_RESOURCE",
                                        "E_BAD_TYPE",
                                        "E_BAD_HANDLE",
                                        "E_FAILED",
                                        "E_TRANSPORT",
                                        "E_NOT_READY",
                                        "E_STALE",
                                        "E_SESSION",
                                        "E_UNSUPPORTED",
                                        "E_BUSY",
                                        "E_TIMEOUT",
                                        "E_IO",
                                        "E_NO_MEMORY",
                                        "E_NOT_FOUND",
                                        "E_PERMISSION",
                                        "E_CANCELLED",
                                        "E_IN_PROGRESS",
                                        "E_INVALID_TEXT"};
    const int32_t index = -(int32_t)status;
    const char *name = "E_UNKNOWN";

    if ((index >= 0) &&
        ((size_t)index < (sizeof(names) / sizeof(names[0])))) {
        name = names[index];
    }
    return name;
}

/* What the board said, in pieces: lcd.print cuts a line at 126 bytes
 * silently (modlcd.c), and this text is ASCII. */
static void board_said(const char *prefix, bento_status_t status,
                       const char *reason)
{
    char message[TEXT_BYTES * 2];
    char piece[CONSOLE_PIECE + 1];
    size_t length;
    size_t at = 0U;

    (void)snprintf(message, sizeof(message), "%s%s%s%s", prefix,
                   status_name(status), (reason[0] != '\0') ? ": " : "",
                   reason);
    length = strlen(message);
    while (at < length) {
        const size_t left = length - at;
        const size_t take = (left < (size_t)CONSOLE_PIECE)
                                ? left
                                : (size_t)CONSOLE_PIECE;

        (void)memcpy(piece, &message[at], take);
        piece[take] = '\0';
        (void)bento_lcd_print(piece);
        at += take;
    }
}

/* The keys a dict has, as bits in sorted() order. */
static uint32_t snapshot_keys(const bento_sensors_snapshot_t *snap)
{
    uint32_t keys = 0U;

    keys |= (snap->has_bmi270 != 0U) ? (uint32_t)KEY_BMI270 : 0U;
    keys |= (snap->has_capsense != 0U) ? (uint32_t)KEY_CAPSENSE : 0U;
    keys |= (snap->has_pot != 0U) ? (uint32_t)KEY_POT : 0U;
    return keys;
}

static uint32_t all_keys(const bento_sensors_all_t *all)
{
    uint32_t keys = 0U;

    keys |= (all->has_bmi270 != 0U) ? (uint32_t)KEY_BMI270 : 0U;
    keys |= (all->has_bmm350 != 0U) ? (uint32_t)KEY_BMM350 : 0U;
    keys |= (all->has_capsense != 0U) ? (uint32_t)KEY_CAPSENSE : 0U;
    keys |= (all->has_dps368 != 0U) ? (uint32_t)KEY_DPS368 : 0U;
    keys |= (all->has_lis3dh != 0U) ? (uint32_t)KEY_LIS3DH : 0U;
    keys |= (all->has_ltr303 != 0U) ? (uint32_t)KEY_LTR303 : 0U;
    keys |= (all->has_pot != 0U) ? (uint32_t)KEY_POT : 0U;
    keys |= (all->has_sht40 != 0U) ? (uint32_t)KEY_SHT40 : 0U;
    return keys;
}

/* ",".join(sorted(keys)), and how many there are. */
static uint32_t join_keys(uint32_t keys, char *text, size_t text_bytes)
{
    static const char *const key_names[KEY_COUNT] = {
        "bmi270", "bmm350", "capsense", "dps368",
        "lis3dh", "ltr303", "pot",      "sht40"};
    uint32_t count = 0U;
    uint32_t i;

    text[0] = '\0';
    for (i = 0U; i < (uint32_t)KEY_COUNT; i++) {
        if ((keys & ((uint32_t)1U << i)) != 0U) {
            const size_t used = strlen(text);

            (void)snprintf(&text[used], text_bytes - used, "%s%s",
                           (count == 0U) ? "" : ",", key_names[i]);
            count++;
        }
    }
    return count;
}

/* 1) snapshot(): the main path on both boards, what the display core
 * holds, over IPC. Its first answer after a reset can take a while,
 * and it refuses until then, so the refusal is always caught. A build
 * without it is the original's AttributeError. */
static uint32_t try_snapshot(const tour_t *tour, bool *have)
{
    bento_sensors_snapshot_t snap;
    char keys[TEXT_BYTES];
    char text[TEXT_BYTES];
    char reason[BENTO_SENSORS_REASON_MAX + 1U];
    uint32_t length = 0U;
    uint32_t mask = 0U;
    const bento_status_t status =
        bento_sensors_snapshot(BENTO_SENSORS_WAIT_MAX_MS, &snap);

    *have = status == BENTO_OK;
    if (*have) {
        mask = snapshot_keys(&snap);
        (void)snprintf(text, sizeof(text), "OK %" PRIu32 " กลุ่ม",
                       join_keys(mask, keys, sizeof(keys)));
        verdict(tour, ROW_SNAPSHOT, true, text);
        /* a label that is too long runs off the screen without an
         * error, so it is cut here, with "..." to show it */
        (void)snprintf(text, sizeof(text), "คีย์: %.24s%s", keys,
                       (strlen(keys) > (size_t)KEYS_SHOWN) ? "..."
                                                           : "");
        show_text(tour->ui, tour->ids[W_KEYS], text);
        (void)snprintf(text, sizeof(text), "คีย์ที่ snapshot คืนมาครบชุด: %s",
                       keys);
        (void)bento_lcd_print(text);
    } else if (status == BENTO_E_UNSUPPORTED) {
        verdict(tour, ROW_SNAPSHOT, false, "ไม่มีชื่อนี้ในเฟิร์มแวร์รุ่นนี้");
    } else {
        verdict(tour, ROW_SNAPSHOT, false, "OSError - ดูคอนโซล");
        if (bento_sensors_snapshot_why(reason, sizeof(reason),
                                       &length) != BENTO_OK) {
            reason[0] = '\0';
        }
        board_said("snapshot(): ", status, reason);
    }
    return mask;
}

/* 2) read_all(): on the Eva Kit it calls snapshot() and gives the same
 * dict; it reads no bus and adds nothing. */
static void try_read_all(const tour_t *tour, bool have_snapshot,
                         uint32_t snapshot_mask)
{
    bento_sensors_all_t all;
    char keys[TEXT_BYTES];
    char text[TEXT_BYTES];
    uint32_t mask;

    (void)memset(&all, 0, sizeof(all));
    all.struct_size = (uint32_t)sizeof(all);
    if (bento_sensors_read_all(&all) == BENTO_OK) {
        mask = all_keys(&all);
        (void)snprintf(text, sizeof(text), "OK %" PRIu32 " กลุ่ม",
                       join_keys(mask, keys, sizeof(keys)));
        verdict(tour, ROW_READ_ALL, true, text);
        if (have_snapshot) {
            (void)snprintf(text, sizeof(text),
                           "read_all = snapshot: %s",
                           (mask == snapshot_mask) ? "True" : "False");
            show_text(tour->ui, tour->ids[W_SAME], text);
        }
    } else {
        verdict(tour, ROW_READ_ALL, false, "OSError");
    }
}

/* 3) auto_status() is not refused; the board's own numbers are read
 * here rather than guessed. 4) auto_rate(ms) only sets the pace of a
 * background task that a board refusing auto() never starts: callable
 * does not mean it has an effect. */
static void try_auto_status(const tour_t *tour)
{
    bento_sensors_auto_status_t state;
    char text[TEXT_BYTES];

    bento_sensors_auto_status_init(&state);
    if (bento_sensors_auto_status(&state) == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "OK running=%s",
                       (state.running != 0U) ? "True" : "False");
        verdict(tour, ROW_AUTO_STATUS, true, text);
        (void)snprintf(text, sizeof(text),
                       "auto_status: %" PRIu32 " ms, push %" PRIu32,
                       state.rate_ms, state.push_count);
        show_text(tour->ui, tour->ids[W_AUTO_STATUS], text);
    } else {
        verdict(tour, ROW_AUTO_STATUS, false, "OSError");
    }
    if (bento_sensors_auto_set_rate(AUTO_RATE_MS) == BENTO_OK) {
        verdict(tour, ROW_AUTO_RATE, true,
                "OK (มีผลต่อเมื่อ auto() เปิดได้)");
    } else {
        verdict(tour, ROW_AUTO_RATE, false, "OSError");
    }
}

/* A refusal: an orange row, and the board's answer on the console. */
static void refused(const tour_t *tour, int32_t row,
                    bento_status_t status)
{
    char prefix[TEXT_BYTES];

    verdict(tour, row, false, "OSError - บอร์ดปฏิเสธ (ดูคอนโซล)");
    (void)snprintf(prefix, sizeof(prefix), "%s: ", NAMES[row]);
    board_said(prefix, status, "");
}

/* 5-6) The names the two boards answer differently: called for real,
 * and what comes back is reported, not guessed. */
static void try_init_scan(const tour_t *tour)
{
    uint8_t addresses[BENTO_SENSORS_SCAN_MAX];
    char text[TEXT_BYTES];
    uint32_t failed = 0U;
    uint32_t found = 0U;
    bento_status_t status = bento_sensors_init(&failed);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "ผ่าน คืน %s",
                       (failed == 0U) ? "True" : "False");
        verdict(tour, ROW_INIT, true, text);
    } else {
        refused(tour, ROW_INIT, status);
    }
    status =
        bento_sensors_scan(addresses, BENTO_SENSORS_SCAN_MAX, &found);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "ผ่าน พบ %" PRIu32 " ที่อยู่",
                       found);
        verdict(tour, ROW_SCAN, true, text);
    } else {
        refused(tour, ROW_SCAN, status);
    }
}

/* 7-9) Called only on the Eva Kit, where the firmware closes these
 * three (modsensors.c). Elsewhere push() sends real data, live_push()
 * loops until Ctrl+C, and auto() leaves a background task running. */
static void try_eva_only(const tour_t *tour)
{
    uint32_t sent = 0U;
    bento_status_t status;

    if (tour->is_eva) {
        status = bento_sensors_push(&sent);
        if (status == BENTO_OK) {
            verdict(tour, ROW_PUSH, true, "ผ่าน");
        } else {
            refused(tour, ROW_PUSH, status);
        }
        status =
            bento_sensors_auto_start(); /* live_push() starts auto */
        if (status == BENTO_OK) {
            verdict(tour, ROW_LIVE_PUSH, true, "ผ่าน");
        } else {
            refused(tour, ROW_LIVE_PUSH, status);
        }
        status = bento_sensors_auto_start();
        if (status == BENTO_OK) {
            verdict(tour, ROW_AUTO, true, "ผ่าน");
        } else {
            refused(tour, ROW_AUTO, status);
        }
    } else {
        verdict(tour, ROW_PUSH, false, "ข้าม - บอร์ดนี้ส่งค่าจริง");
        verdict(tour, ROW_LIVE_PUSH, false,
                "ข้าม - บอร์ดนี้จะวนจนกด Ctrl+C");
        verdict(tour, ROW_AUTO, false, "ข้าม - บอร์ดนี้จะเปิด task ค้างไว้");
    }
}

static bento_real_t real_nearbyint(bento_real_t value)
{
#if BENTO_FMT_REAL_DOUBLE
    return nearbyint(value);
#else
    return nearbyintf(value);
#endif
}

/* str(round(value, digits)): nearbyint(value * 10**digits) / 10**digits,
 * as MicroPython's round() does. */
static void rounded_text(float value, int32_t digits, char *text,
                         uint32_t text_bytes)
{
    const bento_real_t ten = (bento_real_t)10.0;
    bento_real_t scale = (bento_real_t)1.0;
    bento_real_t rounded;
    uint32_t length = 0U;
    int32_t i;

    for (i = 0; i < digits; i++) {
        scale = scale * ten;
    }
    rounded = real_nearbyint((bento_real_t)value * scale) / scale;
    if (bento_fmt_real(text, text_bytes, rounded, &length) !=
        BENTO_OK) {
        text[0] = '\0';
    }
}

static const char *bool_text(uint8_t value)
{
    return (value != 0U) ? "True" : "False";
}

/* The knob line; a failed read ends the readings. */
static bento_status_t print_knob(void)
{
    char percent_text[BENTO_FMT_BUF_SIZE];
    char volt_text[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint16_t raw = 0U;
    float percent = 0.0F;
    float volts = 0.0F;
    bento_status_t status = bento_sensors_pot_read(&raw);

    if (status == BENTO_OK) {
        status = bento_sensors_pot_percent(&percent);
    }
    if (status == BENTO_OK) {
        status = bento_sensors_pot_voltage(&volts);
    }
    if (status == BENTO_OK) {
        rounded_text(percent, PERCENT_DIGITS, percent_text,
                     sizeof(percent_text));
        rounded_text(volts, VOLT_DIGITS, volt_text, sizeof(volt_text));
        (void)snprintf(text, sizeof(text), "pot: %u | %s %% | %s V",
                       (unsigned int)raw, percent_text, volt_text);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* What still works as usual, to show nothing was lost. */
static void print_readings(void)
{
    char text[TEXT_BYTES];
    uint8_t first = 0U;
    uint8_t second = 0U;
    uint8_t slider = 0U;
    bento_status_t status = print_knob();

    if (status == BENTO_OK) {
        status = bento_sensors_capsense_buttons(&first, &second);
    }
    if (status == BENTO_OK) {
        status = bento_sensors_capsense_slider(&slider);
    }
    if (status == BENTO_OK) {
        (void)snprintf(
            text, sizeof(text), "capsense: btn0 %s btn1 %s slider %u",
            bool_text(first), bool_text(second), (unsigned int)slider);
        (void)bento_lcd_print(text);
        status = bento_sensors_capsense_read(&first, &second, &slider);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "capsense.read(): {'btn0': %s, 'btn1': %s, "
                       "'slider': %u}",
                       bool_text(first), bool_text(second),
                       (unsigned int)slider);
        (void)bento_lcd_print(text);
    } else {
        (void)bento_lcd_print("เซนเซอร์ยังไม่พร้อม ลองกดทดสอบอีกครั้ง");
    }
}

static void run_tour(const tour_t *tour)
{
    bool have_snapshot = false;
    const uint32_t snapshot_mask = try_snapshot(tour, &have_snapshot);

    try_read_all(tour, have_snapshot, snapshot_mask);
    try_auto_status(tour);
    try_init_scan(tour);
    try_eva_only(tour);
    print_readings();
}

/* The board's own name, asked directly: "PSoC Edge Eval Kit" or "PSoC
 * Edge AI Dev Kit". Since snapshot() exists on the Dev Kit too
 * (2026-09-16), it no longer tells the boards apart. */
static bento_status_t read_board(tour_t *tour)
{
    bento_gpio_board_info_t info;
    char text[TEXT_BYTES];
    bento_status_t status;

    bento_gpio_board_info_init(&info);
    status = bento_gpio_board_info(&info);
    if (status == BENTO_OK) {
        const char *name = (info.name != NULL) ? info.name : "";

        tour->is_eva = strstr(name, "Eval") != NULL;
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ทัวร์โมดูล sensors</h2>");
        (void)snprintf(text, sizeof(text), "บอร์ด: %s | Eva Kit: %s",
                       name, tour->is_eva ? "True" : "False");
        (void)bento_lcd_print(text);
    }
    return status;
}

/* The original reads only ev['handle'], so any kind of event on a
 * button counts. A failed poll answers no events. */
static bool handle_events(const tour_t *tour)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    bool running = true;
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(tour->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (events[i].id == tour->ids[W_AGAIN]) {
            run_tour(tour);
        } else if (events[i].id == tour->ids[W_EXIT]) {
            running = false;
        } else {
            /* another widget: nothing to do */
        }
    }
    return running;
}

int main(void)
{
    tour_t tour;
    bool running = true;
    bento_status_t status;

    (void)memset(&tour, 0, sizeof(tour));
    tour.ui = bento_runtime_get_ui();
    status = read_board(&tour);
    if (status == BENTO_OK) {
        status = bento_ui_screen(tour.ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                                 BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    }
    if (status == BENTO_OK) {
        status = add_side(&tour);
    }
    if (status == BENTO_OK) {
        run_tour(&tour);
    }
    while (running && (status == BENTO_OK)) {
        running = handle_events(&tour);
        status = pause_ms(LOOP_MS);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_clear(tour.ui);
        (void)printf("ชื่อที่ถูกปฏิเสธ ไม่ได้หายไปจากโมดูล "
                     "มันยังอยู่และยังเรียกได้\n");
        (void)printf("สิ่งที่เปลี่ยนคือมันตอบว่า 'ไม่' แทนที่จะพาบอร์ดไปค้าง - "
                     "จะกี่ชื่อ ให้บอร์ดเป็นคนบอก\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
