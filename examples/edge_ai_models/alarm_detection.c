/*
 * alarm_detection.c -- ฟังเสียงสัญญาณเตือนภัย (Alarm Listener) ด้วย
 * โมเดลในตัว
 *
 * รันโมเดล "Alarm Detection" ที่มากับบอร์ด เสียงบี๊บหนึ่งชุดคือผลโหวต
 * 2 ใน 3 ผลล่าสุดถึงเกณฑ์ Confirm at ถ้าได้ยินสองชุดภายใน 4 วินาที
 * จอทั้งสองการ์ดเปลี่ยนเป็นสีแดงเข้มและกะพริบ จนกว่าจะกด ACK บนจอหรือ
 * ปุ่ม SW6 ปุ่ม SW5 หยุด/รันต่อ ปุ่ม Snooze พักการเตือน 60 วินาที
 * (ยังนับต่อ) ถ้าเสียงยังดังเมื่อหมดเวลาพัก จอจะเตือนอีกครั้ง
 * ใช้สาธิตการสอนเท่านั้น ไม่ใช่อุปกรณ์ความปลอดภัย และตรวจควันหรือ CO
 * ไม่ได้
 *
 * Runs the board's built-in "Alarm Detection" model. A beep burst is
 * 2 of the last 3 results at Confirm at; two bursts within 4 s are an
 * alarm, and both cards turn dark red and flash until ACK is tapped
 * or SW6 is pressed. SW5 pauses and resumes. Snooze shelves the alarm
 * for 60 s (it still counts); still sounding when Snooze ends, the
 * alarm is announced again. A teaching demo, NOT a safety device: it
 * detects alarm beeps, not smoke or CO.
 *
 * Twin of alarm_detection.py (embedded-systems-for-aiot-developer/
 * examples/edge_ai_models/alarm_detection.py).
 * Closest library twin: devkit-21-ai-model-lab (style only; the code
 * is new). Style: CLEAN_C.md.
 * Differs from the original:
 *   - edge_ai.models() reads every row at once; here the rows are read
 *     one by one, and any failure prints "cannot read models:" as the
 *     original's except does. A row is built in when its index is
 *     below the engine's count of compiled models (bento_edgeai_diag);
 *     when that cannot be read, "builtin" is unknown (the original's
 *     None, as on the Emulator), which BUILTIN = true accepts and
 *     BUILTIN = false refuses.
 *   - A board without edge_ai (import fails there) answers
 *     BENTO_E_UNSUPPORTED here, so it prints "cannot read models:" and
 *     shows the "Model not on this board" card.
 *   - A board without `buttons` (the Eva Kit) is one where
 *     bento_buttons_count() is 0.
 *   - edge_ai.select() and edge_ai.stop() wait inside the binding; here
 *     they are started and polled every 20 ms, so the IDE's Stop ends a
 *     wait at once. The OSError texts are the binding's own words.
 *   - The class Stop is a stop text in the program's state: the loop
 *     ends when it is set, then the status line shows it.
 *   - gc.collect() and the free-heap print are left out: C has no
 *     garbage collector.
 *   - A score is score_pct / 100 there, so int(score * 100 + 0.5) is
 *     score_pct exactly, and the percentages here are integers.
 *   - The burst times of the last T3_MS are a fixed table of
 *     BURSTS_MAX entries (the original's list has no limit). Bursts
 *     are at least 400 ms apart (a result every 100 ms, then GAP_MS of
 *     quiet), so 4 s hold about 10; a full table drops its oldest.
 *   - The IDE's Stop still runs the finally (the model is stopped),
 *     then the program returns 0.
 *   - In the BENTO Emulator, Edge AI from C is not available yet: the
 *     program says so and then shows the "Model not on this board"
 *     card, as the original does where the model is missing.
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

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
/* The exact name, looked up every run. */
static const char MODEL[] = "Alarm Detection";
/* false = deployed from the Store */
static const bool BUILTIN = true;
/* K, N: a beep burst = K of the last N results at Confirm at.
 * DEFAULT_HEARING, DEFAULT_CONFIRM, DEFAULT_CLEAR: DEF_TH in the .py,
 * the fallback thresholds (Hearing at, Confirm at, clear below). */
enum {
    K = 2,
    N = 3,
    REFRACT_MS = 1500, /* no second event sooner than this */
    HOLD_MS = 2000,    /* an event stays on screen this long */
    DEFAULT_HEARING = 70,
    DEFAULT_CONFIRM = 85,
    DEFAULT_CLEAR = 55,
    POLL_MS = 100,
    DRAW_MS = 500,
    GAP_MS = 300, /* quiet between bursts */
    T3_MS = 4000, /* 2 bursts in T3_MS = an alarm */
    SNOOZE_S = 60 /* shelving */
};
/* ---- end of settings ---- */

enum {
    ALARM_BURSTS = 2 /* bursts within T3_MS that make an alarm */
};

enum { /* times */
    LOOP_MS = 20,
    SETTLE_MS = 150,
    OP_POLL_MS = 20,
    SILENT_LIMIT_MS = 3000,
    MS_PER_S = 1000
};

