/*
 * 00_ai_model_lab.c -- ห้องทดลองโมเดล AI (AI Model Lab)
 *
 * เลือกโมเดล Edge AI ตัวไหนก็ได้บนบอร์ดด้วย roller แตะ "Run" (เขียว)
 * เพื่อรัน ปุ่มกลายเป็น "Stop" (แดง) การ์ดกลางบอกคลาสเหตุการณ์ที่แรง
 * ที่สุด (ไม่ใช่คลาส 0) เทียบกับเกณฑ์ Hearing at / Confirm at ของคลาส
 * นั้น พร้อมมาตรวัด LED และตัวนับครั้งที่ยืนยัน เลือกคลาสใน dropdown
 * แล้วเลื่อน slider เพื่อปรับเกณฑ์บนบอร์ด การ์ดขวาบอกคะแนนทุกคลาส
 * ปุ่มบนบอร์ด: SW5 (ปุ่มล่าง) = โมเดลถัดไป SW6 (ปุ่มบน) = Run/Stop
 * ทุกอย่างอ่านจากบอร์ด
 * ตั้งใจไม่มีเสียง เพราะเสียงลำโพงรั่วเข้าโมเดลไมค์
 *
 * Pick any Edge AI model on the board with the roller and tap "Run"
 * (green); the button becomes "Stop" (red). The middle card shows
 * the strongest event class (not class 0) against that class's
 * Hearing at / Confirm at thresholds, with an LED meter and a count
 * of confirmations. Pick a class in the dropdown and move the
 * sliders to change its thresholds on the board. The right card
 * shows every class's score. Board buttons: SW5 (the lower one,
 * button 1) = next model, SW6 (the upper one, button 0) = Run/Stop.
 * Everything is read from the
 * board. Silent on purpose: the speaker would leak into a
 * microphone model.
 *
 * Twin of 00_ai_model_lab.py
 * (embedded-systems-for-aiot-developer/examples/s13/00_ai_model_lab.py),
 * merged into one file from the library twin devkit-21-ai-model-lab.
 * Style: CLEAN_C.md.
 * Differs from the original:
 *   - edge_ai.models() reads every row with its labels; here a
 *     table keeps each row's index, name, source, sensor and class
 *     count, at most MODELS_MAX rows (the original has no limit).
 *     The listing prints each row as print() prints the tuple, its
 *     labels read for that line; the current model's labels are
 *     read again when it is chosen ("" when one cannot be read, as
 *     the binding does).
 *   - The texts of caught OSErrors are the binding's own words for
 *     the C status: a link timeout "edge_ai: model link timeout", a
 *     dead engine "edge_ai: engine unavailable", a query the board
 *     does not answer "edge_ai: this firmware does not answer that
 *     query", any other failure to start the link "edge_ai: model
 *     link init failed"; a select "edge_ai: select send failed" or
 *     "edge_ai: select not confirmed"; a threshold "edge_ai:
 *     threshold send failed".
 *   - edge_ai.select(), stop() and the threshold setter wait inside
 *     the binding; here they are started and polled, the setter's
 *     wait for diag.ctrl_handled done here as modedgeai.c does it.
 *   - The order of the two pending thresholds follows MicroPython's
 *     dict: with both waiting, the one first moved second is sent
 *     first (both keys hash to one slot of a two-slot table).
 *   - `import buttons` fails on the Eva Kit; here a board with no
 *     buttons (bento_buttons_count() 0) is that board. `import
 *     edge_ai` fails where there is no model link; here
 *     bento_edgeai_supported() false ends the program with
 *     BENTO_E_UNSUPPORTED.
 *   - Repr of a name in the listing escapes as the PSoC Edge's
 *     MicroPython does (no unicode str there).
 *   - A model's "builtin" key is not in the C model info: a row
 *     below the count of compiled models is built-in, any later row
 *     came from the Store, and "?" is shown when that count cannot be
 *     read.
 *   - Stop ends the program after the same finally (the model is
 *     stopped), where MicroPython raises KeyboardInterrupt.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_buttons.h"
#include "bento_edgeai.h"
#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_time.h"

/* Times, limits and the meter. */
enum {
    POLL_MS = 100, /* read a result this often */
    DRAW_MS = 500, /* redraw this often: faster flickers */
    LOOP_MS = 20,  /* the main loop's rest */
    LOAD_MS = 150, /* let the screen settle after ui.screen() */
    OP_POLL_MS = 20,
    HANDLED_POLLS = 50, /* 50 x 4 ms: the setter's 200 ms wait */
    HANDLED_MS = 4,
    CONF_OK = 60, /* a confidence (%) to trust */
    MAX_CLASSES = BENTO_EDGEAI_MAX_CLASSES,
    MODELS_MAX = 32,
    SENSOR_COUNT = 3,
    PERCENT = 100,
    VU_LEDS = 16,  /* LEDs in the confidence meter */
    VU_GREEN = 10, /* LEDs 0-9 green */
    VU_AMBER = 14, /* 10-13 amber, the rest red */
    TEXT_BYTES = 128,
    NUMBER_BYTES = 12,
    /* "HH:MM:SS"; at worst three int32_t of 11 bytes, two colons
     * and the NUL. */
    CLOCK_BYTES = 36,
    SECONDS_PER_MS = 1000
};

/* Which threshold a slider moves; also the pending slot's index. */
enum {
    PEND_ENTER = 0,
    PEND_CONFIRM = 1,
    PEND_COUNT = 2,
    PEND_NONE = PEND_COUNT
};

/* What the touches and the buttons asked for this round. */
typedef enum {
    WANT_NOTHING = 0,
    WANT_PICK,
    WANT_TOGGLE
} want_kind_t;

/* One row of the model list. */
typedef struct {
    uint32_t index; /* what edge_ai.select() takes */
    char name[BENTO_EDGEAI_NAME_LEN];
    const char *source; /* "built-in", "Store" or "?" */
    const char *sensor; /* "IMU", "radar", "mic" or "?" */
    uint32_t class_count;
} lab_model_t;

/* The labels of the model chosen now. */
typedef struct {
    uint32_t count;
    char names[MAX_CLASSES][BENTO_EDGEAI_LABEL_LEN];
} lab_labels_t;

/* One class row on the right: its name and its score bar. */
typedef struct {
    bento_ui_id_t name;
    bento_ui_id_t bar;
} score_row_t;

/* The widgets the program changes after it has drawn them. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t led;
    bento_ui_id_t roller;
    bento_ui_id_t go;
    bento_ui_id_t hits;
    bento_ui_id_t signal;
    bento_ui_id_t verdict;
    bento_ui_id_t percent;
    bento_ui_id_t vu[VU_LEDS];
    int32_t lit; /* meter LEDs on now */
    bento_ui_id_t classes;
    bento_ui_id_t hearing_label;
    bento_ui_id_t hearing;
    bento_ui_id_t confirm_label;
    bento_ui_id_t confirm;
    bento_ui_id_t info;
    score_row_t rows[MAX_CLASSES];
    bento_ui_id_t status;
} lab_screen_t;

