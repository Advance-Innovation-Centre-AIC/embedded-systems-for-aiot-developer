/*
 * 02_payload_shape.c -- payload สามแบบ วัดเป็นไบต์
 * (Payload Shape)
 *
 * วาง payload JSON สามแบบเทียบกัน: แบบหลวมเกินไป แบบแน่นเกินไป
 * และแบบพอดี การ์ดซ้ายมีแท่งสามแท่งยาวตามจำนวนไบต์บนสเกลเดียวกัน
 * การ์ดขวามีเลขเจ็ดส่วนบอกว่าแบบพอดีกินกี่ไบต์ ข้างล่างเตือนว่า
 * ตัวเลขต้องส่งเป็นตัวเลข ไม่ใช่สตริง ลิ้นชัก Console เก็บ JSON
 * ตัวเต็มทุกแบบ รูปที่แพลตฟอร์มคาดหวัง และผลของการแกะกลับ
 * ไม่มีอะไรให้กด โปรแกรมวาดจอครั้งเดียวแล้วจบ
 *
 * Three JSON payloads side by side: too loose, too tight and just
 * right. The left card shows three bars whose length is the byte
 * count on one scale; the right card's seven-segment number says
 * how many bytes the good one costs. Below, a reminder that a
 * number must travel as a number, not a string. The Console drawer
 * keeps each JSON text in full, the flat shape the platform
 * expects, and what parsing it back gives. Nothing to tap: the
 * program draws once and ends.
 *
 * Twin of 02_payload_shape.py
 * (embedded-systems-for-aiot-developer/examples/s10/02_payload_shape.py),
 * merged from the library twin aiot-s10-02-payload-shape. Style: CLEAN_C.md.
 *
 * Differs from the original:
 *   - There is no json module in C. Each text is built with
 *     snprintf exactly as MicroPython's json.dumps writes it: ", "
 *     and ": " separators, and the keys in MicroPython's order. Its
 *     dict is a hash table walked slot by slot, not in insertion
 *     order, so the good payload reads id, uptime_s, count, unit.
 *   - json.loads is a small reader that finds one key's value in
 *     the text; the type line names the type that value has.
 *   - The real numbers are written by the C compiler, not by
 *     MicroPython's parser, so 25.123456789012345 may differ from
 *     the board's in the last bit; bento_fmt prints it the board's
 *     way.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* The course palette, 0xRRGGBB, one value per role. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_TRACK = COLOR_CARD /* a Bar's color= is its track */
};

enum {
    SHAPE_LOOSE = 0,
    SHAPE_TIGHT = 1,
    SHAPE_GOOD = 2,
    SHAPE_COUNT = 3,
    COUNTER = 7, /* the value sent: a counter, not a real reading */
    MS_PER_S = 1000,
    JSON_CHARS = 96,  /* the longest payload is 62 bytes */
    TEXT_CHARS = 128, /* a drawer line carries at most 127 bytes */
    VALUE_CHARS = 32,
    SETTLE_MS = 200 /* let the cleared screen settle */
};

/* One payload: its name, its JSON text and its colour. */
typedef struct {
    const char *name;
    char json[JSON_CHARS];
    uint32_t color;
} shape_t;

/* Everything the program builds once and shows twice. */
typedef struct {
    bento_ui_t *ui;
    shape_t shapes[SHAPE_COUNT];
    int32_t sizes[SHAPE_COUNT];
    int32_t widest;
} payloads_t;

static const char DEVICE_ID[] = "team03";

/* str() of a real number, as the board's MicroPython writes it. */
static void format_real(bento_real_t value, char *out, uint32_t cap)
{
    uint32_t length = 0U;

    if (bento_fmt_real(out, cap, value, &length) != BENTO_OK) {
        out[0] = '\0'; /* only a too-small buffer fails */
    }
}

