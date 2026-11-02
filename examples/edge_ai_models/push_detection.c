/*
 * push_detection.c -- ปุ่มไร้สัมผัส (Touchless Button) ด้วยโมเดลในตัว
 * "Push Detection" (ต้องใช้เรดาร์ของ Dev Kit)
 *
 * ดันฝ่ามือเข้าหาเรดาร์ ระยะราว 60 ซม. (ตามตัวอย่างเรดาร์ของ
 * Infineon) วงกลมซ้ายสว่างขาว "PUSH" นาน 2 วินาที ตัวนับขวาบอก
 * จำนวนครั้ง พร้อมเวลาสามครั้งล่าสุด มาตรวัด LED บอกคะแนน มีขีด
 * เกณฑ์ Hearing at (เทา) และ Confirm at (ขาว) ปุ่ม SW6 หรือแตะปุ่ม
 * Refractory = เวลาห้ามนับซ้ำ 0.3/1/2 วินาที ปุ่ม SW5 = หยุดชั่วคราว/
 * ทำต่อ โปรแกรมตั้งเกณฑ์สอน Hearing at 20% Confirm at 30% (ข้อมูล
 * HMI Kit: ดันที่ 60 ซม. ได้ 21-39% ต่ำกว่า 40/65/30 ของ Dev Kit)
 * ยังไม่ได้ลองบน Dev Kit จบแล้วคืนค่าเดิมของบอร์ดและคืนแกน AI เสมอ
 *
 * Push a palm toward the radar, about 60 cm away (Infineon's radar
 * example). The round light turns white with "PUSH" for 2 s; the
 * right card counts the pushes and shows the last three. The LED
 * meter shows the score, with marks at Hearing at (grey) and
 * Confirm at (white). SW6 or a tap on Refractory = no second push
 * for 0.3, 1 or 2 s; SW5 = pause and resume. The program runs Push
 * at Hearing at 20%, Confirm at 30% (HMI Kit data: 60 cm pushes
 * peaked at 21-39%, under the Dev Kit's 40/65/30); not yet checked
 * on a Dev Kit. The board's own numbers are put back at the end and
 * the AI core is always released.
 *
 * Twin of push_detection.py
 * (embedded-systems-for-aiot-developer/examples/edge_ai_models/push_detection.py).
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
 *   - edge_ai.select(), stop() and the threshold setter wait inside
 *     the binding; here they are started and polled, the setter's
 *     wait for diag.ctrl_handled done as modedgeai.c does it. A
 *     failed select prints the binding's words ("edge_ai: select
 *     send failed" or "select not confirmed"), a failed threshold
 *     "edge_ai: threshold send failed".
 *   - The global REFRACT_MS is a field of the program state.
 *   - th != th0 compares the board's numbers; a fallback (the
 *     board gave none) never equals a read from the board, as a
 *     list never equals a tuple in MicroPython.
 *   - The Stop exception is a stop text kept in the program state:
 *     the loop ends, the board's thresholds are put back, the text
 *     goes on the status line in red and is printed, and the AI
 *     core is released, as the two finally blocks do.
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
    HOLD_MS = 2000,     /* an event stays on screen this long */
    NEAR_MS = 1000,     /* two pushes closer than this: "< 1 s apart" */
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
    LOG_LINES = 3,
    VU_LEDS = 10,
    VU_STEP = 10, /* % per LED */
    REFRACT_CHOICES = 3,
    LEVEL_KINDS = 3, /* Hearing at, Confirm at, clear below */
    PUSH_CLASS = 1,
    TEACH_ENTER = 20, /* teaching thresholds (see the header) */
    TEACH_CONFIRM = 30,
    PERCENT = 100
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

enum { /* the meter and its marks */
    METER_X = 28,
    METER_W = 432,
    MARK_Y = 256
};

static const char MODEL[] =
    "Push Detection"; /* exact name, looked up every run */
/* SW6 / touch: no second push sooner than this */
static const int32_t REFRACTS_MS[REFRACT_CHOICES] = {300, 1000, 2000};

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

