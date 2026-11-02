/*
 * home_sounds.c -- กระดานสามเสียง: เด็กร้อง ไอ และก๊อกน้ำเปิดทิ้ง
 *
 * ต้องติดตั้ง HomeSounds จาก Edge AI Store ก่อน ไทล์สามช่องบอกเสียง
 * เด็กร้อง เสียงไอ และเสียงน้ำไหลจากก๊อก แต่ละช่องนับครั้งที่ได้ยิน
 * และเวลาครั้งล่าสุด แตะไทล์เพื่อปิดหรือเปิดเสียงนั้น ก๊อกน้ำที่เปิด
 * นานเกิน 30 วินาทีเป็นสัญญาณเตือนที่ค้างจนกว่าจะกด ACK
 * ปุ่ม SW6 (บน) หรือแตะ ACK = รับทราบ ปุ่ม SW5 (ล่าง) = หยุดชั่วคราว
 *
 * Needs HomeSounds from the Edge AI Store. Three tiles show a baby
 * cry, a cough and a running water tap, each with its count and
 * the time it was last heard. Tap a tile to mute or unmute it. A
 * tap that runs longer than 30 s is an alarm that stays until ACK.
 * SW6 (upper) or a tap on ACK acknowledges, SW5 (lower) pauses.
 *
 * Twin of home_sounds.py
 * (embedded-systems-for-aiot-developer/examples/edge_ai_models/home_sounds.py).
 * Style: CLEAN_C.md. Closest library twin: devkit-21-ai-model-lab
 * (style only, the code is new).
 * Differs from the original:
 *   - In the BENTO Emulator, Edge AI from C is not available yet:
 *     the program says so and then shows the "Model not on this
 *     board" card, as the original does when the model is missing.
 *   - gc.collect() and the free-heap print are left out: C has no
 *     garbage collector.
 *   - m["builtin"] is "row index below diag.compiled_models"; when
 *     the board cannot report that it is None (the Emulator), so the
 *     Store name is accepted only when the board says False.
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
 *   - Without a water_tap class, eps[tap] is eps[-1] in the original:
 *     an info episode that ACK never changes. Here ACK does nothing
 *     then, which is the same.
 *   - "%.1f" is written by bento_fmt in the board's float, and
 *     r["latency_ms"] is latency_us / 1000 as the binding makes it.
 *   - str.upper() changes the ASCII letters only, as MicroPython's
 *     does.
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

enum { /* settings */
    VOTES_NEEDED =
        3, /* an event = 3 of the last 5 results at Confirm at */
    VOTES_KEPT = 5,
    REFRACT_MS = 1500, /* no second event sooner than this */
    HOLD_MS = 2000,    /* an event stays on screen this long */
    DEF_HEARING =
        40, /* fallback thresholds: Hearing at, Confirm at, clear below */
    DEF_CONFIRM = 65,
    DEF_CLEAR = 30,
    POLL_MS = 100,
    DRAW_MS = 500,
    LOOP_MS = 20,
    LOAD_MS = 150,   /* let the screen settle after ui.screen() */
    OP_POLL_MS = 20, /* how often a started select or stop is asked */
    FAIL_STOP_MS =
        3000,         /* result() failing this long ends the program */
    TAP_S = 30,       /* water_tap running this long = alarm */
    HEALTH_MS = 2000, /* the health line is renewed this often */
    MS_PER_S = 1000,
    CLASSES_SHOWN = 4, /* class 0 and the three tiles */
    TILES = 3,
    NO_TAP = -1,
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
    CARD_RADIUS = 12,
    CARD_TITLE_DX = 14,
    CARD_TITLE_DY = 8,
    CARD_X = 12,
    CARD_Y = 64,
    CARD_W = 776,
    CARD_H = 272,
    TILE_X = 28,
    TILE_STEP_X = 252,
    TILE_Y = 96,
    TILE_W = 236,
    TILE_H = 130,
    TILE_PX = 20,
    DISC_Y = 236,
    DISC_SIZE = 28,
    STATE_DX = 36,
    STATE_Y = 240,
    LOG_X = 28,
    LOG_Y = 280,
    ACKNOWLEDGE_X = 508,
    ACKNOWLEDGE_Y = 270,
    ACKNOWLEDGE_W = 264,
    ACKNOWLEDGE_H = 56,
    STATUS_X = 12,
    STATUS_Y = 346,
    HEALTH_Y = 370,
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

