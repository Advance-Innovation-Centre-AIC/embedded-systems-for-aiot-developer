/*
 * siren_detection_store.c -- ไซเรน A/B: โมเดลจาก Store เทียบกับโมเดลในตัว
 *
 * ต้องติดตั้ง SirenDetection จาก Edge AI Store ก่อน การ์ด A ฟังด้วยโมเดล
 * จาก Store การ์ด B ฟังด้วย Siren Detection ที่มากับบอร์ด ทีละโมเดล
 * เปิดคลิปเสียงไซเรน แตะ Swap แล้วเปิดเสียงเดิมอีกครั้ง แต่ละการ์ดนับ
 * ไซเรนที่เกณฑ์ Confirm at ของตัวเอง และบอกว่ายืนยันช้ากว่าคะแนนแรก
 * ที่ถึงเกณฑ์เท่าไร ปุ่ม SW6 หรือแตะ ACK = รับทราบ ปุ่ม SW5 = หยุดชั่วคราว
 *
 * Needs SirenDetection from the Edge AI Store. Card A listens with
 * the Store model, card B with the built-in Siren Detection, one at
 * a time. Play a siren clip, tap Swap, play it again. Each card
 * counts sirens at its own Confirm at and shows how much later than
 * the first score at Confirm at the siren was confirmed. SW6 or a
 * tap on ACK acknowledges, SW5 pauses.
 *
 * Twin of siren_detection_store.py
 * (embedded-systems-for-aiot-developer/examples/edge_ai_models/siren_detection_store.py).
 * Style: CLEAN_C.md. Closest library twin: devkit-21-ai-model-lab
 * (style only, the code is new).
 * Differs from the original:
 *   - In the BENTO Emulator, Edge AI from C is not available yet:
 *     the program says so and then shows the "Model not on this
 *     board" card, as the original does when the model is missing.
 *   - gc.collect() and the free-heap print are left out: C has no
 *     garbage collector.
 *   - m["builtin"] is "row index below diag.compiled_models"; when
 *     the board cannot report that it is None (the Emulator), so a
 *     built-in name accepts True or None and a Store name only False.
 *   - edge_ai.models() fails as one OSError: a link failure prints
 *     the binding's words for the C status. A row the engine refuses
 *     is skipped. A board without the model link answers every call
 *     with BENTO_E_UNSUPPORTED, so it prints that and shows the
 *     "Model not on this board" card.
 *   - edge_ai.select() and stop() wait inside the binding; here they
 *     are started and polled every 20 ms, and the IDE's Stop cancels
 *     the wait. A failed select prints "edge_ai: select send failed"
 *     or "edge_ai: select not confirmed", the binding's texts.
 *   - The Stop exception is a stop text kept in the program state:
 *     the loop ends, the text goes on the status line in red and is
 *     printed, then the AI core is stopped and the program ends.
 *   - put() keeps the last text and colour of each widget it changes
 *     in a small struct instead of a dict.
 *   - "%.1f" is written by bento_fmt in the board's float.
 *   - The IDE's Stop ends the loop; the finally still stops the AI
 *     core, where MicroPython raises KeyboardInterrupt.
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
static const char MODEL[] =
    "SirenDetection";              /* exact name, looked up every run */
static const bool BUILTIN = false; /* false = deployed from the Store */
enum {
    K = 3, /* an event = K of the last N results at Confirm at */
    N = 5,
    REFRACT_MS = 1500, /* no second event sooner than this */
    HOLD_MS = 2000     /* an event stays on screen this long */
};
/* DEF_TH in the .py: Hearing at, Confirm at, clear below (fallback) */
static const uint8_t DEF_THRESHOLDS[3] = {40U, 65U, 30U};
enum {
    POLL_MS = 100,
    DRAW_MS = 500
};
/* A = Store, B = built-in: one space apart, two models */
static const char *const NAMES[2] = {MODEL, "Siren Detection"};
/* ---- end of settings ---- */

