/*
 * s03_led_button.c -- ไฟวิ่งทุกดวง + ปุ่มนับครั้งแบบกันเด้ง (ฉบับฝึกเติมโค้ด, LED Chase and
 * Debounced Button practice)
 *
 * วิธีรัน: 1) บนจอบอร์ด แตะการ์ด Playground ก่อน
 *          2) เติมหกช่องตามคำใบ้ (TODO) - จุด 1-2 เรื่องไฟ จุด 3-5 เรื่องปุ่ม จุด 6 ปิดท้าย
 *          3) กด Program to Device โปรแกรมรันเอง 30 วินาทีแล้วจบ ไม่ต้องกด RESTART
 * ลูปเดียวทำสองงานคนละจังหวะ โดยไม่ใช้ sleep ยาว ๆ ขวางทาง
 * ดูที่จอ: บนซ้ายไฟ บนขวาปุ่มกับตัวนับ ล่างซ้ายปุ่มสั่งงาน ล่างขวาเวลาที่เหลือ
 *         แผงบนจอเขียนมาให้แล้ว ช่องว่างอยู่ที่ตรรกะ
 * กับดัก: ปุ่มเปิดกับปุ่มปิดต้องแยกกัน ปุ่มสลับบอกไม่ได้ว่าตอนนี้อยู่สถานะไหน
 *
 * The practice skeleton: six blanks (each a TODO naming the C call
 * to use) do nothing until they are filled in, exactly as the .py
 * skeleton's pass lines, so the light stays on LED 1 and no press is
 * counted yet. One loop does two jobs at different paces without a long sleep in
 * the way: a light chases across every board LED every STEP_MS, and
 * the user button is read every POLL_MS and counted only once it has
 * been still for DEBOUNCE_MS. The screen mirrors the LEDs, the button
 * and the count, has Run and Stop buttons (Stop asks to confirm) and
 * counts down the 30 seconds of the run.
 *
 * Twin of s03_led_button.py
 * (embedded-systems-for-aiot-developer/practise_codes/s03_led_button.py).
 * Style: CLEAN_C.md.
 * Differs from the original:
 *   - lcd.print() with several values joins them with spaces itself;
 *     here snprintf builds the same line first.
 *   - gpio.button(0) raises ValueError on a board without a button,
 *     and dividing 408 by NUM_LEDS raises ZeroDivisionError on one without
 *     LEDs; here both end the program with BENTO_E_INVALID_ARGUMENT.
 *   - Stop ends the program at once, without the closing summary
 *     (MicroPython raises KeyboardInterrupt there).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

enum {                /* the four values to play with */
    STEP_MS = 150,    /* the chase's pace */
    DEBOUNCE_MS = 40, /* stillness before a value is believed */
    POLL_MS = 5,      /* how often the loop reads the button */
    RUN_MS = 30000    /* the life of this run */
};

enum { /* other times, ms */
    SETTLE_MS = 200,
    UI_MS =
        100, /* the screen is updated every 100 ms, not every loop round */
    MS_PER_SECOND = 1000
};

enum { /* layout */
    TITLE_X = 24,
    TITLE_Y = 8,
    TITLE_PX = 28,
    STATUS_X = 360,
    STATUS_Y = 12,
    NOTE_PX = 20, /* Thai on the board is 20 px or larger */
    CARD_RADIUS = 12,
    LEFT_X = 24,
    LEFT_W = 440,
    RIGHT_X = 480,
    RIGHT_W = 288,
    TOP_Y = 52,
    TOP_H = 144,
    BOTTOM_Y = 212,
    BOTTOM_H = 160,
    LEFT_TEXT_X = 40,
    RIGHT_TEXT_X = 496,
    TOP_TEXT_Y = 68,
    BOTTOM_TEXT_Y = 228
};

enum { /* the LED card */
    LED_ROW_Y = 104,
    LED_TAG_Y = 112,
    LED_SPACE = 408,  /* the room the LEDs share */
    PITCH_WIDE = 128, /* the widest step between two LEDs */
    LED_WIDE = 48,
    LED_NARROW = 36,
    TAG_GAP_WIDE = 16,
    TAG_GAP_NARROW = 8,
    MIRROR_NOTE_Y = 160,
    TAG_BYTES = 24 /* "ดวง " and any uint32_t */
};

