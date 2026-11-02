/*
 * yes_no.c -- ยืนยันด้วยเสียง (Voice Confirm)
 *
 * ถามด้วยเสียงด้วยโมเดล "Yes / No" (ทดลอง) จาก Edge AI Store แตะ Ask
 * (หรือปุ่ม SW6 บน) แล้วพูด yes หรือ no ภายใน 5 วินาที คำว่า yes เปิดปั๊ม
 * คำว่า no ปิดปั๊ม คำที่ได้ยินตอนไม่มีใครถามจะถูกนับว่าไม่สนใจ การ์ดขวา
 * แสดงคะแนนของสามคลาสแรกพร้อมแถบ และบันทึกสามคำล่าสุด
 * ปุ่ม SW5 (ล่าง) = หยุดพัก/ทำต่อ
 *
 * Asks by voice with the "Yes / No" (experimental) model from the Edge
 * AI Store. Tap Ask (or SW6, upper), then say yes or no within 5 s:
 * yes switches the pump on, no switches it off. A word heard when
 * nobody asked is counted as ignored. The right card shows the scores
 * of the first three classes with bars and the last three words. SW5
 * (lower) pauses and resumes.
 *
 * Twin of yes_no.py
 * (embedded-systems-for-aiot-developer/examples/edge_ai_models/yes_no.py).
 * Style: CLEAN_C.md. Written new; devkit-21-ai-model-lab gave the style.
 * Differs from the original:
 *   - In the BENTO Emulator, Edge AI/MQTT from C is not available
 *     yet: the program says so and then shows the "Model not on
 *     this board" card, as the .py does when the model is missing.
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
    ASK_MS = 5000,     /* the answer window after Ask */
    POLL_MS = 100,
    DRAW_MS = 500,
    LOOP_MS = 20,
    LOAD_MS = 150,
    OP_POLL_MS = 20,
    FAIL_MS = 3000,
    MS_PER_SECOND = 1000
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
enum { /* sizes */
    MAX_CLASSES = BENTO_EDGEAI_MAX_CLASSES,
    SCORE_ROWS = 3,
    LOG_LINES = 3,
    TEXT_BYTES = 128,
    /* "HH:MM:SS"; at worst three int32_t, two colons and the NUL */
    CLOCK_BYTES = 36,
    SW5_INDEX = 1,
    SW6_INDEX = 0,
    BAR_MAX = 100
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
/* What the BENTO Emulator says: 119 bytes, within the 126 one
 * label takes; the label gets a width so it wraps. */
static const char EMULATOR_TEXT[] =
    "Edge AI จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";
static const char MODEL_NAME[] = "Yes / No";

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
    shown_t question;
    shown_t word;
    shown_t pump;
    shown_t countdown;
    shown_t ignored;
    shown_t ask;
    shown_t score_text[SCORE_ROWS];
    shown_t bar[SCORE_ROWS];
    bool has_bar_value[SCORE_ROWS];
    int32_t bar_value[SCORE_ROWS];
    shown_t legend;
    shown_t log;
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

typedef struct {
    bento_ui_t *ui;
    bool has_buttons;
    board_button_t sw5;
    board_button_t sw6;
    model_t model;
    screen_t screen;
    bento_edgeai_thresholds_t thresholds;
    episode_t episode;
    vote_t votes[MAX_CLASSES];
    bool has_votes; /* the .py's vs != 0 */
    uint32_t held;  /* a word that counted and still sounds */
    char word[BENTO_EDGEAI_LABEL_LEN];
    bool has_top;
    uint8_t top[MAX_CLASSES];
    uint32_t top_count;
    bool asking;
    uint32_t ask_ms;
    bool pump_on;
    char note[TEXT_BYTES];
    int32_t answered;
    int32_t ignored;
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
} voice_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern voice_t g_voice;
voice_t g_voice;

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
static void raise_stop(voice_t *voice, const char *text)
{
    voice->stopped = true;
    (void)snprintf(voice->stop_text, sizeof(voice->stop_text), "%s",
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
static void find_model(model_t *model, bool *found, bool *emulator)
{
    bool supported = false;
    int32_t rows = 0;
    const bento_status_t link = bento_edgeai_supported(&supported);
    bento_status_t status;

    *found = false;
    *emulator = link == BENTO_E_UNSUPPORTED;
    if ((link == BENTO_OK) && supported) {
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

/* The heading, the subtitle and the "Answer" card. */
static bento_status_t add_answer_card(voice_t *voice)
{
    static const rect_t title_at = {12, 4, -1, -1};
    static const widget_t heartbeat = {
        UI_WIDGET_LED, NULL, {756, 6, 26, 26}, COLOR_OK, 0, 0, 0};
    static const rect_t subtitle_at = {12, 38, -1, -1};
    static const rect_t card_at = {12, 64, 470, 272};
    static const rect_t question_at = {28, 96, -1, -1};
    static const widget_t frame = {
        UI_WIDGET_PANEL, NULL, {28, 136, 200, 110}, COLOR_CARD, 1,
        COLOR_DIM,       12};
    screen_t *screen = &voice->screen;
    bento_ui_id_t unused = 0U;
    bento_status_t status =
        add_label(voice->ui, "Voice Confirm", title_at, COLOR_TEXT,
                  LARGE_PX, &unused);

    if (status == BENTO_OK) {
        status = add_widget(voice->ui, &heartbeat, &screen->heartbeat);
    }
    if (status == BENTO_OK) {
        status = add_label(voice->ui,
                           "Experimental. Detects English 'yes' and "
                           "'no'. Cannot: other words or Thai.",
                           subtitle_at, COLOR_DIM, SMALL_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(voice->ui, card_at, "Answer");
    }
    if (status == BENTO_OK) {
        status = add_label(voice->ui, "Start the pump?", question_at,
                           COLOR_DIM, LARGE_PX, &screen->question.id);
    }
    if (status == BENTO_OK) {
        status = add_widget(voice->ui, &frame, &unused);
    }
    return status;
}

/* The word, the pump, the countdown, the ignored count and Ask. */
static bento_status_t add_answer_parts(voice_t *voice)
{
    static const rect_t word_at = {48, 176, -1, -1};
    static const rect_t pump_at = {244, 140, -1, -1};
    static const rect_t countdown_at = {244, 172, -1, -1};
    static const rect_t ignored_at = {244, 200, -1, -1};
    static const widget_t ask = {UI_WIDGET_BUTTON,
                                 "Ask",
                                 {28, 262, 200, 64},
                                 COLOR_BUTTON,
                                 24,
                                 0,
                                 0};
    screen_t *screen = &voice->screen;
    bento_status_t status = add_label(
        voice->ui, "-", word_at, COLOR_DIM, HUGE_PX, &screen->word.id);

    if (status == BENTO_OK) {
        status = add_label(voice->ui, "Pump: OFF", pump_at, COLOR_DIM,
                           MEDIUM_PX, &screen->pump.id);
    }
    if (status == BENTO_OK) {
        status = add_label(voice->ui, " ", countdown_at, COLOR_DIM,
                           SMALL_PX, &screen->countdown.id);
    }
    if (status == BENTO_OK) {
        status = add_label(voice->ui, "ignored 0", ignored_at,
                           COLOR_DIM, SMALL_PX, &screen->ignored.id);
    }
    if (status == BENTO_OK) {
        status = add_widget(voice->ui, &ask, &screen->ask.id);
    }
    return status;
}

/* The "Scores" card: one name and one bar per row. */
static bento_status_t add_scores_card(voice_t *voice)
{
    enum {
        ROW_X = 508,
        TEXT_Y = 96,
        BAR_Y = 118,
        ROW_STEP = 44
    };
    enum {
        BAR_W = 266,
        BAR_H = 12
    };
    static const rect_t card_at = {494, 64, 294, 272};
    static const rect_t legend_at = {508, 230, -1, -1};
    static const rect_t log_at = {508, 256, -1, -1};
    screen_t *screen = &voice->screen;
    bento_status_t status = add_card(voice->ui, card_at, "Scores");
    int32_t i;

    for (i = 0; (i < SCORE_ROWS) && (status == BENTO_OK); i++) {
        const rect_t text_at = {ROW_X, TEXT_Y + (ROW_STEP * i), -1, -1};

        status = add_label(voice->ui, " ", text_at, COLOR_DIM, SMALL_PX,
                           &screen->score_text[i].id);
    }
    for (i = 0; (i < SCORE_ROWS) && (status == BENTO_OK); i++) {
        const widget_t bar = {
            UI_WIDGET_BAR,
            NULL,
            {ROW_X, BAR_Y + (ROW_STEP * i), BAR_W, BAR_H},
            0U,
            0,
            0,
            BAR_MAX};

        status = add_widget(voice->ui, &bar, &screen->bar[i].id);
    }
    if (status == BENTO_OK) {
        status = add_label(voice->ui, " ", legend_at, COLOR_DIM,
                           SMALL_PX, &screen->legend.id);
    }
    if (status == BENTO_OK) {
        status = add_label(voice->ui, " ", log_at, COLOR_TEXT, SMALL_PX,
                           &screen->log.id);
    }
    return status;
}

static bento_status_t build_screen(voice_t *voice)
{
    static const rect_t status_at = {12, 346, -1, -1};
    bento_status_t status =
        bento_ui_screen(voice->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_answer_card(voice);
    }
    if (status == BENTO_OK) {
        status = add_answer_parts(voice);
    }
    if (status == BENTO_OK) {
        status = add_scores_card(voice);
    }
    if (status == BENTO_OK) {
        status = add_label(voice->ui, " ", status_at, COLOR_WARN,
                           SMALL_PX, &voice->screen.status.id);
    }
    if (status == BENTO_OK) {
        drain_events(voice->ui);
    }
    return status;
}

/* read_th(): per class Hearing at, Confirm at, clear below. */
static void read_thresholds(voice_t *voice)
{
    bento_edgeai_thresholds_t *thresholds = &voice->thresholds;
    uint32_t i;

    bento_edgeai_thresholds_init(thresholds);
    if (bento_edgeai_thresholds(voice->model.index, thresholds) !=
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
static bento_status_t check_list(voice_t *voice)
{
    int32_t count = 0;
    const bento_status_t link = bento_edgeai_count(&count);

    if (link != BENTO_OK) {
        (void)printf("select: %s\n", link_error_text(link));
    } else if (count != voice->model.row_count) {
        raise_stop(voice, "Model list changed - run again");
    } else {
        /* the same list */
    }
    return link;
}

/* edge_ai.select(i): loads and starts it, up to 15 s. *link is the
 * OSError, if any; the status is the IDE's Stop. */
static bento_status_t select_model(const voice_t *voice,
                                   bento_status_t *link)
{
    bento_status_t status = BENTO_OK;

    *link = bento_edgeai_select_start(voice->model.index);
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
static bento_status_t start_model(voice_t *voice, bool check_count)
{
    static const char help_buttons[] =
        "Tap Ask or SW6, then say yes or no - SW5 = pause";
    static const char help_touch[] = "Tap Ask, then say yes or no";
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    bento_status_t link = BENTO_OK;

    (void)snprintf(text, sizeof(text), "Loading %s ...", MODEL_NAME);
    put_text(voice->ui, &voice->screen.status, text);
    put_color(voice->ui, &voice->screen.status, COLOR_WARN);
    drain_events(voice->ui);
    voice->failing = false;
    if (check_count) {
        link = check_list(voice);
    }
    if ((link == BENTO_OK) && !voice->stopped) {
        status = select_model(voice, &link);
    }
    if ((status == BENTO_OK) && !voice->stopped && (link != BENTO_OK)) {
        (void)snprintf(text, sizeof(text), "Cannot start %s",
                       MODEL_NAME);
        raise_stop(voice, text);
    }
    if ((status == BENTO_OK) && !voice->stopped) {
        put_text(voice->ui, &voice->screen.status,
                 voice->has_buttons ? help_buttons : help_touch);
        put_color(voice->ui, &voice->screen.status, COLOR_TEXT);
    }
    return status;
}

/* read_result(): only NEW results of this model; Stop after 3 s of
 * errors. */
static bool read_result(voice_t *voice, bento_edgeai_result_t *result)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    bool fresh = false;

    bento_edgeai_result_init(result);
    if (bento_edgeai_result(result) != BENTO_OK) {
        if (!voice->failing) {
            voice->failing = true;
            voice->fail_start_ms = now_ms;
        }
        if (bento_time_ticks_diff(now_ms, voice->fail_start_ms) >=
            FAIL_MS) {
            raise_stop(voice, "AI core not answering - run again");
        }
    } else {
        voice->failing = false;
        fresh = result->has_verdict &&
                (!voice->has_sequence ||
                 (result->seq != voice->last_sequence)) &&
                ((uint32_t)result->index == voice->model.index);
        if (fresh) {
            voice->has_sequence = true;
            voice->last_sequence = result->seq;
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
static void event_of(const voice_t *voice, uint32_t classes,
                     event_t *event)
{
    const bento_edgeai_thresholds_t *thresholds = &voice->thresholds;
    uint32_t strongest = 0U;
    uint32_t i;

    for (i = 1U; i < classes; i++) {
        if ((voice->top[i] > voice->top[strongest]) ||
            (strongest == 0U)) {
            strongest = i;
        }
    }
    event->klass = strongest;
    event->percent = voice->top[strongest];
    event->enter = thresholds->enter[strongest];
    event->confirm = thresholds->confirm[strongest];
    event->exit = thresholds->exit[strongest];
}

/* The legend under the scores: class 1's thresholds. */
static void show_legend(voice_t *voice)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "Hearing at %u%% | Confirm at %u%%",
                   (unsigned int)voice->thresholds.enter[1],
                   (unsigned int)voice->thresholds.confirm[1]);
    put_text(voice->ui, &voice->screen.legend, text);
}

/* Fresh votes: the last N scores of each class. */
static void reset_votes(voice_t *voice)
{
    (void)memset(voice->votes, 0, sizeof(voice->votes));
    voice->has_votes = true;
    voice->held = 0U;
    voice->episode.state = STATE_NORMAL;
}

/* all(v.clear(level) for v in vs[1:]) */
static bool all_clear(const voice_t *voice, uint8_t level)
{
    bool clear = true;
    uint32_t i;

    for (i = 1U; i < voice->model.class_count; i++) {
        clear = clear && vote_clear(&voice->votes[i], level);
    }
    return clear;
}

/* The newest line first; three lines are kept. */
static void add_log_line(voice_t *voice, const char *line)
{
    char joined[TEXT_BYTES];
    size_t used = 0U;
    uint32_t i;

    for (i = LOG_LINES - 1U; i > 0U; i--) {
        (void)memcpy(voice->log[i], voice->log[i - 1U],
                     sizeof(voice->log[i]));
    }
    (void)snprintf(voice->log[0], sizeof(voice->log[0]), "%s", line);
    if (voice->log_count < (uint32_t)LOG_LINES) {
        voice->log_count++;
    }
    (void)printf("%s\n", line);
    joined[0] = '\0';
    for (i = 0U; i < voice->log_count; i++) {
        (void)snprintf(&joined[used], sizeof(joined) - used, "%s%s",
                       (i > 0U) ? "\n" : "", voice->log[i]);
        used = strlen(joined);
    }
    put_text(voice->ui, &voice->screen.log, joined);
}

/* A new word: an answer when somebody asked, else ignored. */
static void count_word(voice_t *voice, uint32_t klass)
{
    char clock[CLOCK_BYTES];
    char line[TEXT_BYTES];
    const size_t cap = sizeof(line);
    size_t used;

    voice->held = klass;
    clock_text(clock, sizeof(clock));
    (void)snprintf(line, cap, "%s %s %u%% ", clock, voice->word,
                   (unsigned int)vote_max(&voice->votes[klass]));
    used = strlen(line);
    if (!voice->asking) { /* nobody asked: the word changes nothing */
        voice->ignored++;
        (void)snprintf(&line[used], cap - used, "ignored %" PRId32,
                       voice->ignored);
    } else {
        voice->answered++;
        voice->asking = false;
        voice->pump_on = strcmp(voice->word, "yes") == 0;
        (void)snprintf(voice->note, sizeof(voice->note), "you said %s",
                       voice->word);
        (void)snprintf(&line[used], cap - used, "#%" PRId32,
                       voice->answered);
    }
    add_log_line(voice, line);
}

/* A word = K of its last N at Confirm at, once per utterance. */
static void take_result(voice_t *voice,
                        const bento_edgeai_result_t *result,
                        uint32_t now_ms)
{
    const uint32_t classes =
        (result->class_count < voice->model.class_count)
            ? result->class_count
            : voice->model.class_count;
    const uint32_t held = voice->held;
    event_t event;
    bool fired;
    uint32_t i;

    if (classes > 0U) {
        voice->has_top = true;
        voice->top_count = classes;
        for (i = 0U; i < classes; i++) {
            voice->top[i] = result->score_pct[i];
            vote_add(&voice->votes[i], voice->top[i]);
        }
        event_of(voice, classes, &event);
        if ((held != 0U) && !vote_hits(&voice->votes[held],
                                       voice->thresholds.exit[held])) {
            voice->held = 0U; /* that word ended: it may count again */
        }
        fired = episode_step(
            &voice->episode, event.percent >= event.enter,
            (event.klass != voice->held) &&
                vote_hits(&voice->votes[event.klass], event.confirm),
            all_clear(voice, event.exit), now_ms);
        if (fired || (voice->episode.state != STATE_SEEN)) {
            (void)snprintf(voice->word, sizeof(voice->word), "%s",
                           voice->model.labels[event.klass]);
        }
        if (fired) {
            count_word(voice, event.klass);
        }
    }
}

/* One row per class; white = at Confirm at. */
static void draw_bars(voice_t *voice)
{
    char text[TEXT_BYTES];
    uint32_t i;

    for (i = 0U; (i < voice->top_count) && (i < (uint32_t)SCORE_ROWS);
         i++) {
        const uint8_t percent = voice->top[i];
        const bool confirmed =
            (i > 0U) && (percent >= voice->thresholds.confirm[i]);

        (void)snprintf(text, sizeof(text), "%s %u%%",
                       voice->model.labels[i], (unsigned int)percent);
        put_text(voice->ui, &voice->screen.score_text[i], text);
        /* a Bar needs .color() */
        put_color(voice->ui, &voice->screen.bar[i],
                  confirmed ? COLOR_TEXT : COLOR_DIM);
        if (!voice->screen.has_bar_value[i] ||
            (voice->screen.bar_value[i] != (int32_t)percent)) {
            voice->screen.has_bar_value[i] = true;
            voice->screen.bar_value[i] = (int32_t)percent;
            (void)bento_ui_set_value(voice->ui, voice->screen.bar[i].id,
                                     (int32_t)percent);
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

/* wd.upper(), or "maybe " + wd while it is only heard, else "-". */
static void draw_word(voice_t *voice)
{
    char text[TEXT_BYTES];
    const int32_t state = voice->episode.state;

    if (state == STATE_SEEN) {
        upper_text(voice->word, text, sizeof(text));
    } else if (state == STATE_HEARD) {
        (void)snprintf(text, sizeof(text), "maybe %s", voice->word);
    } else {
        (void)snprintf(text, sizeof(text), "%s", "-");
    }
    put_text(voice->ui, &voice->screen.word, text);
    put_color(voice->ui, &voice->screen.word,
              (state == STATE_SEEN) ? COLOR_TEXT : COLOR_DIM);
}

/* The question, the countdown, the pump and the ignored count. */
static void draw_answer(voice_t *voice, uint32_t now_ms)
{
    screen_t *screen = &voice->screen;
    char text[TEXT_BYTES];
    int32_t left_s = 0; /* seconds left, rounded up */

    if (voice->asking) {
        left_s =
            (ASK_MS - bento_time_ticks_diff(now_ms, voice->ask_ms) +
             (MS_PER_SECOND - 1)) /
            MS_PER_SECOND;
    }
    put_color(voice->ui, &screen->question,
              (left_s != 0) ? COLOR_TEXT : COLOR_DIM);
    if (left_s != 0) {
        (void)snprintf(text, sizeof(text), "answer in %" PRId32 " s",
                       left_s);
    } else {
        (void)snprintf(text, sizeof(text), "%s", voice->note);
    }
    put_text(voice->ui, &screen->countdown, text);
    put_color(voice->ui, &screen->countdown,
              (left_s != 0) ? COLOR_TEXT : COLOR_DIM);
    put_text(voice->ui, &screen->pump,
             voice->pump_on ? "Pump: ON" : "Pump: OFF");
    put_color(voice->ui, &screen->pump,
              voice->pump_on ? COLOR_TEXT : COLOR_DIM);
    (void)snprintf(text, sizeof(text), "ignored %" PRId32,
                   voice->ignored);
    put_text(voice->ui, &screen->ignored, text);
}

/* Every DRAW_MS. */
static void redraw(voice_t *voice, uint32_t now_ms)
{
    voice->drawn_ms = now_ms;
    voice->blink = !voice->blink;
    draw_answer(voice, now_ms);
    /* the heartbeat: on at every other draw that had a new result */
    (void)bento_ui_set_value(voice->ui, voice->screen.heartbeat,
                             (voice->blink && voice->has_top) ? 1 : 0);
    draw_word(voice);
    if (voice->has_top) {
        draw_bars(voice);
    }
    voice->has_top = false;
}

/* A tap on Ask. */
static bool read_touch(voice_t *voice)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    bool tapped = false;
    uint32_t i;

    if (bento_ui_poll(voice->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U; /* a failed poll answers no events */
    }
    for (i = 0U; i < count; i++) {
        if ((events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) &&
            (events[i].id == voice->screen.ask.id)) {
            tapped = true;
        }
    }
    return tapped;
}

/* SW5: pause (the AI core stops) or resume (it starts again). */
static bento_status_t toggle_pause(voice_t *voice)
{
    bento_status_t status = BENTO_OK;

    voice->paused = !voice->paused;
    voice->asking = false;
    voice->has_votes = false;
    if (voice->paused) {
        status = stop_ai();
        put_text(voice->ui, &voice->screen.status,
                 "Paused - SW5 resumes");
        put_color(voice->ui, &voice->screen.status, COLOR_WARN);
    } else {
        status = start_model(voice, true);
    }
    return status;
}

/* The buttons and the touches: SW6 or Ask opens the answer window,
 * which closes by itself after ASK_MS. */
static bento_status_t read_controls(voice_t *voice, uint32_t *now_ms)
{
    bool asked;
    bento_status_t status =
        sample_button(voice->has_buttons, &voice->sw5);

    if (status == BENTO_OK) {
        status = sample_button(voice->has_buttons, &voice->sw6);
    }
    if (status == BENTO_OK) {
        asked = button_clicked(&voice->sw6);
        asked = read_touch(voice) || asked;
        *now_ms = bento_time_ticks_ms();
        if (asked) {
            voice->asking = true;
            voice->ask_ms = *now_ms;
        }
        if (voice->asking &&
            (bento_time_ticks_diff(*now_ms, voice->ask_ms) >= ASK_MS)) {
            voice->asking = false;
            (void)snprintf(voice->note, sizeof(voice->note), "%s",
                           "no answer - pump unchanged");
        }
        if (button_clicked(&voice->sw5)) {
            status = toggle_pause(voice);
        }
    }
    return status;
}

/* One round of the main loop. */
static bento_status_t voice_round(voice_t *voice)
{
    bento_edgeai_result_t result;
    uint32_t now_ms = 0U;
    bento_status_t status = read_controls(voice, &now_ms);

    if ((status == BENTO_OK) && !voice->stopped && !voice->has_votes) {
        reset_votes(voice);
    }
    if ((status == BENTO_OK) && !voice->stopped && !voice->paused &&
        (bento_time_ticks_diff(now_ms, voice->polled_ms) >= POLL_MS)) {
        voice->polled_ms = now_ms;
        if (read_result(voice, &result)) {
            take_result(voice, &result, now_ms);
        }
    }
    if ((status == BENTO_OK) && !voice->stopped) {
        if (bento_time_ticks_diff(now_ms, voice->drawn_ms) >= DRAW_MS) {
            redraw(voice, now_ms);
        }
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* run(): the thresholds, the first start, then the loop until Stop. */
static bento_status_t run_voice(voice_t *voice)
{
    bento_status_t status;

    read_thresholds(voice);
    show_legend(voice);
    (void)snprintf(voice->note, sizeof(voice->note), "%s", " ");
    status = start_model(voice, false);
    voice->drawn_ms = bento_time_ticks_ms();
    voice->polled_ms = voice->drawn_ms;
    while ((status == BENTO_OK) && !voice->stopped) {
        status = voice_round(voice);
    }
    return status;
}

/* The screen, the run, and the finally: the AI core is always released. */
static bento_status_t run_program(voice_t *voice)
{
    bento_status_t status = build_screen(voice);
    bento_status_t stop_status;

    if (status == BENTO_OK) {
        status = run_voice(voice);
        if (status == BENTO_E_CANCELLED) { /* the finally still runs */
            (void)bento_runtime_acknowledge_stop();
        }
        if (voice->stopped) {
            put_text(voice->ui, &voice->screen.status,
                     voice->stop_text);
            put_color(voice->ui, &voice->screen.status, COLOR_BAD);
            (void)printf("%s\n", voice->stop_text);
        }
        stop_status = stop_ai();
        status = (status == BENTO_E_CANCELLED) ? stop_status : status;
    }
    return status;
}

/* The BENTO Emulator has no Edge AI from C yet: say so on the console
 * now, and under the missing card once it is drawn. */
static void say_no_emulator_ai(void)
{
    (void)printf(
        "%s / Edge AI from C is not in the BENTO Emulator yet: "
        "run it on a Dev Kit or Eva Kit.\n",
        EMULATOR_TEXT);
}

static bento_status_t show_no_emulator_ai(bento_ui_t *ui)
{
    static const rect_t text_at = {96, 300, 600, -1};
    bento_ui_id_t unused = 0U;

    return add_label(ui, EMULATOR_TEXT, text_at, COLOR_BAD, MEDIUM_PX,
                     &unused);
}

int main(void)
{
    voice_t *voice = &g_voice;
    bool found = false;
    bool emulator = false;
    bento_status_t status;

    (void)memset(voice, 0, sizeof(*voice));
    voice->ui = bento_runtime_get_ui();
    voice->has_buttons = bento_buttons_count() > 0U;
    voice->sw5.index = SW5_INDEX;
    voice->sw6.index = SW6_INDEX;
    find_model(&voice->model, &found, &emulator);
    if (!found) {
        if (emulator) {
            say_no_emulator_ai();
        }
        status = missing_model(voice->ui);
        if ((status == BENTO_OK) && emulator) {
            status = show_no_emulator_ai(voice->ui);
        }
    } else {
        status = run_program(voice);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
