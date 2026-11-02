/*
 * voice_commands.c -- บังคับรถเข็นด้วยเสียง (Steer the Cart)
 *
 * บังคับรถเข็นอาหารสัตว์บนลานขนาด 5 x 5 ช่องด้วยโมเดล "Voice Commands"
 * (ทดลอง) จาก Edge AI Store พูด left right up down เพื่อหันหัว (และขยับ
 * หนึ่งช่องเมื่อจอดอยู่) พูด go เพื่อวิ่งช่องละวินาที พูด stop เพื่อหยุด
 * รั้วรอบลานหยุดรถเอง Safe stop เปิดอยู่: ได้ยิน stop เพียงครั้งเดียวก็หยุด
 * ทันที แตะปุ่ม Safe stop หรือปุ่ม SW6 (บน) = เปิด/ปิด Safe stop
 * ปุ่ม SW5 (ล่าง) = หยุดพัก/ทำต่อ
 *
 * Steers a feed cart on a 5 x 5 yard with the "Voice Commands"
 * (experimental) model from the Edge AI Store. Say left, right, up or
 * down to turn (and step one cell when halted), go to drive one cell
 * per second, stop to halt; the fence stops it too. With Safe stop on,
 * one stop score at Hearing at halts a moving cart at once. A tap on
 * Safe stop or SW6 (upper) switches it, SW5 (lower) pauses and
 * resumes.
 *
 * Twin of voice_commands.py
 * (embedded-systems-for-aiot-developer/examples/edge_ai_models/voice_commands.py).
 * Style: CLEAN_C.md. Written new; devkit-21-ai-model-lab gave the style.
 * Differs from the original:
 *   - gc.collect() and the free-heap print are left out: C has no
 *     garbage collector.
 *   - m["builtin"] is "row index below diag.compiled_models"; when the
 *     diag cannot be read it is None (the Emulator's case), so a Store
 *     model (BUILTIN false) is not matched then, as in the .py.
 *   - edge_ai.select() and stop() wait inside the binding; here they
 *     are started and polled every 20 ms, and the IDE's Stop cancels
 *     the wait. A failed select prints the binding's own words
 *     ("edge_ai: select send failed" or "edge_ai: select not
 *     confirmed").
 *   - The Stop exception is a stop text kept in the program state; the
 *     loop ends when it is set, then the text goes on the status line
 *     and the console, and the AI core is stopped, as in the .py.
 *   - A board without edge_ai (the .py stops at the import) shows the
 *     "Model not on this board" card. A board without buttons
 *     (bento_buttons_count() 0, the Eva Kit) is the .py's buttons =
 *     None.
 *   - A score is score_pct, so int(score * 100 + 0.5) is that number.
 *   - The Episode keeps only its info path (NORMAL, HEARD, SEEN): this
 *     example makes it with latch False, so the alarm states and
 *     ack() never occur.
 *   - max() of an empty vote, which raises ValueError in the .py, is 0
 *     here; it can only happen for a model without a "stop" class.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_buttons.h"
#include "bento_edgeai.h"
#include "bento_runtime.h"
#include "bento_time.h"

enum {                 /* times, ms */
    REFRACT_MS = 1000, /* no second word sooner than this */
    HOLD_MS = 2000,    /* a word stays on screen this long */
    POLL_MS = 100,
    DRAW_MS = 500,
    LOOP_MS = 20,
    LOAD_MS = 150,
    OP_POLL_MS = 20,
    FAIL_MS = 3000
};
enum { /* a word = VOTE_HITS of the last VOTE_WINDOW results */
    VOTE_HITS = 2,
    VOTE_WINDOW = 4
};
enum { /* fallback thresholds, %: Hearing at, Confirm at, clear below */
    DEFAULT_ENTER = 40,
    DEFAULT_CONFIRM = 65,
    DEFAULT_EXIT = 30
};
enum { /* the yard: YARD_CELLS x YARD_CELLS, the cart starts mid-yard */
    YARD_CELLS = 5,
    START_CELL = 2,
    CELL_PX = 45,
    DOT_X = 32,
    DOT_Y = 100,
    DIRECTION_COUNT = 4
};
enum { /* sizes */
    MAX_CLASSES = BENTO_EDGEAI_MAX_CLASSES,
    SCORE_ROWS = 3,
    LOG_LINES = 3,
    TEXT_BYTES = 128,
    /* "HH:MM:SS"; at worst three int32_t, two colons and the NUL */
    CLOCK_BYTES = 36,
    SW5_INDEX = 1,
    SW6_INDEX = 0
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
enum { /* episode states (the info path) */
    STATE_NORMAL = 0,
    STATE_HEARD = 1,
    STATE_SEEN = 5
};
enum { /* font sizes, px */
    SMALL_PX = 16,
    MEDIUM_PX = 20,
    LARGE_PX = 24,
    HUGE_PX = 28
};

/* The exact name, looked up every run. */
static const char MODEL_NAME[] = "Voice Commands";

/* A word that turns the cart, and the step it makes. */
typedef struct {
    const char *word;
    int32_t step_x;
    int32_t step_y;
} direction_t;

static const direction_t DIRECTIONS[DIRECTION_COUNT] = {
    {"up", 0, -1}, {"down", 0, 1}, {"left", -1, 0}, {"right", 1, 0}};

/* A rectangle on the screen; -1 = the widget's own size. */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} rect_t;