enum { /* the button card */
    BUTTON_LIGHT_SIZE = 48,
    BUTTON_LIGHT_Y = 104,
    PRESSED_Y = 156,
    COUNT_TITLE_X = 596,
    COUNT_TITLE_Y = 100,
    SEG_X = 640,
    SEG_Y = 132,
    SEG_W = 88,
    SEG_H = 56
};

enum { /* the command card: 88 px tall, 32 px apart, the size of a real finger */
    COMMAND_Y = 268,
    RUN_BUTTON_X = 40,
    STOP_BUTTON_X = 248,
    COMMAND_W = 176,
    COMMAND_H = 88,
    BUTTON_PX = 20
};

enum { /* the time card */
    LEFT_LABEL_Y = 264,
    LEFT_LABEL_PX = 24,
    BAR_Y = 308,
    BAR_W = 192,
    BAR_H = 16,
    SCALE_Y = 324,
    SCALE_H = 40,
    SCALE_TICKS = 16,
    SCALE_MAJOR_EVERY = 5
};

enum { /* the confirm box and its two answers */
    BOX_X = 48,
    BOX_Y = 96,
    BOX_W = 496,
    BOX_H = 160,
    ANSWER_X = 568,
    YES_Y = 96,
    NO_Y = 216,
    ANSWER_W = 152,
    ANSWER_H = 88
};

enum { /* colours, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_RUN = 0x4A9EFF,
    COLOR_RUN_BUTTON = 0x30A46C,
    COLOR_IDLE_BUTTON = 0x3A4150
};

enum { /* text buffers */
    LINE_BYTES =
        256, /* a console line before lcd cuts it at 127 bytes */
    LABEL_BYTES = 64,  /* a short label with a number */
    NUMBER_BYTES = 12, /* any int32_t with its sign and NUL */
    USER_BUTTON = 0 /* gpio opens one button on both boards: index 0 */
};

/* A label's colour and size. */
typedef struct {
    uint32_t color;
    int32_t size_px;
} text_style_t;

static const text_style_t NOTE_STYLE = {COLOR_DIM, NOTE_PX};

/* Where a button goes. */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} rect_t;

/* Every widget the loop changes. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t leds[BENTO_GPIO_LED_CAP];
    bento_ui_id_t status;
    bento_ui_id_t button_light;
    bento_ui_id_t count;
    bento_ui_id_t run;
    bento_ui_id_t stop;
    bento_ui_id_t time_left;
    bento_ui_id_t time_bar;
    bento_ui_id_t box;
    bento_ui_id_t yes;
    bento_ui_id_t no;
} panel_t;

/* The program's state: the chase, the debouncer and the clocks. */
typedef struct {
    panel_t panel;
    const char *button_name;
    uint32_t led_count;
    uint32_t
        led_index; /* kept here: led.value() reads the pin, not what was asked */
    int32_t count;
    bool raw;
    bool last_raw;
    bool stable;
    bool chase_on; /* the screen's buttons change this */
    bool asking;   /* waiting for the confirm box */
    uint32_t start_ms;
    uint32_t last_step_ms;
    uint32_t last_change_ms;
    uint32_t last_ui_ms;
    int32_t last_second;
} chase_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static int16_t px(int32_t value)
{
    return (int16_t)value;
}

/* ui.poll() here only keeps the event queue empty. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The widget setters ignore a refusal, as modui.c does. */
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
                                int32_t x, int32_t y,
                                const text_style_t *style,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = px(x);
    args.y = px(y);
    args.color = style->color;
    args.font_px = style->size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_note(bento_ui_t *ui, const char *text,
                               int32_t x, int32_t y)
{
    bento_ui_id_t unused = 0U;

    return add_label(ui, text, x, y, &NOTE_STYLE, &unused);
}

static bento_status_t add_card(bento_ui_t *ui, int32_t x, int32_t y,
                               int32_t w, int32_t h)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    bento_ui_widget_args_init(&args);
    args.x = px(x);
    args.y = px(y);
    args.w = px(w);
    args.h = px(h);
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* the border colour */
    args.max_val = CARD_RADIUS;
    args.font_px = 1; /* the border width */
    return bento_ui_panel_create(ui, &args, &unused);
}

