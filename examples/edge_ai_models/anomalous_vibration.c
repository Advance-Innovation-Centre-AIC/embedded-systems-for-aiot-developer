/*
 * anomalous_vibration.c -- สุขภาพปั๊ม (Pump Health) ด้วยโมเดล
 * "AnomalousVibration" จาก Edge AI Store
 *
 * ติดบอร์ดกับพัดลมที่มีตะแกรงครอบ โมเดลให้คะแนนว่าแรงสั่นต่างจาก
 * เครื่องที่ใช้สอนแค่ไหน (คลาส 1 = ผิดปกติ) เข็มเกจบอกคะแนน ถึงเกณฑ์
 * Hearing at = Alert (ไม่ค้าง) ถึง Confirm at 3 ใน 5 ผลล่าสุด =
 * Danger ซึ่งค้างและกะพริบจนกว่าจะกด ACK แถบเลื่อนด้านขวาตั้งเกณฑ์
 * Alert/Danger บนบอร์ด ปุ่ม SW6 หรือแตะ ACK = รับทราบ ปุ่ม SW5 =
 * หยุดชั่วคราว/ทำต่อ ต้องติดตั้งโมเดลจาก edgeai-store.tesaiot.dev
 * ก่อน จบแล้วคืนเกณฑ์เดิมของบอร์ดและคืนแกน AI เสมอ
 *
 * Tape the board to a guarded fan. The model scores how unlike its
 * training machine the vibration is (class 1 = anomaly); the gauge
 * needle shows the score. Hearing at = Alert (not latched); Confirm
 * at in 3 of the last 5 results = Danger, which latches and flashes
 * until ACK. The sliders set Alert and Danger on the board. SW6 or
 * a tap on ACK = acknowledge; SW5 = pause and resume. Deploy the
 * model from edgeai-store.tesaiot.dev first. The board's own
 * thresholds are put back at the end and the AI core is always
 * released.
 *
 * Twin of anomalous_vibration.py
 * (embedded-systems-for-aiot-developer/examples/edge_ai_models/anomalous_vibration.py).
 * Style: CLEAN_C.md. Written new; the library twin
 * devkit-21-ai-model-lab gave the style and the Edge AI calls.
 * Differs from the original:
 *   - m["builtin"] is read as (row < diag.compiled_models); when the
 *     board cannot answer diag it is None (the Emulator's case), so
 *     BUILTIN = False accepts only a row past the built-in ones.
 *   - edge_ai.models() reads every row; here the rows are read one
 *     by one and a row the engine refuses is skipped. The model's
 *     labels are read with bento_edgeai_label(), "" when one cannot
 *     be read, as the binding does.
 *   - `import buttons` fails on the Eva Kit; here a board with no
 *     buttons (bento_buttons_count() 0) is that board. A board
 *     without the model link (bento_edgeai_supported() false) shows
 *     the "not on this board" card.
 *   - edge_ai.select(), stop() and the threshold setter wait inside
 *     the binding; here they are started and polled, the setter's
 *     wait for diag.ctrl_handled done as modedgeai.c does it. A
 *     failed select prints the binding's words ("edge_ai: select
 *     send failed" or "select not confirmed"), a failed threshold
 *     "edge_ai: threshold send failed".
 *   - The pending sliders are sent in MicroPython's dict order: with
 *     both waiting, the one moved first is sent second (both keys
 *     hash to one slot of a two-slot table, as devkit-21 notes).
 *   - th != th0 compares the board's numbers; a fallback (the
 *     board gave none) never equals a read from the board, as a
 *     list never equals a tuple in MicroPython. "th is not" (a new
 *     read-back) is a flag set by every read.
 *   - The Stop exception is a stop text kept in the program state:
 *     the loop ends, the board's thresholds are put back, the text
 *     goes on the status line in red and is printed, and the AI
 *     core is released, as the two finally blocks do.
 *   - gc.collect() and the free-heap print are left out: C has no
 *     garbage collector.
 *   - The scores are the engine's whole percentages, the same number
 *     int(score * 100 + 0.5) gives.
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

enum { /* times, in ms */
    POLL_MS = 100,
    DRAW_MS = 500,
    LOOP_MS = 20,
    LOAD_MS = 150,
    OP_POLL_MS = 20,
    FAIL_STOP_MS = 3000,
    REFRACT_MS = 1500,  /* no second event sooner than this */
    HANDLED_POLLS = 50, /* 50 x 4 ms: the setter's 200 ms wait */
    HANDLED_MS = 4
};

enum { /* sizes and limits */
    TEXT_BYTES = 128,
    CLOCK_BYTES = 36, /* "HH:MM:SS"; three int32_t at worst */
    MAX_CLASSES = BENTO_EDGEAI_MAX_CLASSES,
    LABEL_BYTES = BENTO_EDGEAI_LABEL_LEN,
    VOTE_K = 3, /* event = K of the last N results at Confirm at */
    VOTE_N = 5,
    INFO_LINES = 2,
    LEVEL_KINDS = 3, /* Hearing at, Confirm at, clear below */
    SLIDERS = 2,     /* Hearing at and Confirm at */
    ANOMALY_CLASS = 1,
    PERCENT = 100,
    NEEDLE_PX = 60, /* the needle prop: (length << 16) | value */
    NEEDLE_SHIFT = 16,
    SCALE_TICKS = 11,
    SCALE_MAJOR_EVERY = 2
};

enum { /* fallback thresholds, % */
    DEF_ENTER = 40,
    DEF_CONFIRM = 65,
    DEF_EXIT = 30
};

