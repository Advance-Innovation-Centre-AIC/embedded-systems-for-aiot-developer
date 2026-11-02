/*
 * 11_two_fields_one_keyboard.c -- ฟอร์มสองช่อง แป้นพิมพ์เดียว
 * (Two fields, one keyboard)
 *
 * แตะช่องซ้ายแล้วพิมพ์ชื่อวง แตะช่องขวาแล้วพิมพ์รหัสผ่าน แป้นพิมพ์บน
 * จออันเดียวผูกตามช่องที่ถูกแตะ (เหตุการณ์ focused) กดตกลงแล้วบรรทัด
 * ล่างรายงานความยาวของสิ่งที่อ่านกลับมาได้ ไม่ใช่ตัวรหัสเอง ปุ่ม
 * "เชื่อมต่อ" ลองต่อ WiFi ด้วยค่าที่พิมพ์ บรรทัด log แสดงสามเหตุการณ์
 * ล่าสุด ทำงาน 60 วินาทีแล้วจบ
 *
 * Tap the left field and type the network name, tap the right one
 * and type the password: the one on-screen keyboard follows the
 * tapped field (the focused event). Press OK and the bottom line
 * reports the lengths read back, never the password itself. The
 * connect button tries Wi-Fi with what was typed. The log line shows
 * the last three events. The program runs for 60 seconds.
 *
 * Twin of 11_two_fields_one_keyboard.py
 * (embedded-systems-for-aiot-developer/examples/s09/11_two_fields_one_keyboard.py),
 * merged from the library twin aiot-s09-11-two-fields-one-keyboard (the course file names fewer constants; it runs the same). Style:
 * CLEAN_C.md.
 * Differs from the original: wifi.connect() is
 * bento_wifi_connect_start and its poll. The binding's lines
 * "[WiFi] Connecting..." and "[WiFi] Connected! IP=..." are written
 * by the program here; its per-attempt and "not found" lines are
 * not, the C API does not report them. A name or password the C
 * module refuses (over 32 bytes, or a password of 1 to 7 bytes)
 * ends as "ต่อไม่ได้" at once; the binding cut or tried it first.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    RUN_MS = 60000,  /* how long the demo runs */
    LOOP_MS = 50,    /* ask the screen this often */
    WIFI_SLICE_MS = 50
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_OK = 0x30A46C
};

/* Sizes. */
enum {
    LOG_KEEP = 3,        /* the log line keeps the last 3 events */
    FIELD_BYTES = 240,   /* a GET_TEXT answers at most 239 bytes */
    LINE_BYTES = 280,    /* a field's text and a few words around it */
    LOG_ENTRY_BYTES = 40 /* "<any uint32_t>:long_pressed_repeat " */
};

/* One event in the log line. */
typedef struct {
    uint32_t handle;
    uint8_t kind;
} log_entry_t;

/* The widgets the program changes, and the log of events. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ssid;
    bento_ui_id_t password;
    bento_ui_id_t keyboard;
    bento_ui_id_t focus;
    bento_ui_id_t read_back;
    bento_ui_id_t state;
    bento_ui_id_t connect;
    bento_ui_id_t log;
    log_entry_t entries[LOG_KEEP];
    uint32_t entry_count;
} form_t;

/* The two typed values, as read back from the fields. */
typedef struct {
    char ssid[FIELD_BYTES];
    char password[FIELD_BYTES];
} typed_t;

static bento_status_t check_running(bento_status_t status)
{
    bento_status_t result = status;

    if ((result == BENTO_OK) && !bento_runtime_is_running()) {
        result = BENTO_E_CANCELLED;
    }
    return result;
}

/* Python's len(): characters, not bytes. */
static int32_t utf8_length(const char *text)
{
    const uint8_t top_bits = 0xC0U;     /* marks a UTF-8 byte's kind */
    const uint8_t continuation = 0x80U; /* 10xxxxxx: not a first byte */
    int32_t count = 0;
    size_t i;

    for (i = 0U; text[i] != '\0'; i++) {
        if (((uint8_t)text[i] & top_bits) != continuation) {
            count++;
        }
    }
    return count;
}

/* The name poll() gives an event kind, for the log line only: the
 * program itself compares the kinds as numbers. */