typedef struct {
    bento_ui_t *ui;
    bool has_buttons;
    const char *help;
    model_t model;
    stop_t stop;
    reader_t reader;
    bento_ui_id_t heartbeat;
    bento_ui_id_t vu[VU_LEDS];
    bento_ui_id_t mark_enter;
    bento_ui_id_t mark_confirm;
    shown_t disc;
    shown_t verdict;
    shown_t state;
    shown_t legend;
    shown_t count;
    shown_t last;
    shown_t near;
    shown_t lines[LOG_LINES];
    shown_t refractory;
    shown_t status;
    thresholds_t original;
    thresholds_t thresholds;
    vote_t vote;
    episode_t episode;
    char log[LOG_LINES][TEXT_BYTES];
    int32_t log_count;
    board_button_t pause_button;
    board_button_t refract_button;
    int32_t refract_index;
    int32_t refract_ms;
    int32_t near_count;
    bool has_last;
    uint32_t last_ms;
    int32_t lit;
    int32_t peak;
    bool paused;
    bool blink;
    uint32_t drawn_ms;
    uint32_t polled_ms;
} push_t;

/* The program's state, too big for the 8 KB program stack: at file
 * scope, zeroed by the loader on every load and set again at the
 * start of main() (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern push_t g_push;
push_t g_push;

/* A position on the screen: the widget arguments are 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

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
static bento_status_t select_model(push_t *app)
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
static bento_status_t start_model(push_t *app, int32_t expected_rows)
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
static bento_status_t set_threshold(push_t *app, uint32_t klass,
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
    }
    return status;
}

/* restore_th(): put back what this program changed; enter,
 * confirm, exit in this order undoes the board's clamp. */
static bento_status_t restore_thresholds(push_t *app)
{
    bento_status_t status = BENTO_OK;
    uint32_t kind;

    if (thresholds_differ(&app->thresholds, &app->original)) {
        for (kind = 0U;
             (kind < (uint32_t)LEVEL_KINDS) && (status == BENTO_OK);
             kind++) {
            status =
                set_threshold(app, PUSH_CLASS, kind,
                              app->original.level[kind][PUSH_CLASS]);
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

/* True once per new event; refractory from the last event. */
static bool step_episode(episode_t *episode, const signal_t *signal,
                         uint32_t now_ms, int32_t refract_ms)
{
    int32_t state = episode->state;
    bool fired = false;
    bool held = false;

    if (signal->confirm) {
        fired = confirm_event(episode, now_ms, refract_ms);
    }
    if (!fired && (state == SEEN)) {
        held =
            bento_time_ticks_diff(now_ms, episode->time_ms) < HOLD_MS;
        state = NORMAL;
    }
    if (!fired && !held) {
        episode->state = settle_state(episode, state, signal);
    }
    return fired;
}

/* event_of(): the strongest EVENT class; class 0 = nothing. */
static uint32_t event_class(const bento_edgeai_result_t *result)
{
    const uint32_t classes =
        (result->class_count < (uint8_t)MAX_CLASSES)
            ? result->class_count
            : (uint32_t)MAX_CLASSES;
    uint32_t event = 0U;
    uint32_t klass;

    for (klass = 1U; klass < classes; klass++) {
        if ((result->score_pct[klass] > result->score_pct[event]) ||
            (event == 0U)) {
            event = klass;
        }
    }
    return event;
}

static const char *label_of(const push_t *app, uint32_t klass)
{
    return (klass < app->model.class_count) ? app->model.labels[klass]
                                            : "";
}

/* ---- the screen of this example ---- */

static bento_status_t build_meter(push_t *app)
{
    const spec_t enter = {METER_X, MARK_Y, 4, 32, COLOR_DIM, 0, 0, 0};
    const spec_t confirm = {METER_X,    MARK_Y, 4, 32,
                            COLOR_TEXT, 0,      0, 0};
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < VU_LEDS) && (status == BENTO_OK); i++) {
        const spec_t led = {
            (int16_t)(28 + (44 * i)), 262, 36, 20, COLOR_TEXT, 0, 0, 0};

        status = add_widget(app->ui, (uint8_t)UI_WIDGET_LED, NULL, &led,
                            &app->vu[i]);
    }
    if (status == BENTO_OK) {
        status = add_widget(app->ui, (uint8_t)UI_WIDGET_PANEL, NULL,
                            &enter, &app->mark_enter);
    }
    if (status == BENTO_OK) {
        status = add_widget(app->ui, (uint8_t)UI_WIDGET_PANEL, NULL,
                            &confirm, &app->mark_confirm);
    }
    return status;
}

static bento_status_t build_now_card(push_t *app)
{
    const spec_t card = {12, 64, 470, 272, 0, 0, 0, 0};
    const spec_t disc = {28, 100,       140, 140, COLOR_DIM,
                         0,  COLOR_DIM, 70}; /* a round light */
    const spec_t verdict = {184,       110,        0, 0,
                            COLOR_DIM, VERDICT_PX, 0, 0};
    const spec_t state = {184, 150, 0, 0, COLOR_DIM, SMALL_PX, 0, 0};
    const spec_t legend = {28, 290, 0, 0, COLOR_DIM, SMALL_PX, 0, 0};
    bento_status_t status = add_card(app->ui, &card, "Now");

    if (status == BENTO_OK) {
        status = add_widget(app->ui, (uint8_t)UI_WIDGET_PANEL, NULL,
                            &disc, &app->disc.id);
    }
    if (status == BENTO_OK) {
        status =
            add_label(app->ui, "Ready", &verdict, &app->verdict.id);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, " ", &state, &app->state.id);
    }
    if (status == BENTO_OK) {
        status = build_meter(app);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, " ", &legend, &app->legend.id);
    }
    return status;
}

