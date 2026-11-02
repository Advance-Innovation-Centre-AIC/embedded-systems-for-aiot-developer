/*
 * ex04_temperature_gauge.c -- มาตรวัดอุณหภูมิ (Temperature Gauge, Dev Kit)
 *
 * อุณหภูมิจริงจาก SHT40 (I2C 0x44) แสดงเป็นเข็มบนหน้าปัดวงกลม 0-100
 * พร้อมตัวเลของศาที่กลางจอ ตัวเลขเป็นสีแดงเมื่อถึงโซนร้อน (75 องศาขึ้นไป)
 * อ่านทุก 500 ms วาดใหม่เมื่อค่าองศาเต็มเปลี่ยน ทำงาน 2 นาทีแล้วจบ
 * และวาดแถบ "ตัวอย่างจบแล้ว" ทับแถวล่าง
 *
 * The real temperature from the SHT40 (I2C 0x44) moves a needle on a
 * round 0-100 scale, with the degrees in the middle of the screen.
 * The number turns red in the hot zone (75 C and up). The program reads
 * every 500 ms, redraws when the whole degrees change, ends after two
 * minutes, and then draws a "finished" band over the bottom rows.
 *
 * C twin of ex04_temperature_gauge.py
 * (embedded-systems-for-aiot-developer/examples/lvgl_ports/
 * sec3_sensor_viz/devkit/ex04_temperature_gauge.py), from the
 * library twin aiot-lvgl-sec3-devkit-ex04-temperature-gauge.
 * Style: CLEAN_C.md. Left out on purpose: the "< Menu" button, which the
 * original shows only when the board's own menu starts it (MENU_MODE);
 * a C program is never started from that menu. The touch events are
 * still read every round. What C changes: Python's int is int32_t here.
 */
#include <inttypes.h>
#include <stdbool.h>
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
    SCALE_SIZE = 200,
    SCALE_Y = 99,
    TEMP_Y = 230,
    SOURCE_Y = 310,
    HINT_Y = 350,
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

/* The scale and its needle. */
enum {
    SCALE_MIN = 0,
    SCALE_MAX = 100,
    SCALE_TICKS = 21,
    SCALE_MAJOR_EVERY = 5,
    NEEDLE_PX = 85,           /* the needle's length */
    NEEDLE_LENGTH_SHIFT = 16, /* the length goes in the high 16 bits */
    NEEDLE_VALUE_MASK = 0xFFFF,
    HOT_ZONE_C = 75,
    TEMP_TEXT_BYTES = 14 /* any int32_t, " C" and the NUL */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 500,
    RUN_MS = 120000 /* the example ends after two minutes */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x1A1A2E,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_SCALE = 0x1565C0,
    COLOR_NEEDLE = 0xFF9800,
    COLOR_HOT = 0xF44336,
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
    bento_ui_id_t scale;
    bento_ui_id_t temp;
} gauge_screen_t;

/* A label's place and look: the original's x=, y=, color= and value=. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} label_style_t;

/* The real temperature: the Dev Kit's SHT40. */
static bento_status_t read_temp(float *celsius)
{
    return bento_sensors_sht40_temperature(celsius);
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

/* The round scale; .prop() and .ticks() never raise in MicroPython. */
static bento_status_t add_scale(gauge_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = SCREEN_CENTER_X - (SCALE_SIZE / 2);
    args.y = SCALE_Y;
    args.w = SCALE_SIZE;
    args.h = SCALE_SIZE;
    args.color = COLOR_SCALE;
    args.min_val = SCALE_MIN;
    args.max_val = SCALE_MAX;
    status = bento_ui_scale_create(screen->ui, &args, &screen->scale);
    if (status == BENTO_OK) {
        (void)bento_ui_set_prop(screen->ui, screen->scale,
                                BENTO_UI_PROP_SCALE_MODE,
                                BENTO_UI_SCALE_ROUND_OUT);
        (void)bento_ui_set_ticks(screen->ui, screen->scale, SCALE_TICKS,
                                 SCALE_MAJOR_EVERY);
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
        status = add_centered(screen->ui,
                              "Part 2 - Example 4: Temperature Gauge",
                              TITLE_Y, COLOR_TEXT, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_scale(screen);
    }
    if (status == BENTO_OK) {
        status = add_centered(screen->ui, "-- C", TEMP_Y, COLOR_TEXT,
                              FONT_BIG_PX, &screen->temp);
    }
    if (status == BENTO_OK) {
        status = add_centered(screen->ui, "(SHT40)", SOURCE_Y,
                              COLOR_HINT, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_centered(screen->ui,
                              "Learning: lv_scale_4, needle via "
                              "PROP_SCALE_NEEDLE, hot zone >= 75 C",
                              HINT_Y, COLOR_HINT, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_centered(screen->ui,
                         "(C) 2023-2026 AIC-EEC.com and BiiL Centre, "
                         "Burapha University",
                         FOOTER_Y, COLOR_FOOTER, FONT_PX, &unused);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_prop(screen->ui, screen->scale,
                                BENTO_UI_PROP_SCALE_NEEDLE_COLOR,
                                COLOR_NEEDLE);
    }
    return status;
}

/* lv_scale's own needle turns in place, so nothing blinks: its length
 * in the high 16 bits, the value in the low 16. */
static void draw_needle(const gauge_screen_t *screen, float value)
{
    const uint32_t packed =
        ((uint32_t)NEEDLE_PX << NEEDLE_LENGTH_SHIFT) |
        ((uint32_t)(int32_t)value & (uint32_t)NEEDLE_VALUE_MASK);

    (void)bento_ui_set_prop(screen->ui, screen->scale,
                            BENTO_UI_PROP_SCALE_NEEDLE,
                            (int32_t)packed);
}

/* New whole degrees: the needle (kept on the 0-100 scale), the number
 * and its colour. MicroPython's widget setters ignore a refusal
 * (modui.c), so the twin does too. */
static void show_temp(const gauge_screen_t *screen, float celsius,
                      int32_t shown)
{
    float needle = celsius;
    char text[TEMP_TEXT_BYTES];

    if (celsius < (float)SCALE_MIN) {
        needle = (float)SCALE_MIN;
    } else if (celsius > (float)SCALE_MAX) {
        needle = (float)SCALE_MAX;
    } else {
        /* inside the scale: the needle shows the reading itself */
    }
    draw_needle(screen, needle);
    (void)snprintf(text, sizeof(text), "%" PRId32 " C", shown);
    (void)bento_ui_set_text(screen->ui, screen->temp, text,
                            (uint32_t)strlen(text));
    (void)bento_ui_set_color(screen->ui, screen->temp,
                             (shown >= HOT_ZONE_C) ? COLOR_HOT
                                                   : COLOR_TEXT);
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

/* Reads the temperature every LOOP_MS until RUN_MS has passed, Stop is
 * pressed or the sensor fails. */
static bento_status_t run_loop(const gauge_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bool has_shown = false;
    int32_t shown = 0;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        float celsius = 0.0F;

        skip_events(screen->ui);
        status = read_temp(&celsius);
        /* int() cuts toward zero, as the cast does */
        if ((status == BENTO_OK) &&
            (!has_shown || ((int32_t)celsius != shown))) {
            has_shown = true;
            shown = (int32_t)celsius;
            show_temp(screen, celsius, shown);
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
        (void)bento_lcd_print(
            "sec3 ex04: real temperature on a round gauge (SHT40)");
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("sec3 ex04: done\n");
        status = show_finished(screen.ui);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
