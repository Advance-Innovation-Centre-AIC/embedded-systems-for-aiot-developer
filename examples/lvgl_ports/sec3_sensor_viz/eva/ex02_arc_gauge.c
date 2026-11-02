/*
 * ex02_arc_gauge.c -- มาตรวัดโค้ง (Arc Gauge, Eva Kit)
 *
 * หมุนลูกบิด POTEN บนบอร์ด แล้วส่วนโค้งกับตัวเลขเปอร์เซ็นต์กลางวงขยับตาม
 * โปรแกรมแปลงค่า ADC 0-4095 เป็น 0-100 % แล้วส่งให้ ui.Arc อ่านลูกบิด
 * ทุก 100 ms ทำงาน 2 นาทีแล้วจบ และวาดแถบ "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * Turn the POTEN knob on the board: the arc and the percentage in its
 * middle follow. The program turns the ADC's 0-4095 into 0-100 % and
 * gives it to ui.Arc. It reads the knob every 100 ms, ends after two
 * minutes, and then draws a "finished" band over the bottom rows.
 *
 * C twin of ex02_arc_gauge.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/eva/ex02_arc_gauge.py), from the library twin
 * aiot-lvgl-sec3-eva-ex02-arc-gauge. Style: CLEAN_C.md.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu. The touch events are still read every
 * round. What C changes: Python's int is int32_t here.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"
#include "bento_sensors.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    FONT_BIG_PX = 24,
    TITLE_Y = 8,
    ARC_SIZE = 200,
    ARC_Y = 99,
    PERCENT_Y = 187,
    HINT_Y = 336,
    HINT2_Y = 354,
    FOOTER_Y = 378
};

/* The "finished" band over the bottom rows. */
enum {
    END_Y = 330,
    END_W = 792,
    END_H = 36,
    END_TEXT_X = 232,
    END_TEXT_Y = 338,
    END_FONT_PX = 16
};

/* The knob and the gauge. */
enum {
    POT_SHIFT = 4,  /* the Eva Kit's pot is 0-65535: keep 12 bits */
    ADC_MAX = 4095, /* the ADC's full scale */
    PERCENT = 100,  /* the gauge shows 0-100 % */
    START_PERCENT = 50,
    NO_PERCENT = -1,   /* no reading shown yet */
    PERCENT_BYTES = 13 /* any int32_t, its "%" and the NUL */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,   /* read the knob this often */
    RUN_MS = 120000  /* the example ends after two minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x0F0F23,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_ARC = 0x00BCD4,
    COLOR_HINT = 0xAAAAAA,
    COLOR_FOOTER = 0x666666
};

/* The "finished" band's colours, 0xRRGGBB. */
enum {
    COLOR_END = 0x1A1A2E,
    COLOR_END_TEXT = 0xFF6600
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t arc;
    bento_ui_id_t percent;
} gauge_screen_t;

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
        *raw = (int32_t)(uint16_t)(value >> (uint32_t)POT_SHIFT);
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

/* The arc and the number in its middle, both at 50 % to start. */
static bento_status_t add_gauge(gauge_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = SCREEN_CENTER_X - (ARC_SIZE / 2);
    args.y = ARC_Y;
    args.w = ARC_SIZE;
    args.h = ARC_SIZE;
    args.min_val = 0;
    args.max_val = PERCENT;
    args.font_px = START_PERCENT;
    args.color = COLOR_ARC;
    status = bento_ui_arc_create(screen->ui, &args, &screen->arc);
    if (status == BENTO_OK) {
        status = add_centered(screen->ui, "50%", PERCENT_Y, COLOR_TEXT,
                              FONT_BIG_PX, &screen->percent);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(gauge_screen_t *screen)
{
    bento_ui_id_t unused;
    bento_status_t status = clear_screen(screen->ui);

    if (status == BENTO_OK) {
        status = add_background(screen->ui);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(screen->ui, "Part 2 - Example 2: Arc Gauge",
                         TITLE_Y, COLOR_TEXT, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_gauge(screen);
    }
    if (status == BENTO_OK) {
        status = add_centered(
            screen->ui, "Learning: lv_arc_create, lv_arc_set_rotation",
            HINT_Y, COLOR_HINT, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_centered(
            screen->ui, "Use case: Gauge display, gyroscope angle",
            HINT2_Y, COLOR_HINT, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(screen->ui,
                         "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                         "Burapha University",
                         FOOTER_Y, COLOR_FOOTER, FONT_PX, &unused);
    }
    return status;
}

/* A new percentage: the arc and the number in its middle follow.
 * MicroPython's widget setters ignore a refusal (modui.c), so the twin
 * does too. */
static void show_percent(const gauge_screen_t *screen, int32_t percent)
{
    char text[PERCENT_BYTES];

    (void)bento_ui_set_value(screen->ui, screen->arc, percent);
    (void)snprintf(text, sizeof(text), "%" PRId32 "%%", percent);
    (void)bento_ui_set_text(screen->ui, screen->percent, text,
                            (uint32_t)strlen(text));
}

/* The knob as a percentage, floor-divided as the original does; the
 * screen changes only when it differs from the one shown. Answers the
 * percentage now shown. */
static int32_t show_if_changed(const gauge_screen_t *screen,
                               int32_t raw, int32_t shown)
{
    const int32_t percent = (raw * PERCENT) / ADC_MAX;

    if (percent != shown) {
        show_percent(screen, percent);
    }
    return percent;
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
static bento_status_t run_loop(const gauge_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    int32_t shown = NO_PERCENT;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        int32_t raw = 0;

        skip_events(screen->ui);
        status = read_raw(&raw);
        if (status == BENTO_OK) {
            shown = show_if_changed(screen, raw, shown);
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
    gauge_screen_t screen = {bento_runtime_get_ui(), 0U, 0U};
    bento_status_t status = build_screen(&screen);

    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print("sec3 ex02: pot -> arc gauge percent");
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex02: done\n");
        status = show_finished(screen.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