static bento_status_t build_log(push_t *app)
{
    bento_status_t status = BENTO_OK;
    int32_t j;

    for (j = 0; (j < LOG_LINES) && (status == BENTO_OK); j++) {
        const spec_t line = {508,        (int16_t)(188 + (24 * j)),
                             0,          0,
                             COLOR_TEXT, SMALL_PX,
                             0,          0};

        status = add_label(app->ui, " ", &line, &app->lines[j].id);
    }
    return status;
}

static bento_status_t build_push_card(push_t *app)
{
    const spec_t card = {494, 64, 294, 272, 0, 0, 0, 0};
    const spec_t count = {508, 96, 120, 56, COLOR_TEXT, 0, 0, 0};
    const spec_t last = {640, 110, 0, 0, COLOR_DIM, SMALL_PX, 0, 0};
    const spec_t near = {508, 164, 0, 0, COLOR_TEXT, SMALL_PX, 0, 0};
    const spec_t choice = {508,          266,     266, 60,
                           COLOR_BUTTON, CHIP_PX, 0,   0};
    bento_status_t status = add_card(app->ui, &card, "Pushes");

    if (status == BENTO_OK) {
        status = add_widget(app->ui, (uint8_t)UI_WIDGET_SEG7, "0",
                            &count, &app->count.id);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, "last -", &last, &app->last.id);
    }
    if (status == BENTO_OK) {
        status =
            add_label(app->ui, "< 1 s apart: 0", &near, &app->near.id);
    }
    if (status == BENTO_OK) {
        status = build_log(app);
    }
    if (status == BENTO_OK) {
        status =
            add_widget(app->ui, (uint8_t)UI_WIDGET_BUTTON,
                       "Refractory 1 s", &choice, &app->refractory.id);
    }
    return status;
}

static bento_status_t build_screen(push_t *app)
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
        status =
            add_label(app->ui, "Touchless Button", &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_widget(app->ui, (uint8_t)UI_WIDGET_LED, NULL,
                            &heartbeat, &app->heartbeat);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui,
                           "Detects: a palm push toward the radar, ~60 "
                           "cm. Cannot: other "
                           "gestures. Demo.",
                           &note, &unused);
    }
    if (status == BENTO_OK) {
        status = build_now_card(app);
    }
    if (status == BENTO_OK) {
        status = build_push_card(app);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, " ", &status_line, &app->status.id);
    }
    if (status == BENTO_OK) {
        drain_events(app->ui);
    }
    return status;
}

/* ticks(): marks over the meter at Hearing at (grey) and Confirm at
 * (white). pos() is a setter: its refusal was ignored before too. */
static void show_marks(push_t *app, uint32_t klass)
{
    const int32_t enter =
        app->thresholds.level[BENTO_EDGEAI_TH_ENTER][klass];
    const int32_t confirm =
        app->thresholds.level[BENTO_EDGEAI_TH_CONFIRM][klass];
    char text[TEXT_BYTES];

    (void)bento_ui_set_pos(
        app->ui, app->mark_enter,
        px((int32_t)METER_X +
           ((enter * (int32_t)METER_W) / (int32_t)PERCENT)),
        MARK_Y);
    (void)bento_ui_set_pos(
        app->ui, app->mark_confirm,
        px((int32_t)METER_X +
           ((confirm * (int32_t)METER_W) / (int32_t)PERCENT)),
        MARK_Y);
    (void)snprintf(text, sizeof(text),
                   "Hearing at %" PRId32 "%% | Confirm at %" PRId32
                   "%%",
                   enter, confirm);
    put_text(app->ui, &app->legend, text);
}