static const char *kind_text(uint8_t kind)
{
    static const char *const names[] = {
        "unknown",    "clicked",      "value_changed",
        "toggled",    "pressed",      "released",
        "press_lost", "long_pressed", "long_pressed_repeat",
        "ready",      "cancel",       "focused",
        "defocused",  "scroll_begin", "scroll_end",
        "gesture"};
    const uint32_t count = (uint32_t)(sizeof(names) / sizeof(names[0]));

    return ((uint32_t)kind < count) ? names[kind] : names[0];
}

/* The field this id names, or BENTO_UI_ID_INVALID. */
static bento_ui_id_t field_of(const form_t *form, bento_ui_id_t id)
{
    bento_ui_id_t field = BENTO_UI_ID_INVALID;

    if ((id == form->ssid) || (id == form->password)) {
        field = id;
    }
    return field;
}

static bool is_kind(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

/* wifi.ip(): "0.0.0.0" when there is no address. */
static void read_ip(char *text, uint32_t capacity)
{
    uint32_t address = 0U;
    uint32_t length = 0U;

    if ((bento_wifi_ip(BENTO_WIFI_IF_ANY, &address) != BENTO_OK) ||
        (bento_wifi_ipv4_format(address, text, capacity, &length) !=
         BENTO_OK)) {
        (void)snprintf(text, capacity, "0.0.0.0");
    }
}

/* Asks the started join every WIFI_SLICE_MS until it ends. The
 * poll itself answers BENTO_E_CANCELLED on Stop. */
static bento_status_t wait_connect(void)
{
    bento_status_t status = BENTO_OK;
    bool waiting = true;

    while (waiting) {
        status = bento_wifi_connect_poll();
        waiting = status == BENTO_E_IN_PROGRESS;
        if (waiting) {
            status = check_running(bento_time_sleep_ms(WIFI_SLICE_MS));
            waiting = status == BENTO_OK;
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* answers BENTO_OK always */
    }
    return status;
}

/* wifi.connect() with what was typed. The original turns every
 * OSError into False, so only Stop is an error here. The binding
 * saves the network after a join; the twin asks for that too. */
static bento_status_t connect_wifi(const typed_t *typed, bool *joined)
{
    bento_wifi_connect_params_t params;
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = typed->ssid;
    params.ssid_len = (uint32_t)strlen(typed->ssid);
    params.pass = typed->password;
    params.pass_len = (uint32_t)strlen(typed->password);
    params.flags = BENTO_WIFI_F_SAVE_CREDS;
    (void)printf("[WiFi] Connecting...\n");
    status = bento_wifi_connect_start(&params);
    if (status == BENTO_OK) {
        status = wait_connect();
    }
    *joined = status == BENTO_OK;
    if (*joined) {
        read_ip(address, sizeof(address));
        (void)printf("[WiFi] Connected! IP=%s\n", address);
    }
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* Layout on the 792 x 398 canvas. */
enum {
    FONT_PX = 20,
    FONT_TITLE_PX = 28,
    FONT_FOCUS_PX = 24,
    LEFT_X = 24,
    TITLE_Y = 8,
    CAPTION_Y = 48,
    FIELD_Y = 72,
    FIELD_H = 88,
    SSID_W = 328,
    PASS_X = 384,
    PASS_W = 304,
    KEYBOARD_Y = 176,
    KEYBOARD_W = 440,
    KEYBOARD_H = 192,
    SIDE_X = 496, /* the column right of the keyboard */
    FOCUS_Y = 176,
    READ_Y = 216,
    STATE_Y = 248,
    BUTTON_Y = 280,
    BUTTON_W = 192,
    BUTTON_H = 88,
    LOG_Y = 368
};

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show(const form_t *form, bento_ui_id_t label,
                 const char *text, uint32_t color)
{
    (void)bento_ui_set_text(form->ui, label, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_color(form->ui, label, color);
}

static void place(bento_ui_widget_args_t *args, int16_t x, int16_t y,
                  uint32_t color)
{
    bento_ui_widget_args_init(args);
    args->x = x;
    args->y = y;
    args->color = color;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const bento_ui_widget_args_t *where,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args = *where;

    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_caption(bento_ui_t *ui, const char *text,
                                  int16_t x)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;

    place(&args, x, CAPTION_Y, COLOR_DIM);
    return add_label(ui, text, &args, FONT_PX, &unused);
}

/* An empty one-line field that tells the program when it is
 * tapped. "pressed" too: seeing pressed without focused says the
 * finger reached the widget but LVGL sent no focus. */
static bento_status_t add_field(bento_ui_t *ui, int16_t x, int16_t w,
                                bento_ui_id_t *out_id)
{
    const uint32_t events =
        (uint32_t)BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_FOCUSED) |
        (uint32_t)BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_PRESSED);
    bento_ui_widget_args_t args;
    bento_status_t status;

    place(&args, x, FIELD_Y, COLOR_TEXT);
    args.text = "";
    args.text_len = 0U;
    args.w = w;
    args.h = FIELD_H;
    status = bento_ui_textarea_create(ui, &args, out_id);
    if (status == BENTO_OK) {
        /* listen() ignores its result (modui.c) */
        (void)bento_ui_listen_compat(ui, *out_id, events);
    }
    return status;
}

static bento_status_t add_fields(form_t *form)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    place(&args, LEFT_X, TITLE_Y, COLOR_TEXT);
    status = add_label(form->ui, "ฟอร์มสองช่อง แป้นพิมพ์เดียว", &args,
                       FONT_TITLE_PX, &unused);
    if (status == BENTO_OK) {
        status = add_caption(form->ui, "ชื่อวง", LEFT_X);
    }
    if (status == BENTO_OK) {
        status = add_field(form->ui, LEFT_X, SSID_W, &form->ssid);
    }
    if (status == BENTO_OK) {
        status = add_caption(form->ui, "รหัสผ่าน", PASS_X);
    }
    if (status == BENTO_OK) {
        status = add_field(form->ui, PASS_X, PASS_W, &form->password);
    }
    if (status == BENTO_OK) { /* stars: a neighbour must not read it */
        (void)bento_ui_set_prop(form->ui, form->password,
                                BENTO_UI_PROP_PASSWORD, 1);
    }
    return status;
}

/* One keyboard, bound to the left field first. ready comes with
 * OK, cancel when the keyboard is closed. Both calls ignore their
 * result in MicroPython (modui.c). */
static bento_status_t add_keyboard(form_t *form)
{
    const uint32_t events =
        (uint32_t)BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_READY) |
        (uint32_t)BENTO_UI_EVENT_MASK(BENTO_UI_EVENT_CANCEL);
    bento_ui_widget_args_t args;
    bento_status_t status;

    place(&args, LEFT_X, KEYBOARD_Y, COLOR_TEXT);
    args.w = KEYBOARD_W;
    args.h = KEYBOARD_H;
    status = bento_ui_keyboard_create(form->ui, &args, &form->keyboard);
    if (status == BENTO_OK) {
        (void)bento_ui_bind_textarea(form->ui, form->keyboard,
                                     form->ssid);
        (void)bento_ui_listen_compat(form->ui, form->keyboard, events);
    }
    return status;
}

