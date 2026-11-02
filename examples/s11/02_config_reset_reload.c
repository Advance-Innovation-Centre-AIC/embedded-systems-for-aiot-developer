/*
 * 02_config_reset_reload.c -- ล้างค่าตั้ง กับ ย้อนค่าตั้ง เป็นคนละเรื่องกัน
 *
 * สองฟังก์ชันที่ชื่อคล้ายกันแต่ผลต่างกันมาก config_reset() ล้างของจริงกลับเป็น
 * ค่าโรงงานทั้ง 19 คีย์ ส่วน config_reload() อ่านไฟล์ตั้งค่าจากแฟลชขึ้นมาทับของที่
 * แก้ค้างไว้ จอแสดงสามจังหวะเรียงลงมา ค่า device_id เปลี่ยนให้เห็นทีละขั้น
 * ไฟล์นี้ล้างค่าตั้งของบอร์ดจริง จบไฟล์แล้วตั้งค่ากลับให้ ถ้ากดหยุดกลางทาง
 * ต้องตั้ง device_id / api_key / mqtt_pass ใหม่เอง
 *
 * Two functions with look-alike names and very different effects:
 * config_reset() wipes all 19 keys back to factory values, config_reload()
 * reads the settings file from flash over the edits in memory. The screen
 * shows the steps one under another, device_id changing at each. This
 * wipes the real board settings and sets them back at the end; if you stop
 * it half way, set device_id, api_key and mqtt_pass again yourself.
 *
 * Twin of 02_config_reset_reload.py
 * (embedded-systems-for-aiot-developer/examples/s11/02_config_reset_reload.py),
 * from the library twin aiot-s11-02-config-reset-reload. Style: CLEAN_C.md.
 * What C changes: tesaiot.config() is a dict there; here device_id and the
 * empty-key count are read field by field (integers come back as decimal
 * text, never empty, as an int is never "" in Python). config_reset()
 * returns None there, so its text says None; config_set() and
 * config_reload() answer True for BENTO_OK and False for BENTO_E_FAILED,
 * any other status ends the program. A Stop seen after a poll, a widget
 * or a sleep ends the program with 0, where MicroPython raises
 * KeyboardInterrupt.
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
    TEXT_BYTES = 256,
    VALUE_BYTES = BENTO_TESAIOT_CONFIG_VALUE_MAX + 1U,
    KEY_BYTES = BENTO_TESAIOT_CONFIG_KEY_MAX + 1U,
    SETTLE_MS = 200,
    BEFORE_MS = 1200,
    RESET_MS = 1500,
    RELOAD_MS = 800
};
enum { /* the four identity keys set back after the wipe */
    SAVED_DEVICE_ID,
    SAVED_API_KEY,
    SAVED_BROKER,
    SAVED_SNI,
    IDENTITY_KEYS
};
enum { /* layout */
    TITLE_X = 20,
    TITLE_Y = 12,
    CARD_X = 20,
    CARD_W = 652,
    STEP_CARD_Y = 52,
    STEP_CARD_H = 152,
    NOTE_CARD_Y = 216,
    NOTE_CARD_H = 112,
    INNER_X = 40,
    STEP_Y = 68,
    LINE_A_Y = 100,
    LINE_B_Y = 132,
    LINE_C_Y = 164,
    NOTE_1_Y = 232,
    NOTE_2_Y = 262,
    NOTE_3_Y = 290,
    FOOTER_Y = 338
};
enum { /* fonts and card shape */
    FONT_TITLE = 24,
    FONT_TEXT = 20,
    CARD_RADIUS = 12,
    CARD_BORDER = 1
};
enum { /* the course palette */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t step;
    bento_ui_id_t line_a;
    bento_ui_id_t line_b;
    bento_ui_id_t line_c;
    bento_ui_id_t note_1;
    bento_ui_id_t note_2;
    bento_ui_id_t note_3;
    char after_set[VALUE_BYTES];
} reset_screen_t;

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

