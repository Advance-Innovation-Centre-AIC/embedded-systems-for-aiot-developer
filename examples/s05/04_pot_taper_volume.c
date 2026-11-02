/*
 * 04_pot_taper_volume.c -- เส้นตรง เทียบ เส้นโค้งเสียง (log taper)
 *
 * หูคนได้ยินความดังเป็นเส้นโค้ง ลูกบิดเสียงจึงใช้เส้นโค้ง (ยกกำลังสาม) ไม่ใช่
 * เส้นตรง กราฟวาดทั้งสองเส้นและหมุดจุดที่กำลังดู เดินดูห้าท่า (0 25 50 75 100%)
 * ด้วยปุ่มย้อน เดินหน้า เล่นรวด และเริ่มใหม่ แต่ละท่าเล่นโน้ต A4 ที่ความแรง
 * ตามเส้นโค้ง หมุนลูกบิดจริงเกิน 5% แล้วกระโดดไปท่าที่ใกล้ที่สุด ทำงานจนกดหยุด
 *
 * The ear hears loudness as a curve, so a volume knob uses a curve
 * (the cube) rather than a straight line. The chart draws both lines
 * and a marker at the step being looked at. Five steps (0 25 50 75
 * 100%) are walked with back, next, play-through and restart; each
 * plays an A4 at the curve's strength. Turn the real knob more than
 * 5% and the program jumps to the nearest step. Runs until Stop.
 *
 * Twin of 04_pot_taper_volume.py
 * (embedded-systems-for-aiot-developer/examples/s05/04_pot_taper_volume.py),
 * rewritten from the library twin aiot-s05-04-pot-taper-volume. Style:
 * CLEAN_C.md. x ** 3 is x * x * x (MicroPython's powf can differ in
 * the last bit, which int() could show only at an exact whole number).
 * The original calls ui.tone() without asking for a sound chip; here a
 * board without one stays silent instead of raising.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add, or a value differs in the last digit. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

enum {
    NOTE = 69, /* A4: one note, so only loudness differs */
    NOTE_MS = 300,
    VELOCITY_MAX = 127, /* ui.tone's velocity range is 0-127 */
    POINTS = 50,        /* the chart holds exactly 50 points */
    PLAY_MS = 1100,
    LOOP_MS = 30,
    STEP_COUNT = 5,
    STEP_PCT = 25,
    MARK_TOP = 100,
    PERCENT_MAX = 100,
    AUTO = -1,       /* no w= or h= given: the widget's own size */
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_LOG = 0xFF5555,  /* the curve on the chart */
    COLOR_MARK = 0xFFC107, /* the marker of the step being looked at */
    COLOR_THEME = 0        /* no color= given */
};

/* The widgets, in the order they are made. */
enum {
    W_TITLE,
    W_CHART,
    W_LINEAR_NOTE,
    W_CURVE_NOTE,
    W_MARK_NOTE,
    W_SEG,
    W_POSITION,
    W_BODY,
    W_VOLUME_CAPTION,
    W_VOLUME_BAR,
    W_KNOB,
    W_PREVIOUS,
    W_NEXT,
    W_PLAY,
    W_HOME,
    W_HINT,
    W_COUNT
};

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
    uint8_t curve_series;
    uint8_t mark_series;
    int32_t step; /* the current step: the program's truth */
    bool playing;
    uint32_t next_ms;
    bento_real_t knob;
} taper_t;

/* One widget as the original makes it: its factory, its text and its
 * keyword arguments. value= is a Label's or Button's font size and a
 * control's start value; min= and max= are a control's range (a
 * Panel's border colour and corner radius). */
typedef struct {
    uint8_t type;
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value;
    int32_t min;
    int32_t max;
} widget_spec_t;

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t widget,
                      const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_widget(bento_ui_t *ui,
                                 const widget_spec_t *spec,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    if (spec->text != NULL) {
        args.text = spec->text;
        args.text_len = (uint32_t)strlen(spec->text);
    }
    args.x = spec->x;
    args.y = spec->y;
    args.w = spec->w;
    args.h = spec->h;
    args.color = spec->color;
    args.font_px = spec->value;
    args.min_val = spec->min;
    args.max_val = spec->max;
    return bento_ui_create(ui, spec->type, &args, out_id);
}

