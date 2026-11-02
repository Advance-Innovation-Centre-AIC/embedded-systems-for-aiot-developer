/*
 * 06_sense_decide_act_report.c -- วงจรเต็มสี่ขั้นในไฟล์เดียว
 *
 * โครงของผลิตภัณฑ์ AIoT ทุกตัวคือสี่ขั้นวนไปเรื่อย ๆ: วัด -> ตัดสิน -> สั่งของจริง ->
 * รายงานออกไป แถวสถานะสี่ช่องบนจอสว่างทีละช่องตามขั้นที่กำลังทำ ไฟ LED สีเขียว
 * บนบอร์ด ("พัดลม") ติดจริงเมื่ออุณหภูมิข้ามเกณฑ์ โดยมีช่วงหน่วง (hysteresis)
 * เปิดเมื่อเกิน 27.5 ปิดเมื่อต่ำกว่า 26.5 และพิมพ์รายงาน JSON ทุก 3 วินาที
 * อุณหภูมิมาจาก SHT40 ถ้าบอร์ดมี ไม่งั้นใช้ลูกบิด 0-100 % แทน 15-45 C
 *
 * Every AIoT product is these four steps in a loop: sense, decide, act,
 * report. Four status labels light one at a time with the step being done;
 * the board's green LED (the "fan") really lights when the temperature
 * crosses the threshold, with hysteresis (on above 27.5, off below 26.5),
 * and a JSON report is printed every 3 seconds. The temperature comes from
 * the SHT40 when the board has one, else the knob stands in (0-100 % =
 * 15-45 C). It runs 160 rounds.
 *
 * Twin of 06_sense_decide_act_report.py
 * (embedded-systems-for-aiot-developer/examples/s12/06_sense_decide_act_report.py),
 * written new (the library twin aiot-s12-06-sense-decide-act-report reads a
 * different original). Style: CLEAN_C.md.
 * What C changes:
 *  - led_named() reads the LED names one by one (bento_gpio_led_name) and
 *    gives an index; gpio.led(i) is that index, and a refused LED call ends
 *    the program with its status, as the OSError does;
 *  - hasattr(sensors, "sht40") plus try/except OSError is one status test of
 *    bento_sensors_sht40_temperature(); the global _TEMP_SRC is a member of
 *    the loop state; a failing sensors.snapshot() ends the program;
 *  - round(temp, 1) is nearbyint(temp * 10) / 10 (ties to even), written
 *    out without the maths library; "{:.1f}" and str(float) go through
 *    bento_fmt_real_precision / bento_fmt_real, which print what the
 *    board's MicroPython prints;
 *  - json.dumps of the three-key dict is printf in MicroPython's slot
 *    order: temp_c, device, fan with the PSoC Edge's 1-byte qstr hash;
 *    fan, device, temp_c with a 2-byte hash (the other targets);
 *  - like the original, nothing switches the fan off when Stop arrives:
 *    the .py has no finally, so the LED keeps its last state.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_time.h"

/* MicroPython rounds a*b and +c separately: the compiler must not fuse
 * them into one multiply-add, or the knob temperature drifts. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

enum { /* times and counts */
    SETTLE_MS = 200,
    LOOP_MS = 200,
    REPORT_MS = 3000,
    ROUNDS = 160,
    STEP_COUNT = 4,
    STEP_SENSE = 0,
    STEP_DECIDE = 1,
    STEP_ACT = 2,
    STEP_REPORT = 3,
    TEMP_SERIES = 0, /* the chart's own first line */
    TENTHS = 10,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};