/* A board button: one event per press, a held press does not
 * repeat. */
typedef struct {
    uint32_t index;
    bool down;
    bool clicked;
} board_button_t;

/* Results counted, results per second, average latency. */
typedef struct {
    int32_t count;
    bento_real_t latency_ms;
    uint32_t start_ms;
} lab_stats_t;

/* Thresholds a slider moved, sent at the next redraw. */
typedef struct {
    bool waiting[PEND_COUNT];
    int32_t value[PEND_COUNT];
    int32_t first; /* the key put in first, PEND_NONE when empty */
} pending_t;

/* The whole program's state. */
typedef struct {
    lab_screen_t screen;
    uint32_t model_count;
    uint32_t current;
    bool running;
    lab_labels_t labels;
    int32_t tuned_class;
    bool has_thresholds; /* read_th() did not answer None */
    bento_edgeai_thresholds_t thresholds;
    pending_t pending;
    bool has_buttons;
    board_button_t next_button;
    board_button_t go_button;
    lab_stats_t stats;
    bool has_sequence;
    uint32_t last_sequence;
    bool has_fresh;
    bento_edgeai_result_t fresh;
    char shown[TEXT_BYTES]; /* the note shown last */
    bool has_shown;
    int32_t hits;
    bool confirmed_now;
    uint32_t drawn_ms;
    uint32_t polled_ms;
} lab_t;

/* The strongest "event" class, not class 0 (unlabeled / idle /
 * background), with its score and its two thresholds. */
typedef struct {
    uint32_t klass;
    int32_t percent;
    int32_t hearing;
    int32_t confirm;
} lab_event_t;

/* What the touches asked for this round. */
typedef struct {
    want_kind_t kind;
    int32_t pick; /* the menu position, for WANT_PICK */
} want_t;

/* Layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 12,
    TITLE_Y = 4,
    LED_X = 752,
    LED_Y = 6,
    LED_SIZE = 26,
    ROLLER_X = 12,
    ROLLER_Y = 40,
    ROLLER_W = 210,
    ROLLER_H = 232,
    ROLLER_ROWS = 7,
    GO_Y = 282,
    GO_H = 58,
    MIDDLE_X = 234,
    CARD_Y = 40,
    MIDDLE_W = 282,
    CARD_H = 300,
    RIGHT_X = 528,
    RIGHT_W = 260,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CARD_TITLE_DX = 14,
    CARD_TITLE_DY = 8,
    HITS_X = 320,
    HITS_Y = 48,
    INNER_X = 250,
    SIGNAL_Y = 72,
    SIGNAL_SIZE = 44,
    SIGNAL_RADIUS = 22, /* half the size: a round dot */
    VERDICT_X = 308,
    VERDICT_Y = 78,
    PERCENT_Y = 122,
    VU_Y = 146,
    VU_STEP = 16,
    VU_W = 12,
    VU_H = 24,
    CLASS_Y = 184,
    CLASSES_X = 310,
    CLASSES_Y = 174,
    CLASSES_W = 190,
    CLASSES_H = 38,
    HEARING_LABEL_Y = 220,
    SLIDER_X = 262,
    HEARING_Y = 246,
    SLIDER_W = 226,
    SLIDER_H = 10,
    CONFIRM_LABEL_Y = 264,
    CONFIRM_Y = 290,
    INFO_Y = 310,
    SCORE_X = 542,
    SCORE_NAME_Y = 80,
    SCORE_BAR_Y = 100,     /* where the bars are made ... */
    SCORE_ROW_BAR_Y = 102, /* ... and where a model puts them */
    SCORE_W = 232,
    THIN_BAR_H = 8,
    THICK_BAR_H = 14,
    THICK_ROW_PX = 44, /* rows this tall get the thick bar */
    ROW_MAX_PX = 62,
    ROWS_PX = 250, /* the height the rows share */
    STATUS_Y = 350,
    TITLE_PX = 24,
    BIG_PX = 28,
    TEXT_PX = 16
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF, /* secondary text, not heard yet */
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,   /* confirmed, Run */
    COLOR_WARN = 0xF5A623, /* heard, loading */
    COLOR_BAD = 0xE5484D,  /* Stop, errors */
    COLOR_INFO = 0x4A9EFF  /* card titles */
};

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* int(pct / 100 * 100), in the board's float as MicroPython does
 * it: it may be one less than pct. */
static int32_t as_percent(uint8_t pct)
{
    const float score = (float)pct / (float)PERCENT;
    const bento_real_t scaled =
        (bento_real_t)score * (bento_real_t)PERCENT;

    return (int32_t)scaled;
}

/* str() of the OSError the binding raises for a failed query. */
static const char *link_error_text(bento_status_t status)
{
    const char *text = "edge_ai: model link init failed";

    if (status == BENTO_E_TIMEOUT) {
        text = "edge_ai: model link timeout";
    } else if (status == BENTO_E_NOT_READY) {
        text = "edge_ai: engine unavailable";
    } else if (status == BENTO_E_UNSUPPORTED) {
        text = "edge_ai: this firmware does not answer that query";
    } else {
        /* the link did not come up */
    }
    return text;
}

/* repr() of a str: in quotes, ' unless it holds a ' and no ", with
 * the quote, \ and control bytes escaped (py/objstr.c). */
static void print_repr(const char *text)
{
    const char quote =
        ((strchr(text, '\'') != NULL) && (strchr(text, '"') == NULL))
            ? '"'
            : '\'';
    const uint8_t printable_min = 0x20U;
    const uint8_t delete_byte = 0x7FU;
    size_t i;

    (void)printf("%c", quote);
    for (i = 0U; text[i] != '\0'; i++) {
        const uint8_t byte = (uint8_t)text[i];

        if ((text[i] == quote) || (text[i] == '\\')) {
            (void)printf("\\%c", text[i]);
        } else if ((byte >= printable_min) && (byte != delete_byte)) {
            (void)printf("%c", text[i]);
        } else if (text[i] == '\n') {
            (void)printf("\\n");
        } else if (text[i] == '\r') {
            (void)printf("\\r");
        } else if (text[i] == '\t') {
            (void)printf("\\t");
        } else {
            (void)printf("\\x%02x", (unsigned int)byte);
        }
    }
    (void)printf("%c", quote);
}

/* The model's labels, "" for one that cannot be read. */
static void read_labels(const lab_model_t *model, lab_labels_t *labels)
{
    uint32_t length = 0U;
    uint32_t i;

    labels->count = model->class_count;
    for (i = 0U; i < labels->count; i++) {
        if (bento_edgeai_label(model->index, i, labels->names[i],
                               BENTO_EDGEAI_LABEL_LEN,
                               &length) != BENTO_OK) {
            labels->names[i][0] = '\0';
        }
    }
}