/* One widget as ui.<Factory>(...) makes it. */
typedef struct {
    uint8_t kind;     /* UI_WIDGET_* */
    const char *text; /* NULL: no text */
    rect_t place;
    uint32_t color;
    int32_t value;   /* value=: font size, start value, border width */
    int32_t minimum; /* min=: a Panel's border colour */
    int32_t maximum; /* max=: a Panel's corner radius */
} widget_t;

/* A widget the program changes, with what was sent last (put()). */
typedef struct {
    bento_ui_id_t id;
    bool has_text;
    bool has_color;
    uint32_t color;
    char text[TEXT_BYTES];
} shown_t;

typedef struct {
    uint32_t index;
    bool down;
    bool clicked;
} board_button_t;

typedef struct {
    uint32_t index; /* what edge_ai.select() takes */
    uint32_t class_count;
    int32_t row_count; /* len(edge_ai.models()) when it was found */
    char labels[MAX_CLASSES][BENTO_EDGEAI_LABEL_LEN];
} model_t;

typedef struct {
    bento_ui_id_t heartbeat;
    bento_ui_id_t dot;
    shown_t word;
    shown_t mode;
    shown_t log;
    shown_t safe;
    shown_t score_text[SCORE_ROWS];
    shown_t legend;
    shown_t status;
} screen_t;

/* The last N scores; K of them at or above a level = confirmed. */
typedef struct {
    uint8_t scores[VOTE_WINDOW];
    uint32_t count;
} vote_t;

/* NORMAL > HEARD > SEEN for HOLD_MS, no ACK. */
typedef struct {
    int32_t state;
    int32_t count;
    bool has_time;
    uint32_t time_ms;
} episode_t;

/* The thresholds of the strongest event class: event_of(). */
typedef struct {
    uint32_t klass;
    uint8_t percent;
    uint8_t enter;
    uint8_t confirm;
    uint8_t exit;
} event_t;

/* The dot on the 5 x 5 yard; the fence stops it. */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t direction; /* index into DIRECTIONS */
    bool going;
} cart_t;

typedef struct {
    bento_ui_t *ui;
    bool has_buttons;
    board_button_t sw5;
    board_button_t sw6;
    model_t model;
    screen_t screen;
    bento_edgeai_thresholds_t thresholds;
    uint32_t stop_class; /* sp: the "stop" class, 0 when none */
    episode_t episode;
    vote_t votes[MAX_CLASSES];
    bool has_votes; /* the .py's vs != 0 */
    uint32_t held;  /* a word that counted and still sounds */
    char word[BENTO_EDGEAI_LABEL_LEN];
    bool has_top;
    uint8_t top[MAX_CLASSES];
    uint32_t top_count;
    cart_t cart;
    bool safe;
    char log[LOG_LINES][TEXT_BYTES];
    uint32_t log_count;
    bool paused;
    bool blink;
    bool has_sequence;
    uint32_t last_sequence;
    bool failing;
    uint32_t fail_start_ms;
    uint32_t polled_ms;
    uint32_t drawn_ms;
    bool stopped;
    char stop_text[TEXT_BYTES];
} steer_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern steer_t g_steer;
steer_t g_steer;

/* A position or size on the screen: the widget arguments are 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
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

static bento_status_t add_widget(bento_ui_t *ui, const widget_t *spec,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    if (spec->text != NULL) {
        args.text = spec->text;
        args.text_len = (uint32_t)strlen(spec->text);
    }
    args.x = px(spec->place.x);
    args.y = px(spec->place.y);
    args.w = px(spec->place.w);
    args.h = px(spec->place.h);
    args.color = spec->color;
    args.font_px = spec->value;
    args.min_val = spec->minimum;
    args.max_val = spec->maximum;
    return bento_ui_create(ui, spec->kind, &args, out_id);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                rect_t place, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    const widget_t spec = {UI_WIDGET_LABEL,
                           text,
                           {place.x, place.y, place.w, place.h},
                           color,
                           font_px,
                           0,
                           0};

    return add_widget(ui, &spec, out_id);
}

/* A card: a rounded panel and its title. */
static bento_status_t add_card(bento_ui_t *ui, rect_t place,
                               const char *title)
{
    const widget_t panel = {UI_WIDGET_PANEL,
                            NULL,
                            {place.x, place.y, place.w, place.h},
                            COLOR_CARD,
                            1,
                            COLOR_DIM,
                            12};
    const rect_t title_at = {place.x + 14, place.y + 8, -1, -1};
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_widget(ui, &panel, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, title, title_at, COLOR_INFO, SMALL_PX,
                           &unused);
    }
    return status;
}

/* put(): send only what changed, a full display queue drops commands.
 * A setter's refusal was ignored before too (modui.c). */
static void put_text(bento_ui_t *ui, shown_t *shown, const char *text)
{
    if (!shown->has_text || (strcmp(shown->text, text) != 0)) {
        shown->has_text = true;
        (void)snprintf(shown->text, sizeof(shown->text), "%s", text);
        (void)bento_ui_set_text(ui, shown->id, text,
                                (uint32_t)strlen(text));
    }
}

