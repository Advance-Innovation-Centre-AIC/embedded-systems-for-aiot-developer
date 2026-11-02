/*
 * 15_one_number_many_faces.c -- ตัวเลขตัวเดียว หลายวิธีเล่า (One number,
 * many faces)
 *
 * ไม่มีเซนเซอร์ ไม่มีเน็ต มีแต่ตัวเลขหนึ่งตัวที่เดินขึ้นลงเอง ค่าเดียวกัน
 * เล่าได้หลายแบบ และแต่ละแบบตอบคำถามคนละข้อ วงแหวนตอบว่า "เต็มแค่ไหน"
 * ตัวเลขตอบว่า "เท่าไรพอดี" กราฟตอบว่า "ที่ผ่านมาเป็นยังไง" ไฟติดดับตอบว่า
 * "ถึงเกณฑ์หรือยัง" แตะปุ่มแถวล่างเพื่อฟังเสียง และบรรทัด ui.list() บอกว่า
 * จอมี widget กี่ตัว จากเพดาน 64
 * กับดัก: Seg7 ต้องส่งเป็นข้อความ Slider Arc Bar Switch รับเฉพาะค่า และชนิด
 * ไหนรับอะไรไม่มีทางรู้จาก error เพราะไม่มี
 *
 * No sensor and no network: one number walks up and down by itself.
 * The same value told in many ways, each answering its own question:
 * the ring "how full", the digits "exactly how much", the chart "how
 * has it been", the lamps "past the threshold yet". Tap the button on
 * the bottom row to hear a sound; the ui.list() line says how many
 * widgets the screen holds out of 64. Trap: a Seg7 takes text, a
 * Slider, Arc, Bar or Switch only a value, and nothing tells you which
 * takes what, because there is no error.
 *
 * Twin of 15_one_number_many_faces.py
 * (embedded-systems-for-aiot-developer/examples/s01/15_one_number_many_faces.py).
 * Style: CLEAN_C.md.
 * Where C differs: ui.list() fills an array of 64 (512 bytes, so at
 * file scope); w["id"] is the slot of each id and w["type"] the name
 * modui.c's widget_type_name() gives (the same names, in a table
 * below). A click is BENTO_UI_EVENT_CLICKED on the button's id.
 *
 * ตาคุณ: เพิ่ม bento_ui_set_value() ของ Seg7 เข้าไปในลูป แล้วลองสลับเป็น
 * set_text() ของ Bar ดูว่าเกิดอะไรขึ้น
 * Your turn: set the Seg7 with bento_ui_set_value() in the loop, then
 * give the Bar a text instead, and explain what happens (hint: silence
 * is not success).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

enum {
    STEP = 4,     /* the number moves this much a round */
    HIGH_AT = 70, /* the upper threshold: lamps on, a sound */
    VALUE_MAX = 100,
    DEGREES_MAX = 359, /* a Compass counts degrees 0 to 359 */
    LIST_MAX = 64,     /* the display's ceiling of widgets */
    TEXT_BYTES = 128
};

/* ui.tone(note, wave, velocity, ms): a MIDI note 0-127, not hertz. */
enum {
    NOTE_HIGH = 72,
    NOTE_LOW = 60,
    TONE_VELOCITY = 90,
    TONE_MS = 120
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    RUN_MS = 36000,
    TICK_MS = 120
};

/* Layout on the 792 x 398 canvas. The card is 744 wide with 16 inside
 * on every side, so what sits on it lies between x 40 and 752. */
