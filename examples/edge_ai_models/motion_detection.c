/*
 * motion_detection.c -- สวิตช์ท่าทาง (Gesture Switch) ด้วยโมเดลในตัว
 * "Motion Detection"
 *
 * วาดวงกลมกลางอากาศด้วยบอร์ดเพื่อเปิด/ปิดหลอดไฟบนจอ โมเดลแยกสามท่า:
 * idle, circle และ shaking ท่าหนึ่งนับเมื่ออยู่อันดับหนึ่งที่เกณฑ์
 * Confirm at นานครบเวลา Dwell (0, 0.5 หรือ 1 วินาที) แถบบอกเวลาที่
 * รออยู่ เขย่าบอร์ดแล้วบรรทัดล่างเตือน "tamper?" ปุ่ม SW6 (บน) หรือ
 * แตะปุ่ม Dwell = เปลี่ยนเวลา Dwell ปุ่ม SW5 (ล่าง) = หยุดชั่วคราว/
 * ทำต่อ จบโปรแกรมแล้วคืนแกน AI เสมอ
 *
 * Draw a circle in the air with the board to flip the lamp on the
 * screen. The model tells three gestures apart: idle, circle and
 * shaking. A gesture counts once it has been on top at Confirm at
 * for the dwell time (0, 0.5 or 1 s); a bar shows the wait. Shaking
 * the board warns "tamper?" on the bottom line. SW6 (upper) or a
 * tap on the Dwell button = the next dwell time; SW5 (lower) =
 * pause and resume. The AI core is always released at the end.
 *
 * Twin of motion_detection.py
 * (embedded-systems-for-aiot-developer/examples/edge_ai_models/motion_detection.py).
 * Style: CLEAN_C.md. Written new; the library twin
 * devkit-21-ai-model-lab gave the style and the Edge AI calls.
 * Differs from the original:
 *   - m["builtin"] is read as (row < diag.compiled_models); when the
 *     board cannot answer diag it is None (the Emulator's case), so
 *     BUILTIN = True accepts a built-in row or an unknown one.
 *   - edge_ai.models() reads every row; here the rows are read one
 *     by one and a row the engine refuses is skipped. The model's
 *     labels are read with bento_edgeai_label(), "" when one cannot
 *     be read, as the binding does.
 *   - `import buttons` fails on the Eva Kit; here a board with no
 *     buttons (bento_buttons_count() 0) is that board. A board
 *     without the model link (bento_edgeai_supported() false) shows
 *     the "not on this board" card.
 *   - edge_ai.select() and stop() wait inside the binding; here they
 *     are started and polled every 20 ms, so the IDE's Stop ends a
 *     wait at once. A failed select prints the binding's words
 *     ("edge_ai: select send failed" or "select not confirmed").
 *   - The Stop exception is a stop text kept in the program state:
 *     the loop ends, the text goes on the status line in red and is
 *     printed, and the AI core is released, as the finally does.
 *   - In the BENTO Emulator, Edge AI from C is not available yet:
 *     the program says so and then shows the "Model not on this
 *     board" card, as the .py does there.
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
    MS_PER_S = 1000
};

enum { /* sizes and limits */
    TEXT_BYTES = 128,
    CLOCK_BYTES = 36, /* "HH:MM:SS"; three int32_t at worst */
    MAX_CLASSES = BENTO_EDGEAI_MAX_CLASSES,
    LABEL_BYTES = BENTO_EDGEAI_LABEL_LEN,
    CHIP_COUNT = 3,
    DWELL_CHOICES = 3,
    PERCENT = 100,
    DEF_CONFIRM = 65, /* Confirm at when the board gives none */
    NO_CLASS = -1     /* "Not sure" */
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
    COLOR_INFO = 0x4A9EFF
};

enum { /* font sizes, px */
    SMALL_PX = 16,
    CHIP_PX = 20,
    TITLE_PX = 24,
    VERDICT_PX = 28
};

static const char MODEL[] =
    "Motion Detection"; /* exact name, looked up every run */
static const char NOT_SURE[] = "Not sure";
/* ms on top before a gesture counts (a USB cable tug moves it too) */
static const int32_t DWELLS_MS[DWELL_CHOICES] = {0, 500, 1000};
/* the classes this example needs, and its chips */
static const char *const CHIPS[CHIP_COUNT] = {"idle", "circle",
                                              "shaking"};

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

