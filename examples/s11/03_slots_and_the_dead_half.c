/*
 * 03_slots_and_the_dead_half.c -- ครึ่งที่ตอบทันที กับ ครึ่งที่ต้องมีชิป OPTIGA
 *
 * โมดูล tesaiot มีบางคำสั่งที่ตอบทันที เช่น slots() ซึ่งอ่านตารางชื่อในเฟิร์มแวร์
 * ส่วนอีกสิบหกตัวส่งคำสั่งข้ามไปคอร์จอซึ่งประกอบมาโดยไม่มี OPTIGA จึงล้มเหลว
 * ไฟล์นี้จับเวลาให้ดูกับตา ครึ่งบนของจอคือ slots() ครึ่งล่างบอกว่า license_verify()
 * กับ device_id() คืนอะไรและใช้เวลากี่ ms ใช้เวลารันราว 25 วินาที
 *
 * Some tesaiot calls answer at once: slots() reads a name table in the
 * firmware. Sixteen others go to the display core, built without OPTIGA,
 * and fail there. The program times them: the top half of the screen is
 * slots(), the bottom half shows what license_verify() and device_id()
 * return and how many ms each took. It runs for about 25 seconds.
 *
 * Twin of 03_slots_and_the_dead_half.py
 * (embedded-systems-for-aiot-developer/examples/s11/03_slots_and_the_dead_half.py),
 * from the library twin aiot-s11-03-slots-and-the-dead-half.
 * Style: CLEAN_C.md. What C changes: slots() is a dict there; here
 * bento_tesaiot_slot_count / _slot_at read the same table, and sorted() by
 * slot number is an insertion sort. license_verify() answers False on any
 * failure in MicroPython, so every status but BENTO_OK is False here.
 * device_id() raises OSError "Trust M device ID read failed" on failure;
 * here a failed status takes the except branch and prints that text.
 * In the BENTO Emulator, the TESAIoT platform from C is not available yet: the program says
 * so and ends (every bento_tesaiot_* call answers BENTO_E_UNSUPPORTED there).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_tesaiot.h"
#include "bento_time.h"

enum { /* sizes and times */
    TEXT_BYTES = 192,
    NAME_BYTES = 16,
    SLOT_MAX = BENTO_TESAIOT_SLOT_NAMED,
    SETTLE_MS = 200,
    SLOTS_SHOWN_MS = 1500
};
enum { /* layout */
    TITLE_X = 20,
    TITLE_Y = 12,
    CARD_X = 20,
    CARD_W = 652,
    TOP_CARD_Y = 52,
    TOP_CARD_H = 120,
    BOTTOM_CARD_Y = 188,
    BOTTOM_CARD_H = 140,
    INNER_X = 40,
    CAPTION_Y = 62,
    SEG_X = 248,
    SEG_Y = 84,
    SEG_W = 152,
    SEG_H = 60,
    FAST_Y = 142,
    SLOT_1_X = 408,
    SLOT_1_Y = 84,
    SLOT_ROWS_X = 216,
    SLOT_2_Y = 112,
    SLOT_3_Y = 134,
    DEAD_1_Y = 200,
    DEAD_2_Y = 232,
    DEAD_3_Y = 264,
    VERDICT_Y = 294,
    FOOTER_Y = 338
};
enum { /* fonts and card shape */
    FONT_TITLE = 24,
    FONT_TEXT = 20,
    FONT_SMALL = 16,
    CARD_RADIUS = 12,
    CARD_BORDER = 1
};
enum { /* the course palette */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t digits;
    bento_ui_id_t fast;
    bento_ui_id_t slot_1;
    bento_ui_id_t slot_2;
    bento_ui_id_t slot_3;
    bento_ui_id_t dead_1;
    bento_ui_id_t dead_2;
    bento_ui_id_t dead_3;
    bento_ui_id_t verdict;
} slots_screen_t;

typedef struct {
    char name[SLOT_MAX][NAME_BYTES];
    uint32_t number[SLOT_MAX];
    uint32_t order[SLOT_MAX];
    uint32_t count;
} slot_table_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