enum { /* times, counts and sizes */
    LOOP_MS = 20,
    LOAD_MS = 150,   /* let the screen settle after ui.screen() */
    OP_POLL_MS = 20, /* how often a started select or stop is asked */
    FAIL_STOP_MS =
        3000, /* result() failing this long ends the program */
    MS_PER_S = 1000,
    SIDES = 2,
    MAX_CLASSES = BENTO_EDGEAI_MAX_CLASSES,
    TEXT_BYTES = 128, /* a SET_TEXT carries at most 126 bytes */
    DRAWN_STATES = 5, /* the screen tables cover NORMAL to ENDED */
    CLOCK_BYTES =
        36 /* "HH:MM:SS"; at worst three int32_t, two colons, the NUL */
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

enum { /* layout */
    TITLE_X = 12,
    TITLE_Y = 4,
    NOTE_Y = 38,
    TEXT_PX = 16,
    TITLE_PX = 24,
    BIG_PX = 28,
    NOW_PX = 20,
    CARD_RADIUS = 12,
    CARD_TITLE_DX = 14,
    CARD_TITLE_DY = 8,
    SIDE_X = 28,
    SIDE_STEP_X = 394,
    SIDE_CARD_DX = 16,
    SIDE_CARD_Y = 64,
    SIDE_CARD_W = 382,
    SIDE_CARD_H = 272,
    DISC_Y = 100,
    DISC_SIZE = 56,
    VERDICT_DX = 68,
    VERDICT_Y = 108,
    NOW_Y = 168,
    COUNT_Y = 198,
    DELAY_Y = 222,
    SWAP_X = 28,
    ACKNOWLEDGE_X = 422,
    BUTTON_Y = 272,
    BUTTON_W = 350,
    BUTTON_H = 56,
    SWAP_PX = 20,
    STATUS_X = 12,
    STATUS_Y = 346,
    BEAT_X = 756,
    BEAT_Y = 6,
    BEAT_SIZE = 26,
    MISSING_X = 96,
    MISSING_Y = 110,
    MISSING_W = 600,
    MISSING_H = 180,
    MISSING_TEXT_X = 120,
    MISSING_TITLE_Y = 126,
    MISSING_BODY_Y = 170,
    MISSING_PX = 20,
    EMULATOR_NOTE_Y =
        302 /* under the missing card, which ends at 290 */
};

/* An episode, as the original's Episode class names its states. */
typedef enum {
    EPISODE_NORMAL = 0,
    EPISODE_HEARD,
    EPISODE_UNACK,
    EPISODE_ACKED,
    EPISODE_ENDED,
    EPISODE_SEEN
} episode_state_t;

static const char SIDE_LETTERS[] = "AB";

/* One board button: one event per press, holding does not repeat. */
typedef struct {
    uint32_t index;
    bool down;
    bool clicked;
} board_button_t;

/* A widget whose text and colour are sent only when they change. */
typedef struct {
    bento_ui_id_t id;
    bool has_text;
    bool has_color;
    uint32_t color;
    char text[TEXT_BYTES];
} tracked_t;

/* A place for a text: where, its colour and its size. */
typedef struct {
    int32_t x;
    int32_t y;
    uint32_t color;
    int32_t size_px;
} text_place_t;

/* A rectangle on the screen. */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} box_t;

/* How a Panel looks: min= is its border colour, max= its corner
 * radius, value= its border width. */
typedef struct {
    uint32_t fill;
    uint32_t border;
    int32_t radius;
    int32_t border_px;
} panel_look_t;

/* A model the program found: its row and its labels. */
typedef struct {
    uint32_t index;
    uint32_t class_count;
    char labels[MAX_CLASSES][BENTO_EDGEAI_LABEL_LEN];
} model_t;

/* The last N scores. */
typedef struct {
    int32_t scores[N];
    uint32_t count;
} vote_t;

/* An alarm that latches until ACK. */
typedef struct {
    bool latch;
    episode_state_t state;
    int32_t events;
    bool has_time;
    uint32_t time_ms;
    char end[CLOCK_BYTES];
} episode_t;

/* One card: its model (or none), its alarm, its tallies and widgets. */
typedef struct {
    bool present;
    model_t model;
    bento_edgeai_thresholds_t thresholds;
    episode_t episode;
    vote_t vote;
    int32_t top;
    int32_t delay_ms;
    bool has_onset;
    uint32_t onset_ms;
    tracked_t disc;
    tracked_t verdict;
    tracked_t now_line;
    tracked_t count_line;
    tracked_t delay_line;
} side_t;

/* The strongest event class of one result and its thresholds. */
typedef struct {
    uint32_t klass;
    int32_t percent;
    int32_t hearing;
    int32_t confirm;
    int32_t clear;
} event_t;

/* Everything the program changes while it runs. */
typedef struct {
    bento_ui_t *ui;
    side_t sides[SIDES];
    tracked_t swap;
    tracked_t acknowledge;
    tracked_t status;
    bento_ui_id_t heartbeat;
    board_button_t sw5;
    board_button_t sw6;
    bool has_buttons;
    uint32_t row_count;
    uint32_t active;
    const char *model_name;
    bool paused;
    bool blink;
    int32_t peak;
    uint32_t drawn_ms;
    uint32_t polled_ms;
    bool has_sequence;
    uint32_t last_sequence;
    bool failing;
    uint32_t fail_since_ms;
    bool stopped;
    char stop_text[TEXT_BYTES];
} siren_t;

/* The program's state: too big for the 8 KB program stack, so at
 * file scope, zeroed by the loader on every load and set again at
 * the start of main() (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern siren_t g_siren;
siren_t g_siren;

/* A position or size on the screen: the widget arguments are 16-bit. */
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

/* The time now as HH:MM:SS; a refused clock read leaves 00:00:00. */
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

/* ui.poll() here only empties the queue; a failed poll answers no events. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* put(): sends a text only when it changed. A setter's refusal was
 * ignored before too (modui.c). */
static void put_text(bento_ui_t *ui, tracked_t *widget,
                     const char *text)
{
    if (!widget->has_text ||
        (strncmp(widget->text, text, sizeof(widget->text)) != 0)) {
        widget->has_text = true;
        (void)snprintf(widget->text, sizeof(widget->text), "%s", text);
        (void)bento_ui_set_text(ui, widget->id, text,
                                (uint32_t)strlen(text));
    }
}

/* put(): sends a colour only when it changed. */
static void put_color(bento_ui_t *ui, tracked_t *widget, uint32_t color)
{
    if (!widget->has_color || (widget->color != color)) {
        widget->has_color = true;
        widget->color = color;
        (void)bento_ui_set_color(ui, widget->id, color);
    }
}

/* Reads a board button and remembers a new press. No buttons (the
 * Eva Kit): nothing is read. */
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