static bento_status_t add_side(form_t *form)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    place(&args, SIDE_X, FOCUS_Y, COLOR_ACCENT);
    status = add_label(form->ui, "กำลังแก้: ชื่อวง", &args, FONT_FOCUS_PX,
                       &form->focus);
    if (status == BENTO_OK) {
        place(&args, SIDE_X, READ_Y, COLOR_DIM);
        status = add_label(form->ui, "ยังไม่ได้อ่านค่ากลับ", &args, FONT_PX,
                           &form->read_back);
    }
    if (status == BENTO_OK) {
        place(&args, SIDE_X, STATE_Y, COLOR_DIM);
        status = add_label(form->ui, "กดตกลงเมื่อพิมพ์ครบ", &args, FONT_PX,
                           &form->state);
    }
    if (status == BENTO_OK) {
        place(&args, SIDE_X, BUTTON_Y, COLOR_ACCENT);
        args.text = "เชื่อมต่อ";
        args.text_len = (uint32_t)strlen(args.text);
        args.w = BUTTON_W;
        args.h = BUTTON_H;
        args.font_px = FONT_FOCUS_PX;
        status =
            bento_ui_button_create(form->ui, &args, &form->connect);
    }
    return status;
}

/* The log line exists because on a real board the keys once went
 * to the wrong field and the emulator could not show why: the
 * board itself must report every event as it arrives. */