enum {
    FONT_PX = 20,
    SMALL_FONT_PX = 16,
    TITLE_FONT_PX = 24,
    LEFT_X = 24,
    TITLE_Y = 24,
    STATUS_X = 456,
    STATUS_Y = 32,
    CARD_Y = 72,
    CARD_W = 744,
    CARD_H = 160,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    DIAL_Y = 88,
    DIAL_SIZE = 88, /* without w an Arc or a Compass is 150 x 150 */
    ARC_X = 40,
    COMPASS_X = 144,
    SEG_X = 248,
    SEG_W = 152,
    SEG_H = 64,
    BAR_Y = 160,
    BAR_H = 16,
    SLIDER_X = 416,
    SLIDER_Y = 120,
    SLIDER_W = 192,
    SLIDER_H = 24, /* the finger holds the knob, not the rail */
    SPINNER_X = 624,
    SPINNER_Y = 100,
    SPINNER_SIZE = 64,
    IMAGE_X = 704,
    IMAGE_Y = 104,
    IMAGE_SIZE = 48,
    ROW_Y = 248,
    ROW_H = 88, /* things to touch: 88 high, at least 32 apart */
    CHART_W = 264,
    BUTTON_X = 320,
    BUTTON_W = 152,
    SWITCH_X = 504,
    CHECKBOX_X = 624,
    CHECKBOX_W = 144,
    CHECK_LABEL_X = 652,
    CHECK_LABEL_Y = 242,
    COUNT_X = 112,
    COUNT_Y = 340
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_INFO = 0x4A9EFF,
    COLOR_BUTTON = 0x3A4150
};

/* Icon names the firmware has. A wrong name when the Image is made is
 * a RuntimeError at once; a wrong name later through .icon() does
 * nothing, silently. */
static const char ICON_UP[] = "arrow_up";
static const char ICON_DOWN[] = "arrow_down";

/* modui.c's widget_type_name(), indexed by UI_WIDGET_* (0 and 17, the
 * sprite, have no name there). */
static const char *const TYPE_NAMES[] = {
    "Unknown",   "Button",   "Label",     "Slider",   "Switch",
    "Checkbox",  "Arc",      "Bar",       "Spinner",  "Dropdown",
    "Textarea",  "Seg7",     "DotMatrix", "Chart",    "Image",
    "Panel",     "Compass",  "Unknown",   "Led",      "Scale",
    "Spinbox",   "Table",    "List",      "Roller",   "ButtonMatrix",
    "MsgBox",    "Keyboard", "Tabview",   "Tileview", "Win",
    "Line",      "Picture",  "Container", "Menu",     "MenuPage",
    "SpanGroup", "Calendar"};

enum {
    TYPE_NAME_COUNT = sizeof(TYPE_NAMES) / sizeof(TYPE_NAMES[0])
};

/* ui.list()'s answer: 64 x 8 bytes, too big for the program stack, so
 * file scope, zeroed by the loader and again at the start of main()
 * (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern bento_ui_widget_info_t g_live[LIST_MAX];
bento_ui_widget_info_t g_live[LIST_MAX];

typedef bento_ui_status_t (*create_fn_t)(
    bento_ui_t *ui, const bento_ui_widget_args_t *args,
    bento_ui_id_t *out_id);

typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} place_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
    const char *text;
} caption_t;

/* Every widget the number is sent to. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t status;
    bento_ui_id_t arc;
    bento_ui_id_t compass;
    bento_ui_id_t seg;
    bento_ui_id_t bar;
    bento_ui_id_t slider;
    bento_ui_id_t image;
    bento_ui_id_t chart;
    bento_ui_id_t button;
    bento_ui_id_t toggle;
    bento_ui_id_t checkbox;
    bento_ui_id_t count;
} faces_t;

typedef struct {
    int32_t value;
    int32_t step;
    int32_t beeps;
    int32_t taps;
    bool was_high;
} walk_t;

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The widget setters ignore a refusal in MicroPython (modui.c), so
 * they do here too. */