/* A class counts after the dwell on top at Confirm at; NO_CLASS = Not
 * sure. */
typedef struct {
    int32_t accepted;
    int32_t candidate;
    uint32_t since_ms;
    int32_t chatter;
    int32_t count;
    int32_t bar_value;
    int32_t chip_of[MAX_CLASSES];
} dwell_t;

/* The newest result, for the hint line: top class, %, Confirm at. */
typedef struct {
    bool fresh;
    int32_t klass;
    int32_t percent;
    int32_t confirm;
} top_t;

typedef struct {
    bento_ui_t *ui;
    bool has_buttons;
    const char *help;
    model_t model;
    stop_t stop;
    reader_t reader;
    bento_ui_id_t heartbeat;
    bento_ui_id_t bar;
    shown_t verdict;
    shown_t chip_panels[CHIP_COUNT];
    shown_t chip_labels[CHIP_COUNT];
    shown_t candidate;
    shown_t lamp;
    shown_t lamp_line;
    shown_t dwell_choice;
    shown_t status;
    dwell_t dwell;
    top_t top;
    board_button_t pause_button;
    board_button_t dwell_button;
    int32_t confirm[MAX_CLASSES];
    int32_t dwell_index;
    int32_t toggles;
    bool lamp_on;
    bool paused;
    bool blink;
    uint32_t drawn_ms;
    uint32_t polled_ms;
} gesture_t;