static const char *python_bool(bool value)
{
    return value ? "True" : "False";
}

/* ui.poll() answers [] on any failure: its events are dropped. */
static void poll_screen(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t pause_ms(uint32_t wait_ms)
{
    bento_status_t status = bento_time_sleep_ms(wait_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const rect_t *place, uint32_t color,
                                int32_t font_px, bento_ui_id_t *id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, id);
}

static bento_status_t add_card(bento_ui_t *ui, const rect_t *place)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t id;

    bento_ui_widget_args_init(&args);
    args.x = place->x;
    args.y = place->y;
    args.w = place->w;
    args.h = place->h;
    args.color = (uint32_t)COLOR_CARD;
    args.min_val = COLOR_DIM;
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &id);
}

static bento_status_t add_seg7(bento_ui_t *ui, const rect_t *place,
                               bento_ui_id_t *id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0";
    args.text_len = 1U;
    args.x = place->x;
    args.y = place->y;
    args.w = place->w;
    args.h = place->h;
    args.color = (uint32_t)COLOR_INFO;
    return bento_ui_seg7_create(ui, &args, id);
}

/* A setter's refusal is ignored, as in MicroPython (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t id, const char *text)
{
    (void)bento_ui_set_text(ui, id, text, (uint32_t)strlen(text));
}

static void show(bento_ui_t *ui, bento_ui_id_t id, const char *text,
                 uint32_t color)
{
    (void)bento_ui_set_color(ui, id, color);
    set_text(ui, id, text);
}

/* The BENTO Emulator's C runtime has no TESAIoT link yet: slots() is the
 * first bento_tesaiot_* call, and it answers BENTO_E_UNSUPPORTED there. Say
 * so on the bottom card and on the console; the program then ends with 0. */
static void show_emulator_note(const slots_screen_t *screen)
{
    static const char note[] = "TESAIoT จาก C ยังไม่มีใน BENTO "
                               "Emulator\nให้รันบน Dev Kit หรือ Eva Kit";

    show(screen->ui, screen->dead_1, note, (uint32_t)COLOR_WARN);
    show(screen->ui, screen->dead_2, "",
         (uint32_t)COLOR_WARN); /* room for its second line */
    poll_screen(screen->ui);
    (void)printf(
        "TESAIoT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva "
        "Kit / TESAIoT platform from C is not in the BENTO Emulator "
        "yet: run it on a Dev Kit or Eva Kit.\n");
}

/* The top half: slots() answers at once, because it asks no chip. */
static bento_status_t add_top_half(slots_screen_t *screen)
{
    const rect_t card = {CARD_X, TOP_CARD_Y, CARD_W, TOP_CARD_H};
    const rect_t caption = {INNER_X, CAPTION_Y, 0, 0};
    const rect_t digits = {SEG_X, SEG_Y, SEG_W, SEG_H};
    const rect_t fast = {INNER_X, FAST_Y, 0, 0};
    const rect_t slot_1 = {SLOT_1_X, SLOT_1_Y, 0, 0};
    const rect_t slot_2 = {SLOT_ROWS_X, SLOT_2_Y, 0, 0};
    const rect_t slot_3 = {SLOT_ROWS_X, SLOT_3_Y, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status = add_card(screen->ui, &card);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "slots() คืนกี่ช่อง", &caption,
                           (uint32_t)COLOR_DIM, FONT_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg7(screen->ui, &digits, &screen->digits);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "-", &fast, (uint32_t)COLOR_OK,
                           FONT_TEXT, &screen->fast);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "-", &slot_1, (uint32_t)COLOR_TEXT,
                      FONT_SMALL, &screen->slot_1);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "-", &slot_2, (uint32_t)COLOR_TEXT,
                      FONT_SMALL, &screen->slot_2);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "-", &slot_3, (uint32_t)COLOR_TEXT,
                      FONT_TEXT, &screen->slot_3);
    }
    return status;
}