enum { /* the episode's states */
    NORMAL = 0,
    HEARD,
    UNACK,
    ACKED,
    ENDED,
    SEEN
};

enum { /* the pending sliders */
    PEND_ENTER = 0,
    PEND_CONFIRM = 1,
    PEND_NONE = 2
};

enum { /* board buttons */
    BUTTON_SW6 = 0,
    BUTTON_SW5 = 1
};

enum { /* colours, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_BUTTON = 0x5E6878,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF,
    COLOR_ALARM_BG = 0x3A1416
};

enum { /* font sizes, px */
    SMALL_PX = 16,
    BODY_PX = 20,
    TITLE_PX = 24,
    VERDICT_PX = 28
};

static const char MODEL[] =
    "AnomalousVibration"; /* exact name, looked up every run */

/* Where a widget goes and how it looks: MicroPython's keywords. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value;
    int32_t min;
    int32_t max;
} spec_t;

/* A widget the program changes, with what it last sent (put()). */
typedef struct {
    bento_ui_id_t id;
    bool has_text;
    bool has_color;
    uint32_t color;
    char text[TEXT_BYTES];
} shown_t;

/* One board button: one event per press. */
typedef struct {
    uint32_t index;
    bool down;
    bool clicked;
} board_button_t;

/* The model this example runs. */
typedef struct {
    uint32_t index;
    uint32_t class_count;
    int32_t row_count;
    char labels[MAX_CLASSES][LABEL_BYTES];
} model_t;

/* The Stop exception: a text that ends the program. */
typedef struct {
    bool raised;
    char text[TEXT_BYTES];
} stop_t;

/* read_result()'s memory: the last sequence, and when reads began to
 * fail. */
typedef struct {
    bool has_sequence;
    uint32_t last_sequence;
    bool failing;
    uint32_t fail_since_ms;
} reader_t;

/* Per class: Hearing at, Confirm at, clear below. from_board false:
 * the fallback, which never equals a read from the board. */
typedef struct {
    bool from_board;
    uint32_t class_count;
    int32_t level[LEVEL_KINDS][MAX_CLASSES];
} thresholds_t;

/* The last N scores; K of them at or above a level = confirmed. */
typedef struct {
    int32_t count;
    int32_t scores[VOTE_N];
} vote_t;

/* info: NORMAL > HEARD > SEEN for HOLD_MS, no ACK.
 * alarm (latch): NORMAL > HEARD > UNACK > ACKED or ENDED, needs ACK. */
typedef struct {
    bool latch;
    int32_t state;
    int32_t count;
    bool has_time;
    uint32_t time_ms;
    char end[CLOCK_BYTES];
} episode_t;

/* What one result says to the episode. */
typedef struct {
    bool heard;
    bool confirm;
    bool clear;
} signal_t;

/* The sliders moved since the last draw, sent at the next one. */
typedef struct {
    bool waiting[SLIDERS];
    int32_t value[SLIDERS];
    int32_t first;
} pending_t;

typedef struct {
    bento_ui_t *ui;
    bool has_buttons;
    const char *help;
    model_t model;
    stop_t stop;
    reader_t reader;
    bento_ui_id_t heartbeat;
    bento_ui_id_t gauge;
    bento_ui_id_t sliders[SLIDERS];
    shown_t disc;
    shown_t verdict;
    shown_t state;
    shown_t lines[INFO_LINES];
    shown_t levels[SLIDERS];
    shown_t acknowledge_key;
    shown_t status;
    thresholds_t original;
    thresholds_t thresholds;
    bool thresholds_shown;
    vote_t vote;
    episode_t episode;
    pending_t pending;
    board_button_t pause_button;
    board_button_t acknowledge_button;
    int32_t alerts;
    int32_t top;
    int32_t peak;
    int32_t needle;
    bool alert;
    bool paused;
    bool blink;
    uint32_t drawn_ms;
    uint32_t polled_ms;
} pump_t;

/* The program's state, too big for the 8 KB program stack: at file
 * scope, zeroed by the loader on every load and set again at the
 * start of main() (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern pump_t g_pump;
pump_t g_pump;

/* Sleeps, and turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static void raise_stop(stop_t *stop, const char *text)
{
    stop->raised = true;
    (void)snprintf(stop->text, sizeof(stop->text), "%s", text);
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

/* The time now as HH:MM:SS; a refused read leaves 00:00:00. */
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

/* ---- the screen ---- */

/* ui.poll() only to keep the queue empty; a failed poll answers no
 * events. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t add_widget(bento_ui_t *ui, uint8_t type,
                                 const char *text, const spec_t *spec,
                                 bento_ui_id_t *out)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    if (text != NULL) {
        args.text = text;
        args.text_len = (uint32_t)strlen(text);
    }
    args.x = spec->x;
    args.y = spec->y;
    if (spec->w > 0) {
        args.w = spec->w;
        args.h = spec->h;
    }
    args.color = spec->color;
    args.font_px = spec->value;
    args.min_val = spec->min;
    args.max_val = spec->max;
    return bento_ui_create(ui, type, &args, out);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const spec_t *spec, bento_ui_id_t *out)
{
    return add_widget(ui, (uint8_t)UI_WIDGET_LABEL, text, spec, out);
}

/* A dark card with its title in blue. */
static bento_status_t add_card(bento_ui_t *ui, const spec_t *box,
                               const char *title)
{
    const spec_t panel = {box->x,     box->y, box->w,    box->h,
                          COLOR_CARD, 1,      COLOR_DIM, 12};
    const spec_t heading = {(int16_t)(box->x + 14),
                            (int16_t)(box->y + 8),
                            0,
                            0,
                            COLOR_INFO,
                            SMALL_PX,
                            0,
                            0};
    bento_ui_id_t unused;
    bento_status_t status =
        add_widget(ui, (uint8_t)UI_WIDGET_PANEL, NULL, &panel, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, title, &heading, &unused);
    }
    return status;
}

