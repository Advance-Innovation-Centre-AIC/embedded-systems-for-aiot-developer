/*
 * 02_heartbeat_liveness.c -- ไฟหัวใจเต้น พิสูจน์ว่าลูปยังหมุน
 * (a heartbeat LED that proves the loop is alive)
 *
 * ไฟดวงหนึ่งกะพริบเป็นจังหวะหัวใจ (ติด 80 ดับ 120 ติด 80 ดับ 900 ms) โดยดูนาฬิกา
 * ไม่ใช่การหลับนาน ๆ ลูปจึงว่างทำงานจริงได้ตลอด กราฟเส้นฟ้าคือระดับไฟ เส้นส้มคือ
 * จำนวนรอบลูปในทุก 60 ms Seg7 นับรอบลูปสะสม การ์ดขวาบอกช่วงของจังหวะ และ LCD
 * รายงานทุกสองวินาที หลัง 4000 รอบไฟดับและโปรแกรมจบ
 *
 * One LED blinks a heartbeat (on 80, off 120, on 80, off 900 ms) by
 * watching the clock instead of sleeping long, so the loop stays free
 * for real work. The chart's blue line is the LED level and the orange
 * line the loop rounds in each 60 ms; the Seg7 counts all rounds, the
 * card shows the pattern step, and the LCD reports every two seconds.
 * After 4000 rounds the LED goes off and the program ends.
 *
 * Twin of 02_heartbeat_liveness.py
 * (embedded-systems-for-aiot-developer/examples/usecase/
 *  02_heartbeat_liveness.py), rewritten clean from the library twin
 * aiot-usecase-02-heartbeat-liveness.
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - led_named() returns the LED's index (bento_gpio's LED handle); the
 *    name table is bento_gpio_led_name() for every LED of the board;
 *  - this course file has no try/finally: as in the .py, the LED is
 *    turned off after the 4000 rounds only, not when Stop or an error
 *    ends the program early;
 *  - Stop during a sleep raises KeyboardInterrupt in MicroPython; here
 *    the sleep returns and the program ends at that point.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_runtime.h"

/* The loop's rhythm and the chart's scale. */
enum {
    SAMPLE_MS = 60,   /* one chart point; 50 points fill it in 3 s */
    LOOP_GAIN = 6,    /* loop rounds are multiplied so the line shows */
    REPORT_MS = 2000, /* the LCD reports the round count this often */
    LOOP_MS = 5,
    ROUNDS = 4000,
    BEAT_HIGH = 90, /* the blue line while the LED is on */
    BEAT_LOW = 5,   /* the blue line while the LED is off */
    LOAD_MAX = 100,
    PATTERN_STEPS = 4,
    LED_FALLBACK = 0,
    HEARTBEAT_NAMES = 2,
    TEXT_CHARS = 128
};

/* Colours, 0xRRGGBB; 0 leaves the widget's own colour. */
enum {
    COLOR_DEFAULT = 0,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_CARD = 0x171B22,
    COLOR_LOAD = 0xFFA040,
    COLOR_ON = 0x33DD77,
    COLOR_OFF = 0x8899AA
};

/* Layout on the 792 x 398 canvas: the chart and the status card on the
 * left, the legend, the counter and the pattern card on the right. */
enum {
    LEFT_X = 12,
    RIGHT_X = 496,
    CARD_TEXT_X = 512,
    STATUS_TEXT_X = 24,
    TITLE_Y = 8,
    CHART_Y = 44,
    CHART_W = 472,
    CHART_H = 212,
    CHART_MAX = 110,
    LEGEND_BEAT_Y = 40,
    LEGEND_LOAD_Y = 68,
    COUNTER_LABEL_Y = 102,
    SEG_Y = 132,
    SEG_W = 200,
    SEG_H = 40,
    STEP_CARD_Y = 180,
    STEP_CARD_W = 284,
    STEP_CARD_H = 100,
    STEP_Y = 188,
    DURATION_Y = 216,
    CYCLE_Y = 244,
    STATUS_CARD_Y = 292,
    STATUS_CARD_H = 44,
    STATUS_Y = 304,
    CARD_BORDER = 2,
    TITLE_PX = 24,
    TEXT_PX = 20
};

/* One step of the heartbeat: the LED on or off, for so many ms. */
typedef struct {
    bool on;
    int32_t duration_ms;
} beat_t;

/* Change this table and the heartbeat changes at once. */
static const beat_t PATTERN[PATTERN_STEPS] = {
    {true, 80}, {false, 120}, {true, 80}, {false, 900}};

