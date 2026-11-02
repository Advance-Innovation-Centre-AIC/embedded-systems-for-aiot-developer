/*
 * 05_on_result_callback.c -- ให้บอร์ดเรียกเราเมื่อผลเปลี่ยน (callback) เทียบกับ
 * วนอ่านเอง (the board calls us when the result changes, against polling)
 *
 * หาโมเดลด้วยชื่อแล้วรัน ขอให้บอร์ดแจ้งผล (on_result) และวนอ่าน result() เองไป
 * พร้อมกัน นับสองคอลัมน์ในช่วง 10 วินาทีเดียวกัน ซ้ายคือบอร์ดแจ้งผลกี่ครั้ง ขวาคือ
 * วนอ่านได้ผลใหม่ (seq เปลี่ยน) กี่ครั้ง บอร์ดไม่ได้แจ้งทุกผล แจ้งเมื่อคลาสที่ชนะ
 * เปลี่ยน ถ้าไม่เปลี่ยนแจ้งไม่เกินวินาทีละครั้ง ทำ 6 ช่วงแล้วจบ ลองวางนิ่งแล้วลองเขย่า
 *
 * Finds a model by name and runs it, asks the board to report results
 * (on_result) and polls result() itself at the same time. Two columns
 * over the same 10 second window: on the left how often the board
 * reported, on the right how many new results (a changed seq) polling
 * found. The board does not report every result: it reports when the
 * winning class changes, otherwise at most once a second. Six windows,
 * then it ends. Try it lying still, then shaking.
 *
 * Twin of 05_on_result_callback.py
 * (embedded-systems-for-aiot-developer/examples/s13/
 *  05_on_result_callback.py).
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - C has no callback here: on_result(fn) is bento_edgeai_listen(true)
 *    and every loop pass takes the waiting verdict events with
 *    bento_edgeai_poll_result(); the callback count is the number of
 *    verdict events polled (plus any the full ring dropped), and
 *    on_result(None) is bento_edgeai_listen(false);
 *  - the callback's r is pulled when the events are polled, with
 *    bento_edgeai_result_of(model) (or result()); when that fails, r is
 *    the event's own class number and percent, with no seq, as the
 *    binding's short r;
 *  - result() is None when the board has no verdict (has_verdict
 *    false) or the read fails; conf is score_pct[top] in percent; the
 *    label is bento_edgeai_label();
 *  - edge_ai.models() is bento_edgeai_count() and model_info() per
 *    row; a row's index is its place in the table;
 *  - select() and stop() wait inside the binding; here they are
 *    started and polled; an OSError from select() ends the program
 *    after the finally, here its status is returned;
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

#include "bento_edgeai.h"
#include "bento_runtime.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
enum {
    WINDOW_S = 10, /* one counting window, in seconds */
    RUNS = 6,      /* windows, then the end */
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
    MS_PER_S = 1000,
    LOOP_MS = 50,
    SETTLE_MS = 150, /* let the cleared screen settle */
    OP_POLL_MS = 100,
    MODEL_KEY_COUNT = 2,
    ROWS_CHARS = 160, /* six "n/m" pairs, each any two int32_t */
    TEXT_CHARS = 200,
    SEQUENCE_CHARS = 12 /* "-" or any uint32_t, with its NUL */
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_OK = 0x30A46C,
    COLOR_INFO = 0x4A9EFF
};

/* Layout on the 792 x 398 canvas: the callback column on the left, the
 * polling column on the right. */
enum {
    LEFT_X = 12,
    RIGHT_X = 404,
    TITLE_Y = 6,
    CAPTION_Y = 48,
    COUNT_Y = 76,
    LAST_Y = 130,
    HISTORY_Y = 170,
    SMALL_PX = 16,
    TEXT_PX = 20,
    BIG_PX = 28
};

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

/* The r the board's last report carried. */
typedef struct {
    bool present;
    bool has_sequence; /* false: the short r, without seq */
    uint32_t sequence;
    int32_t conf_pct;
    char label[BENTO_EDGEAI_LABEL_LEN];
} report_t;

/* The model, the widgets, the reports and the windows. */
typedef struct {
    bento_ui_t *ui;
    uint32_t model;
    char name[BENTO_EDGEAI_NAME_LEN];
    bento_ui_id_t callback_label;
    bento_ui_id_t polling_label;
    bento_ui_id_t last_label;
    bento_ui_id_t history_label;
    uint32_t calls;
    report_t last;
    char rows[ROWS_CHARS];
} compare_t;

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