static bento_status_t add_light(bento_ui_t *ui, int32_t x, int32_t y,
                                int32_t size, uint32_t color,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = px(x);
    args.y = px(y);
    args.w = px(size);
    args.h = px(size);
    args.color = color;
    args.font_px = 0;
    return bento_ui_led_create(ui, &args, out_id);
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 const rect_t *place, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = px(place->x);
    args.y = px(place->y);
    args.w = px(place->w);
    args.h = px(place->h);
    args.color = color;
    args.font_px = BUTTON_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* Step 1: ask the board what it has. The button's name comes from the
 * board, not from the print on the circuit board. lcd.print() ignores
 * a console that is not ready (modlcd.c), and so does this. */
static bento_status_t greet(chase_t *chase)
{
    bento_gpio_board_info_t info;
    char line[LINE_BYTES];
    bento_status_t status =
        (bento_gpio_num_buttons() > (uint32_t)USER_BUTTON)
            ? BENTO_OK
            : BENTO_E_INVALID_ARGUMENT;

    chase->led_count = bento_gpio_num_leds();
    if (status == BENTO_OK) {
        status =
            bento_gpio_button_name(USER_BUTTON, &chase->button_name);
    }
    if (status == BENTO_OK) {
        bento_gpio_board_info_init(&info);
        status = bento_gpio_board_info(&info);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>AIoT in Action - คาบ 3</h2>");
        (void)snprintf(line, sizeof(line),
                       "บอร์ด: %s | LED: %" PRIu32 " | ปุ่ม: %" PRIu32,
                       info.name, info.leds, info.buttons);
        (void)bento_lcd_print(line);
        /* two calls: together they pass 127 bytes and would be cut silently */
        (void)bento_lcd_print(
            "<span class=muted>ปุ่มผู้ใช้มีตัวเดียว ดัชนี 0</span>");
        (void)snprintf(line, sizeof(line),
                       "<span class=muted>โค้ดเรียกมันว่า %s</span>",
                       chase->button_name);
        (void)bento_lcd_print(line);
    }
    return status;
}

/* Step 2: switch every LED off first. */
static bento_status_t leds_off(const chase_t *chase)
{
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < chase->led_count) && (status == BENTO_OK); i++) {
        /* TODO จุดที่ 1: เติม status = bento_gpio_led_off(i);
         * on/off สั่งค่าตรง ๆ ส่วน toggle สั่งกลับด้านจากค่าเดิม (examples/s03/02_led_blink.py) */
    }
    return status;
}

/* The step between two LEDs comes from the LED count, so five LEDs
 * still fit the card; 0 when the board has none. */
static int32_t led_pitch(uint32_t led_count)
{
    int32_t pitch = 0;

    if (led_count > 0U) {
        pitch = LED_SPACE / (int32_t)led_count;
        if (pitch > PITCH_WIDE) {
            pitch = PITCH_WIDE;
        }
    }
    return pitch;
}

/* One LED on the screen and its tag beside it. */
static bento_status_t add_led_column(panel_t *panel, uint32_t led,
                                     int32_t pitch)
{
    char tag[TAG_BYTES];
    const bool wide = pitch >= PITCH_WIDE;
    const int32_t led_w = wide ? LED_WIDE : LED_NARROW;
    const int32_t x = LEFT_TEXT_X + ((int32_t)led * pitch);
    bento_status_t status = add_light(panel->ui, x, LED_ROW_Y, led_w,
                                      COLOR_OK, &panel->leds[led]);

    if (status == BENTO_OK) {
        (void)snprintf(tag, sizeof(tag), "%s%" PRIu32,
                       wide ? "ดวง " : "", led + 1U);
        status =
            add_note(panel->ui, tag,
                     x + led_w + (wide ? TAG_GAP_WIDE : TAG_GAP_NARROW),
                     LED_TAG_Y);
    }
    return status;
}

/* The screen's LEDs mirror what the program ordered, not what a pin
 * reads. */
