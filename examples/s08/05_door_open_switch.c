/*
 * 05_door_open_switch.c -- สวิตช์แม่เหล็กบอกว่าประตูเปิดหรือปิด (คาบ 08)
 *
 * เก็บเส้นฐานของสนามแม่เหล็ก BMM350 สิบครั้งตอนประตูเปิด แล้ว 900 รอบ
 * รอบละ 100 ms วัดค่าเบี่ยงจากเส้นฐาน เกณฑ์สองระดับ (hysteresis):
 * ตอนเปิด เบี่ยงเกิน 45 คือปิด ตอนปิด เบี่ยงต่ำกว่า 25 คือเปิด
 * ต้องอ่านตรงกันสามครั้งติดจึงเปลี่ยนสถานะ กราฟเส้นฟ้าคือค่าเบี่ยง
 * เส้นแดงกับเหลืองคือสองเกณฑ์ ไฟแดงคือเปิด ไฟเขียวคือปิด พร้อมเสียงสั้น
 * บอร์ด: Dev Kit และ Eva Kit
 *
 * Takes a baseline of the BMM350's field ten times while the door is
 * open, then for 900 rounds of 100 ms measures the deviation from it.
 * Two thresholds (hysteresis): while open, a deviation above 45 means
 * shut; while shut, one below 25 means open; three agreeing readings in
 * a row change the state. The blue line is the deviation, red and
 * yellow the two thresholds; the red LED means open, the green one
 * shut, each change with a short tone. Boards: Dev Kit and Eva Kit.
 *
 * Twin of 05_door_open_switch.py
 * (embedded-systems-for-aiot-developer/examples/s08/05_door_open_switch.py),
 * written new, in the style of the library twin
 * aiot-s08-05-door-open-switch. Style: CLEAN_C.md.
 * What C changes only because the language forces it: the LEDs are
 * found by name as led_named() does, through bento_gpio_led_name() over
 * every LED the board has, falling back to LED 0; the original's except
 * OSError around magnetic() is any failed read here (the last size is
 * kept, as there); reals are bento_real_t, the board MicroPython's
 * float width; a build without ui.tone (an AttributeError in
 * MicroPython) ends the program with BENTO_E_UNSUPPORTED. As in the
 * original, the LEDs are switched off only when the 900 rounds end.
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_gpio.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* The detector and the run. */
enum {
    CLOSE_DEV = 45, /* a deviation above this: the door is shut */
    OPEN_DEV = 25,  /* below this: open; clearly under CLOSE_DEV */
    CONFIRM = 3,    /* agreeing readings in a row to change state */
    Y_MAX = 60,     /* the Y axis, well above CLOSE_DEV */
    BASE_SAMPLES = 10,
    ROUNDS = 900,
    LOOP_MS = 100,
    DEVIATION_SERIES = 0, /* the Chart is made with series 0 */
    TEXT_BYTES = 112 /* the longest text built here, with its numbers */
};

/* The two tones: MIDI note, velocity and length. */
enum {
    SHUT_NOTE = 72,
    SHUT_VELOCITY = 90,
    SHUT_MS = 90,
    OPEN_NOTE = 60,
    OPEN_VELOCITY = 100,
    OPEN_MS = 250
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 12,
    TITLE_Y = 8,
    CHART_X = 12,
    CHART_Y = 40,
    CHART_W = 472,
    CHART_H = 200,
    RIGHT_X = 496,
    LEGEND_DEVIATION_Y = 38,
    LEGEND_CLOSE_Y = 66,
    LEGEND_OPEN_Y = 94,
    NOW_CAPTION_Y = 120,
    SEG_Y = 148,
    SEG_W = 180,
    SEG_H = 44,
    BAR_Y = 200,
    BAR_H = 16,
    BAR_CAPTION_Y = 222,
    STATUS_X = 24,
    STATUS_Y = 264,
    SUB_Y = 300,
    FONT_TITLE = 24,
    FONT_STATUS = 28,
    FONT_TEXT = 20
};

/* Colours, 0xRRGGBB; 0 is the theme's colour. */
enum {
    COLOR_THEME = 0,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_BAD = 0xE5484D,
    COLOR_WARN = 0xF5A623,
    COLOR_OK = 0x30A46C,
    COLOR_CLOSE_LINE = 0xFF5555, /* also the "shut" label */
    COLOR_OPEN_LINE = 0xFFC83D,
    COLOR_OPENED = 0x55DD55
};

