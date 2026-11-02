/*
 * 05_adc_counts_to_volts.c -- ADC: เลขดิบ ไม่ใช่โวลต์
 *
 * ADC คืน "จำนวนขั้น" ไม่ใช่โวลต์ แปลงเป็นโวลต์ด้วยแรงดันเต็มสเกล (สมมติ 1.8 V)
 * หารด้วย 2^บิต - 1 กราฟวาดคลื่น sine ที่เราสร้างเอง (เส้นฟ้า) กับบันไดที่ ADC
 * เห็น (เส้นแดง) เดินดูเจ็ดขั้น ลดจำนวนบิตจาก 16 ลงถึง 3 แล้วจบด้วยเลข 12 บิต
 * ที่ถูกสเกลเป็น 16 บิต ทุกขั้นแสดงขนาดขั้นและความคลาดเคลื่อนสูงสุด ซึ่งไม่เกิน
 * ครึ่งขั้นเสมอ ปุ่มย้อน เดินหน้า เล่นรวด เริ่มใหม่ ทำงานจนกดหยุด
 *
 * An ADC returns counts, not volts: volts are counts over 2^bits - 1
 * times the full-scale voltage (assumed 1.8 V here). The chart draws a
 * made-up sine (blue) and the staircase the ADC sees (red). Seven steps
 * cut the bits from 16 down to 3, then show a 12-bit number scaled up
 * to 16 bits. Each step shows the step size and the worst error, which
 * never passes half a step. Back, next, play-through and restart
 * buttons. Runs until Stop.
 *
 * Twin of 05_adc_counts_to_volts.py
 * (embedded-systems-for-aiot-developer/examples/s05/05_adc_counts_to_volts.py),
 * rewritten from the library twin aiot-s05-05-adc-counts-to-volts.
 * Style: CLEAN_C.md. Reals are bento_real_t (float on the PSoC Edge,
 * double on the reTerminal, as each board's MicroPython); round() is
 * nearbyint, ties to even, as MicroPython's; "%.1f" and "%.4f" are
 * bento_fmt_real_precision().
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
    N_BITS_API =
        16,        /* sensors.pot.read() gives 0-65535 on both boards */
    N_POINTS = 50, /* the chart's window, exactly */
    WAVE_PERIOD = 34,
    PLAY_MS = 1300,
    LOOP_MS = 30,
    STEP_COUNT = 7,
    RAW_COUNT = 5,
    VOLT_DIGITS = 4,  /* "%.4f" */
    CHART_MAX = 1800, /* millivolts */
    AUTO = -1,        /* no w= or h= given: the widget's own size */
    TEXT_BYTES = 128  /* a SET_TEXT carries at most 126 bytes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_REAL = 0x00BFFF, /* the true voltage on the chart */
    COLOR_THEME = 0        /* no color= given */
};

/* The widgets, in the order they are made; the chart's second series
 * comes after the chart. */
enum {
    W_TITLE,
    W_POSITION,
    W_CHART,
    W_REAL_NOTE,
    W_ADC_NOTE,
    W_BITS,
    W_STEP_CAPTION,
    W_STEP_SIZE,
    W_NOTE,
    W_ERROR,
    W_PREVIOUS,
    W_NEXT,
    W_PLAY,
    W_HOME,
    W_HINT,
    W_COUNT
};

/* The full-scale voltage ASSUMED for this lesson: nobody has measured
 * the knob's reference on either board (sensors.pot.voltage() also
 * multiplies by 3.3 without measuring). The signal here is made up, so
 * this is only the chart's scale, not a fact about the board. */
static const bento_real_t V_FULL_SCALE = (bento_real_t)1.8;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[W_COUNT];
    uint8_t
        real_series; /* series 0, the ADC's staircase, comes first */
    int32_t step;    /* the current step: the program's truth */
    bool playing;
    uint32_t next_ms;
} adc_t;

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

static bento_real_t real_sin(bento_real_t radians)
{
#if BENTO_FMT_REAL_DOUBLE
    return sin(radians);
#else
    return sinf(radians);
#endif
}

static bento_real_t real_nearbyint(bento_real_t value)
{
#if BENTO_FMT_REAL_DOUBLE
    return nearbyint(value);
#else
    return nearbyintf(value);
#endif
}

