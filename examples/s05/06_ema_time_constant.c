/*
 * 06_ema_time_constant.c -- EMA: alpha คือเวลา ไม่ใช่เลขลอย ๆ
 *
 * ตัวกรอง EMA (dsp.EMA) ตอบสนองต่อขั้นบันไดช้าหรือเร็วตาม alpha กราฟวาด
 * ขั้นบันไดดิบ (ฟ้า) EMA ของขั้นนี้ (แดง) และเงาของ alpha ขั้นที่แล้ว (เทา)
 * เดินดูหกค่า alpha จาก 1.0 ลงถึง 0.02 ทุกขั้นบอกค่าคงที่เวลา tau เป็น ms
 * (ลูปสมมติ 200 ms) จำนวนตัวอย่างที่ถึง 63.2% และ SMA ที่เทียบเท่า ปุ่มย้อน
 * เดินหน้า เล่นรวด เริ่มใหม่ ทำงานจนกดหยุด
 *
 * An EMA filter (dsp.EMA) follows a step slowly or quickly by its
 * alpha. The chart draws the raw step (blue), this step's EMA (red)
 * and the previous alpha's EMA as a shadow (grey). Six alphas are
 * walked, from 1.0 down to 0.02; each shows the time constant tau in
 * ms (an assumed 200 ms loop), the sample that reaches 63.2% and the
 * equivalent SMA. Back, next, play-through and restart buttons. Runs
 * until Stop.
 *
 * Twin of 06_ema_time_constant.py
 * (embedded-systems-for-aiot-developer/examples/s05/06_ema_time_constant.py),
 * rewritten from the library twin aiot-s05-06-ema-time-constant. Style:
 * CLEAN_C.md. dsp.EMA is bento_dsp_ema_state_*, the same filter held
 * in the program's own memory; "%.2f" and "%.0f" are
 * bento_fmt_real_precision() and str() of a real is bento_fmt_real().
 * The reTerminal's MicroPython has no dsp module (the original raises
 * ImportError there); this C twin runs there too.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_dsp.h"
#include "bento_fmt.h"
#include "bento_runtime.h"

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add, or a value differs in the last digit. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    DT_MS = 200,     /* the loop period assumed: 5 Hz, as in class */
    N_SAMPLES = 50,  /* the chart's window, exactly */
    STEP_AT = 8,
    ALPHA_COUNT = 6,
    PLAY_MS = 1400,
    LOOP_MS = 30,
    SCALE_BY = 100, /* the chart's unit is the value times 100 */
    CHART_MIN = -10,
    CHART_MAX = 120,
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
    COLOR_SHADOW = 0x6B7280, /* the previous alpha's line */
    COLOR_NOW = 0xFF5555,    /* this alpha's line */
    COLOR_THEME = 0          /* no color= given */
};

/* The widgets, in the order they are made; the chart's two series come
 * after the chart. */
enum {
    W_TITLE,
    W_POSITION,
    W_CHART,
    W_RAW_NOTE,
    W_NOW_NOTE,
    W_SHADOW_NOTE,
    W_ALPHA,
    W_TAU_CAPTION,
    W_TAU,
    W_NOTE,
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
    uint8_t shadow_series;
    uint8_t now_series;
    int32_t step; /* the current step: the program's truth */
    bool playing;
    uint32_t next_ms;
} ema_t;

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

/* True when the event is of this kind; the kind passes through a
 * uint8_t, so both sides of the comparison have one type. */
