/*
 * 06_hmi_machine.c -- Smart HMI โรงงาน: จอซ่อมบำรุงปั๊มที่ AI ฟังการสั่น
 * (a factory HMI: a pump maintenance screen driven by a vibration AI)
 *
 * ผล AI ขับจอ HMI ของโรงงาน กรองผลด้วยความมั่นใจและกฎ 3 ใน 5 ก่อนเชื่อ สถานะ
 * สามขั้นแบบแผงควบคุม: ปกติ -> สั่นผิดปกติ (กะพริบ ยังไม่มีใครรับเรื่อง) -> ส่งใบสั่ง
 * ซ่อมแล้ว -> ปกติ เมื่อกด "ซ่อมเสร็จ" และเครื่องสั่นปกติแล้วเท่านั้น ซ้ายคือสภาพ
 * เครื่อง ผลดิบ ไฟ 5 ดวง จำนวนครั้ง และปุ่มใบสั่งซ่อม ขวาคือกราฟว่า AI ว่าผิดปกติ
 * กี่ % ทุกครึ่งวินาที และแถบสุขภาพจาก 30 ผลล่าสุด ทุกครั้งที่สถานะเปลี่ยนจะพิมพ์
 * JSON หนึ่งบรรทัด กด Stop เพื่อจบ
 *
 * The AI's results drive a factory HMI. Each result passes a
 * confidence floor and a 3-of-5 rule first. A three-step panel state:
 * normal -> alert (blinking, nobody has taken it) -> work order sent ->
 * normal, only after "repair done" is tapped and the machine runs
 * smoothly again. Left: the machine state, the raw result, five lamps,
 * the event count and the work-order button. Right: a chart of how
 * abnormal the AI thinks it is every half second, and a health bar
 * over the last 30 results. Every state change prints one JSON line.
 * Press Stop to end.
 *
 * Twin of 06_hmi_machine.py
 * (embedded-systems-for-aiot-developer/examples/s13/06_hmi_machine.py).
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - DEMO is False in the original, and edge_ai.inject() and events()
 *    have no C API yet (bento_edgeai README: to do), so the demo
 *    section is left out: this is the program with DEMO = False;
 *  - edge_ai.models() is bento_edgeai_count() and model_info() per
 *    row, read once for each wanted name; a row's index is its place;
 *  - a result's conf is score_pct[top] in percent, so int(conf * 100)
 *    is that number; its label is bento_edgeai_label() ("" when the
 *    board cannot name it);
 *  - select() and stop() wait inside the binding; here they are
 *    started and polled, and the OSError text is the binding's words
 *    ("edge_ai: select send failed", "edge_ai: select not confirmed");
 *  - json.dumps(snapshot()) is one printf with the keys in the order
 *    the dict is written (MicroPython's dict may print another order);
 *  - put()'s dict of what each widget shows is a cache per widget;
 *  - import edge_ai fails where there is no model link: here
 *    bento_edgeai_supported() false ends with BENTO_E_UNSUPPORTED;
 *  - Stop raises KeyboardInterrupt in MicroPython; here the wait
 *    returns and the program ends after the same finally (the model is
 *    stopped).
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
/* TEAM: the team number the teacher gives, used in session 12 (no
 * need to change it in this session) */
static const char TEAM[] = "teamXX";
static const char KIT[] = "machine";
enum {
    MANUAL_RESET = 1, /* 1: back to normal by "repair done" only */
    CONF_MIN = 60,    /* a result less sure than this does not count */
    CONFIRM_N = 3,    /* wins needed ... */
    WINDOW_N = 5,     /* ... among this many latest results */
    HEALTH_N = 30,    /* the machine's health from this many results */
    CHART_N = 40,     /* one point every DRAW_MS: 20 s of chart */
    DRAW_MS = 500,
    NOTE_MS = 3000, /* how long "cannot close" stays */
    HOLD_S = 30,    /* how long an error screen stays */
    DEMO = 0,      /* 1: demo with injected results; no demo in C yet */
    DEMO_PCT = 95, /* the demo's confidence, in percent */
    DEMO_STEPS = 60 /* the demo's length, in model results */
};
/* ---- end of settings ---- */

