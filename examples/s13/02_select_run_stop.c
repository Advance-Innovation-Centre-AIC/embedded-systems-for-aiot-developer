/*
 * 02_select_run_stop.c -- เลือกโมเดลด้วยชื่อ แล้วรัน/หยุดด้วยโค้ด
 * (select a model by name, then run and stop it from code)
 *
 * หาโมเดลด้วยชื่อ (AnomalousVibration ก่อน ไม่มีค่อยใช้ Motion Detection) ปุ่ม Run
 * บนจอ (หรือปุ่ม SW6 บนบอร์ด) เรียก select() ซึ่งโหลดแล้วเริ่มรันทันที จอบอกว่า
 * select() ใช้เวลากี่ ms กดอีกครั้งเป็น Stop เรียก stop() ปุ่ม start() เรียก
 * edge_ai.start() แบบไม่ใส่เลข ดูว่าได้โมเดลไหน ทุกครึ่งวินาทีจอบอก active()
 * กับชื่อโมเดล และผลล่าสุด กด Stop ใน IDE เพื่อจบ (โปรแกรมหยุดโมเดลเสมอ)
 *
 * Finds a model by name (AnomalousVibration first, else Motion
 * Detection). The Run button on the screen (or SW6 on the board) calls
 * select(), which loads and starts the model at once, and the screen
 * shows how long select() took; pressed again it is Stop and calls
 * stop(). The start() button calls edge_ai.start() with no index to
 * show which model that runs. Every half second the screen shows
 * active() with the model's name, and the latest result. Press Stop in
 * the IDE to end; the model is always stopped.
 *
 * Twin of 02_select_run_stop.py
 * (embedded-systems-for-aiot-developer/examples/s13/
 *  02_select_run_stop.py).
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - import buttons fails on the Eva Kit; here a board with no buttons
 *    (bento_buttons_count() 0) is that board;
 *  - edge_ai.models() is bento_edgeai_count() and model_info() per
 *    row; a row's index is its place in the table;
 *  - select(), start() and stop() wait inside the binding; here they
 *    are started and polled. The text of a caught OSError is the
 *    binding's own words for the C status; stop() answers True when
 *    the core stopped and False on its time limit;
 *  - a result is shown when the board has a verdict; conf is
 *    score_pct[top] and the label is bento_edgeai_label();
 *  - import edge_ai fails where there is no model link: here
 *    bento_edgeai_supported() false ends with BENTO_E_UNSUPPORTED;
 *  - Stop raises KeyboardInterrupt in MicroPython; here the wait
 *    returns and the program ends after the same finally.
 * In the BENTO Emulator, Edge AI from C is not available yet: the
 * program says so, then goes on as the original does when no
 * model is on the board.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_buttons.h"
#include "bento_edgeai.h"
#include "bento_runtime.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
enum {
    DRAW_MS = 500
};
/* ---- end of settings ---- */

/* The note for the BENTO Emulator, whose C runtime has no Edge AI yet. */
static const char EMULATOR_NOTE[] =
    "Edge AI จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";

/* Where that note sits on the screen. */
enum {
    EMULATOR_NOTE_X = 12,
    EMULATOR_NOTE_Y = 180,
    EMULATOR_NOTE_PX = 20,
    EMULATOR_NOTE_COLOR = 0xF5A623
};

/* Timing. */
enum {
    LOOP_MS = 30,
    SETTLE_MS = 150, /* let the cleared screen settle */
    OP_POLL_MS = 20,
    TOP_BUTTON = 0, /* SW6, the upper button */
    MODEL_KEY_COUNT = 2,
    TEXT_CHARS = 128
};

/* What a tap asks for. */
typedef enum {
    WANT_NONE = 0,
    WANT_TOGGLE = 1,
    WANT_START = 2
} want_t;

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

/* Layout on the 792 x 398 canvas. */
enum {
    LEFT_X = 12,
    START_X = 248,
    TITLE_Y = 6,
    MODEL_Y = 44,
    BUTTON_Y = 84,
    BUTTON_W = 220,
    BUTTON_H = 64,
    TOOK_Y = 164,
    ACTIVE_Y = 196,
    RESULT_Y = 236,
    SAY_Y = 300,
    SMALL_PX = 16,
    TEXT_PX = 20,
    HEAD_PX = 24,
    BIG_PX = 28
};

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