/* meter(): 10 LEDs; switch only the ones that change. */
static void meter(push_t *app, int32_t percent)
{
    const int32_t lit = (percent + (VU_STEP / 2)) / VU_STEP;
    const int32_t low = (lit < app->lit) ? lit : app->lit;
    const int32_t high = (lit < app->lit) ? app->lit : lit;
    int32_t i;

    for (i = low; (i < high) && (i < VU_LEDS); i++) {
        (void)bento_ui_set_value(app->ui, app->vu[i],
                                 (i < lit) ? 1 : 0); /* a setter */
    }
    app->lit = lit;
}

/* Info kind: grey, then white for HOLD_MS; "maybe" only in dim text. */
static void draw(push_t *app)
{
    const int32_t state = app->episode.state;
    const int32_t percent = (app->peak > 0) ? app->peak : 0;
    const bool seen = state == SEEN;
    const char *verdict = "Ready";
    char text[TEXT_BYTES];

    /* heartbeat; peak -1 = no new result */
    (void)bento_ui_set_value(app->ui, app->heartbeat,
                             (app->blink && (app->peak >= 0)) ? 1 : 0);
    put_color(app->ui, &app->disc, seen ? COLOR_TEXT : COLOR_DIM);
    if (seen) {
        verdict = "PUSH";
    } else if (state == HEARD) {
        verdict = "maybe push";
    } else {
        /* Ready */
    }
    put(app->ui, &app->verdict, verdict, seen ? COLOR_TEXT : COLOR_DIM);
    (void)snprintf(text, sizeof(text), "score %" PRId32 "%%", percent);
    put_text(app->ui, &app->state, text);
    meter(app, percent);
}

/* ---- the main program ---- */

/* A double push, or one push counted twice. */
static void count_near(push_t *app, uint32_t now_ms)
{
    char text[TEXT_BYTES];

    if (app->has_last &&
        (bento_time_ticks_diff(now_ms, app->last_ms) < NEAR_MS)) {
        app->near_count++;
        (void)snprintf(text, sizeof(text), "< 1 s apart: %" PRId32,
                       app->near_count);
        put_text(app->ui, &app->near, text);
    }
    app->has_last = true;
    app->last_ms = now_ms;
}

/* A new push: the console line, the log (newest first), the count. */
static void log_push(push_t *app, uint32_t event, uint32_t now_ms)
{
    char clock[CLOCK_BYTES];
    char text[TEXT_BYTES];
    int32_t j;

    count_near(app, now_ms);
    for (j = LOG_LINES - 1; j > 0; j--) {
        (void)memcpy(app->log[j], app->log[j - 1], sizeof(app->log[j]));
    }
    clock_text(clock, sizeof(clock));
    (void)snprintf(app->log[0], sizeof(app->log[0]),
                   "%s %s %" PRId32 "%% #%" PRId32, clock,
                   label_of(app, event), vote_max(&app->vote),
                   app->episode.count);
    app->log_count =
        (app->log_count < LOG_LINES) ? (app->log_count + 1) : LOG_LINES;
    (void)printf("%s\n", app->log[0]);
    for (j = 0; j < app->log_count; j++) {
        put_text(app->ui, &app->lines[j], app->log[j]);
    }
    (void)snprintf(text, sizeof(text), "%" PRId32, app->episode.count);
    put_text(app->ui, &app->count, text);
    clock_text(clock, sizeof(clock));
    (void)snprintf(text, sizeof(text), "last %s", clock);
    put_text(app->ui, &app->last, text);
}

static void poll_model(push_t *app, uint32_t now_ms)
{
    bento_edgeai_result_t result;

    if (read_result(&app->reader, app->model.index, &app->stop,
                    &result)) {
        const uint32_t event = event_class(&result);
        const int32_t percent = (int32_t)result.score_pct[event];
        const int32_t (*level)[MAX_CLASSES] = app->thresholds.level;
        signal_t signal;

        vote_add(&app->vote, percent);
        app->peak = (percent > app->peak) ? percent : app->peak;
        signal.heard = percent >= level[BENTO_EDGEAI_TH_ENTER][event];
        signal.confirm = vote_hits(
            &app->vote, level[BENTO_EDGEAI_TH_CONFIRM][event]);
        signal.clear =
            vote_clear(&app->vote, level[BENTO_EDGEAI_TH_EXIT][event]);
        if (step_episode(&app->episode, &signal, now_ms,
                         app->refract_ms)) {
            log_push(app, event, now_ms);
        }
    }
}