/* print() of one row's tuple:
 * (index, 'name', 'source', 'sensor', ['label', ...]). */
static void print_model(const lab_model_t *model)
{
    lab_labels_t labels;
    uint32_t i;

    read_labels(model, &labels);
    (void)printf("(%" PRIu32 ", ", model->index);
    print_repr(model->name);
    (void)printf(", ");
    print_repr(model->source);
    (void)printf(", ");
    print_repr(model->sensor);
    (void)printf(", [");
    for (i = 0U; i < labels.count; i++) {
        if (i > 0U) {
            (void)printf(", ");
        }
        print_repr(labels.names[i]);
    }
    (void)printf("])\n");
}

/* How many rows the firmware compiled in: those are built in.
 * -1 when the board cannot say (the binding's None). */
static int32_t compiled_count(void)
{
    bento_edgeai_diag_t diag;
    int32_t count = -1;

    bento_edgeai_diag_init(&diag);
    if (bento_edgeai_diag(&diag) == BENTO_OK) {
        count = (int32_t)diag.compiled_models;
    }
    return count;
}

/* One row as the program keeps it: built-in, Store or "?" when the
 * board cannot say, and the sensor's name. */
static void keep_row(const bento_edgeai_model_info_t *info,
                     int32_t index, int32_t compiled,
                     lab_model_t *model)
{
    static const char *const sensor_names[SENSOR_COUNT] = {
        "IMU", "radar", "mic"};
    const char *source = "?";

    if (compiled >= 0) {
        source = (index < compiled) ? "built-in" : "Store";
    }
    model->index = (uint32_t)index;
    (void)snprintf(model->name, sizeof(model->name), "%s", info->name);
    model->source = source;
    model->sensor = (info->sensor < (uint8_t)SENSOR_COUNT)
                        ? sensor_names[info->sensor]
                        : "?";
    model->class_count = info->class_count;
    if (model->class_count > (uint32_t)MAX_CLASSES) {
        model->class_count = MAX_CLASSES;
    }
}

/* The meter's LED i: green, amber, then red. */
static uint32_t vu_color(int32_t led)
{
    uint32_t color = COLOR_BAD;

    if (led < VU_GREEN) {
        color = COLOR_OK;
    } else if (led < VU_AMBER) {
        color = COLOR_WARN;
    } else {
        /* the last ones: red */
    }
    return color;
}

/* pend = {} */
static void clear_pending(pending_t *pending)
{
    pending->waiting[PEND_ENTER] = false;
    pending->waiting[PEND_CONFIRM] = false;
    pending->first = PEND_NONE;
}

/* pend[which] = value: a key already there keeps its place. */
static void put_pending(pending_t *pending, int32_t which,
                        int32_t value)
{
    if (pending->first == PEND_NONE) {
        pending->first = which;
    }
    pending->waiting[which] = true;
    pending->value[which] = value;
}

static uint32_t strongest_event(const bento_edgeai_result_t *result)
{
    uint32_t strongest = 0U;
    uint32_t i;

    if (result->class_count > 1U) {
        strongest = 1U;
    }
    for (i = 2U; i < result->class_count; i++) {
        if (result->score_pct[i] > result->score_pct[strongest]) {
            strongest = i;
        }
    }
    return strongest;
}

static void event_of(const lab_t *lab,
                     const bento_edgeai_result_t *result,
                     lab_event_t *event)
{
    const uint32_t strongest = strongest_event(result);

    event->klass = strongest;
    event->percent = as_percent(result->score_pct[strongest]);
    if (lab->has_thresholds &&
        (strongest < lab->thresholds.class_count)) {
        event->hearing = lab->thresholds.enter[strongest];
        event->confirm = lab->thresholds.confirm[strongest];
    } else {
        event->hearing = CONF_OK / 2;
        event->confirm = CONF_OK;
    }
}

/* A real number as '%.<digits>f' writes it on this board; the
 * buffer holds any number. */
static void real_text(bento_real_t value, int32_t digits, char *text)
{
    uint32_t length = 0U;

    if (bento_fmt_real_precision(text, BENTO_FMT_BUF_SIZE, value,
                                 digits, &length) != BENTO_OK) {
        text[0] = '\0';
    }
}

static void stats_start(lab_stats_t *stats)
{
    stats->count = 0;
    stats->latency_ms = (bento_real_t)0.0;
    stats->start_ms = bento_time_ticks_ms();
}

/* r["latency_ms"] is (float)latency_us / 1000 in the binding. */
static void stats_add(lab_stats_t *stats,
                      const bento_edgeai_result_t *result)
{
    const float latency_ms =
        (float)result->latency_us / (float)SECONDS_PER_MS;

    stats->count++;
    stats->latency_ms += (bento_real_t)latency_ms;
}

/* Results per second since the counting started. */
static bento_real_t stats_rate(const lab_stats_t *stats)
{
    const bento_real_t seconds =
        (bento_real_t)bento_time_ticks_diff(bento_time_ticks_ms(),
                                            stats->start_ms) /
        (bento_real_t)SECONDS_PER_MS;
    bento_real_t rate = (bento_real_t)0.0;

    if (seconds > (bento_real_t)0.0) {
        rate = (bento_real_t)stats->count / seconds;
    }
    return rate;
}

static void stats_report(const lab_stats_t *stats,
                         const lab_model_t *model)
{
    char rate[BENTO_FMT_BUF_SIZE];
    char latency[BENTO_FMT_BUF_SIZE];

    if (stats->count != 0) {
        real_text(stats_rate(stats), 1, rate);
        real_text(stats->latency_ms / (bento_real_t)stats->count, 2,
                  latency);
        (void)printf("[%s] %" PRId32 " results | %s /s | %s ms\n",
                     model->name, stats->count, rate, latency);
    }
}

/* The time now as HH:MM:SS. time.localtime() reads the board's
 * clock; a refused read leaves 00:00:00. */
static void clock_text(char *text, size_t cap)
{
    bento_time_tm_t now;

    (void)memset(&now, 0, sizeof(now));
    now.struct_size = (uint32_t)sizeof(now);
    if (bento_time_localtime_now(&now) != BENTO_OK) {
        (void)memset(&now, 0, sizeof(now));
    }
    (void)snprintf(text, cap, "%02" PRId32 ":%02" PRId32 ":%02" PRId32,
                   now.hour, now.min, now.sec);
}

/* Reads a board button and remembers a new press (a rising edge). */
static bento_status_t sample_button(const lab_t *lab,
                                    board_button_t *button)
{
    bool pressed = false;
    bento_status_t status = BENTO_OK;

    if (lab->has_buttons) {
        status = bento_buttons_pressed(button->index, &pressed);
        if ((status == BENTO_OK) && pressed && !button->down) {
            button->clicked = true;
        }
        button->down = pressed;
    }
    return status;
}

/* Was it pressed since the last question? Asking clears it. */
static bool button_clicked(board_button_t *button)
{
    const bool fired = button->clicked;

    button->clicked = false;
    return fired;
}