/* put(): send only what changed, as a full display queue drops
 * commands. A setter's refusal was ignored before too (modui.c). */
static void put_text(bento_ui_t *ui, shown_t *widget, const char *text)
{
    if (!widget->has_text || (strcmp(widget->text, text) != 0)) {
        widget->has_text = true;
        (void)snprintf(widget->text, sizeof(widget->text), "%s", text);
        (void)bento_ui_set_text(ui, widget->id, text,
                                (uint32_t)strlen(text));
    }
}

static void put_color(bento_ui_t *ui, shown_t *widget, uint32_t color)
{
    if (!widget->has_color || (widget->color != color)) {
        widget->has_color = true;
        widget->color = color;
        (void)bento_ui_set_color(ui, widget->id, color);
    }
}

static void put(bento_ui_t *ui, shown_t *widget, const char *text,
                uint32_t color)
{
    put_text(ui, widget, text);
    put_color(ui, widget, color);
}

/* missing(): a card that says why nothing runs. */
static bento_status_t show_missing(bento_ui_t *ui, const char *title,
                                   const char *text)
{
    const spec_t card = {96,         110, 600,       180,
                         COLOR_CARD, 1,   COLOR_BAD, 12};
    const spec_t heading = {120, 126, 0, 0, COLOR_BAD, TITLE_PX, 0, 0};
    const spec_t body = {120, 170, 0, 0, COLOR_TEXT, BODY_PX, 0, 0};
    bento_ui_id_t unused;
    bento_ui_id_t lines = 0U;
    bento_status_t status;

    (void)printf("%s\n", text);
    status = bento_ui_screen_default(ui);
    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_widget(ui, (uint8_t)UI_WIDGET_PANEL, NULL, &card,
                            &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, title, &heading, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, " ", &body, &lines);
    }
    if (status == BENTO_OK) {
        /* .text() carries up to 126 bytes, more than a create does */
        (void)bento_ui_set_text(ui, lines, text,
                                (uint32_t)strlen(text));
        drain_events(ui);
    }
    return status;
}

/* ---- board buttons and the AI core ---- */

/* Reads a board button and remembers a new press. No buttons:
 * nothing. */
