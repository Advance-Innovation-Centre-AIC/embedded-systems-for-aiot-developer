/*
 * s04_touch_panel.c -- แผงควบคุม LED บนจอสัมผัส
 *
 * วิธีรัน: บนจอบอร์ด แตะการ์ด Playground ค้างไว้ (widget เกิดบนหน้านี้เท่านั้น)
 * แล้วกด Program to Device และมองสองที่พร้อมกัน: จอ กับ หลอด LED จริง
 * ซ้ายคือสามแถว แถวละหนึ่งสี มีไฟสถานะ ปุ่มเปิด และปุ่มปิด แยกกันคนละปุ่ม
 * ขวาคือตัวเลขสรุปกับคำสั่งทั้งชุด "ปิดทั้งหมด" ถามยืนยันก่อนเสมอ
 * ทุกเส้นทางที่เปลี่ยนไฟผ่านประตูเดียวคือ set_led() จอกับไฟจริงจึงตรงกันเสมอ
 *
 * A touch panel for three colour LEDs. Each row has a status lamp, an
 * "on" button and a separate "off" button, so pressing one ten times
 * gives the same result as pressing it once. The right card shows how
 * many LEDs are on and holds the all-on / all-off commands; all-off
 * asks for a confirmation first. Every path that changes an LED goes
 * through set_led(), so the screen and the real LEDs always agree.
 *
 * Twin of s04_touch_panel.py
 * (embedded-systems-for-aiot-developer/solution_codes/s04_touch_panel.py).
 * Style: CLEAN_C.md.
 * Differs from the original only where C must:
 *   - gpio.board_info()["led_names"] is read one name at a time with
 *     bento_gpio_led_name(); a name that cannot be read counts as
 *     missing, so the panel falls back to LEDs 0, 1, 2 as the original
 *     does on a board without the three RGB names.
 *   - Stop ends the loop with BENTO_E_CANCELLED and main() returns 0,
 *     where MicroPython raises KeyboardInterrupt. Like the original, the
 *     LEDs are left as they are.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_runtime.h"

enum { /* the panel's three colours */
    LED_COUNT = 3
};

enum { /* layout, px */
    TITLE_X = 24,
    TITLE_Y = 8,
    STATUS_X = 360,
    STATUS_Y = 12,
    ROW_TOP = 52,    /* the first row's top edge */
    ROW_PITCH = 120, /* a button 88 high + 32 between touch targets */
    LAMP_X = 24,
    LAMP_DY = 20,
    LAMP_SIZE = 48,
    NAME_X = 88,
    NAME_DY = 32,
    ON_X = 184,
    OFF_X = 360,
    ROW_BUTTON_W = 144,
    BUTTON_H = 88,
    CARD_X = 520,
    CARD_Y = 48,
    CARD_W = 248,
    CARD_H = 308,
    CARD_RADIUS = 12,
    CARD_INNER_X = 536,
    COUNT_Y = 60,
    SET_LABEL_Y = 96,
    ALL_ON_Y = 132,
    ALL_OFF_Y = 252,
    ALL_W = 216,
    BOX_X = 48,
    BOX_Y = 88,
    BOX_W = 496,
    BOX_H = 160,
    ANSWER_X = 568,
    YES_Y = 88,
    NO_Y = 208,
    ANSWER_W = 152
};

enum { /* font sizes, px */
    FONT_TITLE = 28,
    FONT_VALUE = 20,
    FONT_SMALL = 16
};

enum {              /* times, ms */
    CLEAR_MS = 200, /* let CM55 finish clearing before new commands */
    LOOP_MS = 50
};

enum { /* colours, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_GO = 0x30A46C,
    COLOR_STOP = 0x3A4150
};

enum { /* "ติดอยู่ " + any int32_t + " จาก " + any int32_t + " ดวง" */
    TEXT_BYTES = 96
};

static const char *const BUTTON_TEXT[LED_COUNT] = {"แดง", "เขียว",
                                                   "น้ำเงิน"};
static const uint32_t COLOR_ON[LED_COUNT] = {0xE53935U, 0x43A047U,
                                             0x1E88E5U};

/* The panel's widgets and its truth: led_on is what the program
 * commanded, never read back from the pins. */