/* The note for the BENTO Emulator, whose C runtime has no Edge AI yet. */
static const char EMULATOR_NOTE[] =
    "Edge AI จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";

/* The screen's rhythm and the panel's limits. */
enum {
    HOLD_POLLS_PER_S = 5,
    HOLD_POLL_MS = 200,
    LOOP_MS = 50,
    SETTLE_MS = 150, /* let the cleared screen settle */
    OP_POLL_MS = 100,
    FULL_PCT = 100,
    HEALTH_GOOD = 80,
    HEALTH_FAIR = 50,
    MODEL_KEY_COUNT = 2,
    DANGER_COUNT = 2,
    HOLD_LINES = 4,
    SHOWN_CHARS = 128,
    TEXT_CHARS = 160
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_LINE = 0x293441,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF,
    COLOR_KEY = 0x212431
};

/* Layout on the 792 x 398 canvas: the machine card on the left, the
 * chart card on the right; it ends at y=344, clear of the Console. */
enum {
    MARGIN_X = 24,
    CARD_TEXT_X = 40,
    RIGHT_TEXT_X = 336,
    TITLE_Y = 10,
    AI_X = 480,
    AI_Y = 14,
    CARD_Y = 48,
    LEFT_CARD_W = 280,
    LEFT_CARD_H = 328,
    RIGHT_CARD_X = 320,
    RIGHT_CARD_W = 448,
    RIGHT_CARD_H = 296,
    CARD_RADIUS = 16,
    CAPTION_Y = 60,
    STATE_Y = 90,
    SUB_Y = 132,
    RAW_Y = 168,
    DOTS_CAPTION_Y = 200,
    DOT_Y = 230,
    DOT_STEP = 40,
    DOT_SIZE = 28,
    EVENTS_Y = 270,
    KEY_Y = 312,
    KEY_W = 248,
    KEY_H = 52,
    CHART_Y = 92,
    CHART_W = 416,
    CHART_H = 136,
    HEALTH_CAPTION_Y = 244,
    BAR_Y = 282,
    BAR_W = 300,
    BAR_H = 24,
    HP_X = 652,
    HP_Y = 276,
    HINT_Y = 312,
    HOLD_X = 24,
    HOLD_Y = 24,
    HOLD_STEP = 44,
    SMALL_PX = 16,
    TEXT_PX = 20,
    HOLD_PX = 24,
    BIG_PX = 28
};

/* The panel's three states. */
typedef enum {
    STATE_NORMAL = 0,
    STATE_ALERT = 1,
    STATE_ACKNOWLEDGED = 2,
    STATE_COUNT = 3
} panel_state_t;

/* Where a widget sits and how big it is (-1: the widget's default). */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} place_t;

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

/* A widget and what it shows now, so a text or colour is sent only
 * when it changes: every command crosses to the display core. */
typedef struct {
    bento_ui_id_t id;
    bool known;
    uint32_t color;
    char text[SHOWN_CHARS];
} shown_t;

/* Up to HOLD_LINES lines for an error screen. */
typedef struct {
    const char *line[HOLD_LINES];
    uint32_t count;
} hold_text_t;

/* The model, the widgets, the rule's memory and the snapshot. */
typedef struct {
    bento_ui_t *ui;
    uint32_t model;
    char model_name[BENTO_EDGEAI_NAME_LEN];
    shown_t state_label;
    shown_t sub;
    shown_t raw;
    shown_t key;
    shown_t events_label;
    bento_ui_id_t dots[WINDOW_N];
    bento_ui_id_t chart;
    uint8_t threshold_series;
    bento_ui_id_t bar;
    bento_ui_id_t hp_label;
    uint8_t hist[WINDOW_N];
    int32_t hist_count;
    bool alerting;
    int32_t lit[WINDOW_N];
    uint8_t health[HEALTH_N];
    int32_t health_count;
    bool hp_known;
    int32_t hp_shown;
    uint32_t hp_color;
    int32_t peak;
    bool blink;
    bool has_sequence;
    uint32_t last_sequence;
    const char *note;
    uint32_t note_ms;
    uint32_t drawn_ms;
    char label[BENTO_EDGEAI_LABEL_LEN];
    int32_t conf;
    panel_state_t state;
    int32_t events;
    uint32_t sequence;
} machine_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern machine_t g_machine;
machine_t g_machine;

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