static void put_color(bento_ui_t *ui, shown_t *shown, uint32_t color)
{
    if (!shown->has_color || (shown->color != color)) {
        shown->has_color = true;
        shown->color = color;
        (void)bento_ui_set_color(ui, shown->id, color);
    }
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

/* The Stop exception: the loops end once this is set. */
static void raise_stop(steer_t *steer, const char *text)
{
    steer->stopped = true;
    (void)snprintf(steer->stop_text, sizeof(steer->stop_text), "%s",
                   text);
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

/* Reads a board button and remembers a new press (a rising edge). */
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

/* One event per press; asking clears it. */
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

/* stop_ai(): an OSError is passed over; only Stop comes back. */
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

/* m.get("builtin"): -1 when the board cannot say (None). */
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

/* BUILTIN true takes True or None; BUILTIN false takes only False. */
static bool source_matches(int32_t index, int32_t compiled)
{
    const bool want_builtin =
        false; /* false = deployed from the Store */
    bool matches = want_builtin;

    if (compiled >= 0) {
        matches =
            want_builtin ? (index < compiled) : (index >= compiled);
    }
    return matches;
}

/* The labels of the found model; one that cannot be read is "". */
static void keep_model(model_t *model, uint32_t index,
                       const bento_edgeai_model_info_t *info)
{
    uint32_t length = 0U;
    uint32_t i;

    model->index = index;
    model->class_count = (info->class_count > (uint8_t)MAX_CLASSES)
                             ? (uint32_t)MAX_CLASSES
                             : info->class_count;
    for (i = 0U; i < model->class_count; i++) {
        if (bento_edgeai_label(index, i, model->labels[i],
                               BENTO_EDGEAI_LABEL_LEN,
                               &length) != BENTO_OK) {
            model->labels[i][0] = '\0';
        }
    }
}

/* One row: counted, and kept when it is MODEL_NAME from the right
 * source and nothing was found before. */
static void consider_row(model_t *model, bool *found,
                         const bento_edgeai_model_info_t *info,
                         int32_t index, int32_t compiled)
{
    if (!*found && (strcmp(info->name, MODEL_NAME) == 0) &&
        source_matches(index, compiled)) {
        *found = true;
        keep_model(model, (uint32_t)index, info);
    }
}

/* Every row of the registry. *rows counts the rows read (len(ms)). */
static bento_status_t scan_models(model_t *model, bool *found,
                                  int32_t *rows)
{
    bento_edgeai_model_info_t info;
    int32_t count = 0;
    int32_t i;
    const int32_t compiled = compiled_count();
    bento_status_t status = bento_edgeai_count(&count);

    for (i = 0; (i < count) && (status == BENTO_OK); i++) {
        bento_edgeai_model_info_init(&info);
        status = bento_edgeai_model_info((uint32_t)i, &info);
        if (status == BENTO_OK) {
            (*rows)++;
            consider_row(model, found, &info, i, compiled);
        } else if ((status == BENTO_E_NOT_READY) ||
                   (status == BENTO_E_NOT_FOUND)) {
            /* a row the engine refuses is not listed */
            status = BENTO_OK;
        } else {
            /* a link failure: models() raises */
        }
    }
    return status;
}

/* find_model(): never reads the model id. */
static void find_model(model_t *model, bool *found)
{
    bool supported = false;
    int32_t rows = 0;
    bento_status_t status;

    *found = false;
    if ((bento_edgeai_supported(&supported) == BENTO_OK) && supported) {
        status = scan_models(model, found, &rows);
        if (status != BENTO_OK) {
            (void)printf("cannot read models: %s\n",
                         link_error_text(status));
            *found = false;
        }
        model->row_count = rows;
    }
}

/* The card that says why nothing runs. */
static bento_status_t missing(bento_ui_t *ui, const char *title,
                              const char *text)
{
    static const widget_t panel = {
        UI_WIDGET_PANEL, NULL, {96, 110, 600, 180}, COLOR_CARD, 1,
        COLOR_BAD,       12};
    static const rect_t title_at = {120, 126, -1, -1};
    static const rect_t text_at = {120, 170, -1, -1};
    bento_ui_id_t unused = 0U;
    bento_ui_id_t body = 0U;
    bento_status_t status;

    (void)printf("%s\n", text);
    status = bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                             BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_widget(ui, &panel, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, title, title_at, COLOR_BAD, LARGE_PX,
                           &unused);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, " ", text_at, COLOR_TEXT, MEDIUM_PX, &body);
    }
    if (status == BENTO_OK) {
        /* the text fits the 126 bytes one SET_TEXT carries */
        (void)bento_ui_set_text(ui, body, text, (uint32_t)strlen(text));
        drain_events(ui);
    }
    return status;
}

static bento_status_t missing_model(bento_ui_t *ui)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "Deploy %s\nfrom edgeai-store.tesaiot.dev\n(sign in "
                   "with GitHub) on a Dev Kit.",
                   MODEL_NAME);
    return missing(ui, "Model not on this board", text);
}