/* The widgets the program changes, the LEDs and the detector. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t chart;
    uint8_t close_series;
    uint8_t open_series;
    bento_ui_id_t seg;
    bento_ui_id_t bar;
    bento_ui_id_t status;
    bento_ui_id_t sub;
    uint32_t led_open;
    uint32_t led_shut;
    bento_real_t last_magnitude; /* kept when a read fails */
    bento_real_t base;
    bool shut;
    int32_t votes;
    int32_t flips;
} door_t;

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

static bento_real_t real_sqrt(bento_real_t value)
{
#if BENTO_FMT_REAL_DOUBLE
    return sqrt(value);
#else
    return sqrtf(value);
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

/* True, with its index, when the board has an LED of this name. */
static bool find_led(const char *name, uint32_t *led)
{
    const uint32_t count = bento_gpio_num_leds();
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < count) && !found; i++) {
        const char *led_name = NULL;

        if ((bento_gpio_led_name(i, &led_name) == BENTO_OK) &&
            (led_name != NULL) && (strcmp(led_name, name) == 0)) {
            *led = i;
            found = true;
        }
    }
    return found;
}

/* The LED of the first name the board has (the index differs between
 * boards, the name does not), else LED 0; gpio.led() refuses an LED
 * the board lacks. */
static bento_status_t led_named(const char *first, const char *second,
                                uint32_t *led)
{
    bento_status_t status = BENTO_OK;

    if (!find_led(first, led) &&
        ((second == NULL) || !find_led(second, led))) {
        *led = 0U;
        if (bento_gpio_num_leds() == 0U) {
            status = BENTO_E_INVALID_ARGUMENT;
        }
    }
    return status;
}

/* Red for open and green for shut: RGB_RED / RGB_GREEN on the Dev Kit,
 * LED1 / LED2 on the Eva Kit. */
static bento_status_t find_leds(door_t *door)
{
    uint32_t green = 0U;
    const bool has_rgb = find_led("RGB_GREEN", &green);
    bento_status_t status =
        led_named(has_rgb ? "RGB_RED" : "LED1", NULL, &door->led_open);

    if (status == BENTO_OK) {
        status = led_named("RGB_GREEN", "LED2", &door->led_shut);
    }
    return status;
}

/* led.on() and led.off() raise nothing (modgpio.c), so their status is
 * ignored, as there. */
static void set_led(uint32_t led, bool on)
{
    if (on) {
        (void)bento_gpio_led_on(led);
    } else {
        (void)bento_gpio_led_off(led);
    }
}

/* The field's size; a read that fails (the first ones after a reset)
 * gives the last size again instead of ending the program. */
static bento_real_t magnitude(door_t *door)
{
    float x_axis = 0.0F;
    float y_axis = 0.0F;
    float z_axis = 0.0F;

    if (bento_sensors_bmm350_magnetic(&x_axis, &y_axis, &z_axis) ==
        BENTO_OK) {
        const bento_real_t x = (bento_real_t)x_axis;
        const bento_real_t y = (bento_real_t)y_axis;
        const bento_real_t z = (bento_real_t)z_axis;

        door->last_magnitude = real_sqrt(((x * x) + (y * y)) + (z * z));
    }
    return door->last_magnitude;
}

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* ui.poll() whose events are not used; it answers no events when it
 * fails, so its status is not an error here. */
static void skip_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the twin
 * does too. */