static void set_text(const faces_t *faces, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(faces->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void set_color(const faces_t *faces, bento_ui_id_t widget,
                      uint32_t color)
{
    (void)bento_ui_set_color(faces->ui, widget, color);
}

static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* ui.list() answers [] on every failure: 0 widgets here. */
static uint32_t list_widgets(bento_ui_t *ui)
{
    uint32_t count = 0U;

    if (bento_ui_list(ui, g_live, LIST_MAX, &count) != BENTO_OK) {
        count = 0U;
    }
    return count;
}

static const char *type_name(uint8_t widget_type)
{
    return (widget_type < (uint32_t)TYPE_NAME_COUNT)
               ? TYPE_NAMES[widget_type]
               : TYPE_NAMES[0];
}

static bento_status_t add_caption(bento_ui_t *ui,
                                  const caption_t *caption,
                                  bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = caption->text;
    args.text_len = (uint32_t)strlen(caption->text);
    args.x = caption->x;
    args.y = caption->y;
    args.color = caption->color;
    args.font_px = caption->font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* One widget made by `create` at `place`; range_max > 0 gives it the
 * range 0..range_max (min= and max= of the original). */
static bento_status_t add_shape(bento_ui_t *ui, create_fn_t create,
                                const place_t *place, uint32_t color,
                                int32_t range_max,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = place->x;
    args.y = place->y;
    args.w = place->w;
    args.h = place->h;
    args.color = color;
    args.min_val = 0;
    args.max_val = range_max;
    return create(ui, &args, out_id);
}

/* Like add_shape, with a text: the Seg7, the Image's icon name, the
 * Button and the Checkbox. */
static bento_status_t add_texted(bento_ui_t *ui, create_fn_t create,
                                 const place_t *place,
                                 const caption_t *look,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = look->text;
    args.text_len = (uint32_t)strlen(look->text);
    args.x = place->x;
    args.y = place->y;
    args.w = place->w;
    args.h = place->h;
    args.color = look->color;
    args.font_px = look->font_px;
    return create(ui, &args, out_id);
}

/* The Panel is the card's background and is made first, or it would
 * cover what is already there. */
static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = LEFT_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border colour */
    args.max_val = CARD_RADIUS; /* and max= its corner radius */
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &card);
}

/* The ring and the compass. An Arc takes no color= when made, and a
 * Compass reads w as its diameter and ignores h. */
static bento_status_t add_dials(faces_t *faces)
{
    const place_t arc = {ARC_X, DIAL_Y, DIAL_SIZE, DIAL_SIZE};
    const place_t compass = {COMPASS_X, DIAL_Y, DIAL_SIZE, DIAL_SIZE};
    bento_status_t status = add_shape(faces->ui, bento_ui_arc_create,
                                      &arc, 0U, VALUE_MAX, &faces->arc);

    if (status == BENTO_OK) {
        set_color(faces, faces->arc, COLOR_OK);
        status = add_shape(faces->ui, bento_ui_compass_create, &compass,
                           0U, 0, &faces->compass);
    }
    if (status == BENTO_OK) {
        set_color(faces, faces->compass, COLOR_INFO);
    }
    return status;
}

/* The digits over the bar, and the slider beside them. */
static bento_status_t add_levels(faces_t *faces)
{
    const place_t seg = {SEG_X, DIAL_Y, SEG_W, SEG_H};
    static const caption_t seg_look = {0, 0, COLOR_OK, 0, "0"};
    const place_t bar = {SEG_X, BAR_Y, SEG_W, BAR_H};
    const place_t slider = {SLIDER_X, SLIDER_Y, SLIDER_W, SLIDER_H};
    bento_status_t status = add_texted(faces->ui, bento_ui_seg7_create,
                                       &seg, &seg_look, &faces->seg);

    if (status == BENTO_OK) {
        status = add_shape(faces->ui, bento_ui_bar_create, &bar, 0U,
                           VALUE_MAX, &faces->bar);
    }
    if (status == BENTO_OK) {
        set_color(faces, faces->bar, COLOR_OK);
        status = add_shape(faces->ui, bento_ui_slider_create, &slider,
                           0U, VALUE_MAX, &faces->slider);
    }
    return status;
}

/* A Spinner has no value: it only says "still working". An Image
 * takes an icon name when made and changes it with .icon(). */
static bento_status_t add_signs(faces_t *faces)
{
    const place_t spinner = {SPINNER_X, SPINNER_Y, SPINNER_SIZE,
                             SPINNER_SIZE};
    const place_t image = {IMAGE_X, IMAGE_Y, IMAGE_SIZE, IMAGE_SIZE};
    static const caption_t image_look = {0, 0, COLOR_OK, 0, ICON_UP};
    bento_ui_id_t unused;
    bento_status_t status = add_shape(
        faces->ui, bento_ui_spinner_create, &spinner, 0U, 0, &unused);

    if (status == BENTO_OK) {
        status = add_texted(faces->ui, bento_ui_image_create, &image,
                            &image_look, &faces->image);
    }
    return status;
}

static bento_status_t add_card_captions(bento_ui_t *ui)
{
    static const caption_t captions[] = {
        {40, 184, COLOR_DIM, SMALL_FONT_PX, "Arc"},
        {144, 184, COLOR_DIM, SMALL_FONT_PX, "Compass"},
        {248, 180, COLOR_DIM, FONT_PX, "Seg7 กับ Bar"},
        {416, 184, COLOR_DIM, SMALL_FONT_PX, "Slider"},
        {608, 184, COLOR_DIM, SMALL_FONT_PX, "Spinner"},
        {696, 184, COLOR_DIM, SMALL_FONT_PX, "Image"}};
    bento_ui_id_t unused;
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < (sizeof(captions) / sizeof(captions[0]))) &&
                 (status == BENTO_OK);
         i++) {
        status = add_caption(ui, &captions[i], &unused);
    }
    return status;
}