/* The heading, the subtitle and the "Feed yard" card. */
static bento_status_t add_yard_card(steer_t *steer)
{
    static const rect_t title_at = {12, 4, -1, -1};
    static const widget_t heartbeat = {
        UI_WIDGET_LED, NULL, {756, 6, 26, 26}, COLOR_OK, 0, 0, 0};
    static const rect_t subtitle_at = {12, 38, -1, -1};
    static const rect_t card_at = {12, 64, 470, 272};
    static const widget_t yard = {
        UI_WIDGET_PANEL, NULL, {28, 96, 225, 225}, COLOR_CARD, 1,
        COLOR_DIM,       0};
    bento_ui_id_t unused = 0U;
    bento_status_t status =
        add_label(steer->ui, "Steer the Cart", title_at, COLOR_TEXT,
                  LARGE_PX, &unused);

    if (status == BENTO_OK) {
        status =
            add_widget(steer->ui, &heartbeat, &steer->screen.heartbeat);
    }
    if (status == BENTO_OK) {
        status = add_label(steer->ui,
                           "Experimental. English only: left right up "
                           "down stop go. Cannot: other words.",
                           subtitle_at, COLOR_DIM, SMALL_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(steer->ui, card_at, "Feed yard");
    }
    if (status == BENTO_OK) {
        status = add_widget(steer->ui, &yard, &unused);
    }
    return status;
}

/* Grid line number `line` (1..4): one across the yard, one down it. */
static bento_status_t add_grid_line(steer_t *steer, int32_t line)
{
    enum {
        LINE_PX = 2,
        LONG_PX = 225,
        LEFT_X = 28,
        TOP_Y = 96
    };
    const widget_t across = {
        UI_WIDGET_PANEL,
        NULL,
        {(LEFT_X - 1) + (CELL_PX * line), TOP_Y, LINE_PX, LONG_PX},
        COLOR_BUTTON,
        0,
        0,
        0};
    const widget_t down = {
        UI_WIDGET_PANEL,
        NULL,
        {LEFT_X, (TOP_Y - 1) + (CELL_PX * line), LONG_PX, LINE_PX},
        COLOR_BUTTON,
        0,
        0,
        0};
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_widget(steer->ui, &across, &unused);

    if (status == BENTO_OK) {
        status = add_widget(steer->ui, &down, &unused);
    }
    return status;
}

/* The grid lines, then the cart at cell (2, 2). */
static bento_status_t add_grid(steer_t *steer)
{
    enum {
        DOT_PX = 37,
        DOT_RADIUS = 18
    };
    const widget_t dot = {UI_WIDGET_PANEL,
                          NULL,
                          {DOT_X + (CELL_PX * START_CELL),
                           DOT_Y + (CELL_PX * START_CELL), DOT_PX,
                           DOT_PX},
                          COLOR_INFO,
                          0,
                          0,
                          DOT_RADIUS};
    bento_status_t status = BENTO_OK;
    int32_t line;

    for (line = 1; (line < YARD_CELLS) && (status == BENTO_OK);
         line++) {
        status = add_grid_line(steer, line);
    }
    if (status == BENTO_OK) {
        status = add_widget(steer->ui, &dot, &steer->screen.dot);
    }
    return status;
}

/* The word, the mode, the log and the Safe stop button. */
static bento_status_t add_words(steer_t *steer)
{
    static const rect_t word_at = {266, 100, -1, -1};
    static const rect_t mode_at = {266, 140, -1, -1};
    static const rect_t log_at = {266, 170, -1, -1};
    static const widget_t safe = {UI_WIDGET_BUTTON,
                                  "Safe stop: ON",
                                  {266, 262, 200, 64},
                                  COLOR_BUTTON,
                                  20,
                                  0,
                                  0};
    screen_t *screen = &steer->screen;
    bento_status_t status = add_label(
        steer->ui, "-", word_at, COLOR_DIM, HUGE_PX, &screen->word.id);

    if (status == BENTO_OK) {
        status = add_label(steer->ui, " ", mode_at, COLOR_TEXT,
                           SMALL_PX, &screen->mode.id);
    }
    if (status == BENTO_OK) {
        status = add_label(steer->ui, " ", log_at, COLOR_TEXT, SMALL_PX,
                           &screen->log.id);
    }
    if (status == BENTO_OK) {
        status = add_widget(steer->ui, &safe, &screen->safe.id);
    }
    return status;
}

/* The "Top 3 scores" card and its legend. */
static bento_status_t add_scores_card(steer_t *steer)
{
    enum {
        ROW_X = 508,
        ROW_Y = 100,
        ROW_STEP = 40
    };
    static const rect_t card_at = {494, 64, 294, 272};
    static const rect_t legend_at = {508, 230, -1, -1};
    screen_t *screen = &steer->screen;
    bento_status_t status =
        add_card(steer->ui, card_at, "Top 3 scores");
    int32_t i;

    for (i = 0; (i < SCORE_ROWS) && (status == BENTO_OK); i++) {
        const rect_t text_at = {ROW_X, ROW_Y + (ROW_STEP * i), -1, -1};

        status = add_label(steer->ui, " ", text_at, COLOR_DIM,
                           MEDIUM_PX, &screen->score_text[i].id);
    }
    if (status == BENTO_OK) {
        status = add_label(steer->ui, " ", legend_at, COLOR_DIM,
                           SMALL_PX, &screen->legend.id);
    }
    return status;
}

static bento_status_t build_screen(steer_t *steer)
{
    static const rect_t status_at = {12, 346, -1, -1};
    bento_status_t status =
        bento_ui_screen(steer->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_yard_card(steer);
    }
    if (status == BENTO_OK) {
        status = add_grid(steer);
    }
    if (status == BENTO_OK) {
        status = add_words(steer);
    }
    if (status == BENTO_OK) {
        status = add_scores_card(steer);
    }
    if (status == BENTO_OK) {
        status = add_label(steer->ui, " ", status_at, COLOR_WARN,
                           SMALL_PX, &steer->screen.status.id);
    }
    if (status == BENTO_OK) {
        drain_events(steer->ui);
    }
    return status;
}

/* read_th(): per class Hearing at, Confirm at, clear below. */
static void read_thresholds(steer_t *steer)
{
    bento_edgeai_thresholds_t *thresholds = &steer->thresholds;
    uint32_t i;

    bento_edgeai_thresholds_init(thresholds);
    if (bento_edgeai_thresholds(steer->model.index, thresholds) !=
        BENTO_OK) {
        for (i = 0U; i < (uint32_t)MAX_CLASSES; i++) {
            thresholds->enter[i] = (uint8_t)DEFAULT_ENTER;
            thresholds->confirm[i] = (uint8_t)DEFAULT_CONFIRM;
            thresholds->exit[i] = (uint8_t)DEFAULT_EXIT;
        }
    }
}

/* edge_ai.count() on resume: a changed list is the Stop "Model list
 * changed"; a failed read is the OSError. */
static bento_status_t check_list(steer_t *steer)
{
    int32_t count = 0;
    const bento_status_t link = bento_edgeai_count(&count);

    if (link != BENTO_OK) {
        (void)printf("select: %s\n", link_error_text(link));
    } else if (count != steer->model.row_count) {
        raise_stop(steer, "Model list changed - run again");
    } else {
        /* the same list */
    }
    return link;
}

/* edge_ai.select(i): loads and starts it, up to 15 s. *link is the
 * OSError, if any; the status is the IDE's Stop. */
static bento_status_t select_model(const steer_t *steer,
                                   bento_status_t *link)
{
    bento_status_t status = BENTO_OK;

    *link = bento_edgeai_select_start(steer->model.index);
    if (*link != BENTO_OK) {
        (void)printf("select: %s\n", "edge_ai: select send failed");
    } else {
        status = finish_op();
        if ((status != BENTO_OK) && (status != BENTO_E_CANCELLED)) {
            (void)printf("select: %s\n",
                         "edge_ai: select not confirmed");
            *link = status;
            status = BENTO_OK;
        }
    }
    return status;
}

/* start(): on resume, Stop if a model was added or removed meanwhile. */
static bento_status_t start_model(steer_t *steer, bool check_count)
{
    static const char help_buttons[] =
        "Say a word - SW6 = Safe stop, SW5 = pause";
    static const char help_touch[] = "Say a word";
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    bento_status_t link = BENTO_OK;

    (void)snprintf(text, sizeof(text), "Loading %s ...", MODEL_NAME);
    put_text(steer->ui, &steer->screen.status, text);
    put_color(steer->ui, &steer->screen.status, COLOR_WARN);
    drain_events(steer->ui);
    steer->failing = false;
    if (check_count) {
        link = check_list(steer);
    }
    if ((link == BENTO_OK) && !steer->stopped) {
        status = select_model(steer, &link);
    }
    if ((status == BENTO_OK) && !steer->stopped && (link != BENTO_OK)) {
        (void)snprintf(text, sizeof(text), "Cannot start %s",
                       MODEL_NAME);
        raise_stop(steer, text);
    }
    if ((status == BENTO_OK) && !steer->stopped) {
        put_text(steer->ui, &steer->screen.status,
                 steer->has_buttons ? help_buttons : help_touch);
        put_color(steer->ui, &steer->screen.status, COLOR_TEXT);
    }
    return status;
}

/* read_result(): only NEW results of this model; Stop after 3 s of
 * errors. */
static bool read_result(steer_t *steer, bento_edgeai_result_t *result)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    bool fresh = false;

    bento_edgeai_result_init(result);
    if (bento_edgeai_result(result) != BENTO_OK) {
        if (!steer->failing) {
            steer->failing = true;
            steer->fail_start_ms = now_ms;
        }
        if (bento_time_ticks_diff(now_ms, steer->fail_start_ms) >=
            FAIL_MS) {
            raise_stop(steer, "AI core not answering - run again");
        }
    } else {
        steer->failing = false;
        fresh = result->has_verdict &&
                (!steer->has_sequence ||
                 (result->seq != steer->last_sequence)) &&
                ((uint32_t)result->index == steer->model.index);
        if (fresh) {
            steer->has_sequence = true;
            steer->last_sequence = result->seq;
        }
    }
    return fresh;
}

