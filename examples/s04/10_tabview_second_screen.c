/*
 * 10_tabview_second_screen.c -- Tabview: จอที่สองในโปรแกรมเดียว
 *
 * ของหกอย่างไม่ลงจอเดียว แต่ลงสามแท็บได้ Tabview มีสามหน้า ตอนนี้ (ค่าปัจจุบัน
 * ตัวโต) ทั้งวัน (ค่าสูงสุดกับแถบ) และเกณฑ์ (Spinbox ตั้งเกณฑ์แจ้งเตือน) ค่าจำลอง
 * เป็นคลื่นสามเหลี่ยมช้า ๆ 240 รอบ รอบละ 160 ms แตะแถบแท็บแล้วบรรทัดล่างบอกว่า
 * อยู่แท็บไหน บรรทัดนั้นอยู่นอก Tabview จึงเห็นได้ทุกแท็บ
 *
 * Six things do not fit one screen, but they fit three tabs. The
 * Tabview has three pages: now (the current value, big), day (the
 * peak as a number and a bar) and threshold (a spinbox). A simulated
 * slow triangle wave runs 240 rounds of 160 ms; tap a tab and the line
 * below says which one is open. That line is outside the Tabview, so
 * every tab shows it.
 *
 * Twin of 10_tabview_second_screen.py
 * (embedded-systems-for-aiot-developer/examples/s04/10_tabview_second_screen.py),
 * from the library twin aiot-s04-10-tabview-second-screen. Style:
 * CLEAN_C.md. The course file runs one wave step, one poll and one
 * 160 ms sleep per round, so the twin's separate 20 ms tap clock is
 * gone. "%.1f" is bento_fmt_real_precision().
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add, or the simulated temperature differs in
 * the last digit. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

/* Timing, the simulated wave, and the limits. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    ROUNDS = 240,    /* about forty seconds at 160 ms a round */
    STEP_MS = 160,   /* one round of the wave and the taps */
    SHOW_EVERY = 8,  /* write the numbers about once a second */
    /* one triangle: up for half of it, down for the other half */
    WAVE_STEPS = 120,
    WARN_AT = 60,      /* degrees: from here the number turns orange */
    PERCENT_MAX = 100, /* the peak bar's range is 0-100 */
    THRESHOLD_START = 60,
    TAB_COUNT = 3,
    TEXT_BYTES = 128 /* the longest text built here is 88 bytes */
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t tabs;
    bento_ui_id_t now_digits;
    bento_ui_id_t peak_digits;
    bento_ui_id_t peak_bar;
    bento_ui_id_t threshold;
    bento_ui_id_t line;
} tab_screen_t;

/* The simulated sensor: the round it is in and the highest value so
 * far. */
typedef struct {
    int32_t round;
    bento_real_t peak;
} wave_t;

/* The course palette, 0xRRGGBB: one value per role, one accent per
 * screen. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_OK = 0x30A46C
};

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The simulated temperature of a round: a slow triangle from 20.0 up
 * to 65.0 and back. */
static bento_real_t wave_temperature(int32_t round)
{
    const bento_real_t base = (bento_real_t)20.0;
    const bento_real_t per_step = (bento_real_t)0.75;
    /* round >= 0: C's % is Python's here */
    const int32_t phase = round % WAVE_STEPS;
    const int32_t rise =
        (phase < (WAVE_STEPS / 2)) ? phase : (WAVE_STEPS - phase);
    const bento_real_t lift = (bento_real_t)rise * per_step;

    return base + lift; /* two statements: see FP_CONTRACT at the top */
}

/* True when the event is of this kind and came from this widget. */
static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* Layout on the 792 x 398 canvas. Inside a tab, x and y count from
 * the tab page's corner. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    TABS_Y = 64,
    TABS_W = 744,
    /* ends at y=336: the corner below belongs to the firmware's
     * Console button */
    TABS_H = 272,
    /* a tab is touched, so its bar is at least 88 px tall */
    TAB_BAR_H = 88,
    PAGE_X = 16,
    PAGE_CAPTION_Y = 16,
    BIG_Y = 48,
    BIG_W = 224,
    BIG_H = 88,
    SIDE_X = 256,
    SIDE_LABEL_Y = 80,
    BAR_Y = 72,
    BAR_W = 400,
    BAR_H = 40,
    LINE_Y = 344,
    TITLE_PX = 28,
    TEXT_PX = 20
};

/* The tabs' names: created in this order, and named again when one
 * is opened. */
static const char *const TAB_NAMES[TAB_COUNT] = {"ตอนนี้", "ทั้งวัน",
                                                 "เกณฑ์"};

/* Where a widget goes: x and y inside its parent (0 = the screen). */
typedef struct {
    int16_t x;
    int16_t y;
    bento_ui_id_t parent;
} spot_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                spot_t spot, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = spot.x;
    args.y = spot.y;
    args.color = color;
    args.font_px = font_px;
    args.parent = spot.parent;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A dim caption inside a tab page. */
