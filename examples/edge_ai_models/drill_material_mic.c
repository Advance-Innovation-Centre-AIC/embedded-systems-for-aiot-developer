/*
 * drill_material_mic.c -- สว่านอัจฉริยะ (Smart Drill)
 *
 * ฟังเสียงดอกสว่านด้วยโมเดล DrillMaterialMic จาก Edge AI Store
 * การ์ดซ้ายบอกว่ากำลังเจาะอะไร (air, plastic, wood) เมื่อคลาสนั้นอยู่
 * บนสุดเกินเกณฑ์ Confirm at นาน 500 ms การ์ดขวาบอกการเจาะทะลุ
 * (คลาส *_out สองในสามผลล่าสุด) โหมด Cut power ล็อกไว้จนกด ACK
 * โหมด Notify only ล้างตัวเองเมื่อเสียงหยุด
 * ปุ่ม SW6 (บน) หรือแตะ ACK = รับทราบ ปุ่ม SW5 (ล่าง) = หยุดพัก/ทำต่อ
 * สำหรับผู้สอนเท่านั้น: ยึดชิ้นงาน ใส่แว่นและที่ครอบหู
 *
 * Listens to the drill bit with the DrillMaterialMic model from the
 * Edge AI Store. The left card says what is being drilled (air,
 * plastic, wood) once that class stays on top at its Confirm at
 * threshold for 500 ms. The right card shows a breakthrough (a *_out
 * class in 2 of the last 3 results). Cut power latches until ACK;
 * Notify only clears itself when the sound ends. SW6 (upper) or a tap
 * on ACK acknowledges, SW5 (lower) pauses and resumes. Instructor
 * only: clamp the work, wear eye and ear protection.
 *
 * Twin of drill_material_mic.py
 * (embedded-systems-for-aiot-developer/examples/edge_ai_models/drill_material_mic.py).
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

enum { /* times, ms */
    REFRACT_MS = 1500,
    POLL_MS = 100,
    DRAW_MS = 500,
    DWELL_MS = 500,
    LOOP_MS = 20,
    LOAD_MS = 150,
    OP_POLL_MS = 20,
    FAIL_MS = 3000
};
enum { /* breakthrough = VOTE_HITS of the last VOTE_WINDOW results */
    VOTE_HITS = 2,
    VOTE_WINDOW = 3
};
enum { /* fallback thresholds, %: Hearing at, Confirm at, clear below */
    DEFAULT_ENTER = 40,
    DEFAULT_CONFIRM = 65,
    DEFAULT_EXIT = 30
};
enum { /* sizes */
    MAX_CLASSES = BENTO_EDGEAI_MAX_CLASSES,
    CHIP_COUNT = 3,
    NEED_COUNT = 5,
    TEXT_BYTES = 128,
    /* "HH:MM:SS"; at worst three int32_t, two colons and the NUL */
    CLOCK_BYTES = 36,
    NO_CLASS = -1,
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
    COLOR_INFO = 0x4A9EFF,
    COLOR_ALARM_BG = 0x3A1416
};
enum { /* episode states */
    STATE_NORMAL = 0,
    STATE_HEARD,
    STATE_UNACKED,
    STATE_ACKED,
    STATE_ENDED,
    STATE_SEEN,
    STATE_COUNT
};
enum { /* font sizes, px */
    SMALL_PX = 16,
    MEDIUM_PX = 20,
    LARGE_PX = 24,
    HUGE_PX = 28
};

/* The exact name, looked up every run. */
static const char MODEL_NAME[] = "DrillMaterialMic";
static const char NOT_SURE[] = "Not sure";
static const char *const CHIP_NAMES[CHIP_COUNT] = {
    "air", "plastic", "wood"}; /* wood_out = wood */

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
    shown_t verdict;
    shown_t chip_panel[CHIP_COUNT];
    shown_t chip_text[CHIP_COUNT];
    shown_t disc;
    shown_t through;
    shown_t count;
    shown_t last;
    shown_t acknowledge;
    shown_t cut;
    shown_t status;
} screen_t;

