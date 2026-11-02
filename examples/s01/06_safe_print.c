/*
 * 06_safe_print.c -- พิมพ์ยาวเท่าไรก็ไม่หาย (A print helper that is
 * never cut off)
 *
 * รู้เพดาน 127 ไบต์แล้ว แต่ถ้าต้องนับไบต์ด้วยมือทุกบรรทัด สักวันจะลืม
 * จึงย้ายกฎนั้นเข้าไปอยู่ในฟังก์ชัน safe_print() แล้วเรียกแทน lcd.print()
 * ตัวเลขจำนวนก้อนเพิ่มขึ้นทีละก้อน แถบไบต์ของก้อนล่าสุดขยับตาม และไม่มีก้อน
 * ไหนชนขีดแดง ตัวข้อความอยู่ในลิ้นชัก Console
 * กับดัก: ฟังก์ชันนี้ตัดที่ตัวอักษร ไม่ใช่ที่คำ และไม่รู้จักแท็ก
 *
 * Knowing the 127-byte limit is not enough: count bytes by hand on
 * every line and one day a line is forgotten. So the rule moves into
 * one function, safe_print(), called instead of lcd.print(). The piece
 * count goes up one by one, the bar shows the last piece's bytes, and
 * no piece reaches the red line; the text is in the Console drawer.
 * Trap: it cuts at a character, not at a word, and knows nothing of
 * tags, so never pass it a text with tags.
 *
 * Twin of 06_safe_print.py
 * (embedded-systems-for-aiot-developer/examples/s01/06_safe_print.py).
 * Style: CLEAN_C.md.
 * Where C differs: the chunk grows in a fixed buffer of SAFE_BYTES + 1
 * instead of a new str per character; flush() is a function of its
 * own instead of a nested one, and gets the widgets as a struct.
 *
 * ตาคุณ: ลด SAFE_BYTES เหลือ 40 แล้วตอบว่าทำไมตัวอักษรไม่หายสักตัว
 * Your turn: set SAFE_BYTES to 40 and explain why not one character
 * is lost (hint: read the line that checks "before adding").
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* 127 is the real ceiling; 7 bytes are kept as a safety margin in
 * case lcd adds something at the end. */
enum {
    LIMIT_BYTES = 127,
    SAFE_BYTES = 120,
    PREVIEW_CHARS = 24, /* the piece label shows this much of a chunk */
    UTF8_MAX_BYTES = 4,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    FLUSH_MS = 400   /* each piece stays long enough to see */
};

/* Layout on the 792 x 398 canvas. */
enum {
    FONT_PX = 20,
    TITLE_FONT_PX = 24,
    TITLE_X = 20,
    TITLE_Y = 12,
    CARD_X = 20,
    CARD_Y = 52,
    CARD_W = 652,
    CARD_H = 172,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    CAPTION_Y = 62,
    COUNT_X = 40,
    SEG_Y = 92,
    SEG_W = 112,
    SEG_H = 60,
    SIZE_CAPTION_X = 248,
    BAR_X = 212,
    BAR_Y = 100,
    BAR_W = 420,
    BAR_H = 28,
    SIZE_Y = 134,
    PIECE_Y = 172,
    STATUS_X = 20,
    STATUS_Y = 240
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_INFO = 0x4A9EFF
};

static const char STORY[] =
    "บอร์ดนี้มีคอร์สองก้อนทำงานพร้อมกัน "
    "ก้อนแรกรัน Python ที่เราเขียน อีกก้อนวาดจอกับรันโมเดล "
    "สองก้อนคุยกันผ่านกล่องจดหมายที่เรียกว่า IPC "
    "เวลาเราสั่ง lcd.print "
    "ข้อความจึงต้องเดินทางข้ามไปอีกก้อนก่อนถึงจะขึ้นจอ";

typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} text_style_t;

