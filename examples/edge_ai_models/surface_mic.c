/*
 * surface_mic.c -- สมองหุ่นยนต์ดูดฝุ่น (Robot Vacuum Brain)
 *
 * ฟังเสียงเครื่องดูดฝุ่นด้วยโมเดล SurfaceMic จาก Edge AI Store แล้วแยก
 * ว่าหัวดูดอยู่บนอะไร (Air, Floor, Carpet) คลาสต้องอยู่บนสุดเกินเกณฑ์
 * Confirm at นาน 1 วินาทีก่อนหุ่นยนต์จะเปลี่ยนสิ่งที่ทำ การ์ดขวาบอกว่า
 * หุ่นยนต์ทำอะไร: ดับมอเตอร์ ดูดปกติ หรือ BOOST และยกผ้าถูพื้นบนพรม
 * เปิดเครื่องดูดฝุ่นใกล้บอร์ด ปุ่ม SW6 (บน) หรือแตะปุ่ม Mop = เปิด/ปิด
 * ผ้าถู ปุ่ม SW5 (ล่าง) = หยุดพัก/ทำต่อ
 *
 * Listens to a vacuum cleaner with the SurfaceMic model from the Edge
 * AI Store and tells what the nozzle is over (Air, Floor, Carpet). A
 * class must stay on top at its Confirm at threshold for 1 s before
 * the robot changes what it does. The right card shows what the robot
 * does: motor off, normal or BOOST, and the mop lifted on carpet. Run
 * a vacuum near the board. SW6 (upper) or a tap on Mop switches the
 * mop on and off, SW5 (lower) pauses and resumes.
 *
 * Twin of surface_mic.py
 * (embedded-systems-for-aiot-developer/examples/edge_ai_models/surface_mic.py).
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
 *     The dwell time "%.1f" is written by bento_fmt as the board's
 *     MicroPython writes it.
 *   - K, N, REFRACT_MS and HOLD_MS are left out: the .py does not use
 *     them in this example.
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
static const char MODEL[] = "SurfaceMic";
/* false = deployed from the Store */
static const bool BUILTIN = false;
/* Hearing at, Confirm at, clear below (fallback), % */
static const uint8_t DEF_THRESHOLDS[3] = {40U, 65U, 30U};
enum {
    POLL_MS = 100,
    DRAW_MS = 500
};
/* [0] without board buttons (Eva Kit), [1] with them */
static const char *const HELP[2] = {
    "Run a vacuum near it",
    "Run a vacuum near it - SW6 = Mop, SW5 = pause"};
/* ms on top before the robot changes what it does */
static const int32_t DWELLS[1] = {1000};
/* also NEED: the classes this example needs */
static const char *const CHIPS[3] = {"Air", "Floor", "Carpet"};
/* ---- end of settings ---- */

enum { /* times, ms */
    LOOP_MS = 20,
    LOAD_MS = 150,
    OP_POLL_MS = 20,
    FAIL_MS = 3000,
    MS_PER_SECOND = 1000
};
enum { /* sizes */
    MAX_CLASSES = BENTO_EDGEAI_MAX_CLASSES,
    CHIP_COUNT = 3,
    ACT_COUNT = 4,
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
    COLOR_INFO = 0x4A9EFF
};
enum { /* font sizes, px */
    SMALL_PX = 16,
    MEDIUM_PX = 20,
    LARGE_PX = 24,
    HUGE_PX = 28
};

/* What the BENTO Emulator says: 119 bytes, within the 126 one
 * label takes; the label gets a width so it wraps. */
static const char EMULATOR_TEXT[] =
    "Edge AI จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";
static const char NOT_SURE[] = "Not sure";

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
    shown_t candidate;
    shown_t act;
    shown_t why;
    shown_t mop;
    shown_t mop_button;
    shown_t status;
} screen_t;

/* A class counts after DWELLS[0] on top at Confirm at; -1 = Not sure. */
typedef struct {
    int32_t accepted;
    int32_t candidate;
    uint32_t since_ms;
    int32_t count;
    int32_t chip_of[MAX_CLASSES];
} dwell_t;