static void place_args(bento_ui_widget_args_t *args, place_t place)
{
    bento_ui_widget_args_init(args);
    args->x = place.x;
    args->y = place.y;
    args->w = place.w;
    args->h = place.h;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    place_args(&args, (place_t){look.x, look.y, -1, -1});
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A card with a rounded border of COLOR_LINE. */
static bento_status_t add_card(bento_ui_t *ui, place_t place)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    place_args(&args, place);
    args.color = COLOR_CARD;
    args.min_val = COLOR_LINE;
    args.max_val = CARD_RADIUS;
    return bento_ui_panel_create(ui, &args, &unused);
}

/* A ui.poll() whose events the original throws away. */
static void poll_drop(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL); /* the events are not read, as before */
}

/* Sends the text and colour only when either changed. MicroPython's
 * setters ignore a refusal (modui.c). */
static void put(bento_ui_t *ui, shown_t *shown, const char *text,
                uint32_t color)
{
    if (!shown->known || (shown->color != color) ||
        (strcmp(shown->text, text) != 0)) {
        shown->known = true;
        shown->color = color;
        (void)snprintf(shown->text, sizeof(shown->text), "%s", text);
        (void)bento_ui_set_text(ui, shown->id, text,
                                (uint32_t)strlen(text));
        (void)bento_ui_set_color(ui, shown->id, color);
    }
}

/* The first model whose name matches, in the order of the names. */
static bento_status_t find_model(machine_t *machine, bool *found)
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
            machine->model = (uint32_t)row;
        }
    }
    if (*found) {
        (void)snprintf(machine->model_name, sizeof(machine->model_name),
                       "%s", info.name);
    }
    return status;
}

/* Clears the screen, shows why line by line, and keeps it HOLD_S
 * seconds to read; then the program ends by itself without failing. */