/* A Stop arrives as a cleared running flag; it ends the program where
 * MicroPython raises KeyboardInterrupt. */
static bento_status_t stop_check(bento_status_t status)
{
    return ((status == BENTO_OK) && !bento_runtime_is_running())
               ? BENTO_E_CANCELLED
               : status;
}

static bento_status_t pause_ms(uint32_t wait_ms)
{
    return stop_check(bento_time_sleep_ms(wait_ms));
}

/* ui.poll() answers [] on any failure, so only a Stop counts. */
static bento_status_t poll_screen(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    const bento_status_t polled = bento_ui_poll(
        ui, events, BENTO_UI_POLL_EVENTS_MAX, &count, NULL);

    return (polled == BENTO_UI_E_CANCELLED) ? BENTO_E_CANCELLED
                                            : stop_check(BENTO_OK);
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
    return stop_check(bento_ui_label_create(ui, &args, id));
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
    return stop_check(bento_ui_panel_create(ui, &args, &id));
}

/* A setter's refusal is ignored, as in MicroPython (modui.c). */
static void show(bento_ui_t *ui, bento_ui_id_t id, const char *text,
                 uint32_t color)
{
    (void)bento_ui_set_color(ui, id, color);
    (void)bento_ui_set_text(ui, id, text, (uint32_t)strlen(text));
}

static bento_status_t add_step_card(reset_screen_t *screen)
{
    const rect_t card = {CARD_X, STEP_CARD_Y, CARD_W, STEP_CARD_H};
    const rect_t step = {INNER_X, STEP_Y, 0, 0};
    const rect_t line_a = {INNER_X, LINE_A_Y, 0, 0};
    const rect_t line_b = {INNER_X, LINE_B_Y, 0, 0};
    const rect_t line_c = {INNER_X, LINE_C_Y, 0, 0};
    bento_status_t status = add_card(screen->ui, &card);

    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "จังหวะที่ 1 - อ่านของเดิม", &step,
                      (uint32_t)COLOR_WARN, FONT_TEXT, &screen->step);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "1) ก่อนแตะอะไร      -", &line_a,
                      (uint32_t)COLOR_DIM, FONT_TEXT, &screen->line_a);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "2) หลัง reset()     -", &line_b,
                      (uint32_t)COLOR_DIM, FONT_TEXT, &screen->line_b);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "3) หลังตั้งใหม่     -", &line_c,
                      (uint32_t)COLOR_DIM, FONT_TEXT, &screen->line_c);
    }
    return status;
}

static bento_status_t add_note_card(reset_screen_t *screen)
{
    const rect_t card = {CARD_X, NOTE_CARD_Y, CARD_W, NOTE_CARD_H};
    const rect_t note_1 = {INNER_X, NOTE_1_Y, 0, 0};
    const rect_t note_2 = {INNER_X, NOTE_2_Y, 0, 0};
    const rect_t note_3 = {INNER_X, NOTE_3_Y, 0, 0};
    const rect_t footer = {TITLE_X, FOOTER_Y, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status = add_card(screen->ui, &card);

    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "-", &note_1, (uint32_t)COLOR_DIM,
                      FONT_TEXT, &screen->note_1);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "-", &note_2, (uint32_t)COLOR_DIM,
                      FONT_TEXT, &screen->note_2);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "-", &note_3, (uint32_t)COLOR_DIM,
                      FONT_TEXT, &screen->note_3);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "reset ล้างของจริง - reload แค่ย้อนกลับ",
                      &footer, (uint32_t)COLOR_DIM, FONT_TEXT, &unused);
    }
    return status;
}

static bento_status_t build_screen(reset_screen_t *screen)
{
    const rect_t title = {TITLE_X, TITLE_Y, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status;

    /* MicroPython ignores a refused ui.screen(): the runtime already set it up */
    (void)bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms((uint32_t)SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ล้างค่าตั้ง กับ ย้อนค่าตั้ง", &title,
                           (uint32_t)COLOR_TEXT, FONT_TITLE, &unused);
    }
    if (status == BENTO_OK) {
        status = add_step_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_note_card(screen);
    }
    if (status == BENTO_OK) {
        status = poll_screen(screen->ui);
    }
    return status;
}