enum { /* sizes */
    MAX_CLASSES = BENTO_EDGEAI_MAX_CLASSES,
    LOG_LINES = 4,
    VU_LEDS = 10,
    TEXT_BYTES = 128,
    CLOCK_BYTES = 36,
    PERCENT_ROUND = 5,
    PERCENT_PER_LED = 10,
    BURSTS_MAX = 16
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

/* Episode states; a drawn table has an entry per state below SEEN. */
enum {
    STATE_NORMAL = 0,
    STATE_HEARD,
    STATE_UNACK,
    STATE_ACKED,
    STATE_ENDED,
    STATE_SEEN
};

enum { /* layout */
    TITLE_X = 12,
    TITLE_Y = 4,
    TITLE_PX = 24,
    NOTE_Y = 38,
    TEXT_PX = 16,
    HEART_X = 756,
    HEART_Y = 6,
    HEART_SIZE = 26,
    CARD_Y = 64,
    CARD_H = 272,
    CARD_RADIUS = 12,
    CARD_TITLE_DX = 14,
    CARD_TITLE_DY = 8,
    NOW_X = 12,
    NOW_W = 470,
    DISC_X = 28,
    DISC_Y = 100,
    DISC_SIZE = 72,
    VERDICT_X = 116,
    VERDICT_Y = 104,
    VERDICT_PX = 28,
    STATE_Y = 144,
    VU_X = 28,
    VU_STEP = 44,
    VU_Y = 190,
    VU_W = 36,
    VU_H = 20,
    LEGEND_X = 28,
    LEGEND_Y = 218,
    SNOOZE_X = 336,
    SNOOZE_Y = 248,
    SNOOZE_W = 132,
    SNOOZE_H = 76,
    SNOOZE_PX = 16,
    LOG_X = 494,
    LOG_W = 294,
    COUNT_X = 508,
    COUNT_Y = 96,
    COUNT_W = 120,
    COUNT_H = 56,
    LINES_X = 508,
    LINES_Y = 166,
    LINE_STEP = 24,
    ACKNOWLEDGE_X = 508,
    ACKNOWLEDGE_Y = 266,
    ACKNOWLEDGE_W = 266,
    ACKNOWLEDGE_H = 60,
    ACKNOWLEDGE_PX = 24,
    STATUS_X = 12,
    STATUS_Y = 346,
    MISSING_X = 96,
    MISSING_Y = 110,
    MISSING_W = 600,
    MISSING_H = 180,
    MISSING_TEXT_X = 120,
    MISSING_HEAD_Y = 126,
    MISSING_BODY_Y = 170,
    MISSING_PX = 20,
    EMU_NOTE_Y = 310 /* below the missing card */
};

/* Where a text goes and how it looks. */
typedef struct {
    int32_t x;
    int32_t y;
    uint32_t color;
    int32_t size_px;
} pen_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} box_t;

/* A Panel: fill, border colour (min=), corner radius (max=) and border
 * width (value=). */
typedef struct {
    uint32_t fill;
    uint32_t border;
    int32_t radius;
    int32_t border_px;
} panel_style_t;

/* A widget the program changes, with what it was last sent: a full
 * display queue drops commands, so only a change is sent. */
typedef struct {
    bento_ui_id_t id;
    bool has_text;
    char text[TEXT_BYTES];
    bool has_color;
    uint32_t color;
} shown_t;

typedef struct {
    bento_ui_id_t heartbeat;
    shown_t now_card;
    shown_t log_card;
    shown_t disc;
    shown_t verdict;
    shown_t state;
    bento_ui_id_t vu[VU_LEDS];
    shown_t legend;
    shown_t snooze_button;
    shown_t count;
    shown_t lines[LOG_LINES];
    shown_t acknowledge;
    shown_t status;
    int32_t lit;
} screen_t;

/* One event per press: holding does not repeat. */
typedef struct {
    uint32_t index;
    bool down;
    bool clicked;
} board_button_t;

/* The last N scores; `needed` of them at or above a level =
 * confirmed. */
typedef struct {
    int32_t needed;
    int32_t count;
    int32_t scores[N];
} vote_t;

/* Latched: NORMAL > HEARD > UNACK > ACKED or ENDED, and only ACK ends
 * it. */
typedef struct {
    bool latch;
    int32_t state;
    int32_t events;
    bool has_time;
    uint32_t time_ms;
    char end[CLOCK_BYTES];
} episode_t;

/* The strongest event class (class 0 = nothing) and its thresholds. */
typedef struct {
    uint32_t klass;
    int32_t percent;
    int32_t enter;
    int32_t confirm;
    int32_t exit;
} event_t;

typedef struct {
    uint32_t index;
    uint32_t class_count;
    int32_t rows;
    char labels[MAX_CLASSES][BENTO_EDGEAI_LABEL_LEN];
} model_t;

/* Only new results count; a result() that keeps failing ends the
 * program. */
typedef struct {
    bool has_sequence;
    uint32_t last_sequence;
    bool failing;
    uint32_t failing_since_ms;
} reader_t;

typedef struct {
    bento_ui_t *ui;
    bool has_buttons;
    screen_t screen;
    model_t model;
    bento_edgeai_thresholds_t thresholds;
    reader_t reader;
    bool stopped;
    char stop_text[TEXT_BYTES];
    board_button_t pause_button;
    board_button_t acknowledge_button;
    vote_t vote;
    episode_t alarm;
    uint32_t bursts[BURSTS_MAX];
    int32_t bursts_count;
    bool in_burst;
    bool has_burst_end;
    uint32_t burst_end_ms;
    bool shelved;
    uint32_t shelved_ms;
    char log[LOG_LINES][TEXT_BYTES];
    int32_t log_count;
    bool paused;
    bool blink;
    int32_t peak;
    uint32_t drawn_ms;
    uint32_t polled_ms;
} alert_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern alert_t g_alert;
alert_t g_alert;

/* Sleeps, and turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* A position or size on the screen: the widget arguments are 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

/* ui.poll() here only keeps the queue empty; a failed poll answers no
 * events. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static void put_text(bento_ui_t *ui, shown_t *shown, const char *text)
{
    if (!shown->has_text || (strcmp(shown->text, text) != 0)) {
        shown->has_text = true;
        (void)snprintf(shown->text, sizeof(shown->text), "%s", text);
        /* a setter's refusal was ignored before too */
        (void)bento_ui_set_text(ui, shown->id, text,
                                (uint32_t)strlen(text));
    }
}

static void put_color(bento_ui_t *ui, shown_t *shown, uint32_t color)
{
    if (!shown->has_color || (shown->color != color)) {
        shown->has_color = true;
        shown->color = color;
        /* a setter's refusal was ignored before too */
        (void)bento_ui_set_color(ui, shown->id, color);
    }
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const pen_t *pen, bento_ui_id_t *out)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = px(pen->x);
    args.y = px(pen->y);
    args.color = pen->color;
    args.font_px = pen->size_px;
    return bento_ui_label_create(ui, &args, out);
}