/* raise Stop(text): the loops end and main() shows the text. */
static void raise_stop(siren_t *app, const char *text)
{
    app->stopped = true;
    (void)snprintf(app->stop_text, sizeof(app->stop_text), "%s", text);
}

/* How many rows the firmware compiled in, or -1 when the board
 * cannot say (the binding's None for "builtin"). */
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

/* The exact-name rule: a built-in name takes True or None, a Store
 * name only False. */
static bool row_matches(const bento_edgeai_model_info_t *info,
                        int32_t row, int32_t compiled, const char *name,
                        bool builtin)
{
    const bool known = compiled >= 0;
    const bool is_builtin = known && (row < compiled);
    const bool source_ok =
        builtin ? (!known || is_builtin) : (known && !is_builtin);

    return source_ok && (strcmp(info->name, name) == 0);
}

/* The model's labels, "" for one that cannot be read. */
static void read_labels(model_t *model)
{
    uint32_t length = 0U;
    uint32_t i;

    for (i = 0U; i < model->class_count; i++) {
        if (bento_edgeai_label(model->index, i, model->labels[i],
                               BENTO_EDGEAI_LABEL_LEN,
                               &length) != BENTO_OK) {
            model->labels[i][0] = '\0';
        }
    }
}

/* Keeps a matching row as the model. */
static void keep_model(const bento_edgeai_model_info_t *info,
                       int32_t row, model_t *model)
{
    model->index = (uint32_t)row;
    model->class_count = info->class_count;
    if (model->class_count > (uint32_t)MAX_CLASSES) {
        model->class_count = MAX_CLASSES;
    }
    read_labels(model);
}

/* One row of edge_ai.models(): counted, and kept when it is the
 * first with this exact name and source. A row the engine refuses
 * is skipped; any other failure is the listing's. */
static bento_status_t check_row(int32_t row, int32_t compiled,
                                const char *name, bool builtin,
                                model_t *model, uint32_t *row_count)
{
    bento_edgeai_model_info_t info;
    bento_status_t status;

    bento_edgeai_model_info_init(&info);
    status = bento_edgeai_model_info((uint32_t)row, &info);
    if ((status == BENTO_E_NOT_READY) ||
        (status == BENTO_E_NOT_FOUND)) {
        status = BENTO_OK;
    } else if (status == BENTO_OK) {
        (*row_count)++;
        if ((model->class_count == 0U) &&
            row_matches(&info, row, compiled, name, builtin)) {
            keep_model(&info, row, model);
        }
    } else {
        /* a link failure: the whole listing fails */
    }
    return status;
}

/* find_model(): the first row with this exact name and source, and
 * how many rows were listed. A link failure prints why and finds
 * nothing. A model always has a class, so class_count 0 is "none
 * kept yet". */
static bool find_model(const char *name, bool builtin, model_t *model,
                       uint32_t *row_count)
{
    int32_t rows = 0;
    int32_t row;
    bento_status_t status = bento_edgeai_count(&rows);
    const int32_t compiled =
        (status == BENTO_OK) ? compiled_count() : -1;

    *row_count = 0U;
    model->class_count = 0U;
    for (row = 0; (row < rows) && (status == BENTO_OK); row++) {
        status =
            check_row(row, compiled, name, builtin, model, row_count);
    }
    if (status != BENTO_OK) {
        (void)printf("cannot read models: %s\n",
                     link_error_text(status));
    }
    return (status == BENTO_OK) && (model->class_count != 0U);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const text_place_t *place,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = px(place->x);
    args.y = px(place->y);
    args.color = place->color;
    args.font_px = place->size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_panel(bento_ui_t *ui, const box_t *box,
                                const panel_look_t *look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = px(box->x);
    args.y = px(box->y);
    args.w = px(box->w);
    args.h = px(box->h);
    args.color = look->fill;
    args.min_val = (int32_t)look->border;
    args.max_val = look->radius;
    args.font_px = look->border_px;
    return bento_ui_panel_create(ui, &args, out_id);
}

/* card(): a dark card with its title. */
static bento_status_t add_card(bento_ui_t *ui, const box_t *box,
                               const char *title)
{
    static const panel_look_t look = {COLOR_CARD, COLOR_DIM,
                                      CARD_RADIUS, 1};
    const text_place_t place = {box->x + CARD_TITLE_DX,
                                box->y + CARD_TITLE_DY, COLOR_INFO,
                                TEXT_PX};
    bento_ui_id_t unused;
    bento_status_t status = add_panel(ui, box, &look, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, title, &place, &unused);
    }
    return status;
}

/* disc(): a round Panel, because an Led keeps its first colour on
 * the board. */
static bento_status_t add_disc(bento_ui_t *ui, int32_t x, int32_t y,
                               int32_t size, bento_ui_id_t *out_id)
{
    const box_t box = {x, y, size, size};
    const panel_look_t look = {COLOR_DIM, COLOR_DIM, size / 2, 0};

    return add_panel(ui, &box, &look, out_id);
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 const box_t *box, int32_t size_px,
                                 bento_ui_id_t *out_id)
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
    return bento_ui_button_create(ui, &args, out_id);
}