/* The current device_id; an empty one reads "(empty)", so nobody takes a
 * blank for a frozen screen. */
static bento_status_t read_device_id(char *out, uint32_t out_bytes)
{
    uint32_t length = 0U;
    const bento_status_t status = bento_tesaiot_config_get_field(
        "device_id", (uint32_t)strlen("device_id"), 0U, out, out_bytes,
        &length);

    if ((status == BENTO_OK) && (length == 0U)) {
        (void)snprintf(out, (size_t)out_bytes, "%s", "(ว่าง)");
    }
    return status;
}

/* True or False as the binding answers; any other status is returned. */
static bento_status_t set_config(const char *key, const char *value,
                                 bool *saved)
{
    bento_status_t status = bento_tesaiot_config_set(
        key, (uint32_t)strlen(key), value, (uint32_t)strlen(value));

    *saved = (status == BENTO_OK);
    if (status == BENTO_E_FAILED) {
        status = BENTO_OK; /* False: an unknown key or a failed save */
    }
    return status;
}

static bento_status_t set_identity(bool saved[IDENTITY_KEYS])
{
    /* the team's identity: change these three to your own before running */
    static const char device_id[] = "team03";
    static const char api_key[] = "<api key ของทีม>";
    static const char broker[] = "mqtt.tesaiot.dev";
    bento_status_t status =
        set_config("device_id", device_id, &saved[SAVED_DEVICE_ID]);

    if (status == BENTO_OK) {
        status = set_config("api_key", api_key, &saved[SAVED_API_KEY]);
    }
    if (status == BENTO_OK) {
        status = set_config("broker", broker, &saved[SAVED_BROKER]);
    }
    if (status == BENTO_OK) {
        status = set_config("sni_hostname", broker, &saved[SAVED_SNI]);
    }
    return status;
}

static bento_status_t step_before(const reset_screen_t *screen)
{
    char before[VALUE_BYTES];
    char text[TEXT_BYTES];
    bento_status_t status = read_device_id(before, sizeof(before));

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "1) ก่อนแตะอะไร      %s",
                       before);
        show(screen->ui, screen->line_a, text, (uint32_t)COLOR_TEXT);
        status = poll_screen(screen->ui);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "ก่อนแตะอะไร device_id = %s",
                       before);
        (void)bento_lcd_print(
            text); /* lcd.print() ignores a console not ready */
        (void)printf("ก่อนแตะอะไร device_id = %s\n", before);
        status = pause_ms((uint32_t)BEFORE_MS);
    }
    return status;
}

/* After the wipe every other key is gone too: count the empty ones. */
static bento_status_t count_empty_keys(uint32_t *key_count,
                                       int32_t *empty)
{
    char key[KEY_BYTES];
    char value[VALUE_BYTES];
    uint32_t key_length = 0U;
    uint32_t value_length = 0U;
    uint32_t i;
    bento_status_t status = bento_tesaiot_config_key_count(key_count);

    *empty = 0;
    for (i = 0U; (status == BENTO_OK) && (i < *key_count); i++) {
        status = bento_tesaiot_config_key_at(i, key, sizeof(key),
                                             &key_length);
        if (status == BENTO_OK) {
            status = bento_tesaiot_config_get_field(
                key, key_length, BENTO_TESAIOT_CONFIG_REVEAL_SECRET,
                value, sizeof(value), &value_length);
        }
        if ((status == BENTO_OK) && (value_length == 0U)) {
            *empty += 1;
        }
    }
    return status;
}