/* The model, the widgets and what the loop remembers. */
typedef struct {
    bento_ui_t *ui;
    uint32_t model;
    char name[BENTO_EDGEAI_NAME_LEN];
    bool has_buttons;
    bento_ui_id_t go;
    bento_ui_id_t start;
    bento_ui_id_t took;
    bento_ui_id_t active;
    bento_ui_id_t result;
    bento_ui_id_t say;
    bool running;
    bool switch_was;
    uint32_t shown_ms;
    bool has_last;
    char last_result[TEXT_CHARS];
} selector_t;

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

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 label_look_t look,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.w = BUTTON_W;
    args.h = BUTTON_H;
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_button_create(ui, &args, out_id);
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

/* The first model whose name matches, in the order of the names: the
 * Store model first, the built-in one when it is missing. */
static bento_status_t find_model(selector_t *selector, bool *found)
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
            selector->model = (uint32_t)row;
        }
    }
    if (*found) {
        (void)snprintf(selector->name, sizeof(selector->name), "%s",
                       info.name);
    }
    return status;
}

/* The name of model i, "(none)" for no model, "?" when it cannot be
 * read. */
static void name_of(int32_t index, char *name, uint32_t capacity)
{
    bento_edgeai_model_info_t info;

    bento_edgeai_model_info_init(&info);
    if (index < 0) {
        (void)snprintf(name, capacity, "(none)");
    } else if (bento_edgeai_model_info((uint32_t)index, &info) ==
               BENTO_OK) {
        (void)snprintf(name, capacity, "%s", info.name);
    } else {
        (void)snprintf(name, capacity, "?");
    }
}

/* Waits for a started select, start or stop; Stop cancels it. */
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

/* The two buttons and the four lines under them. */
static bento_status_t add_controls(selector_t *selector)
{
    bento_status_t status =
        add_button(selector->ui, "Run",
                   (label_look_t){LEFT_X, BUTTON_Y, COLOR_OK, HEAD_PX},
                   &selector->go);

    if (status == BENTO_OK) {
        status = add_button(
            selector->ui, "start()",
            (label_look_t){START_X, BUTTON_Y, COLOR_DIM, HEAD_PX},
            &selector->start);
    }
    if (status == BENTO_OK) {
        status = add_label(
            selector->ui, " ",
            (label_look_t){LEFT_X, TOOK_Y, COLOR_DIM, SMALL_PX},
            &selector->took);
    }
    if (status == BENTO_OK) {
        status = add_label(
            selector->ui, " ",
            (label_look_t){LEFT_X, ACTIVE_Y, COLOR_TEXT, TEXT_PX},
            &selector->active);
    }
    if (status == BENTO_OK) {
        status = add_label(
            selector->ui, " ",
            (label_look_t){LEFT_X, RESULT_Y, COLOR_TEXT, BIG_PX},
            &selector->result);
    }
    return status;
}