/* Waits for a started select or stop; Stop cancels it. */
static bento_status_t finish_op(void)
{
    bento_status_t status = BENTO_E_IN_PROGRESS;

    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(OP_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_edgeai_op_poll(NULL);
        } else {
            (void)bento_edgeai_op_cancel(); /* Stop wins */
        }
    }
    return status;
}

/* stop_ai(): stopped already is fine, and an OSError is passed. */
static bento_status_t stop_ai(void)
{
    bento_status_t status = bento_edgeai_stop_start();

    if (status == BENTO_OK) {
        status = finish_op();
    }
    if (status != BENTO_E_CANCELLED) {
        status = BENTO_OK;
    }
    return status;
}

/* The engine's count of serviced commands, false when it cannot
 * be read. */
static bool commands_handled(uint32_t *handled)
{
    bento_edgeai_diag_t diag;
    bool known;

    bento_edgeai_diag_init(&diag);
    known = bento_edgeai_diag(&diag) == BENTO_OK;
    *handled = diag.ctrl_handled;
    return known;
}

/* The board applies a threshold at its next pass: wait up to 200 ms
 * for its command count to move before reading back, as the
 * binding does. */
static bento_status_t wait_handled(uint32_t before)
{
    uint32_t handled = before;
    int32_t poll;
    bento_status_t status = BENTO_OK;
    bool moved = false;

    for (poll = 0;
         (poll < HANDLED_POLLS) && !moved && (status == BENTO_OK);
         poll++) {
        moved = commands_handled(&handled) && (handled != before);
        if (!moved) {
            status = wait_ms(HANDLED_MS);
        }
    }
    return status;
}

/* A position or size on the screen: the widget arguments are
 * 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

/* ui.poll() here only keeps the queue empty; it answers no events
 * when it fails. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* MicroPython's widget setters ignore a refusal (modui.c), and so do
 * clear_items() and add_option(), which are sent and forgotten. */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static void add_option(bento_ui_t *ui, bento_ui_id_t widget,
                       const char *text)
{
    (void)bento_ui_add_option(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t x, int16_t y, uint32_t color,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.color = color;
    args.font_px = TEXT_PX;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A dark card with its title. */
static bento_status_t add_card(bento_ui_t *ui, int16_t x, int16_t w,
                               const char *title)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = x;
    args.y = CARD_Y;
    args.w = w;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    status = bento_ui_panel_create(ui, &args, &unused);
    if (status == BENTO_OK) {
        status =
            add_label(ui, title, px(x + CARD_TITLE_DX),
                      px(CARD_Y + CARD_TITLE_DY), COLOR_INFO, &unused);
    }
    return status;
}

/* The status dot (a round Panel: a Led on the board keeps the
 * colour it was made with), the answer and its percentage. */
static bento_status_t add_answer(lab_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status = add_label(screen->ui, " ", HITS_X, HITS_Y,
                                      COLOR_DIM, &screen->hits);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = INNER_X;
        args.y = SIGNAL_Y;
        args.w = SIGNAL_SIZE;
        args.h = SIGNAL_SIZE;
        args.color = COLOR_DIM;
        args.min_val = COLOR_DIM;
        args.max_val = SIGNAL_RADIUS;
        args.font_px = 0;
        status =
            bento_ui_panel_create(screen->ui, &args, &screen->signal);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "-";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = VERDICT_X;
        args.y = VERDICT_Y;
        args.color = COLOR_TEXT;
        args.font_px = BIG_PX;
        status =
            bento_ui_label_create(screen->ui, &args, &screen->verdict);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "-", INNER_X, PERCENT_Y,
                           COLOR_TEXT, &screen->percent);
    }
    return status;
}

/* VU_LEDS small LEDs in a row, all off. */
static bento_status_t add_meter(lab_screen_t *screen)
{
    bento_ui_widget_args_t args;
    int32_t led;
    bento_status_t status = BENTO_OK;

    for (led = 0; (led < VU_LEDS) && (status == BENTO_OK); led++) {
        bento_ui_widget_args_init(&args);
        args.x = px(INNER_X + (led * VU_STEP));
        args.y = VU_Y;
        args.w = VU_W;
        args.h = VU_H;
        args.font_px = 0;
        args.color = vu_color(led);
        status =
            bento_ui_led_create(screen->ui, &args, &screen->vu[led]);
    }
    screen->lit = 0;
    return status;
}

static bento_status_t add_slider(bento_ui_t *ui, int16_t y,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = SLIDER_X;
    args.y = y;
    args.w = SLIDER_W;
    args.h = SLIDER_H;
    args.min_val = 0;
    args.max_val = PERCENT;
    args.font_px = 0;
    return bento_ui_slider_create(ui, &args, out_id);
}

/* The class dropdown and the two threshold sliders. */
static bento_status_t add_tuning(lab_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(screen->ui, "Class", INNER_X,
                                      CLASS_Y, COLOR_INFO, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = CLASSES_X;
        args.y = CLASSES_Y;
        args.w = CLASSES_W;
        args.h = CLASSES_H;
        status = bento_ui_dropdown_create(screen->ui, &args,
                                          &screen->classes);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, " ", INNER_X, HEARING_LABEL_Y,
                           COLOR_TEXT, &screen->hearing_label);
    }
    if (status == BENTO_OK) {
        status = add_slider(screen->ui, HEARING_Y, &screen->hearing);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, " ", INNER_X, CONFIRM_LABEL_Y,
                           COLOR_TEXT, &screen->confirm_label);
    }
    if (status == BENTO_OK) {
        status = add_slider(screen->ui, CONFIRM_Y, &screen->confirm);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, " ", INNER_X, INFO_Y, COLOR_DIM,
                           &screen->info);
    }
    return status;
}

/* The right card: eight names, then eight bars. */
static bento_status_t add_scores(lab_screen_t *screen)
{
    bento_ui_widget_args_t args;
    int32_t i;
    bento_status_t status =
        add_card(screen->ui, RIGHT_X, RIGHT_W, "Class scores");

    for (i = 0; (i < MAX_CLASSES) && (status == BENTO_OK); i++) {
        status = add_label(screen->ui, " ", SCORE_X, SCORE_NAME_Y,
                           COLOR_TEXT, &screen->rows[i].name);
    }
    for (i = 0; (i < MAX_CLASSES) && (status == BENTO_OK); i++) {
        bento_ui_widget_args_init(&args);
        args.x = SCORE_X;
        args.y = SCORE_BAR_Y;
        args.w = SCORE_W;
        args.h = THIN_BAR_H;
        args.min_val = 0;
        args.max_val = PERCENT;
        args.font_px = 0;
        status = bento_ui_bar_create(screen->ui, &args,
                                     &screen->rows[i].bar);
    }
    return status;
}

