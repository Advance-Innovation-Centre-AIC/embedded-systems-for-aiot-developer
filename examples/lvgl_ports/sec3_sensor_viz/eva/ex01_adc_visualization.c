/*
 * ex01_adc_visualization.c -- แสดงค่า ADC (ADC Visualization, Eva Kit)
 *
 * หมุนลูกบิด POTEN บนบอร์ด แล้วสไลเดอร์ แถบเปอร์เซ็นต์ ค่าดิบ และแรงดัน
 * บนจอขยับตาม โปรแกรมอ่านลูกบิดทุก 100 ms และวาดใหม่เมื่อค่าเปลี่ยน
 * เกิน 8 ทำงาน 2 นาทีแล้วจบ และวาดแถบ "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * Turn the POTEN knob on the board: the slider, the percentage bar, the
 * raw value and the voltage on the screen follow. The program reads the
 * knob every 100 ms and redraws when the value moves by more than 8. It
 * ends after two minutes and then draws a "finished" band over the
 * bottom rows.
 *
 * C twin of ex01_adc_visualization.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/eva/ex01_adc_visualization.py), from the library
 * twin aiot-lvgl-sec3-eva-ex01-adc-visualization. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. The touch events are still read every
 * round. What C changes: Python's int is int32_t here.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    FONT_BIG_PX = 24,
    TITLE_Y = 8,
    LABEL_X = 20,
    RAW_CAPTION_Y = 42,
    SLIDER_HALF_W = 150,
    SLIDER_Y = 66,
    SLIDER_H = 22,
    PERCENT_CAPTION_Y = 108,
    BAR_Y = 133,
    BAR_H = 25,
    READOUT_DX = 100, /* raw left of the centre, percent right */
    RAW_TEXT_DX = 34,
    PERCENT_TEXT_DX = 42,
    READOUT_Y = 224,
    VOLTAGE_Y = 262,
    HINT_Y = 336,
    HINT2_Y = 354,
    FOOTER_Y = 378,
    END_Y = 330, /* the "finished" band over the bottom rows */
    END_H = 36,
    END_TEXT_X = 232,
    END_TEXT_Y = 338,
    END_FONT_PX = 16
};

/* The knob. */
enum {
    POT_SHIFT = 4,  /* the Eva Kit's pot is 0-65535: keep 12 bits */
    ADC_MAX = 4095, /* the ADC's full scale */
    ADC_START = 2048,
    PERCENT = 100,
    START_PERCENT = 50,
    MIN_STEP = 8, /* a smaller move is noise, not a turn */
    NO_RAW = -1,
    VOLT_DIGITS = 3
};