static bento_status_t add_panel(bento_ui_t *ui, const box_t *box,
                                const panel_style_t *style,
                                bento_ui_id_t *out)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = px(box->x);
    args.y = px(box->y);
    args.w = px(box->w);
    args.h = px(box->h);
    args.color = style->fill;
    args.min_val = (int32_t)style->border;
    args.max_val = style->radius;
    args.font_px = style->border_px;
    return bento_ui_panel_create(ui, &args, out);
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 const box_t *box, int32_t size_px,
                                 bento_ui_id_t *out)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = px(box->x);
    args.y = px(box->y);
    args.w = px(box->w);
    args.h = px(box->h);
    args.color = COLOR_BUTTON;
    args.font_px = size_px;
    return bento_ui_button_create(ui, &args, out);
}

/* An Led, off. */
static bento_status_t add_led(bento_ui_t *ui, const box_t *box,
                              uint32_t color, bento_ui_id_t *out)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = px(box->x);
    args.y = px(box->y);
    args.w = px(box->w);
    args.h = px(box->h);
    args.color = color;
    args.font_px = 0;
    return bento_ui_led_create(ui, &args, out);
}

/* A card: a framed panel with a blue title; *out is the panel. */
static bento_status_t add_card(bento_ui_t *ui, const box_t *box,
                               const char *title, bento_ui_id_t *out)
{
    const panel_style_t style = {COLOR_CARD, COLOR_DIM, CARD_RADIUS, 1};
    bento_ui_id_t unused;
    pen_t pen;
    bento_status_t status = add_panel(ui, box, &style, out);

    if (status == BENTO_OK) {
        pen.x = box->x + CARD_TITLE_DX;
        pen.y = box->y + CARD_TITLE_DY;
        pen.color = COLOR_INFO;
        pen.size_px = TEXT_PX;
        status = add_label(ui, title, &pen, &unused);
    }
    return status;
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

/* How many rows the firmware compiled in: those are built in. -1 when
 * the board cannot say (the binding's None). */
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

/* BUILTIN = true takes a built-in row or an unknown one; false takes a
 * Store row only. */
static bool source_matches(int32_t row, int32_t compiled)
{
    bool matches = BUILTIN;

    if (compiled >= 0) {
        matches = BUILTIN ? (row < compiled) : (row >= compiled);
    }
    return matches;
}

/* The found row: its index, labels ("" for one that cannot be read) and
 * the row count. */
static void keep_model(const bento_edgeai_model_info_t *info,
                       int32_t row, int32_t rows, model_t *model)
{
    uint32_t length = 0U;
    uint32_t klass;

    model->index = (uint32_t)row;
    model->rows = rows;
    model->class_count = info->class_count;
    if (model->class_count > (uint32_t)MAX_CLASSES) {
        model->class_count = MAX_CLASSES;
    }
    for (klass = 0U; klass < model->class_count; klass++) {
        if (bento_edgeai_label(
                model->index, klass, model->labels[klass],
                BENTO_EDGEAI_LABEL_LEN, &length) != BENTO_OK) {
            model->labels[klass][0] = '\0';
        }
    }
}

/* find_model(): the first row with the name and the wanted source.
 * Never reads the id. */
static bool find_model(model_t *model)
{
    bento_edgeai_model_info_t info;
    int32_t rows = 0;
    int32_t row;
    bool found = false;
    bento_status_t status = bento_edgeai_count(&rows);
    const int32_t compiled = compiled_count();

    for (row = 0; (row < rows) && (status == BENTO_OK); row++) {
        bento_edgeai_model_info_init(&info);
        status = bento_edgeai_model_info((uint32_t)row, &info);
        if ((status == BENTO_OK) && !found &&
            (strcmp(info.name, MODEL) == 0) &&
            source_matches(row, compiled)) {
            keep_model(&info, row, rows, model);
            found = true;
        }
    }
    if (status != BENTO_OK) {
        (void)printf("cannot read models: %s\n",
                     link_error_text(status));
        found = false;
    }
    return found;
}

/* missing(): the model is not on this board. */
static bento_status_t show_missing(bento_ui_t *ui)
{
    const box_t box = {MISSING_X, MISSING_Y, MISSING_W, MISSING_H};
    const panel_style_t style = {COLOR_CARD, COLOR_BAD, CARD_RADIUS, 1};
    const pen_t head = {MISSING_TEXT_X, MISSING_HEAD_Y, COLOR_BAD,
                        TITLE_PX};
    const pen_t body = {MISSING_TEXT_X, MISSING_BODY_Y, COLOR_TEXT,
                        MISSING_PX};
    char text[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_ui_id_t body_id;
    bento_status_t status;

    (void)snprintf(
        text, sizeof(text),
        "%s\nis built into the TESAIoT Dev Kit.\nNot on this board.",
        MODEL);
    (void)printf("%s\n", text);
    status = bento_ui_screen_default(ui);
    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_panel(ui, &box, &style, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "Model not on this board", &head, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, " ", &body, &body_id);
    }
    if (status == BENTO_OK) {
        /* set afterwards: a text sent this way fits 126 bytes */
        (void)bento_ui_set_text(ui, body_id, text,
                                (uint32_t)strlen(text));
        drain_events(ui);
    }
    return status;
}

/* The BENTO Emulator's C runtime has no Edge AI yet: it answers
 * BENTO_E_UNSUPPORTED. The note goes below the missing card, as wide
 * as the card, so it wraps there. */
static bento_status_t show_emulator_note(bento_ui_t *ui)
{
    static const char note[] = "Edge AI จาก C ยังไม่มีใน BENTO Emulator "
                               "ให้รันบน Dev Kit หรือ Eva Kit";
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = note;
    args.text_len = (uint32_t)strlen(note);
    args.x = MISSING_X;
    args.y = EMU_NOTE_Y;
    args.w = MISSING_W;
    args.color = COLOR_WARN;
    args.font_px = MISSING_PX;
    status = bento_ui_label_create(ui, &args, &unused);
    if (status == BENTO_OK) {
        drain_events(ui);
    }
    return status;
}

static void set_stop(alert_t *alert, const char *text)
{
    alert->stopped = true;
    (void)snprintf(alert->stop_text, sizeof(alert->stop_text), "%s",
                   text);
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
            /* Stop wins; the cancel's answer changes nothing */
            (void)bento_edgeai_op_cancel();
        }
    }
    return status;
}