/* The bottom row lies outside the card: the things to touch. */
static bento_status_t add_touch_row(faces_t *faces)
{
    const place_t chart = {LEFT_X, ROW_Y, CHART_W, ROW_H};
    const place_t button = {BUTTON_X, ROW_Y, BUTTON_W, ROW_H};
    static const caption_t button_look = {0, 0, COLOR_BUTTON, FONT_PX,
                                          "แตะฟังเสียง"};
    const place_t toggle = {SWITCH_X, ROW_Y, ROW_H, ROW_H};
    bento_status_t status =
        add_shape(faces->ui, bento_ui_chart_create, &chart, COLOR_INFO,
                  VALUE_MAX, &faces->chart);

    if (status == BENTO_OK) {
        status = add_texted(faces->ui, bento_ui_button_create, &button,
                            &button_look, &faces->button);
    }
    if (status == BENTO_OK) {
        status = add_shape(faces->ui, bento_ui_switch_create, &toggle,
                           0U, 0, &faces->toggle);
    }
    return status;
}

/* A Checkbox's own text is always 16 px (its value= means ticked, not
 * a size), and Thai under 20 px breaks up on the board: so an empty
 * box, with a 20 px Thai label beside it. */
static bento_status_t add_check_row(faces_t *faces)
{
    const place_t checkbox = {CHECKBOX_X, ROW_Y, CHECKBOX_W, ROW_H};
    static const caption_t checkbox_look = {0, 0, COLOR_TEXT, 0, " "};
    static const caption_t captions[] = {
        {652, 242, COLOR_TEXT, FONT_PX, "เกินเกณฑ์บน"},
        {24, 344, COLOR_DIM, SMALL_FONT_PX, "Chart"}};
    static const caption_t count = {COUNT_X, COUNT_Y, COLOR_DIM,
                                    FONT_PX, "ยังไม่ได้นับ widget"};
    static const caption_t switch_caption = {
        SWITCH_X, 344, COLOR_DIM, SMALL_FONT_PX, "Switch Checkbox"};
    bento_ui_id_t unused;
    bento_status_t status =
        add_texted(faces->ui, bento_ui_checkbox_create, &checkbox,
                   &checkbox_look, &faces->checkbox);

    if (status == BENTO_OK) {
        status = add_caption(faces->ui, &captions[0], &unused);
    }
    if (status == BENTO_OK) {
        status = add_caption(faces->ui, &captions[1], &unused);
    }
    if (status == BENTO_OK) {
        status = add_caption(faces->ui, &count, &faces->count);
    }
    if (status == BENTO_OK) {
        status = add_caption(faces->ui, &switch_caption, &unused);
    }
    return status;
}