/* The program's state, too big for the 8 KB program stack: at file
 * scope, zeroed by the loader on every load and set again at the
 * start of main() (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern gesture_t g_gesture;
gesture_t g_gesture;

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

/* A real number as '%.1f' writes it on this board. */
static void tenths_text(bento_real_t value, char *text)
{
    uint32_t length = 0U;

    if (bento_fmt_real_precision(text, BENTO_FMT_BUF_SIZE, value, 1,
                                 &length) != BENTO_OK) {
        text[0] = '\0';
    }
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
    const spec_t body = {120, 170, 0, 0, COLOR_TEXT, CHIP_PX, 0, 0};
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
        true; /* BUILTIN: false = from the Store */
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

/* No model link at all, or a list that cannot be read: not found.
 * Answers true in the BENTO Emulator, whose C runtime has no Edge
 * AI yet (every call answers BENTO_E_UNSUPPORTED there; a board
 * without the model link answers BENTO_OK and false). */
static bool look_up_model(model_t *model, bool *found)
{
    bool supported = false;
    bento_status_t status = bento_edgeai_supported(&supported);
    const bool in_emulator = status == BENTO_E_UNSUPPORTED;

    *found = false;
    if ((status == BENTO_OK) && supported) {
        status = find_model(model, found);
        if (status != BENTO_OK) {
            (void)printf("cannot read models: %s\n",
                         link_error_text(status));
            *found = false;
        }
    }
    return in_emulator;
}

/* The BENTO Emulator's note, under the "not on this board" card. */
static bento_status_t show_emulator_note(bento_ui_t *ui)
{
    static const char note[] = "Edge AI จาก C ยังไม่มีใน BENTO Emulator "
                               "ให้รันบน Dev Kit หรือ Eva Kit";
    const spec_t place = {96, 300, 600, 60, COLOR_WARN, 20, 0, 0};
    bento_ui_id_t unused;
    const bento_status_t status = add_label(ui, note, &place, &unused);

    (void)printf(
        "%s / Edge AI from C is not in the BENTO Emulator yet: "
        "run it on a Dev Kit or Eva Kit.\n",
        note);
    drain_events(ui);
    return status;
}

/* edge_ai.select(i): loads and starts it, up to 15 s. */
static bento_status_t select_model(gesture_t *app)
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
static bento_status_t start_model(gesture_t *app, int32_t expected_rows)
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

/* read_th(i)[1]: Confirm at per class, or the fallback for all. */
static void read_confirm(gesture_t *app)
{
    bento_edgeai_thresholds_t thresholds;
    bool known;
    uint32_t i;

    bento_edgeai_thresholds_init(&thresholds);
    known = bento_edgeai_thresholds(app->model.index, &thresholds) ==
            BENTO_OK;
    for (i = 0U; i < (uint32_t)MAX_CLASSES; i++) {
        app->confirm[i] =
            known ? (int32_t)thresholds.confirm[i] : DEF_CONFIRM;
    }
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

/* ---- the dwell ---- */

/* Class c's name, "Not sure" for NO_CLASS. */
static const char *class_name(const gesture_t *app, int32_t klass)
{
    const char *name = NOT_SURE;

    if ((klass >= 0) && ((uint32_t)klass < app->model.class_count)) {
        name = app->model.labels[klass];
    }
    return name;
}

/* The chip of class c, -1 for none. */
static int32_t chip_of(const gesture_t *app, int32_t klass)
{
    int32_t chip = -1;

    if ((klass >= 0) && ((uint32_t)klass < app->model.class_count)) {
        chip = app->dwell.chip_of[klass];
    }
    return chip;
}

/* The chip that shows this label, -1 for none. */
static int32_t chip_index(const char *label)
{
    int32_t chip = -1;
    int32_t j;

    for (j = 0; (j < CHIP_COUNT) && (chip < 0); j++) {
        if (strcmp(label, CHIPS[j]) == 0) {
            chip = j;
        }
    }
    return chip;
}

static void start_dwell(gesture_t *app)
{
    dwell_t *dwell = &app->dwell;
    uint32_t i;

    dwell->accepted = NO_CLASS;
    dwell->candidate = NO_CLASS;
    for (i = 0U; i < app->model.class_count; i++) {
        dwell->chip_of[i] = chip_index(app->model.labels[i]);
    }
}

/* One console line per change of the accepted state. */
static void print_change(const gesture_t *app, int32_t klass,
                         int32_t percent)
{
    char clock[CLOCK_BYTES];

    clock_text(clock, sizeof(clock));
    (void)printf("%s %s %" PRId32 "%%", clock, class_name(app, klass),
                 percent);
    if (klass >= 0) {
        (void)printf(" #%" PRId32, app->dwell.count);
    }
    (void)printf("\n");
}

/* True when the accepted state changes. */
static bool step_dwell(gesture_t *app, int32_t klass, int32_t percent,
                       uint32_t now_ms)
{
    dwell_t *dwell = &app->dwell;
    bool changed = false;

    if (klass != dwell->candidate) {
        if (dwell->candidate != dwell->accepted) {
            dwell->chatter++; /* dropped before its dwell = chatter */
        }
        dwell->candidate = klass;
        dwell->since_ms = now_ms;
    }
    if ((klass != dwell->accepted) &&
        (bento_time_ticks_diff(now_ms, dwell->since_ms) >=
         DWELLS_MS[app->dwell_index])) {
        dwell->accepted = klass;
        if (klass >= 0) {
            dwell->count++;
        }
        print_change(app, klass, percent);
        changed = true;
    }
    return changed;
}

/* ---- the screen of this example ---- */

static bento_status_t build_chips(gesture_t *app)
{
    bento_status_t status = BENTO_OK;
    int32_t j;

    for (j = 0; (j < CHIP_COUNT) && (status == BENTO_OK); j++) {
        const int16_t x = (int16_t)(28 + (148 * j));
        const spec_t panel = {x,          170, 140,       56,
                              COLOR_CARD, 1,   COLOR_DIM, 8};
        const spec_t name = {(int16_t)(x + 10), 186,     0, 0,
                             COLOR_DIM,         CHIP_PX, 0, 0};

        status = add_widget(app->ui, (uint8_t)UI_WIDGET_PANEL, NULL,
                            &panel, &app->chip_panels[j].id);
        if (status == BENTO_OK) {
            status = add_label(app->ui, CHIPS[j], &name,
                               &app->chip_labels[j].id);
        }
    }
    return status;
}

static bento_status_t build_now_card(gesture_t *app)
{
    const spec_t card = {12, 64, 470, 272, 0, 0, 0, 0};
    const spec_t heartbeat = {756, 6, 26, 26, COLOR_OK, 0, 0, 0};
    const spec_t verdict = {28, 96, 0, 0, COLOR_DIM, VERDICT_PX, 0, 0};
    const spec_t bar = {28, 236, 436, 10, 0, 0, 0, PERCENT};
    const spec_t candidate = {28, 250, 0, 0, COLOR_DIM, SMALL_PX, 0, 0};
    bento_status_t status = add_card(app->ui, &card, "Now");

    if (status == BENTO_OK) {
        status = add_widget(app->ui, (uint8_t)UI_WIDGET_LED, NULL,
                            &heartbeat, &app->heartbeat);
    }
    if (status == BENTO_OK) {
        status =
            add_label(app->ui, NOT_SURE, &verdict, &app->verdict.id);
    }
    if (status == BENTO_OK) {
        status = build_chips(app);
    }
    if (status == BENTO_OK) {
        status = add_widget(app->ui, (uint8_t)UI_WIDGET_BAR, NULL, &bar,
                            &app->bar);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_color(app->ui, app->bar,
                                 COLOR_TEXT); /* a setter: ignored */
        status =
            add_label(app->ui, " ", &candidate, &app->candidate.id);
    }
    return status;
}

static bento_status_t build_lamp_card(gesture_t *app)
{
    const spec_t card = {494, 64, 294, 272, 0, 0, 0, 0};
    const spec_t lamp = {560,        100, 120,       120,
                         COLOR_CARD, 2,   COLOR_DIM, 60};
    const spec_t line = {508, 236, 0, 0, COLOR_TEXT, SMALL_PX, 0, 0};
    const spec_t choice = {508,          266,     266, 60,
                           COLOR_BUTTON, CHIP_PX, 0,   0};
    const spec_t status_line = {12,         346,      0, 0,
                                COLOR_WARN, SMALL_PX, 0, 0};
    bento_status_t status = add_card(app->ui, &card, "Lamp");

    if (status == BENTO_OK) {
        status = add_widget(app->ui, (uint8_t)UI_WIDGET_PANEL, NULL,
                            &lamp, &app->lamp.id);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, " ", &line, &app->lamp_line.id);
    }
    if (status == BENTO_OK) {
        status =
            add_widget(app->ui, (uint8_t)UI_WIDGET_BUTTON,
                       "Dwell 0.5 s", &choice, &app->dwell_choice.id);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, " ", &status_line, &app->status.id);
    }
    return status;
}