static bento_status_t add_led_card(chase_t *chase)
{
    panel_t *panel = &chase->panel;
    char text[LABEL_BYTES];
    const int32_t pitch = led_pitch(chase->led_count);
    uint32_t i;
    bento_status_t status =
        add_card(panel->ui, LEFT_X, TOP_Y, LEFT_W, TOP_H);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "ไฟบนบอร์ด %" PRIu32 " ดวง",
                       chase->led_count);
        status = add_note(panel->ui, text, LEFT_TEXT_X, TOP_TEXT_Y);
    }
    if ((status == BENTO_OK) && (chase->led_count == 0U)) {
        status =
            BENTO_E_INVALID_ARGUMENT; /* the original divides by the LED count */
    }
    for (i = 0U; (i < chase->led_count) && (status == BENTO_OK); i++) {
        status = add_led_column(panel, i, pitch);
    }
    if (status == BENTO_OK) {
        status = add_note(panel->ui, "จอสะท้อนคำสั่ง ไม่ใช่ค่าที่ขาอ่าน",
                          LEFT_TEXT_X, MIRROR_NOTE_Y);
    }
    return status;
}

/* The real button's state and the count. */
static bento_status_t add_button_card(chase_t *chase)
{
    panel_t *panel = &chase->panel;
    bento_ui_widget_args_t args;
    char text[LABEL_BYTES];
    bento_status_t status =
        add_card(panel->ui, RIGHT_X, TOP_Y, RIGHT_W, TOP_H);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "ปุ่ม %s", chase->button_name);
        status = add_note(panel->ui, text, RIGHT_TEXT_X, TOP_TEXT_Y);
    }
    if (status == BENTO_OK) {
        status = add_light(panel->ui, RIGHT_TEXT_X, BUTTON_LIGHT_Y,
                           BUTTON_LIGHT_SIZE, COLOR_RUN,
                           &panel->button_light);
    }
    if (status == BENTO_OK) {
        status = add_note(panel->ui, "กำลังกด", RIGHT_TEXT_X, PRESSED_Y);
    }
    if (status == BENTO_OK) {
        status = add_note(panel->ui, "นับได้ (ครั้ง)", COUNT_TITLE_X,
                          COUNT_TITLE_Y);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "0";
        args.text_len = 1U;
        args.x = SEG_X;
        args.y = SEG_Y;
        args.w = SEG_W;
        args.h = SEG_H;
        args.color = COLOR_TEXT;
        status = bento_ui_seg7_create(panel->ui, &args, &panel->count);
    }
    return status;
}

static bento_status_t add_command_card(panel_t *panel)
{
    const rect_t run_place = {RUN_BUTTON_X, COMMAND_Y, COMMAND_W,
                              COMMAND_H};
    const rect_t stop_place = {STOP_BUTTON_X, COMMAND_Y, COMMAND_W,
                               COMMAND_H};
    bento_status_t status =
        add_card(panel->ui, LEFT_X, BOTTOM_Y, LEFT_W, BOTTOM_H);

    if (status == BENTO_OK) {
        status =
            add_note(panel->ui, "คำสั่งไฟวิ่ง", LEFT_TEXT_X, BOTTOM_TEXT_Y);
    }
    if (status == BENTO_OK) {
        status = add_button(panel->ui, "เดินไฟวิ่ง", &run_place,
                            COLOR_RUN_BUTTON, &panel->run);
    }
    if (status == BENTO_OK) {
        status = add_button(panel->ui, "หยุดไฟวิ่ง", &stop_place,
                            COLOR_IDLE_BUTTON, &panel->stop);
    }
    return status;
}