/* The confidence meter: only the LEDs that change are switched. */
static void meter(lab_screen_t *screen, int32_t confidence)
{
    const int32_t lit = ((confidence * VU_LEDS) + (PERCENT / 2)) /
                        PERCENT; /* rounded to the nearest LED */
    const int32_t first = (lit < screen->lit) ? lit : screen->lit;
    const int32_t end = (lit > screen->lit) ? lit : screen->lit;
    int32_t led;

    for (led = first; led < end; led++) {
        (void)bento_ui_set_value(screen->ui, screen->vu[led],
                                 (led < lit) ? 1 : 0);
    }
    screen->lit = lit;
}

/* One score row of a model with `shown` rows, each row_px tall. */
static void place_row(const lab_t *lab, int32_t row, int32_t row_px)
{
    const lab_screen_t *screen = &lab->screen;
    const score_row_t *place = &screen->rows[row];
    const int32_t bar_h =
        (row_px >= THICK_ROW_PX) ? THICK_BAR_H : THIN_BAR_H;

    (void)bento_ui_set_pos(screen->ui, place->name, SCORE_X,
                           px(SCORE_NAME_Y + (row * row_px)));
    set_text(screen->ui, place->name,
             ((uint32_t)row < lab->labels.count)
                 ? lab->labels.names[row]
                 : "-");
    (void)bento_ui_set_pos(screen->ui, place->bar, SCORE_X,
                           px(SCORE_ROW_BAR_Y + (row * row_px)));
    (void)bento_ui_set_size(screen->ui, place->bar, SCORE_W, px(bar_h));
    (void)bento_ui_set_value(screen->ui, place->bar, 0);
    (void)bento_ui_set_color(screen->ui, place->bar, COLOR_DIM);
    (void)bento_ui_set_visible(screen->ui, place->name, 1U);
    (void)bento_ui_set_visible(screen->ui, place->bar, 1U);
}

/* A row per class, fewer classes get thicker bars; the rest are
 * hidden. */
static void show_class_rows(const lab_t *lab)
{
    const lab_screen_t *screen = &lab->screen;
    int32_t shown = (int32_t)lab->labels.count;
    int32_t row_px;
    int32_t row;

    if (shown < 1) {
        shown = 1;
    }
    row_px = ROWS_PX / shown;
    if (row_px > ROW_MAX_PX) {
        row_px = ROW_MAX_PX;
    }
    for (row = 0; row < MAX_CLASSES; row++) {
        if (row < shown) {
            place_row(lab, row, row_px);
        } else {
            (void)bento_ui_set_visible(screen->ui,
                                       screen->rows[row].name, 0U);
            (void)bento_ui_set_visible(screen->ui,
                                       screen->rows[row].bar, 0U);
        }
    }
}

/* Class k's thresholds on the two labels and sliders. */
static void show_thresholds(const lab_t *lab)
{
    const lab_screen_t *screen = &lab->screen;
    const int32_t klass = lab->tuned_class;
    char text[TEXT_BYTES];

    if (lab->has_thresholds &&
        (klass < (int32_t)lab->thresholds.class_count)) {
        const int32_t hearing = lab->thresholds.enter[klass];
        const int32_t confirm = lab->thresholds.confirm[klass];

        (void)snprintf(text, sizeof(text), "Hearing at %" PRId32 "%%",
                       hearing);
        set_text(screen->ui, screen->hearing_label, text);
        (void)snprintf(text, sizeof(text), "Confirm at %" PRId32 "%%",
                       confirm);
        set_text(screen->ui, screen->confirm_label, text);
        (void)bento_ui_set_value(screen->ui, screen->hearing, hearing);
        (void)bento_ui_set_value(screen->ui, screen->confirm, confirm);
    } else {
        set_text(screen->ui, screen->hearing_label, "No thresholds");
    }
}

/* The bottom status line. */
static void say(const lab_t *lab, const char *text, uint32_t color)
{
    (void)bento_ui_set_color(lab->screen.ui, lab->screen.status, color);
    set_text(lab->screen.ui, lab->screen.status, text);
}

/* Every score bar and its name with the percentage. */
static void show_scores(const lab_t *lab,
                        const bento_edgeai_result_t *result)
{
    const lab_screen_t *screen = &lab->screen;
    char text[TEXT_BYTES];
    uint32_t i;

    for (i = 0U; i < (uint32_t)MAX_CLASSES; i++) {
        if ((i < result->class_count) && (i < lab->labels.count)) {
            const int32_t percent = as_percent(result->score_pct[i]);

            (void)bento_ui_set_value(screen->ui, screen->rows[i].bar,
                                     percent);
            (void)bento_ui_set_color(screen->ui, screen->rows[i].bar,
                                     (i == result->top) ? COLOR_OK
                                                        : COLOR_DIM);
            (void)snprintf(text, sizeof(text), "%s  %" PRId32 "%%",
                           lab->labels.names[i], percent);
            set_text(screen->ui, screen->rows[i].name, text);
        }
    }
}

/* Grey below Hearing at, amber heard, green confirmed. */
static void show_result(lab_t *lab, const bento_edgeai_result_t *result)
{
    lab_screen_t *screen = &lab->screen;
    lab_event_t event;
    const char *word = "quiet";
    uint32_t color = COLOR_DIM;
    char text[TEXT_BYTES];

    event_of(lab, result, &event);
    if (event.percent >= event.confirm) {
        color = COLOR_OK;
        word = "confirmed";
    } else if (event.percent >= event.hearing) {
        color = COLOR_WARN;
        word = "heard";
    } else {
        /* quiet: stays grey */
    }
    (void)bento_ui_set_color(screen->ui, screen->signal, color);
    set_text(screen->ui, screen->verdict,
             (event.klass < lab->labels.count)
                 ? lab->labels.names[event.klass]
                 : "-");
    (void)bento_ui_set_color(
        screen->ui, screen->verdict,
        (event.percent >= event.hearing) ? color : COLOR_TEXT);
    (void)snprintf(text, sizeof(text), "%" PRId32 "%%  %s",
                   event.percent, word);
    set_text(screen->ui, screen->percent, text);
    meter(screen, event.percent);
    show_scores(lab, result);
}

/* After any change: the button, the light, and a fresh count. */
static void reset_run_view(lab_t *lab)
{
    lab_screen_t *screen = &lab->screen;

    set_text(screen->ui, screen->go, lab->running ? "Stop" : "Run");
    (void)bento_ui_set_color(screen->ui, screen->go,
                             lab->running ? COLOR_BAD : COLOR_OK);
    (void)bento_ui_set_value(screen->ui, screen->led,
                             lab->running ? 1 : 0);
    stats_start(&lab->stats);
    lab->has_sequence = false;
    lab->has_fresh = false;
    lab->hits = 0;
    lab->confirmed_now = false;
    set_text(screen->ui, screen->hits, " ");
}

/* One touch: the roller picks, Run toggles, the dropdown chooses
 * the class to tune, a slider waits to be sent at the next redraw. */