/* SW6 or any event on the button (the original reads only the
 * handle). A failed poll answers no events. */
static bool refract_tapped(push_t *app)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bool tapped = button_clicked(&app->refract_button);

    if (bento_ui_poll(app->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        tapped = tapped || (events[i].id == app->refractory.id);
    }
    return tapped;
}

/* Refractory 0.3 -> 1 -> 2 s. */
static void next_refract(push_t *app)
{
    static const char *const seconds[REFRACT_CHOICES] = {"0.3", "1",
                                                         "2"};
    char clock[CLOCK_BYTES];
    char text[TEXT_BYTES];

    app->refract_index = (app->refract_index + 1) % REFRACT_CHOICES;
    app->refract_ms = REFRACTS_MS[app->refract_index];
    (void)snprintf(text, sizeof(text), "Refractory %s s",
                   seconds[app->refract_index]);
    put_text(app->ui, &app->refractory, text);
    clock_text(clock, sizeof(clock));
    (void)printf("%s refractory %" PRId32 " ms\n", clock,
                 app->refract_ms);
}

/* SW5: pause (the AI core is released) or resume. */
static bento_status_t toggle_pause(push_t *app)
{
    bento_status_t status;

    app->paused = !app->paused;
    app->vote.count = 0;
    app->episode.state = NORMAL;
    if (app->paused) {
        status = stop_ai();
        put(app->ui, &app->status, "Paused - SW5 resumes", COLOR_WARN);
    } else {
        status = start_model(app, app->model.row_count);
    }
    return status;
}

static bento_status_t read_controls(push_t *app)
{
    bento_status_t status =
        sample_button(app->has_buttons, &app->pause_button);

    if (status == BENTO_OK) {
        status = sample_button(app->has_buttons, &app->refract_button);
    }
    if ((status == BENTO_OK) && refract_tapped(app)) {
        next_refract(app);
    }
    if ((status == BENTO_OK) && button_clicked(&app->pause_button)) {
        status = toggle_pause(app);
    }
    return status;
}

/* One pass of the main loop. */
static bento_status_t run_round(push_t *app)
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
        draw(app);
        app->peak = -1;
    }
    if ((status == BENTO_OK) && !app->stop.raised) {
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* The loop, then the finally: the board keeps thresholds until a
 * reboot, so its own are put back. */
static bento_status_t loop_then_restore(push_t *app)
{
    bento_status_t status = start_model(app, 0);
    bento_status_t restore_status;

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

/* run(): the teaching thresholds, their marks, then the loop. */
static bento_status_t run_push(push_t *app)
{
    bento_status_t status;

    read_thresholds(app->model.index, &app->original);
    app->thresholds = app->original;
    status = set_threshold(app, PUSH_CLASS, BENTO_EDGEAI_TH_ENTER,
                           TEACH_ENTER);
    if (status == BENTO_OK) { /* exit follows enter down to 20 */
        status = set_threshold(app, PUSH_CLASS, BENTO_EDGEAI_TH_CONFIRM,
                               TEACH_CONFIRM);
    }
    if (status == BENTO_OK) {
        show_marks(app, PUSH_CLASS);
        app->refract_index = 1;
        app->refract_ms = REFRACTS_MS[app->refract_index];
        app->peak = -1;
        app->episode.latch = false;
        status = loop_then_restore(app);
    }
    return status;
}

/* The screen, the run, then the except and the finally. */
static bento_status_t run_program(push_t *app)
{
    bento_status_t status = build_screen(app);
    bento_status_t stop_status;

    if (status == BENTO_OK) {
        status = run_push(app);
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
        "Push Detection\n"
        "is built into the TESAIoT Dev Kit.\n"
        "Not on this board.";
    push_t *app = &g_push;
    bool found = false;
    bool in_emulator;
    bento_status_t status;

    (void)memset(app, 0, sizeof(*app));
    app->ui = bento_runtime_get_ui();
    app->has_buttons = bento_buttons_count() > 0U;
    app->help = app->has_buttons
                    ? "Push your palm - SW6 = refractory, SW5 = pause"
                    : "Push your palm toward the radar";
    app->pause_button.index = BUTTON_SW5;
    app->refract_button.index = BUTTON_SW6;
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