static bento_status_t build_screen(faces_t *faces)
{
    static const caption_t title = {LEFT_X, TITLE_Y, COLOR_TEXT,
                                    TITLE_FONT_PX,
                                    "ตัวเลขตัวเดียว หลายวิธีเล่า"};
    static const caption_t status_look = {STATUS_X, STATUS_Y, COLOR_DIM,
                                          FONT_PX, "กำลังจะเริ่มเดิน"};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(faces->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_caption(faces->ui, &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_caption(faces->ui, &status_look, &faces->status);
    }
    if (status == BENTO_OK) {
        status = add_card(faces->ui);
    }
    if (status == BENTO_OK) {
        status = add_dials(faces);
    }
    if (status == BENTO_OK) {
        status = add_levels(faces);
    }
    if (status == BENTO_OK) {
        status = add_signs(faces);
    }
    if (status == BENTO_OK) {
        status = add_card_captions(faces->ui);
    }
    if (status == BENTO_OK) {
        status = add_touch_row(faces);
    }
    if (status == BENTO_OK) {
        status = add_check_row(faces);
    }
    return status;
}

/* A failed poll answers no events, so its result is not needed. */
static uint32_t poll_events(bento_ui_t *ui, bento_ui_event_t *events)
{
    uint32_t count = 0U;

    if (bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    return count;
}

static void poll_once(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];

    (void)poll_events(ui, events);
}

/* ui.list() counts the widgets on the screen, so nobody counts the
 * quota by hand. lcd.print() ignores a console that is not ready. */
static void report_widgets(const faces_t *faces)
{
    char text[TEXT_BYTES];
    const uint32_t count = list_widgets(faces->ui);
    uint32_t i;

    set_color(faces, faces->count, COLOR_OK);
    (void)snprintf(text, sizeof(text),
                   "ui.list() นับได้ %" PRIu32 " ตัว จาก 64", count);
    set_text(faces, faces->count, text);
    (void)snprintf(text, sizeof(text),
                   "บนจอตอนนี้มี widget %" PRIu32 " ตัว จากเพดาน 64 ตัว",
                   count);
    (void)bento_lcd_print(text);
    for (i = 0U; i < count; i++) {
        (void)snprintf(text, sizeof(text), "  id %" PRIu32 " = %s",
                       bento_ui_id_to_handle(g_live[i].id),
                       type_name(g_live[i].widget_type));
        (void)bento_lcd_print(text);
    }
}

/* The number walks up to the top and back: the one source of the
 * whole screen. */
static void step_value(walk_t *walk)
{
    walk->value += walk->step;
    if (walk->value >= VALUE_MAX) {
        walk->value = VALUE_MAX;
        walk->step = -STEP;
    } else if (walk->value <= 0) {
        walk->value = 0;
        walk->step = STEP;
    } else {
        /* still on its way */
    }
}

/* The same value into every widget. A Compass counts degrees, so the
 * scale is changed first, in integers. */
static void show_value(const faces_t *faces, int32_t value, bool high)
{
    char text[TEXT_BYTES];
    const uint32_t color = high ? COLOR_WARN : COLOR_OK;
    const char *icon = high ? ICON_UP : ICON_DOWN;

    (void)bento_ui_set_value(faces->ui, faces->arc, value);
    (void)bento_ui_set_value(faces->ui, faces->bar, value);
    (void)bento_ui_set_value(faces->ui, faces->slider, value);
    (void)snprintf(text, sizeof(text), "%" PRId32, value);
    set_text(faces, faces->seg, text);
    (void)bento_ui_chart_set_next(faces->ui, faces->chart, 0U, value);
    (void)bento_ui_set_value(faces->ui, faces->compass,
                             (value * DEGREES_MAX) / VALUE_MAX);
    (void)bento_ui_set_value(faces->ui, faces->toggle, high ? 1 : 0);
    (void)bento_ui_set_value(faces->ui, faces->checkbox, high ? 1 : 0);
    set_color(faces, faces->arc, color);
    set_color(faces, faces->bar, color);
    set_color(faces, faces->seg, color);
    (void)bento_ui_image_icon(faces->ui, faces->image, icon,
                              (uint32_t)strlen(icon));
    set_color(faces, faces->image, color);
}

/* A sound only when the threshold is crossed, not every round above
 * it, or it becomes a hum nobody can read. */
static void sound_crossing(const faces_t *faces, walk_t *walk,
                           bool high)
{
    char text[TEXT_BYTES];

    if (high != walk->was_high) {
        walk->was_high = high;
        walk->beeps++;
        (void)bento_ui_sfx_tone(faces->ui, high ? NOTE_HIGH : NOTE_LOW,
                                BENTO_UI_WAVE_SINE, TONE_VELOCITY,
                                TONE_MS);
        (void)snprintf(text, sizeof(text),
                       "ข้ามเกณฑ์ที่ค่า %" PRId32 " ->  %s", walk->value,
                       high ? "สูง" : "ต่ำ");
        (void)bento_lcd_print(text);
    }
}

/* Read every round, or the events pile up until new ones are thrown
 * away. sfx plays a ready-made sound, chosen by a constant. */
static void handle_taps(const faces_t *faces, walk_t *walk)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    char text[TEXT_BYTES];
    const uint32_t count = poll_events(faces->ui, events);
    uint32_t i;

    for (i = 0U; i < count; i++) {
        if (event_is(&events[i], BENTO_UI_EVENT_CLICKED,
                     faces->button)) {
            walk->taps++;
            (void)bento_ui_sfx_play(faces->ui, BENTO_UI_SFX_UI_SELECT);
            (void)snprintf(text, sizeof(text),
                           "<span class=ok>แตะปุ่มครั้งที่ %" PRId32
                           " </span>",
                           walk->taps);
            (void)bento_lcd_print(text);
        }
    }
}