static void read_touch(lab_t *lab, const bento_ui_event_t *event,
                       want_t *want)
{
    const lab_screen_t *screen = &lab->screen;
    const bool changed =
        event->type == (uint8_t)BENTO_UI_EVENT_VALUE_CHANGED;

    if (changed && (event->id == screen->roller)) {
        want->kind = WANT_PICK;
        want->pick = event->value;
    } else if ((event->id == screen->go) &&
               (event->type == (uint8_t)BENTO_UI_EVENT_CLICKED)) {
        want->kind = WANT_TOGGLE;
    } else if (changed && (event->id == screen->classes)) {
        lab->tuned_class = event->value;
        show_thresholds(lab);
    } else if (changed && (event->id == screen->hearing)) {
        put_pending(&lab->pending, PEND_ENTER, event->value);
    } else if (changed && (event->id == screen->confirm)) {
        put_pending(&lab->pending, PEND_CONFIRM, event->value);
    } else {
        /* any other widget changes nothing */
    }
}

/* The touches, then the board buttons: button 1 (SW5) picks the next
 * model, button 0 (SW6) toggles. ui.poll() answers no events when it
 * fails. */
static bento_status_t read_controls(lab_t *lab, want_t *want)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status;

    want->kind = WANT_NOTHING;
    want->pick = 0;
    if (bento_ui_poll(lab->screen.ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        read_touch(lab, &events[i], want);
    }
    status = sample_button(lab, &lab->next_button);
    if (status == BENTO_OK) {
        status = sample_button(lab, &lab->go_button);
    }
    if ((status == BENTO_OK) && button_clicked(&lab->next_button)) {
        const uint32_t next = (lab->current + 1U) % lab->model_count;

        want->kind = WANT_PICK;
        want->pick = (int32_t)next;
    }
    if ((status == BENTO_OK) && button_clicked(&lab->go_button)) {
        want->kind = WANT_TOGGLE;
    }
    return status;
}

/* Counted once per event, the moment it first reaches Confirm at. */
static void count_hit(lab_t *lab, const lab_event_t *event)
{
    char clock[CLOCK_BYTES];
    char text[TEXT_BYTES];

    if ((event->percent >= event->confirm) && !lab->confirmed_now) {
        lab->hits++;
        clock_text(clock, sizeof(clock));
        (void)snprintf(text, sizeof(text), "%" PRId32 "x | %s",
                       lab->hits, clock);
        set_text(lab->screen.ui, lab->screen.hits, text);
        if (event->klass < lab->labels.count) {
            (void)printf("%s %s %" PRId32 "%% #%" PRId32 "\n", clock,
                         lab->labels.names[event->klass],
                         event->percent, lab->hits);
        } else {
            (void)printf("%s %" PRIu32 " %" PRId32 "%% #%" PRId32 "\n",
                         clock, event->klass, event->percent,
                         lab->hits);
        }
    }
    lab->confirmed_now = event->percent >= event->confirm;
}

/* No model at all: one line on a clean screen. */
static bento_status_t show_no_models(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "No models on this board";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = TITLE_X;
        args.y = TITLE_X;
        args.color = COLOR_BAD;
        args.font_px = TITLE_PX;
        status = bento_ui_label_create(ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        drain_events(ui);
    }
    return status;
}

/* After "Model list changed" no note replaces it until it runs. */
static const char LIST_CHANGED_MARK[] = "x";

/* The models, the list being read, and the program's state: too
 * big for the 8 KB program stack, so at file scope, zeroed by the
 * loader on every load and set again at the start of main()
 * (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern lab_model_t g_models[MODELS_MAX];
lab_model_t g_models[MODELS_MAX];
extern lab_model_t g_listed[MODELS_MAX];
lab_model_t g_listed[MODELS_MAX];
extern lab_t g_lab;
lab_t g_lab;

/* edge_ai.models() into g_listed: a row the engine refuses is
 * skipped, a link failure answers its status. */
static bento_status_t read_rows(uint32_t *count)
{
    bento_edgeai_model_info_t info;
    int32_t rows = 0;
    int32_t compiled;
    int32_t i;
    bento_status_t status = bento_edgeai_count(&rows);

    *count = 0U;
    compiled = (status == BENTO_OK) ? compiled_count() : -1;
    for (i = 0; (i < rows) && (status == BENTO_OK); i++) {
        bento_edgeai_model_info_init(&info);
        status = bento_edgeai_model_info((uint32_t)i, &info);
        if ((status == BENTO_OK) && (*count < (uint32_t)MODELS_MAX)) {
            keep_row(&info, i, compiled, &g_listed[*count]);
            (*count)++;
        } else if ((status == BENTO_E_NOT_READY) ||
                   (status == BENTO_E_NOT_FOUND)) {
            status = BENTO_OK; /* a row the engine refuses */
        } else {
            /* kept, or past MODELS_MAX, or a link failure */
        }
    }
    return status;
}

/* list_models(): every row printed, then the list. A link that
 * fails prints why and gives an empty list; an empty list leaves
 * the old one (the original's `list_models() or models`). */
static void list_models(lab_t *lab)
{
    uint32_t count = 0U;
    const bento_status_t status = read_rows(&count);
    uint32_t i;

    if (status != BENTO_OK) {
        (void)printf("cannot read models: %s\n",
                     link_error_text(status));
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        print_model(&g_listed[i]);
    }
    if (count > 0U) {
        (void)memcpy(g_models, g_listed,
                     (size_t)count * sizeof(g_models[0]));
        lab->model_count = count;
    }
}

/* The roller, one name at a time (one long text would be cut at
 * 126 bytes). */
static void fill_roller(const lab_t *lab)
{
    uint32_t i;

    (void)bento_ui_items_clear(lab->screen.ui, lab->screen.roller);
    for (i = 0U; i < lab->model_count; i++) {
        add_option(lab->screen.ui, lab->screen.roller,
                   g_models[i].name);
    }
}

/* The title and the run light. */
static bento_status_t add_title(lab_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "AI Model Lab";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = TITLE_X;
    args.y = TITLE_Y;
    args.color = COLOR_TEXT;
    args.font_px = TITLE_PX;
    status = bento_ui_label_create(screen->ui, &args, &unused);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = LED_X;
        args.y = LED_Y;
        args.w = LED_SIZE;
        args.h = LED_SIZE;
        args.color = COLOR_OK;
        args.font_px = 0; /* off: nothing runs */
        status = bento_ui_led_create(screen->ui, &args, &screen->led);
    }
    return status;
}