/* The last N scores; K of them at or above a level = confirmed. */
typedef struct {
    uint8_t scores[VOTE_WINDOW];
    uint32_t count;
} vote_t;

/* The breakthrough latch: NORMAL > HEARD > UNACKED > ACKED or ENDED. */
typedef struct {
    int32_t state;
    int32_t count;
    bool has_time;
    uint32_t time_ms;
} episode_t;

/* A class counts after DWELL_MS on top at Confirm at; -1 = Not sure. */
typedef struct {
    int32_t accepted;
    int32_t candidate;
    uint32_t since_ms;
    int32_t count;
    int32_t chip_of[MAX_CLASSES];
} dwell_t;

typedef struct {
    bento_ui_t *ui;
    bool has_buttons;
    board_button_t sw5;
    board_button_t sw6;
    model_t model;
    screen_t screen;
    bento_edgeai_thresholds_t thresholds;
    uint32_t outs[MAX_CLASSES]; /* the *_out classes */
    uint32_t out_count;
    vote_t vote;
    episode_t episode;
    dwell_t dwell;
    int32_t cut; /* 1 = Cut power, 0 = Notify only */
    bool paused;
    bool blink;
    bool fresh;
    bool has_sequence;
    uint32_t last_sequence;
    bool failing;
    uint32_t fail_start_ms;
    uint32_t polled_ms;
    uint32_t drawn_ms;
    bool stopped;
    char stop_text[TEXT_BYTES];
} drill_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern drill_t g_drill;
drill_t g_drill;

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
static void raise_stop(drill_t *drill, const char *text)
{
    drill->stopped = true;
    (void)snprintf(drill->stop_text, sizeof(drill->stop_text), "%s",
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

/* The heading, the subtitle and the "Now" card with its chips. */
static bento_status_t add_now_card(drill_t *drill)
{
    static const rect_t title_at = {12, 4, -1, -1};
    static const rect_t subtitle_at = {12, 38, -1, -1};
    static const rect_t card_at = {12, 64, 470, 272};
    static const widget_t heartbeat = {
        UI_WIDGET_LED, NULL, {756, 6, 26, 26}, COLOR_OK, 0, 0, 0};
    static const rect_t verdict_at = {28, 96, -1, -1};
    screen_t *screen = &drill->screen;
    bento_ui_id_t unused = 0U;
    bento_status_t status =
        add_label(drill->ui, "Smart Drill", title_at, COLOR_TEXT,
                  LARGE_PX, &unused);

    if (status == BENTO_OK) {
        status = add_label(drill->ui,
                           "Instructor only. Detects: air, plastic, "
                           "wood + breakthrough. Cannot: metal.",
                           subtitle_at, COLOR_DIM, SMALL_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(drill->ui, card_at, "Now");
    }
    if (status == BENTO_OK) {
        status = add_widget(drill->ui, &heartbeat, &screen->heartbeat);
    }
    if (status == BENTO_OK) {
        status = add_label(drill->ui, NOT_SURE, verdict_at, COLOR_DIM,
                           HUGE_PX, &screen->verdict.id);
    }
    return status;
}

/* One chip per material: a panel and its name. */
static bento_status_t add_chips(drill_t *drill)
{
    enum {
        CHIP_X = 28,
        CHIP_STEP = 148,
        CHIP_Y = 170,
        CHIP_W = 140,
        CHIP_H = 56,
        TEXT_Y = 186
    };
    enum {
        TEXT_INSET = 10,
        CHIP_RADIUS = 8
    };
    screen_t *screen = &drill->screen;
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < CHIP_COUNT) && (status == BENTO_OK); i++) {
        const int32_t x = CHIP_X + (CHIP_STEP * i);
        const widget_t panel = {
            UI_WIDGET_PANEL, NULL, {x, CHIP_Y, CHIP_W, CHIP_H},
            COLOR_CARD,      1,    COLOR_DIM,
            CHIP_RADIUS};
        const rect_t text_at = {x + TEXT_INSET, TEXT_Y, -1, -1};

        status =
            add_widget(drill->ui, &panel, &screen->chip_panel[i].id);
        if (status == BENTO_OK) {
            status =
                add_label(drill->ui, CHIP_NAMES[i], text_at, COLOR_DIM,
                          MEDIUM_PX, &screen->chip_text[i].id);
        }
    }
    return status;
}

/* The "Breakthrough" card: a round light, the count, ACK and Cut power. */
static bento_status_t add_through_card(drill_t *drill)
{
    static const rect_t card_at = {494, 64, 294, 272};
    static const widget_t disc = {
        UI_WIDGET_PANEL, NULL, {508, 100, 56, 56}, COLOR_DIM, 0,
        COLOR_DIM,       28};
    static const rect_t through_at = {576, 108, -1, -1};
    static const widget_t count = {
        UI_WIDGET_SEG7, "0", {508, 170, 120, 56}, COLOR_TEXT, 0, 0, 0};
    static const rect_t last_at = {640, 186, -1, -1};
    static const widget_t acknowledge = {UI_WIDGET_BUTTON,
                                         "ACK",
                                         {508, 266, 130, 60},
                                         COLOR_BUTTON,
                                         24,
                                         0,
                                         0};
    static const widget_t cut = {UI_WIDGET_BUTTON,
                                 "Cut power ON",
                                 {648, 266, 126, 60},
                                 COLOR_BUTTON,
                                 16,
                                 0,
                                 0};
    screen_t *screen = &drill->screen;
    bento_status_t status =
        add_card(drill->ui, card_at, "Breakthrough");

    if (status == BENTO_OK) {
        status = add_widget(drill->ui, &disc, &screen->disc.id);
    }
    if (status == BENTO_OK) {
        status = add_label(drill->ui, "not through", through_at,
                           COLOR_DIM, HUGE_PX, &screen->through.id);
    }
    if (status == BENTO_OK) {
        status = add_widget(drill->ui, &count, &screen->count.id);
    }
    if (status == BENTO_OK) {
        status = add_label(drill->ui, "last -", last_at, COLOR_DIM,
                           SMALL_PX, &screen->last.id);
    }
    if (status == BENTO_OK) {
        status = add_widget(drill->ui, &acknowledge,
                            &screen->acknowledge.id);
    }
    if (status == BENTO_OK) {
        status = add_widget(drill->ui, &cut, &screen->cut.id);
    }
    return status;
}

static bento_status_t build_screen(drill_t *drill)
{
    static const rect_t status_at = {12, 346, -1, -1};
    bento_status_t status =
        bento_ui_screen(drill->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_now_card(drill);
    }
    if (status == BENTO_OK) {
        status = add_chips(drill);
    }
    if (status == BENTO_OK) {
        status = add_through_card(drill);
    }
    if (status == BENTO_OK) {
        status = add_label(drill->ui, " ", status_at, COLOR_WARN,
                           SMALL_PX, &drill->screen.status.id);
    }
    if (status == BENTO_OK) {
        drain_events(drill->ui);
    }
    return status;
}

static bool has_label(const model_t *model, const char *name)
{
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < model->class_count) && !found; i++) {
        found = strcmp(model->labels[i], name) == 0;
    }
    return found;
}