static void vote_add(vote_t *vote, uint8_t score)
{
    uint32_t i;

    if (vote->count < (uint32_t)VOTE_WINDOW) {
        vote->scores[vote->count] = score;
        vote->count++;
    } else {
        for (i = 1U; i < (uint32_t)VOTE_WINDOW; i++) {
            vote->scores[i - 1U] = vote->scores[i];
        }
        vote->scores[VOTE_WINDOW - 1] = score;
    }
}

static bool vote_hits(const vote_t *vote, uint8_t level)
{
    int32_t hits = 0;
    uint32_t i;

    for (i = 0U; i < vote->count; i++) {
        if (vote->scores[i] >= level) {
            hits++;
        }
    }
    return hits >= VOTE_HITS;
}

static uint8_t vote_max(const vote_t *vote)
{
    uint8_t highest = 0U;
    uint32_t i;

    for (i = 0U; i < vote->count; i++) {
        if (vote->scores[i] > highest) {
            highest = vote->scores[i];
        }
    }
    return highest;
}

static bool vote_clear(const vote_t *vote, uint8_t level)
{
    return (vote->count == (uint32_t)VOTE_WINDOW) &&
           (vote_max(vote) < level);
}

/* No new word: a shown word stays HOLD_MS, then the state follows the
 * sound. */