/* The roller with every model's name, and the Run button. */
static bento_status_t add_picker(lab_t *lab)
{
    lab_screen_t *screen = &lab->screen;
    bento_ui_widget_args_t args;
    bento_status_t status = add_title(screen);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = g_models[0].name;
        args.text_len = (uint32_t)strlen(args.text);
        args.x = ROLLER_X;
        args.y = ROLLER_Y;
        args.w = ROLLER_W;
        args.h = ROLLER_H;
        args.color = COLOR_TEXT;
        status =
            bento_ui_roller_create(screen->ui, &args, &screen->roller);
    }
    if (status == BENTO_OK) {
        fill_roller(lab);
        (void)bento_ui_set_prop(screen->ui, screen->roller,
                                (uint8_t)BENTO_UI_PROP_VISIBLE_ROWS,
                                ROLLER_ROWS);
        bento_ui_widget_args_init(&args);
        args.text = "Run";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = ROLLER_X;
        args.y = GO_Y;
        args.w = ROLLER_W;
        args.h = GO_H;
        args.color = COLOR_OK;
        args.font_px = TITLE_PX;
        status = bento_ui_button_create(screen->ui, &args, &screen->go);
    }
    return status;
}

/* Every widget, made once: a new model only changes what differs,
 * so the screen never blinks. */
static bento_status_t build_screen(lab_t *lab)
{
    lab_screen_t *screen = &lab->screen;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_picker(lab);
    }
    if (status == BENTO_OK) {
        status = add_card(screen->ui, MIDDLE_X, MIDDLE_W, "AI says");
    }
    if (status == BENTO_OK) {
        status = add_answer(screen);
    }
    if (status == BENTO_OK) {
        status = add_meter(screen);
    }
    if (status == BENTO_OK) {
        status = add_tuning(screen);
    }
    if (status == BENTO_OK) {
        status = add_scores(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, " ", TITLE_X, STATUS_Y,
                           COLOR_WARN, &screen->status);
    }
    if (status == BENTO_OK) {
        drain_events(screen->ui);
    }
    return status;
}

/* The chosen model: its rows, its classes, its facts, a clean
 * answer. */
static void apply_model(lab_t *lab)
{
    lab_screen_t *screen = &lab->screen;
    const lab_model_t *model = &g_models[lab->current];
    char text[TEXT_BYTES];
    uint32_t i;

    read_labels(model, &lab->labels);
    show_class_rows(lab);
    (void)bento_ui_items_clear(screen->ui, screen->classes);
    for (i = 0U; i < lab->labels.count; i++) {
        add_option(screen->ui, screen->classes, lab->labels.names[i]);
    }
    (void)snprintf(text, sizeof(text), "%s | %s | %" PRIu32 " classes",
                   model->source, model->sensor, lab->labels.count);
    set_text(screen->ui, screen->info, text);
    set_text(screen->ui, screen->verdict, "-");
    (void)bento_ui_set_color(screen->ui, screen->verdict, COLOR_TEXT);
    set_text(screen->ui, screen->percent, "-");
    (void)bento_ui_set_color(screen->ui, screen->signal, COLOR_DIM);
    meter(screen, 0);
}

/* read_th(): the model's thresholds, or none (the original's None
 * on any failure). */
static void read_thresholds(lab_t *lab)
{
    bento_edgeai_thresholds_init(&lab->thresholds);
    lab->has_thresholds =
        bento_edgeai_thresholds(g_models[lab->current].index,
                                &lab->thresholds) == BENTO_OK;
}

/* Starts at class 1: class 0 is the "rest" class of nearly every
 * model. Nothing is pending for the new model. */
static void tune_class(lab_t *lab)
{
    lab->tuned_class = (lab->labels.count > 1U) ? 1 : 0;
    (void)bento_ui_set_value(lab->screen.ui, lab->screen.classes,
                             lab->tuned_class);
    read_thresholds(lab);
    show_thresholds(lab);
    clear_pending(&lab->pending);
}

/* set_th(): one threshold of class k, then what the board really
 * keeps (it holds hearing <= confirm). */
static bento_status_t set_threshold(lab_t *lab, int32_t which,
                                    int32_t percent)
{
    const uint32_t kind = (which == PEND_ENTER)
                              ? BENTO_EDGEAI_TH_ENTER
                              : BENTO_EDGEAI_TH_CONFIRM;
    uint32_t before = 0U;
    const bool counted = commands_handled(&before);
    bento_status_t status = bento_edgeai_threshold_set(
        g_models[lab->current].index, (uint32_t)lab->tuned_class, kind,
        (uint32_t)percent);

    if (status != BENTO_OK) {
        (void)printf("cannot set threshold: %s\n",
                     "edge_ai: threshold send failed");
        status = BENTO_OK;
    } else if (counted) {
        status = wait_handled(before);
    } else {
        /* no count to wait for: read back at once */
    }
    if (status == BENTO_OK) {
        read_thresholds(lab);
    }
    return status;
}

/* The pending thresholds, in MicroPython's dict order: both keys
 * hash to the same slot of the two-slot table, so the key put in
 * second sits before the first one. */
static bento_status_t send_pending(lab_t *lab)
{
    pending_t *pending = &lab->pending;
    const int32_t first = pending->first;
    const int32_t second =
        (first == PEND_ENTER) ? PEND_CONFIRM : PEND_ENTER;
    bento_status_t status = BENTO_OK;

    if (first != PEND_NONE) {
        if (pending->waiting[second]) {
            status = set_threshold(lab, second, pending->value[second]);
        }
        if (status == BENTO_OK) {
            status = set_threshold(lab, first, pending->value[first]);
        }
        if (status == BENTO_OK) {
            clear_pending(pending);
            show_thresholds(lab);
        }
    }
    return status;
}

/* run(): starts the chosen model. *started false: the model list
 * changed meanwhile (a model was removed or deployed), or select()
 * failed; both mean "read the list again". */
static bento_status_t run_model(lab_t *lab, bool *started)
{
    const lab_model_t *model = &g_models[lab->current];
    char text[TEXT_BYTES];
    int32_t count = 0;
    bento_status_t status;
    bool sent;

    *started = false;
    if ((bento_edgeai_count(&count) == BENTO_OK) &&
        (count != (int32_t)lab->model_count)) {
        status = BENTO_OK; /* the list changed: no select */
    } else { /* a count that cannot be read is passed over */
        (void)snprintf(text, sizeof(text), "Loading %s ...",
                       model->name);
        say(lab, text, COLOR_WARN);
        drain_events(lab->screen.ui);
        status = bento_edgeai_select_start(model->index);
        sent = status == BENTO_OK;
        if (sent) {
            status = finish_op(); /* select() loads, then runs */
        }
        *started = status == BENTO_OK;
        if ((status != BENTO_OK) && (status != BENTO_E_CANCELLED)) {
            (void)printf("select %s : %s\n", model->name,
                         sent ? "edge_ai: select not confirmed"
                              : "edge_ai: select send failed");
            status = BENTO_OK;
        }
    }
    return status;
}

/* The list changed: read it again, start over at the first model
 * and wait for Run. */
static void reload_models(lab_t *lab)
{
    list_models(lab);
    lab->current = 0U;
    lab->running = false;
    fill_roller(lab);
    (void)bento_ui_set_value(lab->screen.ui, lab->screen.roller, 0);
    apply_model(lab);
    tune_class(lab);
    say(lab, "Model list changed - pick again, then Run", COLOR_BAD);
    (void)snprintf(lab->shown, sizeof(lab->shown), "%s",
                   LIST_CHANGED_MARK);
    lab->has_shown = true;
}

