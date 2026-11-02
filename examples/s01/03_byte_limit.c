/*
 * 03_byte_limit.c -- นับไบต์ก่อนพิมพ์ (Count the bytes before you print)
 *
 * ข้อความยาวเกิน 127 ไบต์ จะถูกตัดหายเงียบ ๆ จอนับไบต์ ไม่ได้นับตัวอักษร
 * ไทยหนึ่งตัวกิน 3 ไบต์ ตัวเลขไบต์ของก้อนที่กำลังวัดเปลี่ยนทุกวินาที
 * เขียวคือส่งได้ครบ แดงคือเกินเพดาน ข้อความจริงไปรอในลิ้นชัก Console
 * กับดัก: การตัดเกิดขึ้นเงียบ ๆ และ \n ท้ายบรรทัดหายไปด้วย
 *
 * A text longer than 127 bytes is cut off without a word. The screen
 * counts bytes, not characters, and one Thai character takes 3 bytes.
 * The byte count of the piece being measured changes every second:
 * green means it goes through whole, red means it is over the limit.
 * The real texts wait in the Console drawer. Trap: the cut is silent,
 * and the line's newline goes too, so the next line runs on.
 *
 * Twin of 03_byte_limit.py
 * (embedded-systems-for-aiot-developer/examples/s01/03_byte_limit.py).
 * Style: CLEAN_C.md.
 * Where C differs: len(str) counts the UTF-8 lead bytes (code points)
 * and len(str.encode()) is strlen(); the items list that the original
 * grows with append() is the fixed table plus the pieces cut in a
 * loop, in the same order.
 *
 * ตาคุณ: เพิ่มชื่อสมาชิกในทีมเป็นภาษาไทยลงใน items แล้วทำนายก่อนรันว่า
 * ตัวเลขไบต์จะเป็นเท่าไร และเป็นสีเขียวหรือแดง
 * Your turn: add a team member's Thai name to items, predict its byte
 * count and colour before you run, then compare (hint: Thai = 3 bytes,
 * a space or a digit = 1).
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
    LIMIT_BYTES = 127, /* the most one call carries */
    CHUNK_CHARS = 40,  /* pieces cut by hand (40 Thai = 120 bytes) */
    FIXED_COUNT = 4,   /* the rows of items before the pieces */
    UTF8_MAX_BYTES = 4 /* the longest UTF-8 character */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    SHOW_MS = 900    /* each piece stays long enough to read */
};

/* Layout: one card holds the number on the left, the story on the
 * right, so the eye meets "how much" before "of what". */
enum {
    LEFT_X = 24,
    TITLE_Y = 8,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 224,
    CARD_RADIUS = 12,
    CARD_VALUE = 1,
    NUMBER_X = 40,
    UNIT_Y = 88,
    SEG_Y = 128,
    SEG_W = 208,
    SEG_H = 96,
    STORY_X = 288,
    WHAT_Y = 88,
    STATUS_Y = 152,
    FOOTER_Y = 352,
    FONT_TITLE_PX = 28,
    FONT_WHAT_PX = 24,
    FONT_SMALL_PX = 20
};

/* The course palette, 0xRRGGBB. Green and red here are verdicts:
 * whole or cut, not decoration. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_BAD = 0xE5484D
};

enum {
    PIECE_BYTES = (CHUNK_CHARS * UTF8_MAX_BYTES) + 1,
    NAME_BYTES = 64, /* "แบ่งเอง ก้อนที่ " (44 bytes) and any int */
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* 66 characters, but 198 bytes: over the limit on purpose. */
static const char LONG_LINE[] = "บอร์ดนี้มีสมองสองก้อนคือคอร์สามสามกับ"
                                "คอร์ห้าห้าทำงานคนละหน้าที่กัน";

typedef struct {
    const char *name;
    const char *text;
} item_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} text_style_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t seg;
    bento_ui_id_t what;
    bento_ui_id_t status;
} byte_screen_t;

/* len() of a str: characters, every byte that does not continue a
 * UTF-8 sequence. */
static int32_t utf8_length(const char *text)
{
    const uint8_t top_bits = 0xC0U;
    const uint8_t continuation = 0x80U;
    int32_t count = 0;
    size_t i;

    for (i = 0U; text[i] != '\0'; i++) {
        if (((uint8_t)text[i] & top_bits) != continuation) {
            count++;
        }
    }
    return count;
}

/* Byte offset of character number `index` (the text's length when it
 * has fewer characters). */
static size_t utf8_offset(const char *text, int32_t index)
{
    const uint8_t top_bits = 0xC0U;
    const uint8_t continuation = 0x80U;
    int32_t seen = 0;
    size_t i = 0U;
    bool found = false;

    while ((text[i] != '\0') && !found) {
        if (((uint8_t)text[i] & top_bits) != continuation) {
            found = (seen == index);
            seen++;
        }
        if (!found) {
            i++;
        }
    }
    return i;
}