/* stop_ai(): stopped already is fine, and an OSError is passed over. */
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

/* edge_ai.select(): loads and starts the model, up to 15 s. *why is the
 * OSError's text. */
static bento_status_t select_model(uint32_t index, const char **why)
{
    bento_status_t status = bento_edgeai_select_start(index);

    *why = "edge_ai: select send failed";
    if (status == BENTO_OK) {
        *why = "edge_ai: select not confirmed";
        status = finish_op();
    }
    return status;
}

static const char *help_text(const alert_t *alert)
{
    return alert->has_buttons ? "Listening - SW6 = ACK, SW5 = pause"
                              : "Listening";
}

/* start(): on a resume (check_rows) it stops the program when a model
 * was added or removed meanwhile. */
static bento_status_t start_model(alert_t *alert, bool check_rows)
{
    char text[TEXT_BYTES];
    const char *why = "";
    int32_t rows = 0;
    bento_status_t link = BENTO_OK;
    bento_status_t status = BENTO_OK;

    (void)snprintf(text, sizeof(text), "Loading %s ...", MODEL);
    put_text(alert->ui, &alert->screen.status, text);
    put_color(alert->ui, &alert->screen.status, COLOR_WARN);
    drain_events(alert->ui);
    alert->reader.failing = false;
    if (check_rows && (alert->model.rows != 0)) {
        link = bento_edgeai_count(&rows);
        why = link_error_text(link);
    }
    if ((link == BENTO_OK) && check_rows && (alert->model.rows != 0) &&
        (rows != alert->model.rows)) {
        set_stop(alert, "Model list changed - run again");
    } else if (link == BENTO_OK) {
        link = select_model(alert->model.index, &why);
    } else {
        /* the count's OSError, reported below */
    }
    if (link == BENTO_E_CANCELLED) {
        status = link;
    } else if (link != BENTO_OK) {
        (void)printf("select: %s\n", why);
        (void)snprintf(text, sizeof(text), "Cannot start %s", MODEL);
        set_stop(alert, text);
    } else if (!alert->stopped) {
        put_text(alert->ui, &alert->screen.status, help_text(alert));
        put_color(alert->ui, &alert->screen.status, COLOR_TEXT);
    } else {
        /* the model list changed */
    }
    return status;
}

/* read_th(): per class Hearing at, Confirm at and clear below; the
 * defaults for every class when the board cannot say. */
static void read_thresholds(alert_t *alert)
{
    bento_edgeai_thresholds_t *limits = &alert->thresholds;
    uint32_t klass;

    bento_edgeai_thresholds_init(limits);
    if (bento_edgeai_thresholds(alert->model.index, limits) !=
        BENTO_OK) {
        for (klass = 0U; klass < (uint32_t)MAX_CLASSES; klass++) {
            limits->enter[klass] = DEFAULT_HEARING;
            limits->confirm[klass] = DEFAULT_CONFIRM;
            limits->exit[klass] = DEFAULT_CLEAR;
        }
    }
}

/* read_result(): true for a NEW result of this model only. */
static bool read_result(alert_t *alert, bento_edgeai_result_t *result)
{
    reader_t *reader = &alert->reader;
    const uint32_t now_ms = bento_time_ticks_ms();
    bool fresh = false;

    bento_edgeai_result_init(result);
    if (bento_edgeai_result(result) != BENTO_OK) {
        if (!reader->failing) {
            reader->failing = true;
            reader->failing_since_ms = now_ms;
        }
        if (bento_time_ticks_diff(now_ms, reader->failing_since_ms) >=
            SILENT_LIMIT_MS) {
            set_stop(alert, "AI core not answering - run again");
        }
    } else {
        reader->failing = false;
        fresh = result->has_verdict &&
                (!reader->has_sequence ||
                 (result->seq != reader->last_sequence)) &&
                ((uint32_t)result->index == alert->model.index);
        if (fresh) {
            reader->has_sequence = true;
            reader->last_sequence = result->seq;
        }
    }
    return fresh;
}

static void event_of(const alert_t *alert,
                     const bento_edgeai_result_t *result,
                     event_t *event)
{
    uint32_t count = result->class_count;
    uint32_t strongest = 0U;
    uint32_t klass;

    if (count > (uint32_t)MAX_CLASSES) {
        count = MAX_CLASSES;
    }
    for (klass = 1U; klass < count; klass++) {
        if ((result->score_pct[klass] > result->score_pct[strongest]) ||
            (strongest == 0U)) {
            strongest = klass;
        }
    }
    event->klass = strongest;
    event->percent = (int32_t)result->score_pct[strongest];
    event->enter = (int32_t)alert->thresholds.enter[strongest];
    event->confirm = (int32_t)alert->thresholds.confirm[strongest];
    event->exit = (int32_t)alert->thresholds.exit[strongest];
}

static void vote_reset(vote_t *vote, int32_t needed)
{
    vote->needed = needed;
    vote->count = 0;
}

/* Keeps the last N scores, oldest first. */
static void vote_add(vote_t *vote, int32_t percent)
{
    int32_t i;

    if (vote->count == N) {
        for (i = 1; i < N; i++) {
            vote->scores[i - 1] = vote->scores[i];
        }
        vote->count--;
    }
    vote->scores[vote->count] = percent;
    vote->count++;
}

static bool vote_hits(const vote_t *vote, int32_t level)
{
    int32_t hits = 0;
    int32_t i;

    for (i = 0; i < vote->count; i++) {
        if (vote->scores[i] >= level) {
            hits++;
        }
    }
    return hits >= vote->needed;
}