/* Where a widget sits and how big it is (-1: the widget's default). */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} place_t;

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

/* The LED, the widgets the loop changes, and the loop's clocks. */
typedef struct {
    bento_ui_t *ui;
    uint32_t led;
    bento_ui_id_t chart;
    uint8_t series_load;
    bento_ui_id_t seg;
    bento_ui_id_t step_label;
    bento_ui_id_t duration_label;
    bento_ui_id_t cycle_label;
    bento_ui_id_t status_label;
    int32_t step;
    int32_t work;
    int32_t sample_work;
    uint32_t step_start_ms;
    uint32_t sample_at_ms;
    uint32_t report_at_ms;
} heartbeat_t;

/* time.sleep_ms(): BENTO_E_CANCELLED when Stop arrives, as the
 * original's KeyboardInterrupt. */
static bento_status_t pause_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The index of the LED with this name, as table.index(name). */
static bool led_index(const char *name, uint32_t *index)
{
    const uint32_t led_count = bento_gpio_num_leds();
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < led_count) && !found; i++) {
        const char *led_name = "";

        if ((bento_gpio_led_name(i, &led_name) == BENTO_OK) &&
            (strcmp(led_name, name) == 0)) {
            found = true;
            *index = i;
        }
    }
    return found;
}

/* The LED whose name comes first in names, by its place in the board's
 * table: the index differs from board to board, the name does not. */
static bento_status_t led_named(const char *const *names,
                                uint32_t name_count, uint32_t *led)
{
    bento_gpio_board_info_t info;
    bool found = false;
    uint32_t name;
    bento_status_t status;

    bento_gpio_board_info_init(&info);
    status = bento_gpio_board_info(&info);
    for (name = 0U;
         (name < name_count) && !found && (status == BENTO_OK);
         name++) {
        found = led_index(names[name], led);
    }
    if ((status == BENTO_OK) && !found) {
        *led = LED_FALLBACK;
        /* gpio.led() raises ValueError for an LED the board lacks */
        status = (*led < bento_gpio_num_leds())
                     ? BENTO_OK
                     : BENTO_E_INVALID_ARGUMENT;
    }
    return status;
}

/* The heartbeat is blue on both boards: RGB_BLUE on the Dev Kit, and
 * on the Eva Kit the LED named RGB_RED is the blue one. */
static bento_status_t find_heartbeat_led(uint32_t *led)
{
    static const char *const names[HEARTBEAT_NAMES] = {"RGB_BLUE",
                                                       "RGB_RED"};

    return led_named(names, HEARTBEAT_NAMES, led);
}

static int32_t cycle_ms(void)
{
    int32_t total = 0;
    int32_t i;

    for (i = 0; i < PATTERN_STEPS; i++) {
        total += PATTERN[i].duration_ms;
    }
    return total;
}

static void place_args(bento_ui_widget_args_t *args, place_t place)
{
    bento_ui_widget_args_init(args);
    args->x = place.x;
    args->y = place.y;
    args->w = place.w;
    args->h = place.h;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    place_args(&args, (place_t){look.x, look.y, -1, -1});
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A dark card with a thin border of the same colour. */
static bento_status_t add_card(bento_ui_t *ui, place_t place)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    place_args(&args, place);
    args.color = COLOR_CARD;
    args.min_val = COLOR_CARD;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &unused);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

/* A ui.poll() whose events the original throws away. */
static void poll_drop(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL); /* the events are not read, as before */
}

/* The title, the chart with its orange second series, and the legend. */
static bento_status_t add_chart(heartbeat_t *beat)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        beat->ui, "ไฟหัวใจเต้น พิสูจน์ว่าลูปยังหมุน",
        (label_look_t){LEFT_X, TITLE_Y, COLOR_DEFAULT, TITLE_PX},
        &unused);

    if (status == BENTO_OK) {
        place_args(&args, (place_t){LEFT_X, CHART_Y, CHART_W, CHART_H});
        args.color = COLOR_ACCENT;
        args.min_val = 0;
        args.max_val = CHART_MAX;
        status = bento_ui_chart_create(beat->ui, &args, &beat->chart);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            beat->ui, beat->chart, COLOR_LOAD, &beat->series_load);
    }
    if (status == BENTO_OK) {
        status = add_label(beat->ui, "เส้นฟ้า = ระดับไฟหัวใจ",
                           (label_look_t){RIGHT_X, LEGEND_BEAT_Y,
                                          COLOR_ACCENT, TEXT_PX},
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            beat->ui, "เส้นส้ม = รอบลูปต่อ 60 ms",
            (label_look_t){RIGHT_X, LEGEND_LOAD_Y, COLOR_WARN, TEXT_PX},
            &unused);
    }
    return status;
}