static void set_text(const byte_screen_t *screen, bento_ui_id_t widget,
                     const char *text)
{
    /* a setter's refusal was ignored before too (modui.c) */
    (void)bento_ui_set_text(screen->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void poll_once(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* ui.poll() answers no events when it fails; none are read */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const text_style_t *style,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = style->x;
    args.y = style->y;
    args.color = style->color;
    args.font_px = style->font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

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
    args.min_val = COLOR_CARD;
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_VALUE;
    return bento_ui_panel_create(ui, &args, &card);
}

/* Sent as text to control its format; the Seg7 digits are always
 * 28 px, w and h only size the box they sit in. */
static bento_status_t add_seg(byte_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0";
    args.text_len = 1U;
    args.x = NUMBER_X;
    args.y = SEG_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = COLOR_OK;
    return bento_ui_seg7_create(screen->ui, &args, &screen->seg);
}

static bento_status_t add_story(byte_screen_t *screen)
{
    const text_style_t what = {STORY_X, WHAT_Y, COLOR_TEXT,
                               FONT_WHAT_PX};
    const text_style_t status_style = {STORY_X, STATUS_Y, COLOR_DIM,
                                       FONT_SMALL_PX};
    bento_status_t status =
        add_label(screen->ui, "กำลังจะเริ่มวัด", &what, &screen->what);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่ได้วัดก้อนไหน", &status_style,
                           &screen->status);
    }
    return status;
}

static bento_status_t build_screen(byte_screen_t *screen)
{
    const text_style_t title = {LEFT_X, TITLE_Y, COLOR_TEXT,
                                FONT_TITLE_PX};
    const text_style_t unit = {NUMBER_X, UNIT_Y, COLOR_DIM,
                               FONT_SMALL_PX};
    const text_style_t footer = {LEFT_X, FOOTER_Y, COLOR_DIM,
                                 FONT_SMALL_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "นับไบต์ก่อนพิมพ์", &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui);
    }
    /* a bare number means nothing: the unit above makes it readable */
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ขนาดเป็นไบต์", &unit, &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg(screen);
    }
    if (status == BENTO_OK) {
        status = add_story(screen);
    }
    /* the limit stays in sight for whoever walks up halfway */
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "เพดานจริง 127 ไบต์ต่อหนึ่งครั้ง",
                           &footer, &unused);
    }
    return status;
}

/* One item: its size on the card, then the real text in the drawer
 * to see where the screen cuts it. lcd.print() ignores a console that
 * is not ready (modlcd.c). */
static bento_status_t measure(const byte_screen_t *screen,
                              const item_t *item)
{
    const int32_t byte_count = (int32_t)strlen(item->text);
    const bool over = byte_count > LIMIT_BYTES;
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%s - %d ตัวอักษร", item->name,
                   (int)utf8_length(item->text));
    set_text(screen, screen->what, text);
    (void)snprintf(text, sizeof(text), "%d", (int)byte_count);
    set_text(screen, screen->seg, text);
    (void)bento_ui_set_color(screen->ui, screen->seg,
                             over ? COLOR_BAD : COLOR_OK);
    (void)snprintf(text, sizeof(text),
                   over ? "%d ไบต์ เกินเพดาน ส่วนที่เกินหายเงียบ ๆ"
                        : "%d ไบต์ ส่งได้ครบทุกตัว",
                   (int)byte_count);
    set_text(screen, screen->status, text);
    (void)snprintf(text, sizeof(text), "<span class=muted>%s</span>",
                   item->name);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print(item->text);
    poll_once(screen->ui);
    return sleep_checked(SHOW_MS);
}

static bento_status_t measure_fixed(const byte_screen_t *screen)
{
    /* an empty tag eats bytes before a single character is printed */
    static const char tag[] = "<span class=muted></span>";
    static const item_t items[FIXED_COUNT] = {
        {"hello - อังกฤษ 5 ตัว", "hello"},
        {"สวัสดี - ไทย 6 ตัว", "สวัสดี"},
        {"แท็ก span เปล่า ๆ", tag},
        {"ประโยคยาว ส่งทั้งก้อน", LONG_LINE},
    };
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < (uint32_t)FIXED_COUNT) && (status == BENTO_OK);
         i++) {
        status = measure(screen, &items[i]);
    }
    return status;
}

/* Cut at a character, never inside one: half a character cannot be
 * decoded and the screen shows rubbish. */
static bento_status_t measure_pieces(const byte_screen_t *screen)
{
    const int32_t length = utf8_length(LONG_LINE);
    char piece[PIECE_BYTES];
    char name[NAME_BYTES];
    item_t item = {name, piece};
    bento_status_t status = BENTO_OK;
    int32_t start;

    for (start = 0; (start < length) && (status == BENTO_OK);
         start += CHUNK_CHARS) {
        const size_t from = utf8_offset(LONG_LINE, start);
        const size_t to = utf8_offset(LONG_LINE, start + CHUNK_CHARS);

        (void)memcpy(piece, &LONG_LINE[from], to - from);
        piece[to - from] = '\0';
        (void)snprintf(name, sizeof(name), "แบ่งเอง ก้อนที่ %" PRId32,
                       (start / CHUNK_CHARS) + 1);
        status = measure(screen, &item);
    }
    return status;
}

int main(void)
{
    byte_screen_t screen = {bento_runtime_get_ui(), 0U, 0U, 0U};
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>นับไบต์ก่อนพิมพ์</h2>");
        status = measure_fixed(&screen);
    }
    if (status == BENTO_OK) {
        status = measure_pieces(&screen);
    }
    /* piece 4 is the whole sentence, cut; pieces 5-6 are the same
     * sentence split by hand */
    if (status == BENTO_OK) {
        set_text(&screen, screen.what, "วัดครบทุกก้อนแล้ว");
        set_text(&screen, screen.status,
                 "เทียบก้อนที่ 4 กับก้อนที่ 5-6 ในลิ้นชัก");
        (void)bento_lcd_print(
            "<span class=ok>วัดครบแล้ว - แบ่งก่อนส่ง จึงได้ครบ</span>");
        poll_once(screen.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