/* The bottom half: two calls of the dead group and the time they cost. */
static bento_status_t add_bottom_half(slots_screen_t *screen)
{
    const rect_t card = {CARD_X, BOTTOM_CARD_Y, CARD_W, BOTTOM_CARD_H};
    const rect_t dead_1 = {INNER_X, DEAD_1_Y, 0, 0};
    const rect_t dead_2 = {INNER_X, DEAD_2_Y, 0, 0};
    const rect_t dead_3 = {INNER_X, DEAD_3_Y, 0, 0};
    const rect_t verdict = {INNER_X, VERDICT_Y, 0, 0};
    const rect_t footer = {TITLE_X, FOOTER_Y, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status = add_card(screen->ui, &card);

    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "license_verify()  รอ...", &dead_1,
                      (uint32_t)COLOR_DIM, FONT_TEXT, &screen->dead_1);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "device_id()       รอ...", &dead_2,
                      (uint32_t)COLOR_DIM, FONT_TEXT, &screen->dead_2);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "รวมเวลาที่เสียไป  -", &dead_3,
                      (uint32_t)COLOR_DIM, FONT_TEXT, &screen->dead_3);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "-", &verdict, (uint32_t)COLOR_DIM,
                      FONT_TEXT, &screen->verdict);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "ชิปมีจริง แต่ต้องเข้าทางโมดูล optiga",
                      &footer, (uint32_t)COLOR_DIM, FONT_TEXT, &unused);
    }
    return status;
}

static bento_status_t build_screen(slots_screen_t *screen)
{
    const rect_t title = {TITLE_X, TITLE_Y, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status;

    /* MicroPython ignores a refused ui.screen(): the runtime already set it up */
    (void)bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms((uint32_t)SETTLE_MS);
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "เก้าตัวที่ตอบ กับ สิบหกตัวที่ต้องมีชิป", &title,
                      (uint32_t)COLOR_TEXT, FONT_TITLE, &unused);
    }
    if (status == BENTO_OK) {
        status = add_top_half(screen);
    }
    if (status == BENTO_OK) {
        status = add_bottom_half(screen);
    }
    return status;
}

/* slots() sends no IPC anywhere: it reads the name table in the firmware. */
static bento_status_t read_slots(slot_table_t *table)
{
    uint32_t name_length = 0U;
    uint32_t i;
    bento_status_t status = bento_tesaiot_slot_count(&table->count);

    if ((status == BENTO_OK) && (table->count > (uint32_t)SLOT_MAX)) {
        status = BENTO_E_NO_RESOURCE;
    }
    for (i = 0U; (status == BENTO_OK) && (i < table->count); i++) {
        status = bento_tesaiot_slot_at(i, table->name[i], NAME_BYTES,
                                       &name_length, &table->number[i]);
    }
    return status;
}

/* The slot numbers are distinct, so the order is fully determined. */
static void sort_by_number(slot_table_t *table)
{
    uint32_t i;

    for (i = 0U; i < table->count; i++) {
        uint32_t j = i;

        while ((j > 0U) && (table->number[table->order[j - 1U]] >
                            table->number[i])) {
            table->order[j] = table->order[j - 1U];
            j--;
        }
        table->order[j] = i;
    }
}

static void show_slots(const slots_screen_t *screen,
                       const slot_table_t *table, int32_t fast_ms)
{
    char text[TEXT_BYTES];
    const uint32_t first = table->order[0];
    const uint32_t second = table->order[1];

    (void)snprintf(text, sizeof(text), "%u",
                   (unsigned int)table->count);
    set_text(screen->ui, screen->digits, text);
    (void)snprintf(text, sizeof(text), "ตอบใน %d ms", (int)fast_ms);
    set_text(screen->ui, screen->fast, text);
    (void)snprintf(text, sizeof(text), "%s = %u", table->name[first],
                   (unsigned int)table->number[first]);
    set_text(screen->ui, screen->slot_1, text);
    (void)snprintf(text, sizeof(text), "%s = %u", table->name[second],
                   (unsigned int)table->number[second]);
    set_text(screen->ui, screen->slot_2, text);
    (void)snprintf(text, sizeof(text), "อีก %d ช่อง ดูในคอนโซล",
                   (int)table->count - 2);
    set_text(screen->ui, screen->slot_3, text);
    poll_screen(screen->ui);
}