static bento_status_t build_screen(gesture_t *app)
{
    const spec_t title = {12, 4, 0, 0, COLOR_TEXT, TITLE_PX, 0, 0};
    const spec_t note = {12, 38, 0, 0, COLOR_DIM, SMALL_PX, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status = bento_ui_screen_default(app->ui);

    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, "Gesture Switch", &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui,
                           "Detects: idle, circle, shaking. Cannot: "
                           "other gestures or who "
                           "moved it.",
                           &note, &unused);
    }
    if (status == BENTO_OK) {
        status = build_now_card(app);
    }
    if (status == BENTO_OK) {
        status = build_lamp_card(app);
    }
    if (status == BENTO_OK) {
        drain_events(app->ui);
    }
    return status;
}

/* Accepted = white, candidate = grey-blue. */
static void show_chips(gesture_t *app)
{
    static const uint32_t panel_colors[] = {COLOR_CARD, COLOR_TEXT,
                                            COLOR_BUTTON};
    static const uint32_t text_colors[] = {COLOR_DIM, COLOR_CARD,
                                           COLOR_TEXT};
    const int32_t accepted_chip = chip_of(app, app->dwell.accepted);
    const int32_t candidate_chip = chip_of(app, app->dwell.candidate);
    int32_t j;

    for (j = 0; j < CHIP_COUNT; j++) {
        int32_t look = 0;

        if (j == accepted_chip) {
            look = 1;
        } else if (j == candidate_chip) {
            look = 2;
        } else {
            /* neither: plain */
        }
        put_color(app->ui, &app->chip_panels[j], panel_colors[look]);
        put_color(app->ui, &app->chip_labels[j], text_colors[look]);
    }
}