enum { /* layout */
    MARGIN_X = 20,
    TITLE_Y = 12,
    TITLE_PX = 24,
    TEXT_PX = 20,
    STATE_PX = 28,
    CARD_EDGE_PX = 12,
    CARD_BORDER = 1,
    STEPS_Y = 52,
    STEPS_W = 652,
    STEPS_H = 64,
    STEP_X = 40,
    STEP_STEP_X = 160,
    STEP_Y = 72,
    CARD_Y = 128,
    CARD_H = 96,
    TEMP_CARD_W = 332,
    TEMP_X = 36,
    FAN_CARD_X = 368,
    FAN_CARD_W = 304,
    FAN_X = 384,
    CARD_HEAD_Y = 140,
    CARD_VALUE_Y = 168,
    SEG_W = 200,
    SEG_H = 48,
    CHART_Y = 240,
    CHART_W = 548,
    CHART_H = 120,
    CHART_MIN = 20,
    CHART_MAX = 35,
    LEGEND_X = 580,
    LEGEND_ON_Y = 248,
    LEGEND_OFF_Y = 272,
    SENT_Y = 364
};
enum { /* the course palette, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ON = 0x30A46C,
    COLOR_OFF = 0x9AA3AF,
    COLOR_WARN = 0xF5A623,
    COLOR_SEG = 0x4A9EFF
};

static const char DEVICE_ID[] = "team03";
static const bento_real_t ON_ABOVE =
    (bento_real_t)27.5; /* cross above: switch on */
static const bento_real_t OFF_BELOW =
    (bento_real_t)26.5; /* fall below: switch off; 1 C gap */

/* Where the temperature came from last; a new source is announced once. */
typedef enum {
    SOURCE_NONE = 0,
    SOURCE_SHT40,
    SOURCE_KNOB
} temp_source_t;

/* The widgets the program changes, and the fan LED. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t steps[STEP_COUNT];
    bento_ui_id_t digits;
    bento_ui_id_t fan_text;
    bento_ui_id_t chart;
    uint8_t on_line;
    uint8_t off_line;
    bento_ui_id_t sent;
    uint32_t fan;
} report_screen_t;

/* What the loop keeps between rounds. */
typedef struct {
    bool on;
    int32_t reports;
    uint32_t last_report_ms;
    temp_source_t source;
} loop_state_t;

static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* round(x, 1): nearbyint(x * 10) / 10, a tie going to the even tenth. */
static bento_real_t round_tenth(bento_real_t value)
{
    const bento_real_t half = (bento_real_t)0.5;
    const bento_real_t scaled = value * (bento_real_t)TENTHS;
    int32_t whole = (int32_t)scaled;
    const bento_real_t rest = scaled - (bento_real_t)whole;
    const bool odd = (whole % 2) != 0;

    if ((rest > half) || ((rest == half) && odd)) {
        whole++;
    } else if ((rest < -half) || ((rest == -half) && odd)) {
        whole--;
    } else {
        /* already the nearest tenth */
    }
    return (bento_real_t)whole / (bento_real_t)TENTHS;
}

/* str(round(value, 1)) */
static void format_tenth(char *text, uint32_t cap, bento_real_t value)
{
    uint32_t length = 0U;

    if (bento_fmt_real(text, cap, round_tenth(value), &length) !=
        BENTO_OK) {
        text[0] = '\0'; /* BENTO_FMT_BUF_SIZE is always enough */
    }
}

/* The index of the LED called wanted, if the board has one. */
static bento_status_t find_led(const char *wanted, uint32_t leds,
                               uint32_t *led, bool *found)
{
    uint32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < leds) && !*found && (status == BENTO_OK); i++) {
        const char *name = NULL;

        status = bento_gpio_led_name(i, &name);
        if ((status == BENTO_OK) && (name != NULL) &&
            (strcmp(name, wanted) == 0)) {
            *led = i;
            *found = true;
        }
    }
    return status;
}

/* The LED by its name in the firmware's table: the index differs between
 * boards, the name does not. The first name is the Dev Kit's. */
static bento_status_t led_named(const char *first, const char *second,
                                uint32_t *led)
{
    bool found = false;
    bento_gpio_board_info_t info;
    bento_status_t status;

    bento_gpio_board_info_init(&info);
    status = bento_gpio_board_info(&info);
    *led = 0U; /* fallback=0 */
    if (status == BENTO_OK) {
        status = find_led(first, info.leds, led, &found);
    }
    if ((status == BENTO_OK) && !found) {
        status = find_led(second, info.leds, led, &found);
    }
    return status;
}

static bento_status_t add_label(const char *text, int32_t x, int32_t y,
                                uint32_t color, int32_t font_px,
                                bento_ui_id_t *id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = (int16_t)x;
    args.y = (int16_t)y;
    args.color = color;
    args.font_px = font_px;
    /* the runtime's ui core, so the helper keeps to six parameters */
    return bento_ui_label_create(bento_runtime_get_ui(), &args, id);
}

static bento_status_t add_card(bento_ui_t *ui, int32_t x, int32_t y,
                               int32_t w, int32_t h)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t id;

    bento_ui_widget_args_init(&args);
    args.x = (int16_t)x;
    args.y = (int16_t)y;
    args.w = (int16_t)w;
    args.h = (int16_t)h;
    args.color = (uint32_t)COLOR_CARD;
    args.min_val = COLOR_DIM;
    args.max_val = CARD_EDGE_PX;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &id);
}