typedef struct {
    bento_ui_t *ui;
    uint32_t led_index[LED_COUNT];
    bool led_on[LED_COUNT];
    bento_ui_id_t lamps[LED_COUNT];
    bento_ui_id_t on_buttons[LED_COUNT];
    uint32_t on_count;
    bento_ui_id_t off_buttons[LED_COUNT];
    uint32_t off_count;
    bento_ui_id_t status;
    bento_ui_id_t count_label;
    bento_ui_id_t all_on;
    bento_ui_id_t all_off;
    bento_ui_id_t box;
    bento_ui_id_t yes;
    bento_ui_id_t no;
    bool asking;
} panel_t;

/* A widget box with no text. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

/* A position or size on the screen: the widget arguments are 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

/* A setter's refusal was ignored by MicroPython too (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static void set_shown(bento_ui_t *ui, bento_ui_id_t widget, bool shown)
{
    (void)bento_ui_set_visible(ui, widget, shown ? 1U : 0U);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const rect_t *place, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 const rect_t *place, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.w = place->w;
    args.h = place->h;
    args.color = color;
    args.font_px = FONT_VALUE;
    return bento_ui_button_create(ui, &args, out_id);
}

/* The board's LED numbered by this name, or false when it has none. */
static bool find_led(const char *wanted, uint32_t *led)
{
    const uint32_t count = bento_gpio_num_leds();
    const char *name = NULL;
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < count) && !found; i++) {
        if ((bento_gpio_led_name(i, &name) == BENTO_OK) &&
            (name != NULL) && (strcmp(name, wanted) == 0)) {
            *led = i;
            found = true;
        }
    }
    return found;
}

/* Names where the board reports all three (Dev Kit), else 0, 1, 2
 * (Eva Kit: LED 2 is blue although it is called "RGB_RED"). */
static void pick_leds(panel_t *panel)
{
    static const char *const RGB_NAMES[LED_COUNT] = {
        "RGB_RED", "RGB_GREEN", "RGB_BLUE"};
    uint32_t found[LED_COUNT] = {0U};
    bool all_found = true;
    uint32_t i;

    for (i = 0U; i < (uint32_t)LED_COUNT; i++) {
        all_found = find_led(RGB_NAMES[i], &found[i]) && all_found;
    }
    for (i = 0U; i < (uint32_t)LED_COUNT; i++) {
        panel->led_index[i] = all_found ? found[i] : i;
    }
}

static bento_status_t add_title(panel_t *panel)
{
    const rect_t title = {TITLE_X, TITLE_Y, 0, 0};
    const rect_t status_place = {STATUS_X, STATUS_Y, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(panel->ui, "แผงควบคุม LED ของทีม", &title, COLOR_TEXT,
                  FONT_TITLE, &unused);

    if (status == BENTO_OK) {
        status = add_label(panel->ui, "พร้อมรับคำสั่ง", &status_place,
                           COLOR_DIM, FONT_VALUE, &panel->status);
    }
    return status;
}

/* One row: the lamp tells what is, the buttons tell what can be done. */
static bento_status_t add_row(panel_t *panel, uint32_t row)
{
    const int32_t top = ROW_TOP + ((int32_t)row * ROW_PITCH);
    const rect_t name_place = {NAME_X, px(top + NAME_DY), 0, 0};
    const rect_t on_place = {ON_X, px(top), ROW_BUTTON_W, BUTTON_H};
    const rect_t off_place = {OFF_X, px(top), ROW_BUTTON_W, BUTTON_H};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = LAMP_X;
    args.y = px(top + LAMP_DY);
    args.w = LAMP_SIZE;
    args.h = LAMP_SIZE;
    args.color = COLOR_ON[row];
    args.font_px = 0;
    status = bento_ui_led_create(panel->ui, &args, &panel->lamps[row]);
    if (status == BENTO_OK) {
        status = add_label(panel->ui, BUTTON_TEXT[row], &name_place,
                           COLOR_DIM, FONT_SMALL, &unused);
    }
    if (status == BENTO_OK) {
        status = add_button(panel->ui, "เปิด", &on_place, COLOR_GO,
                            &panel->on_buttons[panel->on_count]);
    }
    if (status == BENTO_OK) {
        panel->on_count++;
        status = add_button(panel->ui, "ปิด", &off_place, COLOR_STOP,
                            &panel->off_buttons[panel->off_count]);
    }
    if (status == BENTO_OK) {
        panel->off_count++;
    }
    return status;
}

/* The card on the right: the count and the commands for the whole set. */
static bento_status_t add_set_card(panel_t *panel)
{
    char text[TEXT_BYTES];
    const rect_t count_place = {CARD_INNER_X, COUNT_Y, 0, 0};
    const rect_t set_place = {CARD_INNER_X, SET_LABEL_Y, 0, 0};
    const rect_t all_on_place = {CARD_INNER_X, ALL_ON_Y, ALL_W,
                                 BUTTON_H};
    const rect_t all_off_place = {CARD_INNER_X, ALL_OFF_Y, ALL_W,
                                  BUTTON_H};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CARD_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM;
    args.max_val = CARD_RADIUS;
    args.font_px = 1;
    status = bento_ui_panel_create(panel->ui, &args, &unused);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "ติดอยู่ 0 จาก %d ดวง",
                       (int)LED_COUNT);
        status = add_label(panel->ui, text, &count_place, COLOR_TEXT,
                           FONT_VALUE, &panel->count_label);
    }
    if (status == BENTO_OK) {
        status = add_label(panel->ui, "คำสั่งทั้งชุด", &set_place, COLOR_DIM,
                           FONT_SMALL, &unused);
    }
    if (status == BENTO_OK) {
        status = add_button(panel->ui, "เปิดทั้งหมด", &all_on_place,
                            COLOR_GO, &panel->all_on);
    }
    if (status == BENTO_OK) {
        status = add_button(panel->ui, "ปิดทั้งหมด", &all_off_place,
                            COLOR_STOP, &panel->all_off);
    }
    return status;
}