static bento_status_t add_caption(bento_ui_t *ui, const char *text,
                                  spot_t spot)
{
    bento_ui_id_t unused;

    return add_label(ui, text, spot, COLOR_DIM, TEXT_PX, &unused);
}

/* The big seven-segment number of a tab, starting at "0.0". */
static bento_status_t add_digits(bento_ui_t *ui, bento_ui_id_t page,
                                 uint32_t color, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0.0";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = PAGE_X;
    args.y = BIG_Y;
    args.w = BIG_W;
    args.h = BIG_H;
    args.color = color;
    args.parent = page;
    return bento_ui_seg7_create(ui, &args, out_id);
}

/* Tab 1: the current value. */
static bento_status_t fill_now_tab(tab_screen_t *screen,
                                   bento_ui_id_t page)
{
    const spot_t caption = {PAGE_X, PAGE_CAPTION_Y, page};
    const spot_t unit = {SIDE_X, SIDE_LABEL_Y, page};
    bento_status_t status =
        add_caption(screen->ui, "อุณหภูมิห้องเครื่อง", caption);

    if (status == BENTO_OK) {
        status = add_digits(screen->ui, page, COLOR_ACCENT,
                            &screen->now_digits);
    }
    if (status == BENTO_OK) {
        status = add_caption(screen->ui, "องศาเซลเซียส", unit);
    }
    return status;
}

/* Tab 2: the peak of this run, as a number and as a bar. */
static bento_status_t fill_day_tab(tab_screen_t *screen,
                                   bento_ui_id_t page)
{
    const spot_t caption = {PAGE_X, PAGE_CAPTION_Y, page};
    bento_ui_widget_args_t args;
    bento_status_t status =
        add_caption(screen->ui, "สูงสุดที่เจอในรอบนี้", caption);

    if (status == BENTO_OK) {
        status = add_digits(screen->ui, page, COLOR_TEXT,
                            &screen->peak_digits);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = SIDE_X;
        args.y = BAR_Y;
        args.w = BAR_W;
        args.h = BAR_H;
        args.color = COLOR_ACCENT;
        args.min_val = 0;
        args.max_val = PERCENT_MAX;
        args.font_px = 0; /* the bar starts empty */
        args.parent = page;
        status =
            bento_ui_bar_create(screen->ui, &args, &screen->peak_bar);
    }
    return status;
}

/* Tab 3: the warning threshold, set with the spinbox. */
static bento_status_t fill_threshold_tab(tab_screen_t *screen,
                                         bento_ui_id_t page)
{
    const spot_t caption = {PAGE_X, PAGE_CAPTION_Y, page};
    const spot_t hint = {SIDE_X, SIDE_LABEL_Y, page};
    bento_ui_widget_args_t args;
    bento_status_t status =
        add_caption(screen->ui, "แจ้งเตือนเมื่อเกินกี่องศา", caption);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = PAGE_X;
        args.y = BIG_Y;
        args.w = BIG_W;
        args.h = BIG_H;
        args.color = COLOR_TEXT;
        args.min_val = 0;
        args.max_val = PERCENT_MAX;
        args.font_px = THRESHOLD_START;
        args.parent = page;
        status = bento_ui_spinbox_create(screen->ui, &args,
                                         &screen->threshold);
    }
    if (status == BENTO_OK) {
        status = add_caption(screen->ui, "แตะลูกศรขึ้นลงเพื่อปรับ", hint);
    }
    return status;
}

/* The Tabview and its three pages (four handles before anything is
 * in them). */