static const char MODEL_NAME[] = "HomeSounds";

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

/* The last VOTES_KEPT scores. */
typedef struct {
    int32_t scores[VOTES_KEPT];
    uint32_t count;
} vote_t;

/* An alarm (latch, needs ACK) or an info event (shown HOLD_MS). */
typedef struct {
    bool latch;
    episode_state_t state;
    int32_t events;
    bool has_time;
    uint32_t time_ms;
    char end[CLOCK_BYTES];
} episode_t;

/* One tile on the screen: its button, its disc and its state line. */
typedef struct {
    tracked_t button;
    tracked_t disc;
    tracked_t state;
} tile_t;

/* Everything the program changes while it runs. */
typedef struct {
    bento_ui_t *ui;
    model_t model;
    uint32_t row_count;
    const char *model_name;
    bento_edgeai_thresholds_t thresholds;
    uint32_t class_count;
    int32_t tap;
    vote_t votes[CLASSES_SHOWN];
    episode_t episodes[CLASSES_SHOWN];
    bool muted[CLASSES_SHOWN];
    char last[CLASSES_SHOWN][CLOCK_BYTES];
    bool tap_running;
    uint32_t tap_on_ms;
    tile_t tiles[TILES];
    tracked_t log;
    tracked_t acknowledge;
    tracked_t status;
    tracked_t health;
    bento_ui_id_t heartbeat;
    board_button_t sw5;
    board_button_t sw6;
    bool has_buttons;
    bool paused;
    bool blink;
    int32_t results;
    bento_real_t latency_ms;
    uint32_t drawn_ms;
    uint32_t polled_ms;
    uint32_t health_ms;
    bool has_sequence;
    uint32_t last_sequence;
    bool failing;
    uint32_t fail_since_ms;
    bool stopped;
    char stop_text[TEXT_BYTES];
} sounds_t;