/* The three payloads, written as json.dumps writes them. */
static void build_payloads(payloads_t *data, uint32_t uptime_ms)
{
    const bento_real_t loose_temp = (bento_real_t)25.123456789012345;
    const bento_real_t tight_temp = (bento_real_t)25.1;
    const uint32_t uptime_s = uptime_ms / (uint32_t)MS_PER_S;
    char real_text[BENTO_FMT_BUF_SIZE];
    shape_t *shapes = data->shapes;

    format_real(loose_temp, real_text, sizeof(real_text));
    (void)snprintf(shapes[SHAPE_LOOSE].json, JSON_CHARS,
                   "{\"temperature_value_in_celsius\": %s, "
                   "\"the_counter\": %d}",
                   real_text, COUNTER);
    format_real(tight_temp, real_text, sizeof(real_text));
    (void)snprintf(shapes[SHAPE_TIGHT].json, JSON_CHARS,
                   "{\"a\": %s, \"b\": %d}", real_text, COUNTER);
    (void)snprintf(shapes[SHAPE_GOOD].json, JSON_CHARS,
                   "{\"id\": \"%s\", \"uptime_s\": %lu, "
                   "\"count\": %d, \"unit\": \"s\"}",
                   DEVICE_ID, (unsigned long)uptime_s, COUNTER);
}

/* Names, colours, and the byte count of each text: an ASCII
 * text's length is the bytes it puts on the wire. */
static void measure_payloads(payloads_t *data)
{
    static const char *const names[SHAPE_COUNT] = {"หลวมเกินไป",
                                                   "แน่นเกินไป", "พอดี"};
    static const uint32_t colors[SHAPE_COUNT] = {COLOR_BAD, COLOR_WARN,
                                                 COLOR_OK};
    int32_t i;

    data->widest = 0;
    for (i = 0; i < SHAPE_COUNT; i++) {
        data->shapes[i].name = names[i];
        data->shapes[i].color = colors[i];
        data->sizes[i] = (int32_t)strlen(data->shapes[i].json);
        if (data->sizes[i] > data->widest) {
            data->widest = data->sizes[i];
        }
    }
}

/* The raw text of one key's value in a flat JSON object: what
 * json.loads would read for that key. Answers false when the key
 * is not there. */
static uint32_t value_length(const char *start, uint32_t cap)
{
    uint32_t length = 0U;
    bool in_string = false;

    while ((start[length] != '\0') && ((length + 1U) < cap) &&
           (in_string ||
            ((start[length] != ',') && (start[length] != '}')))) {
        if (start[length] == '"') {
            in_string = !in_string;
        }
        length++;
    }
    return length;
}

static bool find_json_value(const char *json, const char *key,
                            char *out, uint32_t cap)
{
    char pattern[VALUE_CHARS];
    const char *start;
    uint32_t length = 0U;

    (void)snprintf(pattern, sizeof(pattern), "\"%s\": ", key);
    start = strstr(json, pattern);
    if (start != NULL) {
        start += strlen(pattern);
        length = value_length(start, cap);
        (void)memcpy(out, start, length);
    }
    out[length] = '\0';
    return start != NULL;
}

/* The Python type a JSON value comes back as. */
static const char *type_of_json_value(const char *value)
{
    const char *type = "<class 'int'>";

    if (value[0] == '"') {
        type = "<class 'str'>";
    } else if ((strchr(value, '.') != NULL) ||
               (strchr(value, 'e') != NULL)) {
        type = "<class 'float'>";
    } else {
        /* digits only: an int */
    }
    return type;
}

/* A JSON string's text without its quotes (str() of it). */
static const char *unquote(char *value)
{
    const char *text = value;
    const size_t length = strlen(value);

    if ((length >= 2U) && (value[0] == '"')) {
        value[length - 1U] = '\0';
        text = &value[1];
    }
    return text;
}

/* Parse the flat payload back: every field must come back. */
static void print_parsed_back(const char *json)
{
    char value[VALUE_CHARS];
    char line[TEXT_CHARS];

    (void)find_json_value(json, "id", value, sizeof(value));
    (void)snprintf(line, sizeof(line), "แกะกลับได้ id = %s",
                   unquote(value));
    (void)bento_lcd_print(line);
    (void)find_json_value(json, "count", value, sizeof(value));
    (void)snprintf(line, sizeof(line), "count = %s ชนิด %s", value,
                   type_of_json_value(value));
    (void)bento_lcd_print(line);
}

/* The drawer keeps the full JSON texts, one per line; the screen
 * keeps the comparison. */