static bento_status_t hold(bento_ui_t *ui, const hold_text_t *text,
                           uint32_t color)
{
    bento_ui_id_t unused = 0U;
    uint32_t i;
    int32_t poll;
    bento_status_t status;

    (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    for (i = 0U; (i < text->count) && (status == BENTO_OK); i++) {
        const int16_t y = (int16_t)(HOLD_Y + ((int32_t)i * HOLD_STEP));

        status = add_label(
            ui, text->line[i],
            (label_look_t){HOLD_X, y, (i == 0U) ? color : COLOR_TEXT,
                           HOLD_PX},
            &unused);
    }
    for (poll = 0;
         (poll < ((int32_t)HOLD_S * (int32_t)HOLD_POLLS_PER_S)) &&
         (status == BENTO_OK);
         poll++) {
        poll_drop(ui);
        status = pause_ms(HOLD_POLL_MS);
    }
    return status;
}

static void add_line(hold_text_t *text, const char *line)
{
    if (text->count < (uint32_t)HOLD_LINES) {
        text->line[text->count] = line;
        text->count++;
    }
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

static bento_status_t hold_no_model(bento_ui_t *ui, bool in_emulator)
{
    hold_text_t text;

    text.count = 0U;
    add_line(&text, "ไม่พบโมเดลบนบอร์ด");
    add_line(&text, "AnomalousVibration หรือ Motion Detection");
    add_line(&text, "deploy จาก Store ก่อน");
    if (in_emulator) {
        add_line(&text, EMULATOR_NOTE);
    }
    return hold(ui, &text, COLOR_BAD);
}

/* Waits for a started select or stop; Stop cancels it. */
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

/* select() waits until the AI core confirms. Not confirmed: a screen
 * asks for RESET instead of failing. *started says whether it runs. */
static bento_status_t start_model(const machine_t *machine,
                                  bool *started)
{
    hold_text_t text;
    const char *reason = "edge_ai: select not confirmed";
    bento_status_t status = bento_edgeai_select_start(machine->model);

    if (status == BENTO_OK) {
        status = finish_op();
    } else {
        reason = "edge_ai: select send failed";
    }
    *started = status == BENTO_OK;
    if ((status != BENTO_OK) && (status != BENTO_E_CANCELLED)) {
        (void)printf("select failed: %s\n", reason);
        text.count = 0U;
        add_line(&text, "เปิดโมเดลไม่สำเร็จ");
        add_line(&text, reason);
        add_line(&text, "กดปุ่ม RESET บนบอร์ด");
        add_line(&text, "แล้วกด Run ใหม่อีกครั้ง");
        status = hold(machine->ui, &text, COLOR_BAD);
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

/* WINDOW_N lamps for the latest results (lit = danger). */
static bento_status_t add_dots(machine_t *machine)
{
    bento_ui_widget_args_t args;
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < WINDOW_N) && (status == BENTO_OK); i++) {
        const int16_t x = (int16_t)(CARD_TEXT_X + (i * DOT_STEP));

        place_args(&args, (place_t){x, DOT_Y, DOT_SIZE, DOT_SIZE});
        args.color = COLOR_BAD;
        args.font_px = 0;
        status =
            bento_ui_led_create(machine->ui, &args, &machine->dots[i]);
    }
    return status;
}

/* The left card: the state, the raw result, the lamps, the count and
 * the work-order button. */
static bento_status_t add_machine_card(machine_t *machine)
{
    bento_ui_t *ui = machine->ui;
    char text[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_card(
        ui, (place_t){MARGIN_X, CARD_Y, LEFT_CARD_W, LEFT_CARD_H});

    if (status == BENTO_OK) {
        status = add_card(ui, (place_t){RIGHT_CARD_X, CARD_Y,
                                        RIGHT_CARD_W, RIGHT_CARD_H});
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "สภาพเครื่อง",
            (label_look_t){CARD_TEXT_X, CAPTION_Y, COLOR_DIM, TEXT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){CARD_TEXT_X, STATE_Y, COLOR_OK, BIG_PX},
            &machine->state_label.id);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){CARD_TEXT_X, SUB_Y, COLOR_DIM, TEXT_PX},
            &machine->sub.id);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "-",
            (label_look_t){CARD_TEXT_X, RAW_Y, COLOR_DIM, TEXT_PX},
            &machine->raw.id);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%d ผลล่าสุด", WINDOW_N);
        status = add_label(ui, text,
                           (label_look_t){CARD_TEXT_X, DOTS_CAPTION_Y,
                                          COLOR_DIM, TEXT_PX},
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_dots(machine);
    }
    return status;
}

/* The event count and the work-order button under the lamps. */
static bento_status_t add_work_order(machine_t *machine)
{
    bento_ui_widget_args_t args;
    bento_status_t status = add_label(
        machine->ui, " ",
        (label_look_t){CARD_TEXT_X, EVENTS_Y, COLOR_TEXT, TEXT_PX},
        &machine->events_label.id);

    if (status == BENTO_OK) {
        place_args(&args, (place_t){CARD_TEXT_X, KEY_Y, KEY_W, KEY_H});
        args.text = "เครื่องปกติ";
        args.text_len = (uint32_t)strlen(args.text);
        args.color = COLOR_KEY;
        args.font_px = TEXT_PX;
        status = bento_ui_button_create(machine->ui, &args,
                                        &machine->key.id);
    }
    return status;
}

/* The chart of how abnormal the AI thinks it is, with the grey
 * threshold line. .prop() is a setter, and modui.c ignores a refusal. */