/* The widgets flush() and the summary write over. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t seg;
    bento_ui_id_t bar;
    bento_ui_id_t size_label;
    bento_ui_id_t piece;
    bento_ui_id_t status;
} print_screen_t;

/* len(ch.encode()) of the character whose first byte is lead. */
static uint32_t utf8_char_bytes(uint8_t lead)
{
    uint32_t bytes = UTF8_MAX_BYTES;

    if (lead < 0x80U) {
        bytes = 1U;
    } else if ((lead & 0xE0U) == 0xC0U) {
        bytes = 2U;
    } else if ((lead & 0xF0U) == 0xE0U) {
        bytes = 3U;
    } else {
        bytes = UTF8_MAX_BYTES;
    }
    return bytes;
}

/* Bytes of the first `count` characters of a text of `length` bytes. */
static uint32_t utf8_prefix_bytes(const char *text, uint32_t length,
                                  uint32_t count)
{
    uint32_t i = 0U;
    uint32_t seen = 0U;

    while ((i < length) && (seen < count)) {
        i += utf8_char_bytes((uint8_t)text[i]);
        seen++;
    }
    return (i > length) ? length : i;
}

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* A setter's refusal was ignored before too (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static void poll_once(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* ui.poll() answers no events when it fails; none are read */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
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
    args.x = CARD_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border colour */
    args.max_val = CARD_RADIUS; /* and max= its corner radius */
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &card);
}

static bento_status_t add_seg(print_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0";
    args.text_len = 1U;
    args.x = COUNT_X;
    args.y = SEG_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    args.color = COLOR_OK;
    return bento_ui_seg7_create(screen->ui, &args, &screen->seg);
}

static bento_status_t add_bar(print_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = BAR_X;
    args.y = BAR_Y;
    args.w = BAR_W;
    args.h = BAR_H;
    args.min_val = 0;
    args.max_val = LIMIT_BYTES;
    args.font_px = 0;
    return bento_ui_bar_create(screen->ui, &args, &screen->bar);
}

/* The title, the card, and the piece count on the card's left. */
static bento_status_t build_card(print_screen_t *screen)
{
    const text_style_t title = {TITLE_X, TITLE_Y, COLOR_TEXT,
                                TITLE_FONT_PX};
    const text_style_t caption = {COUNT_X, CAPTION_Y, COLOR_DIM,
                                  FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "พิมพ์ยาวเท่าไรก็ไม่หาย", &title, &unused);

    if (status == BENTO_OK) {
        status = add_card(screen->ui);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "ก้อนที่ปล่อยไปแล้ว", &caption, &unused);
    }
    if (status == BENTO_OK) {
        status = add_seg(screen);
    }
    return status;
}

/* The last piece's size, its first characters, and the status line. */
static bento_status_t build_details(print_screen_t *screen)
{
    const text_style_t caption = {SIZE_CAPTION_X, CAPTION_Y, COLOR_DIM,
                                  FONT_PX};
    const text_style_t size = {BAR_X, SIZE_Y, COLOR_INFO, FONT_PX};
    const text_style_t piece = {COUNT_X, PIECE_Y, COLOR_TEXT, FONT_PX};
    const text_style_t status_style = {STATUS_X, STATUS_Y, COLOR_DIM,
                                       FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "ขนาดก้อนล่าสุด (ไบต์)", &caption, &unused);

    if (status == BENTO_OK) {
        status = add_bar(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "0 / 120 ไบต์ - เพดานจริง 127",
                           &size, &screen->size_label);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่ได้ปล่อยก้อนไหน", &piece,
                           &screen->piece);
    }
    /* never an empty label: LVGL would show "Label" until written */
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ยังไม่ได้วัดอะไร", &status_style,
                           &screen->status);
    }
    return status;
}

static bento_status_t build_screen(print_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = build_card(screen);
    }
    if (status == BENTO_OK) {
        status = build_details(screen);
    }
    return status;
}

/* Every piece sent shows its size on the screen at once, so the
 * viewer sees that none is over the limit instead of taking it on
 * trust. lcd.print() ignores a console that is not ready (modlcd.c). */