static void print_slots(const slot_table_t *table, int32_t fast_ms)
{
    char text[TEXT_BYTES];
    uint32_t i;

    (void)snprintf(text, sizeof(text), "slots() คืน %u ช่อง ใน %d ms",
                   (unsigned int)table->count, (int)fast_ms);
    (void)bento_lcd_print(
        text); /* lcd.print() ignores a console not ready */
    (void)printf("slots() คืน %u ช่อง ใช้เวลา %d ms\n",
                 (unsigned int)table->count, (int)fast_ms);
    for (i = 0U; i < table->count; i++) {
        (void)printf("  %-12s -> ช่อง %u\n",
                     table->name[table->order[i]],
                     (unsigned int)table->number[table->order[i]]);
    }
    /* slot 4 is kept for Protected Update, so no name points at it */
    (void)printf("สังเกตว่าไม่มีชื่อไหนชี้ไปช่อง 4 - ช่องนั้นถูกกันไว้\n");
}

static bento_status_t step_slots(const slots_screen_t *screen)
{
    slot_table_t table;
    const uint32_t start_ms = bento_time_ticks_ms();
    int32_t fast_ms;
    bento_status_t status;

    (void)memset(&table, 0, sizeof(table));
    status = read_slots(&table);
    fast_ms = bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);
    if ((status == BENTO_OK) && (table.count < 2U)) {
        status =
            BENTO_E_INVALID_ARGUMENT; /* names[1] raises IndexError there */
    }
    if (status == BENTO_OK) {
        sort_by_number(&table);
        show_slots(screen, &table, fast_ms);
        print_slots(&table, fast_ms);
        status = pause_ms((uint32_t)SLOTS_SHOWN_MS);
    }
    return status;
}

/* license_verify() returns a bool and never raises. */
static int32_t time_license(const slots_screen_t *screen)
{
    char text[TEXT_BYTES];
    bool verified = false;
    uint32_t start_ms;
    int32_t spent_ms;

    (void)bento_lcd_print(
        "<b>กำลังเรียก license_verify() - จับเวลาอยู่</b>");
    show(screen->ui, screen->dead_1, "license_verify()  กำลังรอ...",
         (uint32_t)COLOR_WARN);
    poll_screen(screen->ui);
    start_ms = bento_time_ticks_ms();
    if (bento_tesaiot_license_verify(&verified) != BENTO_OK) {
        verified = false; /* every failure is False in MicroPython */
    }
    spent_ms = bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);
    (void)snprintf(text, sizeof(text),
                   "license_verify()  คืน %s ใน %d ms",
                   python_bool(verified), (int)spent_ms);
    show(screen->ui, screen->dead_1, text,
         verified ? (uint32_t)COLOR_OK : (uint32_t)COLOR_BAD);
    poll_screen(screen->ui);
    (void)snprintf(text, sizeof(text),
                   "license_verify() คืน %s ใน %d ms",
                   python_bool(verified), (int)spent_ms);
    (void)bento_lcd_print(text);
    (void)printf("license_verify() คืน %s ใช้เวลา %d ms\n",
                 python_bool(verified), (int)spent_ms);
    return spent_ms;
}

/* Calls of this group do not all fail alike: some answer False, some
 * raise, so device_id() needs the except branch or the program dies. */