/* config_reset() returns None, not True, so the text prints what it is. */
static bento_status_t step_reset(const reset_screen_t *screen)
{
    char after_reset[VALUE_BYTES];
    char text[TEXT_BYTES];
    uint32_t key_count = 0U;
    int32_t empty = 0;
    bento_status_t status;

    show(screen->ui, screen->step,
         "จังหวะที่ 2 - config_reset() ล้างทั้ง 19 คีย์", (uint32_t)COLOR_BAD);
    status = poll_screen(screen->ui);
    if (status == BENTO_OK) {
        status = bento_tesaiot_config_reset(
            BENTO_TESAIOT_CONFIG_RESET_CONFIRM);
    }
    if (status == BENTO_OK) {
        status = read_device_id(after_reset, sizeof(after_reset));
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "2) หลัง reset()     %s",
                       after_reset);
        show(screen->ui, screen->line_b, text, (uint32_t)COLOR_BAD);
        show(screen->ui, screen->note_1,
             "config_reset() คืน None (ไม่ใช่ True)", (uint32_t)COLOR_BAD);
        status = poll_screen(screen->ui);
    }
    if (status == BENTO_OK) {
        (void)snprintf(
            text, sizeof(text),
            "<span class=err>reset แล้ว device_id = %s </span>",
            after_reset);
        (void)bento_lcd_print(text);
        (void)printf("config_reset() คืนค่า None | device_id = %s\n",
                     after_reset);
        status = count_empty_keys(&key_count, &empty);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "จาก %u คีย์ ตอนนี้ว่างไปแล้ว %d คีย์",
                       (unsigned int)key_count, (int)empty);
        show(screen->ui, screen->note_2, text, (uint32_t)COLOR_BAD);
        (void)printf("จาก %u คีย์ ว่างไป %d คีย์\n", (unsigned int)key_count,
                     (int)empty);
        status = pause_ms((uint32_t)RESET_MS);
    }
    return status;
}

/* After a wipe set every value again, not only the one you meant to; a
 * misspelt key answers False quietly, so always look at the answer. */
static bento_status_t step_set_again(reset_screen_t *screen)
{
    char text[TEXT_BYTES];
    bool saved[IDENTITY_KEYS];
    bool typo = false;
    bento_status_t status;

    (void)memset(saved, 0, sizeof(saved));

    show(screen->ui, screen->step, "จังหวะที่ 3 - ตั้งค่ากลับเข้าไปเอง",
         (uint32_t)COLOR_WARN);
    status = poll_screen(screen->ui);
    if (status == BENTO_OK) {
        status = set_identity(saved);
    }
    if (status == BENTO_OK) {
        (void)printf("config_set คืนค่า: %s %s %s %s\n",
                     python_bool(saved[SAVED_DEVICE_ID]),
                     python_bool(saved[SAVED_API_KEY]),
                     python_bool(saved[SAVED_BROKER]),
                     python_bool(saved[SAVED_SNI]));
        status = set_config("devise_id", "ผิดแน่ ๆ", &typo);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "คีย์ที่สะกดผิดคืน %s ไม่โยน error",
                       python_bool(typo));
        (void)bento_ui_set_text(screen->ui, screen->note_3, text,
                                (uint32_t)strlen(text));
        (void)printf("ตั้งคีย์ที่สะกดผิด คืนค่า %s\n", python_bool(typo));
        status = read_device_id(screen->after_set,
                                sizeof(screen->after_set));
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "3) หลังตั้งใหม่     %s",
                       screen->after_set);
        show(screen->ui, screen->line_c, text, (uint32_t)COLOR_OK);
        status = poll_screen(screen->ui);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>ตั้งกลับแล้ว device_id = %s </span>",
                       screen->after_set);
        (void)bento_lcd_print(text);
    }
    return status;
}

/* reload reads the file in flash over the values in memory: anything set
 * but not saved yet is lost here. */
static bento_status_t reload_config(bool *reloaded)
{
    bento_status_t status = bento_tesaiot_config_reload();

    *reloaded = (status == BENTO_OK);
    if (status == BENTO_E_FAILED) {
        status = BENTO_OK;
    }
    return status;
}