static bento_status_t sample_button(bool has_buttons,
                                    board_button_t *button)
{
    bool pressed = false;
    bento_status_t status = BENTO_OK;

    if (has_buttons) {
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

/* Waits for a started select or stop; the IDE's Stop cancels it. */
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

/* stop_ai(): an OSError is passed over; only the IDE's Stop is kept. */
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

/* How many rows the firmware compiled in, -1 when it cannot say. */
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

/* m["builtin"] against BUILTIN; None (-1 compiled) passes only
 * "is not False". */
static bool source_matches(int32_t row, int32_t compiled)
{
    const bool want_builtin =
        false; /* BUILTIN: false = from the Store */
    bool matches = want_builtin;

    if (compiled >= 0) {
        const bool builtin = row < compiled;

        matches =
            (builtin && want_builtin) || (!builtin && !want_builtin);
    }
    return matches;
}

static void keep_model(const bento_edgeai_model_info_t *info,
                       int32_t row, model_t *model)
{
    uint32_t length = 0U;
    uint32_t i;

    model->index = (uint32_t)row;
    model->class_count = info->class_count;
    if (model->class_count > (uint32_t)MAX_CLASSES) {
        model->class_count = MAX_CLASSES;
    }
    for (i = 0U; i < model->class_count; i++) {
        if (bento_edgeai_label(model->index, i, model->labels[i],
                               LABEL_BYTES, &length) != BENTO_OK) {
            model->labels[i][0] = '\0';
        }
    }
}

/* One row: counted, and kept when it is the first with this name and
 * source. A row the engine refuses is skipped; a link failure is
 * models()'s OSError. */
static bento_status_t read_row(int32_t row, int32_t compiled,
                               model_t *model, bool *found)
{
    bento_edgeai_model_info_t info;
    bento_status_t status;

    bento_edgeai_model_info_init(&info);
    status = bento_edgeai_model_info((uint32_t)row, &info);
    if ((status == BENTO_E_NOT_READY) ||
        (status == BENTO_E_NOT_FOUND)) {
        status = BENTO_OK;
    } else if (status == BENTO_OK) {
        model->row_count++;
        if (!*found && (strcmp(info.name, MODEL) == 0) &&
            source_matches(row, compiled)) {
            *found = true;
            keep_model(&info, row, model);
        }
    } else {
        /* a link failure */
    }
    return status;
}

/* find_model(): the first row with this name and source, and how many
 * rows there are. */
static bento_status_t find_model(model_t *model, bool *found)
{
    int32_t rows = 0;
    int32_t row;
    const int32_t compiled = compiled_count();
    bento_status_t status = bento_edgeai_count(&rows);

    *found = false;
    model->row_count = 0;
    for (row = 0; (row < rows) && (status == BENTO_OK); row++) {
        status = read_row(row, compiled, model, found);
    }
    return status;
}

/* No model link at all, or a list that cannot be read: not found. */
static void look_up_model(model_t *model, bool *found)
{
    bool supported = false;
    bento_status_t status = bento_edgeai_supported(&supported);

    *found = false;
    if ((status == BENTO_OK) && supported) {
        status = find_model(model, found);
        if (status != BENTO_OK) {
            (void)printf("cannot read models: %s\n",
                         link_error_text(status));
            *found = false;
        }
    }
}

/* edge_ai.select(i): loads and starts it, up to 15 s. */
static bento_status_t select_model(pump_t *app)
{
    char text[TEXT_BYTES];
    bento_status_t status = bento_edgeai_select_start(app->model.index);
    const bool sent = status == BENTO_OK;

    if (sent) {
        status = finish_op();
    }
    if ((status != BENTO_OK) && (status != BENTO_E_CANCELLED)) {
        (void)printf("select: %s\n",
                     sent ? "edge_ai: select not confirmed"
                          : "edge_ai: select send failed");
        (void)snprintf(text, sizeof(text), "Cannot start %s", MODEL);
        raise_stop(&app->stop, text);
        status = BENTO_OK;
    }
    return status;
}

/* start(): on resume (expected_rows > 0), Stop if a model was added
 * or removed meanwhile. */
static bento_status_t start_model(pump_t *app, int32_t expected_rows)
{
    char text[TEXT_BYTES];
    int32_t rows = 0;
    bento_status_t link = BENTO_OK;
    bento_status_t status = BENTO_OK;

    (void)snprintf(text, sizeof(text), "Loading %s ...", MODEL);
    put(app->ui, &app->status, text, COLOR_WARN);
    drain_events(app->ui);
    app->reader.failing = false;
    if (expected_rows != 0) {
        link = bento_edgeai_count(&rows);
    }
    if (link != BENTO_OK) {
        (void)printf("select: %s\n", link_error_text(link));
        (void)snprintf(text, sizeof(text), "Cannot start %s", MODEL);
        raise_stop(&app->stop, text);
    } else if ((expected_rows != 0) && (rows != expected_rows)) {
        raise_stop(&app->stop, "Model list changed - run again");
    } else {
        status = select_model(app);
    }
    if ((status == BENTO_OK) && !app->stop.raised) {
        put(app->ui, &app->status, app->help, COLOR_TEXT);
    }
    return status;
}

/* read_th(i): per class Hearing at, Confirm at, clear below, or
 * the fallback for every class. */
static void read_thresholds(uint32_t index, thresholds_t *out)
{
    bento_edgeai_thresholds_t board;
    uint32_t i;

    bento_edgeai_thresholds_init(&board);
    out->from_board =
        bento_edgeai_thresholds(index, &board) == BENTO_OK;
    out->class_count = MAX_CLASSES;
    for (i = 0U; i < (uint32_t)MAX_CLASSES; i++) {
        out->level[BENTO_EDGEAI_TH_ENTER][i] = DEF_ENTER;
        out->level[BENTO_EDGEAI_TH_CONFIRM][i] = DEF_CONFIRM;
        out->level[BENTO_EDGEAI_TH_EXIT][i] = DEF_EXIT;
    }
    if (out->from_board) {
        out->class_count = board.class_count;
        for (i = 0U; i < (uint32_t)MAX_CLASSES; i++) {
            out->level[BENTO_EDGEAI_TH_ENTER][i] =
                (int32_t)board.enter[i];
            out->level[BENTO_EDGEAI_TH_CONFIRM][i] =
                (int32_t)board.confirm[i];
            out->level[BENTO_EDGEAI_TH_EXIT][i] =
                (int32_t)board.exit[i];
        }
    }
}

/* th != th0, class by class. */
static bool thresholds_differ(const thresholds_t *now,
                              const thresholds_t *before)
{
    bool differ = (now->from_board != before->from_board) ||
                  (now->class_count != before->class_count);
    uint32_t kind;
    uint32_t i;

    for (kind = 0U; (kind < (uint32_t)LEVEL_KINDS) && !differ; kind++) {
        for (i = 0U;
             (i < now->class_count) && (i < (uint32_t)MAX_CLASSES);
             i++) {
            differ = differ ||
                     (now->level[kind][i] != before->level[kind][i]);
        }
    }
    return differ;
}

/* The engine's count of serviced commands, false when it cannot be
 * read. */
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
 * for its command count to move, as the binding does. */
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

/* set_th(): the board keeps clear <= Hearing at <= Confirm at, so
 * what it reads back is what counts. */
static bento_status_t set_threshold(pump_t *app, uint32_t klass,
                                    uint32_t kind, int32_t percent)
{
    uint32_t before = 0U;
    const bool counted = commands_handled(&before);
    bento_status_t status = bento_edgeai_threshold_set(
        app->model.index, klass, kind, (uint32_t)percent);

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
        read_thresholds(app->model.index, &app->thresholds);
        app->thresholds_shown = false; /* a new read-back */
    }
    return status;
}

/* restore_th(): put back what this program changed; enter,
 * confirm, exit in this order undoes the board's clamp. */
static bento_status_t restore_thresholds(pump_t *app)
{
    bento_status_t status = BENTO_OK;
    uint32_t kind;

    if (thresholds_differ(&app->thresholds, &app->original)) {
        for (kind = 0U;
             (kind < (uint32_t)LEVEL_KINDS) && (status == BENTO_OK);
             kind++) {
            status =
                set_threshold(app, ANOMALY_CLASS, kind,
                              app->original.level[kind][ANOMALY_CLASS]);
        }
    }
    return status;
}

/* read_result(): only NEW results of this model; Stop after 3 s of
 * errors. */
static bool read_result(reader_t *reader, uint32_t index, stop_t *stop,
                        bento_edgeai_result_t *result)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    bool fresh = false;

    bento_edgeai_result_init(result);
    if (bento_edgeai_result(result) != BENTO_OK) {
        if (!reader->failing) {
            reader->failing = true;
            reader->fail_since_ms = now_ms;
        }
        if (bento_time_ticks_diff(now_ms, reader->fail_since_ms) >=
            FAIL_STOP_MS) {
            raise_stop(stop, "AI core not answering - run again");
        }
    } else {
        reader->failing = false;
        fresh = result->has_verdict &&
                (!reader->has_sequence ||
                 (result->seq != reader->last_sequence)) &&
                ((uint32_t)result->index == index);
        if (fresh) {
            reader->has_sequence = true;
            reader->last_sequence = result->seq;
        }
    }
    return fresh;
}