/* The time left, with a bar that shows the range. */
static bento_status_t add_time_card(panel_t *panel)
{
    static const text_style_t time_style = {COLOR_TEXT, LEFT_LABEL_PX};
    const int32_t run_s = (int32_t)RUN_MS / (int32_t)MS_PER_SECOND;
    bento_ui_widget_args_t args;
    bento_ui_id_t scale = 0U;
    bento_status_t status =
        add_card(panel->ui, RIGHT_X, BOTTOM_Y, RIGHT_W, BOTTOM_H);

    if (status == BENTO_OK) {
        status = add_note(panel->ui, "เวลาที่เหลือของรอบนี้", RIGHT_TEXT_X,
                          BOTTOM_TEXT_Y);
    }
    if (status == BENTO_OK) {
        status =
            add_label(panel->ui, "30 วิ", RIGHT_TEXT_X, LEFT_LABEL_Y,
                      &time_style, &panel->time_left);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = RIGHT_TEXT_X;
        args.y = BAR_Y;
        args.w = BAR_W;
        args.h = BAR_H;
        args.color = COLOR_RUN;
        args.max_val = run_s;
        args.font_px =
            0; /* created at 0: a Bar created with a value blanks the page */
        status =
            bento_ui_bar_create(panel->ui, &args, &panel->time_bar);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_value(panel->ui, panel->time_bar, run_s);
        bento_ui_widget_args_init(&args);
        args.x = RIGHT_TEXT_X;
        args.y = SCALE_Y;
        args.w = BAR_W;
        args.h = SCALE_H;
        args.color = COLOR_TEXT;
        args.max_val = run_s;
        status = bento_ui_scale_create(panel->ui, &args, &scale);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_ticks(panel->ui, scale, SCALE_TICKS,
                                 SCALE_MAJOR_EVERY);
    }
    return status;
}

/* Made now and hidden, not made on the tap: widget handles are few.
 * The box's buttons send no events yet, so two Buttons answer. */
static bento_status_t add_confirm(panel_t *panel)
{
    const rect_t yes_place = {ANSWER_X, YES_Y, ANSWER_W, ANSWER_H};
    const rect_t no_place = {ANSWER_X, NO_Y, ANSWER_W, ANSWER_H};
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "ยืนยันหยุด\nไฟทุกดวงจะดับทันที"; /* a MsgBox holds 95 bytes */
    args.text_len = (uint32_t)strlen(args.text);
    args.x = BOX_X;
    args.y = BOX_Y;
    args.w = BOX_W;
    args.h = BOX_H;
    args.color = COLOR_CARD;
    status = bento_ui_msgbox_create(panel->ui, &args, &panel->box);
    if (status == BENTO_OK) {
        set_shown(panel->ui, panel->box, false);
        status = add_button(panel->ui, "ยืนยัน", &yes_place,
                            COLOR_IDLE_BUTTON, &panel->yes);
    }
    if (status == BENTO_OK) {
        status = add_button(panel->ui, "ยกเลิก", &no_place,
                            COLOR_IDLE_BUTTON, &panel->no);
    }
    if (status == BENTO_OK) {
        set_shown(panel->ui, panel->yes, false);
        set_shown(panel->ui, panel->no, false);
    }
    return status;
}

/* Made once before the loop: the screen holds 64 widgets, and making
 * them in the loop only wastes messages. */