/* Text buffers, each sized for any int32_t it can hold. */
enum {
    RAW_TEXT_BYTES = 17,     /* "Raw: " and an int32_t with its NUL */
    PERCENT_TEXT_BYTES = 22, /* "Percent: ", an int32_t, "%" and NUL */
    VOLTAGE_TEXT_BYTES = 48  /* "Voltage: ", a real and " V" */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,
    RUN_MS = 120000 /* the example ends after two minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x1A1A2E,
    COLOR_TITLE = 0xFFFFFF,
    COLOR_CAPTION = 0xCCCCCC,
    COLOR_RAW = 0x00FF00,
    COLOR_PERCENT = 0x00FFFF,
    COLOR_VOLTAGE = 0xFFFF00,
    COLOR_HINT = 0xAAAAAA,
    COLOR_FOOTER = 0x666666,
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

static const char FOOTER[] =
    "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t slider;
    bento_ui_id_t bar;
    bento_ui_id_t raw;
    bento_ui_id_t percent;
    bento_ui_id_t voltage;
} adc_screen_t;

/* A label's place and look: the original's x=, y=, color= and value=. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} label_style_t;

/* The board's knob, 0-4095: the Eva Kit's single pot (P15.1) reads
 * 0-65535, and its top 12 bits give the Dev Kit's range. */
static bento_status_t read_raw(int32_t *raw)
{
    uint16_t value = 0U;
    const bento_status_t status = bento_sensors_pot_read(&value);

    if (status == BENTO_OK) {
        *raw = (int32_t)((uint32_t)value >> POT_SHIFT);
    }
    return status;
}

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

static bento_status_t add_slider_and_bar(adc_screen_t *screen)
{
    const label_style_t raw_caption = {LABEL_X, RAW_CAPTION_Y,
                                       COLOR_CAPTION, FONT_PX};
    const label_style_t percent_caption = {LABEL_X, PERCENT_CAPTION_Y,
                                           COLOR_CAPTION, FONT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(
        screen->ui, "ADC Raw Value (0-4095):", &raw_caption, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = SCREEN_CENTER_X - SLIDER_HALF_W;
        args.y = SLIDER_Y;
        args.w = 2 * SLIDER_HALF_W;
        args.h = SLIDER_H;
        args.min_val = 0;
        args.max_val = ADC_MAX;
        args.font_px = ADC_START;
        status =
            bento_ui_slider_create(screen->ui, &args, &screen->slider);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "Percentage:", &percent_caption,
                           &unused);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = SCREEN_CENTER_X - SLIDER_HALF_W;
        args.y = BAR_Y;
        args.w = 2 * SLIDER_HALF_W;
        args.h = BAR_H;
        args.min_val = 0;
        args.max_val = PERCENT;
        args.font_px = 0;
        status = bento_ui_bar_create(screen->ui, &args, &screen->bar);
    }
    if (status == BENTO_OK) {
        /* created at 0, then set: a Bar created with a non-zero value
         * blanks the whole screen. A setter's refusal is ignored, as
         * MicroPython ignores it (modui.c). */
        (void)bento_ui_set_value(screen->ui, screen->bar,
                                 START_PERCENT);
    }
    return status;
}

static bento_status_t add_readouts(adc_screen_t *screen)
{
    const label_style_t raw_style = {SCREEN_CENTER_X - READOUT_DX -
                                         RAW_TEXT_DX,
                                     READOUT_Y, COLOR_RAW, FONT_PX};
    const label_style_t percent_style = {
        SCREEN_CENTER_X + READOUT_DX - PERCENT_TEXT_DX, READOUT_Y,
        COLOR_PERCENT, FONT_PX};
    bento_status_t status =
        add_label(screen->ui, "Raw: 2048", &raw_style, &screen->raw);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, "Percent: 50%", &percent_style,
                           &screen->percent);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(screen->ui, "Voltage: 1.650 V", VOLTAGE_Y,
                         COLOR_VOLTAGE, FONT_BIG_PX, &screen->voltage);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(adc_screen_t *screen)
{
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_background(screen->ui);
    }
    if (status == BENTO_OK) {
        status = add_centered(screen->ui,
                              "Part 2 - Example 1: ADC Visualization",
                              TITLE_Y, COLOR_TITLE, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_slider_and_bar(screen);
    }
    if (status == BENTO_OK) {
        status = add_readouts(screen);
    }
    if (status == BENTO_OK) {
        status = add_centered(screen->ui,
                              "Learning: lv_slider, lv_bar, ADC value "
                              "mapping",
                              HINT_Y, COLOR_HINT, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(screen->ui, "Formula: V = (raw / 4095) x 3.3V",
                         HINT2_Y, COLOR_HINT, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_centered(screen->ui, FOOTER, FOOTER_Y,
                              COLOR_FOOTER, FONT_PX, &unused);
    }
    return status;
}

/* A new reading: the slider, the bar and the three texts follow.
 * MicroPython's widget setters ignore a refusal (modui.c), so the twin
 * does too. */
static void show_raw(const adc_screen_t *screen, int32_t raw)
{
    const int32_t percent = (raw * PERCENT) / ADC_MAX;
    const bento_real_t full_scale_volts = (bento_real_t)3.3;
    const bento_real_t fraction =
        (bento_real_t)raw / (bento_real_t)ADC_MAX;
    char raw_text[RAW_TEXT_BYTES];
    char percent_text[PERCENT_TEXT_BYTES];
    char volts[BENTO_FMT_BUF_SIZE];
    char voltage_text[VOLTAGE_TEXT_BYTES];
    uint32_t length = 0U;

    (void)bento_ui_set_value(screen->ui, screen->slider, raw);
    (void)bento_ui_set_value(screen->ui, screen->bar, percent);
    (void)snprintf(raw_text, sizeof(raw_text), "Raw: %" PRId32, raw);
    (void)bento_ui_set_text(screen->ui, screen->raw, raw_text,
                            (uint32_t)strlen(raw_text));
    (void)snprintf(percent_text, sizeof(percent_text),
                   "Percent: %" PRId32 "%%", percent);
    (void)bento_ui_set_text(screen->ui, screen->percent, percent_text,
                            (uint32_t)strlen(percent_text));
    /* the buffer holds any real, so the formatter cannot refuse it */
    (void)bento_fmt_real_precision(volts, sizeof(volts),
                                   fraction * full_scale_volts,
                                   VOLT_DIGITS, &length);
    (void)snprintf(voltage_text, sizeof(voltage_text), "Voltage: %s V",
                   volts);
    (void)bento_ui_set_text(screen->ui, screen->voltage, voltage_text,
                            (uint32_t)strlen(voltage_text));
}

/* The original reads the touch events every round; with no "< Menu"
 * button none of them matters, and ui.poll() answers no events when it
 * fails, so its status is not an error. */
static void skip_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* Reads the knob every LOOP_MS until RUN_MS has passed, Stop is
 * pressed or the knob fails. */
static bento_status_t run_loop(const adc_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    int32_t shown = NO_RAW;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        int32_t raw = 0;

        skip_events(screen->ui);
        status = read_raw(&raw);
        if ((status == BENTO_OK) && (abs(raw - shown) > MIN_STEP)) {
            shown = raw;
            show_raw(screen, raw);
        }
        if (status == BENTO_OK) {
            status = bento_time_sleep_ms(LOOP_MS);
        }
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
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
    args.w = BENTO_RUNTIME_SCREEN_WIDTH_PX;
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
    adc_screen_t screen = {bento_runtime_get_ui(), 0U, 0U, 0U, 0U, 0U};
    bento_status_t status = build_screen(&screen);

    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print(
            "sec3 ex01: turn the pot - slider/bar/volts follow");
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex01: done\n");
        status = show_finished(screen.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