static int32_t vote_max(const vote_t *vote)
{
    int32_t best = 0;
    int32_t i;

    for (i = 0; i < vote->count; i++) {
        if ((i == 0) || (vote->scores[i] > best)) {
            best = vote->scores[i];
        }
    }
    return best;
}

/* A full window, every score below the level. */
static bool vote_clear(const vote_t *vote, int32_t level)
{
    return (vote->count == N) && (vote_max(vote) < level);
}

/* time.localtime() as HH:MM:SS; a clock that cannot be read gives
 * 00:00:00. */
static void clock_text(char *text, size_t cap)
{
    bento_time_tm_t now;

    bento_time_tm_init(&now);
    if (bento_time_localtime_now(&now) != BENTO_OK) {
        bento_time_tm_init(&now);
    }
    (void)snprintf(text, cap, "%02" PRId32 ":%02" PRId32 ":%02" PRId32,
                   now.hour, now.min, now.sec);
}

static void episode_start(episode_t *episode, bool latch)
{
    (void)memset(episode, 0, sizeof(*episode));
    episode->latch = latch;
    episode->state = STATE_NORMAL;
}

/* A confirm: a new event unless the episode is latched or still in its
 * refractory time since the last confirm. */
static bool episode_confirm(episode_t *episode, uint32_t now_ms)
{
    const bool latched = (episode->state == STATE_UNACK) ||
                         (episode->state == STATE_ACKED);
    bool fired = false;

    if (!latched && (!episode->has_time ||
                     (bento_time_ticks_diff(now_ms, episode->time_ms) >=
                      REFRACT_MS))) {
        episode->state = episode->latch ? STATE_UNACK : STATE_SEEN;
        episode->events++;
        episode->has_time = true;
        episode->time_ms = now_ms;
        fired = true;
    } else if (latched) {
        episode->time_ms = now_ms;
    } else {
        /* refractory: not a new event */
    }
    return fired;
}

/* The scores fell below "clear below": an unacknowledged alarm ends
 * (and stays until ACK). */
static int32_t cleared_state(episode_t *episode, int32_t state)
{
    int32_t next = STATE_NORMAL;

    if (state == STATE_UNACK) {
        next = STATE_ENDED;
        clock_text(episode->end, sizeof(episode->end));
    } else if (state == STATE_ENDED) {
        next = STATE_ENDED;
    } else {
        /* back to normal */
    }
    return next;
}

/* True once per new event. */
static bool episode_step(episode_t *episode, bool heard, bool confirm,
                         bool clear, uint32_t now_ms)
{
    int32_t state = episode->state;
    bool fired = false;

    if (confirm) {
        fired = episode_confirm(episode, now_ms);
    }
    if (!fired) {
        if (clear) {
            state = cleared_state(episode, state);
        } else if (heard && (state == STATE_NORMAL)) {
            state = STATE_HEARD;
        } else {
            /* unchanged */
        }
        episode->state = state;
    }
    return fired;
}

static void episode_acknowledge(episode_t *episode)
{
    if (episode->state == STATE_UNACK) {
        episode->state = STATE_ACKED;
    } else if (episode->state == STATE_ENDED) {
        episode->state = STATE_NORMAL;
    } else {
        /* nothing to acknowledge */
    }
}

/* 10 LEDs; only the ones that change are switched. */
static void meter(alert_t *alert, int32_t percent)
{
    screen_t *screen = &alert->screen;
    const int32_t lit = (percent + PERCENT_ROUND) / PERCENT_PER_LED;
    int32_t low = (lit < screen->lit) ? lit : screen->lit;
    int32_t high = (lit > screen->lit) ? lit : screen->lit;
    int32_t i;

    if (high > VU_LEDS) {
        high = VU_LEDS;
    }
    for (i = low; i < high; i++) {
        /* a setter's refusal was ignored before too */
        (void)bento_ui_set_value(alert->ui, screen->vu[i],
                                 (i < lit) ? 1 : 0);
    }
    screen->lit = lit;
}

static bento_status_t add_header(alert_t *alert)
{
    const pen_t title = {TITLE_X, TITLE_Y, COLOR_TEXT, TITLE_PX};
    const pen_t note = {TITLE_X, NOTE_Y, COLOR_DIM, TEXT_PX};
    const box_t heart = {HEART_X, HEART_Y, HEART_SIZE, HEART_SIZE};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(alert->ui, "Alarm Listener", &title, &unused);

    if (status == BENTO_OK) {
        status = add_led(alert->ui, &heart, COLOR_OK,
                         &alert->screen.heartbeat);
    }
    if (status == BENTO_OK) {
        status =
            add_label(alert->ui,
                      "Teaching demo - NOT a safety device. Detects "
                      "alarm beeps, not smoke or CO.",
                      &note, &unused);
    }
    return status;
}

/* The "Now" card: the round light, the verdict and the state line. */
static bento_status_t add_now_card(alert_t *alert)
{
    const box_t card = {NOW_X, CARD_Y, NOW_W, CARD_H};
    const box_t disc = {DISC_X, DISC_Y, DISC_SIZE, DISC_SIZE};
    const panel_style_t round = {COLOR_DIM, COLOR_DIM, DISC_SIZE / 2,
                                 0};
    const pen_t verdict = {VERDICT_X, VERDICT_Y, COLOR_DIM, VERDICT_PX};
    const pen_t state = {VERDICT_X, STATE_Y, COLOR_DIM, TEXT_PX};
    screen_t *screen = &alert->screen;
    bento_status_t status =
        add_card(alert->ui, &card, "Now", &screen->now_card.id);

    /* a round Panel: an Led keeps its first colour on the board */
    if (status == BENTO_OK) {
        status = add_panel(alert->ui, &disc, &round, &screen->disc.id);
    }
    if (status == BENTO_OK) {
        status = add_label(alert->ui, "Quiet", &verdict,
                           &screen->verdict.id);
    }
    if (status == BENTO_OK) {
        status = add_label(alert->ui, " ", &state, &screen->state.id);
    }
    return status;
}