/* missing(): the model is not on this board. */
static bento_status_t show_missing(bento_ui_t *ui)
{
    static const box_t box = {MISSING_X, MISSING_Y, MISSING_W,
                              MISSING_H};
    static const panel_look_t look = {COLOR_CARD, COLOR_BAD,
                                      CARD_RADIUS, 1};
    static const text_place_t title = {MISSING_TEXT_X, MISSING_TITLE_Y,
                                       COLOR_BAD, TITLE_PX};
    static const text_place_t body = {MISSING_TEXT_X, MISSING_BODY_Y,
                                      COLOR_TEXT, MISSING_PX};
    char text[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_ui_id_t body_id;
    bento_status_t status;

    (void)snprintf(text, sizeof(text),
                   "Deploy %s\nfrom edgeai-store.tesaiot.dev\n(sign in "
                   "with GitHub) on a Dev Kit.",
                   MODEL);
    (void)printf("%s\n", text);
    status = bento_ui_screen_default(ui);
    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_panel(ui, &box, &look, &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "Model not on this board", &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, " ", &body, &body_id);
    }
    if (status == BENTO_OK) {
        /* .text() carries the long text: a label's CREATE holds less */
        (void)bento_ui_set_text(ui, body_id, text,
                                (uint32_t)strlen(text));
        drain_events(ui);
    }
    return status;
}

/* The disc and the four lines of one card. */
static bento_status_t add_side_lines(bento_ui_t *ui, side_t *side,
                                     int32_t x)
{
    const text_place_t verdict = {x + VERDICT_DX, VERDICT_Y, COLOR_DIM,
                                  BIG_PX};
    const text_place_t now_line = {x, NOW_Y, COLOR_TEXT, NOW_PX};
    const text_place_t count_line = {x, COUNT_Y, COLOR_TEXT, TEXT_PX};
    const text_place_t delay_line = {x, DELAY_Y, COLOR_DIM, TEXT_PX};
    bento_status_t status =
        add_disc(ui, x, DISC_Y, DISC_SIZE, &side->disc.id);

    if (status == BENTO_OK) {
        status = add_label(ui, "paused", &verdict, &side->verdict.id);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, " ", &now_line, &side->now_line.id);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, " ", &count_line, &side->count_line.id);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, " ", &delay_line, &side->delay_line.id);
    }
    return status;
}

/* Card k: its frame with the model's name, then its lines. */
static bento_status_t add_side(siren_t *app, uint32_t side_index)
{
    const int32_t x = SIDE_X + (SIDE_STEP_X * (int32_t)side_index);
    const box_t box = {x - SIDE_CARD_DX, SIDE_CARD_Y, SIDE_CARD_W,
                       SIDE_CARD_H};
    char title[TEXT_BYTES];
    bento_status_t status;

    if (side_index == 0U) {
        (void)snprintf(title, sizeof(title), "A: %s (Store)", MODEL);
    } else {
        (void)snprintf(title, sizeof(title), "B: %s (built-in)",
                       NAMES[1]);
    }
    status = add_card(app->ui, &box, title);
    if (status == BENTO_OK) {
        status = add_side_lines(app->ui, &app->sides[side_index], x);
    }
    return status;
}

/* The title, the heartbeat and the line under the title. */
static bento_status_t add_header(siren_t *app)
{
    static const text_place_t title = {TITLE_X, TITLE_Y, COLOR_TEXT,
                                       TITLE_PX};
    static const text_place_t note = {TITLE_X, NOTE_Y, COLOR_DIM,
                                      TEXT_PX};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(app->ui, "Siren: Store vs Built-in", &title, &unused);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = BEAT_X;
        args.y = BEAT_Y;
        args.w = BEAT_SIZE;
        args.h = BEAT_SIZE;
        args.color = COLOR_OK;
        args.font_px = 0; /* off until a result arrives */
        status = bento_ui_led_create(app->ui, &args, &app->heartbeat);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui,
                           "Same siren, two models, one at a time. "
                           "Each counts at its own Confirm at.",
                           &note, &unused);
    }
    return status;
}

/* build(): the whole screen, made once. */
static bento_status_t build_screen(siren_t *app)
{
    static const box_t swap_box = {SWAP_X, BUTTON_Y, BUTTON_W,
                                   BUTTON_H};
    static const box_t acknowledge_box = {ACKNOWLEDGE_X, BUTTON_Y,
                                          BUTTON_W, BUTTON_H};
    static const text_place_t status_place = {STATUS_X, STATUS_Y,
                                              COLOR_WARN, TEXT_PX};
    bento_status_t status = bento_ui_screen_default(app->ui);
    uint32_t side_index;

    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_header(app);
    }
    for (side_index = 0U;
         (side_index < (uint32_t)SIDES) && (status == BENTO_OK);
         side_index++) {
        status = add_side(app, side_index);
    }
    if (status == BENTO_OK) {
        status = add_button(app->ui, "Swap: run B", &swap_box, SWAP_PX,
                            &app->swap.id);
    }
    if (status == BENTO_OK) {
        status = add_button(app->ui, "ACK", &acknowledge_box, TITLE_PX,
                            &app->acknowledge.id);
    }
    if (status == BENTO_OK) {
        status =
            add_label(app->ui, " ", &status_place, &app->status.id);
    }
    if (status == BENTO_OK) {
        drain_events(app->ui);
    }
    return status;
}

static void vote_reset(vote_t *vote)
{
    vote->count = 0U;
}