static bento_status_t build_screen(form_t *form)
{
    bento_ui_widget_args_t args;
    bento_status_t status =
        bento_ui_screen(form->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = check_running(bento_time_sleep_ms(SETTLE_MS));
    }
    if (status == BENTO_OK) {
        status = add_fields(form);
    }
    if (status == BENTO_OK) {
        status = add_keyboard(form);
    }
    if (status == BENTO_OK) {
        status = add_side(form);
    }
    if (status == BENTO_OK) {
        place(&args, LEFT_X, LOG_Y, COLOR_DIM);
        status = add_label(form->ui, "log: ยังไม่มีเหตุการณ์", &args,
                           FONT_PX, &form->log);
    }
    return status;
}

/* Keeps the last LOG_KEEP events and shows them, oldest first. */
static void log_event(form_t *form, uint32_t handle, uint8_t kind)
{
    char line[LOG_KEEP * LOG_ENTRY_BYTES];
    size_t used;
    uint32_t i;

    if (form->entry_count == (uint32_t)LOG_KEEP) {
        for (i = 1U; i < (uint32_t)LOG_KEEP; i++) {
            form->entries[i - 1U] = form->entries[i];
        }
        form->entry_count--;
    }
    form->entries[form->entry_count].handle = handle;
    form->entries[form->entry_count].kind = kind;
    form->entry_count++;
    (void)snprintf(line, sizeof(line), "log");
    for (i = 0U; i < form->entry_count; i++) {
        used = strlen(line);
        (void)snprintf(&line[used], sizeof(line) - used, " %lu:%s",
                       (unsigned long)form->entries[i].handle,
                       kind_text(form->entries[i].kind));
    }
    (void)bento_ui_set_text(form->ui, form->log, line,
                            (uint32_t)strlen(line));
    (void)printf("EV %lu %s\n", (unsigned long)handle, kind_text(kind));
}

/* A field's text; "" when the read fails, as .text() answers. Text
 * that is not UTF-8 raised UnicodeError there and ends the run. */
static bento_status_t read_field(const form_t *form,
                                 bento_ui_id_t field, char *text)
{
    bento_status_t status =
        bento_ui_get_text(form->ui, field, text, FIELD_BYTES, NULL);

    if (status != BENTO_UI_E_INVALID_TEXT) {
        if (status != BENTO_OK) {
            text[0] = '\0';
        }
        status = BENTO_OK;
    }
    return status;
}

/* Reports how much was read back, never the text: the console is a
 * screen the whole room sees. */
static bento_status_t report(const form_t *form, typed_t *typed)
{
    char line[LINE_BYTES];
    int32_t ssid_chars;
    int32_t password_chars;
    bento_status_t status = read_field(form, form->ssid, typed->ssid);

    if (status == BENTO_OK) {
        status = read_field(form, form->password, typed->password);
    }
    if (status == BENTO_OK) {
        ssid_chars = utf8_length(typed->ssid);
        password_chars = utf8_length(typed->password);
        (void)snprintf(line, sizeof(line),
                       "อ่านกลับได้ %d และ %d "
                       "ตัวอักษร",
                       (int)ssid_chars, (int)password_chars);
        show(form, form->read_back, line,
             (typed->ssid[0] != '\0') ? COLOR_TEXT : COLOR_DIM);
        (void)snprintf(line, sizeof(line),
                       "อ่านกลับ: ชื่อวง %d ตัว รหัส %d "
                       "ตัว",
                       (int)ssid_chars, (int)password_chars);
        (void)bento_lcd_print(line); /* ignores a console not ready */
    }
    return status;
}

/* ui.poll() here only lets the label reach the screen before the
 * join blocks; its events are dropped, as in the original. */
static void flush_screen(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static void show_join(const form_t *form, bool joined)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    char line[LINE_BYTES];

    if (joined) {
        read_ip(address, sizeof(address));
        (void)snprintf(line, sizeof(line), "ต่อได้ ip %s", address);
        show(form, form->state, line, COLOR_OK);
        read_ip(address, sizeof(address));
        (void)snprintf(line, sizeof(line),
                       "<span class=ok>ต่อสำเร็จ</span> %s", address);
        (void)bento_lcd_print(line); /* ignores a console not ready */
    } else {
        show(form, form->state, "ต่อไม่ได้ - ตรวจชื่อวงและรหัส", COLOR_WARN);
        (void)bento_lcd_print("<span class=warn>ต่อไม่สำเร็จ</span>");
    }
}