static bento_status_t flush(const print_screen_t *screen,
                            const char *chunk, uint32_t length)
{
    char text[TEXT_BYTES];
    uint32_t cut;

    (void)bento_lcd_print(chunk);
    (void)bento_ui_set_value(screen->ui, screen->bar, (int32_t)length);
    (void)snprintf(text, sizeof(text), "%d / %d ไบต์ - เพดานจริง 127",
                   (int)length, (int)SAFE_BYTES);
    set_text(screen->ui, screen->size_label, text);
    cut = utf8_prefix_bytes(chunk, length, PREVIEW_CHARS);
    (void)snprintf(text, sizeof(text), "%.*s...", (int)cut, chunk);
    set_text(screen->ui, screen->piece, text);
    poll_once(screen->ui);
    return sleep_checked(FLUSH_MS);
}

/* A piece goes out, then the count on the card goes up. */
static bento_status_t send_piece(const print_screen_t *screen,
                                 const char *chunk, uint32_t length,
                                 int32_t *sent)
{
    char text[TEXT_BYTES];
    bento_status_t status = flush(screen, chunk, length);

    if (status == BENTO_OK) {
        *sent += 1;
        (void)snprintf(text, sizeof(text), "%d", (int)*sent);
        set_text(screen->ui, screen->seg, text);
    }
    return status;
}

/* Prints a text of any length, split into pieces automatically;
 * *out_sent is the number of pieces. */
static bento_status_t safe_print(const print_screen_t *screen,
                                 const char *text, int32_t *out_sent)
{
    const uint32_t text_length = (uint32_t)strlen(text);
    char chunk[SAFE_BYTES + 1];
    uint32_t used = 0U; /* bytes in the chunk being gathered */
    uint32_t i = 0U;
    int32_t sent = 0;
    bento_status_t status = BENTO_OK;

    if (text_length <= (uint32_t)SAFE_BYTES) {
        /* the easy case first: short enough, send it as it is */
        status = flush(screen, text, text_length);
        sent = 1;
        i = text_length;
    }
    while ((i < text_length) && (status == BENTO_OK)) {
        /* this character's size in bytes: Thai 3, English 1 */
        const uint32_t size = utf8_char_bytes((uint8_t)text[i]);

        /* check before adding, not after, or the piece sent would
         * already be over the limit */
        if ((used + size) > (uint32_t)SAFE_BYTES) {
            status = send_piece(screen, chunk, used, &sent);
            used = 0U;
        }
        (void)memcpy(&chunk[used], &text[i], size);
        used += size;
        chunk[used] = '\0';
        i += size;
    }
    /* the characters left in the last piece go too, or the end of the
     * sentence is lost */
    if ((status == BENTO_OK) && (used != 0U)) {
        status = send_piece(screen, chunk, used, &sent);
    }
    *out_sent = sent;
    return status;
}

int main(void)
{
    print_screen_t screen = {
        bento_runtime_get_ui(), 0U, 0U, 0U, 0U, 0U};
    char text[TEXT_BYTES];
    const int32_t total = (int32_t)strlen(STORY);
    int32_t pieces = 0;
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>พิมพ์ยาวเท่าไรก็ไม่หาย</h2>");
        /* measure first, then tell the reader what is coming */
        (void)snprintf(text, sizeof(text),
                       "ข้อความยาว %d ไบต์ - เกินเพดาน %d เท่า", (int)total,
                       (int)(total / LIMIT_BYTES));
        set_text(screen.ui, screen.status, text);
        (void)snprintf(text, sizeof(text), "ข้อความยาว %d ไบต์",
                       (int)total);
        (void)bento_lcd_print(text);
        status = safe_print(&screen, STORY, &pieces);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "แบ่งเป็น %d ก้อน - ไม่มีตัวอักษรไหนหายไปเลย",
                       (int)pieces);
        set_text(screen.ui, screen.status, text);
        set_text(screen.ui, screen.piece,
                 "ครบทุกตัว อ่านได้ในลิ้นชัก Console");
        (void)bento_lcd_print("<span class=ok>ครบทุกตัว ไม่มีหาย</span>");
        poll_once(screen.ui);
        /* the raw numbers are too long for the 4.3-inch screen: they
         * go to the computer's console */
        (void)printf("ความยาวจริงเป็นไบต์: %d | แบ่งได้ %d ก้อน\n",
                     (int)total, (int)pieces);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