/* Keeps the last N scores. */
static void vote_add(vote_t *vote, int32_t score)
{
    uint32_t i;

    if (vote->count == (uint32_t)N) {
        for (i = 1U; i < (uint32_t)N; i++) {
            vote->scores[i - 1U] = vote->scores[i];
        }
        vote->count--;
    }
    vote->scores[vote->count] = score;
    vote->count++;
}

static int32_t vote_max(const vote_t *vote)
{
    int32_t best = 0;
    uint32_t i;

    for (i = 0U; i < vote->count; i++) {
        if ((i == 0U) || (vote->scores[i] > best)) {
            best = vote->scores[i];
        }
    }
    return best;
}

/* K of the kept scores at or above the level = confirmed. */
static bool vote_hits(const vote_t *vote, int32_t level)
{
    int32_t hits = 0;
    uint32_t i;

    for (i = 0U; i < vote->count; i++) {
        if (vote->scores[i] >= level) {
            hits++;
        }
    }
    return hits >= K;
}

/* A full window, every score below the level. */
static bool vote_clear(const vote_t *vote, int32_t level)
{
    return (vote->count == (uint32_t)N) && (vote_max(vote) < level);
}

/* A confirm: true once per new event, then no other within
 * REFRACT_MS of the episode's last confirm. */
static bool episode_confirm(episode_t *episode, uint32_t now_ms)
{
    const bool active = (episode->state == EPISODE_UNACK) ||
                        (episode->state == EPISODE_ACKED);
    const bool rested =
        !episode->has_time ||
        (bento_time_ticks_diff(now_ms, episode->time_ms) >= REFRACT_MS);
    bool fired = false;

    if (!active && rested) {
        episode->state = episode->latch ? EPISODE_UNACK : EPISODE_SEEN;
        episode->events++;
        episode->has_time = true;
        episode->time_ms = now_ms;
        fired = true;
    } else if (active) {
        episode->time_ms = now_ms;
    } else {
        /* too soon after the last event */
    }
    return fired;
}

/* Episode.step(): NORMAL > HEARD > UNACK > ACKED or ENDED. True once
 * per new event. */
static bool episode_step(episode_t *episode, bool heard, bool confirm,
                         bool clear, uint32_t now_ms)
{
    episode_state_t state = episode->state;
    bool fired = false;

    if (confirm) {
        fired = episode_confirm(episode, now_ms);
    }
    if (!fired) {
        if (clear && (state == EPISODE_UNACK)) {
            state = EPISODE_ENDED;
            clock_text(episode->end, sizeof(episode->end));
        } else if (clear && (state != EPISODE_ENDED)) {
            state = EPISODE_NORMAL;
        } else if (!clear && heard && (state == EPISODE_NORMAL)) {
            state = EPISODE_HEARD;
        } else {
            /* the state stays */
        }
        episode->state = state;
    }
    return fired;
}

static void episode_acknowledge(episode_t *episode)
{
    if (episode->state == EPISODE_UNACK) {
        episode->state = EPISODE_ACKED;
    } else if (episode->state == EPISODE_ENDED) {
        episode->state = EPISODE_NORMAL;
    } else {
        /* nothing to acknowledge */
    }
}

/* read_th(): per class Hearing at, Confirm at, clear below, or the
 * fallback for every class. */
static void read_thresholds(side_t *side)
{
    uint32_t i;

    bento_edgeai_thresholds_init(&side->thresholds);
    if (bento_edgeai_thresholds(side->model.index, &side->thresholds) !=
        BENTO_OK) {
        for (i = 0U; i < (uint32_t)MAX_CLASSES; i++) {
            side->thresholds.enter[i] =
                DEF_THRESHOLDS[BENTO_EDGEAI_TH_ENTER];
            side->thresholds.confirm[i] =
                DEF_THRESHOLDS[BENTO_EDGEAI_TH_CONFIRM];
            side->thresholds.exit[i] =
                DEF_THRESHOLDS[BENTO_EDGEAI_TH_EXIT];
        }
    }
}

/* Side.rest(): a card that stops starts fresh. */
static void rest_side(side_t *side)
{
    vote_reset(&side->vote);
    side->has_onset = false;
    side->episode.state = EPISODE_NORMAL;
}

/* Side(m): a card with its model, or an empty one. */
static void init_side(side_t *side, bool present)
{
    side->present = present;
    side->episode.latch = true;
    side->episode.state = EPISODE_NORMAL;
    side->top = 0;
    side->delay_ms = -1;
    if (present) {
        read_thresholds(side);
    }
    rest_side(side);
}

static const char *help_text(const siren_t *app)
{
    static const char help_buttons[] =
        "Listening - SW6 = ACK, SW5 = pause";
    static const char help_plain[] = "Listening";

    return app->has_buttons ? help_buttons : help_plain;
}

/* edge_ai.select(): loads and starts the model, up to 15 s. *why is
 * the OSError's text when it fails. */
static bento_status_t select_model(uint32_t index, const char **why)
{
    bento_status_t status = bento_edgeai_select_start(index);

    *why = NULL;
    if (status != BENTO_OK) {
        *why = "edge_ai: select send failed";
        status = BENTO_OK;
    } else {
        status = finish_op();
        if ((status != BENTO_OK) && (status != BENTO_E_CANCELLED)) {
            *why = "edge_ai: select not confirmed";
            status = BENTO_OK;
        }
    }
    return status;
}