/* The one keyboard follows the finger: this bind is the whole
 * mechanism. */
static void on_focus(const form_t *form, bento_ui_id_t field)
{
    const bool is_ssid = field == form->ssid;

    (void)bento_ui_bind_textarea(form->ui, form->keyboard, field);
    show(form, form->focus,
         is_ssid ? "กำลังแก้: ชื่อวง"
                 : "กำลังแก้: "
                   "รหัสผ่าน",
         COLOR_ACCENT);
}

/* OK on the keyboard, or Enter in a one-line field: typing is
 * done, so read back what was typed. */
static bento_status_t on_ready(const form_t *form)
{
    typed_t typed;

    show(form, form->state, "พิมพ์เสร็จแล้ว", COLOR_OK);
    return report(form, &typed);
}

/* The connect button: read back, then join with what was typed. */
static bento_status_t on_connect(const form_t *form)
{
    typed_t typed;
    char line[LINE_BYTES];
    bool joined = false;
    bento_status_t status = report(form, &typed);

    if ((status == BENTO_OK) && (typed.ssid[0] == '\0')) {
        show(form, form->state, "ยังไม่ได้พิมพ์ชื่อวง", COLOR_WARN);
    } else if (status == BENTO_OK) {
        (void)snprintf(line, sizeof(line), "กำลังต่อ %s", typed.ssid);
        show(form, form->state, line, COLOR_WARN);
        flush_screen(form->ui);
        status = connect_wifi(&typed, &joined);
        if (status == BENTO_OK) {
            show_join(form, joined);
        }
    } else {
        /* the read-back failed: its status ends the run */
    }
    return status;
}

static bento_status_t handle_event(form_t *form,
                                   const bento_ui_event_t *event)
{
    const bento_ui_id_t field = field_of(form, event->id);
    bento_status_t status = BENTO_OK;

    log_event(form, bento_ui_id_to_handle(event->id), event->type);
    if (is_kind(event, BENTO_UI_EVENT_FOCUSED) &&
        (field != BENTO_UI_ID_INVALID)) {
        on_focus(form, field);
    } else if (is_kind(event, BENTO_UI_EVENT_READY)) {
        status = on_ready(form);
    } else if (is_kind(event, BENTO_UI_EVENT_CANCEL)) {
        show(form, form->state, "ปิดแป้นพิมพ์ - ยังไม่ได้ยืนยัน", COLOR_WARN);
    } else if (is_kind(event, BENTO_UI_EVENT_CLICKED) &&
               (event->id == form->connect)) {
        status = on_connect(form);
    } else {
        /* pressed and defocused are only logged */
    }
    return status;
}

/* Reads the screen every LOOP_MS until RUN_MS has passed or Stop. */
static bento_status_t run_loop(form_t *form)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        /* ui.poll() answers no events when it fails */
        if (bento_ui_poll(form->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                          &count, NULL) != BENTO_OK) {
            count = 0U;
        }
        for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
            status = handle_event(form, &events[i]);
        }
        if (status == BENTO_OK) {
            status = check_running(bento_time_sleep_ms(LOOP_MS));
        }
    }
    return status;
}

/* lcd calls ignore a console that is not ready (modlcd.c). */
static void print_intro(void)
{
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>ฟอร์มสองช่อง แป้นพิมพ์เดียว</h2>");
    (void)bento_lcd_print("focused บอกว่าแตะช่องไหน - "
                          "แป้นพิมพ์ผูกตามไปเอง");
    (void)bento_lcd_print("ready บอกว่าพิมพ์เสร็จ - .text() "
                          "อ่านค่ากลับมาได้");
}

int main(void)
{
    form_t form = {0};
    bento_status_t status;

    form.ui = bento_runtime_get_ui();
    status = build_screen(&form);
    if (status == BENTO_OK) {
        print_intro();
        status = run_loop(&form);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_text(form.ui, form.state, "หมดเวลา",
                                (uint32_t)strlen("หมดเวลา"));
        (void)printf("สองช่อง แป้นพิมพ์เดียว - focused ผูกให้ ready"
                     " บอกว่าเสร็จ\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