/* The LED meter, its legend and the Snooze button. */
static bento_status_t add_meter(alert_t *alert)
{
    const pen_t legend = {LEGEND_X, LEGEND_Y, COLOR_DIM, TEXT_PX};
    const box_t snooze_box = {SNOOZE_X, SNOOZE_Y, SNOOZE_W, SNOOZE_H};
    screen_t *screen = &alert->screen;
    char text[TEXT_BYTES];
    box_t led = {VU_X, VU_Y, VU_W, VU_H};
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < VU_LEDS) && (status == BENTO_OK); i++) {
        led.x = VU_X + (VU_STEP * i);
        status = add_led(alert->ui, &led, COLOR_TEXT, &screen->vu[i]);
    }
    if (status == BENTO_OK) {
        status = add_label(alert->ui, " ", &legend, &screen->legend.id);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "Snooze %d s", SNOOZE_S);
        status = add_button(alert->ui, text, &snooze_box, SNOOZE_PX,
                            &screen->snooze_button.id);
    }
    return status;
}

/* The "Alarms" card: the count and the log lines. */
static bento_status_t add_log_card(alert_t *alert)
{
    const box_t card = {LOG_X, CARD_Y, LOG_W, CARD_H};
    screen_t *screen = &alert->screen;
    bento_ui_widget_args_t args;
    pen_t line = {LINES_X, LINES_Y, COLOR_TEXT, TEXT_PX};
    int32_t i;
    bento_status_t status =
        add_card(alert->ui, &card, "Alarms", &screen->log_card.id);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "0";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = COUNT_X;
        args.y = COUNT_Y;
        args.w = COUNT_W;
        args.h = COUNT_H;
        args.color = COLOR_TEXT;
        status =
            bento_ui_seg7_create(alert->ui, &args, &screen->count.id);
    }
    for (i = 0; (i < LOG_LINES) && (status == BENTO_OK); i++) {
        line.y = LINES_Y + (LINE_STEP * i);
        status = add_label(alert->ui, " ", &line, &screen->lines[i].id);
    }
    return status;
}

static bento_status_t add_footer(alert_t *alert)
{
    const box_t acknowledge = {ACKNOWLEDGE_X, ACKNOWLEDGE_Y,
                               ACKNOWLEDGE_W, ACKNOWLEDGE_H};
    const pen_t status_pen = {STATUS_X, STATUS_Y, COLOR_WARN, TEXT_PX};
    screen_t *screen = &alert->screen;
    bento_status_t status =
        add_button(alert->ui, "ACK", &acknowledge, ACKNOWLEDGE_PX,
                   &screen->acknowledge.id);

    if (status == BENTO_OK) {
        status =
            add_label(alert->ui, " ", &status_pen, &screen->status.id);
    }
    return status;
}

/* build(): this model's screen (a T1 annunciator). */
static bento_status_t build_screen(alert_t *alert)
{
    bento_status_t status = bento_ui_screen_default(alert->ui);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_header(alert);
    }
    if (status == BENTO_OK) {
        status = add_now_card(alert);
    }
    if (status == BENTO_OK) {
        status = add_meter(alert);
    }
    if (status == BENTO_OK) {
        status = add_log_card(alert);
    }
    if (status == BENTO_OK) {
        status = add_footer(alert);
    }
    if (status == BENTO_OK) {
        drain_events(alert->ui);
    }
    return status;
}

/* The state line's text for state (a latched episode never reaches
 * SEEN); bursts = bursts in the last T3_MS. */
static void state_text(const alert_t *alert, int32_t state,
                       int32_t percent, int32_t bursts, char *text,
                       size_t cap)
{
    switch (state) {
    case STATE_HEARD:
        (void)snprintf(text, cap,
                       "! hearing %" PRId32 "%% - bursts %" PRId32,
                       percent, bursts);
        break;
    case STATE_UNACK:
        (void)snprintf(text, cap, "%s", "!! ALARM SOUND - tap ACK");
        break;
    case STATE_ACKED:
        (void)snprintf(text, cap, "%s", "ACKED - still hearing");
        break;
    case STATE_ENDED:
        (void)snprintf(text, cap, "ENDED %s - tap ACK",
                       alert->alarm.end);
        break;
    default:
        (void)snprintf(text, cap, "score %" PRId32 "%%", percent);
        break;
    }
}

/* draw(): peak -1 = no new result; bursts = bursts in the last T3_MS.
 * Both cards take over until ACK; UNACK and ENDED flash at 1 Hz. */
static void draw(alert_t *alert, int32_t bursts)
{
    static const uint32_t disc_colors[2][STATE_SEEN] = {
        {COLOR_DIM, COLOR_WARN, COLOR_ALARM_BG, COLOR_BAD,
         COLOR_ALARM_BG},
        {COLOR_DIM, COLOR_WARN, COLOR_BAD, COLOR_BAD, COLOR_DIM}};
    static const uint32_t state_colors[STATE_SEEN] = {
        COLOR_DIM, COLOR_WARN, COLOR_TEXT, COLOR_TEXT, COLOR_TEXT};
    static const char *const verdicts[STATE_SEEN] = {
        "Quiet", "Hearing", "ALARM SOUND", "ALARM SOUND", "Quiet"};
    static const uint32_t verdict_colors[STATE_SEEN] = {
        COLOR_DIM, COLOR_WARN, COLOR_BAD, COLOR_BAD, COLOR_DIM};
    screen_t *screen = &alert->screen;
    const int32_t state = (alert->alarm.state < STATE_SEEN)
                              ? alert->alarm.state
                              : STATE_NORMAL;
    const int32_t percent = (alert->peak > 0) ? alert->peak : 0;
    const bool alarm = (state == STATE_UNACK) || (state == STATE_ENDED);
    const bool beat = alert->blink && (alert->peak >= 0);
    const uint32_t card = alarm ? COLOR_ALARM_BG : COLOR_CARD;
    char text[TEXT_BYTES];

    /* the heartbeat; a setter's refusal was ignored before too */
    (void)bento_ui_set_value(alert->ui, screen->heartbeat,
                             beat ? 1 : 0);
    put_color(alert->ui, &screen->now_card, card);
    put_color(alert->ui, &screen->log_card, card);
    put_color(alert->ui, &screen->disc,
              disc_colors[alert->blink ? 1 : 0][state]);
    state_text(alert, state, percent, bursts, text, sizeof(text));
    put_text(alert->ui, &screen->state, text);
    put_color(alert->ui, &screen->state, state_colors[state]);
    put_text(alert->ui, &screen->verdict, verdicts[state]);
    put_color(alert->ui, &screen->verdict, verdict_colors[state]);
    put_color(alert->ui, &screen->acknowledge,
              alarm ? COLOR_BAD : COLOR_BUTTON);
    meter(alert, percent);
}

