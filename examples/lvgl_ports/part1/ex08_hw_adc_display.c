/*
 * ex08_hw_adc_display.c -- ค่า ADC จากตัวต้านทานปรับค่าจริง (HW ADC Display, Part 1)
 *
 * จอสามแถวสามสี: แถวเขียวเป็น slider ค่า ADC ดิบ 0-4095 พร้อม "Raw: n",
 * แถวฟ้าเป็น bar เปอร์เซ็นต์พร้อม "n%" และแถวเหลืองเป็นแรงดัน "Voltage: n.nnn V"
 * ตัวโต อ่านตัวต้านทานปรับค่าจริงทุก 100 ms (Dev Kit ใช้ pots.read(0),
 * Eva ใช้ sensors.pot.read() >> 4) และเขียนจอเมื่อค่าขยับเกิน 8 บน Dev Kit
 * ระดับยังขึ้นเป็นแถบบน RGB Matrix ด้วย ทำงาน 2 นาทีแล้วจบ
 *
 * Three rows in three colours: green, a slider of the raw 0-4095 ADC
 * value with "Raw: n"; cyan, a percentage bar with "n%"; yellow, a
 * large "Voltage: n.nnn V". The real potentiometer is read every 100 ms
 * (pots.read(0) on the Dev Kit, sensors.pot.read() >> 4 on the Eva
 * Kit), and the screen is written when the value moves by more than 8.
 * On the Dev Kit the level is also a bar on the RGB matrix. The example
 * runs for two minutes, then ends.
 *
 * Twin of ex08_hw_adc_display.py (embedded-systems-for-aiot-developer/
 * examples/lvgl_ports/part1/ex08_hw_adc_display.py), written into one
 * clean file from the library's C twin of the Example Explorer program
 * aiot-lvgl-part1-ex08-hw-adc-display (the same code). Style:
 * CLEAN_C.md. Where C differs:
 *  - `import pots` / `import rgbmatrix` succeeding is
 *    bento_pots_count() > 0 / bento_rgbmatrix_available();
 *  - "%.3f" % v is bento_fmt_real_precision(), the board's own float
 *    text.
 * Left out on purpose: the "< Menu" button, which the original shows
 * only when the board's own menu starts it (MENU_MODE); a C program is
 * never started from that menu.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

#include "bento_fmt.h"
#include "bento_pots.h"
#include "bento_rgbmatrix.h"
#include "bento_sensors.h"

/* Screen layout on the 792 x 398 canvas, top to bottom. */
enum {
    SCREEN_CENTER_X = 396,
    FONT_PX = 14,
    VOLT_FONT_PX = 24,
    TITLE_Y = 8,
    ROW_X = 241, /* C's CENTER(-55, ...) for a 200 wide row: CX - 55 - 100 */
    ROW_W = 200,
    VALUE_DX = 215, /* the value label right of its row */
    RAW_CAPTION_Y = 133,
    SLIDER_Y = 158,
    SLIDER_H = 16,
    PCT_CAPTION_Y = 191,
    BAR_Y = 211,
    BAR_H = 18,
    VOLT_Y = 253,
    HINT_Y = 352,
    FOOTER_Y = 374
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_BACKGROUND = 0x1A1A2E,
    COLOR_TEXT = 0xFFFFFF,
    COLOR_HINT = 0xAAAAAA,
    COLOR_FOOTER = 0x666666,
    COLOR_RAW = 0x00FF00,
    COLOR_PCT = 0x00FFFF,
    COLOR_VOLT = 0xFFFF00
};

/* The ADC values and their limits. */
enum {
    ADC_MAX = 4095,
    RAW_START = 2048,
    PCT_FULL = 100,
    PCT_START = 50,
    SENSOR_DIVISOR = 16, /* >> 4: sensors.pot.read()'s 0..65535 down to 0..4095 */
    CHANGE_MIN = 8,      /* write the screen only when the value really moved */
    VOLT_DIGITS = 3,     /* "%.3f" */
    MATRIX_LEVELS = 5,   /* the matrix bar is five pixels long */
    NO_VALUE = -1        /* prev before the first read */
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 100,   /* read the potentiometer this often */
    RUN_MS = 120000  /* the example ends after two minutes */
};