/* The row of four steps: the lit label is the step this round is in. */
static bento_status_t add_steps(report_screen_t *screen)
{
    static const char *const names[STEP_COUNT] = {"1 วัด", "2 ตัดสิน",
                                                  "3 สั่ง", "4 รายงาน"};
    int32_t i;
    bento_status_t status =
        add_card(screen->ui, MARGIN_X, STEPS_Y, STEPS_W, STEPS_H);

    for (i = 0; (i < STEP_COUNT) && (status == BENTO_OK); i++) {
        status =
            add_label(names[i], STEP_X + (i * STEP_STEP_X), STEP_Y,
                      (uint32_t)COLOR_DIM, TEXT_PX, &screen->steps[i]);
    }
    return status;
}

static bento_status_t add_seg7(report_screen_t *screen)
{
    static const char dashes[] = "--";
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = dashes;
    args.text_len = (uint32_t)strlen(dashes);
    args.x = (int16_t)TEMP_X;
    args.y = (int16_t)CARD_VALUE_Y;
    args.w = (int16_t)SEG_W;
    args.h = (int16_t)SEG_H;
    args.color = (uint32_t)COLOR_SEG;
    return bento_ui_seg7_create(screen->ui, &args, &screen->digits);
}

/* The temperature card and the fan card side by side. */
static bento_status_t add_cards(report_screen_t *screen)
{
    bento_ui_id_t id;
    bento_status_t status =
        add_card(screen->ui, MARGIN_X, CARD_Y, TEMP_CARD_W, CARD_H);

    if (status == BENTO_OK) {
        status = add_label("อุณหภูมิ", TEMP_X, CARD_HEAD_Y,
                           (uint32_t)COLOR_DIM, TEXT_PX, &id);
    }
    if (status == BENTO_OK) {
        status = add_seg7(screen);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui, FAN_CARD_X, CARD_Y, FAN_CARD_W,
                          CARD_H);
    }
    if (status == BENTO_OK) {
        status = add_label("สถานะพัดลม", FAN_X, CARD_HEAD_Y,
                           (uint32_t)COLOR_DIM, TEXT_PX, &id);
    }
    if (status == BENTO_OK) {
        status =
            add_label("ปิด", FAN_X, CARD_VALUE_Y, (uint32_t)COLOR_OFF,
                      STATE_PX, &screen->fan_text);
    }
    return status;
}

/* The chart: its own first line is the temperature, then the two
 * thresholds, each named by a label in its colour. */
static bento_status_t add_chart(report_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t id;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = (int16_t)MARGIN_X;
    args.y = (int16_t)CHART_Y;
    args.w = (int16_t)CHART_W;
    args.h = (int16_t)CHART_H;
    args.min_val = CHART_MIN;
    args.max_val = CHART_MAX;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status = add_label("เส้นบน = เกณฑ์เปิด", LEGEND_X, LEGEND_ON_Y,
                           (uint32_t)COLOR_WARN, TEXT_PX, &id);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           (uint32_t)COLOR_WARN,
                                           &screen->on_line);
    }
    if (status == BENTO_OK) {
        status = add_label("เส้นล่าง = เกณฑ์ปิด", LEGEND_X, LEGEND_OFF_Y,
                           (uint32_t)COLOR_DIM, TEXT_PX, &id);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           (uint32_t)COLOR_DIM,
                                           &screen->off_line);
    }
    return status;
}

static bento_status_t build_screen(report_screen_t *screen)
{
    bento_ui_id_t id;
    bento_status_t status =
        add_label("วัด -> ตัดสิน -> สั่ง -> รายงาน", MARGIN_X, TITLE_Y,
                  (uint32_t)COLOR_TEXT, TITLE_PX, &id);

    if (status == BENTO_OK) {
        status = add_steps(screen);
    }
    if (status == BENTO_OK) {
        status = add_cards(screen);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_label("ยังไม่ได้รายงาน", MARGIN_X, SENT_Y,
                           (uint32_t)COLOR_DIM, TEXT_PX, &screen->sent);
    }
    return status;
}

static void set_text(const report_screen_t *screen, bento_ui_id_t label,
                     const char *text)
{
    /* MicroPython's widget setters ignore a refusal (modui.c) */
    (void)bento_ui_set_text(screen->ui, label, text,
                            (uint32_t)strlen(text));
}

static void drop_events(const report_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* the poll's events are thrown away, as in the original */
    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
}

/* Shows by eye which step this is: only one is lit. */
static void light(const report_screen_t *screen, int32_t step)
{
    int32_t i;

    for (i = 0; i < STEP_COUNT; i++) {
        (void)bento_ui_set_color(screen->ui, screen->steps[i],
                                 (i == step) ? (uint32_t)COLOR_ON
                                             : (uint32_t)COLOR_DIM);
    }
}