/* The first model whose name matches, in the order of the names. */
static bento_status_t find_model(compare_t *compare, bool *found)
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
            compare->model = (uint32_t)row;
        }
    }
    if (*found) {
        (void)snprintf(compare->name, sizeof(compare->name), "%s",
                       info.name);
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

/* The finally: the reports end first, then the AI core is released and
 * its OSError passed over. It waits even after Stop, as the binding
 * does. */
static void stop_model(void)
{
    bento_status_t status;

    (void)bento_edgeai_listen(false); /* answers nothing in Python */
    status = bento_edgeai_stop_start();
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        /* the IDE's Stop may already be in: the wait is not checked */
        (void)bento_time_sleep_ms(OP_POLL_MS);
        status = bento_edgeai_op_poll(NULL);
    }
}

static bento_status_t build_screen(compare_t *compare)
{
    char title[TEXT_CHARS];
    bento_ui_t *ui = compare->ui;
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        (void)snprintf(title, sizeof(title), "Callback vs polling: %s",
                       compare->name);
        status = add_label(
            ui, title,
            (label_look_t){LEFT_X, TITLE_Y, COLOR_TEXT, TEXT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "on_result() calls",
            (label_look_t){LEFT_X, CAPTION_Y, COLOR_INFO, SMALL_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "new seq by polling",
            (label_look_t){RIGHT_X, CAPTION_Y, COLOR_OK, SMALL_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "-",
            (label_look_t){LEFT_X, COUNT_Y, COLOR_INFO, BIG_PX},
            &compare->callback_label);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "-", (label_look_t){RIGHT_X, COUNT_Y, COLOR_OK, BIG_PX},
            &compare->polling_label);
    }
    return status;
}

static bento_status_t add_notes(compare_t *compare)
{
    bento_status_t status =
        add_label(compare->ui, " ",
                  (label_look_t){LEFT_X, LAST_Y, COLOR_TEXT, SMALL_PX},
                  &compare->last_label);

    if (status == BENTO_OK) {
        status = add_label(
            compare->ui, " ",
            (label_look_t){LEFT_X, HISTORY_Y, COLOR_DIM, SMALL_PX},
            &compare->history_label);
    }
    if (status == BENTO_OK) {
        poll_drop(compare->ui);
    }
    return status;
}

/* The r of one report: the full result when the board gives it, else
 * the short r of the event itself. */
static void pull_report(report_t *report,
                        const bento_edgeai_verdict_ev_t *event)
{
    bento_edgeai_result_t result;
    const bool named = event->has_model != 0U;
    bool full;

    bento_edgeai_result_init(&result);
    full = (named ? bento_edgeai_result_of(event->model, &result)
                  : bento_edgeai_result(&result)) == BENTO_OK;
    full = full && result.has_verdict &&
           (result.top < BENTO_EDGEAI_MAX_CLASSES) &&
           (bento_edgeai_label(result.index, result.top, report->label,
                               (uint32_t)sizeof(report->label),
                               NULL) == BENTO_OK);
    report->present = true;
    report->has_sequence = full;
    if (full) {
        report->sequence = result.seq;
        report->conf_pct = (int32_t)result.score_pct[result.top];
    } else {
        (void)snprintf(report->label, sizeof(report->label), "%u",
                       (unsigned int)event->cls);
        report->conf_pct = (int32_t)event->pct;
    }
}

/* What the callback did for each report: count it and keep its r. */
static void take_reports(compare_t *compare)
{
    bento_edgeai_verdict_ev_t events[BENTO_EDGEAI_RING_DEPTH];
    uint32_t count = 0U;
    uint32_t dropped = 0U;
    uint32_t i;

    for (i = 0U; i < BENTO_EDGEAI_RING_DEPTH; i++) {
        bento_edgeai_verdict_ev_init(&events[i]);
    }
    if (bento_edgeai_poll_result(events, BENTO_EDGEAI_RING_DEPTH,
                                 &count, &dropped) != BENTO_OK) {
        count = 0U; /* no report this pass */
        dropped = 0U;
    }
    compare->calls += count + dropped;
    if (count != 0U) {
        pull_report(&compare->last, &events[count - 1U]);
    }
}

/* A new result of this model found by polling. */
static bool polled_new(const compare_t *compare, bool *has_sequence,
                       uint32_t *last_sequence)
{
    bento_edgeai_result_t result;
    bool fresh;

    bento_edgeai_result_init(&result);
    fresh = (bento_edgeai_result(&result) == BENTO_OK) &&
            result.has_verdict && (result.index == compare->model) &&
            (!*has_sequence || (result.seq != *last_sequence));
    if (fresh) {
        *has_sequence = true;
        *last_sequence = result.seq;
    }
    return fresh;
}

/* Both counts, and the last report when there is one. */
static void draw_counts(const compare_t *compare, uint32_t polled)
{
    char text[TEXT_CHARS];
    char sequence[SEQUENCE_CHARS];
    const report_t *last = &compare->last;

    (void)snprintf(text, sizeof(text), "%" PRIu32, compare->calls);
    set_text(compare->ui, compare->callback_label, text);
    (void)snprintf(text, sizeof(text), "%" PRIu32, polled);
    set_text(compare->ui, compare->polling_label, text);
    if (last->present) {
        (void)snprintf(sequence, sizeof(sequence), "-");
        if (last->has_sequence) {
            (void)snprintf(sequence, sizeof(sequence), "%" PRIu32,
                           last->sequence);
        }
        (void)snprintf(text, sizeof(text),
                       "last callback: label %s  conf %" PRId32
                       "%%  seq %s",
                       last->label, last->conf_pct, sequence);
        set_text(compare->ui, compare->last_label, text);
    }
}

/* One window of WINDOW_S: reports and polling side by side. */
static bento_status_t run_window(compare_t *compare, uint32_t *polled)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    uint32_t drawn_ms = start_ms;
    uint32_t last_sequence = 0U;
    bool has_sequence = false;
    bento_status_t status = BENTO_OK;

    compare->calls = 0U;
    *polled = 0U;
    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            ((int32_t)WINDOW_S * (int32_t)MS_PER_S))) {
        uint32_t now_ms;

        take_reports(compare);
        if (polled_new(compare, &has_sequence, &last_sequence)) {
            (*polled)++;
        }
        now_ms = bento_time_ticks_ms();
        if (bento_time_ticks_diff(now_ms, drawn_ms) >= DRAW_MS) {
            drawn_ms = now_ms;
            draw_counts(compare, *polled);
        }
        poll_drop(compare->ui);
        status = pause_ms(LOOP_MS);
    }
    return status;
}