static void print_to_drawer(const payloads_t *data)
{
    const bento_real_t number = (bento_real_t)25.5;
    char real_text[BENTO_FMT_BUF_SIZE];
    char line[TEXT_CHARS];
    int32_t i;

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>คาบ 10 - รูปร่างของ payload</h2>");
    for (i = 0; i < SHAPE_COUNT; i++) {
        (void)snprintf(line, sizeof(line), "%s %ld bytes",
                       data->shapes[i].name, (long)data->sizes[i]);
        (void)bento_lcd_print(line);
        (void)snprintf(line, sizeof(line), "   %s",
                       data->shapes[i].json);
        (void)bento_lcd_print(line);
    }
    format_real(number, real_text, sizeof(real_text));
    (void)snprintf(line, sizeof(line), "ส่งเป็นตัวเลข {\"v\": %s}",
                   real_text);
    (void)bento_lcd_print(line);
    (void)bento_lcd_print("ส่งเป็นสตริง {\"v\": \"25.5\"} "
                          "(วาดกราฟไม่ได้)");
    /* the platform's bridge wraps the payload under data itself;
     * wrapping it here too turns temperature into data_temperature */
    (void)bento_lcd_print("รูปที่แพลตฟอร์มคาดหวัง");
    (void)snprintf(line, sizeof(line), "   %s",
                   data->shapes[SHAPE_GOOD].json);
    (void)bento_lcd_print(line);
    /* the platform guesses the unit from the key's name */
    (void)bento_lcd_print("ตั้งชื่อคีย์ให้ตรง ได้หน่วยฟรี");
    print_parsed_back(data->shapes[SHAPE_GOOD].json);
}

/* Screen layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 24,
    TITLE_Y = 8,
    TITLE_FONT_PX = 28,
    FONT_PX = 20,
    CARD_Y = 56,
    CARD_H = 224,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    LEFT_CARD_X = 24,
    LEFT_CARD_W = 488,
    ROW_X = 40,
    FIRST_ROW_Y = 72,
    ROW_STEP = 72, /* label 27 + gap + bar 24 */
    BAR_DY = 32,
    BAR_W = 456,
    BAR_H = 24,
    RIGHT_CARD_X = 528,
    RIGHT_CARD_W = 240,
    RIGHT_X = 544,
    CAPTION_Y = 72,
    SEG_Y = 112,
    SEG_W = 208,
    SEG_H = 88,
    UNIT_Y = 216, /* under the Seg7, which ends at y = 200 */
    NOTE_X = 24,
    NUMBER_NOTE_Y = 296,
    STRING_NOTE_Y = 328,
    RULE_NOTE_Y = 360
};

/* A position or size on the screen: widget arguments are 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t x, int16_t y, uint32_t color)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t label;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.color = color;
    args.font_px = FONT_PX;
    return bento_ui_label_create(ui, &args, &label);
}

/* A card: rounded, with a border the colour of the card itself. */
static bento_status_t add_card(bento_ui_t *ui, int16_t x, int16_t w)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;

    bento_ui_widget_args_init(&args);
    args.x = x;
    args.y = CARD_Y;
    args.w = w;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_CARD;  /* the border colour */
    args.max_val = CARD_RADIUS; /* the corner radius */
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* A Bar created with a non-zero value blanks the whole screen, so
 * it starts at 0 and gets its length afterwards. */
static bento_status_t add_bar(const payloads_t *data, int32_t row,
                              int32_t row_y)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t bar;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = ROW_X;
    args.y = px(row_y + BAR_DY);
    args.w = BAR_W;
    args.h = BAR_H;
    args.min_val = 0;
    args.max_val = data->widest;
    args.font_px = 0;
    args.color = COLOR_TRACK;
    status = bento_ui_bar_create(data->ui, &args, &bar);
    if (status == BENTO_OK) {
        /* a setter's refusal is ignored, as in MicroPython */
        (void)bento_ui_set_value(data->ui, bar, data->sizes[row]);
    }
    return status;
}