/* odd(): a model whose classes are not the ones this example needs
 * gets a card, never a guess. */
static bento_status_t check_classes(drill_t *drill, bool *odd)
{
    static const char *const need[NEED_COUNT] = {
        "air", "plastic", "wood", "plastic_out", "wood_out"};
    char absent[TEXT_BYTES];
    char text[TEXT_BYTES];
    size_t used = 0U;
    int32_t i;
    bento_status_t status = BENTO_OK;

    *odd = false;
    absent[0] = '\0';
    for (i = 0; i < NEED_COUNT; i++) {
        if (!has_label(&drill->model, need[i])) {
            (void)snprintf(&absent[used], sizeof(absent) - used, "%s%s",
                           *odd ? ", " : "", need[i]);
            used = strlen(absent);
            *odd = true;
        }
    }
    if (*odd) {
        (void)snprintf(text, sizeof(text), "%s has no class\n%s",
                       MODEL_NAME, absent);
        status = missing(
            drill->ui, "Model classes differ from this example", text);
    }
    return status;
}

/* read_th(): per class Hearing at, Confirm at, clear below. */
static void read_thresholds(drill_t *drill)
{
    bento_edgeai_thresholds_t *thresholds = &drill->thresholds;
    uint32_t i;

    bento_edgeai_thresholds_init(thresholds);
    if (bento_edgeai_thresholds(drill->model.index, thresholds) !=
        BENTO_OK) {
        for (i = 0U; i < (uint32_t)MAX_CLASSES; i++) {
            thresholds->enter[i] = (uint8_t)DEFAULT_ENTER;
            thresholds->confirm[i] = (uint8_t)DEFAULT_CONFIRM;
            thresholds->exit[i] = (uint8_t)DEFAULT_EXIT;
        }
    }
}

