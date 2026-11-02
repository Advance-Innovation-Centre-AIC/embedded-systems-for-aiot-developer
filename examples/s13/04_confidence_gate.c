/*
 * 04_confidence_gate.c -- อย่าเชื่อผล AI ทีละผล: กรองด้วยความมั่นใจ + "k ใน n"
 * (do not trust one AI result: a confidence floor and k of n)
 *
 * กรองผลดิบสองชั้นก่อนเตือน: ความมั่นใจต่ำกว่า 60% ไม่นับ และคลาสอันตราย (anomaly
 * หรือ shaking) ต้องชนะ 3 ครั้งใน 5 ผลล่าสุดจึงเตือน หายเตือนเมื่อใน 5 ผลล่าสุด
 * ไม่มีคลาสอันตรายเลย แถวบนคือผลดิบ วงกลมคือสถานะหลังกรอง (เขียวปกติ แดงเตือน)
 * ไฟ 5 ดวงคือ 5 ผลล่าสุด (ติด = อันตราย) ตัวนับเทียบจำนวนครั้งที่ป้ายดิบเปลี่ยนกับ
 * จำนวนครั้งที่เตือนจริง รัน 120 วินาที
 *
 * Two filters on the raw results before an alarm: a result under 60 %
 * confidence does not count, and a danger class (anomaly or shaking)
 * must win 3 of the last 5 results to raise the alarm. The alarm clears
 * when none of the last 5 is a danger. The top row is the raw result,
 * the disc the filtered state (green OK, red ALARM), the five lamps the
 * last five results (lit = danger), and the counter compares raw label
 * changes with real alarms. It runs for 120 seconds.
 *
 * Twin of 04_confidence_gate.py
 * (embedded-systems-for-aiot-developer/examples/s13/04_confidence_gate.py).
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - edge_ai.models() is bento_edgeai_count() and model_info() per
 *    row; a row's index is its place in the table;
 *  - result() is None when the board has no verdict (has_verdict
 *    false) or the read fails; conf is score_pct[top] in percent, so
 *    int(conf * 100) is that number; the label is bento_edgeai_label();
 *  - the history list is a ring of WINDOW_N flags with a count;
 *  - select() and stop() wait inside the binding; here they are
 *    started and polled; an OSError from select() ends the program
 *    after the finally, here its status is returned;
 *  - import edge_ai fails where there is no model link: here
 *    bento_edgeai_supported() false ends with BENTO_E_UNSUPPORTED;
 *  - Stop raises KeyboardInterrupt in MicroPython; here the wait
 *    returns and the program ends after the same finally.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_edgeai.h"
#include "bento_runtime.h"

/* The rule and the timing. */
enum {
    CONF_MIN = 60, /* a result less sure than this does not count */
    CONFIRM_N = 3, /* wins needed ... */
    WINDOW_N = 5,  /* ... among this many latest results */
    RUN_MS = 120000,
    DRAW_MS = 500,
    LOOP_MS = 100,
    SETTLE_MS = 150, /* let the cleared screen settle */
    OP_POLL_MS = 100,
    MODEL_KEY_COUNT = 2,
    DANGER_COUNT = 2,
    TEXT_CHARS = 160
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_OK = 0x30A46C,
    COLOR_BAD = 0xE5484D
};

/* Layout on the 792 x 398 canvas. */
enum {
    LEFT_X = 12,
    TITLE_Y = 6,
    RAW_Y = 44,
    DISC_Y = 88,
    DISC_SIZE = 96,
    DISC_RADIUS = 48,
    STATE_X = 128,
    STATE_Y = 116,
    DOTS_CAPTION_Y = 200,
    DOT_Y = 226,
    DOT_STEP = 40,
    DOT_SIZE = 28,
    COUNT_Y = 276,
    RULE_Y = 310,
    SMALL_PX = 16,
    TEXT_PX = 20,
    BIG_PX = 28
};

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