/* The confirmation says what will happen. The MsgBox's own buttons send
 * no events, so two real buttons are the answer; all start hidden. */
static bento_status_t add_confirm(panel_t *panel)
{
    const rect_t yes_place = {ANSWER_X, YES_Y, ANSWER_W, BUTTON_H};
    const rect_t no_place = {ANSWER_X, NO_Y, ANSWER_W, BUTTON_H};
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "ปิดทั้งหมด\nไฟทุกสีจะดับพร้อมกัน";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = BOX_X;
    args.y = BOX_Y;
    args.w = BOX_W;
    args.h = BOX_H;
    args.color = COLOR_CARD;
    status = bento_ui_msgbox_create(panel->ui, &args, &panel->box);
    if (status == BENTO_OK) {
        status = add_button(panel->ui, "ปิดทั้งหมด", &yes_place,
                            COLOR_STOP, &panel->yes);
    }
    if (status == BENTO_OK) {
        status = add_button(panel->ui, "ไม่ปิด", &no_place, COLOR_STOP,
                            &panel->no);
    }
    if (status == BENTO_OK) {
        set_shown(panel->ui, panel->box, false);
        set_shown(panel->ui, panel->yes, false);
        set_shown(panel->ui, panel->no, false);
    }
    return status;
}

static bento_status_t build_screen(panel_t *panel)
{
    bento_status_t status =
        bento_ui_screen(panel->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    uint32_t row;

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(CLEAR_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_title(panel);
    }
    for (row = 0U; (row < (uint32_t)LED_COUNT) && (status == BENTO_OK);
         row++) {
        status = add_row(panel, row);
    }
    if (status == BENTO_OK) {
        status = add_set_card(panel);
    }
    if (status == BENTO_OK) {
        status = add_confirm(panel);
    }
    return status;
}

/* Reads led_on only, never the hardware, so it is always right. */
static void show_status(const panel_t *panel)
{
    char text[TEXT_BYTES];
    int32_t lit = 0;
    uint32_t i;

    for (i = 0U; i < (uint32_t)LED_COUNT; i++) {
        if (panel->led_on[i]) {
            lit++;
        }
    }
    (void)snprintf(text, sizeof(text), "ติดอยู่ %" PRId32 " จาก %d ดวง",
                   lit, (int)LED_COUNT);
    set_text(panel->ui, panel->count_label, text);
}

/* The one gate that changes an LED: remember, drive the real LED, show
 * it on the lamp, report. A dimmed lamp (value 0) still shows the
 * screen is alive. */
static bento_status_t set_led(panel_t *panel, uint32_t row, bool on)
{
    bento_status_t status;

    panel->led_on[row] = on;
    if (on) {
        status = bento_gpio_led_on(panel->led_index[row]);
    } else {
        status = bento_gpio_led_off(panel->led_index[row]);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_value(panel->ui, panel->lamps[row],
                                 on ? 1 : 0);
        show_status(panel);
    }
    return status;
}

static bento_status_t set_all(panel_t *panel, bool on)
{
    bento_status_t status = BENTO_OK;
    uint32_t row;

    for (row = 0U; (row < (uint32_t)LED_COUNT) && (status == BENTO_OK);
         row++) {
        status = set_led(panel, row, on);
    }
    return status;
}

/* The row whose button this is, or -1. */
static int32_t row_of(const bento_ui_id_t *buttons, uint32_t count,
                      bento_ui_id_t widget)
{
    int32_t row = -1;
    uint32_t i;

    for (i = 0U; (i < count) && (row < 0); i++) {
        if (buttons[i] == widget) {
            row = (int32_t)i;
        }
    }
    return row;
}

static void say_row(const panel_t *panel, const char *verb, int32_t row)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%s%s", verb, BUTTON_TEXT[row]);
    set_text(panel->ui, panel->status, text);
}