/* Text buffers: "Voltage: " + a float text + " V", and the shorter
 * value lines. */
enum {
    TEXT_CHARS = 64
};

/* The widgets the loop changes and the hardware the imports found. */
typedef struct {
    bento_ui_t *ui;
    bool has_pots;
    bool has_matrix;
    bento_ui_id_t slider;
    bento_ui_id_t raw_label;
    bento_ui_id_t bar;
    bento_ui_id_t pct_label;
    bento_ui_id_t volt_label;
    int32_t prev;
} adc_screen_t;

/* Python's len(): characters, not bytes, so a Thai text is centred
 * like the original. */
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

/* x that centres a text: half its width left of the centre, taking a
 * character as half the font size wide (the original's len(text) *
 * size, floor-divided by 4). */
static int16_t centered_x(const char *text, int32_t font_px)
{
    return (int16_t)(SCREEN_CENTER_X - (((utf8_length(text) * font_px) / 2) / 2));
}

/* The potentiometer as 0..4095: pots.read(0) (VR1) on the Dev Kit,
 * else the sensors module's 16-bit value shifted down. */
static bento_status_t read_raw(const adc_screen_t *screen, int32_t *raw)
{
    uint16_t value = 0U;
    bento_status_t status;

    if (screen->has_pots) {
        status = bento_pots_read(0U, &value);
        *raw = (int32_t)value;
    } else {
        status = bento_sensors_pot_read(&value);
        *raw = (int32_t)value / SENSOR_DIVISOR;
    }
    return status;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text, int16_t x, int16_t y,
                                uint32_t color, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.color = color;
    args.font_px = FONT_PX;
    return bento_ui_label_create(ui, &args, out_id);
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
    /* min= repeats the colour, as the original does */
    args.min_val = COLOR_BACKGROUND;
    args.max_val = 0;
    args.font_px = 0;
    return bento_ui_panel_create(ui, &args, &panel);
}

/* Green row: the raw value as a slider and as a number. */
static bento_status_t add_raw_row(adc_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(screen->ui, "ADC Raw Value (0-4095)", ROW_X,
                                      RAW_CAPTION_Y, COLOR_RAW, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = ROW_X;
        args.y = SLIDER_Y;
        args.w = ROW_W;
        args.h = SLIDER_H;
        args.min_val = 0;
        args.max_val = ADC_MAX;
        args.font_px = RAW_START;
        args.color = COLOR_RAW;
        status = bento_ui_slider_create(screen->ui, &args, &screen->slider);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "Raw: 2048", ROW_X + VALUE_DX, SLIDER_Y, COLOR_RAW,
                           &screen->raw_label);
    }
    return status;
}

/* Cyan row: the percentage as a bar and as a number. The bar is made
 * at 0 and set afterwards: a bar made with a value other than zero
 * blanked the whole screen. */
static bento_status_t add_percent_row(adc_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(screen->ui, "Percentage", ROW_X, PCT_CAPTION_Y, COLOR_PCT, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = ROW_X;
        args.y = BAR_Y;
        args.w = ROW_W;
        args.h = BAR_H;
        args.min_val = 0;
        args.max_val = PCT_FULL;
        args.font_px = 0;
        args.color = COLOR_PCT;
        status = bento_ui_bar_create(screen->ui, &args, &screen->bar);
    }
    if (status == BENTO_OK) {
        /* a setter's refusal was ignored before too */
        (void)bento_ui_set_value(screen->ui, screen->bar, PCT_START);
        status = add_label(screen->ui, "50%", ROW_X + VALUE_DX, BAR_Y, COLOR_PCT,
                           &screen->pct_label);
    }
    return status;
}