/* The model, the widgets and the rule's memory. */
typedef struct {
    bento_ui_t *ui;
    uint32_t model;
    char name[BENTO_EDGEAI_NAME_LEN];
    bento_ui_id_t raw_label;
    bento_ui_id_t disc;
    bento_ui_id_t state_label;
    bento_ui_id_t dots[WINDOW_N];
    bento_ui_id_t count_label;
    int32_t hist[WINDOW_N]; /* oldest first, 1 = danger */
    int32_t hist_count;
    int32_t lit[WINDOW_N];
    bool alerting;
    int32_t flips;
    int32_t alarms;
    bool has_sequence;
    uint32_t last_sequence;
    bool has_label;
    char last_label[BENTO_EDGEAI_LABEL_LEN];
    bool has_raw;
    char raw[TEXT_CHARS];
} gate_t;

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

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text_color(bento_ui_t *ui, bento_ui_id_t widget,
                           const char *text, uint32_t color)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
    (void)bento_ui_set_color(ui, widget, color);
}

/* A ui.poll() whose events the original throws away. */
static void poll_drop(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL); /* the events are not read, as before */
}

/* The first model whose name matches, in the order of the names. */
static bento_status_t find_model(gate_t *gate, bool *found)
{
    static const char *const keys[MODEL_KEY_COUNT] = {
        "AnomalousVibration", "Motion Detection"};
    bento_edgeai_model_info_t info;
    int32_t count = 0;
    uint32_t key;
    int32_t row;
    bento_status_t status = bento_edgeai_count(&count);

    *found = false;
    for (key = 0U; (key < (uint32_t)MODEL_KEY_COUNT) && !*found &&
                   (status == BENTO_OK);
         key++) {
        for (row = 0; (row < count) && !*found && (status == BENTO_OK);
             row++) {
            bento_edgeai_model_info_init(&info);
            status = bento_edgeai_model_info((uint32_t)row, &info);
            *found = (status == BENTO_OK) &&
                     (strcmp(info.name, keys[key]) == 0);
            gate->model = (uint32_t)row;
        }
    }
    if (*found) {
        (void)snprintf(gate->name, sizeof(gate->name), "%s", info.name);
    }
    return status;
}

/* Waits for a started select; Stop cancels it. */
static bento_status_t finish_op(void)
{
    bento_status_t status = BENTO_E_IN_PROGRESS;

    while (status == BENTO_E_IN_PROGRESS) {
        status = pause_ms(OP_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_edgeai_op_poll(NULL);
        } else {
            (void)bento_edgeai_op_cancel(); /* Stop wins */
        }
    }
    return status;
}

/* The finally: the AI core is always released, and its OSError is
 * passed over. It waits even after Stop, as the binding does. */
static void stop_model(void)
{
    bento_status_t status = bento_edgeai_stop_start();

    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        /* the IDE's Stop may already be in: the wait is not checked */
        (void)bento_time_sleep_ms(OP_POLL_MS);
        status = bento_edgeai_op_poll(NULL);
    }
}

/* The status disc and its word. */
static bento_status_t add_disc(gate_t *gate)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    place_args(&args, (place_t){LEFT_X, DISC_Y, DISC_SIZE, DISC_SIZE});
    args.color = COLOR_OK;
    args.min_val = COLOR_DIM;
    args.max_val = DISC_RADIUS;
    args.font_px = 0;
    status = bento_ui_panel_create(gate->ui, &args, &gate->disc);
    if (status == BENTO_OK) {
        status = add_label(
            gate->ui, "OK",
            (label_look_t){STATE_X, STATE_Y, COLOR_OK, BIG_PX},
            &gate->state_label);
    }
    return status;
}