/* The SHT40 when the board has one, else the knob (0-100 % = 15-45 C, so
 * a class can turn it across the threshold); false when neither answered. */
static bool read_temp(const bento_sensors_snapshot_t *snap,
                      temp_source_t *source, bento_real_t *temp_c)
{
    const bento_real_t knob_base_c = (bento_real_t)15.0;
    const bento_real_t c_per_percent = (bento_real_t)0.3;
    float sht40_c = 0.0F;
    bool found = true;

    if (bento_sensors_sht40_temperature(&sht40_c) == BENTO_OK) {
        if (*source != SOURCE_SHT40) {
            *source = SOURCE_SHT40;
            (void)bento_lcd_print("อุณหภูมิจาก SHT40 (เซนเซอร์จริงบนบอร์ด)");
        }
        *temp_c = (bento_real_t)sht40_c;
    } else if (snap->has_pot != 0U) {
        const bento_real_t scaled =
            (bento_real_t)snap->pot_percent * c_per_percent;

        if (*source != SOURCE_KNOB) {
            *source = SOURCE_KNOB;
            (void)bento_lcd_print(
                "<span class=warn>ไม่มีเซนเซอร์อุณหภูมิ</span>");
            (void)bento_lcd_print("ใช้ลูกบิดแทน: 0-100 % = 15-45 C");
        }
        *temp_c = knob_base_c + scaled;
    } else {
        found = false;
    }
    return found;
}

/* Step 1 on screen: the reading in the Seg7 and on the chart with both
 * thresholds; int() truncates, as the cast does. */
static void show_reading(const report_screen_t *screen,
                         bento_real_t temp_c)
{
    char digits[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;

    if (bento_fmt_real_precision(digits, sizeof(digits), temp_c, 1,
                                 &length) != BENTO_OK) {
        digits[0] = '\0'; /* BENTO_FMT_BUF_SIZE is always enough */
    }
    set_text(screen, screen->digits, digits);
    /* set_next() sends without an answer (modui.c) */
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  (uint8_t)TEMP_SERIES,
                                  (int32_t)temp_c);
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  screen->on_line, (int32_t)ON_ABOVE);
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  screen->off_line, (int32_t)OFF_BELOW);
}

/* Step 2: two thresholds, not one. Between them nothing changes, which is
 * what stops the fan switching many times a second. */
static bool decide(bool on, bento_real_t temp_c)
{
    bool want = on;

    if (!on && (temp_c > ON_ABOVE)) {
        want = true;
    } else if (on && (temp_c < OFF_BELOW)) {
        want = false;
    } else {
        /* between the two thresholds: keep what it is */
    }
    return want;
}

/* Step 3: switch the real LED, its label and an lcd line, only on a change. */
static bento_status_t act(const report_screen_t *screen,
                          loop_state_t *state, bool want,
                          bento_real_t temp_c)
{
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    const bool changed = want != state->on;
    bento_status_t status = BENTO_OK;

    format_tenth(number, sizeof(number), temp_c);
    if (changed) {
        state->on = want;
        status = state->on ? bento_gpio_led_on(screen->fan)
                           : bento_gpio_led_off(screen->fan);
    }
    if (changed && (status == BENTO_OK)) {
        if (state->on) {
            set_text(screen, screen->fan_text, "เปิด");
            (void)bento_ui_set_color(screen->ui, screen->fan_text,
                                     (uint32_t)COLOR_ON);
            (void)snprintf(text, sizeof(text),
                           "<span class=ok>เปิดพัดลมที่ %s C</span>",
                           number);
        } else {
            set_text(screen, screen->fan_text, "ปิด");
            (void)bento_ui_set_color(screen->ui, screen->fan_text,
                                     (uint32_t)COLOR_OFF);
            (void)snprintf(text, sizeof(text), "ปิดพัดลมที่ %s C", number);
        }
        (void)bento_lcd_print(text);
    }
    return status;
}

/* json.dumps({"device": ..., "temp_c": ..., "fan": ...}) in the order
 * MicroPython's hash table walks the three keys. */
static void print_report(bool on, bento_real_t temp_c)
{
    char number[BENTO_FMT_BUF_SIZE];

    format_tenth(number, sizeof(number), temp_c);
#if defined(BENTO_TARGET_PSOC_EDGE)
    (void)printf("{\"temp_c\": %s, \"device\": \"%s\", \"fan\": %d}\n",
                 number, DEVICE_ID, on ? 1 : 0);
#else
    (void)printf("{\"fan\": %d, \"device\": \"%s\", \"temp_c\": %s}\n",
                 on ? 1 : 0, DEVICE_ID, number);
#endif
}