/* The top-right line has room for about 30 characters; longer runs
 * off the screen without a word. */
static void show_status(const faces_t *faces, const walk_t *walk,
                        bool high)
{
    char text[TEXT_BYTES];

    set_color(faces, faces->status, high ? COLOR_WARN : COLOR_DIM);
    (void)snprintf(text, sizeof(text),
                   "ค่า %" PRId32 " | ข้าม %" PRId32 " | แตะ %" PRId32,
                   walk->value, walk->beeps, walk->taps);
    set_text(faces, faces->status, text);
}

static bento_status_t walk_once(const faces_t *faces, walk_t *walk,
                                uint32_t work_start_ms)
{
    bool high;
    int32_t left_ms;

    step_value(walk);
    high = walk->value >= HIGH_AT;
    show_value(faces, walk->value, high);
    sound_crossing(faces, walk, high);
    handle_taps(faces, walk);
    show_status(faces, walk, high);
    left_ms = TICK_MS - bento_time_ticks_diff(bento_time_ticks_ms(),
                                              work_start_ms);
    return (left_ms > 0) ? sleep_checked((uint32_t)left_ms) : BENTO_OK;
}

static bento_status_t run_walk(const faces_t *faces, walk_t *walk)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    uint32_t work_start_ms = start_ms;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(work_start_ms, start_ms) < RUN_MS)) {
        status = walk_once(faces, walk, work_start_ms);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
        work_start_ms = bento_time_ticks_ms();
    }
    return status;
}

/* The last values stay on the screen. Long sentences go to the
 * console, which has every glyph and no width limit. */
static void show_end(const faces_t *faces, const walk_t *walk)
{
    char text[TEXT_BYTES];

    set_color(faces, faces->status, COLOR_DIM);
    (void)snprintf(text, sizeof(text),
                   "จบแล้ว | ข้าม %" PRId32 " | แตะ %" PRId32,
                   walk->beeps, walk->taps);
    set_text(faces, faces->status, text);
    poll_once(faces->ui);
    (void)bento_lcd_print("Spinner ยังหมุนอยู่ เพราะมันไม่เคยรู้ว่างานจบ");
    (void)bento_lcd_print(
        "อีกสองชนิดอยู่ไฟล์ 14 ส่วน Dropdown อยู่ examples/s04/08");
    (void)bento_lcd_console(
        "<span class=muted>------------------------</span>");
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ข้ามเกณฑ์ %" PRId32
                   " ครั้ง | แตะปุ่ม %" PRId32 " ครั้ง</span>",
                   walk->beeps, walk->taps);
    (void)bento_lcd_print(text);
    (void)printf("widget บนจอ %" PRIu32 " ตัว | เพดาน 64 ตัว\n",
                 list_widgets(faces->ui));
}

int main(void)
{
    faces_t faces;
    walk_t walk = {0, STEP, 0, 0, false};
    bento_status_t status;

    (void)memset(g_live, 0, sizeof(g_live));
    (void)memset(&faces, 0, sizeof(faces));
    faces.ui = bento_runtime_get_ui();
    status = build_screen(&faces);
    if (status == BENTO_OK) {
        poll_once(faces.ui);
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ตัวเลขตัวเดียว หลายวิธีเล่า</h2>");
        report_widgets(&faces);
        poll_once(faces.ui);
        status = run_walk(&faces, &walk);
    }
    if (status == BENTO_OK) {
        show_end(&faces, &walk);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