/* ---- votes and episodes ---- */

static void vote_add(vote_t *vote, int32_t score)
{
    int32_t i;

    if (vote->count == VOTE_N) {
        for (i = 1; i < VOTE_N; i++) {
            vote->scores[i - 1] = vote->scores[i];
        }
        vote->count--;
    }
    vote->scores[vote->count] = score;
    vote->count++;
}

static int32_t vote_max(const vote_t *vote)
{
    int32_t top = 0;
    int32_t i;

    for (i = 0; i < vote->count; i++) {
        top = (vote->scores[i] > top) ? vote->scores[i] : top;
    }
    return top;
}

/* K of the last N at or above the level. */
static bool vote_hits(const vote_t *vote, int32_t level)
{
    int32_t hits = 0;
    int32_t i;

    for (i = 0; i < vote->count; i++) {
        if (vote->scores[i] >= level) {
            hits++;
        }
    }
    return hits >= VOTE_K;
}

/* N results, all below the level. */
static bool vote_clear(const vote_t *vote, int32_t level)
{
    return (vote->count == VOTE_N) && (vote_max(vote) < level);
}

/* A confirmed result: a new event unless one is latched or the last
 * was sooner than the refractory time. */
static bool confirm_event(episode_t *episode, uint32_t now_ms,
                          int32_t refract_ms)
{
    const bool latched =
        (episode->state == UNACK) || (episode->state == ACKED);
    bool fired = false;

    if (!latched && (!episode->has_time ||
                     (bento_time_ticks_diff(now_ms, episode->time_ms) >=
                      refract_ms))) {
        episode->state = episode->latch ? UNACK : SEEN;
        episode->count++;
        episode->has_time = true;
        episode->time_ms = now_ms;
        fired = true;
    } else if (latched) {
        episode->time_ms = now_ms; /* the episode's last confirm */
    } else {
        /* refractory: nothing */
    }
    return fired;
}

/* Clear below ends an episode; Hearing at marks one. */
static int32_t settle_state(episode_t *episode, int32_t state,
                            const signal_t *signal)
{
    int32_t next = state;

    if (signal->clear) {
        if (state == UNACK) {
            next = ENDED;
            clock_text(episode->end, sizeof(episode->end));
        } else if (state != ENDED) {
            next = NORMAL;
        } else {
            /* ENDED waits for ACK */
        }
    } else if (signal->heard && (state == NORMAL)) {
        next = HEARD;
    } else {
        /* no change */
    }
    return next;
}

/* True once per new event; refractory from the episode's last
 * confirm. */
static bool step_episode(episode_t *episode, const signal_t *signal,
                         uint32_t now_ms, int32_t refract_ms)
{
    const int32_t state = episode->state;
    bool fired = false;

    if (signal->confirm) {
        fired = confirm_event(episode, now_ms, refract_ms);
    }
    if (!fired) {
        episode->state = settle_state(episode, state, signal);
    }
    return fired;
}

static const char *label_of(const pump_t *app, uint32_t klass)
{
    return (klass < app->model.class_count) ? app->model.labels[klass]
                                            : "";
}

/* ---- the screen of this example ---- */

/* The round gauge with its ticks and needle; the props and ticks
 * are setters, whose refusal was ignored before too. */
static bento_status_t build_gauge(pump_t *app)
{
    const spec_t gauge = {28, 96, 170, 170, COLOR_TEXT, 0, 0, PERCENT};
    bento_status_t status = add_widget(
        app->ui, (uint8_t)UI_WIDGET_SCALE, NULL, &gauge, &app->gauge);

    if (status == BENTO_OK) {
        (void)bento_ui_set_prop(app->ui, app->gauge,
                                (uint8_t)BENTO_UI_PROP_SCALE_MODE,
                                BENTO_UI_SCALE_ROUND_IN);
        (void)bento_ui_set_ticks(app->ui, app->gauge, SCALE_TICKS,
                                 SCALE_MAJOR_EVERY);
        (void)bento_ui_set_prop(
            app->ui, app->gauge,
            (uint8_t)BENTO_UI_PROP_SCALE_NEEDLE_COLOR, COLOR_TEXT);
    }
    return status;
}

static bento_status_t build_info(pump_t *app)
{
    bento_status_t status = BENTO_OK;
    int32_t j;

    for (j = 0; (j < INFO_LINES) && (status == BENTO_OK); j++) {
        const spec_t line = {214,        (int16_t)(194 + (24 * j)),
                             0,          0,
                             COLOR_TEXT, SMALL_PX,
                             0,          0};

        status = add_label(app->ui, " ", &line, &app->lines[j].id);
    }
    return status;
}