static bento_status_t show_reload(const reset_screen_t *screen,
                                  bool reloaded,
                                  const char *after_reload)
{
    char text[TEXT_BYTES];
    bento_status_t status;

    (void)snprintf(text, sizeof(text),
                   "config_reload() คืน %s (เป็น bool จริง)",
                   python_bool(reloaded));
    show(screen->ui, screen->note_1, text, (uint32_t)COLOR_DIM);
    (void)snprintf(text, sizeof(text), "หลัง reload device_id = %s",
                   after_reload);
    show(screen->ui, screen->note_2, text, (uint32_t)COLOR_DIM);
    status = poll_screen(screen->ui);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "reload คืนค่า %s -> device_id = %s",
                       python_bool(reloaded), after_reload);
        (void)bento_lcd_print(text);
        (void)printf("config_reload() คืนค่า %s | device_id = %s\n",
                     python_bool(reloaded), after_reload);
    }
    return status;
}

/* The values just set are gone, because the file in flash is still the
 * old one: set them once more. */
static bento_status_t restore_identity(const reset_screen_t *screen)
{
    char now_id[VALUE_BYTES];
    char text[TEXT_BYTES];
    bool saved[IDENTITY_KEYS];
    bento_status_t status;

    (void)memset(saved, 0, sizeof(saved));

    show(screen->ui, screen->step, "reload ทับค่าที่เพิ่งตั้ง - ต้องตั้งใหม่อีกรอบ",
         (uint32_t)COLOR_BAD);
    status = set_identity(saved);
    if (status == BENTO_OK) {
        status = read_device_id(now_id, sizeof(now_id));
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "ตั้งกลับให้แล้ว ตอนนี้ device_id = %s", now_id);
        (void)bento_ui_set_text(screen->ui, screen->note_3, text,
                                (uint32_t)strlen(text));
    }
    return status;
}

static bento_status_t step_reload(const reset_screen_t *screen)
{
    char after_reload[VALUE_BYTES];
    bool reloaded = false;
    bento_status_t status;

    show(screen->ui, screen->step,
         "จังหวะที่ 4 - config_reload() ดึงไฟล์จากแฟลชมาทับ",
         (uint32_t)COLOR_WARN);
    status = poll_screen(screen->ui);
    if (status == BENTO_OK) {
        status = pause_ms((uint32_t)RELOAD_MS);
    }
    if (status == BENTO_OK) {
        status = reload_config(&reloaded);
    }
    if (status == BENTO_OK) {
        status = read_device_id(after_reload, sizeof(after_reload));
    }
    if (status == BENTO_OK) {
        status = show_reload(screen, reloaded, after_reload);
    }
    if ((status == BENTO_OK) &&
        (strcmp(after_reload, screen->after_set) != 0)) {
        status = restore_identity(screen);
    } else if (status == BENTO_OK) {
        show(screen->ui, screen->step,
             "reload ได้ค่าเดิม - ไฟล์ในแฟลชตรงกับที่ตั้งไว้",
             (uint32_t)COLOR_OK);
    } else {
        /* the status chain already ends the program */
    }
    if (status == BENTO_OK) {
        status = poll_screen(screen->ui);
    }
    return status;
}

static void print_summary(void)
{
    (void)printf("\n");
    (void)printf("สรุปความต่างสองตัวนี้:\n");
    (void)printf("  config_reset()  ล้างของจริง คืน None  "
                 "ใช้ตอนตั้งค่ามั่วจนไม่รู้เหลืออะไร\n");
    (void)printf("  config_reload() ย้อนกลับ    คืน bool  "
                 "ใช้ตอนอยากทิ้งการแก้ที่ยังไม่พอใจ\n");
}

int main(void)
{
    reset_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>คาบ 11 - reset กับ reload</h2>");
        status = step_before(&screen);
    }
    if (status == BENTO_OK) {
        status = step_reset(&screen);
    }
    if (status == BENTO_OK) {
        status = step_set_again(&screen);
    }
    if (status == BENTO_OK) {
        status = step_reload(&screen);
    }
    if (status == BENTO_OK) {
        print_summary();
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