static bento_real_t real_fabs(bento_real_t value)
{
#if BENTO_FMT_REAL_DOUBLE
    return fabs(value);
#else
    return fabsf(value);
#endif
}

/* 2 to the power bits, as a real. */
static bento_real_t two_to(int32_t bits)
{
    const uint32_t power = (uint32_t)1U << (uint32_t)bits;

    return (bento_real_t)power;
}

/* 2**bits - 1, because the largest raw value is that, not 2**bits. */
static bento_real_t counts_to_volts(int32_t raw, int32_t bits)
{
    const bento_real_t full_count = two_to(bits) - (bento_real_t)1.0;
    const bento_real_t fraction = (bento_real_t)raw / full_count;

    return fraction * V_FULL_SCALE;
}

/* One step's size: the full range cut into 2**bits equal parts. */
static bento_real_t lsb_size(int32_t bits)
{
    return V_FULL_SCALE / two_to(bits);
}

/* What an ADC really does: divide by the step, round to the nearest
 * step, multiply back. */
static bento_real_t quantise(bento_real_t volts, int32_t bits)
{
    const bento_real_t step_volts = lsb_size(bits);

    return real_nearbyint(volts / step_volts) * step_volts;
}

/* The made-up continuous signal, so the right value is known exactly. */
static bento_real_t true_volts(int32_t point)
{
    const bento_real_t math_pi = (bento_real_t)3.141592653589793;
    const bento_real_t two = (bento_real_t)2.0;
    const bento_real_t middle = (bento_real_t)0.5; /* of full scale */
    const bento_real_t swing = (bento_real_t)0.48; /* up and down */
    bento_real_t angle = (bento_real_t)point * two;
    bento_real_t level;

    angle = angle * math_pi;
    angle = angle / (bento_real_t)WAVE_PERIOD;
    level = swing * real_sin(angle);
    level = middle + level;
    return V_FULL_SCALE * level;
}

/* Series 0 comes with the chart and is drawn first, so it is the ADC's
 * staircase; the true line is added after, to be drawn over it. */
static bento_status_t add_screen(adc_t *adc)
{
    static const widget_spec_t screen_widgets[W_COUNT] = {
        {UI_WIDGET_LABEL, "ADC: เลขดิบ ไม่ใช่โวลต์", 12, 8, AUTO, AUTO,
         COLOR_THEME, 20, 0, 0},
        {UI_WIDGET_LABEL, "1 / 7", 472, 8, AUTO, AUTO, COLOR_OK, 20, 0,
         0},
        {UI_WIDGET_CHART, NULL, 12, 36, 420, 156, COLOR_BAD, 0, 0,
         CHART_MAX},
        {UI_WIDGET_LABEL, "ฟ้า = แรงดันจริง", 448, 36, AUTO, AUTO,
         COLOR_ACCENT, 20, 0, 0},
        {UI_WIDGET_LABEL, "แดง = ที่ ADC เห็น", 448, 64, AUTO, AUTO,
         COLOR_BAD, 20, 0, 0},
        {UI_WIDGET_LABEL, "16 บิต", 448, 92, AUTO, AUTO, COLOR_WARN, 24,
         0, 0},
        {UI_WIDGET_LABEL, "ขนาดขั้น (uV)", 448, 124, AUTO, AUTO,
         COLOR_THEME, 20, 0, 0},
        {UI_WIDGET_SEG7, NULL, 600, 144, 152, 40, COLOR_THEME, 0, 0, 0},
        {UI_WIDGET_LABEL, "กดเดินหน้าเพื่อลดจำนวนบิต", 12, 194, AUTO, AUTO,
         COLOR_THEME, 20, 0, 0},
        {UI_WIDGET_LABEL, "ยังไม่ได้วัด", 12, 222, AUTO, AUTO, COLOR_WARN,
         20, 0, 0},
        {UI_WIDGET_BUTTON, "< ย้อน", 20, 252, 140, 88, COLOR_DIM, 20, 0,
         0},
        {UI_WIDGET_BUTTON, "เดินหน้า >", 176, 252, 160, 88, COLOR_ACCENT,
         20, 0, 0},
        {UI_WIDGET_BUTTON, ">> เล่นรวด", 352, 252, 160, 88, COLOR_OK, 20,
         0, 0},
        {UI_WIDGET_BUTTON, "เริ่มใหม่", 528, 252, 140, 88, COLOR_ACCENT,
         20, 0, 0},
        {UI_WIDGET_LABEL, "กดเดินหน้าเพื่อลดจำนวนบิต", 344, 346, AUTO, AUTO,
         COLOR_DIM, 20, 0, 0}};
    bento_status_t status = add_widgets(
        adc->ui, screen_widgets, (uint32_t)W_CHART + 1U, adc->ids);

    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            adc->ui, adc->ids[W_CHART], COLOR_REAL, &adc->real_series);
    }
    if (status == BENTO_OK) {
        status = add_widgets(adc->ui, &screen_widgets[W_REAL_NOTE],
                             (uint32_t)W_COUNT - (uint32_t)W_REAL_NOTE,
                             &adc->ids[W_REAL_NOTE]);
    }
    return status;
}