static bool type_is(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

/* Checked by walking the equation, not by trusting a formula alone. */
static int32_t steps_to_63(bento_real_t alpha)
{
    const bento_real_t reached = (bento_real_t)0.632;
    bento_real_t output = (bento_real_t)0.0;
    int32_t steps = 0;
    bool below = true;

    while (below) {
        const bento_real_t rest = ((bento_real_t)1.0 - alpha) * output;

        output = alpha * (bento_real_t)1.0;
        output = output + rest;
        steps++;
        below = output < reached;
    }
    return steps;
}

/* A pure step, no noise. */
static float raw(int32_t sample)
{
    return (sample < STEP_AT) ? 0.0F : 1.0F;
}

/* The screen; the chart's unit is the value times 100. */
static bento_status_t add_screen(ema_t *ema)
{
    static const widget_spec_t screen_widgets[W_COUNT] = {
        {UI_WIDGET_LABEL, "EMA: alpha คือเวลา ไม่ใช่เลขลอย ๆ", 12, 8, AUTO,
         AUTO, COLOR_THEME, 20, 0, 0},
        {UI_WIDGET_LABEL, "1 / 6", 472, 8, AUTO, AUTO, COLOR_OK, 20, 0,
         0},
        {UI_WIDGET_CHART, NULL, 12, 36, 420, 160, COLOR_THEME, 0,
         CHART_MIN, CHART_MAX},
        {UI_WIDGET_LABEL, "ฟ้า = ขั้นบันไดดิบ", 448, 38, AUTO, AUTO,
         COLOR_ACCENT, 20, 0, 0},
        {UI_WIDGET_LABEL, "แดง = EMA ขั้นนี้", 448, 74, AUTO, AUTO,
         COLOR_BAD, 20, 0, 0},
        {UI_WIDGET_LABEL, "เทา = alpha ขั้นที่แล้ว", 448, 110, AUTO, AUTO,
         COLOR_DIM, 20, 0, 0},
        {UI_WIDGET_LABEL, "alpha = 1.00", 448, 148, AUTO, AUTO,
         COLOR_WARN, 24, 0, 0},
        {UI_WIDGET_LABEL, "tau (ms)", 616, 140, AUTO, AUTO, COLOR_THEME,
         16, 0, 0},
        {UI_WIDGET_SEG7, NULL, 616, 176, 152, 40, COLOR_THEME, 0, 0, 0},
        {UI_WIDGET_LABEL, "กดเดินหน้าเพื่อลด alpha", 12, 210, AUTO, AUTO,
         COLOR_THEME, 20, 0, 0},
        {UI_WIDGET_BUTTON, "< ย้อน", 20, 252, 140, 88, COLOR_DIM, 20, 0,
         0},
        {UI_WIDGET_BUTTON, "เดินหน้า >", 176, 252, 160, 88, COLOR_ACCENT,
         20, 0, 0},
        {UI_WIDGET_BUTTON, ">> เล่นรวด", 352, 252, 160, 88, COLOR_OK, 20,
         0, 0},
        {UI_WIDGET_BUTTON, "เริ่มใหม่", 528, 252, 140, 88, COLOR_ACCENT,
         20, 0, 0},
        {UI_WIDGET_LABEL, "กดเดินหน้าเพื่อลด alpha", 344, 346, AUTO, AUTO,
         COLOR_DIM, 20, 0, 0}};
    bento_status_t status = add_widgets(
        ema->ui, screen_widgets, (uint32_t)W_CHART + 1U, ema->ids);

    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(ema->ui, ema->ids[W_CHART],
                                           COLOR_SHADOW,
                                           &ema->shadow_series);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(ema->ui, ema->ids[W_CHART],
                                           COLOR_NOW, &ema->now_series);
    }
    if (status == BENTO_OK) {
        status = add_widgets(ema->ui, &screen_widgets[W_RAW_NOTE],
                             (uint32_t)W_COUNT - (uint32_t)W_RAW_NOTE,
                             &ema->ids[W_RAW_NOTE]);
    }
    return status;
}

static bento_status_t start_filter(bento_dsp_ema_state_t *filter,
                                   float alpha)
{
    bento_dsp_ema_config_t config;
    bento_status_t status = bento_dsp_ema_config_init(&config);

    if (status == BENTO_OK) {
        config.alpha = alpha;
        status = bento_dsp_ema_state_init(filter, &config);
    }
    return status;
}

/* One filtered point, in the chart's unit. */
static int32_t filtered(bento_dsp_ema_state_t *filter, float sample)
{
    float output = 0.0F;

    (void)bento_dsp_ema_state_update(filter, sample, &output);
    output = output * (float)SCALE_BY;
    return (int32_t)output;
}

/* All 50 points in one step: the chart's window is 50 wide, so the old
 * picture is replaced whole. A filter that will not start ends the
 * program, as dsp.EMA() raising would. */
static bento_status_t draw_filters(const ema_t *ema, float alpha,
                                   float previous)
{
    bento_dsp_ema_state_t now;
    bento_dsp_ema_state_t shadow;
    bento_status_t status = start_filter(&now, alpha);
    int32_t sample;

    if (status == BENTO_OK) {
        status = start_filter(&shadow, previous);
    }
    for (sample = 0; (sample < N_SAMPLES) && (status == BENTO_OK);
         sample++) {
        const float value = raw(sample);
        const float scaled = value * (float)SCALE_BY;
        const bento_ui_id_t chart = ema->ids[W_CHART];

        (void)bento_ui_chart_set_next(ema->ui, chart, 0U,
                                      (int32_t)scaled);
        (void)bento_ui_chart_set_next(ema->ui, chart,
                                      ema->shadow_series,
                                      filtered(&shadow, value));
        (void)bento_ui_chart_set_next(ema->ui, chart, ema->now_series,
                                      filtered(&now, value));
    }
    return status;
}

/* str() of a real, or "%.<digits>f" when digits >= 0. */
static void real_text(bento_real_t value, int32_t digits, char *text,
                      uint32_t text_bytes)
{
    uint32_t length = 0U;
    const bento_status_t status =
        (digits < 0) ? bento_fmt_real(text, text_bytes, value, &length)
                     : bento_fmt_real_precision(text, text_bytes, value,
                                                digits, &length);

    if (status != BENTO_OK) {
        text[0] = '\0';
    }
}

/* tau is the time constant of the RC circuit this filter imitates; the
 * formula holds for a small alpha, so at large ones it is a trend. */