static bento_status_t build_anomaly_card(pump_t *app)
{
    const spec_t card = {12, 64, 470, 272, 0, 0, 0, 0};
    const spec_t disc = {214, 100,       56, 56, COLOR_DIM,
                         0,   COLOR_DIM, 28}; /* a round light */
    const spec_t verdict = {280,       108,        0, 0,
                            COLOR_DIM, VERDICT_PX, 0, 0};
    const spec_t state = {214, 166, 0, 0, COLOR_DIM, SMALL_PX, 0, 0};
    bento_status_t status = add_card(app->ui, &card, "Anomaly");

    if (status == BENTO_OK) {
        status = build_gauge(app);
    }
    if (status == BENTO_OK) {
        status = add_widget(app->ui, (uint8_t)UI_WIDGET_PANEL, NULL,
                            &disc, &app->disc.id);
    }
    if (status == BENTO_OK) {
        status =
            add_label(app->ui, "Normal", &verdict, &app->verdict.id);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, " ", &state, &app->state.id);
    }
    if (status == BENTO_OK) {
        status = build_info(app);
    }
    return status;
}

/* A label and a slider for Hearing at (y 96) and Confirm at (y 150);
 * a Slider takes its colour after creation. */
static bento_status_t build_sliders(pump_t *app)
{
    static const int16_t rows_y[SLIDERS] = {96, 150};
    bento_status_t status = BENTO_OK;
    int32_t k;

    for (k = 0; (k < SLIDERS) && (status == BENTO_OK); k++) {
        const spec_t level = {508,        rows_y[k], 0, 0,
                              COLOR_TEXT, SMALL_PX,  0, 0};
        const spec_t slider = {
            520, (int16_t)(rows_y[k] + 28), 250, 12, 0, 0, 0, PERCENT};

        status = add_label(app->ui, " ", &level, &app->levels[k].id);
        if (status == BENTO_OK) {
            status = add_widget(app->ui, (uint8_t)UI_WIDGET_SLIDER,
                                NULL, &slider, &app->sliders[k]);
        }
        if (status == BENTO_OK) {
            (void)bento_ui_set_color(app->ui, app->sliders[k],
                                     COLOR_INFO); /* a setter */
        }
    }
    return status;
}

static bento_status_t build_thresholds_card(pump_t *app)
{
    const spec_t card = {494, 64, 294, 272, 0, 0, 0, 0};
    const spec_t key = {508,          266,      266, 60,
                        COLOR_BUTTON, TITLE_PX, 0,   0};
    bento_status_t status = add_card(app->ui, &card, "Thresholds");

    if (status == BENTO_OK) {
        status = build_sliders(app);
    }
    if (status == BENTO_OK) {
        status = add_widget(app->ui, (uint8_t)UI_WIDGET_BUTTON, "ACK",
                            &key, &app->acknowledge_key.id);
    }
    return status;
}

static bento_status_t build_screen(pump_t *app)
{
    const spec_t title = {12, 4, 0, 0, COLOR_TEXT, TITLE_PX, 0, 0};
    const spec_t heartbeat = {756, 6, 26, 26, COLOR_OK, 0, 0, 0};
    const spec_t note = {12, 38, 0, 0, COLOR_DIM, SMALL_PX, 0, 0};
    const spec_t status_line = {12,         346,      0, 0,
                                COLOR_WARN, SMALL_PX, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status = bento_ui_screen_default(app->ui);

    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, "Pump Health", &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_widget(app->ui, (uint8_t)UI_WIDGET_LED, NULL,
                            &heartbeat, &app->heartbeat);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui,
                           "Detects: vibration unlike its training "
                           "machine. Cannot: name the "
                           "fault.",
                           &note, &unused);
    }
    if (status == BENTO_OK) {
        status = build_anomaly_card(app);
    }
    if (status == BENTO_OK) {
        status = build_thresholds_card(app);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, " ", &status_line, &app->status.id);
    }
    if (status == BENTO_OK) {
        drain_events(app->ui);
    }
    return status;
}

/* The round light: UNACK and ENDED flash at 1 Hz. */
static uint32_t disc_color(int32_t state, bool blink)
{
    uint32_t color = COLOR_DIM;

    if (state == HEARD) {
        color = COLOR_WARN;
    } else if (state == UNACK) {
        color = blink ? COLOR_BAD : COLOR_ALARM_BG;
    } else if (state == ACKED) {
        color = COLOR_BAD;
    } else if (state == ENDED) {
        color = blink ? COLOR_DIM : COLOR_ALARM_BG;
    } else {
        /* NORMAL: dim */
    }
    return color;
}

/* The light, the state line, the verdict and the ACK button;
 * UNACK and ENDED flash at 1 Hz. */
static void show_state(pump_t *app, int32_t percent)
{
    static const char *const verdicts[] = {"Normal", "ALERT", "DANGER"};
    static const uint32_t verdict_colors[] = {COLOR_DIM, COLOR_WARN,
                                              COLOR_BAD};
    static const uint32_t state_colors[] = {
        COLOR_DIM, COLOR_WARN, COLOR_BAD, COLOR_BAD, COLOR_TEXT};
    const int32_t state =
        (app->episode.state <= ENDED) ? app->episode.state : NORMAL;
    const bool danger = (state == UNACK) || (state == ACKED);
    const int32_t level = danger ? 2 : (app->alert ? 1 : 0);
    const bool flashing = (state == UNACK) || (state == ENDED);
    char text[TEXT_BYTES];

    put_color(app->ui, &app->disc, disc_color(state, app->blink));
    if (state == NORMAL) {
        (void)snprintf(text, sizeof(text), "score %" PRId32 "%%",
                       percent);
    } else if (state == HEARD) {
        (void)snprintf(text, sizeof(text),
                       "! ALERT - score %" PRId32 "%%", percent);
    } else if (state == ENDED) {
        (void)snprintf(text, sizeof(text), "ENDED %s - tap ACK",
                       app->episode.end);
    } else {
        (void)snprintf(text, sizeof(text), "%s",
                       (state == UNACK) ? "!! DANGER - tap ACK"
                                        : "ACKED - still high");
    }
    put(app->ui, &app->state, text, state_colors[state]);
    put(app->ui, &app->verdict, verdicts[level], verdict_colors[level]);
    put_color(app->ui, &app->acknowledge_key,
              flashing ? COLOR_BAD : COLOR_BUTTON);
}