/* Yellow row: the voltage in a large font. */
static bento_status_t add_volt_row(adc_screen_t *screen)
{
    const char *text = "Voltage: 1.650 V";
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = centered_x(text, VOLT_FONT_PX);
    args.y = VOLT_Y;
    args.color = COLOR_VOLT;
    args.font_px = VOLT_FONT_PX;
    return bento_ui_label_create(screen->ui, &args, &screen->volt_label);
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(adc_screen_t *screen)
{
    const char *title = "Part 1 Ex8: Hardware ADC Display";
    const char *hint = "[Part II] Turn the potentiometer on the board";
    const char *footer = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University";
    bento_ui_t *ui = screen->ui;
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX, BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_background(ui);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, title, centered_x(title, FONT_PX), TITLE_Y, COLOR_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_raw_row(screen);
    }
    if (status == BENTO_OK) {
        status = add_percent_row(screen);
    }
    if (status == BENTO_OK) {
        status = add_volt_row(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, hint, centered_x(hint, FONT_PX), HINT_Y, COLOR_HINT, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, footer, centered_x(footer, FONT_PX), FOOTER_Y, COLOR_FOOTER, &unused);
    }
    return status;
}

static void set_text(const adc_screen_t *screen, bento_ui_id_t widget, const char *text)
{
    /* MicroPython's widget setters ignore a refusal (modui.c), so the
     * twin does too. */
    (void)bento_ui_set_text(screen->ui, widget, text, (uint32_t)strlen(text));
}

/* raw / 4095 * 3.3 as the board's float, with three decimals. */
static void show_voltage(const adc_screen_t *screen, int32_t raw)
{
    const bento_real_t full_scale_volts = (bento_real_t)3.3;
    const bento_real_t ratio = (bento_real_t)raw / (bento_real_t)ADC_MAX;
    const bento_real_t volts = ratio * full_scale_volts;
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_CHARS];
    uint32_t number_len = 0U;

    /* BENTO_FMT_BUF_SIZE always holds the text, so it cannot fail here */
    (void)bento_fmt_real_precision(number, sizeof(number), volts, VOLT_DIGITS, &number_len);
    (void)snprintf(text, sizeof(text), "Voltage: %s V", number);
    set_text(screen, screen->volt_label, text);
}

/* A new reading: every row follows. The values are never negative, so
 * C's division is the floor division. */
static bento_status_t show_reading(const adc_screen_t *screen, int32_t raw)
{
    const int32_t pct = (raw * PCT_FULL) / ADC_MAX;
    char text[TEXT_CHARS];
    bento_status_t status = BENTO_OK;

    (void)bento_ui_set_value(screen->ui, screen->slider, raw);
    (void)bento_ui_set_value(screen->ui, screen->bar, pct);
    (void)snprintf(text, sizeof(text), "Raw: %" PRId32, raw);
    set_text(screen, screen->raw_label, text);
    (void)snprintf(text, sizeof(text), "%" PRId32 "%%", pct);
    set_text(screen, screen->pct_label, text);
    show_voltage(screen, raw);
    if (screen->has_matrix) {
        status = bento_rgbmatrix_bar(((pct * MATRIX_LEVELS) / PCT_FULL) + ((pct != 0) ? 1 : 0),
                                     MATRIX_LEVELS, BENTO_RGBMATRIX_GREEN);
    }
    return status;
}

static bento_status_t read_once(adc_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    int32_t raw = 0;
    int32_t change;
    bento_status_t status;

    /* No widget here answers a tap (the "< Menu" button is left out),
     * so the events are not read, and ui.poll() answers no events
     * when it fails. */
    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count, NULL);
    status = read_raw(screen, &raw);
    change = raw - screen->prev;
    if ((status == BENTO_OK) && (((change < 0) ? -change : change) > CHANGE_MIN)) {
        screen->prev = raw;
        status = show_reading(screen, raw);
    }
    return status;
}

/* Reads the potentiometer every LOOP_MS until RUN_MS has passed or
 * Stop is pressed. */
static bento_status_t run_loop(adc_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) < RUN_MS)) {
        status = read_once(screen);
        if (status == BENTO_OK) {
            status = bento_time_sleep_ms(LOOP_MS);
        }
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    adc_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    screen.has_pots = (bento_pots_count() > 0U);
    screen.has_matrix = bento_rgbmatrix_available();
    screen.prev = NO_VALUE;
    status = build_screen(&screen);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    if (status == BENTO_OK) {
        (void)bento_lcd_print(
            "ex08: three rows, three colors - raw / percent / volts from the pot");
    }
    if (status == BENTO_OK) {
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        (void)printf("ex08_hw_adc_display: done\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