/* A new event: the newest line first, printed, and the count. */
static void log_event(alert_t *alert, const event_t *event)
{
    char clock[CLOCK_BYTES];
    char text[TEXT_BYTES];
    int32_t line;

    clock_text(clock, sizeof(clock));
    for (line = LOG_LINES - 1; line > 0; line--) {
        (void)memcpy(alert->log[line], alert->log[line - 1],
                     sizeof(alert->log[line]));
    }
    (void)snprintf(alert->log[0], sizeof(alert->log[0]),
                   "%s %s %" PRId32 "%% #%" PRId32, clock,
                   alert->model.labels[event->klass],
                   vote_max(&alert->vote), alert->alarm.events);
    if (alert->log_count < LOG_LINES) {
        alert->log_count++;
    }
    (void)printf("%s\n", alert->log[0]);
    for (line = 0; line < alert->log_count; line++) {
        put_text(alert->ui, &alert->screen.lines[line],
                 alert->log[line]);
    }
    (void)snprintf(text, sizeof(text), "%" PRId32, alert->alarm.events);
    put_text(alert->ui, &alert->screen.count, text);
}

/* Keeps the time of a new burst (the oldest goes when the table is
 * full). */
static void keep_burst(alert_t *alert, uint32_t now_ms)
{
    int32_t i;

    if (alert->bursts_count == BURSTS_MAX) {
        for (i = 1; i < BURSTS_MAX; i++) {
            alert->bursts[i - 1] = alert->bursts[i];
        }
        alert->bursts_count--;
    }
    alert->bursts[alert->bursts_count] = now_ms;
    alert->bursts_count++;
}

/* A new burst starts when the vote hits after GAP_MS of quiet; a
 * shorter dip is the same burst. */
static void track_burst(alert_t *alert, bool hit, uint32_t now_ms)
{
    if (hit && !alert->in_burst &&
        (!alert->has_burst_end ||
         (bento_time_ticks_diff(now_ms, alert->burst_end_ms) >=
          GAP_MS))) {
        keep_burst(alert, now_ms);
    } else if (alert->in_burst && !hit) {
        alert->has_burst_end = true;
        alert->burst_end_ms = now_ms;
    } else {
        /* no change */
    }
    alert->in_burst = hit;
}

/* Keeps only the bursts of the last T3_MS. */
static void drop_old_bursts(alert_t *alert, uint32_t now_ms)
{
    int32_t kept = 0;
    int32_t i;

    for (i = 0; i < alert->bursts_count; i++) {
        if (bento_time_ticks_diff(now_ms, alert->bursts[i]) <= T3_MS) {
            alert->bursts[kept] = alert->bursts[i];
            kept++;
        }
    }
    alert->bursts_count = kept;
}

/* One new result: the bursts, then the alarm = 2 bursts in T3_MS; it is
 * over after T3_MS without one. */
static void take_result(alert_t *alert, uint32_t now_ms)
{
    bento_edgeai_result_t result;
    event_t event;
    bool clear;

    if (read_result(alert, &result)) {
        event_of(alert, &result, &event);
        vote_add(&alert->vote, event.percent);
        if (event.percent > alert->peak) {
            alert->peak = event.percent;
        }
        track_burst(alert, vote_hits(&alert->vote, event.confirm),
                    now_ms);
        drop_old_bursts(alert, now_ms);
        clear = (alert->bursts_count == 0) &&
                vote_clear(&alert->vote, event.exit);
        if (episode_step(&alert->alarm, event.percent >= event.enter,
                         alert->bursts_count >= ALARM_BURSTS, clear,
                         now_ms)) {
            log_event(alert, &event);
        }
    }
}