/* The dwell bar and the hint under it. */
static void show_candidate(gesture_t *app, uint32_t now_ms)
{
    const dwell_t *dwell = &app->dwell;
    const int32_t dwell_ms = DWELLS_MS[app->dwell_index];
    char text[TEXT_BYTES];
    char elapsed_text[BENTO_FMT_BUF_SIZE];
    char dwell_text[BENTO_FMT_BUF_SIZE];
    int32_t elapsed_ms = 0;
    int64_t share;
    int32_t value;

    if ((dwell->candidate != dwell->accepted) && (dwell_ms != 0)) {
        elapsed_ms = bento_time_ticks_diff(now_ms, dwell->since_ms);
    }
    share = ((int64_t)elapsed_ms * PERCENT) /
            ((dwell_ms != 0) ? dwell_ms : 1);
    value = (share < PERCENT) ? (int32_t)share : PERCENT;
    if (value != app->dwell.bar_value) {
        app->dwell.bar_value = value;
        (void)bento_ui_set_value(app->ui, app->bar, value);
    }
    if (elapsed_ms != 0) {
        tenths_text((bento_real_t)elapsed_ms / (bento_real_t)MS_PER_S,
                    elapsed_text);
        tenths_text((bento_real_t)dwell_ms / (bento_real_t)MS_PER_S,
                    dwell_text);
        (void)snprintf(text, sizeof(text), "%s %s/%s s",
                       class_name(app, dwell->candidate), elapsed_text,
                       dwell_text);
        put_text(app->ui, &app->candidate, text);
    } else if (app->top.fresh) {
        (void)snprintf(text, sizeof(text),
                       "top %s %" PRId32 "%% | Confirm at %" PRId32
                       "%%",
                       class_name(app, app->top.klass),
                       app->top.percent, app->top.confirm);
        put_text(app->ui, &app->candidate, text);
    } else {
        /* nothing new to say */
    }
}

static void draw(gesture_t *app, uint32_t now_ms)
{
    const int32_t accepted = app->dwell.accepted;
    char text[TEXT_BYTES];

    (void)bento_ui_set_value(app->ui, app->heartbeat,
                             (app->blink && app->top.fresh) ? 1 : 0);
    put(app->ui, &app->verdict, class_name(app, accepted),
        (accepted >= 0) ? COLOR_TEXT : COLOR_DIM);
    show_chips(app);
    show_candidate(app, now_ms);
    (void)snprintf(text, sizeof(text),
                   "Lamp %s | toggles %" PRId32 " | chatter %" PRId32,
                   app->lamp_on ? "ON" : "OFF", app->toggles,
                   app->dwell.chatter);
    put_text(app->ui, &app->lamp_line, text);
}

/* ---- the main program ---- */

/* odd(): a model whose classes are not the ones this example needs
 * gets a card, never guesses. */
static bento_status_t check_classes(gesture_t *app, bool *differs)
{
    char missing[TEXT_BYTES];
    char text[TEXT_BYTES];
    size_t used = 0U;
    uint32_t i;
    int32_t j;

    missing[0] = '\0';
    for (j = 0; j < CHIP_COUNT; j++) {
        bool present = false;

        for (i = 0U; i < app->model.class_count; i++) {
            present = present ||
                      (strcmp(app->model.labels[i], CHIPS[j]) == 0);
        }
        if (!present) {
            (void)snprintf(&missing[used], sizeof(missing) - used,
                           "%s%s", (used == 0U) ? "" : ", ", CHIPS[j]);
            used = strlen(missing);
        }
    }
    *differs = used != 0U;
    (void)snprintf(text, sizeof(text), "%s has no class\n%s", MODEL,
                   missing);
    return *differs
               ? show_missing(app->ui,
                              "Model classes differ from this example",
                              text)
               : BENTO_OK;
}

/* A circle flips the lamp; shaking warns of a tamper. */
static void react(gesture_t *app)
{
    const char *name = class_name(app, app->dwell.accepted);
    char clock[CLOCK_BYTES];
    char text[TEXT_BYTES];

    if (strcmp(name, "circle") == 0) {
        app->lamp_on = !app->lamp_on;
        app->toggles++;
        put_color(app->ui, &app->lamp,
                  app->lamp_on ? COLOR_TEXT : COLOR_CARD);
    }
    if (strcmp(name, "shaking") ==
        0) { /* farm: a shaken pole = tamper? */
        clock_text(clock, sizeof(clock));
        (void)snprintf(text, sizeof(text), "Shaken at %s - tamper?",
                       clock);
        put(app->ui, &app->status, text, COLOR_WARN);
    }
}

/* A new result: its top class counts only at Confirm at. */
/* sc.index(max(sc)): the first class with the top score. */
static uint32_t top_class(const bento_edgeai_result_t *result)
{
    const uint32_t classes =
        (result->class_count < (uint8_t)MAX_CLASSES)
            ? result->class_count
            : (uint32_t)MAX_CLASSES;
    uint32_t top = 0U;
    uint32_t i;

    for (i = 1U; i < classes; i++) {
        if (result->score_pct[i] > result->score_pct[top]) {
            top = i;
        }
    }
    return top;
}