/* The chip a class lights: its name before "_" among CHIP_NAMES. */
static int32_t chip_of_label(const char *label)
{
    size_t length = 0U;
    int32_t chip = NO_CLASS;
    int32_t i;

    while ((label[length] != '\0') && (label[length] != '_')) {
        length++;
    }

    for (i = 0; i < CHIP_COUNT; i++) {
        if ((strlen(CHIP_NAMES[i]) == length) &&
            (strncmp(CHIP_NAMES[i], label, length) == 0)) {
            chip = i;
        }
    }
    return chip;
}

/* The outs, the chips of each class and a fresh dwell. */
static void prepare_classes(drill_t *drill)
{
    const model_t *model = &drill->model;
    uint32_t i;

    drill->out_count = 0U;
    for (i = 0U; i < model->class_count; i++) {
        const size_t length = strlen(model->labels[i]);
        const size_t suffix = sizeof("_out") - 1U;

        if ((length >= suffix) &&
            (strcmp(&model->labels[i][length - suffix], "_out") == 0)) {
            drill->outs[drill->out_count] = i;
            drill->out_count++;
        }
        drill->dwell.chip_of[i] = chip_of_label(model->labels[i]);
    }
    drill->dwell.accepted = NO_CLASS;
    drill->dwell.candidate = NO_CLASS;
}

static const char *class_name(const drill_t *drill, int32_t klass)
{
    return (klass >= 0) ? drill->model.labels[klass] : NOT_SURE;
}

static int32_t chip_of(const drill_t *drill, int32_t klass)
{
    return (klass >= 0) ? drill->dwell.chip_of[klass] : NO_CLASS;
}

/* edge_ai.count() on resume: a changed list is the Stop "Model list
 * changed"; a failed read is the OSError. */
static bento_status_t check_list(drill_t *drill)
{
    int32_t count = 0;
    const bento_status_t link = bento_edgeai_count(&count);

    if (link != BENTO_OK) {
        (void)printf("select: %s\n", link_error_text(link));
    } else if (count != drill->model.row_count) {
        raise_stop(drill, "Model list changed - run again");
    } else {
        /* the same list */
    }
    return link;
}

/* edge_ai.select(i): loads and starts it, up to 15 s. *link is the
 * OSError, if any; the status is the IDE's Stop. */