/* start(): on resume (row_count not 0), Stop if a model was added or
 * removed meanwhile. */
static bento_status_t start_model(siren_t *app, uint32_t index,
                                  uint32_t row_count)
{
    char text[TEXT_BYTES];
    int32_t count = 0;
    const char *why = NULL;
    bento_status_t status = BENTO_OK;

    (void)snprintf(text, sizeof(text), "Loading %s ...",
                   app->model_name);
    put_text(app->ui, &app->status, text);
    put_color(app->ui, &app->status, COLOR_WARN);
    drain_events(app->ui);
    app->failing = false;
    if (row_count != 0U) {
        const bento_status_t link = bento_edgeai_count(&count);

        if (link != BENTO_OK) {
            why = link_error_text(link);
        } else if (count != (int32_t)row_count) {
            raise_stop(app, "Model list changed - run again");
        } else {
            /* the same list: go on */
        }
    }
    if ((why == NULL) && !app->stopped) {
        status = select_model(index, &why);
    }
    if (why != NULL) {
        (void)printf("select: %s\n", why);
        (void)snprintf(text, sizeof(text), "Cannot start %s",
                       app->model_name);
        raise_stop(app, text);
    } else if ((status == BENTO_OK) && !app->stopped) {
        put_text(app->ui, &app->status, help_text(app));
        put_color(app->ui, &app->status, COLOR_TEXT);
    } else {
        /* stopped */
    }
    return status;
}

/* read_result(): only a NEW result of this model; Stop after 3 s of
 * errors. */
static bool read_result(siren_t *app, uint32_t index,
                        bento_edgeai_result_t *result)
{
    bool fresh = false;
    uint32_t now_ms;

    bento_edgeai_result_init(result);
    if (bento_edgeai_result(result) == BENTO_OK) {
        app->failing = false;
        fresh = result->has_verdict &&
                (!app->has_sequence ||
                 (result->seq != app->last_sequence)) &&
                ((uint32_t)result->index == index);
        if (fresh) {
            app->has_sequence = true;
            app->last_sequence = result->seq;
        }
    } else {
        now_ms = bento_time_ticks_ms();
        if (!app->failing) {
            app->failing = true;
            app->fail_since_ms = now_ms;
        }
        if (bento_time_ticks_diff(now_ms, app->fail_since_ms) >=
            FAIL_STOP_MS) {
            raise_stop(app, "AI core not answering - run again");
        }
    }
    return fresh;
}

/* event_of(): the strongest EVENT class (class 0 = nothing) and its
 * thresholds. */
static void event_of(const bento_edgeai_result_t *result,
                     const bento_edgeai_thresholds_t *thresholds,
                     event_t *event)
{
    uint32_t strongest = 0U;
    uint32_t klass;

    for (klass = 1U; (klass < result->class_count) &&
                     (klass < (uint32_t)MAX_CLASSES);
         klass++) {
        if ((result->score_pct[klass] > result->score_pct[strongest]) ||
            (strongest == 0U)) {
            strongest = klass;
        }
    }
    event->klass = strongest;
    event->percent = (int32_t)result->score_pct[strongest];
    event->hearing = (int32_t)thresholds->enter[strongest];
    event->confirm = (int32_t)thresholds->confirm[strongest];
    event->clear = (int32_t)thresholds->exit[strongest];
}

/* One new result for the listening card: vote, onset, episode. */
static void score_result(siren_t *app,
                         const bento_edgeai_result_t *result,
                         uint32_t now_ms)
{
    side_t *side = &app->sides[app->active];
    char clock[CLOCK_BYTES];
    event_t event;

    event_of(result, &side->thresholds, &event);
    vote_add(&side->vote, event.percent);
    if (event.percent > app->peak) {
        app->peak = event.percent;
    }
    if (event.percent > side->top) {
        side->top = event.percent;
    }
    if (vote_clear(&side->vote, event.clear)) {
        side->has_onset = false;
    } else if ((event.percent >= event.confirm) && !side->has_onset) {
        side->has_onset = true; /* the delay is the price of the rule */
        side->onset_ms = now_ms;
    } else {
        /* the onset stays */
    }
    if (episode_step(&side->episode, event.percent >= event.hearing,
                     vote_hits(&side->vote, event.confirm),
                     vote_clear(&side->vote, event.clear), now_ms)) {
        side->delay_ms =
            side->has_onset
                ? bento_time_ticks_diff(now_ms, side->onset_ms)
                : 0;
        clock_text(clock, sizeof(clock));
        (void)printf("%s %c %s %" PRId32 "%% #%" PRId32 "\n", clock,
                     SIDE_LETTERS[app->active],
                     side->model.labels[event.klass],
                     vote_max(&side->vote), side->episode.events);
    }
}

/* The disc's colour by state: UNACK and ENDED flash at 1 Hz. */
static uint32_t disc_color(episode_state_t state, bool blink)
{
    const uint32_t flash = blink ? COLOR_BAD : COLOR_ALARM_BG;
    const uint32_t ended = blink ? COLOR_DIM : COLOR_ALARM_BG;
    const uint32_t colors[DRAWN_STATES] = {COLOR_DIM, COLOR_WARN, flash,
                                           COLOR_BAD, ended};

    return colors[state];
}

