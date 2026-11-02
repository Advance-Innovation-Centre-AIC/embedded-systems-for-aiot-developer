/*
 * s04b_layout_widgets.c -- widget ที่ "ใส่ของอื่นไว้ข้างใน" ได้ (ฉบับฝึกเติมโค้ด)
 *
 * วิธีรัน: บนจอบอร์ด แตะการ์ด Playground ค้างไว้ เติมช่องว่างตามคำใบ้ให้ครบทีละท่า
 * ส่งขึ้นบอร์ดดูผลทุกครั้ง แล้วกด Program to Device
 * แตะแท็บสามใบด้านบนสลับไปมา แท็บ 1 มีหน้าต่างที่มีแถบหัวเรื่องของตัวเอง
 * แท็บ 2 มีจอจุดที่ไฟวิ่ง วงกลมหมุนบอกว่ากำลังทำงาน และรูปไอคอน
 * แท็บ 3 มีไทล์ที่ปัดขึ้นลงได้ รันจบเองใน 30 วินาที
 * กับดัก: add_tab, add_tile และ win_content คืน "ช่องข้างใน" ที่ต้องส่งเป็น
 * parent ของของที่วางเข้าไป ลืมส่ง ของจะไปโผล่บนจอหลักโดยไม่มีอะไรฟ้อง
 *
 * Widgets that hold other widgets: a Tabview with three tabs, a Win with
 * its own title bar, a DotMatrix with a running pattern, a Spinner, an
 * Image icon and a Tileview whose two tiles scroll up and down. Each
 * container answers an inner area that must be passed as the parent of
 * what goes inside. The program ends by itself after 30 seconds.
 *
 * This is the practice skeleton with four blanks (ท่าที่ 1-4).
 *
 * Twin of s04b_layout_widgets.py
 * (embedded-systems-for-aiot-developer/practise_codes/s04b_layout_widgets.py).
 * Style: CLEAN_C.md. The solution is solution_codes/s04b_layout_widgets.c.
 * Differs from the original:
 *   - A blank in the original is the name ____, so MicroPython stops
 *     with NameError at the first one. C must compile, so here a blank
 *     leaves the inner area 0, which means the screen itself, and the
 *     poll blank reads no events. The program runs, and everything meant
 *     for a tab lands on the main screen: the very trap the lesson
 *     names, until the blanks are filled in.
 *   - The original calls pic.icon(0) with an int and dots.set_pixels()
 *     with a list. modui.c reads the icon's name as a str and the pixels
 *     through the buffer protocol, so on the board both raise TypeError.
 *     Here the icon is named "0" (str(0)) and the eight row bytes go as
 *     a byte array, which is what the lesson means.
 *   - Stop ends the program with BENTO_E_CANCELLED and main() returns 0,
 *     where MicroPython raises KeyboardInterrupt.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

enum { /* layout, px */
    TABS_W = 792,
    TABS_H = 336, /* not 398: the console button owns (690, 340) */
    TABS_BAR_H = 44,
    BOX_X = 16,
    BOX_Y = 12,
    BOX_W = 740,
    BOX_H = 280,
    WIN_BAR_H = 40,
    LOG_X = 16,
    LOG_FIRST_Y = 16,
    LOG_PITCH = 32,
    DOTS_XY = 24,
    DOTS_SIZE = 176,
    DOTS_SIDE = 8, /* dots per row and per column, not pixels */
    SPINNER_X = 252,
    SPINNER_Y = 56,
    SPINNER_SIZE = 80,
    SPINNER_TEXT_X = 232,
    SPINNER_TEXT_Y = 148,
    PICTURE_X = 420,
    PICTURE_Y = 44,
    PICTURE_SIZE = 96,
    PICTURE_TEXT_X = 396,
    PICTURE_TEXT_Y = 152,
    TILE_TEXT_X = 24,
    TILE_TEXT_Y = 112
};

enum { /* font sizes, px */
    FONT_VALUE = 20,
    FONT_BIG = 24
};

enum { /* times and counts */
    CLEAR_MS = 200,
    STEP_MS = 400,
    STEPS = 75 /* 75 x 400 ms = 30 s */
};