static bento_status_t select_model(const drill_t *drill,
                                   bento_status_t *link)
{
    bento_status_t status = BENTO_OK;

    *link = bento_edgeai_select_start(drill->model.index);
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
static bento_status_t start_model(drill_t *drill, bool check_count)
{
    static const char help_buttons[] =
        "Listening - SW6 = ACK, SW5 = pause";
    static const char help_touch[] = "Listening";
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    bento_status_t link = BENTO_OK;

    (void)snprintf(text, sizeof(text), "Loading %s ...", MODEL_NAME);
    put_text(drill->ui, &drill->screen.status, text);
    put_color(drill->ui, &drill->screen.status, COLOR_WARN);
    drain_events(drill->ui);
    drill->failing = false;
    if (check_count) {
        link = check_list(drill);
    }
    if ((link == BENTO_OK) && !drill->stopped) {
        status = select_model(drill, &link);
    }
    if ((status == BENTO_OK) && !drill->stopped && (link != BENTO_OK)) {
        (void)snprintf(text, sizeof(text), "Cannot start %s",
                       MODEL_NAME);
        raise_stop(drill, text);
    }
    if ((status == BENTO_OK) && !drill->stopped) {
        put_text(drill->ui, &drill->screen.status,
                 drill->has_buttons ? help_buttons : help_touch);
        put_color(drill->ui, &drill->screen.status, COLOR_TEXT);
    }
    return status;
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

/* No confirm, or one inside the refractory time: the state follows
 * the sound. A clear ends an unacknowledged alarm (ENDED, which waits
 * for ACK) or goes back to NORMAL. */
static void episode_settle(episode_t *episode, bool heard, bool clear)
{
    const int32_t state = episode->state;

    if (clear && (state == STATE_UNACKED)) {
        episode->state = STATE_ENDED;
    } else if (clear && (state != STATE_ENDED)) {
        episode->state = STATE_NORMAL;
    } else if (!clear && heard && (state == STATE_NORMAL)) {
        episode->state = STATE_HEARD;
    } else {
        /* no change */
    }
}

/* True once per new event; the refractory time runs from the episode's
 * last confirm. */
static bool episode_step(episode_t *episode, bool heard, bool confirm,
                         bool clear, uint32_t now_ms)
{
    const bool latched = (episode->state == STATE_UNACKED) ||
                         (episode->state == STATE_ACKED);
    const bool rested =
        !episode->has_time ||
        (bento_time_ticks_diff(now_ms, episode->time_ms) >= REFRACT_MS);
    const bool fired = confirm && !latched && rested;

    if (fired) {
        episode->state = STATE_UNACKED;
        episode->count++;
        episode->has_time = true;
        episode->time_ms = now_ms;
    } else {
        if (confirm && latched) {
            episode->time_ms = now_ms;
        }
        episode_settle(episode, heard, clear);
    }
    return fired;
}

static void episode_acknowledge(episode_t *episode)
{
    if (episode->state == STATE_UNACKED) {
        episode->state = STATE_ACKED;
    } else if (episode->state == STATE_ENDED) {
        episode->state = STATE_NORMAL;
    } else {
        /* nothing to acknowledge */
    }
}

/* Dwell.step(): one console line when the accepted state changes. */
static void dwell_step(drill_t *drill, int32_t klass, uint8_t percent,
                       uint32_t now_ms)
{
    dwell_t *dwell = &drill->dwell;
    char clock[CLOCK_BYTES];

    if (klass != dwell->candidate) {
        dwell->candidate = klass;
        dwell->since_ms = now_ms;
    }
    if ((klass != dwell->accepted) &&
        (bento_time_ticks_diff(now_ms, dwell->since_ms) >= DWELL_MS)) {
        dwell->accepted = klass;
        clock_text(clock, sizeof(clock));
        (void)printf("%s %s %u%%", clock, class_name(drill, klass),
                     (unsigned int)percent);
        if (klass >= 0) {
            dwell->count++;
            (void)printf(" #%" PRId32, dwell->count);
        }
        (void)printf("\n");
    }
}

/* read_result(): only NEW results of this model; Stop after 3 s of
 * errors. */
static bool read_result(drill_t *drill, bento_edgeai_result_t *result)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    bool fresh = false;

    bento_edgeai_result_init(result);
    if (bento_edgeai_result(result) != BENTO_OK) {
        if (!drill->failing) {
            drill->failing = true;
            drill->fail_start_ms = now_ms;
        }
        if (bento_time_ticks_diff(now_ms, drill->fail_start_ms) >=
            FAIL_MS) {
            raise_stop(drill, "AI core not answering - run again");
        }
    } else {
        drill->failing = false;
        fresh = result->has_verdict &&
                (!drill->has_sequence ||
                 (result->seq != drill->last_sequence)) &&
                ((uint32_t)result->index == drill->model.index);
        if (fresh) {
            drill->has_sequence = true;
            drill->last_sequence = result->seq;
        }
    }
    return fresh;
}

/* The first class with the highest score. */
static uint32_t top_class(const bento_edgeai_result_t *result,
                          uint32_t classes)
{
    uint32_t top = 0U;
    uint32_t i;

    for (i = 1U; i < classes; i++) {
        if (result->score_pct[i] > result->score_pct[top]) {
            top = i;
        }
    }
    return top;
}

/* The strongest *_out class, with its own thresholds. */
static uint32_t strongest_out(const drill_t *drill,
                              const bento_edgeai_result_t *result)
{
    uint32_t out = drill->outs[0];
    uint32_t i;

    for (i = 0U; i < drill->out_count; i++) {
        if (result->score_pct[drill->outs[i]] >
            result->score_pct[out]) {
            out = drill->outs[i];
        }
    }
    return out;
}

/* A breakthrough: one console line, the count and the time. */
static void report_through(drill_t *drill, uint32_t out)
{
    /* Notify only (clears itself when the sound ends), Cut power
     * (latched until ACK) */
    static const char *const cut_words[2] = {"THROUGH", "POWER CUT"};
    char clock[CLOCK_BYTES];
    char text[TEXT_BYTES];

    clock_text(clock, sizeof(clock));
    (void)printf("%s %s %u%% %s #%" PRId32 "\n", clock,
                 drill->model.labels[out],
                 (unsigned int)vote_max(&drill->vote),
                 cut_words[drill->cut], drill->episode.count);
    (void)snprintf(text, sizeof(text), "%" PRId32,
                   drill->episode.count);
    put_text(drill->ui, &drill->screen.count, text);
    clock_text(clock, sizeof(clock));
    (void)snprintf(text, sizeof(text), "last %s", clock);
    put_text(drill->ui, &drill->screen.last, text);
}

/* One new result: the material, then the breakthrough vote. */
static void take_result(drill_t *drill,
                        const bento_edgeai_result_t *result,
                        uint32_t now_ms)
{
    const bento_edgeai_thresholds_t *thresholds = &drill->thresholds;
    const uint32_t classes =
        (result->class_count < drill->model.class_count)
            ? result->class_count
            : drill->model.class_count;
    uint32_t top;
    uint32_t out;
    uint8_t score;

    if ((classes > 0U) && (drill->out_count > 0U)) {
        drill->fresh = true;
        top = top_class(result, classes);
        score = result->score_pct[top];
        dwell_step(drill,
                   (score >= thresholds->confirm[top]) ? (int32_t)top
                                                       : NO_CLASS,
                   score, now_ms);
        out = strongest_out(drill, result);
        score = result->score_pct[out];
        vote_add(&drill->vote, score);
        if (episode_step(
                &drill->episode, score >= thresholds->enter[out],
                vote_hits(&drill->vote, thresholds->confirm[out]),
                vote_clear(&drill->vote, thresholds->exit[out]),
                now_ms)) {
            report_through(drill, out);
        }
    }
}

/* The chips: accepted white, candidate grey-blue. */
static void draw_chips(drill_t *drill)
{
    static const uint32_t panel_colors[3] = {COLOR_CARD, COLOR_TEXT,
                                             COLOR_BUTTON};
    static const uint32_t text_colors[3] = {COLOR_DIM, COLOR_CARD,
                                            COLOR_TEXT};
    const int32_t accepted = chip_of(drill, drill->dwell.accepted);
    const int32_t candidate = chip_of(drill, drill->dwell.candidate);
    int32_t i;

    for (i = 0; i < CHIP_COUNT; i++) {
        int32_t kind = 0;

        if (i == accepted) {
            kind = 1;
        } else if (i == candidate) {
            kind = 2;
        } else {
            /* neither */
        }
        put_color(drill->ui, &drill->screen.chip_panel[i],
                  panel_colors[kind]);
        put_color(drill->ui, &drill->screen.chip_text[i],
                  text_colors[kind]);
    }
}

/* state = the breakthrough latch. Cut power: UNACKED and ENDED flash
 * until ACK (an interlock). Notify only is acknowledged at once, so the
 * caller passes SEEN = "through now": white, clears itself. */
static void draw(drill_t *drill, int32_t state)
{
    static const char *const words[STATE_COUNT] = {
        "not through", "! near exit", "!! POWER CUT",
        "ACKED",       "ENDED",       "THROUGH"};
    static const uint32_t word_colors[STATE_COUNT] = {
        COLOR_DIM,  COLOR_WARN, COLOR_BAD,
        COLOR_TEXT, COLOR_TEXT, COLOR_TEXT};
    const uint32_t flash = drill->blink ? COLOR_BAD : COLOR_ALARM_BG;
    const uint32_t ended = drill->blink ? COLOR_DIM : COLOR_ALARM_BG;
    const uint32_t disc_colors[STATE_COUNT] = {
        COLOR_DIM, COLOR_WARN, flash, COLOR_BAD, ended, COLOR_TEXT};
    const bool alarm =
        (state == STATE_UNACKED) || (state == STATE_ENDED);
    screen_t *screen = &drill->screen;

    /* the heartbeat: on at every other draw that had a new result */
    (void)bento_ui_set_value(drill->ui, screen->heartbeat,
                             (drill->blink && drill->fresh) ? 1 : 0);
    put_text(drill->ui, &screen->verdict,
             class_name(drill, drill->dwell.accepted));
    put_color(drill->ui, &screen->verdict,
              (drill->dwell.accepted >= 0) ? COLOR_TEXT : COLOR_DIM);
    draw_chips(drill);
    put_color(drill->ui, &screen->disc, disc_colors[state]);
    put_text(drill->ui, &screen->through, words[state]);
    put_color(drill->ui, &screen->through, word_colors[state]);
    put_color(drill->ui, &screen->acknowledge,
              alarm ? COLOR_BAD : COLOR_BUTTON);
}

/* Touches: ACK, and the Cut power / Notify only switch. */
static bool read_touch(drill_t *drill)
{
    static const char *const cut_texts[2] = {"Notify only",
                                             "Cut power ON"};
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    bool acknowledged = false;
    uint32_t i;

    if (bento_ui_poll(drill->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U; /* a failed poll answers no events */
    }
    for (i = 0U; i < count; i++) {
        const bool clicked =
            events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED;

        if (clicked && (events[i].id == drill->screen.acknowledge.id)) {
            acknowledged = true;
        }
        if (clicked && (events[i].id == drill->screen.cut.id)) {
            drill->cut = 1 - drill->cut;
            put_text(drill->ui, &drill->screen.cut,
                     cut_texts[drill->cut]);
        }
    }
    return acknowledged;
}

/* SW5: pause (the AI core stops) or resume (it starts again). */
static bento_status_t toggle_pause(drill_t *drill)
{
    bento_status_t status = BENTO_OK;

    drill->paused = !drill->paused;
    (void)memset(&drill->vote, 0, sizeof(drill->vote));
    drill->dwell.accepted = NO_CLASS;
    drill->dwell.candidate = NO_CLASS;
    if (drill->paused) {
        status = stop_ai();
        put_text(drill->ui, &drill->screen.status,
                 "Paused - SW5 resumes");
        put_color(drill->ui, &drill->screen.status, COLOR_WARN);
    } else {
        status = start_model(drill, true);
    }
    return status;
}

/* The buttons and the touches; true when ACK was asked for. */
static bento_status_t read_controls(drill_t *drill, bool *acknowledged)
{
    bento_status_t status =
        sample_button(drill->has_buttons, &drill->sw5);

    if (status == BENTO_OK) {
        status = sample_button(drill->has_buttons, &drill->sw6);
    }
    if (status == BENTO_OK) {
        *acknowledged = button_clicked(&drill->sw6);
        *acknowledged = read_touch(drill) || *acknowledged;
        if (button_clicked(&drill->sw5)) {
            status = toggle_pause(drill);
        }
    }
    return status;
}

/* Every DRAW_MS: the blink, then the screen. */
static void redraw(drill_t *drill, uint32_t now_ms)
{
    const bool notify =
        (drill->episode.state == STATE_ACKED) && (drill->cut == 0);

    drill->drawn_ms = now_ms;
    drill->blink = !drill->blink;
    draw(drill, notify ? STATE_SEEN : drill->episode.state);
    drill->fresh = false;
}

/* One round of the main loop. */
static bento_status_t drill_round(drill_t *drill)
{
    bento_edgeai_result_t result;
    bool acknowledged = false;
    uint32_t now_ms;
    bento_status_t status = read_controls(drill, &acknowledged);

    now_ms = bento_time_ticks_ms();
    if ((status == BENTO_OK) && !drill->stopped && !drill->paused &&
        (bento_time_ticks_diff(now_ms, drill->polled_ms) >= POLL_MS)) {
        drill->polled_ms = now_ms;
        if (read_result(drill, &result)) {
            take_result(drill, &result, now_ms);
        }
    }
    if ((status == BENTO_OK) && !drill->stopped) {
        /* Notify only: nothing to acknowledge */
        if (acknowledged || (drill->cut == 0)) {
            episode_acknowledge(&drill->episode);
        }
        if (bento_time_ticks_diff(now_ms, drill->drawn_ms) >= DRAW_MS) {
            redraw(drill, now_ms);
        }
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* run(): the classes, the first start, then the loop until Stop. */
static bento_status_t run_drill(drill_t *drill)
{
    bool odd = false;
    bento_status_t status = check_classes(drill, &odd);

    if ((status == BENTO_OK) && !odd) {
        read_thresholds(drill);
        prepare_classes(drill);
        drill->cut = 1;
        status = start_model(drill, false);
        drill->drawn_ms = bento_time_ticks_ms();
        drill->polled_ms = drill->drawn_ms;
        while ((status == BENTO_OK) && !drill->stopped) {
            status = drill_round(drill);
        }
    }
    return status;
}

/* The screen, the run, and the finally: the AI core is always released. */
static bento_status_t run_program(drill_t *drill)
{
    bento_status_t status = build_screen(drill);
    bento_status_t stop_status;

    if (status == BENTO_OK) {
        status = run_drill(drill);
        if (status == BENTO_E_CANCELLED) { /* the finally still runs */
            (void)bento_runtime_acknowledge_stop();
        }
        if (drill->stopped) {
            put_text(drill->ui, &drill->screen.status,
                     drill->stop_text);
            put_color(drill->ui, &drill->screen.status, COLOR_BAD);
            (void)printf("%s\n", drill->stop_text);
        }
        stop_status = stop_ai();
        status = (status == BENTO_E_CANCELLED) ? stop_status : status;
    }
    return status;
}

int main(void)
{
    drill_t *drill = &g_drill;
    bool found = false;
    bento_status_t status;

    (void)memset(drill, 0, sizeof(*drill));
    drill->ui = bento_runtime_get_ui();
    drill->has_buttons = bento_buttons_count() > 0U;
    drill->sw5.index = SW5_INDEX;
    drill->sw6.index = SW6_INDEX;
    find_model(&drill->model, &found);
    if (!found) {
        status = missing_model(drill->ui);
    } else {
        status = run_program(drill);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