static int32_t time_device_id(const slots_screen_t *screen)
{
    char text[TEXT_BYTES];
    uint8_t uid[BENTO_TESAIOT_IPC_RESP_MAX];
    uint32_t uid_length = 0U;
    uint32_t start_ms;
    int32_t spent_ms;
    bento_status_t status;

    (void)bento_lcd_print("<b>กำลังเรียก device_id() - จับเวลาอยู่</b>");
    show(screen->ui, screen->dead_2, "device_id()       กำลังรอ...",
         (uint32_t)COLOR_WARN);
    poll_screen(screen->ui);
    start_ms = bento_time_ticks_ms();
    status = bento_tesaiot_device_id(uid, (uint32_t)sizeof(uid),
                                     &uid_length);
    spent_ms = bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "device_id()       ได้ %u ไบต์ ใน %d ms",
                       (unsigned int)uid_length, (int)spent_ms);
        show(screen->ui, screen->dead_2, text, (uint32_t)COLOR_OK);
        (void)printf("device_id() คืนมา %u ไบต์\n",
                     (unsigned int)uid_length);
    } else {
        (void)snprintf(text, sizeof(text),
                       "device_id()       OSError ใน %d ms",
                       (int)spent_ms);
        show(screen->ui, screen->dead_2, text, (uint32_t)COLOR_BAD);
        (void)snprintf(
            text, sizeof(text),
            "<span class=err>device_id() โยน OSError ใน %d ms</span>",
            (int)spent_ms);
        (void)bento_lcd_print(text);
        (void)printf("device_id() โยน OSError หลังรอ %d ms : %s\n",
                     (int)spent_ms, "Trust M device ID read failed");
    }
    return spent_ms;
}

static void print_lesson(int32_t total_ms)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "<b>รวมสองบรรทัดเสียไป %d ms</b>",
                   (int)total_ms);
    (void)bento_lcd_print(text);
    (void)printf("\n");
    (void)printf("รวมสองบรรทัดเสียเวลาไป %d ms\n", (int)total_ms);
    (void)printf(
        "บทเรียน: ไม่ใช่ทุกชื่อที่ import มาได้ จะเรียกได้จริงบนบอร์ดที่เราถืออยู่\n");
    (void)printf(
        "         กลุ่มที่ตายแล้ว: init device_id license_verify health\n");
    (void)printf(
        "                          cred_read cred_write cred_erase\n");
    (void)printf("                          random hash hmac "
                 "aes_keygen encrypt decrypt sign\n");
    (void)printf(
        "                          counter_read counter_inc\n");
    (void)printf("         อยากคุยกับชิป OPTIGA จริง ๆ ให้ใช้โมดูล optiga "
                 "ซึ่งคุยด้วย I2C\n");
    (void)printf(
        "         จาก CM33 ตรง ๆ ไม่ต้องพึ่งคอร์จอ - ดูสไลด์โบนัสท้ายคาบ\n");
}

static void step_dead_half(const slots_screen_t *screen)
{
    char text[TEXT_BYTES];
    const int32_t license_ms = time_license(screen);
    const int32_t total_ms = license_ms + time_device_id(screen);

    (void)snprintf(text, sizeof(text),
                   "รวมเวลาที่เสียไป  %d ms จากสองบรรทัด", (int)total_ms);
    show(screen->ui, screen->dead_3, text, (uint32_t)COLOR_BAD);
    /* 124 bytes, under the label's 126 */
    show(screen->ui, screen->verdict,
         "สิบหกตัวในกลุ่มนี้เป็นแบบนี้ทุกตัว Eva และ Dev Kit",
         (uint32_t)COLOR_BAD);
    poll_screen(screen->ui);
    print_lesson(total_ms);
}

int main(void)
{
    slots_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        poll_screen(screen.ui);
        (void)bento_lcd_clear();
        (void)bento_lcd_console(
            "<h2>คาบ 11 - ครึ่งที่ใช้ได้ กับ ครึ่งที่ตายแล้ว</h2>");
        status = step_slots(&screen);
    }
    if (status == BENTO_E_UNSUPPORTED) {
        show_emulator_note(&screen);
        status =
            BENTO_E_CANCELLED; /* nothing else can run: a quiet end */
    }
    if (status == BENTO_OK) {
        step_dead_half(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