/* The needle prop: (length << 16) | value, in unsigned bits. */
static int32_t needle_word(int32_t percent)
{
    const uint32_t length = (uint32_t)NEEDLE_PX;
    const uint32_t shift = (uint32_t)NEEDLE_SHIFT;
    const uint32_t word = (length << shift) | (uint32_t)percent;

    return (int32_t)word;
}

/* The counts, the needle and, after a new read-back, the sliders. */
static void show_numbers(pump_t *app, int32_t percent)
{
    const int32_t enter =
        app->thresholds.level[BENTO_EDGEAI_TH_ENTER][ANOMALY_CLASS];
    const int32_t confirm =
        app->thresholds.level[BENTO_EDGEAI_TH_CONFIRM][ANOMALY_CLASS];
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "Alerts %" PRId32 " | Dangers %" PRId32, app->alerts,
                   app->episode.count);
    put_text(app->ui, &app->lines[0], text);
    (void)snprintf(text, sizeof(text), "Peak %" PRId32 "%%", app->top);
    put_text(app->ui, &app->lines[1], text);
    if (app->needle != percent) {
        app->needle = percent;
        (void)bento_ui_set_prop(app->ui, app->gauge,
                                (uint8_t)BENTO_UI_PROP_SCALE_NEEDLE,
                                needle_word(percent));
    }
    if (!app->thresholds_shown) {
        app->thresholds_shown = true;
        (void)snprintf(text, sizeof(text),
                       "Hearing at %" PRId32 "%% = Alert", enter);
        put_text(app->ui, &app->levels[PEND_ENTER], text);
        (void)snprintf(text, sizeof(text),
                       "Confirm at %" PRId32 "%% = Danger", confirm);
        put_text(app->ui, &app->levels[PEND_CONFIRM], text);
        (void)bento_ui_set_value(app->ui, app->sliders[PEND_ENTER],
                                 enter); /* a setter */
        (void)bento_ui_set_value(app->ui, app->sliders[PEND_CONFIRM],
                                 confirm);
    }
}

static void draw(pump_t *app)
{
    const int32_t percent = (app->peak > 0) ? app->peak : 0;

    /* heartbeat; peak -1 = no new result */
    (void)bento_ui_set_value(app->ui, app->heartbeat,
                             (app->blink && (app->peak >= 0)) ? 1 : 0);
    show_state(app, percent);
    show_numbers(app, percent);
}

/* ---- the main program ---- */

/* ep.ack(): UNACK becomes ACKED, ENDED becomes NORMAL. */
static void acknowledge(episode_t *episode)
{
    if (episode->state == UNACK) {
        episode->state = ACKED;
    } else if (episode->state == ENDED) {
        episode->state = NORMAL;
    } else {
        /* nothing to acknowledge */
    }
}

static void put_pending(pending_t *pending, int32_t which,
                        int32_t value)
{
    if (pending->first == PEND_NONE) {
        pending->first =
            which; /* a key already there keeps its place */
    }
    pending->waiting[which] = true;
    pending->value[which] = value;
}

static void clear_pending(pending_t *pending)
{
    pending->waiting[PEND_ENTER] = false;
    pending->waiting[PEND_CONFIRM] = false;
    pending->first = PEND_NONE;
}

/* The pending sliders, in MicroPython's dict order: the key put in
 * second sits before the first one. */
static bento_status_t send_pending(pump_t *app)
{
    const pending_t *pending = &app->pending;
    const int32_t first = pending->first;
    const int32_t second =
        (first == PEND_ENTER) ? PEND_CONFIRM : PEND_ENTER;
    bento_status_t status = BENTO_OK;

    if (first != PEND_NONE) {
        if (pending->waiting[second]) {
            status = set_threshold(app, ANOMALY_CLASS, (uint32_t)second,
                                   pending->value[second]);
        }
        if (status == BENTO_OK) {
            status = set_threshold(app, ANOMALY_CLASS, (uint32_t)first,
                                   pending->value[first]);
        }
        clear_pending(&app->pending);
    }
    return status;
}

/* Class 1 is the anomaly. Alert is not latched; Danger is. */
static void poll_model(pump_t *app, uint32_t now_ms)
{
    bento_edgeai_result_t result;
    char clock[CLOCK_BYTES];

    if (read_result(&app->reader, app->model.index, &app->stop,
                    &result)) {
        const int32_t percent =
            (int32_t)result.score_pct[ANOMALY_CLASS];
        const int32_t (*level)[MAX_CLASSES] = app->thresholds.level;
        const bool was_alert = app->alert;
        signal_t signal;

        vote_add(&app->vote, percent);
        app->peak = (percent > app->peak) ? percent : app->peak;
        app->top = (percent > app->top) ? percent : app->top;
        signal.heard =
            percent >= level[BENTO_EDGEAI_TH_ENTER][ANOMALY_CLASS];
        signal.confirm = vote_hits(
            &app->vote, level[BENTO_EDGEAI_TH_CONFIRM][ANOMALY_CLASS]);
        signal.clear = vote_clear(
            &app->vote, level[BENTO_EDGEAI_TH_EXIT][ANOMALY_CLASS]);
        app->alert = signal.heard || (app->alert && !signal.clear);
        if (app->alert && !was_alert) {
            app->alerts++;
            clock_text(clock, sizeof(clock));
            (void)printf("%s alert %" PRId32 "%%\n", clock, percent);
        }
        if (step_episode(&app->episode, &signal, now_ms, REFRACT_MS)) {
            clock_text(clock, sizeof(clock));
            (void)printf("%s %s %" PRId32 "%% #%" PRId32 "\n", clock,
                         label_of(app, ANOMALY_CLASS),
                         vote_max(&app->vote), app->episode.count);
        }
    }
}