static bento_status_t build_screen(chase_t *chase)
{
    static const text_style_t title_style = {COLOR_TEXT, TITLE_PX};
    panel_t *panel = &chase->panel;
    bento_ui_id_t unused = 0U;
    bento_status_t status =
        bento_ui_screen(panel->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(panel->ui, "แผงคุมไฟวิ่ง - คาบ 3", TITLE_X,
                           TITLE_Y, &title_style, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(panel->ui, "ไฟวิ่งกำลังเดิน", STATUS_X, STATUS_Y,
                           &NOTE_STYLE, &panel->status);
    }
    if (status == BENTO_OK) {
        status = add_led_card(chase);
    }
    if (status == BENTO_OK) {
        status = add_button_card(chase);
    }
    if (status == BENTO_OK) {
        status = add_command_card(panel);
    }
    if (status == BENTO_OK) {
        status = add_time_card(panel);
    }
    if (status == BENTO_OK) {
        status = add_confirm(panel);
    }
    if (status == BENTO_OK) {
        drain_events(panel->ui);
    }
    return status;
}

/* Step 3: the chase follows the clock, not a sleep. */
static bento_status_t step_chase(chase_t *chase, uint32_t now_ms)
{
    uint32_t i;
    bento_status_t status = BENTO_OK;

    if (chase->chase_on &&
        (bento_time_ticks_diff(now_ms, chase->last_step_ms) >=
         STEP_MS)) {
        status = bento_gpio_led_off(
            chase->led_index); /* the old one first */

        /* TODO จุดที่ 2: เติม chase->led_index = (chase->led_index + 1U) % chase->led_count;
         * จำดวงที่ติดไว้เอง ไม่ถามหลอด แล้วเลื่อนด้วย % จำนวนดวง */

        if (status == BENTO_OK) {
            status = bento_gpio_led_on(
                chase->led_index); /* then the new one */
        }
        chase->last_step_ms = now_ms;
        for (i = 0U; i < chase->led_count; i++) {
            (void)bento_ui_set_value(chase->panel.ui,
                                     chase->panel.leds[i],
                                     (i == chase->led_index) ? 1 : 0);
        }
    }
    return status;
}

/* Believe only a value that has been still for DEBOUNCE_MS. */
static void debounce(chase_t *chase, uint32_t now_ms)
{
    char line[LABEL_BYTES];

    if (chase->raw != chase->last_raw) {
        chase->last_raw = chase->raw;

        /* TODO จุดที่ 4: เติม chase->last_change_ms = now_ms;
         * กันเด้ง: ค่าต้องนิ่งครบเวลาก่อนเราจะเชื่อ บรรทัดนี้เริ่มจับเวลาใหม่ */
    } else if ((chase->raw != chase->stable) &&
               (bento_time_ticks_diff(now_ms, chase->last_change_ms) >=
                DEBOUNCE_MS)) {
        chase->stable = chase->raw;
        if (chase->stable) { /* count the press only, not the release */
            /* TODO จุดที่ 5: เติม chase->count++;
             * ถ้าเลขเพิ่มทีละสอง อย่าเพิ่งโทษปุ่ม นั่นคืออาการของการลืม if (chase->stable) */

            (void)snprintf(line, sizeof(line), "กดครั้งที่ %" PRId32,
                           chase->count);
            (void)bento_lcd_print(line);
        }
    } else {
        /* still, or not still long enough */
    }
}

/* Step 4: read the button every round, believe only a still value. */
static bento_status_t read_button(chase_t *chase, uint32_t now_ms)
{
    bento_status_t status = BENTO_OK;

    /* TODO จุดที่ 3: เติม status = bento_gpio_button_is_pressed(USER_BUTTON, &chase->raw);
     * chase->raw ตั้งต้นเป็น false ไว้ให้ไฟล์รันได้ก่อนเติมจุดนี้
     * เลขดิบจาก bento_gpio_button_value() กลับด้านกับที่คิด: 0 คือกด ไม่ใช่ 1
     *   (examples/s03/04_button_active_low.py) */
    if (status == BENTO_OK) {
        debounce(chase, now_ms);
    }
    return status;
}

static void close_confirm(const panel_t *panel)
{
    set_shown(panel->ui, panel->box, false);
    set_shown(panel->ui, panel->yes, false);
    set_shown(panel->ui, panel->no, false);
    set_shown(panel->ui, panel->status, true);
}

/* A command that stops something real asks first, and the question
 * says what will happen; Run needs no question, it is undone at once. */
static bento_status_t on_click(chase_t *chase, bento_ui_id_t widget)
{
    panel_t *panel = &chase->panel;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    if (widget == panel->run) {
        chase->chase_on = true;
        set_text(panel->ui, panel->status, "ไฟวิ่งกำลังเดิน");
    } else if ((widget == panel->stop) && !chase->asking) {
        chase->asking = true;
        set_shown(panel->ui, panel->box, true);
        set_shown(panel->ui, panel->yes, true);
        set_shown(panel->ui, panel->no, true);
        set_shown(panel->ui, panel->status, false);
    } else if ((widget == panel->yes) && chase->asking) {
        chase->asking = false;
        chase->chase_on = false;
        for (i = 0U; (i < chase->led_count) && (status == BENTO_OK);
             i++) {
            status = bento_gpio_led_off(i);
            (void)bento_ui_set_value(panel->ui, panel->leds[i], 0);
        }
        set_text(panel->ui, panel->status, "หยุดแล้ว ไฟดับทุกดวง");
        close_confirm(panel);
    } else if ((widget == panel->no) && chase->asking) {
        chase->asking = false;
        close_confirm(panel);
    } else {
        /* another widget, or a tap the moment does not take */
    }
    return status;
}

/* The screen's own pace: ui.poll() is a message across the cores, so
 * not every loop round, and the numbers change at most once a second. */
static bento_status_t refresh_screen(chase_t *chase, uint32_t now_ms)
{
    panel_t *panel = &chase->panel;
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    char text[LABEL_BYTES];
    uint32_t count = 0U;
    uint32_t i;
    int32_t left_s;
    bento_status_t status = BENTO_OK;

    chase->last_ui_ms = now_ms;
    (void)bento_ui_set_value(panel->ui, panel->button_light,
                             chase->stable ? 1 : 0);
    left_s = (RUN_MS - bento_time_ticks_diff(now_ms, chase->start_ms)) /
             MS_PER_SECOND;
    if (left_s != chase->last_second) {
        chase->last_second = left_s;
        (void)snprintf(text, sizeof(text), "%" PRId32, chase->count);
        set_text(panel->ui, panel->count, text);
        (void)snprintf(text, sizeof(text), "%" PRId32 " วิ", left_s);
        set_text(panel->ui, panel->time_left, text);
        (void)bento_ui_set_value(panel->ui, panel->time_bar, left_s);
    }
    if (bento_ui_poll(panel->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U; /* ui.poll() answers no events when it fails */
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        if (events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) {
            status = on_click(chase, events[i].id);
        }
    }
    return status;
}

/* One loop, two jobs: the time is read once a round and shared. */
static bento_status_t run_loop(chase_t *chase)
{
    uint32_t now_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    chase->start_ms = now_ms;
    chase->last_step_ms = now_ms;
    chase->last_change_ms = now_ms;
    chase->last_ui_ms = now_ms;
    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(now_ms, chase->start_ms) < RUN_MS)) {
        status = step_chase(chase, now_ms);
        if (status == BENTO_OK) {
            status = read_button(chase, now_ms);
        }
        if ((status == BENTO_OK) &&
            (bento_time_ticks_diff(now_ms, chase->last_ui_ms) >=
             UI_MS)) {
            status = refresh_screen(chase, now_ms);
        }
        if (status == BENTO_OK) {
            status =
                wait_ms(POLL_MS); /* the one place the program rests */
        }
        now_ms = bento_time_ticks_ms();
    }
    return status;
}