static bento_status_t add_chart(machine_t *machine)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        machine->ui, "AI ว่าผิดปกติกี่ %  เส้นเทา = เกณฑ์",
        (label_look_t){RIGHT_TEXT_X, CAPTION_Y, COLOR_DIM, TEXT_PX},
        &unused);

    if (status == BENTO_OK) {
        place_args(&args,
                   (place_t){RIGHT_TEXT_X, CHART_Y, CHART_W, CHART_H});
        args.min_val = 0;
        args.max_val = FULL_PCT;
        args.color = COLOR_BAD;
        status =
            bento_ui_chart_create(machine->ui, &args, &machine->chart);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_prop(machine->ui, machine->chart,
                                BENTO_UI_PROP_CHART_POINTS, CHART_N);
        status = bento_ui_chart_add_series(machine->ui, machine->chart,
                                           COLOR_DIM,
                                           &machine->threshold_series);
    }
    return status;
}

/* The health bar; a Bar ignores color= when it is made, so its colour
 * is set afterwards. */
static bento_status_t add_health(machine_t *machine)
{
    bento_ui_widget_args_t args;
    char text[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "สุขภาพเครื่อง จาก %d ผลล่าสุด",
                   HEALTH_N);
    status = add_label(machine->ui, text,
                       (label_look_t){RIGHT_TEXT_X, HEALTH_CAPTION_Y,
                                      COLOR_DIM, TEXT_PX},
                       &unused);
    if (status == BENTO_OK) {
        place_args(&args, (place_t){RIGHT_TEXT_X, BAR_Y, BAR_W, BAR_H});
        args.min_val = 0;
        args.max_val = FULL_PCT;
        args.font_px = FULL_PCT;
        status = bento_ui_bar_create(machine->ui, &args, &machine->bar);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_set_color(machine->ui, machine->bar, COLOR_OK);
        status =
            add_label(machine->ui, "100%",
                      (label_look_t){HP_X, HP_Y, COLOR_TEXT, BIG_PX},
                      &machine->hp_label);
    }
    if (status == BENTO_OK) {
        status = add_label(
            machine->ui, "ลองเขย่าบอร์ด ดูผลดิบด้านซ้าย",
            (label_look_t){RIGHT_TEXT_X, HINT_Y, COLOR_DIM, TEXT_PX},
            &unused);
    }
    return status;
}