static bento_status_t add_tabs(tab_screen_t *screen,
                               bento_ui_id_t *pages)
{
    bento_ui_widget_args_t args;
    bento_status_t status;
    uint32_t i;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = TABS_Y;
    args.w = TABS_W;
    args.h = TABS_H;
    args.color = COLOR_CARD;
    /* value= of a Tabview is its tab bar's height */
    args.font_px = TAB_BAR_H;
    status = bento_ui_tabview_create(screen->ui, &args, &screen->tabs);
    for (i = 0U; (i < (uint32_t)TAB_COUNT) && (status == BENTO_OK);
         i++) {
        status =
            bento_ui_add_tab(screen->ui, screen->tabs, TAB_NAMES[i],
                             (uint32_t)strlen(TAB_NAMES[i]), &pages[i]);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(tab_screen_t *screen)
{
    const spot_t title = {MARGIN_X, TITLE_Y, 0U};
    const spot_t line = {MARGIN_X, LINE_Y, 0U};
    bento_ui_id_t pages[TAB_COUNT] = {0U, 0U, 0U};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "หกอย่างไม่ลงจอเดียว "
                           "ลงสามแท็บได้",
                           title, COLOR_TEXT, TITLE_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_tabs(screen, pages);
    }
    if (status == BENTO_OK) {
        status = fill_now_tab(screen, pages[0]);
    }
    if (status == BENTO_OK) {
        status = fill_day_tab(screen, pages[1]);
    }
    if (status == BENTO_OK) {
        status = fill_threshold_tab(screen, pages[2]);
    }
    if (status == BENTO_OK) {
        /* outside the Tabview, so every tab shows it */
        status = add_label(screen->ui, "แตะแถบข้างบนเพื่อสลับแท็บ", line,
                           COLOR_DIM, TEXT_PX, &screen->line);
    }
    return status;
}

/* A Seg7 shows a real number with one decimal. */
static void show_real(const tab_screen_t *screen, bento_ui_id_t digits,
                      bento_real_t value)
{
    char text[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;

    /* BENTO_FMT_BUF_SIZE holds every result, so the format cannot
     * fail */
    (void)bento_fmt_real_precision(text, BENTO_FMT_BUF_SIZE, value, 1,
                                   &length);
    (void)bento_ui_set_text(screen->ui, digits, text, length);
}

/* One round of the wave; the numbers are written only every SHOW_EVERY
 * rounds, because a number that changes faster than once a second
 * cannot be read. */
static void step_wave(const tab_screen_t *screen, wave_t *wave)
{
    const bento_real_t temperature = wave_temperature(wave->round);

    if (temperature > wave->peak) {
        wave->peak = temperature;
    }
    if ((wave->round % SHOW_EVERY) == 0) {
        /* MicroPython's widget setters ignore a refusal (modui.c),
         * so the twin does too. */
        show_real(screen, screen->now_digits, temperature);
        show_real(screen, screen->peak_digits, wave->peak);
        (void)bento_ui_set_value(screen->ui, screen->peak_bar,
                                 (int32_t)wave->peak);
        (void)bento_ui_set_color(screen->ui, screen->now_digits,
                                 (temperature >= (bento_real_t)WARN_AT)
                                     ? COLOR_WARN
                                     : COLOR_ACCENT);
    }
    wave->round++;
}

/* The line under the tabs says which tab is open (0 is the first). */
static void show_tab(const tab_screen_t *screen, int32_t active)
{
    char text[TEXT_BYTES];
    const char *name = "?";

    if ((active >= 0) && (active < TAB_COUNT)) {
        name = TAB_NAMES[active];
    }
    (void)snprintf(text, sizeof(text),
                   "อยู่แท็บที่ %" PRId32 " ชื่อ "
                   "%s",
                   active, name);
    (void)bento_ui_set_text(screen->ui, screen->line, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_color(screen->ui, screen->line, COLOR_ACCENT);
    (void)snprintf(text, sizeof(text), "สลับไปแท็บ %" PRId32 " %s",
                   active, name);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
}

static void show_threshold(const tab_screen_t *screen, int32_t degrees)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "เกณฑ์แจ้งเตือนใหม่คือ %" PRId32 " องศา", degrees);
    (void)bento_ui_set_text(screen->ui, screen->line, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_color(screen->ui, screen->line, COLOR_OK);
    (void)snprintf(text, sizeof(text), "เกณฑ์ใหม่ %" PRId32, degrees);
    (void)bento_lcd_print(text);
}

/* The Tabview and the spinbox each send value_changed, under their
 * own handles. */
static void handle_events(const tab_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    /* ui.poll() answers no events when it fails, so a failed poll is
     * not an error here. */
    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (event_is(&events[i], BENTO_UI_EVENT_VALUE_CHANGED,
                     screen->tabs)) {
            show_tab(screen, events[i].value);
        } else if (event_is(&events[i], BENTO_UI_EVENT_VALUE_CHANGED,
                            screen->threshold)) {
            show_threshold(screen, events[i].value);
        } else {
            /* any other event is not this program's */
        }
    }
}

/* ROUNDS rounds: one step of the wave, the taps, then a pause. */
static bento_status_t run_event_loop(const tab_screen_t *screen)
{
    wave_t wave = {0, (bento_real_t)0.0};
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) && (wave.round < ROUNDS)) {
        step_wave(screen, &wave);
        handle_events(screen);
        status = wait_ms(STEP_MS);
    }
    return status;
}

int main(void)
{
    tab_screen_t screen = {
        bento_runtime_get_ui(), 0U, 0U, 0U, 0U, 0U, 0U};
    bento_status_t status = build_screen(&screen);

    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console(
            "<h2>Tabview - จอที่สองในโปรแกรมเดียว</h2>");
        (void)bento_lcd_print(
            "Tabview 1 + หน้าอีก 3 = 4 แฮนเดิล ก่อนมีเนื้อหา");
        status = run_event_loop(&screen);
    }
    if (status == BENTO_OK) {
        const char *done = "จบรอบ - ของสามชุดอยู่ครบ โดยไม่มีใครทับใคร";

        (void)bento_ui_set_text(screen.ui, screen.line, done,
                                (uint32_t)strlen(done));
        (void)bento_ui_set_color(screen.ui, screen.line, COLOR_DIM);
        (void)bento_lcd_print(
            "<span class=ok>สามแท็บ = สามหน้าจอ ในโปรแกรมเดียว</span>");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