static void poll_model(gesture_t *app, uint32_t now_ms)
{
    bento_edgeai_result_t result;

    if (read_result(&app->reader, app->model.index, &app->stop,
                    &result)) {
        const uint32_t top = top_class(&result);
        const int32_t percent = (int32_t)result.score_pct[top];
        const int32_t accepted =
            (percent >= app->confirm[top]) ? (int32_t)top : NO_CLASS;

        app->top.fresh = true;
        app->top.klass = (int32_t)top;
        app->top.percent = percent;
        app->top.confirm = app->confirm[top];
        if (step_dwell(app, accepted, percent, now_ms)) {
            react(app);
        }
    }
}

/* SW6 or a tap: the next dwell. A failed poll answers no events. */
static bool dwell_tapped(gesture_t *app)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bool tapped = button_clicked(&app->dwell_button);

    if (bento_ui_poll(app->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        tapped = tapped ||
                 ((events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) &&
                  (events[i].id == app->dwell_choice.id));
    }
    return tapped;
}

static void next_dwell(gesture_t *app)
{
    char seconds[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];

    app->dwell_index = (app->dwell_index + 1) % DWELL_CHOICES;
    tenths_text((bento_real_t)DWELLS_MS[app->dwell_index] /
                    (bento_real_t)MS_PER_S,
                seconds);
    (void)snprintf(text, sizeof(text), "Dwell %s s", seconds);
    put_text(app->ui, &app->dwell_choice, text);
}

/* SW5: pause (the AI core is released) or resume. */
static bento_status_t toggle_pause(gesture_t *app)
{
    bento_status_t status;

    app->paused = !app->paused;
    app->dwell.accepted = NO_CLASS; /* start again from Not sure */
    app->dwell.candidate = NO_CLASS;
    if (app->paused) {
        status = stop_ai();
        put(app->ui, &app->status, "Paused - SW5 resumes", COLOR_WARN);
    } else {
        status = start_model(app, app->model.row_count);
    }
    return status;
}

static bento_status_t read_controls(gesture_t *app)
{
    bento_status_t status =
        sample_button(app->has_buttons, &app->pause_button);

    if (status == BENTO_OK) {
        status = sample_button(app->has_buttons, &app->dwell_button);
    }
    if ((status == BENTO_OK) && dwell_tapped(app)) {
        next_dwell(app);
    }
    if ((status == BENTO_OK) && button_clicked(&app->pause_button)) {
        status = toggle_pause(app);
    }
    return status;
}

/* One pass of the main loop. */
static bento_status_t run_round(gesture_t *app)
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
        draw(app, now_ms);
        app->top.fresh = false;
    }
    if ((status == BENTO_OK) && !app->stop.raised) {
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* run(): the classes, Confirm at, the model started, then the loop. */
static bento_status_t run_gestures(gesture_t *app)
{
    bool differs = false;
    bento_status_t status = check_classes(app, &differs);

    if ((status == BENTO_OK) && !differs) {
        read_confirm(app);
        start_dwell(app);
        app->dwell_index = 1;
        status = start_model(app, 0);
        app->drawn_ms = bento_time_ticks_ms();
        app->polled_ms = app->drawn_ms;
        while ((status == BENTO_OK) && !app->stop.raised) {
            status = run_round(app);
        }
    }
    return status;
}

/* The screen, the run, then the except and the finally. */
static bento_status_t run_program(gesture_t *app)
{
    bento_status_t status = build_screen(app);
    bento_status_t stop_status;

    if (status == BENTO_OK) {
        status = run_gestures(app);
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
        "Motion Detection\n"
        "is built into the TESAIoT Dev Kit.\n"
        "Not on this board.";
    gesture_t *app = &g_gesture;
    bool found = false;
    bool in_emulator;
    bento_status_t status;

    (void)memset(app, 0, sizeof(*app));
    app->ui = bento_runtime_get_ui();
    app->has_buttons = bento_buttons_count() > 0U;
    app->help = app->has_buttons
                    ? "Move the board - SW6 = Dwell, SW5 = pause"
                    : "Move the board";
    app->pause_button.index = BUTTON_SW5;
    app->dwell_button.index = BUTTON_SW6;
    in_emulator = look_up_model(&app->model, &found);
    if (!found) {
        status = show_missing(app->ui, "Model not on this board",
                              missing_text);
        if ((status == BENTO_OK) && in_emulator) {
            status = show_emulator_note(app->ui);
        }
    } else {
        status = run_program(app);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