/* The round counter; its text is set to "0" at once, or the Seg7
 * would show its own 0000. */
static bento_status_t add_counter(heartbeat_t *beat)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status =
        add_label(beat->ui, "รอบลูปสะสม",
                  (label_look_t){RIGHT_X, COUNTER_LABEL_Y,
                                 COLOR_DEFAULT, TEXT_PX},
                  &unused);

    if (status == BENTO_OK) {
        place_args(&args, (place_t){RIGHT_X, SEG_Y, SEG_W, SEG_H});
        status = bento_ui_seg7_create(beat->ui, &args, &beat->seg);
    }
    if (status == BENTO_OK) {
        set_text(beat->ui, beat->seg, "0");
    }
    return status;
}

/* The card with the pattern's step, its duration and the cycle. */
static bento_status_t add_step_card(heartbeat_t *beat)
{
    bento_status_t status =
        add_card(beat->ui, (place_t){RIGHT_X, STEP_CARD_Y, STEP_CARD_W,
                                     STEP_CARD_H});

    if (status == BENTO_OK) {
        status = add_label(
            beat->ui, "ช่วงที่ 1/4",
            (label_look_t){CARD_TEXT_X, STEP_Y, COLOR_OK, TEXT_PX},
            &beat->step_label);
    }
    if (status == BENTO_OK) {
        status = add_label(beat->ui, "ติด 80 ms",
                           (label_look_t){CARD_TEXT_X, DURATION_Y,
                                          COLOR_DEFAULT, TEXT_PX},
                           &beat->duration_label);
    }
    if (status == BENTO_OK) {
        status = add_label(beat->ui, "รอบละ 1180 ms",
                           (label_look_t){CARD_TEXT_X, CYCLE_Y,
                                          COLOR_DEFAULT, TEXT_PX},
                           &beat->cycle_label);
    }
    return status;
}