static void episode_settle(episode_t *episode, bool heard, bool clear,
                           uint32_t now_ms)
{
    const bool holding =
        (episode->state == STATE_SEEN) &&
        (bento_time_ticks_diff(now_ms, episode->time_ms) < HOLD_MS);

    if (!holding) {
        if (clear || (episode->state == STATE_SEEN)) {
            episode->state = STATE_NORMAL;
        }
        if (!clear && heard && (episode->state == STATE_NORMAL)) {
            episode->state = STATE_HEARD;
        }
    }
}

/* True once per new word; no second word sooner than REFRACT_MS. */
static bool episode_step(episode_t *episode, bool heard, bool confirm,
                         bool clear, uint32_t now_ms)
{
    const bool rested =
        !episode->has_time ||
        (bento_time_ticks_diff(now_ms, episode->time_ms) >= REFRACT_MS);
    const bool fired = confirm && rested;

    if (fired) {
        episode->state = STATE_SEEN;
        episode->count++;
        episode->has_time = true;
        episode->time_ms = now_ms;
    } else {
        episode_settle(episode, heard, clear, now_ms);
    }
    return fired;
}

/* event_of(): the strongest EVENT class (class 0 = nothing) and its
 * thresholds. */
static void event_of(const steer_t *steer, uint32_t classes,
                     event_t *event)
{
    const bento_edgeai_thresholds_t *thresholds = &steer->thresholds;
    uint32_t strongest = 0U;
    uint32_t i;

    for (i = 1U; i < classes; i++) {
        if ((steer->top[i] > steer->top[strongest]) ||
            (strongest == 0U)) {
            strongest = i;
        }
    }
    event->klass = strongest;
    event->percent = steer->top[strongest];
    event->enter = thresholds->enter[strongest];
    event->confirm = thresholds->confirm[strongest];
    event->exit = thresholds->exit[strongest];
}

/* The legend: class 1's thresholds and the Safe stop rule. */
static void show_legend(steer_t *steer)
{
    const bento_edgeai_thresholds_t *thresholds = &steer->thresholds;
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "Hearing at %u%% | Confirm at %u%%\nSafe stop: stop "
                   "at %u%%, 1 of %d",
                   (unsigned int)thresholds->enter[1],
                   (unsigned int)thresholds->confirm[1],
                   (unsigned int)thresholds->enter[steer->stop_class],
                   VOTE_WINDOW);
    put_text(steer->ui, &steer->screen.legend, text);
}

/* sp: the index of the "stop" class, 0 when the model has none. */
static void find_stop_class(steer_t *steer)
{
    uint32_t i;
    bool found = false;

    steer->stop_class = 0U;
    for (i = 0U; (i < steer->model.class_count) && !found; i++) {
        if (strcmp(steer->model.labels[i], "stop") == 0) {
            steer->stop_class = i;
            found = true;
        }
    }
}

/* Fresh votes: the last N scores of each class. */
static void reset_votes(steer_t *steer)
{
    (void)memset(steer->votes, 0, sizeof(steer->votes));
    steer->has_votes = true;
    steer->held = 0U;
    steer->episode.state = STATE_NORMAL;
}

/* all(v.clear(level) for v in vs[1:]) */
static bool all_clear(const steer_t *steer, uint8_t level)
{
    bool clear = true;
    uint32_t i;

    for (i = 1U; i < steer->model.class_count; i++) {
        clear = clear && vote_clear(&steer->votes[i], level);
    }
    return clear;
}

/* The newest line first; three lines are kept. */
static void add_log_line(steer_t *steer, const char *line)
{
    char joined[TEXT_BYTES];
    size_t used = 0U;
    uint32_t i;

    for (i = LOG_LINES - 1U; i > 0U; i--) {
        (void)memcpy(steer->log[i], steer->log[i - 1U],
                     sizeof(steer->log[i]));
    }
    (void)snprintf(steer->log[0], sizeof(steer->log[0]), "%s", line);
    if (steer->log_count < (uint32_t)LOG_LINES) {
        steer->log_count++;
    }
    (void)printf("%s\n", line);
    joined[0] = '\0';
    for (i = 0U; i < steer->log_count; i++) {
        (void)snprintf(&joined[used], sizeof(joined) - used, "%s%s",
                       (i > 0U) ? "\n" : "", steer->log[i]);
        used = strlen(joined);
    }
    put_text(steer->ui, &steer->screen.log, joined);
}

/* Cart.step(): one cell ahead; the fence stops it. */
static void cart_step(steer_t *steer)
{
    cart_t *cart = &steer->cart;
    const direction_t *direction = &DIRECTIONS[cart->direction];
    const int32_t x = cart->x + direction->step_x;
    const int32_t y = cart->y + direction->step_y;

    if ((x >= 0) && (x < YARD_CELLS) && (y >= 0) && (y < YARD_CELLS)) {
        cart->x = x;
        cart->y = y;
        (void)bento_ui_set_pos(steer->ui, steer->screen.dot,
                               px(DOT_X + (CELL_PX * x)),
                               px(DOT_Y + (CELL_PX * y)));
    } else {
        cart->going = false;
    }
}

/* Cart.hear(): a direction turns (and steps when halted); go and stop
 * switch driving. */