/* Makes the widgets in table order (creation order is drawing order)
 * and keeps each id; the first refusal ends it, as a raise does in
 * MicroPython. */
static bento_status_t add_widgets(bento_ui_t *ui,
                                  const widget_spec_t *table,
                                  uint32_t count, bento_ui_id_t *ids)
{
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = add_widget(ui, &table[i], &ids[i]);
    }
    return status;
}

/* The cube approximates a log taper, as audio gear commonly does. */
static bento_real_t log_taper(bento_real_t pct)
{
    const bento_real_t hundred = (bento_real_t)100.0;
    const bento_real_t x = pct / hundred;
    bento_real_t cube = x * x;

    cube = cube * x;
    return cube * hundred;
}

/* The step nearest a knob position (pct >= 0, so / is floor). */
static int32_t nearest_step(bento_real_t pct)
{
    const bento_real_t half_step = (bento_real_t)12.5;
    int32_t step =
        (int32_t)((pct + half_step) / (bento_real_t)STEP_PCT);

    if (step < 0) {
        step = 0;
    } else if (step >= STEP_COUNT) {
        step = STEP_COUNT - 1;
    } else {
        /* a step that exists */
    }
    return step;
}

/* Everything sits above y = 244: the button row takes 250 to 340, and
 * the bottom-right corner is the Console button's. */
static bento_status_t add_screen_widgets(bento_ui_t *ui, uint32_t first,
                                         uint32_t count,
                                         bento_ui_id_t *ids)
{
    static const widget_spec_t screen_widgets[W_COUNT] = {
        {UI_WIDGET_LABEL, "เส้นตรง เทียบ เส้นโค้งเสียง", 12, 8, AUTO, AUTO,
         COLOR_THEME, 24, 0, 0},
        {UI_WIDGET_CHART, NULL, 12, 36, 432, 200, COLOR_THEME, 0, 0,
         PERCENT_MAX},
        {UI_WIDGET_LABEL, "ฟ้า = เส้นตรง", 456, 38, AUTO, AUTO,
         COLOR_ACCENT, 20, 0, 0},
        {UI_WIDGET_LABEL, "แดง = เส้นโค้ง ที่ใช้จริง", 456, 74, AUTO, AUTO,
         COLOR_BAD, 20, 0, 0},
        {UI_WIDGET_LABEL, "เหลือง = จุดที่กำลังดู", 456, 110, AUTO, AUTO,
         COLOR_WARN, 20, 0, 0},
        {UI_WIDGET_SEG7, NULL, 456, 148, 120, 36, COLOR_THEME, 0, 0, 0},
        {UI_WIDGET_LABEL, "", 640, 112, AUTO, AUTO, COLOR_OK, 20, 0, 0},
        {UI_WIDGET_LABEL, "", 456, 146, AUTO, AUTO, COLOR_THEME, 20, 0,
         0},
        {UI_WIDGET_LABEL, "ความดังจริงที่ส่งออก", 456, 190, AUTO, AUTO,
         COLOR_THEME, 20, 0, 0},
        {UI_WIDGET_BAR, NULL, 456, 228, 252, 16, COLOR_THEME, 0, 0,
         PERCENT_MAX},
        {UI_WIDGET_LABEL, "", 456, 218, AUTO, AUTO, COLOR_DIM, 20, 0,
         0},
        {UI_WIDGET_BUTTON, "< ย้อน", 20, 252, 140, 88, COLOR_DIM, 20, 0,
         0},
        {UI_WIDGET_BUTTON, "เดินหน้า >", 176, 252, 160, 88, COLOR_ACCENT,
         20, 0, 0},
        {UI_WIDGET_BUTTON, ">> เล่นรวด", 352, 252, 160, 88, COLOR_OK, 20,
         0, 0},
        {UI_WIDGET_BUTTON, "เริ่มใหม่", 528, 252, 140, 88, COLOR_ACCENT,
         20, 0, 0},
        {UI_WIDGET_LABEL, "", 20, 344, AUTO, AUTO, COLOR_DIM, 20, 0,
         0}};

    return add_widgets(ui, &screen_widgets[first], count, &ids[first]);
}