/* One step: the bits that really catch the step, the bits the number
 * claims, and a note. The last step disagrees on purpose: a number can
 * look finer without being finer. */
typedef struct {
    int32_t real_bits;
    int32_t claimed_bits;
    const char *note;
} adc_step_t;

/* All 50 points in one step: the chart's window is 50 wide, so the old
 * picture is replaced whole. Answers the worst error in microvolts. */
static bento_real_t draw_wave(const adc_t *adc, int32_t bits)
{
    const bento_real_t micro = (bento_real_t)1e6;
    const bento_real_t milli = (bento_real_t)1000.0;
    bento_real_t worst_uv = (bento_real_t)0.0;
    int32_t point;

    for (point = 0; point < N_POINTS; point++) {
        const bento_real_t volts = true_volts(point);
        const bento_real_t stepped = quantise(volts, bits);
        const bento_real_t error_uv =
            real_fabs(volts - stepped) * micro;
        const bento_real_t volts_milli = volts * milli;
        const bento_real_t stepped_milli = stepped * milli;

        if (error_uv > worst_uv) {
            worst_uv = error_uv;
        }
        (void)bento_ui_chart_set_next(adc->ui, adc->ids[W_CHART],
                                      adc->real_series,
                                      (int32_t)volts_milli);
        (void)bento_ui_chart_set_next(adc->ui, adc->ids[W_CHART], 0U,
                                      (int32_t)stepped_milli);
    }
    return worst_uv;
}

static void show_labels(const adc_t *adc, const adc_step_t *step,
                        int32_t lsb_uv)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32 " / %d",
                   adc->step + 1, STEP_COUNT);
    show_text(adc->ui, adc->ids[W_POSITION], text);
    if (step->real_bits == step->claimed_bits) {
        (void)snprintf(text, sizeof(text), "%" PRId32 " บิต",
                       step->real_bits);
    } else {
        (void)snprintf(text, sizeof(text),
                       "%" PRId32 " -> %" PRId32 " บิต", step->real_bits,
                       step->claimed_bits);
    }
    show_text(adc->ui, adc->ids[W_BITS], text);
    (void)snprintf(text, sizeof(text), "%" PRId32, lsb_uv);
    show_text(adc->ui, adc->ids[W_STEP_SIZE], text);
    show_text(adc->ui, adc->ids[W_NOTE], step->note);
    show_text(adc->ui, adc->ids[W_HINT],
              adc->playing ? "กำลังเล่นรวด - กดปุ่มไหนก็หยุด"
                           : "กดเดินหน้าเพื่อลดจำนวนบิต");
}

/* The rounding error never passes half a step: a mathematical bound,
 * not the chip's quality. The error line is the evidence. */