static void cart_hear(steer_t *steer, const char *word)
{
    cart_t *cart = &steer->cart;
    int32_t turn = -1;
    int32_t i;

    for (i = 0; i < DIRECTION_COUNT; i++) {
        if (strcmp(word, DIRECTIONS[i].word) == 0) {
            turn = i;
        }
    }
    if (turn >= 0) {
        cart->direction = turn;
        if (!cart->going) {
            cart_step(steer);
        }
    } else {
        cart->going = strcmp(word, "go") == 0;
    }
}

/* A new word: the cart hears it, one console line, the log. */
static void count_word(steer_t *steer, uint32_t klass)
{
    char clock[CLOCK_BYTES];
    char line[TEXT_BYTES];

    steer->held = klass;
    (void)snprintf(steer->word, sizeof(steer->word), "%s",
                   steer->model.labels[klass]);
    cart_hear(steer, steer->word);
    if (steer->cart.going) {
        /* Safe stop hears only stop scores newer than this go */
        (void)memset(&steer->votes[steer->stop_class], 0,
                     sizeof(steer->votes[0]));
    }
    clock_text(clock, sizeof(clock));
    (void)snprintf(line, sizeof(line), "%s %s %u%% #%" PRId32, clock,
                   steer->word,
                   (unsigned int)vote_max(&steer->votes[klass]),
                   steer->episode.count);
    add_log_line(steer, line);
}

/* Safe stop: a moving cart halts on one stop score at Hearing at. */
static bool safe_stop(const steer_t *steer)
{
    const uint32_t stop = steer->stop_class;

    return steer->safe && steer->cart.going && (stop != 0U) &&
           (vote_max(&steer->votes[stop]) >=
            steer->thresholds.enter[stop]);
}

/* A word = K of its last N at Confirm at, once per utterance. */
static void take_result(steer_t *steer,
                        const bento_edgeai_result_t *result,
                        uint32_t now_ms)
{
    const uint32_t classes =
        (result->class_count < steer->model.class_count)
            ? result->class_count
            : steer->model.class_count;
    event_t event;
    bool confirmed;
    uint32_t i;

    if (classes > 0U) {
        steer->has_top = true;
        steer->top_count = classes;
        for (i = 0U; i < classes; i++) {
            steer->top[i] = result->score_pct[i];
            vote_add(&steer->votes[i], steer->top[i]);
        }
        event_of(steer, classes, &event);
        if ((steer->held != 0U) &&
            !vote_hits(&steer->votes[steer->held],
                       steer->thresholds.exit[steer->held])) {
            steer->held = 0U; /* that word ended: it may count again */
        }
        confirmed =
            (event.klass != steer->held) &&
            vote_hits(&steer->votes[event.klass], event.confirm);
        if (safe_stop(steer)) { /* no refractory wait */
            event.klass = steer->stop_class;
            confirmed = true;
            steer->episode.has_time = false;
        }
        if (episode_step(&steer->episode, event.percent >= event.enter,
                         confirmed, all_clear(steer, event.exit),
                         now_ms)) {
            count_word(steer, event.klass);
        }
    }
}

/* str.upper() for the ASCII letters of a label. */
static void upper_text(const char *word, char *text, size_t cap)
{
    size_t i;

    (void)snprintf(text, cap, "%s", word);
    for (i = 0U; text[i] != '\0'; i++) {
        if ((text[i] >= 'a') && (text[i] <= 'z')) {
            text[i] = (char)(text[i] - ('a' - 'A'));
        }
    }
}

/* The next row of the top 3: the highest score not shown yet, the
 * lower class first on a tie (sorted() is stable). */
static uint32_t next_best(const steer_t *steer, const bool *shown)
{
    uint32_t best = steer->top_count;
    uint32_t i;

    for (i = 0U; i < steer->top_count; i++) {
        if (!shown[i] && ((best == steer->top_count) ||
                          (steer->top[i] > steer->top[best]))) {
            best = i;
        }
    }
    return best;
}

/* The three best classes; white = at Confirm at. */
static void draw_scores(steer_t *steer)
{
    bool shown[MAX_CLASSES];
    char text[TEXT_BYTES];
    uint32_t row;

    (void)memset(shown, 0, sizeof(shown));
    for (row = 0U;
         (row < (uint32_t)SCORE_ROWS) && (row < steer->top_count);
         row++) {
        const uint32_t klass = next_best(steer, shown);
        const uint8_t percent = steer->top[klass];
        const bool confirmed =
            (klass > 0U) &&
            (percent >= steer->thresholds.confirm[klass]);

        shown[klass] = true;
        (void)snprintf(text, sizeof(text), "%s %u%%",
                       steer->model.labels[klass],
                       (unsigned int)percent);
        put_text(steer->ui, &steer->screen.score_text[row], text);
        put_color(steer->ui, &steer->screen.score_text[row],
                  confirmed ? COLOR_TEXT : COLOR_DIM);
    }
}