/* Step 5: always end with every LED off, and say the run is over:
 * the numbers left on the screen are the last ones, not live. */
static bento_status_t finish(const chase_t *chase)
{
    const panel_t *panel = &chase->panel;
    char text[LINE_BYTES];
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < chase->led_count) && (status == BENTO_OK); i++) {
        status = bento_gpio_led_off(i);
        (void)bento_ui_set_value(panel->ui, panel->leds[i], 0);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%" PRId32, chase->count);
        set_text(panel->ui, panel->count, text);
        (void)bento_ui_set_value(panel->ui, panel->time_bar, 0);
        set_text(panel->ui, panel->time_left, "0 วิ");
        set_text(panel->ui, panel->status,
                 "รอบนี้จบแล้ว ตัวเลขข้างบนคือค่าสุดท้าย");
        drain_events(panel->ui);

        /* TODO จุดที่ 6: เติม snprintf(text, sizeof(text),
         *   "<span class=ok>จบรอบทดสอบ กดปุ่มทั้งหมด %" PRId32 " ครั้ง</span>", chase->count)
         *   แล้ว bento_lcd_print(text) */

        (void)printf("โปรแกรมจบแล้ว - ไฟทุกดวงถูกดับเรียบร้อย\n");
    }
    return status;
}

int main(void)
{
    chase_t chase;
    bento_status_t status;

    (void)memset(&chase, 0, sizeof(chase));
    chase.panel.ui = bento_runtime_get_ui();
    chase.chase_on = true;
    chase.last_second = -1;
    status = greet(&chase);
    if (status == BENTO_OK) {
        status = leds_off(&chase);
    }
    if (status == BENTO_OK) {
        status = build_screen(&chase);
    }
    if (status == BENTO_OK) {
        status = run_loop(&chase);
    }
    if (status == BENTO_OK) {
        status = finish(&chase);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