static void set_text(bento_ui_t *ui, bento_ui_id_t label,
                     const char *text)
{
    (void)bento_ui_set_text(ui, label, text, (uint32_t)strlen(text));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const int16_t place[2], int32_t font_px,
                                uint32_t color, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place[0];
    args.y = place[1];
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* The title, and the chart with its two threshold lines. */
static bento_status_t add_chart(door_t *door)
{
    static const int16_t title_place[2] = {TITLE_X, TITLE_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(door->ui, "สวิตช์ประตูแบบแม่เหล็ก", title_place,
                  FONT_TITLE, COLOR_THEME, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = CHART_X;
        args.y = CHART_Y;
        args.w = CHART_W;
        args.h = CHART_H;
        args.min_val = 0;
        args.max_val = Y_MAX;
        status = bento_ui_chart_create(door->ui, &args, &door->chart);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(door->ui, door->chart,
                                           COLOR_CLOSE_LINE,
                                           &door->close_series);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            door->ui, door->chart, COLOR_OPEN_LINE, &door->open_series);
    }
    return status;
}

/* The legend and the caption above the Seg7. */
static bento_status_t add_legend(bento_ui_t *ui)
{
    static const int16_t deviation_place[2] = {RIGHT_X,
                                               LEGEND_DEVIATION_Y};
    static const int16_t close_place[2] = {RIGHT_X, LEGEND_CLOSE_Y};
    static const int16_t open_place[2] = {RIGHT_X, LEGEND_OPEN_Y};
    static const int16_t now_place[2] = {RIGHT_X, NOW_CAPTION_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "ฟ้า = เบี่ยงจากเส้นฐาน", deviation_place, FONT_TEXT,
                  COLOR_ACCENT, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "แดง = เกณฑ์ปิด 45", close_place,
                           FONT_TEXT, COLOR_BAD, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เหลือง = เกณฑ์เปิด 25", open_place,
                           FONT_TEXT, COLOR_WARN, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เบี่ยงตอนนี้ (หน่วยบอร์ด)", now_place,
                           FONT_TEXT, COLOR_THEME, &unused);
    }
    return status;
}

/* The Seg7, the bar and its caption. */
static bento_status_t add_readouts(door_t *door)
{
    static const int16_t caption_place[2] = {RIGHT_X, BAR_CAPTION_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = RIGHT_X;
    args.y = SEG_Y;
    args.w = SEG_W;
    args.h = SEG_H;
    status = bento_ui_seg7_create(door->ui, &args, &door->seg);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = RIGHT_X;
        args.y = BAR_Y;
        args.w = SEG_W;
        args.h = BAR_H;
        args.min_val = 0;
        args.max_val = Y_MAX;
        status = bento_ui_bar_create(door->ui, &args, &door->bar);
    }
    if (status == BENTO_OK) {
        status = add_label(door->ui, "แถบ = เบี่ยง 0-60", caption_place,
                           FONT_TEXT, COLOR_THEME, &unused);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(door_t *door)
{
    static const int16_t status_place[2] = {STATUS_X, STATUS_Y};
    static const int16_t sub_place[2] = {STATUS_X, SUB_Y};
    bento_status_t status =
        bento_ui_screen(door->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_chart(door);
    }
    if (status == BENTO_OK) {
        status = add_legend(door->ui);
    }
    if (status == BENTO_OK) {
        status = add_readouts(door);
    }
    if (status == BENTO_OK) {
        status = add_label(door->ui, "ประตูเปิด", status_place,
                           FONT_STATUS, COLOR_OK, &door->status);
    }
    if (status == BENTO_OK) {
        status = add_label(door->ui, "กำลังเก็บเส้นฐาน...", sub_place,
                           FONT_TEXT, COLOR_THEME, &door->sub);
    }
    if (status == BENTO_OK) {
        skip_events(door->ui);
    }
    return status;
}

/* The baseline, taken while the door is open, and the console line. */
static void take_baseline(door_t *door)
{
    bento_real_t sum = (bento_real_t)0.0;
    char text[TEXT_BYTES];
    int32_t i;

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>สวิตช์ประตูแบบแม่เหล็ก</h2>");
    (void)bento_lcd_print("เก็บเส้นฐานตอนประตูเปิด");
    for (i = 0; i < BASE_SAMPLES; i++) {
        sum += magnitude(door);
    }
    door->base = sum / (bento_real_t)BASE_SAMPLES;
    (void)snprintf(text, sizeof(text),
                   "เส้นฐาน %" PRId32 " | ปิดที่ %d เปิดที่ %d",
                   (int32_t)door->base, CLOSE_DEV, OPEN_DEV);
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
}

static void show_votes(const door_t *door)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "เส้นฐาน %" PRId32 " | โหวต %" PRId32 "/%d",
                   (int32_t)door->base, door->votes, CONFIRM);
    set_text(door->ui, door->sub, text);
}

/* ui.tone() is sent without an answer (modui.c), except in a build that
 * has no ui.tone at all. */