static bento_status_t build_screen(heartbeat_t *beat)
{
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(beat->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = add_chart(beat);
    if (status == BENTO_OK) {
        status = add_counter(beat);
    }
    if (status == BENTO_OK) {
        status = add_step_card(beat);
    }
    if (status == BENTO_OK) {
        status = add_card(beat->ui, (place_t){LEFT_X, STATUS_CARD_Y,
                                              CHART_W, STATUS_CARD_H});
    }
    if (status == BENTO_OK) {
        status = add_label(beat->ui, "ลูปยังหมุน - ไฟไม่ได้ยึดลูปไว้",
                           (label_look_t){STATUS_TEXT_X, STATUS_Y,
                                          COLOR_DEFAULT, TEXT_PX},
                           &beat->status_label);
    }
    if (status == BENTO_OK) {
        poll_drop(beat->ui);
    }
    return status;
}

/* The LED and the card move to the next step once the step's time is
 * up: the clock decides, nothing blocks. */
static bento_status_t advance_beat(heartbeat_t *beat, uint32_t now_ms)
{
    char text[TEXT_CHARS];
    bento_status_t status = BENTO_OK;

    if (bento_time_ticks_diff(now_ms, beat->step_start_ms) >=
        PATTERN[beat->step].duration_ms) {
        const beat_t *next;

        beat->step = (beat->step + 1) % PATTERN_STEPS;
        beat->step_start_ms = now_ms;
        next = &PATTERN[beat->step];
        status = next->on ? bento_gpio_led_on(beat->led)
                          : bento_gpio_led_off(beat->led);
        if (status == BENTO_OK) {
            (void)snprintf(text, sizeof(text), "ช่วงที่ %" PRId32 "/%d",
                           beat->step + 1, PATTERN_STEPS);
            set_text(beat->ui, beat->step_label, text);
            (void)bento_ui_set_color(beat->ui, beat->step_label,
                                     next->on ? COLOR_ON : COLOR_OFF);
            (void)snprintf(text, sizeof(text), "%s%" PRId32 " ms",
                           next->on ? "ติด " : "ดับ ", next->duration_ms);
            set_text(beat->ui, beat->duration_label, text);
        }
    }
    return status;
}

/* Every SAMPLE_MS: the LED's square wave and the loop's load in the
 * same window, and the round count on the Seg7. */
static void draw_sample(heartbeat_t *beat, uint32_t now_ms)
{
    char text[TEXT_CHARS];

    if (bento_time_ticks_diff(now_ms, beat->sample_at_ms) >=
        SAMPLE_MS) {
        const int32_t load = beat->sample_work * LOOP_GAIN;

        (void)bento_ui_chart_set_next(
            beat->ui, beat->chart, 0U,
            PATTERN[beat->step].on ? BEAT_HIGH : BEAT_LOW);
        (void)bento_ui_chart_set_next(
            beat->ui, beat->chart, beat->series_load,
            (load < LOAD_MAX) ? load : LOAD_MAX);
        beat->sample_work = 0;
        beat->sample_at_ms = now_ms;
        (void)snprintf(text, sizeof(text), "%" PRId32, beat->work);
        set_text(beat->ui, beat->seg, text);
    }
}

/* Every two seconds, proof of how many rounds the loop has run. */
static void report_rounds(heartbeat_t *beat, uint32_t now_ms)
{
    char text[TEXT_CHARS];

    if (bento_time_ticks_diff(now_ms, beat->report_at_ms) >=
        REPORT_MS) {
        (void)snprintf(text, sizeof(text), "ลูปหมุนไปแล้ว %" PRId32 " รอบ",
                       beat->work);
        (void)bento_lcd_print(text); /* modlcd.c ignores a refusal */
        (void)snprintf(text, sizeof(text), "ลูปหมุนแล้ว %" PRId32 " รอบ",
                       beat->work);
        set_text(beat->ui, beat->status_label, text);
        beat->report_at_ms = now_ms;
    }
}

static bento_status_t run_rounds(heartbeat_t *beat)
{
    bento_status_t status = BENTO_OK;
    int32_t round;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        const uint32_t now_ms = bento_time_ticks_ms();

        status = advance_beat(beat, now_ms);
        if (status == BENTO_OK) {
            beat->work++; /* the real work still runs every round */
            beat->sample_work++;
            draw_sample(beat, now_ms);
            report_rounds(beat, now_ms);
            poll_drop(beat->ui);
            status = pause_ms(LOOP_MS);
        }
    }
    return status;
}

/* The LED found and off, and the LCD's heading with the pattern. */
static bento_status_t start_heartbeat(heartbeat_t *beat)
{
    char text[TEXT_CHARS];
    bento_status_t status = find_heartbeat_led(&beat->led);

    if (status == BENTO_OK) {
        status = bento_gpio_led_off(beat->led);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_clear(); /* lcd calls ignore a refusal */
        (void)bento_lcd_console("<h2>ไฟหัวใจเต้น</h2>");
        (void)snprintf(text, sizeof(text),
                       "จังหวะ %d ช่วง | รอบละ %" PRId32 " ms",
                       PATTERN_STEPS, cycle_ms());
        (void)bento_lcd_print(text);
    }
    return status;
}

/* The clocks start, and the cycle label shows the pattern's length. */
static void start_clocks(heartbeat_t *beat)
{
    char text[TEXT_CHARS];

    beat->step = 0;
    beat->step_start_ms = bento_time_ticks_ms();
    beat->work = 0;
    beat->report_at_ms = bento_time_ticks_ms();
    beat->sample_at_ms = bento_time_ticks_ms();
    beat->sample_work = 0;
    (void)snprintf(text, sizeof(text), "รอบละ %" PRId32 " ms",
                   cycle_ms());
    set_text(beat->ui, beat->cycle_label, text);
}

int main(void)
{
    heartbeat_t beat;
    bento_status_t status;

    (void)memset(&beat, 0, sizeof(beat));
    beat.ui = bento_runtime_get_ui();
    status = start_heartbeat(&beat);
    if (status == BENTO_OK) {
        status = build_screen(&beat);
    }
    if (status == BENTO_OK) {
        start_clocks(&beat);
        status = run_rounds(&beat);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(beat.led);
    }
    if (status == BENTO_OK) {
        set_text(beat.ui, beat.status_label,
                 "หยุดไฟหัวใจแล้ว - ถ้าไฟค้างแปลว่าลูปตาย");
        (void)bento_lcd_print("<span class=ok>หยุดไฟหัวใจแล้ว</span>");
        (void)printf("หยุดไฟหัวใจแล้ว ถ้าเห็นไฟค้างแปลว่าลูปตายคาที่\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