static void show_confirm(panel_t *panel, bool shown)
{
    panel->asking = shown;
    set_shown(panel->ui, panel->box, shown);
    set_shown(panel->ui, panel->yes, shown);
    set_shown(panel->ui, panel->no, shown);
}

/* A command that changes several real things at once asks first. */
static bento_status_t handle_set_click(panel_t *panel,
                                       bento_ui_id_t widget)
{
    bento_status_t status = BENTO_OK;

    if (widget == panel->all_on) {
        status = set_all(panel, true);
        if (status == BENTO_OK) {
            set_text(panel->ui, panel->status, "เปิดครบทุกสี");
        }
    } else if ((widget == panel->all_off) && !panel->asking) {
        show_confirm(panel, true);
        set_shown(panel->ui, panel->status, false);
    } else if ((widget == panel->yes) && panel->asking) {
        panel->asking = false;
        status = set_all(panel, false);
        if (status == BENTO_OK) {
            show_confirm(panel, false);
            set_text(panel->ui, panel->status, "ปิดครบทุกสี");
            set_shown(panel->ui, panel->status, true);
        }
    } else if ((widget == panel->no) && panel->asking) {
        show_confirm(panel, false);
        set_shown(panel->ui, panel->status, true);
    } else {
        /* any other widget commands nothing */
    }
    return status;
}

/* "On" commands on, never a toggle: ten presses act like one. */
static bento_status_t handle_click(panel_t *panel, bento_ui_id_t widget)
{
    const int32_t on_row =
        row_of(panel->on_buttons, panel->on_count, widget);
    const int32_t off_row =
        row_of(panel->off_buttons, panel->off_count, widget);
    bento_status_t status = BENTO_OK;

    if (on_row >= 0) {
        status = set_led(panel, (uint32_t)on_row, true);
        if (status == BENTO_OK) {
            say_row(panel, "สั่งเปิด ", on_row);
        }
    } else if (off_row >= 0) {
        status = set_led(panel, (uint32_t)off_row, false);
        if (status == BENTO_OK) {
            say_row(panel, "สั่งปิด ", off_row);
        }
    } else {
        status = handle_set_click(panel, widget);
    }
    return status;
}

/* Poll every round (else the screen hides widgets for about 2 s), act
 * on clicks only, then give the screen LOOP_MS to draw. */
static bento_status_t run_event_loop(panel_t *panel)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        /* ui.poll() answers no events when it fails */
        if (bento_ui_poll(panel->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                          &count, NULL) != BENTO_OK) {
            count = 0U;
        }
        for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
            if (events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) {
                status = handle_click(panel, events[i].id);
            }
        }
        if (status == BENTO_OK) {
            status = bento_time_sleep_ms(LOOP_MS);
        }
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    panel_t panel = {0};
    bento_status_t status;

    panel.ui = bento_runtime_get_ui();
    pick_leds(&panel);
    status = build_screen(&panel);
    if (status == BENTO_OK) {
        /* start from a known state: command every LED off */
        status = set_all(&panel, false);
    }
    if (status == BENTO_OK) {
        panel.asking = false;
        (void)printf(
            "แผงควบคุมพร้อมแล้ว แตะปุ่มบนจอ แล้วมองหลอดไฟจริงบนบอร์ดด้วย\n");
        status = run_event_loop(&panel);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