/* Every DRAW_MS: the cart drives every other draw (1 cell/s). */
static void redraw(steer_t *steer, uint32_t now_ms)
{
    char text[TEXT_BYTES];

    steer->drawn_ms = now_ms;
    steer->blink = !steer->blink;
    if (steer->cart.going && steer->blink) {
        cart_step(steer);
    }
    /* the heartbeat: on at every other draw that had a new result */
    (void)bento_ui_set_value(steer->ui, steer->screen.heartbeat,
                             (steer->blink && steer->has_top) ? 1 : 0);
    if (steer->episode.state == STATE_SEEN) {
        upper_text(steer->word, text, sizeof(text));
    } else {
        (void)snprintf(text, sizeof(text), "%s", "-");
    }
    put_text(steer->ui, &steer->screen.word, text);
    put_color(steer->ui, &steer->screen.word, COLOR_TEXT);
    (void)snprintf(text, sizeof(text),
                   steer->cart.going ? "Going %s, 1 cell/s"
                                     : "Stopped, facing %s",
                   DIRECTIONS[steer->cart.direction].word);
    put_text(steer->ui, &steer->screen.mode, text);
    if (steer->has_top) {
        draw_scores(steer);
    }
    steer->has_top = false;
}

/* A tap on Safe stop. */
static bool read_touch(steer_t *steer)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    bool tapped = false;
    uint32_t i;

    if (bento_ui_poll(steer->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U; /* a failed poll answers no events */
    }
    for (i = 0U; i < count; i++) {
        if ((events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) &&
            (events[i].id == steer->screen.safe.id)) {
            tapped = true;
        }
    }
    return tapped;
}

/* SW5: pause (the AI core stops) or resume (it starts again). */
static bento_status_t toggle_pause(steer_t *steer)
{
    bento_status_t status = BENTO_OK;

    steer->paused = !steer->paused;
    steer->cart.going = false;
    steer->has_votes = false;
    if (steer->paused) {
        status = stop_ai();
        put_text(steer->ui, &steer->screen.status,
                 "Paused - SW5 resumes");
        put_color(steer->ui, &steer->screen.status, COLOR_WARN);
    } else {
        status = start_model(steer, true);
    }
    return status;
}

/* The buttons and the touches: SW6 or Safe stop switches it. */
static bento_status_t read_controls(steer_t *steer)
{
    bool tapped;
    bento_status_t status =
        sample_button(steer->has_buttons, &steer->sw5);

    if (status == BENTO_OK) {
        status = sample_button(steer->has_buttons, &steer->sw6);
    }
    if (status == BENTO_OK) {
        tapped = button_clicked(&steer->sw6);
        tapped = read_touch(steer) || tapped;
        if (tapped) {
            steer->safe = !steer->safe;
            put_text(steer->ui, &steer->screen.safe,
                     steer->safe ? "Safe stop: ON" : "Safe stop: OFF");
        }
        if (button_clicked(&steer->sw5)) {
            status = toggle_pause(steer);
        }
    }
    return status;
}

/* One round of the main loop. */
static bento_status_t steer_round(steer_t *steer)
{
    bento_edgeai_result_t result;
    uint32_t now_ms;
    bento_status_t status = read_controls(steer);

    if ((status == BENTO_OK) && !steer->stopped && !steer->has_votes) {
        reset_votes(steer);
    }
    now_ms = bento_time_ticks_ms();
    if ((status == BENTO_OK) && !steer->stopped && !steer->paused &&
        (bento_time_ticks_diff(now_ms, steer->polled_ms) >= POLL_MS)) {
        steer->polled_ms = now_ms;
        if (read_result(steer, &result)) {
            take_result(steer, &result, now_ms);
        }
    }
    if ((status == BENTO_OK) && !steer->stopped) {
        if (bento_time_ticks_diff(now_ms, steer->drawn_ms) >= DRAW_MS) {
            redraw(steer, now_ms);
        }
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* run(): the thresholds, the first start, then the loop until Stop. */
static bento_status_t run_steer(steer_t *steer)
{
    bento_status_t status;

    read_thresholds(steer);
    find_stop_class(steer);
    show_legend(steer);
    steer->cart.x = START_CELL;
    steer->cart.y = START_CELL;
    steer->cart.direction = 0; /* "up" */
    steer->safe = true;
    status = start_model(steer, false);
    steer->drawn_ms = bento_time_ticks_ms();
    steer->polled_ms = steer->drawn_ms;
    while ((status == BENTO_OK) && !steer->stopped) {
        status = steer_round(steer);
    }
    return status;
}

/* The screen, the run, and the finally: the AI core is always released. */
static bento_status_t run_program(steer_t *steer)
{
    bento_status_t status = build_screen(steer);
    bento_status_t stop_status;

    if (status == BENTO_OK) {
        status = run_steer(steer);
        if (status == BENTO_E_CANCELLED) { /* the finally still runs */
            (void)bento_runtime_acknowledge_stop();
        }
        if (steer->stopped) {
            put_text(steer->ui, &steer->screen.status,
                     steer->stop_text);
            put_color(steer->ui, &steer->screen.status, COLOR_BAD);
            (void)printf("%s\n", steer->stop_text);
        }
        stop_status = stop_ai();
        status = (status == BENTO_E_CANCELLED) ? stop_status : status;
    }
    return status;
}

int main(void)
{
    steer_t *steer = &g_steer;
    bool found = false;
    bento_status_t status;

    (void)memset(steer, 0, sizeof(*steer));
    steer->ui = bento_runtime_get_ui();
    steer->has_buttons = bento_buttons_count() > 0U;
    steer->sw5.index = SW5_INDEX;
    steer->sw6.index = SW6_INDEX;
    find_model(&steer->model, &found);
    if (!found) {
        status = missing_model(steer->ui);
    } else {
        status = run_program(steer);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