/* Left card: one caption and one bar per payload, one scale. */
static bento_status_t add_size_card(const payloads_t *data)
{
    char text[TEXT_CHARS];
    int32_t row_y = FIRST_ROW_Y;
    int32_t row;
    bento_status_t status =
        add_card(data->ui, LEFT_CARD_X, LEFT_CARD_W);

    for (row = 0; (row < SHAPE_COUNT) && (status == BENTO_OK); row++) {
        const shape_t *shape = &data->shapes[row];

        (void)snprintf(text, sizeof(text), "%s  %ld bytes", shape->name,
                       (long)data->sizes[row]);
        status =
            add_label(data->ui, text, ROW_X, px(row_y), shape->color);
        if (status == BENTO_OK) {
            status = add_bar(data, row, row_y);
        }
        row_y += ROW_STEP;
    }
    return status;
}

/* Right card: the good payload's size in big digits. The digits
 * use the accent colour: green already means "just right" here. */
static bento_status_t add_count_card(const payloads_t *data)
{
    char digits[VALUE_CHARS];
    bento_ui_widget_args_t args;
    bento_ui_id_t seg;
    bento_status_t status =
        add_card(data->ui, RIGHT_CARD_X, RIGHT_CARD_W);

    if (status == BENTO_OK) {
        status = add_label(data->ui, "ใบพอดี กินกี่ไบต์", RIGHT_X, CAPTION_Y,
                           COLOR_DIM);
    }
    if (status == BENTO_OK) {
        (void)snprintf(digits, sizeof(digits), "%ld",
                       (long)data->sizes[SHAPE_GOOD]);
        bento_ui_widget_args_init(&args);
        args.text = digits;
        args.text_len = (uint32_t)strlen(digits);
        args.x = RIGHT_X;
        args.y = SEG_Y;
        args.w = SEG_W;
        args.h = SEG_H;
        args.color = COLOR_ACCENT;
        status = bento_ui_seg7_create(data->ui, &args, &seg);
    }
    if (status == BENTO_OK) {
        status =
            add_label(data->ui, "bytes", RIGHT_X, UNIT_Y, COLOR_DIM);
    }
    return status;
}

/* The two lines differ by one pair of quotes, and the second must
 * end before x = 690. */
static bento_status_t add_notes(bento_ui_t *ui)
{
    bento_status_t status =
        add_label(ui, "ตัวเลข  {\"v\": 25.5}    ฝั่งรับวาดกราฟได้", NOTE_X,
                  NUMBER_NOTE_Y, COLOR_OK);

    if (status == BENTO_OK) {
        status = add_label(ui,
                           "สตริง   {\"v\": \"25.5\"}  "
                           "ฝั่งรับวาดกราฟไม่ได้",
                           NOTE_X, STRING_NOTE_Y, COLOR_BAD);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "พอดี = สั้น เดาออก มีหน่วย มีตัวตน", NOTE_X,
                           RULE_NOTE_Y, COLOR_DIM);
    }
    return status;
}

/* ui.poll() hands the touches over; nothing reads them, and a
 * failed poll answers no events, so its result is not needed. */
static void poll_display(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* Draws the screen in the original's order. */
static bento_status_t build_screen(const payloads_t *data)
{
    bento_status_t status =
        bento_ui_screen(data->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_t args;
        bento_ui_id_t title;

        bento_ui_widget_args_init(&args);
        args.text = "payload สามแบบ วัดเป็นไบต์";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = TITLE_X;
        args.y = TITLE_Y;
        args.color = COLOR_TEXT;
        args.font_px = TITLE_FONT_PX;
        status = bento_ui_label_create(data->ui, &args, &title);
    }
    if (status == BENTO_OK) {
        status = add_size_card(data);
    }
    if (status == BENTO_OK) {
        status = add_count_card(data);
    }
    if (status == BENTO_OK) {
        status = add_notes(data->ui);
    }
    return status;
}

int main(void)
{
    payloads_t data;
    bento_status_t status;

    (void)memset(&data, 0, sizeof(data));
    data.ui = bento_runtime_get_ui();
    build_payloads(&data, bento_time_ticks_ms());
    measure_payloads(&data);
    status = build_screen(&data);
    if (status == BENTO_OK) {
        poll_display(data.ui);
        /* the drawer ignores a console that is not ready, as
         * modlcd.c does */
        print_to_drawer(&data);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