/* The top class of a new result: tp in the .py. */
typedef struct {
    bool known;
    uint32_t klass;
    uint8_t percent;
    uint8_t confirm;
} top_t;

typedef struct {
    bento_ui_t *ui;
    bool has_buttons;
    board_button_t sw5;
    board_button_t sw6;
    model_t model;
    screen_t screen;
    bento_edgeai_thresholds_t thresholds;
    dwell_t dwell;
    top_t top;
    bool mop;
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
} vacuum_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern vacuum_t g_vacuum;
vacuum_t g_vacuum;

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
static void raise_stop(vacuum_t *vacuum, const char *text)
{
    vacuum->stopped = true;
    (void)snprintf(vacuum->stop_text, sizeof(vacuum->stop_text), "%s",
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
    bool matches = BUILTIN;

    if (compiled >= 0) {
        matches = BUILTIN ? (index < compiled) : (index >= compiled);
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

/* One row: counted, and kept when it is MODEL from the right
 * source and nothing was found before. */
static void consider_row(model_t *model, bool *found,
                         const bento_edgeai_model_info_t *info,
                         int32_t index, int32_t compiled)
{
    if (!*found && (strcmp(info->name, MODEL) == 0) &&
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
                   MODEL);
    return missing(ui, "Model not on this board", text);
}

/* The heading, the subtitle and the "Now" card. */
static bento_status_t add_now_card(vacuum_t *vacuum)
{
    static const rect_t title_at = {12, 4, -1, -1};
    static const rect_t subtitle_at = {12, 38, -1, -1};
    static const rect_t card_at = {12, 64, 470, 272};
    static const widget_t heartbeat = {
        UI_WIDGET_LED, NULL, {756, 6, 26, 26}, COLOR_OK, 0, 0, 0};
    static const rect_t verdict_at = {28, 96, -1, -1};
    screen_t *screen = &vacuum->screen;
    bento_ui_id_t unused = 0U;
    bento_status_t status =
        add_label(vacuum->ui, "Robot Vacuum Brain", title_at,
                  COLOR_TEXT, LARGE_PX, &unused);

    if (status == BENTO_OK) {
        status = add_label(vacuum->ui,
                           "Detects: vacuum sound over air, floor, "
                           "carpet. Cannot: work without a vacuum.",
                           subtitle_at, COLOR_DIM, SMALL_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_card(vacuum->ui, card_at, "Now");
    }
    if (status == BENTO_OK) {
        status = add_widget(vacuum->ui, &heartbeat, &screen->heartbeat);
    }
    if (status == BENTO_OK) {
        status = add_label(vacuum->ui, NOT_SURE, verdict_at, COLOR_DIM,
                           HUGE_PX, &screen->verdict.id);
    }
    return status;
}

/* One chip per surface: a panel and its name, then the dwell line. */
static bento_status_t add_chips(vacuum_t *vacuum)
{
    enum {
        CHIP_X = 28,
        CHIP_STEP = 148,
        CHIP_Y = 170,
        CHIP_W = 140,
        CHIP_H = 56,
        TEXT_Y = 186,
        TEXT_INSET = 10,
        CHIP_RADIUS = 8
    };
    static const rect_t candidate_at = {28, 240, -1, -1};
    screen_t *screen = &vacuum->screen;
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
            add_widget(vacuum->ui, &panel, &screen->chip_panel[i].id);
        if (status == BENTO_OK) {
            status = add_label(vacuum->ui, CHIPS[i], text_at, COLOR_DIM,
                               MEDIUM_PX, &screen->chip_text[i].id);
        }
    }
    if (status == BENTO_OK) {
        status = add_label(vacuum->ui, " ", candidate_at, COLOR_DIM,
                           SMALL_PX, &screen->candidate.id);
    }
    return status;
}

/* The "Robot does" card: the action, why, the mop and its button. */
static bento_status_t add_robot_card(vacuum_t *vacuum)
{
    static const rect_t card_at = {494, 64, 294, 272};
    static const rect_t act_at = {508, 100, -1, -1};
    static const rect_t why_at = {508, 140, -1, -1};
    static const rect_t mop_at = {508, 170, -1, -1};
    static const widget_t mop_button = {UI_WIDGET_BUTTON,
                                        "Mop: ON",
                                        {508, 266, 266, 60},
                                        COLOR_BUTTON,
                                        20,
                                        0,
                                        0};
    screen_t *screen = &vacuum->screen;
    bento_status_t status = add_card(vacuum->ui, card_at, "Robot does");

    if (status == BENTO_OK) {
        status = add_label(vacuum->ui, "Normal", act_at, COLOR_DIM,
                           HUGE_PX, &screen->act.id);
    }
    if (status == BENTO_OK) {
        status = add_label(vacuum->ui, " ", why_at, COLOR_DIM, SMALL_PX,
                           &screen->why.id);
    }
    if (status == BENTO_OK) {
        status = add_label(vacuum->ui, " ", mop_at, COLOR_DIM, SMALL_PX,
                           &screen->mop.id);
    }
    if (status == BENTO_OK) {
        status =
            add_widget(vacuum->ui, &mop_button, &screen->mop_button.id);
    }
    return status;
}

static bento_status_t build_screen(vacuum_t *vacuum)
{
    static const rect_t status_at = {12, 346, -1, -1};
    bento_status_t status =
        bento_ui_screen(vacuum->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(LOAD_MS);
    }
    if (status == BENTO_OK) {
        status = add_now_card(vacuum);
    }
    if (status == BENTO_OK) {
        status = add_chips(vacuum);
    }
    if (status == BENTO_OK) {
        status = add_robot_card(vacuum);
    }
    if (status == BENTO_OK) {
        status = add_label(vacuum->ui, " ", status_at, COLOR_WARN,
                           SMALL_PX, &vacuum->screen.status.id);
    }
    if (status == BENTO_OK) {
        drain_events(vacuum->ui);
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

/* read_th(): per class Hearing at, Confirm at, clear below. */
static void read_thresholds(vacuum_t *vacuum)
{
    bento_edgeai_thresholds_t *thresholds = &vacuum->thresholds;
    uint32_t i;

    bento_edgeai_thresholds_init(thresholds);
    if (bento_edgeai_thresholds(vacuum->model.index, thresholds) !=
        BENTO_OK) {
        for (i = 0U; i < (uint32_t)MAX_CLASSES; i++) {
            thresholds->enter[i] = DEF_THRESHOLDS[0];
            thresholds->confirm[i] = DEF_THRESHOLDS[1];
            thresholds->exit[i] = DEF_THRESHOLDS[2];
        }
    }
}

/* edge_ai.count() on resume: a changed list is the Stop "Model list
 * changed"; a failed read is the OSError. */
static bento_status_t check_list(vacuum_t *vacuum)
{
    int32_t count = 0;
    const bento_status_t link = bento_edgeai_count(&count);

    if (link != BENTO_OK) {
        (void)printf("select: %s\n", link_error_text(link));
    } else if (count != vacuum->model.row_count) {
        raise_stop(vacuum, "Model list changed - run again");
    } else {
        /* the same list */
    }
    return link;
}

/* edge_ai.select(i): loads and starts it, up to 15 s. *link is the
 * OSError, if any; the status is the IDE's Stop. */
static bento_status_t select_model(const vacuum_t *vacuum,
                                   bento_status_t *link)
{
    bento_status_t status = BENTO_OK;

    *link = bento_edgeai_select_start(vacuum->model.index);
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
static bento_status_t start_model(vacuum_t *vacuum, bool check_count)
{
    char text[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    bento_status_t link = BENTO_OK;

    (void)snprintf(text, sizeof(text), "Loading %s ...", MODEL);
    put_text(vacuum->ui, &vacuum->screen.status, text);
    put_color(vacuum->ui, &vacuum->screen.status, COLOR_WARN);
    drain_events(vacuum->ui);
    vacuum->failing = false;
    if (check_count) {
        link = check_list(vacuum);
    }
    if ((link == BENTO_OK) && !vacuum->stopped) {
        status = select_model(vacuum, &link);
    }
    if ((status == BENTO_OK) && !vacuum->stopped &&
        (link != BENTO_OK)) {
        (void)snprintf(text, sizeof(text), "Cannot start %s", MODEL);
        raise_stop(vacuum, text);
    }
    if ((status == BENTO_OK) && !vacuum->stopped) {
        put_text(vacuum->ui, &vacuum->screen.status,
                 HELP[vacuum->has_buttons ? 1 : 0]);
        put_color(vacuum->ui, &vacuum->screen.status, COLOR_TEXT);
    }
    return status;
}

/* read_result(): only NEW results of this model; Stop after 3 s of
 * errors. */
static bool read_result(vacuum_t *vacuum, bento_edgeai_result_t *result)
{
    const uint32_t now_ms = bento_time_ticks_ms();
    bool fresh = false;

    bento_edgeai_result_init(result);
    if (bento_edgeai_result(result) != BENTO_OK) {
        if (!vacuum->failing) {
            vacuum->failing = true;
            vacuum->fail_start_ms = now_ms;
        }
        if (bento_time_ticks_diff(now_ms, vacuum->fail_start_ms) >=
            FAIL_MS) {
            raise_stop(vacuum, "AI core not answering - run again");
        }
    } else {
        vacuum->failing = false;
        fresh = result->has_verdict &&
                (!vacuum->has_sequence ||
                 (result->seq != vacuum->last_sequence)) &&
                ((uint32_t)result->index == vacuum->model.index);
        if (fresh) {
            vacuum->has_sequence = true;
            vacuum->last_sequence = result->seq;
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

/* odd(): a model whose classes are not the ones this example needs
 * gets a card, never a guess. */
static bento_status_t check_classes(vacuum_t *vacuum, bool *odd)
{
    char absent[TEXT_BYTES];
    char text[TEXT_BYTES];
    size_t used = 0U;
    int32_t i;
    bento_status_t status = BENTO_OK;

    *odd = false;
    absent[0] = '\0';
    for (i = 0; i < CHIP_COUNT; i++) {
        if (!has_label(&vacuum->model, CHIPS[i])) {
            (void)snprintf(&absent[used], sizeof(absent) - used, "%s%s",
                           *odd ? ", " : "", CHIPS[i]);
            used = strlen(absent);
            *odd = true;
        }
    }
    if (*odd) {
        (void)snprintf(text, sizeof(text), "%s has no class\n%s", MODEL,
                       absent);
        status = missing(
            vacuum->ui, "Model classes differ from this example", text);
    }
    return status;
}

/* The chip a class lights: its name among CHIPS, else none. */
static int32_t chip_of_label(const char *label)
{
    int32_t chip = NO_CLASS;
    int32_t i;

    for (i = 0; i < CHIP_COUNT; i++) {
        if (strcmp(label, CHIPS[i]) == 0) {
            chip = i;
        }
    }
    return chip;
}

/* The chip of each class and a fresh dwell. */
static void prepare_classes(vacuum_t *vacuum)
{
    const model_t *model = &vacuum->model;
    uint32_t i;

    for (i = 0U; i < model->class_count; i++) {
        vacuum->dwell.chip_of[i] = chip_of_label(model->labels[i]);
    }
    vacuum->dwell.accepted = NO_CLASS;
    vacuum->dwell.candidate = NO_CLASS;
}

static const char *class_name(const vacuum_t *vacuum, int32_t klass)
{
    return (klass >= 0) ? vacuum->model.labels[klass] : NOT_SURE;
}

static int32_t chip_of(const vacuum_t *vacuum, int32_t klass)
{
    return (klass >= 0) ? vacuum->dwell.chip_of[klass] : NO_CLASS;
}

/* Dwell.step(): true when the accepted state changes; one console
 * line each. */
static bool dwell_step(vacuum_t *vacuum, int32_t klass, uint8_t percent,
                       uint32_t now_ms)
{
    dwell_t *dwell = &vacuum->dwell;
    char clock[CLOCK_BYTES];
    bool changed = false;

    if (klass != dwell->candidate) {
        dwell->candidate = klass;
        dwell->since_ms = now_ms;
    }
    if ((klass != dwell->accepted) &&
        (bento_time_ticks_diff(now_ms, dwell->since_ms) >= DWELLS[0])) {
        changed = true;
        dwell->accepted = klass;
        clock_text(clock, sizeof(clock));
        (void)printf("%s %s %u%%", clock, class_name(vacuum, klass),
                     (unsigned int)percent);
        if (klass >= 0) {
            dwell->count++;
            (void)printf(" #%" PRId32, dwell->count);
        }
        (void)printf("\n");
    }
    return changed;
}

/* One new result: the top class, then the dwell. */
static void take_result(vacuum_t *vacuum,
                        const bento_edgeai_result_t *result,
                        uint32_t now_ms)
{
    const uint32_t classes =
        (result->class_count < vacuum->model.class_count)
            ? result->class_count
            : vacuum->model.class_count;
    top_t *top = &vacuum->top;

    if (classes > 0U) {
        top->known = true;
        top->klass = top_class(result, classes);
        top->percent = result->score_pct[top->klass];
        top->confirm = vacuum->thresholds.confirm[top->klass];
        (void)dwell_step(vacuum,
                         (top->percent >= top->confirm)
                             ? (int32_t)top->klass
                             : NO_CLASS,
                         top->percent, now_ms);
    }
}

/* The chips: accepted white, candidate grey-blue. */
static void draw_chips(vacuum_t *vacuum)
{
    static const uint32_t panel_colors[3] = {COLOR_CARD, COLOR_TEXT,
                                             COLOR_BUTTON};
    static const uint32_t text_colors[3] = {COLOR_DIM, COLOR_CARD,
                                            COLOR_TEXT};
    const int32_t accepted = chip_of(vacuum, vacuum->dwell.accepted);
    const int32_t candidate = chip_of(vacuum, vacuum->dwell.candidate);
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
        put_color(vacuum->ui, &vacuum->screen.chip_panel[i],
                  panel_colors[kind]);
        put_color(vacuum->ui, &vacuum->screen.chip_text[i],
                  text_colors[kind]);
    }
}

/* A number of milliseconds as "%.1f" seconds on this board. */
static void seconds_text(int32_t duration_ms, char *text)
{
    uint32_t length = 0U;
    const bento_real_t seconds =
        (bento_real_t)duration_ms / (bento_real_t)MS_PER_SECOND;

    if (bento_fmt_real_precision(text, BENTO_FMT_BUF_SIZE, seconds, 1,
                                 &length) != BENTO_OK) {
        text[0] = '\0';
    }
}

/* The line under the chips: the dwell running, or the top class. */
static void draw_candidate(vacuum_t *vacuum, uint32_t now_ms)
{
    const dwell_t *dwell = &vacuum->dwell;
    const top_t *top = &vacuum->top;
    char elapsed[BENTO_FMT_BUF_SIZE];
    char needed[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    int32_t elapsed_ms = 0;

    if (dwell->candidate != dwell->accepted) {
        elapsed_ms = bento_time_ticks_diff(now_ms, dwell->since_ms);
    }
    if (elapsed_ms != 0) {
        seconds_text(elapsed_ms, elapsed);
        seconds_text(DWELLS[0], needed);
        (void)snprintf(text, sizeof(text), "%s %s/%s s",
                       class_name(vacuum, dwell->candidate), elapsed,
                       needed);
        put_text(vacuum->ui, &vacuum->screen.candidate, text);
    } else if (top->known) {
        (void)snprintf(
            text, sizeof(text), "top %s %u%% | Confirm at %u%%",
            vacuum->model.labels[top->klass],
            (unsigned int)top->percent, (unsigned int)top->confirm);
        put_text(vacuum->ui, &vacuum->screen.candidate, text);
    } else {
        /* nothing new to say */
    }
}

static void draw(vacuum_t *vacuum, uint32_t now_ms)
{
    screen_t *screen = &vacuum->screen;
    const int32_t accepted = vacuum->dwell.accepted;

    /* the heartbeat: on at every other draw that had a new result */
    (void)bento_ui_set_value(vacuum->ui, screen->heartbeat,
                             (vacuum->blink && vacuum->top.known) ? 1
                                                                  : 0);
    put_text(vacuum->ui, &screen->verdict,
             class_name(vacuum, accepted));
    put_color(vacuum->ui, &screen->verdict,
              (accepted >= 0) ? COLOR_TEXT : COLOR_DIM);
    draw_chips(vacuum);
    draw_candidate(vacuum, now_ms);
}

/* What the robot does on the accepted surface, and the mop. */
static void show_robot(vacuum_t *vacuum)
{
    typedef struct {
        const char *surface;
        const char *act;
        const char *why;
        uint32_t color;
    } act_t;
    static const act_t acts[ACT_COUNT] = {
        {"Air", "Motor off", "lifted - nozzle in the air", COLOR_TEXT},
        {"Floor", "Normal", "hard floor", COLOR_DIM},
        {"Carpet", "BOOST", "carpet - more suction", COLOR_TEXT},
        {"unlabeled", "Normal", "no vacuum heard", COLOR_DIM}};
    static const act_t unsure = {"", "Normal", "not sure - stay Normal",
                                 COLOR_DIM};
    const char *surface = class_name(vacuum, vacuum->dwell.accepted);
    const bool carpet = strcmp(surface, "Carpet") == 0;
    const act_t *act = &unsure;
    const char *mop = "Mop off";
    int32_t i;

    for (i = 0; i < ACT_COUNT; i++) {
        if (strcmp(surface, acts[i].surface) == 0) {
            act = &acts[i];
        }
    }
    put_text(vacuum->ui, &vacuum->screen.act, act->act);
    put_color(vacuum->ui, &vacuum->screen.act, act->color);
    put_text(vacuum->ui, &vacuum->screen.why, act->why);
    if (vacuum->mop) {
        mop = carpet ? "Mop lifted (carpet)" : "Mop down";
    }
    put_text(vacuum->ui, &vacuum->screen.mop, mop);
    put_color(vacuum->ui, &vacuum->screen.mop,
              (vacuum->mop && carpet) ? COLOR_TEXT : COLOR_DIM);
}

/* A tap on the Mop button. */
static bool read_touch(vacuum_t *vacuum)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    bool tapped = false;
    uint32_t i;

    if (bento_ui_poll(vacuum->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U; /* a failed poll answers no events */
    }
    for (i = 0U; i < count; i++) {
        if ((events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) &&
            (events[i].id == vacuum->screen.mop_button.id)) {
            tapped = true;
        }
    }
    return tapped;
}

/* SW5: pause (the AI core stops) or resume (it starts again). */
static bento_status_t toggle_pause(vacuum_t *vacuum)
{
    bento_status_t status = BENTO_OK;

    vacuum->paused = !vacuum->paused;
    /* start again from Not sure */
    vacuum->dwell.accepted = NO_CLASS;
    vacuum->dwell.candidate = NO_CLASS;
    if (vacuum->paused) {
        status = stop_ai();
        put_text(vacuum->ui, &vacuum->screen.status,
                 "Paused - SW5 resumes");
        put_color(vacuum->ui, &vacuum->screen.status, COLOR_WARN);
    } else {
        status = start_model(vacuum, true);
    }
    return status;
}

/* The buttons and the touches: SW6 or Mop switches the mop. */
static bento_status_t read_controls(vacuum_t *vacuum)
{
    bool tapped;
    bento_status_t status =
        sample_button(vacuum->has_buttons, &vacuum->sw5);

    if (status == BENTO_OK) {
        status = sample_button(vacuum->has_buttons, &vacuum->sw6);
    }
    if (status == BENTO_OK) {
        tapped = button_clicked(&vacuum->sw6);
        tapped = read_touch(vacuum) || tapped;
        if (tapped) {
            vacuum->mop = !vacuum->mop;
            put_text(vacuum->ui, &vacuum->screen.mop_button,
                     vacuum->mop ? "Mop: ON" : "Mop: OFF");
        }
        if (button_clicked(&vacuum->sw5)) {
            status = toggle_pause(vacuum);
        }
    }
    return status;
}

/* One round of the main loop. */
static bento_status_t vacuum_round(vacuum_t *vacuum)
{
    bento_edgeai_result_t result;
    uint32_t now_ms;
    bento_status_t status = read_controls(vacuum);

    now_ms = bento_time_ticks_ms();
    if ((status == BENTO_OK) && !vacuum->stopped && !vacuum->paused &&
        (bento_time_ticks_diff(now_ms, vacuum->polled_ms) >= POLL_MS)) {
        vacuum->polled_ms = now_ms;
        if (read_result(vacuum, &result)) {
            take_result(vacuum, &result, now_ms);
        }
    }
    if ((status == BENTO_OK) && !vacuum->stopped) {
        if (bento_time_ticks_diff(now_ms, vacuum->drawn_ms) >=
            DRAW_MS) {
            vacuum->drawn_ms = now_ms;
            vacuum->blink = !vacuum->blink;
            draw(vacuum, now_ms);
            show_robot(vacuum);
            vacuum->top.known = false;
        }
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* run(): the classes, the first start, then the loop until Stop. */
static bento_status_t run_vacuum(vacuum_t *vacuum)
{
    bool odd = false;
    bento_status_t status = check_classes(vacuum, &odd);

    if ((status == BENTO_OK) && !odd) {
        read_thresholds(vacuum);
        prepare_classes(vacuum);
        vacuum->mop = true;
        status = start_model(vacuum, false);
        vacuum->drawn_ms = bento_time_ticks_ms();
        vacuum->polled_ms = vacuum->drawn_ms;
        while ((status == BENTO_OK) && !vacuum->stopped) {
            status = vacuum_round(vacuum);
        }
    }
    return status;
}

/* The screen, the run, and the finally: the AI core is always released. */
static bento_status_t run_program(vacuum_t *vacuum)
{
    bento_status_t status = build_screen(vacuum);
    bento_status_t stop_status;

    if (status == BENTO_OK) {
        status = run_vacuum(vacuum);
        if (status == BENTO_E_CANCELLED) { /* the finally still runs */
            (void)bento_runtime_acknowledge_stop();
        }
        if (vacuum->stopped) {
            put_text(vacuum->ui, &vacuum->screen.status,
                     vacuum->stop_text);
            put_color(vacuum->ui, &vacuum->screen.status, COLOR_BAD);
            (void)printf("%s\n", vacuum->stop_text);
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
    vacuum_t *vacuum = &g_vacuum;
    bool found = false;
    bool emulator = false;
    bento_status_t status;

    (void)memset(vacuum, 0, sizeof(*vacuum));
    vacuum->ui = bento_runtime_get_ui();
    vacuum->has_buttons = bento_buttons_count() > 0U;
    vacuum->sw5.index = SW5_INDEX;
    vacuum->sw6.index = SW6_INDEX;
    find_model(&vacuum->model, &found, &emulator);
    if (!found) {
        if (emulator) {
            say_no_emulator_ai();
        }
        status = missing_model(vacuum->ui);
        if ((status == BENTO_OK) && emulator) {
            status = show_no_emulator_ai(vacuum->ui);
        }
    } else {
        status = run_program(vacuum);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