enum { /* colours, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_DOTS = 0x4A9EFF
};

/* How a label looks. */
typedef struct {
    uint32_t color;
    int32_t font_px;
} label_style_t;

static const label_style_t NOTE_STYLE = {COLOR_DIM, FONT_VALUE};

/* The inner areas the containers answer, and the widgets the loop
 * changes. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t tabs;
    bento_ui_id_t tab_win;
    bento_ui_id_t tab_dots;
    bento_ui_id_t tab_tile;
    bento_ui_id_t body;
    bento_ui_id_t top_tile;
    bento_ui_id_t next_tile;
    bento_ui_id_t dots;
} layout_t;

/* A position or size on the screen: the widget arguments are 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

static bento_status_t add_label(bento_ui_t *ui, bento_ui_id_t parent,
                                const char *text, int32_t x, int32_t y,
                                const label_style_t *style)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = px(x);
    args.y = px(y);
    args.color = style->color;
    args.font_px = style->font_px;
    args.parent = parent;
    return bento_ui_label_create(ui, &args, &unused);
}

/* The Tabview; value is the tab bar's height. add_tab() answers the
 * tab's page, not the tab itself. */
static bento_status_t add_tabs(layout_t *layout)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = 0;
    args.y = 0;
    args.w = TABS_W;
    args.h = TABS_H;
    args.font_px = TABS_BAR_H;
    /* ท่าที่ 1 - สร้างแท็บสามใบ "หน้าต่าง" "จอจุด" และ "ไทล์"
     * คำใบ้: ใช้ bento_ui_add_tab() ส่ง layout->tabs ชื่อแท็บ และความยาวชื่อ (strlen)
     *        มันคืนแฮนเดิลของหน้าในแท็บนั้น
     *        ต้องเก็บค่าที่คืนมาไว้ใน layout->tab_win, layout->tab_dots และ layout->tab_tile
     *        ไม่งั้นวางของเข้าไปข้างในไม่ได้ (ตอนนี้ทั้งสามยังเป็น 0 คือจอหลัก) */
    return bento_ui_tabview_create(layout->ui, &args, &layout->tabs);
}

/* Tab 1: a Win has its own title bar; its body is the area below. */
static bento_status_t add_window(layout_t *layout)
{
    static const char *const log_lines[] = {
        "ระบบเริ่มทำงาน", "เชื่อมต่อเซนเซอร์แล้ว", "รอข้อมูลชุดแรก"};
    const uint32_t line_count =
        (uint32_t)(sizeof(log_lines) / sizeof(log_lines[0]));
    bento_ui_widget_args_t args;
    bento_ui_id_t win;
    uint32_t i;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "บันทึกเหตุการณ์";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = BOX_X;
    args.y = BOX_Y;
    args.w = BOX_W;
    args.h = BOX_H;
    args.font_px = WIN_BAR_H;
    args.parent = layout->tab_win;
    status = bento_ui_win_create(layout->ui, &args, &win);
    /* ท่าที่ 2 - ขอพื้นที่เนื้อหาใต้แถบหัวเรื่อง เก็บไว้ใน layout->body
     * คำใบ้: Win ใช้ bento_ui_win_content() ไม่ใช่ bento_ui_add_tab() */
    (void)win; /* used once ท่าที่ 2 is filled in */
    for (i = 0U; (i < line_count) && (status == BENTO_OK); i++) {
        status = add_label(
            layout->ui, layout->body, log_lines[i], LOG_X,
            LOG_FIRST_Y + ((int32_t)i * LOG_PITCH), &NOTE_STYLE);
    }
    return status;
}

static bento_status_t add_dots(layout_t *layout)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = DOTS_XY;
    args.y = DOTS_XY;
    args.w = DOTS_SIZE;
    args.h = DOTS_SIZE;
    args.color = COLOR_DOTS;
    args.min_val = DOTS_SIDE; /* cols */
    args.max_val = DOTS_SIDE; /* rows */
    args.parent = layout->tab_dots;
    return bento_ui_dotmatrix_create(layout->ui, &args, &layout->dots);
}

/* A Spinner turns by itself and has no value: "working", never progress. */
static bento_status_t add_spinner(layout_t *layout)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = SPINNER_X;
    args.y = SPINNER_Y;
    args.w = SPINNER_SIZE;
    args.h = SPINNER_SIZE;
    args.parent = layout->tab_dots;
    status = bento_ui_spinner_create(layout->ui, &args, &unused);
    if (status == BENTO_OK) {
        status =
            add_label(layout->ui, layout->tab_dots, "กำลังอ่านเซนเซอร์",
                      SPINNER_TEXT_X, SPINNER_TEXT_Y, &NOTE_STYLE);
    }
    return status;
}