/* The verdict line of one card. */
static void draw_verdict(bento_ui_t *ui, side_t *side, bool listening)
{
    static const char *const words[DRAWN_STATES] = {
        "Quiet", "! hearing", "!! SIREN", "ACKED", "ENDED "};
    static const uint32_t colors[DRAWN_STATES] = {
        COLOR_DIM, COLOR_WARN, COLOR_BAD, COLOR_BAD, COLOR_TEXT};
    const episode_state_t state = side->episode.state;
    char text[TEXT_BYTES];

    if (!listening) {
        (void)snprintf(text, sizeof(text), "%s", "paused");
    } else if (state == EPISODE_ENDED) {
        (void)snprintf(text, sizeof(text), "%s%s", words[state],
                       side->episode.end);
    } else {
        (void)snprintf(text, sizeof(text), "%s", words[state]);
    }
    put_text(ui, &side->verdict, text);
    put_color(ui, &side->verdict, colors[state]);
}

/* The three tally lines of one card. */
static void draw_tallies(bento_ui_t *ui, side_t *side,
                         int32_t now_percent)
{
    char text[TEXT_BYTES];
    char seconds[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;

    (void)snprintf(text, sizeof(text),
                   "now %" PRId32 "%% | max %" PRId32 "%%", now_percent,
                   side->top);
    put_text(ui, &side->now_line, text);
    (void)snprintf(text, sizeof(text),
                   "sirens %" PRId32 " | Confirm at %" PRId32 "%%",
                   side->episode.events,
                   (int32_t)side->thresholds.confirm[1]);
    put_text(ui, &side->count_line, text);
    if (side->delay_ms < 0) {
        (void)snprintf(text, sizeof(text), "%s", "delay -");
    } else {
        const bento_real_t delay_s =
            (bento_real_t)side->delay_ms / (bento_real_t)MS_PER_S;

        if (bento_fmt_real_precision(seconds, sizeof(seconds), delay_s,
                                     1, &length) != BENTO_OK) {
            seconds[0] = '\0';
        }
        (void)snprintf(text, sizeof(text), "delay %s s", seconds);
    }
    put_text(ui, &side->delay_line, text);
}

/* draw(): listening = the listening card, -1 when paused. */
static void draw(siren_t *app)
{
    const int32_t listening = app->paused ? -1 : (int32_t)app->active;
    const int32_t peak = (app->peak > 0) ? app->peak : 0;
    const episode_state_t shown =
        app->sides[app->paused ? 0U : app->active].episode.state;
    uint32_t side_index;

    (void)bento_ui_set_value(app->ui, app->heartbeat,
                             (app->blink && (app->peak >= 0)) ? 1 : 0);
    for (side_index = 0U; side_index < (uint32_t)SIDES; side_index++) {
        side_t *side = &app->sides[side_index];
        const bool is_listening = listening == (int32_t)side_index;

        if (side->present) {
            put_color(app->ui, &side->disc,
                      disc_color(side->episode.state, app->blink));
            draw_verdict(app->ui, side, is_listening);
            draw_tallies(app->ui, side, is_listening ? peak : 0);
        }
    }
    put_color(app->ui, &app->acknowledge,
              ((shown == EPISODE_UNACK) || (shown == EPISODE_ENDED))
                  ? COLOR_BAD
                  : COLOR_BUTTON);
}

/* The board buttons and the taps of this round. The handler reads
 * only the widget of an event, so any kind of event counts. */
static bento_status_t read_inputs(siren_t *app, bool *acknowledge,
                                  bool *swap)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = sample_button(app->has_buttons, &app->sw5);

    if (status == BENTO_OK) {
        status = sample_button(app->has_buttons, &app->sw6);
    }
    *acknowledge = button_clicked(&app->sw6);
    *swap = false;
    if (bento_ui_poll(app->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U; /* a failed poll answers no events */
    }
    for (i = 0U; i < count; i++) {
        *acknowledge =
            *acknowledge || (events[i].id == app->acknowledge.id);
        *swap = *swap || (events[i].id == app->swap.id);
    }
    return status;
}

/* Taps and presses made during the load are dropped: no swap back,
 * no ACK of the new card. */
static bento_status_t drop_inputs(siren_t *app)
{
    bento_status_t status;

    drain_events(app->ui);
    status = sample_button(app->has_buttons, &app->sw5);
    if (status == BENTO_OK) {
        status = sample_button(app->has_buttons, &app->sw6);
    }
    (void)button_clicked(&app->sw5);
    (void)button_clicked(&app->sw6);
    return status;
}

/* Swap: the other card's model listens now. */
static bento_status_t swap_sides(siren_t *app)
{
    char clock[CLOCK_BYTES];
    char text[TEXT_BYTES];
    bento_status_t status;

    rest_side(&app->sides[app->active]);
    status = stop_ai();
    if (status == BENTO_OK) {
        app->active = 1U - app->active;
        app->model_name = NAMES[app->active];
        clock_text(clock, sizeof(clock));
        (void)printf("%s swap: run %s\n", clock, app->model_name);
        status = start_model(app, app->sides[app->active].model.index,
                             app->row_count);
    }
    if ((status == BENTO_OK) && !app->stopped) {
        (void)snprintf(text, sizeof(text), "Swap: run %c",
                       SIDE_LETTERS[1U - app->active]);
        put_text(app->ui, &app->swap, text);
        status = drop_inputs(app);
    }
    return status;
}

/* SW5: pause or resume the listening card. */
static bento_status_t toggle_pause(siren_t *app)
{
    side_t *side = &app->sides[app->active];
    bento_status_t status = BENTO_OK;

    app->paused = !app->paused;
    rest_side(side);
    if (app->paused) {
        status = stop_ai();
        if (status == BENTO_OK) {
            put_text(app->ui, &app->status, "Paused - SW5 resumes");
            put_color(app->ui, &app->status, COLOR_WARN);
        }
    } else {
        status = start_model(app, side->model.index, app->row_count);
    }
    return status;
}

/* A result every POLL_MS and a redraw every DRAW_MS. */
static void poll_and_draw(siren_t *app)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    bento_edgeai_result_t result;

    if (!app->paused &&
        (bento_time_ticks_diff(now_ms, app->polled_ms) >= POLL_MS)) {
        app->polled_ms = now_ms;
        if (read_result(app, app->sides[app->active].model.index,
                        &result)) {
            score_result(app, &result, now_ms);
        }
    }
    if (!app->stopped &&
        (bento_time_ticks_diff(now_ms, app->drawn_ms) >= DRAW_MS)) {
        app->drawn_ms = now_ms;
        app->blink = !app->blink;
        draw(app);
        app->peak = -1;
    }
}

