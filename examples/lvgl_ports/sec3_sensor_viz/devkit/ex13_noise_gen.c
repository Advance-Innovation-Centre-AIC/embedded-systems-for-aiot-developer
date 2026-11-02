/*
 * ex13_noise_gen.c -- เครื่องกำเนิดสัญญาณรบกวน (Noise Generator)
 *
 * สัญญาณรบกวนขาวจาก LFSR 16 บิต (taps 0xB400 แบบเดียวกับ
 * aic_scope_generate_noise) วิ่งบนกราฟ 200 จุด ทีละแปดจุดทุก 100 ms
 * ปุ่ม Run ให้เส้นวิ่ง ปุ่ม Stop หยุดภาพนิ่ง ทำงาน 2 นาทีแล้วจบ
 * และวาดแถบ "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * White noise from a 16-bit LFSR (taps 0xB400, as in
 * aic_scope_generate_noise) scrolls across a 200-point chart, eight
 * points every 100 ms. Run lets it scroll, Stop freezes it. The program
 * ends after two minutes and then draws a "finished" band over the
 * bottom rows.
 *
 * C twin of ex13_noise_gen.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/devkit/ex13_noise_gen.py), from the library twin
 * aiot-lvgl-sec3-ex13-noise-gen. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. What C changes: lfsr_noise() builds no
 * list; each point goes to the chart as it is made.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    BUTTON_FONT_PX = 16,
    TITLE_Y = 8,
    CHART_X = 66,
    CHART_Y = 54,
    CHART_W = 660,
    CHART_H = 250,
    INFO_Y = 324,
    RUN_X = 560,
    STOP_X = 676,
    BUTTON_Y = 316,
    BUTTON_W = 104,
    BUTTON_H = 46,
    FOOTER_Y = 378
};

/* The "finished" band over the bottom rows. */
enum {
    END_Y = 330,
    END_W = 792,
    END_H = 36,
    END_TEXT_X = 232,
    END_TEXT_Y = 334,
    END_FONT_PX = 20
};

/* The noise. */
enum {
    CHART_POINTS = 200,
    POINTS_PER_ROUND = 8, /* eight points a round: the line flows */
    CHART_MAX = 100,
    NOISE_MIDDLE = 50,
    NOISE_AMPLITUDE = 20,
    LFSR_START = 0xACE1,
    LFSR_TAPS = 0xB400
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
    COLOR_TITLE = 0xFF8800,
    COLOR_NOISE = 0xFF6600,
    COLOR_INFO = 0x888888,
    COLOR_RUN = 0x1B5E20,
    COLOR_STOP = 0x333333,
    COLOR_FOOTER = 0x666666
};

/* The "finished" band's colours, 0xRRGGBB. */
enum {
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

static const char FOOTER[] =
    "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";

/* The widgets the program changes, and the generator's state. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    bento_ui_id_t run;
    bento_ui_id_t stop;
    bool running;
    uint32_t lfsr;
} noise_t;

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

/* Widget arguments with a place, a size and a colour. */
static bento_ui_widget_args_t place_at(int16_t x, int16_t y,
                                       int16_t width, int16_t height,
                                       uint32_t color)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = x;
    args.y = y;
    args.w = width;
    args.h = height;
    args.color = color;
    return args;
}

/* A button with the original's 16 px text. */
static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 const bento_ui_widget_args_t *place,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args = *place;

    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.font_px = BUTTON_FONT_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* The scope: a 200-point chart; .prop() never raises in MicroPython. */
static bento_status_t add_chart(noise_t *noise)
{
    bento_ui_widget_args_t args =
        place_at(CHART_X, CHART_Y, CHART_W, CHART_H, COLOR_NOISE);
    bento_status_t status;

    args.min_val = 0;
    args.max_val = CHART_MAX;
    status = bento_ui_chart_create(noise->ui, &args, &noise->chart);
    if (status == BENTO_OK) {
        (void)bento_ui_set_prop(noise->ui, noise->chart,
                                BENTO_UI_PROP_CHART_POINTS,
                                CHART_POINTS);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(noise_t *noise)
{
    const bento_ui_widget_args_t run =
        place_at(RUN_X, BUTTON_Y, BUTTON_W, BUTTON_H, COLOR_RUN);
    const bento_ui_widget_args_t stop =
        place_at(STOP_X, BUTTON_Y, BUTTON_W, BUTTON_H, COLOR_STOP);
    bento_ui_id_t unused;
    bento_status_t status = clear_screen(noise->ui);

    if (status == BENTO_OK) {
        status = add_background(noise->ui);
    }
    if (status == BENTO_OK) {
        status = add_centered(
            noise->ui,
            "Part 3 - Example 2: Noise Generator - White Noise",
            TITLE_Y, COLOR_TITLE, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_chart(noise);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(noise->ui, "LFSR-based pseudo-random noise",
                         INFO_Y, COLOR_INFO, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_button(noise->ui, "Run", &run, &noise->run);
    }
    if (status == BENTO_OK) {
        status = add_button(noise->ui, "Stop", &stop, &noise->stop);
    }
    if (status == BENTO_OK) {
        status = add_centered(noise->ui, FOOTER, FOOTER_Y, COLOR_FOOTER,
                              FONT_PX, &unused);
    }
    return status;
}

/* One step of the 16-bit LFSR with taps 0xB400, the pseudo-random
 * source of aic_scope_generate_noise. */
static uint32_t lfsr_next(uint32_t *lfsr)
{
    const uint32_t low_bit = *lfsr & 1U;

    *lfsr >>= 1U;
    if (low_bit != 0U) {
        *lfsr ^= (uint32_t)LFSR_TAPS;
    }
    return *lfsr;
}

/* Eight noise points around the middle; a point is sent and not
 * answered, as in MicroPython. */
static void push_noise(noise_t *noise)
{
    int32_t i;

    for (i = 0; i < POINTS_PER_ROUND; i++) {
        const uint32_t bits = lfsr_next(&noise->lfsr);
        const int32_t point =
            NOISE_MIDDLE +
            ((int32_t)(bits % (uint32_t)((2 * NOISE_AMPLITUDE) + 1)) -
             NOISE_AMPLITUDE);

        (void)bento_ui_chart_set_next(noise->ui, noise->chart, 0U,
                                      point);
    }
}

static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* One pass: the touch events, then new points while Run is on.
 * ui.poll() answers no events when it fails, so a failed poll is not an
 * error here. */
static void run_pass(noise_t *noise)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(noise->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (event_is(&events[i], BENTO_UI_EVENT_CLICKED, noise->run)) {
            noise->running = true;
        } else if (event_is(&events[i], BENTO_UI_EVENT_CLICKED,
                            noise->stop)) {
            noise->running = false;
        } else {
            /* any other event changes nothing */
        }
    }
    if (noise->running) {
        push_noise(noise);
    }
}

/* One pass every LOOP_MS until RUN_MS has passed or Stop is pressed. */
static bento_status_t run_loop(noise_t *noise)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        run_pass(noise);
        status = bento_time_sleep_ms(LOOP_MS);
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
    noise_t noise;
    bento_status_t status;

    (void)memset(&noise, 0, sizeof(noise));
    noise.ui = bento_runtime_get_ui();
    noise.lfsr = LFSR_START;
    status = build_screen(&noise);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print("sec3 ex13: noise scrolls while Run");
        noise.running = true;
        status = run_loop(&noise);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex13: done\n");
        status = show_finished(noise.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