/* A new model, or Run/Stop. A new model stops the old one and
 * waits for Run. */
static bento_status_t apply_want(lab_t *lab, want_kind_t want,
                                 int32_t pick)
{
    const bool was_running = lab->running;
    bento_status_t status = BENTO_OK;
    bool started = true;

    if (was_running) {
        stats_report(&lab->stats, &g_models[lab->current]);
        status = stop_ai();
    }
    if ((status == BENTO_OK) && (want == WANT_PICK)) {
        lab->current = (uint32_t)pick;
        lab->running = false;
        (void)bento_ui_set_value(lab->screen.ui, lab->screen.roller,
                                 pick);
        apply_model(lab);
        tune_class(lab);
    } else if (status == BENTO_OK) {
        lab->running = !was_running;
    } else {
        /* Stop during the stop: nothing changes */
    }
    if ((status == BENTO_OK) && lab->running) {
        status = run_model(lab, &started);
    }
    if ((status == BENTO_OK) && lab->running && !started) {
        reload_models(lab);
    }
    if (status == BENTO_OK) {
        reset_run_view(lab);
    }
    return status;
}

/* A new result of the running model: kept for the next redraw and
 * counted. A result with no label, an old one, or one of another
 * model is passed over; a failed read is the original's None. */
static void poll_result(lab_t *lab)
{
    const lab_model_t *model = &g_models[lab->current];
    bento_edgeai_result_t result;
    char label[BENTO_EDGEAI_LABEL_LEN];
    uint32_t length = 0U;
    lab_event_t event;

    bento_edgeai_result_init(&result);
    if ((bento_edgeai_result(&result) == BENTO_OK) &&
        result.has_verdict &&
        (bento_edgeai_label(result.index, result.top, label,
                            sizeof(label), &length) == BENTO_OK) &&
        (!lab->has_sequence || (result.seq != lab->last_sequence)) &&
        ((uint32_t)result.index == model->index)) {
        lab->has_sequence = true;
        lab->last_sequence = result.seq;
        lab->fresh = result;
        lab->has_fresh = true;
        stats_add(&lab->stats, &result);
        event_of(lab, &result, &event);
        count_hit(lab, &event);
    }
}

/* The note for the bottom line, or none. After "Model list
 * changed" a stopped lab says nothing until it runs. */
static bool pick_note(const lab_t *lab, char *note, size_t cap)
{
    char rate[BENTO_FMT_BUF_SIZE];
    bool has_note = true;

    if (!lab->running) {
        has_note = !lab->has_shown ||
                   (strcmp(lab->shown, LIST_CHANGED_MARK) != 0);
        (void)snprintf(note, cap, "%s",
                       "Pick a model on the roller, then tap Run");
    } else if (lab->stats.count != 0) {
        real_text(stats_rate(&lab->stats), 1, rate);
        (void)snprintf(note, cap, "Running | %s results/s", rate);
    } else {
        (void)snprintf(note, cap, "%s",
                       "Running - waiting for the first result...");
    }
    return has_note;
}

/* Every DRAW_MS: send the moved thresholds, draw the kept result,
 * and change the note only when it says something new. */
static bento_status_t redraw(lab_t *lab)
{
    char note[TEXT_BYTES];
    bento_status_t status = send_pending(lab);

    if ((status == BENTO_OK) && lab->has_fresh) {
        show_result(lab, &lab->fresh);
        lab->has_fresh = false;
    }
    if ((status == BENTO_OK) && pick_note(lab, note, sizeof(note)) &&
        (!lab->has_shown || (strcmp(note, lab->shown) != 0))) {
        say(lab, note, lab->running ? COLOR_OK : COLOR_WARN);
        (void)snprintf(lab->shown, sizeof(lab->shown), "%s", note);
        lab->has_shown = true;
    }
    return status;
}

/* One round of the main loop: controls, a change, a result every
 * POLL_MS, a redraw every DRAW_MS, then a short rest. */
static bento_status_t lab_round(lab_t *lab)
{
    want_t want;
    uint32_t now_ms;
    bento_status_t status = read_controls(lab, &want);

    if ((status == BENTO_OK) && (want.kind != WANT_NOTHING)) {
        status = apply_want(lab, want.kind, want.pick);
    }
    now_ms = bento_time_ticks_ms();
    if ((status == BENTO_OK) && lab->running &&
        (bento_time_ticks_diff(now_ms, lab->polled_ms) >= POLL_MS)) {
        lab->polled_ms = now_ms;
        poll_result(lab);
    }
    if ((status == BENTO_OK) &&
        (bento_time_ticks_diff(now_ms, lab->drawn_ms) >= DRAW_MS)) {
        status = redraw(lab);
        lab->drawn_ms = now_ms;
    }
    if (status == BENTO_OK) {
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* The main loop until Stop; whenever it ends, the finally reports
 * the run and gives the AI core back. */
static bento_status_t loop_then_stop(lab_t *lab)
{
    bento_status_t status = BENTO_OK;
    bento_status_t stop_status;

    while (status == BENTO_OK) {
        status = lab_round(lab);
    }
    if (status == BENTO_E_CANCELLED) { /* the finally still runs */
        (void)bento_runtime_acknowledge_stop();
    }
    if (lab->running) {
        stats_report(&lab->stats, &g_models[lab->current]);
    }
    stop_status = stop_ai();
    return (status == BENTO_E_CANCELLED) ? stop_status : status;
}

/* The screen, the first model waiting for Run, then the loop. */
static bento_status_t run_lab(lab_t *lab)
{
    bento_status_t status = build_screen(lab);

    if (status == BENTO_OK) {
        apply_model(lab);
        tune_class(lab);
        stats_start(&lab->stats);
        lab->drawn_ms = bento_time_ticks_ms();
        lab->polled_ms = lab->drawn_ms;
        status = loop_then_stop(lab);
    }
    return status;
}

int main(void)
{
    lab_t *lab = &g_lab;
    bool supported = false;
    bento_status_t status;

    (void)memset(g_models, 0, sizeof(g_models));
    (void)memset(g_listed, 0, sizeof(g_listed));
    (void)memset(lab, 0, sizeof(*lab));
    lab->screen.ui = bento_runtime_get_ui();
    lab->has_buttons = bento_buttons_count() > 0U;
    lab->next_button.index = 1U; /* SW5, the lower button */
    lab->go_button.index = 0U;   /* SW6, the upper button */
    status = bento_edgeai_supported(&supported);
    if ((status == BENTO_OK) && !supported) {
        status = BENTO_E_UNSUPPORTED; /* import edge_ai fails */
    }
    if (status == BENTO_OK) {
        list_models(lab);
        if (lab->model_count == 0U) {
            status = show_no_models(lab->screen.ui);
        } else {
            status = run_lab(lab);
        }
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