static bento_status_t build_screen(taper_t *taper)
{
    bento_ui_t *ui = taper->ui;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_screen_widgets(ui, 0U, 2U, taper->ids);
    }
    /* series 0 comes with the chart; the marker, made last, is drawn
     * over the others */
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            ui, taper->ids[W_CHART], COLOR_LOG, &taper->curve_series);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            ui, taper->ids[W_CHART], COLOR_MARK, &taper->mark_series);
    }
    if (status == BENTO_OK) {
        status = add_screen_widgets(ui, W_LINEAR_NOTE,
                                    (uint32_t)W_COUNT - 2U, taper->ids);
    }
    return status;
}

/* The whole range first, so the shape of both lines shows before the
 * walk begins. */
static void sweep_chart(const taper_t *taper)
{
    int32_t point;

    for (point = 0; point < POINTS; point++) {
        bento_real_t pct =
            (bento_real_t)point * (bento_real_t)PERCENT_MAX;

        pct = pct / (bento_real_t)(POINTS - 1);
        (void)bento_ui_chart_set_next(taper->ui, taper->ids[W_CHART],
                                      0U, (int32_t)pct);
        (void)bento_ui_chart_set_next(taper->ui, taper->ids[W_CHART],
                                      taper->curve_series,
                                      (int32_t)log_taper(pct));
    }
}

/* The marker is a full 50-point series, zero everywhere but the
 * current place: all 50 are sent each time, or it would drift against
 * the other two lines. */
static void mark(const taper_t *taper, int32_t pct)
{
    const int32_t hit = (pct * (POINTS - 1)) / PERCENT_MAX;
    int32_t point;

    for (point = 0; point < POINTS; point++) {
        (void)bento_ui_chart_set_next(taper->ui, taper->ids[W_CHART],
                                      taper->mark_series,
                                      (point == hit) ? MARK_TOP : 0);
    }
}

/* Draws the current step: marker, numbers, bar, hint, a console line
 * and the note at the curve's strength. */
static void show(const taper_t *taper)
{
    /* The steps walked through, in percent of the knob. */
    static const int32_t steps[STEP_COUNT] = {0, 25, 50, 75, 100};
    char text[TEXT_BYTES];
    const int32_t pct = steps[taper->step];
    const bento_real_t loudness = log_taper((bento_real_t)pct);
    const int32_t curve = (int32_t)loudness;
    const bento_real_t scaled =
        (loudness * (bento_real_t)VELOCITY_MAX) /
        (bento_real_t)PERCENT_MAX;
    const int32_t velocity = (int32_t)scaled;

    mark(taper, pct);
    (void)snprintf(text, sizeof(text), "%" PRId32, pct);
    show_text(taper->ui, taper->ids[W_SEG], text);
    (void)snprintf(text, sizeof(text), "ท่า %" PRId32 " / %d",
                   taper->step + 1, STEP_COUNT);
    show_text(taper->ui, taper->ids[W_POSITION], text);
    (void)snprintf(text, sizeof(text),
                   "เส้นตรง %" PRId32 " -> เส้นโค้ง %" PRId32, pct, curve);
    show_text(taper->ui, taper->ids[W_BODY], text);
    (void)bento_ui_set_value(taper->ui, taper->ids[W_VOLUME_BAR],
                             curve);
    show_text(taper->ui, taper->ids[W_HINT],
              taper->playing ? "กำลังเล่นรวด - กดปุ่มไหนก็หยุด"
                             : "กดเดินหน้า หรือหมุนลูกบิดจริง");
    (void)snprintf(text, sizeof(text),
                   "%" PRId32 "%% | %" PRId32 " | %" PRId32, pct, pct,
                   curve);
    /* lcd.print() ignores a console that is not ready */
    (void)bento_lcd_print(text);
    if (velocity > 0) {
        (void)bento_ui_sfx_tone(taper->ui, (uint8_t)NOTE,
                                (uint8_t)BENTO_UI_WAVE_SINE,
                                (uint8_t)velocity, (uint16_t)NOTE_MS);
    }
}