/* The program's state: too big for the 8 KB program stack, so at
 * file scope, zeroed by the loader on every load and set again at
 * the start of main() (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern sounds_t g_sounds;
sounds_t g_sounds;

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
static void raise_stop(sounds_t *app, const char *text)
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
                   MODEL_NAME);
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

static void vote_reset(vote_t *vote)
{
    vote->count = 0U;
}

/* Keeps the last VOTES_KEPT scores. */
static void vote_add(vote_t *vote, int32_t score)
{
    uint32_t i;

    if (vote->count == (uint32_t)VOTES_KEPT) {
        for (i = 1U; i < (uint32_t)VOTES_KEPT; i++) {
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

/* VOTES_NEEDED of the kept scores at or above the level = confirmed. */
static bool vote_hits(const vote_t *vote, int32_t level)
{
    int32_t hits = 0;
    uint32_t i;

    for (i = 0U; i < vote->count; i++) {
        if (vote->scores[i] >= level) {
            hits++;
        }
    }
    return hits >= VOTES_NEEDED;
}

/* A full window, every score below the level. */
static bool vote_clear(const vote_t *vote, int32_t level)
{
    return (vote->count == (uint32_t)VOTES_KEPT) &&
           (vote_max(vote) < level);
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

static const char *help_text(const sounds_t *app)
{
    static const char help_buttons[] =
        "Listening - tap a tile = mute, SW6 = ACK, SW5 = pause";
    static const char help_plain[] = "Listening - tap a tile = mute";

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
static bento_status_t start_model(sounds_t *app, uint32_t index,
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
static bool read_result(sounds_t *app, uint32_t index,
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

/* The disc's colour by state: UNACK and ENDED flash at 1 Hz. */
static uint32_t disc_color(episode_state_t state, bool blink)
{
    const uint32_t flash = blink ? COLOR_BAD : COLOR_ALARM_BG;
    const uint32_t ended = blink ? COLOR_DIM : COLOR_ALARM_BG;
    const uint32_t colors[DRAWN_STATES] = {COLOR_DIM, COLOR_WARN, flash,
                                           COLOR_BAD, ended};

    return colors[state];
}

/* Episode.step() after a confirm that did not fire: an info event
 * stays SEEN for HOLD_MS, then clear, heard or nothing moves it. */
static void episode_settle(episode_t *episode, bool heard, bool clear,
                           uint32_t now_ms)
{
    episode_state_t state = episode->state;
    const bool held =
        (state == EPISODE_SEEN) &&
        (bento_time_ticks_diff(now_ms, episode->time_ms) < HOLD_MS);

    if (!held) {
        if (state == EPISODE_SEEN) {
            state = EPISODE_NORMAL;
        }
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
}

/* Episode.step(): alarm NORMAL > HEARD > UNACK > ACKED or ENDED; info
 * NORMAL > HEARD > SEEN. True once per new event. */
static bool episode_step(episode_t *episode, bool heard, bool confirm,
                         bool clear, uint32_t now_ms)
{
    bool fired = false;

    if (confirm) {
        fired = episode_confirm(episode, now_ms);
    }
    if (!fired) {
        episode_settle(episode, heard, clear, now_ms);
    }
    return fired;
}

/* read_th(): per class Hearing at, Confirm at, clear below, or the
 * fallback for every class. */
static void read_thresholds(sounds_t *app)
{
    uint32_t i;

    bento_edgeai_thresholds_init(&app->thresholds);
    if (bento_edgeai_thresholds(app->model.index, &app->thresholds) !=
        BENTO_OK) {
        for (i = 0U; i < (uint32_t)MAX_CLASSES; i++) {
            app->thresholds.enter[i] = DEF_HEARING;
            app->thresholds.confirm[i] = DEF_CONFIRM;
            app->thresholds.exit[i] = DEF_CLEAR;
        }
    }
}

/* The title, the heartbeat, the line under the title and the card. */
static bento_status_t add_header(sounds_t *app)
{
    static const text_place_t title = {TITLE_X, TITLE_Y, COLOR_TEXT,
                                       TITLE_PX};
    static const text_place_t note = {TITLE_X, NOTE_Y, COLOR_DIM,
                                      TEXT_PX};
    static const box_t card = {CARD_X, CARD_Y, CARD_W, CARD_H};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(app->ui, "Three-Sound Board", &title, &unused);

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
                           "Detects: baby cry, cough, running tap. "
                           "Other sounds read as unlabeled.",
                           &note, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(app->ui, &card, "Sounds");
    }
    return status;
}

/* The three tile buttons, then their discs, then their state lines,
 * in the order the original makes them. */
static bento_status_t add_tiles(sounds_t *app)
{
    bento_status_t status = BENTO_OK;
    int32_t tile;

    for (tile = 0; (tile < TILES) && (status == BENTO_OK); tile++) {
        const box_t box = {TILE_X + (TILE_STEP_X * tile), TILE_Y,
                           TILE_W, TILE_H};

        status = add_button(app->ui, " ", &box, TILE_PX,
                            &app->tiles[tile].button.id);
    }
    for (tile = 0; (tile < TILES) && (status == BENTO_OK); tile++) {
        status = add_disc(app->ui, TILE_X + (TILE_STEP_X * tile),
                          DISC_Y, DISC_SIZE, &app->tiles[tile].disc.id);
    }
    for (tile = 0; (tile < TILES) && (status == BENTO_OK); tile++) {
        const text_place_t place = {TILE_X + (TILE_STEP_X * tile) +
                                        STATE_DX,
                                    STATE_Y, COLOR_DIM, TEXT_PX};

        status = add_label(app->ui, "Quiet", &place,
                           &app->tiles[tile].state.id);
    }
    return status;
}

/* build(): the whole screen, made once. */
static bento_status_t build_screen(sounds_t *app)
{
    static const text_place_t log_place = {LOG_X, LOG_Y, COLOR_TEXT,
                                           TEXT_PX};
    static const box_t acknowledge_box = {ACKNOWLEDGE_X, ACKNOWLEDGE_Y,
                                          ACKNOWLEDGE_W, ACKNOWLEDGE_H};
    static const text_place_t status_place = {STATUS_X, STATUS_Y,
                                              COLOR_WARN, TEXT_PX};
    static const text_place_t health_place = {STATUS_X, HEALTH_Y,
                                              COLOR_DIM, TEXT_PX};
    bento_status_t status = bento_ui_screen_default(app->ui);

    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_header(app);
    }
    if (status == BENTO_OK) {
        status = add_tiles(app);
    }
    if (status == BENTO_OK) {
        status = add_label(app->ui, " ", &log_place, &app->log.id);
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
        status =
            add_label(app->ui, " ", &health_place, &app->health.id);
    }
    if (status == BENTO_OK) {
        drain_events(app->ui);
    }
    return status;
}

/* One character in upper case: an ASCII letter changes, any other
 * byte stays. */
static char upper_char(char letter)
{
    static const char lower[] = "abcdefghijklmnopqrstuvwxyz";
    static const char upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char result = letter;
    size_t i;

    for (i = 0U; lower[i] != '\0'; i++) {
        if (lower[i] == letter) {
            result = upper[i];
        }
    }
    return result;
}

/* str.upper(): ASCII letters only, as MicroPython does it. */
static void upper_text(const char *text, char *out, size_t cap)
{
    size_t i = 0U;

    while ((text[i] != '\0') && ((i + 1U) < cap)) {
        out[i] = upper_char(text[i]);
        i++;
    }
    out[i] = '\0';
}

/* The tap tile's alarm line: grey > amber > red until ACK. */
static void tap_line(const episode_t *episode, int32_t on_s, char *text,
                     size_t cap)
{
    switch (episode->state) {
    case EPISODE_HEARD:
        if (on_s != 0) {
            (void)snprintf(text, cap, "! running %" PRId32 " s", on_s);
        } else {
            (void)snprintf(text, cap, "%s", "! hearing");
        }
        break;
    case EPISODE_UNACK:
        (void)snprintf(text, cap, "!! RUNNING %" PRId32 " s - ACK",
                       on_s);
        break;
    case EPISODE_ACKED:
        (void)snprintf(text, cap, "ACKED - running %" PRId32 " s",
                       on_s);
        break;
    case EPISODE_ENDED:
        (void)snprintf(text, cap, "ENDED %s - ACK", episode->end);
        break;
    default:
        (void)snprintf(text, cap, "%s", "Quiet");
        break;
    }
}

/* An info tile's line: white while recognised. */
static uint32_t info_line(const episode_t *episode, const char *label,
                          char *text, size_t cap)
{
    uint32_t color = COLOR_DIM;

    if (episode->state == EPISODE_SEEN) {
        upper_text(label, text, cap);
        color = COLOR_TEXT;
    } else if (episode->state == EPISODE_HEARD) {
        (void)snprintf(text, cap, "maybe %s", label);
    } else {
        (void)snprintf(text, cap, "%s", "Quiet");
    }
    return color;
}

/* One tile: its button text, its disc and its state line. */
static void draw_tile(sounds_t *app, uint32_t klass, int32_t on_s)
{
    static const uint32_t tap_colors[DRAWN_STATES] = {
        COLOR_DIM, COLOR_WARN, COLOR_BAD, COLOR_TEXT, COLOR_TEXT};
    const episode_t *episode = &app->episodes[klass];
    tile_t *tile = &app->tiles[klass - 1U];
    char text[TEXT_BYTES];
    uint32_t disc = COLOR_DIM;
    uint32_t color = COLOR_DIM;

    (void)snprintf(text, sizeof(text), "%s %" PRId32 "x\nlast %s%s",
                   app->model.labels[klass], episode->events,
                   (app->last[klass][0] != '\0') ? app->last[klass]
                                                 : "-",
                   app->muted[klass] ? "\nMUTED" : "");
    put_text(app->ui, &tile->button, text);
    if (app->muted[klass]) {
        (void)snprintf(text, sizeof(text), "%s", "muted");
    } else if ((int32_t)klass == app->tap) {
        disc = disc_color(episode->state, app->blink);
        color = tap_colors[episode->state];
        tap_line(episode, on_s, text, sizeof(text));
    } else {
        color = info_line(episode, app->model.labels[klass], text,
                          sizeof(text));
        disc = color;
    }
    put_color(app->ui, &tile->disc, disc);
    put_text(app->ui, &tile->state, text);
    put_color(app->ui, &tile->state, color);
}

/* draw(): on_s = seconds the tap has run (0 = not running). */
static void draw(sounds_t *app, int32_t on_s)
{
    bool latched = false;
    uint32_t klass;

    for (klass = 1U; klass < app->class_count; klass++) {
        draw_tile(app, klass, on_s);
    }
    if (app->tap != NO_TAP) {
        const episode_state_t state = app->episodes[app->tap].state;

        latched = (state == EPISODE_UNACK) || (state == EPISODE_ENDED);
    }
    put_color(app->ui, &app->acknowledge,
              latched ? COLOR_BAD : COLOR_BUTTON);
}

/* A tile tapped: mute or unmute this sound. */
static void toggle_mute(sounds_t *app, uint32_t klass)
{
    char clock[CLOCK_BYTES];

    app->muted[klass] = !app->muted[klass];
    vote_reset(&app->votes[klass]);
    app->episodes[klass].state = EPISODE_NORMAL;
    if ((int32_t)klass == app->tap) {
        app->tap_running = false;
    }
    clock_text(clock, sizeof(clock));
    (void)printf("%s %s %s\n", clock, app->model.labels[klass],
                 app->muted[klass] ? "muted" : "on");
}

/* One clicked widget: ACK, or a tile. */
static void read_click(sounds_t *app, bento_ui_id_t widget,
                       bool *acknowledge)
{
    uint32_t klass;

    *acknowledge = *acknowledge || (widget == app->acknowledge.id);
    for (klass = 1U; klass < app->class_count; klass++) {
        if (widget == app->tiles[klass - 1U].button.id) {
            toggle_mute(app, klass);
        }
    }
}

/* The board buttons and the taps of this round. */
static bento_status_t read_inputs(sounds_t *app, bool *acknowledge)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = sample_button(app->has_buttons, &app->sw5);

    if (status == BENTO_OK) {
        status = sample_button(app->has_buttons, &app->sw6);
    }
    *acknowledge = button_clicked(&app->sw6);
    if (bento_ui_poll(app->ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U; /* a failed poll answers no events */
    }
    for (i = 0U; i < count; i++) {
        if (events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) {
            read_click(app, events[i].id, acknowledge);
        }
    }
    return status;
}

static void reset_votes(sounds_t *app)
{
    uint32_t klass;

    for (klass = 0U; klass < (uint32_t)CLASSES_SHOWN; klass++) {
        vote_reset(&app->votes[klass]);
    }
}

/* SW5: pause or resume. */
static bento_status_t toggle_pause(sounds_t *app)
{
    bento_status_t status = BENTO_OK;

    app->paused = !app->paused;
    app->tap_running = false;
    reset_votes(app);
    if (app->paused) {
        status = stop_ai();
        if (status == BENTO_OK) {
            put_text(app->ui, &app->status, "Paused - SW5 resumes");
            put_color(app->ui, &app->status, COLOR_WARN);
        }
    } else {
        status = start_model(app, app->model.index, app->row_count);
    }
    return status;
}

/* The tap is running from its first confirm until it clears; it
 * becomes an alarm after TAP_S. *on: it is running now. */
static bool tap_alarm(sounds_t *app, bool hit, bool clear,
                      uint32_t now_ms, bool *on)
{
    if (clear) {
        app->tap_running = false;
    } else if (!app->tap_running && hit) {
        app->tap_running = true;
        app->tap_on_ms = now_ms;
    } else {
        /* it keeps running, or keeps not running */
    }
    *on = app->tap_running;
    return app->tap_running &&
           (bento_time_ticks_diff(now_ms, app->tap_on_ms) >=
            (TAP_S * MS_PER_S));
}

/* One class of a new result: vote, then its episode unless muted. */
static void score_class(sounds_t *app, uint32_t klass, int32_t percent,
                        uint32_t now_ms)
{
    vote_t *vote = &app->votes[klass];
    char text[TEXT_BYTES];
    bool hit;
    bool clear;
    bool on = false;

    vote_add(vote, percent);
    hit = vote_hits(vote, (int32_t)app->thresholds.confirm[klass]);
    clear = vote_clear(vote, (int32_t)app->thresholds.exit[klass]);
    if ((int32_t)klass == app->tap) {
        hit = tap_alarm(app, hit, clear, now_ms, &on);
    }
    if (!app->muted[klass] &&
        episode_step(
            &app->episodes[klass],
            (percent >= (int32_t)app->thresholds.enter[klass]) || on,
            hit, clear, now_ms)) {
        clock_text(app->last[klass], sizeof(app->last[klass]));
        (void)snprintf(text, sizeof(text),
                       "%s %s %" PRId32 "%% #%" PRId32,
                       app->last[klass], app->model.labels[klass],
                       vote_max(vote), app->episodes[klass].events);
        (void)printf("%s\n", text);
        put_text(app->ui, &app->log, text);
    }
}

/* One new result: counted for the health line, then every tile's
 * class. */
static void score_result(sounds_t *app,
                         const bento_edgeai_result_t *result,
                         uint32_t now_ms)
{
    const float latency_ms =
        (float)result->latency_us / (float)MS_PER_S;
    uint32_t klass;

    app->results++;
    app->latency_ms += (bento_real_t)latency_ms;
    for (klass = 1U; klass < app->class_count; klass++) {
        score_class(app, klass, (int32_t)result->score_pct[klass],
                    now_ms);
    }
}

/* Every HEALTH_MS: results per second and the mean latency. */
static void draw_health(sounds_t *app, uint32_t now_ms)
{
    const int32_t elapsed_ms =
        bento_time_ticks_diff(now_ms, app->health_ms);
    const int32_t divisor = (app->results > 1) ? app->results : 1;
    char rate[BENTO_FMT_BUF_SIZE];
    char latency[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint32_t length = 0U;

    if (elapsed_ms >= HEALTH_MS) {
        if (bento_fmt_real_precision(
                rate, sizeof(rate),
                (bento_real_t)(app->results * MS_PER_S) /
                    (bento_real_t)elapsed_ms,
                1, &length) != BENTO_OK) {
            rate[0] = '\0';
        }
        if (bento_fmt_real_precision(latency, sizeof(latency),
                                     app->latency_ms /
                                         (bento_real_t)divisor,
                                     1, &length) != BENTO_OK) {
            latency[0] = '\0';
        }
        (void)snprintf(text, sizeof(text), "%s results/s | %s ms", rate,
                       latency);
        put_text(app->ui, &app->health, text);
        app->health_ms = now_ms;
        app->results = 0;
        app->latency_ms = (bento_real_t)0.0;
    }
}

/* Every DRAW_MS: the heartbeat, the tiles and the health line. */
static void redraw(sounds_t *app, uint32_t now_ms)
{
    const int32_t on_s =
        app->tap_running
            ? (bento_time_ticks_diff(now_ms, app->tap_on_ms) / MS_PER_S)
            : 0;

    app->drawn_ms = now_ms;
    app->blink = !app->blink;
    (void)bento_ui_set_value(app->ui, app->heartbeat,
                             (app->blink && (app->results != 0)) ? 1
                                                                 : 0);
    draw(app, on_s);
    draw_health(app, now_ms);
}

/* A result every POLL_MS and a redraw every DRAW_MS. */
static void poll_and_draw(sounds_t *app)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    bento_edgeai_result_t result;

    if (!app->paused &&
        (bento_time_ticks_diff(now_ms, app->polled_ms) >= POLL_MS)) {
        app->polled_ms = now_ms;
        if (read_result(app, app->model.index, &result)) {
            score_result(app, &result, now_ms);
        }
    }
    if (!app->stopped &&
        (bento_time_ticks_diff(now_ms, app->drawn_ms) >= DRAW_MS)) {
        redraw(app, now_ms);
    }
}

/* One round of the main loop. */
static bento_status_t sounds_round(sounds_t *app)
{
    bool acknowledge = false;
    bento_status_t status = read_inputs(app, &acknowledge);

    if ((status == BENTO_OK) && acknowledge && (app->tap != NO_TAP)) {
        episode_acknowledge(&app->episodes[app->tap]);
    }
    if ((status == BENTO_OK) && button_clicked(&app->sw5)) {
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

/* The tiles show classes 1 to 3; the tap class is the alarm. */
static void prepare_classes(sounds_t *app)
{
    static const char tap_label[] = "water_tap";
    uint32_t klass;

    app->class_count = app->model.class_count;
    if (app->class_count > (uint32_t)CLASSES_SHOWN) {
        app->class_count = CLASSES_SHOWN;
    }
    app->tap = NO_TAP;
    for (klass = 0U; klass < app->class_count; klass++) {
        if ((app->tap == NO_TAP) &&
            (strcmp(app->model.labels[klass], tap_label) == 0)) {
            app->tap = (int32_t)klass;
        }
    }
    for (klass = 0U; klass < app->class_count; klass++) {
        app->episodes[klass].latch = (int32_t)klass == app->tap;
        app->episodes[klass].state = EPISODE_NORMAL;
    }
}

/* run(): the thresholds, the classes, the model started, the loop. */
static bento_status_t run_sounds(sounds_t *app)
{
    bento_status_t status;

    read_thresholds(app);
    prepare_classes(app);
    status = start_model(app, app->model.index, 0U);
    app->drawn_ms = bento_time_ticks_ms();
    app->polled_ms = app->drawn_ms;
    app->health_ms = app->drawn_ms;
    while ((status == BENTO_OK) && !app->stopped) {
        status = sounds_round(app);
    }
    return status;
}

/* run() inside its try: a Stop is shown and printed, and the AI core
 * is always given back. */
static bento_status_t run_then_stop(sounds_t *app)
{
    bento_status_t status = run_sounds(app);
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
    sounds_t *app = &g_sounds;
    bool supported = false;
    bento_status_t status;

    (void)memset(app, 0, sizeof(*app));
    app->ui = bento_runtime_get_ui();
    app->has_buttons = bento_buttons_count() > 0U;
    app->sw5.index = 1U;
    app->sw6.index = 0U;
    app->model_name = MODEL_NAME;
    if (bento_edgeai_supported(&supported) == BENTO_E_UNSUPPORTED) {
        status = show_emulator_note(app->ui);
    } else if (!find_model(MODEL_NAME, false, &app->model,
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