/* WINDOW_N lamps for the latest results (lit = danger). */
static bento_status_t add_dots(gate_t *gate)
{
    bento_ui_widget_args_t args;
    char text[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    int32_t i;
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "last %d (lit = danger)",
                   WINDOW_N);
    status = add_label(
        gate->ui, text,
        (label_look_t){LEFT_X, DOTS_CAPTION_Y, COLOR_DIM, SMALL_PX},
        &unused);
    for (i = 0; (i < WINDOW_N) && (status == BENTO_OK); i++) {
        const int16_t x = (int16_t)(LEFT_X + (i * DOT_STEP));

        place_args(&args, (place_t){x, DOT_Y, DOT_SIZE, DOT_SIZE});
        args.color = COLOR_BAD;
        args.font_px = 0;
        status = bento_ui_led_create(gate->ui, &args, &gate->dots[i]);
    }
    return status;
}

static bento_status_t build_screen(gate_t *gate)
{
    char text[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(gate->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "Confidence gate: %s",
                       gate->name);
        status = add_label(
            gate->ui, text,
            (label_look_t){LEFT_X, TITLE_Y, COLOR_TEXT, TEXT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(gate->ui, "raw: -",
                      (label_look_t){LEFT_X, RAW_Y, COLOR_DIM, TEXT_PX},
                      &gate->raw_label);
    }
    if (status == BENTO_OK) {
        status = add_disc(gate);
    }
    if (status == BENTO_OK) {
        status = add_dots(gate);
    }
    if (status == BENTO_OK) {
        status = add_label(
            gate->ui, " ",
            (label_look_t){LEFT_X, COUNT_Y, COLOR_TEXT, SMALL_PX},
            &gate->count_label);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "rule: conf >= %d%% and %d of last %d", CONF_MIN,
                       CONFIRM_N, WINDOW_N);
        status = add_label(
            gate->ui, text,
            (label_look_t){LEFT_X, RULE_Y, COLOR_DIM, SMALL_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        poll_drop(gate->ui);
    }
    return status;
}

/* The latest WINDOW_N flags take the new one; the alarm rises at
 * CONFIRM_N danger results and holds while any danger is left. */
static void alert_rule(gate_t *gate, bool danger)
{
    int32_t hits = 0;
    int32_t i;

    if (gate->hist_count == WINDOW_N) {
        for (i = 1; i < WINDOW_N; i++) {
            gate->hist[i - 1] = gate->hist[i];
        }
        gate->hist_count--;
    }
    gate->hist[gate->hist_count] = danger ? 1 : 0;
    gate->hist_count++;
    for (i = 0; i < gate->hist_count; i++) {
        hits += gate->hist[i];
    }
    gate->alerting =
        (hits >= CONFIRM_N) || (gate->alerting && (hits > 0));
}

/* A class the program counts as danger (the names the board reports). */
static bool is_danger(const char *label)
{
    static const char *const danger[DANGER_COUNT] = {"anomaly",
                                                     "shaking"};
    bool found = false;
    int32_t i;

    for (i = 0; (i < DANGER_COUNT) && !found; i++) {
        found = strcmp(label, danger[i]) == 0;
    }
    return found;
}

/* The state changed: the disc and the word follow at once. */
static void show_state(gate_t *gate, const char *label, int32_t pct)
{
    const uint32_t color = gate->alerting ? COLOR_BAD : COLOR_OK;

    if (gate->alerting) {
        gate->alarms++;
        (void)printf("ALARM #%" PRId32 "  %s %" PRId32 "%%\n",
                     gate->alarms, label, pct);
    }
    (void)bento_ui_set_color(gate->ui, gate->disc, color);
    set_text_color(gate->ui, gate->state_label,
                   gate->alerting ? "ALARM" : "OK", color);
}

/* A new result of this model with a label goes through the rule. */
static void take_result(gate_t *gate)
{
    bento_edgeai_result_t result;
    char label[BENTO_EDGEAI_LABEL_LEN];

    bento_edgeai_result_init(&result);
    if ((bento_edgeai_result(&result) == BENTO_OK) &&
        result.has_verdict && (result.index == gate->model) &&
        (!gate->has_sequence || (result.seq != gate->last_sequence)) &&
        (result.top < BENTO_EDGEAI_MAX_CLASSES)) {
        const int32_t pct = (int32_t)result.score_pct[result.top];
        const bool was = gate->alerting;

        gate->has_sequence = true;
        gate->last_sequence = result.seq;
        if (bento_edgeai_label(result.index, result.top, label,
                               (uint32_t)sizeof(label),
                               NULL) != BENTO_OK) {
            label[0] = '\0';
        }
        if (!gate->has_label ||
            (strcmp(label, gate->last_label) != 0)) {
            gate->flips++; /* the raw label changed */
            gate->has_label = true;
            (void)snprintf(gate->last_label, sizeof(gate->last_label),
                           "%s", label);
        }
        alert_rule(gate, is_danger(label) && (pct >= CONF_MIN));
        (void)snprintf(gate->raw, sizeof(gate->raw),
                       "raw: %s  %" PRId32 "%%", label, pct);
        gate->has_raw = true;
        if (gate->alerting != was) {
            show_state(gate, label, pct);
        }
    }
}

/* The raw row, the lamps that changed and the counters. */
static void draw(gate_t *gate)
{
    char text[TEXT_CHARS];
    int32_t i;

    (void)bento_ui_set_text(gate->ui, gate->raw_label, gate->raw,
                            (uint32_t)strlen(gate->raw));
    for (i = 0; i < WINDOW_N; i++) {
        const int32_t value =
            (i < gate->hist_count) ? gate->hist[i] : 0;

        if (value != gate->lit[i]) {
            (void)bento_ui_set_value(gate->ui, gate->dots[i], value);
            gate->lit[i] = value;
        }
    }
    (void)snprintf(text, sizeof(text),
                   "raw label changes %" PRId32 " | alarms %" PRId32,
                   gate->flips, gate->alarms);
    (void)bento_ui_set_text(gate->ui, gate->count_label, text,
                            (uint32_t)strlen(text));
}

/* RUN_MS from before the select, a pass every LOOP_MS. */
static bento_status_t run_gate(gate_t *gate, uint32_t start_ms)
{
    uint32_t drawn_ms = start_ms;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        uint32_t now_ms;

        take_result(gate);
        now_ms = bento_time_ticks_ms();
        if (gate->has_raw &&
            (bento_time_ticks_diff(now_ms, drawn_ms) >= DRAW_MS)) {
            drawn_ms = now_ms;
            draw(gate);
        }
        poll_drop(gate->ui);
        status = pause_ms(LOOP_MS);
    }
    return status;
}

/* The model, run under the finally that stops it. */
static bento_status_t run_model(gate_t *gate)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = bento_edgeai_select_start(gate->model);

    if (status == BENTO_OK) {
        status = finish_op();
    }
    if (status == BENTO_OK) {
        status = run_gate(gate, start_ms);
    }
    stop_model();
    return status;
}

int main(void)
{
    gate_t gate;
    bool supported = false;
    bool found = false;
    bento_status_t status;

    (void)memset(&gate, 0, sizeof(gate));
    gate.ui = bento_runtime_get_ui();
    status = bento_edgeai_supported(&supported);
    if ((status == BENTO_OK) && !supported) {
        status = BENTO_E_UNSUPPORTED; /* import edge_ai fails */
    }
    if (status == BENTO_OK) {
        status = find_model(&gate, &found);
    }
    if ((status == BENTO_OK) && !found) {
        (void)printf("none of these models is on the board: "
                     "('AnomalousVibration', 'Motion Detection')\n");
    } else if (status == BENTO_OK) {
        status = build_screen(&gate);
        if (status == BENTO_OK) {
            status = run_model(&gate);
        }
        if (status == BENTO_OK) {
            (void)printf("label changes %" PRId32 " | alarms %" PRId32
                         "\n",
                         gate.flips, gate.alarms);
        }
    } else {
        /* an OSError from edge_ai.models() ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