/* One round of the main loop. */
static bento_status_t siren_round(siren_t *app)
{
    bool acknowledge = false;
    bool swap = false;
    bento_status_t status = read_inputs(app, &acknowledge, &swap);

    if ((status == BENTO_OK) && acknowledge) {
        episode_acknowledge(&app->sides[app->active].episode);
    }
    if ((status == BENTO_OK) && swap &&
        app->sides[1U - app->active].present && !app->paused) {
        status = swap_sides(app);
    }
    if ((status == BENTO_OK) && !app->stopped &&
        button_clicked(&app->sw5)) {
        status = toggle_pause(app);
    }
    if ((status == BENTO_OK) && !app->stopped) {
        poll_and_draw(app);
    }
    if ((status == BENTO_OK) && !app->stopped) {
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* run(): card B looks up the built-in model by the same exact-name
 * rule, then card A's model starts and the loop runs. */
static bento_status_t run_siren(siren_t *app)
{
    side_t *side_b = &app->sides[1];
    uint32_t rows_b = 0U;
    bento_status_t status;

    init_side(&app->sides[0], true);
    init_side(side_b,
              find_model(NAMES[1], true, &side_b->model, &rows_b));
    if (!side_b->present) {
        put_text(app->ui, &side_b->verdict, "not on this board");
        put_text(app->ui, &app->swap, "B not on this board");
    }
    app->peak = -1;
    status = start_model(app, app->sides[0].model.index, 0U);
    app->drawn_ms = bento_time_ticks_ms();
    app->polled_ms = app->drawn_ms;
    while ((status == BENTO_OK) && !app->stopped) {
        status = siren_round(app);
    }
    return status;
}

/* run() inside its try: a Stop is shown and printed, and the AI core
 * is always given back. */
static bento_status_t run_then_stop(siren_t *app)
{
    bento_status_t status = run_siren(app);
    bento_status_t stop_status;

    if (app->stopped) {
        put_text(app->ui, &app->status, app->stop_text);
        put_color(app->ui, &app->status, COLOR_BAD);
        (void)printf("%s\n", app->stop_text);
    }
    if (status == BENTO_E_CANCELLED) { /* the finally still runs */
        (void)bento_runtime_acknowledge_stop();
    }
    stop_status = stop_ai();
    return (status == BENTO_E_CANCELLED) ? stop_status : status;
}

/* The BENTO Emulator has no Edge AI from C yet
 * (bento_edgeai_supported() answers BENTO_E_UNSUPPORTED there): the
 * original's path for a missing model, then the note under its card. */
static bento_status_t show_emulator_note(bento_ui_t *ui)
{
    static const char note[] = "Edge AI จาก C ยังไม่มีใน BENTO Emulator "
                               "ให้รันบน Dev Kit หรือ Eva Kit";
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = show_missing(ui);

    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = note;
        args.text_len = (uint32_t)strlen(note);
        args.x = MISSING_X;
        args.y = EMULATOR_NOTE_Y;
        args.w = MISSING_W; /* a width lets the label wrap */
        args.color = COLOR_WARN;
        args.font_px = MISSING_PX;
        status = bento_ui_label_create(ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        (void)printf(
            "%s / Edge AI from C is not in the BENTO Emulator yet: "
            "run it on a Dev Kit or Eva Kit.\n",
            note);
    }
    return status;
}

int main(void)
{
    siren_t *app = &g_siren;
    bool supported = false;
    bento_status_t status;

    (void)memset(app, 0, sizeof(*app));
    app->ui = bento_runtime_get_ui();
    app->has_buttons = bento_buttons_count() > 0U;
    app->sw5.index = 1U;
    app->sw6.index = 0U;
    app->model_name = MODEL;
    if (bento_edgeai_supported(&supported) == BENTO_E_UNSUPPORTED) {
        status = show_emulator_note(app->ui);
    } else if (!find_model(MODEL, BUILTIN, &app->sides[0].model,
                           &app->row_count)) {
        status = show_missing(app->ui);
    } else {
        status = build_screen(app);
        if (status == BENTO_OK) {
            status = run_then_stop(app);
        }
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