static bento_status_t build_screen(machine_t *machine)
{
    char text[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(machine->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            machine->ui, "ปั๊มหล่อเย็น P-101  AI ฟังการสั่น",
            (label_look_t){MARGIN_X, TITLE_Y, COLOR_TEXT, TEXT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "AI: %s",
                       machine->model_name);
        status = add_label(
            machine->ui, text,
            (label_look_t){AI_X, AI_Y, COLOR_INFO, SMALL_PX}, &unused);
    }
    if (status == BENTO_OK) {
        status = add_machine_card(machine);
    }
    if (status == BENTO_OK) {
        status = add_work_order(machine);
    }
    if (status == BENTO_OK) {
        status = add_chart(machine);
    }
    if (status == BENTO_OK) {
        status = add_health(machine);
    }
    if (status == BENTO_OK) {
        poll_drop(machine->ui);
    }
    return status;
}

/* Adds one flag to a window of the latest flags, dropping the oldest
 * when full, and answers how many are set. */
static int32_t push_flag(uint8_t *window, int32_t *count, int32_t size,
                         bool flag)
{
    int32_t hits = 0;
    int32_t i;

    if (*count == size) {
        for (i = 1; i < size; i++) {
            window[i - 1] = window[i];
        }
        (*count)--;
    }
    window[*count] = flag ? 1U : 0U;
    (*count)++;
    for (i = 0; i < *count; i++) {
        hits += (int32_t)window[i];
    }
    return hits;
}

/* The k-of-n rule: confirmed at CONFIRM_N hits in the window, and it
 * stays confirmed while any hit remains. */
static void alert_rule(machine_t *machine, bool danger)
{
    const int32_t hits = push_flag(machine->hist, &machine->hist_count,
                                   WINDOW_N, danger);

    machine->alerting =
        (hits >= CONFIRM_N) || (machine->alerting && (hits > 0));
}

/* The next state. act: the main button was tapped; manual: normal
 * comes back only by the button. *refused: closing was asked while the
 * danger is still there. */
static panel_state_t step(panel_state_t state, bool alerting, bool act,
                          bool *refused)
{
    const bool manual = MANUAL_RESET != 0;
    panel_state_t next = STATE_ACKNOWLEDGED;

    *refused = false;
    if (state == STATE_NORMAL) {
        next = alerting ? STATE_ALERT : STATE_NORMAL;
    } else if (state == STATE_ALERT) {
        /* a danger that went away still alerts until someone takes it */
        next = act ? STATE_ACKNOWLEDGED : STATE_ALERT;
    } else if (!alerting && (act || !manual)) {
        next = STATE_NORMAL;
    } else {
        *refused = act && alerting;
    }
    return next;
}

/* Every AI result in one JSON line; session 12 publishes it on MQTT. */
static void print_snapshot(const machine_t *machine)
{
    static const char *const state_names[STATE_COUNT] = {
        "normal", "alert", "ack"};

    (void)printf("{\"id\": \"%s\", \"kit\": \"%s\", \"model\": \"%s\", "
                 "\"label\": \"%s\", \"conf\": %" PRId32 ", \"state\": "
                 "\"%s\", \"events\": %" PRId32 ", \"n\": %" PRIu32
                 "}\n",
                 TEAM, KIT, machine->model_name, machine->label,
                 machine->conf, state_names[machine->state],
                 machine->events, machine->sequence);
}

/* The sub line under the state word. */
static void draw_sub(machine_t *machine, bool alerting)
{
    const char *sub = "สั่นปกติแล้ว กดปิดงานได้";
    uint32_t color = COLOR_OK;

    if (machine->note != NULL) {
        sub = machine->note;
        color = COLOR_BAD;
    } else if (machine->state == STATE_NORMAL) {
        sub = "AI ไม่พบการสั่นผิดปกติ";
        color = COLOR_DIM;
    } else if (machine->state == STATE_ALERT) {
        sub = "ยังไม่มีใครรับเรื่อง";
        color = COLOR_BAD;
    } else if (alerting) {
        sub = "รอช่าง เครื่องยังสั่นอยู่";
        color = COLOR_WARN;
    } else {
        /* ack, and the machine runs smoothly: the values above */
    }
    put(machine->ui, &machine->sub, sub, color);
}

/* The state word blinks while nobody has taken the alert. */
static void draw_state(machine_t *machine, bool alerting)
{
    static const char *const words[STATE_COUNT] = {"ปกติ", "สั่นผิดปกติ",
                                                   "ส่งใบสั่งซ่อมแล้ว"};
    static const uint32_t word_colors[STATE_COUNT] = {
        COLOR_OK, COLOR_BAD, COLOR_WARN};
    static const char *const keys[STATE_COUNT] = {
        "เครื่องปกติ", "สร้างใบสั่งซ่อม", "ซ่อมเสร็จ ปิดงาน"};
    static const uint32_t key_colors[STATE_COUNT] = {
        COLOR_KEY, COLOR_WARN, COLOR_INFO};
    const panel_state_t state = machine->state;
    const bool dim = (state == STATE_ALERT) && machine->blink;
    char text[TEXT_CHARS];

    put(machine->ui, &machine->state_label, words[state],
        dim ? COLOR_DIM : word_colors[state]);
    draw_sub(machine, alerting);
    put(machine->ui, &machine->key, keys[state], key_colors[state]);
    (void)snprintf(text, sizeof(text), "สั่นผิดปกติ %" PRId32 " ครั้ง",
                   machine->events);
    put(machine->ui, &machine->events_label, text, COLOR_TEXT);
}

/* True when the main button was tapped in this poll. */
static bool key_tapped(const machine_t *machine)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bool tapped = false;

    /* ui.poll() answers no events when it fails. */
    if (bento_ui_poll(machine->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if ((events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) &&
            (events[i].id == machine->key.id)) {
            tapped = true;
        }
    }
    return tapped;
}

static bool is_danger(const char *label)
{
    static const char *const danger[DANGER_COUNT] = {"anomaly",
                                                     "shaking"};

    return (strcmp(label, danger[0]) == 0) ||
           (strcmp(label, danger[1]) == 0);
}

/* A new result of this model with a label: the rule, the health and
 * the chart's peak take it. A failed read is passed over this round. */
static bool take_result(machine_t *machine)
{
    bento_edgeai_result_t result;
    bool fresh;

    bento_edgeai_result_init(&result);
    fresh = (bento_edgeai_result(&result) == BENTO_OK) &&
            result.has_verdict && (result.index == machine->model) &&
            (!machine->has_sequence ||
             (result.seq != machine->last_sequence)) &&
            (result.top < BENTO_EDGEAI_MAX_CLASSES);
    if (fresh) {
        const int32_t pct = (int32_t)result.score_pct[result.top];
        bool bad;

        machine->has_sequence = true;
        machine->last_sequence = result.seq;
        if (bento_edgeai_label(result.index, result.top, machine->label,
                               (uint32_t)sizeof(machine->label),
                               NULL) != BENTO_OK) {
            machine->label[0] = '\0';
        }
        bad = is_danger(machine->label) && (pct >= CONF_MIN);
        alert_rule(machine, bad);
        (void)push_flag(machine->health, &machine->health_count,
                        HEALTH_N, bad);
        if (is_danger(machine->label) && (pct > machine->peak)) {
            machine->peak = pct;
        }
        machine->conf = pct;
        machine->sequence = result.seq;
    }
    return fresh;
}

/* A new result or a tap moves the state; a change is printed. */
static void update_state(machine_t *machine, bool act, uint32_t now_ms)
{
    const panel_state_t was = machine->state;
    bool refused = false;

    machine->state = step(was, machine->alerting, act, &refused);
    if (refused) {
        machine->note = "ยังสั่นผิดปกติ ปิดงานไม่ได้";
        machine->note_ms = now_ms;
    }
    if (machine->state != was) {
        if (machine->state == STATE_ALERT) {
            machine->events++;
        }
        machine->note = NULL;
        print_snapshot(machine);
    }
    if (act || (machine->state != was)) {
        draw_state(machine, machine->alerting);
    }
}

/* Only the lamps that changed are sent. */
static void draw_dots(machine_t *machine)
{
    int32_t i;

    for (i = 0; i < WINDOW_N; i++) {
        const int32_t value =
            (i < machine->hist_count) ? (int32_t)machine->hist[i] : 0;

        if (value != machine->lit[i]) {
            (void)bento_ui_set_value(machine->ui, machine->dots[i],
                                     value);
            machine->lit[i] = value;
        }
    }
}

/* The health over the latest HEALTH_N results, sent when it changed. */
static void draw_health(machine_t *machine)
{
    char text[TEXT_CHARS];
    int32_t hits = 0;
    int32_t i;

    if (machine->health_count > 0) {
        int32_t hp;
        uint32_t color = COLOR_BAD;

        for (i = 0; i < machine->health_count; i++) {
            hits += (int32_t)machine->health[i];
        }
        hp = FULL_PCT - ((FULL_PCT * hits) / machine->health_count);
        if (hp >= HEALTH_GOOD) {
            color = COLOR_OK;
        } else if (hp >= HEALTH_FAIR) {
            color = COLOR_WARN;
        } else {
            color = COLOR_BAD;
        }
        if (!machine->hp_known || (machine->hp_shown != hp) ||
            (machine->hp_color != color)) {
            machine->hp_known = true;
            machine->hp_shown = hp;
            machine->hp_color = color;
            (void)bento_ui_set_value(machine->ui, machine->bar, hp);
            (void)bento_ui_set_color(machine->ui, machine->bar, color);
            (void)snprintf(text, sizeof(text), "%" PRId32 "%%", hp);
            (void)bento_ui_set_text(machine->ui, machine->hp_label,
                                    text, (uint32_t)strlen(text));
        }
    }
}

/* Every DRAW_MS: the blink, the note's time, the state, the raw
 * result, the lamps, one chart point (the peak of that half second)
 * and the health. */
static void redraw(machine_t *machine, uint32_t now_ms)
{
    char text[TEXT_CHARS];

    machine->drawn_ms = now_ms;
    machine->blink = !machine->blink;
    if ((machine->note != NULL) &&
        (bento_time_ticks_diff(now_ms, machine->note_ms) >= NOTE_MS)) {
        machine->note = NULL;
    }
    draw_state(machine, machine->alerting);
    (void)snprintf(text, sizeof(text), "ผลดิบ: %s  %" PRId32 "%%",
                   machine->label, machine->conf);
    put(machine->ui, &machine->raw, text, COLOR_DIM);
    draw_dots(machine);
    (void)bento_ui_chart_set_next(machine->ui, machine->chart, 0U,
                                  machine->peak);
    (void)bento_ui_chart_set_next(machine->ui, machine->chart,
                                  machine->threshold_series, CONF_MIN);
    machine->peak = 0;
    draw_health(machine);
}

/* The panel's loop; only Stop or an error ends it. */
static bento_status_t run_panel(machine_t *machine)
{
    bento_status_t status = BENTO_OK;

    draw_state(machine, machine->alerting);
    while (status == BENTO_OK) {
        const uint32_t now_ms = bento_time_ticks_ms();
        const bool act = key_tapped(machine);
        const bool fresh = take_result(machine);

        if (fresh || act) {
            update_state(machine, act, now_ms);
        }
        if (bento_time_ticks_diff(now_ms, machine->drawn_ms) >=
            DRAW_MS) {
            redraw(machine, now_ms);
        }
        status = pause_ms(LOOP_MS);
    }
    return status;
}

/* The screen, then the model under the finally that releases it. */
static bento_status_t run_machine(machine_t *machine)
{
    bool started = false;
    bento_status_t status = build_screen(machine);

    if (status == BENTO_OK) {
        machine->drawn_ms = bento_time_ticks_ms();
        put(machine->ui, &machine->sub, "กำลังโหลดโมเดล รอสักครู่",
            COLOR_WARN);
        poll_drop(machine->ui);
        status = start_model(machine, &started);
        if ((status == BENTO_OK) && started) {
            status = run_panel(machine);
        }
        stop_model();
    }
    return status;
}

int main(void)
{
    machine_t *machine = &g_machine;
    bool supported = false;
    bool in_emulator = false;
    bool found = false;
    bento_status_t status;

    (void)memset(machine, 0, sizeof(*machine));
    machine->ui = bento_runtime_get_ui();
    machine->state = STATE_NORMAL;
    (void)snprintf(machine->label, sizeof(machine->label), "-");
    status = bento_edgeai_supported(&supported);
    in_emulator = status == BENTO_E_UNSUPPORTED;
    if (in_emulator) {
        print_emulator_note();
        status = BENTO_OK; /* on as if no model were on the board */
    }
    if ((status == BENTO_OK) && !supported && !in_emulator) {
        status = BENTO_E_UNSUPPORTED; /* import edge_ai fails */
    }
    if ((status == BENTO_OK) && !in_emulator) {
        status = find_model(machine, &found);
    }
    if ((status == BENTO_OK) && !found) {
        (void)printf("none of these models is on the board: "
                     "('AnomalousVibration', 'Motion Detection')\n");
        status = hold_no_model(machine->ui, in_emulator);
    } else if (status == BENTO_OK) {
        (void)printf("found %s at index %" PRIu32 "\n",
                     machine->model_name, machine->model);
        status = run_machine(machine);
    } else {
        /* an OSError from edge_ai.models() ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