static void show_numbers(const ema_t *ema, bento_real_t alpha,
                         int32_t tau_ms)
{
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    const bento_real_t equivalent = ((bento_real_t)2.0 - alpha) / alpha;

    (void)snprintf(text, sizeof(text), "%" PRId32 " / %d",
                   ema->step + 1, ALPHA_COUNT);
    show_text(ema->ui, ema->ids[W_POSITION], text);
    real_text(alpha, 2, number, sizeof(number));
    (void)snprintf(text, sizeof(text), "alpha = %s", number);
    show_text(ema->ui, ema->ids[W_ALPHA], text);
    (void)snprintf(text, sizeof(text), "%" PRId32, tau_ms);
    show_text(ema->ui, ema->ids[W_TAU], text);
    real_text(equivalent, 0, number, sizeof(number));
    (void)snprintf(text, sizeof(text),
                   "ถึง 63.2%% ที่ตัวอย่างที่ %" PRId32 " | เทียบเท่า SMA(%s)",
                   steps_to_63(alpha), number);
    show_text(ema->ui, ema->ids[W_NOTE], text);
    show_text(ema->ui, ema->ids[W_HINT],
              ema->playing ? "กำลังเล่นรวด - กดปุ่มไหนก็หยุด"
                           : "กดเดินหน้าเพื่อลด alpha");
}

static bento_status_t show(const ema_t *ema)
{
    static const bento_real_t alphas[ALPHA_COUNT] = {
        (bento_real_t)1.0, (bento_real_t)0.5,  (bento_real_t)0.2,
        (bento_real_t)0.1, (bento_real_t)0.05, (bento_real_t)0.02};
    const bento_real_t alpha = alphas[ema->step];
    const bento_real_t previous =
        (ema->step != 0) ? alphas[ema->step - 1] : alpha;
    bento_real_t tau =
        (bento_real_t)DT_MS * ((bento_real_t)1.0 - alpha);
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    bento_status_t status =
        draw_filters(ema, (float)alpha, (float)previous);

    tau = tau / alpha;
    if (status == BENTO_OK) {
        show_numbers(ema, alpha, (int32_t)tau);
        real_text(alpha, -1, number, sizeof(number));
        (void)snprintf(text, sizeof(text),
                       "alpha %s -> tau %" PRId32 " ms", number,
                       (int32_t)tau);
        /* lcd.print() ignores a console that is not ready */
        (void)bento_lcd_print(text);
    }
    return status;
}

static bento_status_t handle_click(ema_t *ema, bento_ui_id_t widget)
{
    if (widget == ema->ids[W_PREVIOUS]) {
        ema->playing = false;
        ema->step = (ema->step + ALPHA_COUNT - 1) % ALPHA_COUNT;
    } else if (widget == ema->ids[W_NEXT]) {
        ema->playing = false;
        ema->step = (ema->step + 1) % ALPHA_COUNT;
    } else if (widget == ema->ids[W_HOME]) {
        ema->playing = false;
        ema->step = 0;
    } else if (widget == ema->ids[W_PLAY]) {
        ema->playing = !ema->playing;
        ema->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
    } else {
        /* any other button still redraws, as in the original */
    }
    return show(ema);
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static bento_status_t handle_events(ema_t *ema)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    bento_status_t status = BENTO_OK;
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(ema->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        if (type_is(&events[i], BENTO_UI_EVENT_CLICKED)) {
            status = handle_click(ema, events[i].id);
        }
    }
    return status;
}

/* Playing through moves on by the clock, without sleeping, so the
 * buttons still answer; it stops at the last step. */
static bento_status_t play_on(ema_t *ema)
{
    bento_status_t status = BENTO_OK;

    if (ema->playing && (bento_time_ticks_diff(bento_time_ticks_ms(),
                                               ema->next_ms) >= 0)) {
        ema->step++;
        if (ema->step >= ALPHA_COUNT) {
            ema->step = ALPHA_COUNT - 1;
            ema->playing = false;
        }
        ema->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
        status = show(ema);
    }
    return status;
}

static bento_status_t run_loop(ema_t *ema)
{
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        status = handle_events(ema);
        if (status == BENTO_OK) {
            status = play_on(ema);
        }
        if (status == BENTO_OK) {
            status = pause_ms(LOOP_MS);
        }
    }
    return status;
}

int main(void)
{
    ema_t ema;
    char text[TEXT_BYTES];
    bento_status_t status;

    (void)memset(&ema, 0, sizeof(ema));
    ema.ui = bento_runtime_get_ui();
    status = bento_ui_screen(ema.ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                             BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_screen(&ema);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>EMA และค่าคงที่เวลา</h2>");
        (void)snprintf(text, sizeof(text), "คาบลูปที่สมมติ %d ms", DT_MS);
        (void)bento_lcd_print(text);
        /* dsp.LPF is this same EMA, its alpha worked out from a cut-off
         * frequency and a sample rate */
        (void)bento_lcd_print("<span class=muted>dsp.LPF บอกเป็นความถี่ตัด "
                              "ข้างในคือ EMA ตัวนี้</span>");
        status = show(&ema);
    }
    if (status == BENTO_OK) {
        status = run_loop(&ema);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