static void handle_click(taper_t *taper, bento_ui_id_t widget)
{
    if (widget == taper->ids[W_PREVIOUS]) {
        taper->playing = false;
        taper->step = (taper->step + STEP_COUNT - 1) % STEP_COUNT;
    } else if (widget == taper->ids[W_NEXT]) {
        taper->playing = false;
        taper->step = (taper->step + 1) % STEP_COUNT;
    } else if (widget == taper->ids[W_HOME]) {
        taper->playing = false;
        taper->step = 0;
    } else if (widget == taper->ids[W_PLAY]) {
        taper->playing = !taper->playing;
        taper->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
    } else {
        /* any other button still redraws, as in the original */
    }
    show(taper);
}

/* True when the event is of this kind; the kind passes through a
 * uint8_t, so both sides of the comparison have one type. */
static bool type_is(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static void handle_events(taper_t *taper)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(taper->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (type_is(&events[i], BENTO_UI_EVENT_CLICKED)) {
            handle_click(taper, events[i].id);
        }
    }
}

static void show_knob(const taper_t *taper, bento_real_t pct)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "ลูกบิดจริงตอนนี้ %" PRId32 "%%",
                   (int32_t)pct);
    show_text(taper->ui, taper->ids[W_KNOB], text);
}

/* The real knob is a second way in: turned, it jumps to the nearest
 * step. A failed read raises in the original, so it ends here. */
static bento_status_t follow_knob(taper_t *taper)
{
    /* The real knob moved more than this: a person turned it. */
    static const bento_real_t knob_move = (bento_real_t)5.0;
    float percent = 0.0F;
    const bento_status_t status = bento_sensors_pot_percent(&percent);
    const bento_real_t pct = (bento_real_t)percent;
    bento_real_t moved = pct - taper->knob;

    if (moved < (bento_real_t)0.0) {
        moved = -moved;
    }
    if ((status == BENTO_OK) && (moved >= knob_move)) {
        taper->knob = pct;
        show_knob(taper, pct);
        taper->playing = false;
        taper->step = nearest_step(pct);
        show(taper);
    }
    return status;
}

/* Playing through moves on by the clock, without sleeping, so the
 * buttons still answer; it stops at the last step. */
static void play_on(taper_t *taper)
{
    if (taper->playing &&
        (bento_time_ticks_diff(bento_time_ticks_ms(), taper->next_ms) >=
         0)) {
        taper->step++;
        if (taper->step >= STEP_COUNT) {
            taper->step = STEP_COUNT - 1;
            taper->playing = false;
        }
        taper->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
        show(taper);
    }
}

static bento_status_t run_loop(taper_t *taper)
{
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        handle_events(taper);
        status = follow_knob(taper);
        if (status == BENTO_OK) {
            play_on(taper);
            status = pause_ms(LOOP_MS);
        }
    }
    return status;
}

int main(void)
{
    taper_t taper;
    float percent = 0.0F;
    bento_status_t status;

    (void)memset(&taper, 0, sizeof(taper));
    taper.ui = bento_runtime_get_ui();
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>เส้นตรง เทียบ เส้นโค้งเสียง</h2>");
    (void)bento_lcd_print("ตำแหน่ง | เส้นตรง | เส้นโค้ง (ที่ใช้จริง)");
    status = build_screen(&taper);
    if (status == BENTO_OK) {
        sweep_chart(&taper);
        status = bento_sensors_pot_percent(&percent);
    }
    if (status == BENTO_OK) {
        taper.knob = (bento_real_t)percent;
        show_knob(&taper, taper.knob);
        show(&taper);
        status = run_loop(&taper);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