/* Reads a board button and remembers a new press (a rising edge). */
static bento_status_t sample_button(const alert_t *alert,
                                    board_button_t *button)
{
    bool pressed = false;
    bento_status_t status = BENTO_OK;

    if (alert->has_buttons) {
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

static bool event_is(const bento_ui_event_t *event, uint8_t kind,
                     bento_ui_id_t widget)
{
    return (event->type == kind) && (event->id == widget);
}

/* Snooze: shelved from now; the alarm still counts. */
static void shelve(alert_t *alert)
{
    alert->shelved = true;
    alert->shelved_ms = bento_time_ticks_ms();
}

/* The taps: ACK, or Snooze. A failed poll answers no events. */
static void read_taps(alert_t *alert, bool *acknowledge)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(alert->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (event_is(&events[i], BENTO_UI_EVENT_CLICKED,
                     alert->screen.acknowledge.id)) {
            *acknowledge = true;
        }
        if (event_is(&events[i], BENTO_UI_EVENT_CLICKED,
                     alert->screen.snooze_button.id)) {
            shelve(alert);
        }
    }
}

/* SW5: pause (the model stops) or resume. */
static bento_status_t toggle_pause(alert_t *alert)
{
    bento_status_t status;

    alert->paused = !alert->paused;
    vote_reset(&alert->vote, K);
    alert->in_burst = false;
    if (alert->paused) {
        status = stop_ai();
        put_text(alert->ui, &alert->screen.status,
                 "Paused - SW5 resumes");
        put_color(alert->ui, &alert->screen.status, COLOR_WARN);
    } else {
        status = start_model(alert, true);
    }
    return status;
}

/* The Snooze countdown; still sounding when it ends, the alarm is
 * announced again. Answers the seconds left, 0 when not shelved. */
static int32_t snooze_left(alert_t *alert, uint32_t now_ms)
{
    int32_t left = 0;

    if (alert->shelved) {
        left = SNOOZE_S -
               (bento_time_ticks_diff(now_ms, alert->shelved_ms) /
                MS_PER_S);
    }
    if (alert->shelved && (left <= 0)) {
        alert->shelved = false;
        if (alert->alarm.state == STATE_ACKED) {
            alert->alarm.state = STATE_UNACK;
        }
    }
    return left;
}

/* Every DRAW_MS: the blink, the Snooze line and the screen. */
static void redraw(alert_t *alert, uint32_t now_ms)
{
    const int32_t left = snooze_left(alert, now_ms);
    char text[TEXT_BYTES];

    alert->drawn_ms = now_ms;
    alert->blink = !alert->blink;
    if (!alert->paused && (left > 0)) {
        (void)snprintf(text, sizeof(text),
                       "SHELVED %" PRId32 " s - still counting", left);
        put_text(alert->ui, &alert->screen.status, text);
        put_color(alert->ui, &alert->screen.status, COLOR_WARN);
    } else if (!alert->paused) {
        put_text(alert->ui, &alert->screen.status, help_text(alert));
        put_color(alert->ui, &alert->screen.status, COLOR_TEXT);
    } else {
        /* paused: the status line keeps "Paused" */
    }
    draw(alert, alert->bursts_count);
    alert->peak = -1;
}

/* One round of the main loop: buttons and taps, a result every POLL_MS,
 * a redraw every DRAW_MS, then a short rest. */
static bento_status_t alert_round(alert_t *alert)
{
    bool acknowledge = false;
    uint32_t now_ms;
    bento_status_t status = sample_button(alert, &alert->pause_button);

    if (status == BENTO_OK) {
        status = sample_button(alert, &alert->acknowledge_button);
    }
    if (status == BENTO_OK) {
        acknowledge = button_clicked(&alert->acknowledge_button);
        read_taps(alert, &acknowledge);
        if (button_clicked(&alert->pause_button)) {
            status = toggle_pause(alert);
        }
    }
    now_ms = bento_time_ticks_ms();
    if ((status == BENTO_OK) && !alert->stopped && !alert->paused &&
        (bento_time_ticks_diff(now_ms, alert->polled_ms) >= POLL_MS)) {
        alert->polled_ms = now_ms;
        take_result(alert, now_ms);
    }
    if ((status == BENTO_OK) && !alert->stopped &&
        (acknowledge || alert->shelved)) {
        episode_acknowledge(&alert->alarm); /* shelved: never flashes */
    }
    if ((status == BENTO_OK) && !alert->stopped &&
        (bento_time_ticks_diff(now_ms, alert->drawn_ms) >= DRAW_MS)) {
        redraw(alert, now_ms);
    }
    if ((status == BENTO_OK) && !alert->stopped) {
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* run(): the thresholds, the first start, then the loop until a
 * stop. */
static bento_status_t run_alert(alert_t *alert)
{
    char text[TEXT_BYTES];
    bento_status_t status;

    read_thresholds(alert);
    (void)snprintf(text, sizeof(text),
                   "Hearing at %d%% | Confirm at %d%%",
                   (int)alert->thresholds.enter[1],
                   (int)alert->thresholds.confirm[1]);
    put_text(alert->ui, &alert->screen.legend, text);
    alert->pause_button.index = 1U;       /* SW5 */
    alert->acknowledge_button.index = 0U; /* SW6 */
    vote_reset(&alert->vote, K);
    episode_start(&alert->alarm, true);
    alert->peak = -1;
    status = start_model(alert, false);
    alert->drawn_ms = bento_time_ticks_ms();
    alert->polled_ms = alert->drawn_ms;
    while ((status == BENTO_OK) && !alert->stopped) {
        status = alert_round(alert);
    }
    return status;
}

/* The run, then what the original's except and finally do: show why it
 * stopped and always release the AI core. */
static bento_status_t run_then_release(alert_t *alert)
{
    const bento_status_t status = run_alert(alert);
    bento_status_t stop_status;

    if (alert->stopped) {
        put_text(alert->ui, &alert->screen.status, alert->stop_text);
        put_color(alert->ui, &alert->screen.status, COLOR_BAD);
        (void)printf("%s\n", alert->stop_text);
    }
    if (status == BENTO_E_CANCELLED) {
        /* it answers whether there was a Stop: there was */
        (void)bento_runtime_acknowledge_stop();
    }
    stop_status = stop_ai();
    return (status == BENTO_E_CANCELLED) ? stop_status : status;
}

/* The Emulator first says why, then does what the original does
 * where the model is not on the board. */
static bento_status_t run_in_emulator(bento_ui_t *ui)
{
    bento_status_t status;

    (void)printf("Edge AI จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit "
                 "หรือ Eva Kit / Edge AI from C is not in the BENTO "
                 "Emulator yet: run it on a Dev Kit or Eva Kit.\n");
    status = show_missing(ui);
    if (status == BENTO_OK) {
        status = show_emulator_note(ui);
    }
    return status;
}

int main(void)
{
    alert_t *alert = &g_alert;
    bool supported = false;
    bento_status_t status;

    (void)memset(alert, 0, sizeof(*alert));
    alert->ui = bento_runtime_get_ui();
    alert->has_buttons = bento_buttons_count() > 0U;
    if (bento_edgeai_supported(&supported) == BENTO_E_UNSUPPORTED) {
        status = run_in_emulator(alert->ui);
    } else if (!find_model(&alert->model)) {
        status = show_missing(alert->ui);
    } else {
        status = build_screen(alert);
        if (status == BENTO_OK) {
            status = run_then_release(alert);
        }
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