/* SW6 or any event on ACK (the original reads only the handle); a
 * slider's new value waits for the next draw. A failed poll answers
 * no events. */
static bool read_touch(pump_t *app)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bool acknowledged = button_clicked(&app->acknowledge_button);

    if (bento_ui_poll(app->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        const bento_ui_id_t widget = events[i].id;

        acknowledged =
            acknowledged || (widget == app->acknowledge_key.id);
        if (events[i].type == (uint8_t)BENTO_UI_EVENT_VALUE_CHANGED) {
            put_pending(&app->pending,
                        (widget == app->sliders[PEND_ENTER])
                            ? PEND_ENTER
                            : PEND_CONFIRM,
                        events[i].value);
        }
    }
    return acknowledged;
}

/* SW5: pause (the AI core is released) or resume. */
static bento_status_t toggle_pause(pump_t *app)
{
    bento_status_t status;

    app->paused = !app->paused;
    app->vote.count = 0;
    app->alert = false;
    if (app->paused) {
        status = stop_ai();
        put(app->ui, &app->status, "Paused - SW5 resumes", COLOR_WARN);
    } else {
        status = start_model(app, app->model.row_count);
    }
    return status;
}

static bento_status_t read_controls(pump_t *app)
{
    bento_status_t status =
        sample_button(app->has_buttons, &app->pause_button);

    if (status == BENTO_OK) {
        status =
            sample_button(app->has_buttons, &app->acknowledge_button);
    }
    if ((status == BENTO_OK) && read_touch(app)) {
        acknowledge(&app->episode);
    }
    if ((status == BENTO_OK) && button_clicked(&app->pause_button)) {
        status = toggle_pause(app);
    }
    return status;
}

/* One pass of the main loop. */
static bento_status_t run_round(pump_t *app)
{
    uint32_t now_ms;
    bento_status_t status = read_controls(app);

    now_ms = bento_time_ticks_ms();
    if ((status == BENTO_OK) && !app->stop.raised && !app->paused &&
        (bento_time_ticks_diff(now_ms, app->polled_ms) >= POLL_MS)) {
        app->polled_ms = now_ms;
        poll_model(app, now_ms);
    }
    if ((status == BENTO_OK) && !app->stop.raised &&
        (bento_time_ticks_diff(now_ms, app->drawn_ms) >= DRAW_MS)) {
        app->drawn_ms = now_ms;
        app->blink = !app->blink;
        status = send_pending(app);
        draw(app);
        app->peak = -1;
    }
    if ((status == BENTO_OK) && !app->stop.raised) {
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* run(): the model started, then the loop; the finally puts the
 * board's own thresholds back, as the sliders' numbers would
 * outlive the program. */
static bento_status_t run_pump(pump_t *app)
{
    bento_status_t status;
    bento_status_t restore_status;

    read_thresholds(app->model.index, &app->original);
    app->thresholds = app->original;
    app->thresholds_shown = false;
    app->needle = -1;
    app->peak = -1;
    app->episode.latch = true;
    clear_pending(&app->pending);
    status = start_model(app, 0);
    app->drawn_ms = bento_time_ticks_ms();
    app->polled_ms = app->drawn_ms;
    while ((status == BENTO_OK) && !app->stop.raised) {
        status = run_round(app);
    }
    if (status == BENTO_E_CANCELLED) { /* the finally still runs */
        (void)bento_runtime_acknowledge_stop();
    }
    restore_status = restore_thresholds(app);
    return (status == BENTO_OK) ? restore_status : status;
}

/* The screen, the run, then the except and the finally. */
static bento_status_t run_program(pump_t *app)
{
    bento_status_t status = build_screen(app);
    bento_status_t stop_status;

    if (status == BENTO_OK) {
        status = run_pump(app);
        if (status == BENTO_E_CANCELLED) { /* the finally still runs */
            (void)bento_runtime_acknowledge_stop();
        }
        if ((status == BENTO_OK) && app->stop.raised) {
            put(app->ui, &app->status, app->stop.text, COLOR_BAD);
            (void)printf("%s\n", app->stop.text);
        }
        stop_status = stop_ai(); /* always release the AI core */
        status = (status == BENTO_E_CANCELLED) ? stop_status : status;
    }
    return status;
}

int main(void)
{
    static const char missing_text[] =
        "Deploy AnomalousVibration\n"
        "from edgeai-store.tesaiot.dev\n"
        "(sign in with GitHub) on a Dev Kit.";
    pump_t *app = &g_pump;
    bool found = false;
    bento_status_t status;

    (void)memset(app, 0, sizeof(*app));
    app->ui = bento_runtime_get_ui();
    app->has_buttons = bento_buttons_count() > 0U;
    app->help = app->has_buttons ? "Running - SW6 = ACK, SW5 = pause"
                                 : "Running";
    app->pause_button.index = BUTTON_SW5;
    app->acknowledge_button.index = BUTTON_SW6;
    look_up_model(&app->model, &found);
    if (!found) {
        status = show_missing(app->ui, "Model not on this board",
                              missing_text);
    } else {
        status = run_program(app);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