static bento_status_t build_screen(selector_t *selector)
{
    char text[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(selector->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            selector->ui, "Select / Run / Stop",
            (label_look_t){LEFT_X, TITLE_Y, COLOR_TEXT, HEAD_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "Model: %s", selector->name);
        status = add_label(
            selector->ui, text,
            (label_look_t){LEFT_X, MODEL_Y, COLOR_INFO, TEXT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_controls(selector);
    }
    if (status == BENTO_OK) {
        status = add_label(
            selector->ui,
            selector->has_buttons ? "Tap Run (or press SW6)"
                                  : "Tap Run",
            (label_look_t){LEFT_X, SAY_Y, COLOR_WARN, SMALL_PX},
            &selector->say);
    }
    if (status == BENTO_OK) {
        poll_drop(selector->ui);
    }
    return status;
}

/* SW6, the upper button: true once per press. A read that fails is
 * "not pressed". */
static bool switch_hit(selector_t *selector)
{
    bool now = false;
    bool hit = false;

    if (selector->has_buttons) {
        if (bento_buttons_pressed(TOP_BUTTON, &now) != BENTO_OK) {
            now = false;
        }
        hit = now && !selector->switch_was;
        selector->switch_was = now;
    }
    return hit;
}

/* What a click on this widget asks for: the Run button toggles,
 * start() starts, anything else asks nothing, as the dict lookup gives
 * None. */
static want_t want_of(const selector_t *selector, bento_ui_id_t widget)
{
    want_t want = WANT_NONE;

    if (widget == selector->go) {
        want = WANT_TOGGLE;
    } else if (widget == selector->start) {
        want = WANT_START;
    } else {
        want = WANT_NONE;
    }
    return want;
}

/* The last clicked event of the poll sets the wish. */
static want_t read_taps(const selector_t *selector)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    want_t want = WANT_NONE;
    uint32_t i;

    /* ui.poll() answers no events when it fails. */
    if (bento_ui_poll(selector->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) {
            want = want_of(selector, events[i].id);
        }
    }
    return want;
}

/* stop(): True when the core stopped, False on its time limit; any
 * other failure is the OSError that ends the program. */
static bento_status_t stop_running(selector_t *selector)
{
    char text[TEXT_CHARS];
    bento_status_t status = bento_edgeai_stop_start();

    if (status == BENTO_OK) {
        status = finish_op();
    }
    if ((status == BENTO_OK) || (status == BENTO_E_TIMEOUT)) {
        selector->running = false;
        (void)snprintf(text, sizeof(text), "stop() -> %s",
                       (status == BENTO_OK) ? "True" : "False");
        set_text(selector->ui, selector->say, text);
        status = BENTO_OK;
    }
    return status;
}

/* select() waits until the AI core confirms; an OSError is shown, not
 * raised. */
static bento_status_t select_model(selector_t *selector)
{
    char text[TEXT_CHARS];
    const char *reason = "edge_ai: select not confirmed";
    uint32_t start_ms;
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "select(%" PRIu32 ") ...",
                   selector->model);
    set_text(selector->ui, selector->say, text);
    poll_drop(selector->ui);
    start_ms = bento_time_ticks_ms();
    status = bento_edgeai_select_start(selector->model);
    if (status == BENTO_OK) {
        status = finish_op();
    } else {
        reason = "edge_ai: select send failed";
    }
    if (status == BENTO_OK) {
        selector->running = true;
        (void)snprintf(
            text, sizeof(text), "select() took %" PRId32 " ms",
            bento_time_ticks_diff(bento_time_ticks_ms(), start_ms));
        set_text(selector->ui, selector->took, text);
        set_text(selector->ui, selector->say, "Running");
    } else if (status != BENTO_E_CANCELLED) {
        (void)snprintf(text, sizeof(text), "select failed: %s", reason);
        set_text(selector->ui, selector->say, text);
        status = BENTO_OK;
    } else {
        /* Stop: the finally stops the model */
    }
    return status;
}

/* start() with no index: which model runs is the lesson. */
static bento_status_t start_default(selector_t *selector)
{
    char text[TEXT_CHARS];
    const char *reason = "edge_ai: start not confirmed";
    bento_status_t status = bento_edgeai_run_start();

    if (status == BENTO_OK) {
        status = finish_op();
    } else {
        reason = "edge_ai: start send failed";
    }
    if (status == BENTO_OK) {
        selector->running = true;
        set_text(selector->ui, selector->say, "start() with no index");
    } else if (status != BENTO_E_CANCELLED) {
        (void)snprintf(text, sizeof(text), "start failed: %s", reason);
        set_text(selector->ui, selector->say, text);
        status = BENTO_OK;
    } else {
        /* Stop: the finally stops the model */
    }
    return status;
}

static bento_status_t act_on(selector_t *selector, want_t want)
{
    bento_status_t status = BENTO_OK;

    if ((want == WANT_TOGGLE) && selector->running) {
        status = stop_running(selector);
    } else if (want == WANT_TOGGLE) {
        status = select_model(selector);
    } else if (want == WANT_START) {
        status = start_default(selector);
    } else {
        /* nothing asked */
    }
    if ((status == BENTO_OK) && (want != WANT_NONE)) {
        set_text(selector->ui, selector->go,
                 selector->running ? "Stop" : "Run");
        (void)bento_ui_set_color(selector->ui, selector->go,
                                 selector->running ? COLOR_BAD
                                                   : COLOR_OK);
    }
    return status;
}

/* active() with its name: our model, none, or another one. */
static bento_status_t draw_active(const selector_t *selector)
{
    char name[BENTO_EDGEAI_NAME_LEN];
    char text[TEXT_CHARS];
    int32_t active = BENTO_EDGEAI_NONE;
    const bento_status_t status = bento_edgeai_active(&active);

    if (status == BENTO_OK) {
        uint32_t color = COLOR_BAD;

        name_of(active, name, (uint32_t)sizeof(name));
        (void)snprintf(text, sizeof(text), "active() = %" PRId32 "  %s",
                       active, name);
        set_text(selector->ui, selector->active, text);
        if (active == (int32_t)selector->model) {
            color = COLOR_TEXT;
        } else if (active < 0) {
            color = COLOR_DIM;
        } else {
            /* another model runs */
        }
        (void)bento_ui_set_color(selector->ui, selector->active, color);
    }
    return status;
}

/* The latest result while running, "-" otherwise; sent only when it
 * changed. */
static void draw_result(selector_t *selector)
{
    bento_edgeai_result_t result;
    char label[BENTO_EDGEAI_LABEL_LEN];
    char text[TEXT_CHARS];

    (void)snprintf(text, sizeof(text), "-");
    bento_edgeai_result_init(&result);
    if (selector->running &&
        (bento_edgeai_result(&result) == BENTO_OK) &&
        result.has_verdict && result.running &&
        (result.top < BENTO_EDGEAI_MAX_CLASSES) &&
        (bento_edgeai_label(result.index, result.top, label,
                            (uint32_t)sizeof(label),
                            NULL) == BENTO_OK)) {
        (void)snprintf(text, sizeof(text), "%s  %u%%", label,
                       (unsigned int)result.score_pct[result.top]);
    }
    if (!selector->has_last ||
        (strcmp(text, selector->last_result) != 0)) {
        set_text(selector->ui, selector->result, text);
        (void)snprintf(selector->last_result,
                       sizeof(selector->last_result), "%s", text);
        selector->has_last = true;
    }
}

/* One pass: the taps and SW6, the wish, and the status every DRAW_MS
 * or after a wish. */
static bento_status_t run_pass(selector_t *selector)
{
    want_t want = read_taps(selector);
    uint32_t now_ms;
    bento_status_t status;

    if (switch_hit(selector)) {
        want = WANT_TOGGLE;
    }
    status = act_on(selector, want);
    now_ms = bento_time_ticks_ms();
    if ((status == BENTO_OK) &&
        ((want != WANT_NONE) ||
         (bento_time_ticks_diff(now_ms, selector->shown_ms) >=
          DRAW_MS))) {
        selector->shown_ms = now_ms;
        status = draw_active(selector);
        if (status == BENTO_OK) {
            draw_result(selector);
        }
    }
    if (status == BENTO_OK) {
        status = pause_ms(LOOP_MS);
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

/* Runs until Stop; the model is stopped however it ends. */
static bento_status_t run_selector(selector_t *selector)
{
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        status = run_pass(selector);
    }
    stop_model();
    return status;
}

/* The BENTO Emulator's C runtime answers BENTO_E_UNSUPPORTED to every
 * Edge AI call: the console says so. */
static void print_emulator_note(void)
{
    (void)printf(
        "%s / Edge AI from C is not in the BENTO Emulator yet: "
        "run it on a Dev Kit or Eva Kit.\n",
        EMULATOR_NOTE);
}

/* The same note as a label on the current screen. It is best effort,
 * as a widget setter is, so its status is not checked. */
static void add_emulator_note(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    bento_ui_widget_args_init(&args);
    args.text = EMULATOR_NOTE;
    args.text_len = (uint32_t)strlen(EMULATOR_NOTE);
    args.x = EMULATOR_NOTE_X;
    args.y = EMULATOR_NOTE_Y;
    args.color = EMULATOR_NOTE_COLOR;
    args.font_px = EMULATOR_NOTE_PX;
    (void)bento_ui_label_create(ui, &args, &unused);
}

int main(void)
{
    selector_t selector;
    bool supported = false;
    bool in_emulator = false;
    bool found = false;
    bento_status_t status;

    (void)memset(&selector, 0, sizeof(selector));
    selector.ui = bento_runtime_get_ui();
    selector.has_buttons = bento_buttons_count() > 0U;
    status = bento_edgeai_supported(&supported);
    in_emulator = status == BENTO_E_UNSUPPORTED;
    if (in_emulator) {
        (void)bento_ui_screen(selector.ui,
                              BENTO_RUNTIME_SCREEN_WIDTH_PX,
                              BENTO_RUNTIME_SCREEN_HEIGHT_PX);
        add_emulator_note(selector.ui);
        print_emulator_note();
        status = BENTO_OK; /* on as if no model were on the board */
    }
    if ((status == BENTO_OK) && !supported && !in_emulator) {
        status = BENTO_E_UNSUPPORTED; /* import edge_ai fails */
    }
    if ((status == BENTO_OK) && !in_emulator) {
        status = find_model(&selector, &found);
    }
    if ((status == BENTO_OK) && !found) {
        (void)printf("none of these models is on the board: "
                     "('AnomalousVibration', 'Motion Detection')\n");
    } else if (status == BENTO_OK) {
        (void)printf("found %s at index %" PRIu32 "\n", selector.name,
                     selector.model);
        status = build_screen(&selector);
        if (status == BENTO_OK) {
            status = run_selector(&selector);
        }
    } else {
        /* an OSError from edge_ai.models() ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