/* An Image shows an icon built into the firmware. */
static bento_status_t add_picture(layout_t *layout)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t picture;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = PICTURE_X;
    args.y = PICTURE_Y;
    args.w = PICTURE_SIZE;
    args.h = PICTURE_SIZE;
    args.parent = layout->tab_dots;
    status = bento_ui_image_create(layout->ui, &args, &picture);
    if (status == BENTO_OK) {
        /* a setter: MicroPython sends it and forgets it */
        (void)bento_ui_image_icon(layout->ui, picture, "0", 1U);
        status = add_label(layout->ui, layout->tab_dots,
                           "ui.Image + .icon(0)", PICTURE_TEXT_X,
                           PICTURE_TEXT_Y, &NOTE_STYLE);
    }
    return status;
}

/* Tab 3: add_tile(col, row) answers the tile; one column, two rows, so
 * a swipe moves up and down. */
static bento_status_t add_tiles(layout_t *layout)
{
    static const label_style_t big_style = {COLOR_TEXT, FONT_BIG};
    bento_ui_widget_args_t args;
    bento_ui_id_t tiles;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = BOX_X;
    args.y = BOX_Y;
    args.w = BOX_W;
    args.h = BOX_H;
    args.parent = layout->tab_tile;
    status = bento_ui_tileview_create(layout->ui, &args, &tiles);
    /* ท่าที่ 3 - สร้างไทล์สองใบ เรียงในแนวตั้ง เก็บไว้ใน layout->top_tile และ layout->next_tile
     * คำใบ้: bento_ui_add_tile() รับ col กับ row - คอลัมน์เดียวกัน แถวต่างกัน จึงปัดขึ้นลงได้ */
    (void)tiles; /* used once ท่าที่ 3 is filled in */
    if (status == BENTO_OK) {
        status =
            add_label(layout->ui, layout->top_tile, "ปัดขึ้นเพื่อดูหน้าถัดไป",
                      TILE_TEXT_X, TILE_TEXT_Y, &big_style);
    }
    if (status == BENTO_OK) {
        status = add_label(layout->ui, layout->next_tile, "นี่คือไทล์ที่สอง",
                           TILE_TEXT_X, TILE_TEXT_Y, &big_style);
    }
    return status;
}

static bento_status_t build_screen(layout_t *layout)
{
    bento_status_t status =
        bento_ui_screen(layout->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(CLEAR_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_tabs(layout);
    }
    if (status == BENTO_OK) {
        status = add_window(layout);
    }
    if (status == BENTO_OK) {
        status = add_dots(layout);
    }
    if (status == BENTO_OK) {
        status = add_spinner(layout);
    }
    if (status == BENTO_OK) {
        status = add_picture(layout);
    }
    if (status == BENTO_OK) {
        status = add_tiles(layout);
    }
    return status;
}

/* Row k of the pattern lights dot (step + k) mod 8: the dots run. */
static void show_dots(const layout_t *layout, int32_t step)
{
    uint8_t rows[DOTS_SIDE];
    int32_t row;

    for (row = 0; row < DOTS_SIDE; row++) {
        rows[row] = (uint8_t)((step + row) % DOTS_SIDE);
    }
    /* a setter: MicroPython sends it and forgets it */
    (void)bento_ui_dotmatrix_set(layout->ui, layout->dots, rows,
                                 (uint32_t)DOTS_SIDE);
}

/* Rule 1 of the ui module: poll every round, even when nothing is used.
 * Each event is one tap. */
static bento_status_t run_steps(const layout_t *layout, int32_t *taps)
{
    uint32_t count;
    int32_t step;
    bento_status_t status = BENTO_OK;

    for (step = 0; (step < STEPS) && (status == BENTO_OK); step++) {
        /* ท่าที่ 4 - อ่านเหตุการณ์ทุกรอบ ตามกฎข้อ 1 ของโมดูล ui
         * คำใบ้: เรียก bento_ui_poll() ใส่อาร์เรย์ bento_ui_event_t ขนาด BENTO_UI_POLL_EVENTS_MAX
         *        แล้วให้ count เป็นจำนวนเหตุการณ์ที่ได้ (0 เมื่อ poll ไม่สำเร็จ) */
        count = 0U;
        *taps += (int32_t)count;
        show_dots(layout, step);
        status = bento_time_sleep_ms(STEP_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    layout_t layout = {0};
    int32_t taps = 0;
    bento_status_t status;

    layout.ui = bento_runtime_get_ui();
    status = build_screen(&layout);
    if (status == BENTO_OK) {
        status = run_steps(&layout, &taps);
    }
    if (status == BENTO_OK) {
        (void)printf("s04b: แตะทั้งหมด %" PRId32 " ครั้ง\n", taps);
        (void)printf("Tabview .add_tab() · Win .content() · Tileview "
                     ".add_tile()\n");
        (void)printf(
            "ทั้งสามคืนแฮนเดิลของช่องข้างใน ต้องส่งเป็น parent= เสมอ\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
