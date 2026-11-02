/*
 * ex14_audio_waveform.c -- รูปคลื่นและระดับเสียงจากไมค์
 * (Microphone Waveform + Level)
 *
 * เสียงจริงจากไมค์ PDM บนบอร์ด วาดเป็นรูปคลื่น 128 จุดบนกราฟ (256
 * ตัวอย่าง เลือกทุกตัวที่สอง) พร้อมแถบและตัวเลขระดับเสียง 0-100 %
 * สเกลคงที่ไม่มี autogain ห้องเงียบเส้นเกือบนิ่ง พูดหรือตบมือใกล้บอร์ด
 * คลื่นเต็มตา ปุ่ม Pause หยุดภาพ ปุ่ม Play ให้วิ่งต่อ ทำงาน 2 นาทีแล้วจบ
 * ปิดไมค์เสมอ และวาดแถบ "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * Real sound from the board's PDM microphone, drawn as a 128-point
 * waveform (256 samples, every second one kept), with a level bar and
 * number from 0 to 100 %. The scale is fixed, with no autogain: a quiet
 * room gives an almost flat line, speech or a clap near the board a
 * full wave. Pause freezes the trace, Play lets it run again. The
 * program ends after two minutes, always closes the microphone, and
 * then draws a "finished" band over the bottom rows.
 *
 * C twin of ex14_audio_waveform.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/devkit/ex14_audio_waveform.py), written new: no
 * library twin is close. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. What C changes:
 *  - the frozen `mic` module has no C contract, so this file does what
 *    mic.py does with machine.PDM_PCM's C API (bento_machine_pdm_*):
 *    16 kHz, decimation 96, 16 bits, the right channel (the board wires
 *    PDM channel 3 on P8_5/P8_6, which the board glue routes; the C
 *    configuration names no pins), sens=3 as gain 13, 256 samples a
 *    read, two reads thrown away after the start. mic.level() is its
 *    octave scale over a fresh RMS. bento_machine_pdm_* is a PSoC Edge
 *    API: the reTerminal cannot link this program.
 *  - a read that stays short for 2 s ends the program with
 *    BENTO_E_TIMEOUT, as readinto() raises OSError; the microphone is
 *    closed first, as the original's finally closes it.
 *  - the samples are unpacked from their little-endian bytes before
 *    dsp.s16() keeps every second one.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_dsp.h"
#include "bento_machine.h"
#include "bento_runtime.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    BUTTON_FONT_PX = 16,
    TITLE_Y = 8,
    CHART_X = 46,
    CHART_Y = 54,
    CHART_W = 580,
    CHART_H = 240,
    LEVEL_BAR_X = 666,
    LEVEL_BAR_W = 40,
    LEVEL_TEXT_X = 640,
    LEVEL_TEXT_Y = 306,
    BUTTON_HALF_W = 60,
    BUTTON_Y = 310,
    BUTTON_H = 40,
    INFO_Y = 354,
    FOOTER_Y = 378
};

/* The "finished" band over the bottom rows, left of the level bar. */
enum {
    END_Y = 330,
    END_W = 700,
    END_H = 36,
    END_TEXT_X = 232,
    END_TEXT_Y = 334,
    END_FONT_PX = 20
};

/* The microphone, as mic.py sets it up. */
enum {
    SAMPLE_RATE_HZ = 16000,
    DECIMATION = 96,
    GAIN_SENS_3 = 13, /* sens=3 on mic.py's 1-5 scale */
    READ_SAMPLES = 256,
    SAMPLE_BYTES = 2,
    READ_BYTES = READ_SAMPLES * SAMPLE_BYTES,
    READ_LIMIT_MS = 2000, /* readinto() gives up after 2 s */
    WARM_UP_READS =
        2, /* the first reads after the start are unsteady */
    WARM_UP_MS = 20,
    DECIMATE_STEP = 2,
    POINTS = READ_SAMPLES / DECIMATE_STEP,
    BYTE_BITS = 8,
    SIGN_BIT = 0x8000, /* an int16 above 32767 is negative */
    WORD = 0x10000
};