static bento_status_t play_tone(const door_t *door, uint8_t note,
                                uint8_t wave, uint8_t velocity,
                                uint16_t length_ms)
{
    bento_status_t status = BENTO_OK;

    if (bento_ui_sfx_tone(door->ui, note, wave, velocity, length_ms) ==
        BENTO_UI_E_UNSUPPORTED) {
        status = BENTO_E_UNSUPPORTED;
    }
    return status;
}

/* Three agreeing readings: the state flips, with its LED, label,
 * console line and tone. */
static bento_status_t flip_state(door_t *door, int32_t deviation)
{
    char text[TEXT_BYTES];
    bento_status_t status;

    door->shut = !door->shut;
    door->votes = 0;
    door->flips++;
    set_led(door->led_open, false);
    set_led(door->led_shut, false);
    if (door->shut) {
        set_led(door->led_shut, true);
        set_text(door->ui, door->status, "ประตูปิด");
        (void)bento_ui_set_color(door->ui, door->status,
                                 COLOR_CLOSE_LINE);
        (void)snprintf(text, sizeof(text), "ประตูปิด (เบี่ยง %" PRId32 ")",
                       deviation);
        (void)bento_lcd_print(text);
        status = play_tone(door, SHUT_NOTE, BENTO_UI_WAVE_SINE,
                           SHUT_VELOCITY, SHUT_MS);
    } else {
        set_led(door->led_open, true);
        set_text(door->ui, door->status, "ประตูเปิด");
        (void)bento_ui_set_color(door->ui, door->status, COLOR_OPENED);
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>ประตูเปิด (เบี่ยง %" PRId32
                       ")</span>",
                       deviation);
        (void)bento_lcd_print(text);
        status = play_tone(door, OPEN_NOTE, BENTO_UI_WAVE_SQUARE,
                           OPEN_VELOCITY, OPEN_MS);
    }
    return status;
}

/* One round: the deviation, the vote with the threshold of the current
 * state (that is the hysteresis), the chart, the Seg7 and the bar. */
static bento_status_t run_round(door_t *door)
{
    const bento_real_t deviation =
        real_fabs(magnitude(door) - door->base);
    const bool want = door->shut
                          ? (deviation < (bento_real_t)OPEN_DEV)
                          : (deviation > (bento_real_t)CLOSE_DEV);
    const bento_real_t clipped = (deviation < (bento_real_t)Y_MAX)
                                     ? deviation
                                     : (bento_real_t)Y_MAX;
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;

    door->votes = want ? (door->votes + 1) : 0;
    /* Chart.set_next() and the setters ignore a refusal (modui.c) */
    (void)bento_ui_chart_set_next(door->ui, door->chart,
                                  DEVIATION_SERIES, (int32_t)clipped);
    /* two horizontal lines: the shape of the hysteresis */
    (void)bento_ui_chart_set_next(door->ui, door->chart,
                                  door->close_series, CLOSE_DEV);
    (void)bento_ui_chart_set_next(door->ui, door->chart,
                                  door->open_series, OPEN_DEV);
    (void)snprintf(text, sizeof(text), "%" PRId32, (int32_t)deviation);
    set_text(door->ui, door->seg, text);
    (void)bento_ui_set_value(door->ui, door->bar, (int32_t)clipped);
    show_votes(door);
    if (door->votes >= CONFIRM) {
        status = flip_state(door, (int32_t)deviation);
    }
    if (status == BENTO_OK) {
        skip_events(door->ui);
        status = wait_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    door_t door;
    bento_status_t status;

    (void)memset(&door, 0, sizeof(door));
    door.ui = bento_runtime_get_ui();
    status = find_leds(&door);
    if (status == BENTO_OK) {
        status = build_screen(&door);
    }
    if (status == BENTO_OK) {
        int32_t round;

        take_baseline(&door);
        set_led(door.led_open, true);
        show_votes(&door);
        for (round = 0; (round < ROUNDS) && (status == BENTO_OK);
             round++) {
            status = run_round(&door);
        }
    }
    if (status == BENTO_OK) {
        char text[TEXT_BYTES];

        set_led(door.led_open, false);
        set_led(door.led_shut, false);
        (void)snprintf(text, sizeof(text),
                       "เปลี่ยนสถานะทั้งหมด %" PRId32 " ครั้ง", door.flips);
        (void)bento_lcd_print(text);
        (void)bento_lcd_print("<span class=muted>ลองตั้ง OPEN_DEV = "
                              "CLOSE_DEV แล้วเขย่าแม่เหล็กดู</span>");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