static void show(const adc_t *adc)
{
    static const adc_step_t steps[STEP_COUNT] = {
        {16, 16, "ความละเอียดของ API บนบอร์ดนี้"},
        {12, 12, "SAR ADC ทั่วไปบน MCU"},
        {10, 10, "เริ่มเห็นบันไดด้วยตาเปล่า"},
        {8, 8, "บันไดชัดแล้ว ยังพอใช้งานได้"},
        {5, 5, "หยาบจนรูปทรงเริ่มเพี้ยน"},
        {3, 3, "หยาบมาก เห็นขั้นเต็ม ๆ"},
        {12, 16, "เลข 12 บิตถูกสเกลขึ้นเป็น 16 บิต"}};
    const adc_step_t *step = &steps[adc->step];
    const bento_real_t worst_uv = draw_wave(adc, step->real_bits);
    const bento_real_t lsb_micro =
        lsb_size(step->real_bits) * (bento_real_t)1e6;
    const int32_t lsb_uv = (int32_t)lsb_micro;
    char text[TEXT_BYTES];

    show_labels(adc, step, lsb_uv);
    (void)snprintf(text, sizeof(text),
                   "ผิดสุด %" PRId32 " uV | ครึ่งขั้น %" PRId32 " uV",
                   (int32_t)worst_uv, lsb_uv / 2);
    show_text(adc->ui, adc->ids[W_ERROR], text);
    (void)snprintf(text, sizeof(text),
                   "%" PRId32 " บิต | ขั้น %" PRId32 " uV | ผิดสุด %" PRId32
                   " uV",
                   step->real_bits, lsb_uv, (int32_t)worst_uv);
    /* lcd.print() ignores a console that is not ready */
    (void)bento_lcd_print(text);
}

/* The question students ask most: how many volts is 32768? Sent to
 * the serial console to read later; it follows the assumed V_FS. */
static void print_table(void)
{
    static const int32_t raws[RAW_COUNT] = {0, 16384, 32768, 49152,
                                            65535};
    char number[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;
    uint32_t i;

    (void)bento_fmt_real_precision(number, BENTO_FMT_BUF_SIZE,
                                   V_FULL_SCALE, 1, &length);
    (void)printf(
        "V_FS = %s V (assumed, not measured), %d bit, full count = "
        "%" PRIu32 "\n",
        number, N_BITS_API,
        ((uint32_t)1U << (uint32_t)N_BITS_API) - 1U);
    for (i = 0U; i < (uint32_t)RAW_COUNT; i++) {
        /* BENTO_FMT_BUF_SIZE holds every result */
        (void)bento_fmt_real_precision(
            number, BENTO_FMT_BUF_SIZE,
            counts_to_volts(raws[i], N_BITS_API), VOLT_DIGITS, &length);
        (void)printf("raw %-6" PRId32 " = %s V\n", raws[i], number);
    }
}

static void handle_click(adc_t *adc, bento_ui_id_t widget)
{
    if (widget == adc->ids[W_PREVIOUS]) {
        adc->playing = false;
        adc->step = (adc->step + STEP_COUNT - 1) % STEP_COUNT;
    } else if (widget == adc->ids[W_NEXT]) {
        adc->playing = false;
        adc->step = (adc->step + 1) % STEP_COUNT;
    } else if (widget == adc->ids[W_HOME]) {
        adc->playing = false;
        adc->step = 0;
    } else if (widget == adc->ids[W_PLAY]) {
        adc->playing = !adc->playing;
        adc->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
    } else {
        /* any other button still redraws, as in the original */
    }
    show(adc);
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static void handle_events(adc_t *adc)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(adc->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (type_is(&events[i], BENTO_UI_EVENT_CLICKED)) {
            handle_click(adc, events[i].id);
        }
    }
}

/* Playing through moves on by the clock, without sleeping, so the
 * buttons still answer; it stops at the last step. */
static void play_on(adc_t *adc)
{
    if (adc->playing && (bento_time_ticks_diff(bento_time_ticks_ms(),
                                               adc->next_ms) >= 0)) {
        adc->step++;
        if (adc->step >= STEP_COUNT) {
            adc->step = STEP_COUNT - 1;
            adc->playing = false;
        }
        adc->next_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), PLAY_MS);
        show(adc);
    }
}

static bento_status_t run_loop(adc_t *adc)
{
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        handle_events(adc);
        play_on(adc);
        status = pause_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    adc_t adc;
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint32_t length = 0U;
    bento_status_t status;

    (void)memset(&adc, 0, sizeof(adc));
    adc.ui = bento_runtime_get_ui();
    status = bento_ui_screen(adc.ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                             BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_screen(&adc);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>ADC: นับขั้น ไม่ใช่โวลต์</h2>");
        if (bento_fmt_real(number, BENTO_FMT_BUF_SIZE, V_FULL_SCALE,
                           &length) != BENTO_OK) {
            number[0] = '\0';
        }
        (void)snprintf(text, sizeof(text),
                       "เต็มสเกล (สมมติ) %s V | API %d บิต", number,
                       N_BITS_API);
        (void)bento_lcd_print(text);
        show(&adc);
        print_table();
        status = run_loop(&adc);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