/* RUNS windows; each adds its pair to the history row. */
static bento_status_t run_windows(compare_t *compare)
{
    char text[TEXT_CHARS];
    uint32_t polled = 0U;
    bento_status_t status = BENTO_OK;
    int32_t window;

    for (window = 0; (window < RUNS) && (status == BENTO_OK);
         window++) {
        status = run_window(compare, &polled);
        if (status == BENTO_OK) {
            const size_t used = strlen(compare->rows);

            (void)snprintf(
                &compare->rows[used], sizeof(compare->rows) - used,
                "%s%" PRIu32 "/%" PRIu32, (used != 0U) ? " " : "",
                compare->calls, polled);
            (void)printf("window %" PRId32 " callback %" PRIu32
                         " | polling %" PRIu32 "\n",
                         window + 1, compare->calls, polled);
            (void)snprintf(text, sizeof(text),
                           "callback/polling per %d s: %s", WINDOW_S,
                           compare->rows);
            set_text(compare->ui, compare->history_label, text);
        }
    }
    return status;
}

/* The model with its reports on, under the finally that ends both. */
static bento_status_t run_model(compare_t *compare)
{
    bento_status_t status = bento_edgeai_select_start(compare->model);

    if (status == BENTO_OK) {
        status = finish_op();
    }
    if (status == BENTO_OK) {
        status = bento_edgeai_listen(true);
    }
    if (status == BENTO_OK) {
        status = run_windows(compare);
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
    compare_t compare;
    bool supported = false;
    bool in_emulator = false;
    bool found = false;
    bento_status_t status;

    (void)memset(&compare, 0, sizeof(compare));
    compare.ui = bento_runtime_get_ui();
    status = bento_edgeai_supported(&supported);
    in_emulator = status == BENTO_E_UNSUPPORTED;
    if (in_emulator) {
        (void)bento_ui_screen(compare.ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                              BENTO_RUNTIME_SCREEN_HEIGHT_PX);
        add_emulator_note(compare.ui);
        print_emulator_note();
        status = BENTO_OK; /* on as if no model were on the board */
    }
    if ((status == BENTO_OK) && !supported && !in_emulator) {
        status = BENTO_E_UNSUPPORTED; /* import edge_ai fails */
    }
    if ((status == BENTO_OK) && !in_emulator) {
        status = find_model(&compare, &found);
    }
    if ((status == BENTO_OK) && !found) {
        (void)printf("none of these models is on the board: "
                     "('AnomalousVibration', 'Motion Detection')\n");
    } else if (status == BENTO_OK) {
        status = build_screen(&compare);
        if (status == BENTO_OK) {
            status = add_notes(&compare);
        }
        if (status == BENTO_OK) {
            status = run_model(&compare);
        }
    } else {
        /* an OSError from edge_ai.models() ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