/* mic.level(): octaves of the RMS from 16 up to full scale. */
enum {
    LEVEL_FLOOR = 16,     /* quieter than a quiet room: 0 */
    LEVEL_LOW_Q8 = 1024,  /* log2(16) x 256 */
    LEVEL_SPAN_Q8 = 2816, /* log2(32768) x 256, less LEVEL_LOW_Q8 */
    Q8_ONE = 256,         /* 1.0 in the 8-bit fraction */
    MANTISSA_BITS = 9,    /* the mantissa kept in 256-511 */
    MANTISSA_BASE = 256,
    PERCENT = 100
};

/* The trace. */
enum {
    CHART_MAX = 100,
    CHART_MIDDLE = 50,
    TRACE_GAIN = 40,   /* +-2500 counts fill the screen */
    TRACE_FULL = 2500, /* a quiet room (RMS about 170) stays flat */
    YIELD_EVERY = 16,  /* points between two short pauses */
    YIELD_MS = 6,
    LEVEL_TEXT_BYTES = 20 /* "Level: ", an int32_t, "%" and the NUL */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 80,
    RUN_MS = 120000 /* the example ends after two minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x1A1A2E,
    COLOR_TRACE = 0xFF00FF,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_BUTTON = 0x2196F3,
    COLOR_INFO = 0x888888,
    COLOR_FOOTER = 0x666666
};

/* The "finished" band's colours, 0xRRGGBB. */
enum {
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

/* The samples of one read: 1280 bytes, too big for the 8 KB program
 * stack, so at file scope, zeroed by the loader and set again at the
 * start of main() (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
typedef struct {
    uint8_t bytes[READ_BYTES];
    int16_t samples[READ_SAMPLES];
    int16_t points[POINTS];
} audio_buffers_t;

extern audio_buffers_t g_audio;
audio_buffers_t g_audio;

/* The widgets the program changes, and the microphone. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    bento_ui_id_t level_bar;
    bento_ui_id_t level_text;
    bento_ui_id_t button;
    bool running;
    uint32_t pdm;
} scope_t;

/* A label's place and look: the original's x=, y=, color= and value=. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} label_style_t;

/* Python's len(): characters, not bytes, so a text is centred like
 * the original. */
static int32_t utf8_length(const char *text)
{
    /* the two bits that mark a UTF-8 byte */
    const uint8_t top_bits = 0xC0U;
    /* 10xxxxxx: not the first byte of a character */
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

/* x that centres a text: the original's len(text) * size, floor-divided
 * by 4, left of the centre. */
static int16_t centered_x(const char *text, int32_t font_px)
{
    return (int16_t)(SCREEN_CENTER_X -
                     (((utf8_length(text) * font_px) / 2) / 2));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const label_style_t *style,
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

/* A label centred on the screen, as the original's cx() places it. */
static bento_status_t add_centered(bento_ui_t *ui, const char *text,
                                   int16_t y, uint32_t color,
                                   int32_t font_px,
                                   bento_ui_id_t *out_id)
{
    const label_style_t style = {centered_x(text, font_px), y, color,
                                 font_px};

    return add_label(ui, text, &style, out_id);
}

/* A fresh screen, then a pause to let it settle; Stop during the pause
 * ends the program. */
static bento_status_t clear_screen(bento_ui_t *ui)
{
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static bento_status_t add_background(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;

    bento_ui_widget_args_init(&args);
    args.x = 0;
    args.y = 0;
    args.w = BENTO_RUNTIME_SCREEN_WIDTH_PX;
    args.h = BENTO_RUNTIME_SCREEN_HEIGHT_PX;
    args.color = COLOR_BACKGROUND;
    args.min_val = COLOR_BACKGROUND; /* min= repeats the colour */
    args.max_val = 0;
    args.font_px = 0;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* time.sleep_ms(): Stop during the pause becomes BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Python's floor division by a positive divisor: it rounds toward
 * minus infinity, where C's division rounds toward zero. */
static int32_t floor_div(int32_t dividend, int32_t divisor)
{
    int32_t quotient = dividend / divisor;

    if ((dividend % divisor) < 0) {
        quotient--;
    }
    return quotient;
}

/* The trace (128 points; .prop() never raises in MicroPython) and the
 * level bar beside it. */
static bento_status_t add_chart_and_bar(scope_t *scope)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CHART_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.min_val = 0;
    args.max_val = CHART_MAX;
    args.color = COLOR_TRACE;
    status = bento_ui_chart_create(scope->ui, &args, &scope->chart);
    if (status == BENTO_OK) {
        (void)bento_ui_set_prop(scope->ui, scope->chart,
                                BENTO_UI_PROP_CHART_POINTS, POINTS);
        bento_ui_widget_args_init(&args);
        args.x = LEVEL_BAR_X;
        args.y = CHART_Y;
        args.w = LEVEL_BAR_W;
        args.h = CHART_H;
        args.min_val = 0;
        args.max_val = PERCENT;
        args.font_px = 0;
        status =
            bento_ui_bar_create(scope->ui, &args, &scope->level_bar);
    }
    return status;
}

/* The level text and the Pause button under the trace. */
static bento_status_t add_level_and_button(scope_t *scope)
{
    const label_style_t level = {LEVEL_TEXT_X, LEVEL_TEXT_Y, COLOR_TEXT,
                                 FONT_PX};
    bento_ui_widget_args_t args;
    bento_status_t status =
        add_label(scope->ui, "Level: 0%", &level, &scope->level_text);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "Pause";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = SCREEN_CENTER_X - BUTTON_HALF_W;
        args.y = BUTTON_Y;
        args.w = 2 * BUTTON_HALF_W;
        args.h = BUTTON_H;
        args.color = COLOR_BUTTON;
        args.font_px = BUTTON_FONT_PX;
        status =
            bento_ui_button_create(scope->ui, &args, &scope->button);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(scope_t *scope)
{
    bento_ui_id_t unused;
    bento_status_t status = clear_screen(scope->ui);

    if (status == BENTO_OK) {
        status = add_background(scope->ui);
    }
    if (status == BENTO_OK) {
        status = add_centered(
            scope->ui,
            "Part 3 - Example 3+4: Microphone Waveform + Level",
            TITLE_Y, COLOR_TRACE, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_chart_and_bar(scope);
    }
    if (status == BENTO_OK) {
        status = add_level_and_button(scope);
    }
    if (status == BENTO_OK) {
        status = add_centered(
            scope->ui, "Real PDM mic - speak or clap near the board",
            INFO_Y, COLOR_INFO, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(scope->ui,
                         "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                         "Burapha University",
                         FOOTER_Y, COLOR_FOOTER, FONT_PX, &unused);
    }
    return status;
}

/* One full read, as readinto() does it: a read still short after 2 s
 * is BENTO_E_TIMEOUT, where MicroPython raises OSError. */
static bento_status_t read_samples(const scope_t *scope)
{
    uint32_t got = 0U;
    bento_status_t status = bento_machine_pdm_read(
        scope->pdm, g_audio.bytes, READ_BYTES, READ_LIMIT_MS, &got);

    if ((status == BENTO_OK) && (got < (uint32_t)READ_BYTES)) {
        status = BENTO_E_TIMEOUT;
    }
    return status;
}

/* mic.start(sens=3): open and start the microphone, then throw the
 * first two reads away; their errors are caught there too. */
static bento_status_t start_mic(scope_t *scope)
{
    bento_machine_pdm_cfg_t config;
    int32_t i;
    bento_status_t status;

    bento_machine_pdm_cfg_init(&config);
    config.sample_rate = SAMPLE_RATE_HZ;
    config.decimation_rate = DECIMATION;
    config.bits = BENTO_MACHINE_PDM_BITS_16;
    config.format = (uint32_t)BENTO_MACHINE_PDM_MONO_RIGHT;
    config.left_gain = GAIN_SENS_3;
    config.right_gain = GAIN_SENS_3;
    status = bento_machine_pdm_open(&config, &scope->pdm);
    if (status == BENTO_OK) {
        status = bento_machine_pdm_start(scope->pdm);
    }
    for (i = 0; (i < WARM_UP_READS) && (status == BENTO_OK); i++) {
        (void)read_samples(scope); /* mic.py catches OSError here */
        status = pause_ms(WARM_UP_MS);
    }
    return status;
}

/* mic.stats(fresh=True): the RMS of the newest window; older audio in
 * the ring is dropped, so what follows is "now". */
static bento_status_t fresh_rms(const scope_t *scope, uint32_t *rms)
{
    bento_machine_pdm_stats_t stats;
    bento_status_t status;

    bento_machine_pdm_stats_init(&stats);
    status = bento_machine_pdm_stats(scope->pdm, g_audio.bytes,
                                     READ_BYTES, true, &stats);
    *rms = stats.rms;
    return status;
}

/* log2(value) x 256 in integers: the octave, then a straight line
 * inside it (within 3 %, enough for a meter). */
static int32_t log2_q8(uint32_t value)
{
    uint32_t rest = value;
    int32_t bits = 0;
    uint32_t mantissa;

    while (rest != 0U) {
        rest >>= 1U;
        bits++;
    }
    if (bits <= MANTISSA_BITS) {
        mantissa = value << ((uint32_t)MANTISSA_BITS - (uint32_t)bits);
    } else {
        mantissa = value >> ((uint32_t)bits - (uint32_t)MANTISSA_BITS);
    }
    return ((bits - 1) * Q8_ONE) + ((int32_t)mantissa - MANTISSA_BASE);
}

/* mic.level(): the loudness as 0-100 on the ear's scale. */
static bento_status_t read_level(const scope_t *scope, int32_t *level)
{
    uint32_t rms = 0U;
    const bento_status_t status = fresh_rms(scope, &rms);
    int32_t value = 0;

    if ((status == BENTO_OK) && (rms >= (uint32_t)LEVEL_FLOOR)) {
        value =
            ((log2_q8(rms) - LEVEL_LOW_Q8) * PERCENT) / LEVEL_SPAN_Q8;
        value = (value < 0) ? 0 : value;
        value = (value > PERCENT) ? PERCENT : value;
    }
    *level = value;
    return status;
}

/* mic.raw() then dsp.s16(buf, 2): the 256 samples from their
 * little-endian bytes, every second one kept. */
static bento_status_t read_points(const scope_t *scope, uint32_t *count)
{
    int32_t i;
    bento_status_t status = read_samples(scope);

    for (i = 0; (i < READ_SAMPLES) && (status == BENTO_OK); i++) {
        const int32_t low_byte = i * SAMPLE_BYTES;
        const uint32_t word = (uint32_t)g_audio.bytes[low_byte] |
                              ((uint32_t)g_audio.bytes[low_byte + 1]
                               << (uint32_t)BYTE_BITS);
        const int32_t value = (int32_t)word;

        g_audio.samples[i] =
            (int16_t)((word >= (uint32_t)SIGN_BIT) ? (value - WORD)
                                                   : value);
    }
    if (status == BENTO_OK) {
        status = bento_dsp_s16_decimate(g_audio.samples, READ_SAMPLES,
                                        DECIMATE_STEP, g_audio.points,
                                        POINTS, count);
    }
    return status;
}

/* One trace point: the distance from the mean on a fixed scale (no
 * autogain, so quiet and loud look different), kept on the chart. */
static int32_t trace_point(int32_t sample, int32_t mean)
{
    int32_t offset =
        floor_div((sample - mean) * TRACE_GAIN, TRACE_FULL);

    offset = (offset > TRACE_GAIN) ? TRACE_GAIN : offset;
    offset = (offset < -TRACE_GAIN) ? -TRACE_GAIN : offset;
    return CHART_MIDDLE + offset;
}

/* The points go to the chart with a short pause every 16, so the
 * screen keeps up; a point is sent and not answered. */
static bento_status_t draw_trace(const scope_t *scope, uint32_t count)
{
    int32_t total = 0;
    int32_t mean;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; i < count; i++) {
        total += (int32_t)g_audio.points[i];
    }
    mean = floor_div(total, (int32_t)count);
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        (void)bento_ui_chart_set_next(
            scope->ui, scope->chart, 0U,
            trace_point((int32_t)g_audio.points[i], mean));
        if (((i + 1U) % (uint32_t)YIELD_EVERY) == 0U) {
            status = pause_ms(YIELD_MS);
        }
    }
    return status;
}

/* While running: a fresh window, its trace, then the level. Widget
 * setters ignore a refusal, as MicroPython ignores it (modui.c). */
static bento_status_t show_sound(const scope_t *scope)
{
    char text[LEVEL_TEXT_BYTES];
    uint32_t rms = 0U;
    uint32_t count = 0U;
    int32_t level = 0;
    bento_status_t status = fresh_rms(scope, &rms);

    if (status == BENTO_OK) {
        status = read_points(scope, &count);
    }
    if ((status == BENTO_OK) && (count != 0U)) {
        status = draw_trace(scope, count);
    }
    if (status == BENTO_OK) {
        status = read_level(scope, &level);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_value(scope->ui, scope->level_bar, level);
        (void)snprintf(text, sizeof(text), "Level: %d%%", (int)level);
        (void)bento_ui_set_text(scope->ui, scope->level_text, text,
                                (uint32_t)strlen(text));
    }
    return status;
}

static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* Pause and Play: the button's text says what a tap does next.
 * ui.poll() answers no events when it fails, so a failed poll is not an
 * error here. */
static void check_button(scope_t *scope)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(scope->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (event_is(&events[i], BENTO_UI_EVENT_CLICKED,
                     scope->button)) {
            const char *text = scope->running ? "Play" : "Pause";

            scope->running = !scope->running;
            (void)bento_ui_set_text(scope->ui, scope->button, text,
                                    (uint32_t)strlen(text));
        }
    }
}

/* One pass every LOOP_MS until RUN_MS has passed, Stop is pressed or
 * the microphone fails. */
static bento_status_t run_loop(scope_t *scope)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        check_button(scope);
        if (scope->running) {
            status = show_sound(scope);
        }
        if (status == BENTO_OK) {
            status = pause_ms(LOOP_MS);
        }
    }
    return status;
}

/* The run is over but the picture stays: a band over the bottom rows
 * says so. */
static bento_status_t show_finished(bento_ui_t *ui)
{
    const label_style_t style = {END_TEXT_X, END_TEXT_Y, COLOR_END_TEXT,
                                 END_FONT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t band;
    bento_ui_id_t label;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = 0;
    args.y = END_Y;
    args.w = END_W;
    args.h = END_H;
    args.color = COLOR_END;
    args.min_val = COLOR_END_TEXT; /* the original's min= */
    args.max_val = 0;
    args.font_px = 1;
    status = bento_ui_panel_create(ui, &args, &band);
    if (status == BENTO_OK) {
        status = add_label(ui, "ตัวอย่างจบแล้ว กดปุ่มไม่ได้", &style, &label);
    }
    return status;
}

int main(void)
{
    scope_t scope;
    bento_status_t status;

    (void)memset(&g_audio, 0, sizeof(g_audio));
    (void)memset(&scope, 0, sizeof(scope));
    scope.ui = bento_runtime_get_ui();
    status = build_screen(&scope);
    if (status == BENTO_OK) {
        status = start_mic(&scope);
    }
    if (status == BENTO_OK) {
        scope.running = true;
        /* lcd.print() ignores a console that is not ready (modlcd.c) */
        (void)bento_lcd_print(
            "sec3 ex14: live mic waveform - Pause freezes the trace");
        status = run_loop(&scope);
        /* the finally: mic.stop() swallows any error of the close */
        (void)bento_machine_pdm_close(scope.pdm);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex14: done\n");
        status = show_finished(scope.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