/* Step 4: the report goes out less often than the reading, as in session
 * 10. This file does not reach a broker, so it runs without a network;
 * a mqtt.publish(TOPIC, line) goes where the print is. */
static void report(const report_screen_t *screen, loop_state_t *state,
                   bento_real_t temp_c)
{
    char text[TEXT_BYTES];
    const uint32_t now_ms = bento_time_ticks_ms();

    if (bento_time_ticks_diff(now_ms, state->last_report_ms) >=
        REPORT_MS) {
        state->last_report_ms = now_ms;
        state->reports = state->reports + 1;
        print_report(state->on, temp_c);
        (void)snprintf(text, sizeof(text), "รายงานแล้ว %d ครั้ง",
                       (int)state->reports);
        set_text(screen, screen->sent, text);
    }
}

/* One round of the four steps; a round without a temperature warns,
 * throws one poll away and waits. */
static bento_status_t run_round(const report_screen_t *screen,
                                loop_state_t *state)
{
    bento_sensors_snapshot_t snap;
    bento_real_t temp_c = (bento_real_t)0.0;
    bento_status_t status;

    light(screen, STEP_SENSE);
    bento_sensors_snapshot_init(&snap);
    status = bento_sensors_snapshot(BENTO_SENSORS_SNAPSHOT_DEFAULT_MS,
                                    &snap);
    if ((status == BENTO_OK) &&
        !read_temp(&snap, &state->source, &temp_c)) {
        (void)bento_lcd_print("<span class=warn>ไม่มีค่าอุณหภูมิรอบนี้</span>");
    } else if (status == BENTO_OK) {
        show_reading(screen, temp_c);
        light(screen, STEP_DECIDE);
        {
            const bool want = decide(state->on, temp_c);

            light(screen, STEP_ACT);
            status = act(screen, state, want, temp_c);
        }
        if (status == BENTO_OK) {
            light(screen, STEP_REPORT);
            report(screen, state, temp_c);
        }
    } else {
        /* the snapshot's status ends the program */
    }
    if (status == BENTO_OK) {
        drop_events(screen);
        status = wait_ms(LOOP_MS);
    }
    return status;
}

static bento_status_t start_fan(report_screen_t *screen)
{
    /* the green LED both boards show, as the "on" label is green:
     * Dev Kit RGB_GREEN, Eva Kit LED2 */
    bento_status_t status =
        led_named("RGB_GREEN", "LED2", &screen->fan);

    if (status == BENTO_OK) {
        status = bento_gpio_led_off(screen->fan);
    }
    return status;
}

static void finish(const report_screen_t *screen,
                   const loop_state_t *state)
{
    char text[TEXT_BYTES];
    int32_t i;

    for (i = 0; i < STEP_COUNT; i++) {
        (void)bento_ui_set_color(screen->ui, screen->steps[i],
                                 (uint32_t)COLOR_DIM);
    }
    (void)snprintf(text, sizeof(text), "จบ - รายงานทั้งหมด %d ครั้ง",
                   (int)state->reports);
    set_text(screen, screen->sent, text);
    drop_events(screen);
    (void)bento_lcd_print(
        "<span class=ok>จบรอบ - พัดลมสั่งจากค่าที่วัดจริง</span>");
    (void)printf(
        "สี่ขั้นนี้คือโครงของงานจบ เปลี่ยนแค่ว่าวัดอะไร ตัดสินด้วยกฎอะไร สั่งอะไร\n");
}

int main(void)
{
    report_screen_t screen;
    loop_state_t state;
    int32_t round;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(&state, 0, sizeof(state));
    screen.ui = bento_runtime_get_ui();
    /* MicroPython ignores a refused ui.screen(): the runtime already set it up */
    (void)bento_ui_screen(screen.ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = wait_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    if (status == BENTO_OK) {
        drop_events(&screen);
        (void)
            bento_lcd_clear(); /* lcd.* ignores a console that is not ready */
        (void)bento_lcd_console("<h2>วงจรเต็มสี่ขั้น</h2>");
        status = start_fan(&screen);
    }
    state.last_report_ms = bento_time_ticks_ms();
    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        status = run_round(&screen, &state);
    }
    if (status == BENTO_OK) {
        status = bento_gpio_led_off(screen.fan);
    }
    if (status == BENTO_OK) {
        finish(&screen, &state);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
