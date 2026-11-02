/*
 * 07_hmi_safety.c -- Smart HMI อาคาร: แผงแจ้งเหตุที่ AI ฟังเสียงสัญญาณเตือน
 * (a building HMI: an alarm panel driven by an AI that hears alarms)
 *
 * แผงแจ้งเหตุจริงแยก "รับทราบ" ออกจาก "รีเซ็ต" คนละปุ่ม คนละความหมาย สถานะ
 * ระบบปกติ -> ได้ยินเสียงเตือน (คำกะพริบ) -> รับทราบแล้ว (หยุดกะพริบ โซนยังแดง)
 * -> ปกติ เมื่อกด "รีเซ็ต" และไม่ได้ยินเสียงแล้วเท่านั้น เชื่อผลด้วยความมั่นใจและ
 * กฎ 3 ใน 5 ซ้ายคือผังอาคาร 6 โซน (ห้องคอม 202 = ไมค์ของบอร์ด) ขวาคือสถานะ ผลดิบ
 * ไฟ 5 ดวง ปุ่มรับทราบและรีเซ็ต บรรทัดเหนือปุ่มบอกเหตุผลเมื่อกดแล้วไม่เกิดอะไร
 * ทุกครั้งที่สถานะเปลี่ยนจะพิมพ์ JSON หนึ่งบรรทัด กด Stop เพื่อจบ
 *
 * A real alarm panel keeps "acknowledge" apart from "reset": two
 * buttons, two meanings. System normal -> alarm heard (the word
 * blinks) -> acknowledged (no blinking, the zone stays red) -> normal,
 * only after "reset" is tapped and the alarm is no longer heard. A
 * result is trusted after a confidence floor and a 3-of-5 rule. Left:
 * a six-zone building plan (computer room 202 is the board's
 * microphone). Right: the state, the raw result, five lamps and the
 * two buttons, with a line above them saying why a tap did nothing.
 * Every state change prints one JSON line. Press Stop to end.
 *
 * Twin of 07_hmi_safety.py
 * (embedded-systems-for-aiot-developer/examples/s13/07_hmi_safety.py).
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - DEMO is False in the original, and edge_ai.inject() and events()
 *    have no C API yet (bento_edgeai README: to do), so the demo
 *    section is left out: this is the program with DEMO = False;
 *  - edge_ai.models() is bento_edgeai_count() and model_info() per
 *    row, read once for each wanted name; a row's index is its place,
 *    and its labels are bento_edgeai_label() for each class;
 *  - a result's conf and scores are score_pct[] in percent, so
 *    int(conf * 100) and int(score * 100 + 0.5) are those numbers;
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
static const char KIT[] = "safety";
/* The rule, the screen's rhythm and the error screen. */
enum {
    MANUAL_RESET = 1, /* back to normal only by "reset" (1 = True) */
    CONF_MIN = 60,
    CONFIRM_N = 3,
    WINDOW_N = 5,
    LIVE = 3, /* the zone on the board's microphone (place in ZONES) */
    DRAW_MS = 500,
    NOTE_MS = 3000,
    HOLD_S = 30,    /* how long an error screen stays */
    DEMO = 0,       /* 1 = simulated results: the C has no demo yet */
    DEMO_PCT = 95,  /* the simulated result's percent ... */
    DEMO_STEPS = 60 /* ... and how many model results it lasts */
};
/* ---- end of settings ---- */

/* The note for the BENTO Emulator, whose C runtime has no Edge AI yet. */
static const char EMULATOR_NOTE[] =
    "Edge AI จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";

/* The rule, the screen's rhythm and the panel's limits. */
enum {
    SIREN_CONF =
        45, /* the firmware's confirm level for Siren Detection */
    ZONE_COUNT = 6,
    ZONE_COLUMNS = 2,
    HOLD_POLLS_PER_S = 5,
    HOLD_POLL_MS = 200,
    LOOP_MS = 50,
    SETTLE_MS = 150, /* let the cleared screen settle */
    OP_POLL_MS = 100,
    MS_PER_S = 1000,
    S_PER_MIN = 60,
    ALARM_S_MAX = 599, /* the clock stops at 9:59 */
    NO_CLASS = -1,
    MODEL_KEY_COUNT = 3,
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

/* Layout on the 792 x 398 canvas: the building plan on the left (two
 * columns, the top floor on top), the state card and the buttons on
 * the right; the note sits above the buttons, clear of the Console. */
enum {
    MARGIN_X = 24,
    CARD_TEXT_X = 40,
    TITLE_Y = 10,
    AI_X = 480,
    AI_Y = 14,
    CARD_Y = 48,
    PLAN_W = 416,
    PLAN_H = 328,
    CARD_RADIUS = 16,
    ZONE_Y = 60,
    ZONE_STEP_X = 196,
    ZONE_STEP_Y = 92,
    ZONE_W = 188,
    ZONE_H = 84,
    ZONE_RADIUS = 12,
    ZONE_PAD = 12,
    MIC_Y = 340,
    STATE_CARD_X = 456,
    STATE_CARD_W = 312,
    STATE_CARD_H = 180,
    STATE_TEXT_X = 472,
    WORD_Y = 60,
    SUB_Y = 100,
    RAW_Y = 160,
    DOT_Y = 196,
    DOT_STEP = 32,
    DOT_SIZE = 24,
    COUNT_X = 640,
    COUNT_Y = 194,
    NOTE_Y = 236,
    KEY_Y = 272,
    KEY_H = 52,
    ACKNOWLEDGE_W = 200,
    RESET_X = 664,
    RESET_W = 104,
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

/* A tap on one of the two buttons. */
typedef enum {
    TAP_NONE = 0,
    TAP_ACKNOWLEDGE = 1,
    TAP_RESET = 2
} tap_t;

/* Floors 3 down to 1; shown in a plan, and other zones only display. */
static const char *const ZONES[ZONE_COUNT] = {
    "ห้องสมุด 301", "ห้องประชุม 302", "ห้องเรียน 201",
    "ห้องคอม 202", "โรงอาหาร 101", "ทางหนีไฟ 102"};
static const char HELP[] = "รับทราบก่อน แล้วรีเซ็ต";

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
 * when it changes. */
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
    int32_t danger_class; /* the first danger class, or NO_CLASS */
    int32_t conf_min;
    shown_t word;
    shown_t sub;
    shown_t raw;
    shown_t count_label;
    shown_t note_label;
    shown_t acknowledge;
    shown_t reset;
    shown_t zone_text;
    bento_ui_id_t zone_panel;
    bool zone_known;
    uint32_t zone_color;
    bento_ui_id_t dots[WINDOW_N];
    uint8_t hist[WINDOW_N];
    int32_t hist_count;
    bool alerting;
    int32_t lit[WINDOW_N];
    bool blink;
    bool has_sequence;
    uint32_t last_sequence;
    const char *note;
    uint32_t note_ms;
    uint32_t drawn_ms;
    uint32_t alarm_ms;
    char label[BENTO_EDGEAI_LABEL_LEN];
    int32_t conf;
    panel_state_t state;
    int32_t events;
    uint32_t sequence;
} safety_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern safety_t g_safety;
safety_t g_safety;

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
static bento_status_t add_card(bento_ui_t *ui, place_t place,
                               int32_t radius)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    place_args(&args, place);
    args.color = COLOR_CARD;
    args.min_val = COLOR_LINE;
    args.max_val = radius;
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

static bool is_danger(const char *label)
{
    static const char *const danger[DANGER_COUNT] = {"alarm", "sirens"};

    return (strcmp(label, danger[0]) == 0) ||
           (strcmp(label, danger[1]) == 0);
}

/* The first class of this model whose label is a danger class. */
static int32_t danger_class_of(uint32_t model, uint32_t class_count)
{
    char label[BENTO_EDGEAI_LABEL_LEN];
    int32_t found = NO_CLASS;
    uint32_t i;

    for (i = 0U; (i < class_count) && (found == NO_CLASS); i++) {
        if ((bento_edgeai_label(model, i, label,
                                (uint32_t)sizeof(label),
                                NULL) == BENTO_OK) &&
            is_danger(label)) {
            found = (int32_t)i;
        }
    }
    return found;
}

/* The first model whose name matches, in the order of the names: the
 * built-in models first, the last one from the Store. */
static bento_status_t find_model(safety_t *panel, bool *found)
{
    static const char *const keys[MODEL_KEY_COUNT] = {
        "Alarm Detection", "Siren Detection", "SirenDetection"};
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
            panel->model = (uint32_t)row;
        }
    }
    if (*found) {
        (void)snprintf(panel->model_name, sizeof(panel->model_name),
                       "%s", info.name);
        panel->danger_class =
            danger_class_of(panel->model, info.class_count);
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
    add_line(&text, "Alarm Detection หรือ Siren Detection");
    add_line(&text, "deploy SirenDetection จาก Store");
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
static bento_status_t start_model(const safety_t *panel, bool *started)
{
    hold_text_t text;
    const char *reason = "edge_ai: select not confirmed";
    bento_status_t status = bento_edgeai_select_start(panel->model);

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
        status = hold(panel->ui, &text, COLOR_BAD);
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

/* One zone of the plan: its card and its name with its state. */
static bento_status_t add_zone(safety_t *panel, int32_t zone)
{
    const int32_t columns = ZONE_COLUMNS;
    const int32_t column = zone % columns;
    const int32_t storey = zone / columns;
    const int16_t x = (int16_t)(CARD_TEXT_X + (column * ZONE_STEP_X));
    const int16_t y = (int16_t)(ZONE_Y + (storey * ZONE_STEP_Y));
    bento_ui_widget_args_t args;
    char text[TEXT_CHARS];
    bento_ui_id_t zone_panel = 0U;
    bento_ui_id_t zone_text = 0U;
    bento_status_t status;

    place_args(&args, (place_t){x, y, ZONE_W, ZONE_H});
    args.color = COLOR_KEY;
    args.min_val = COLOR_LINE;
    args.max_val = ZONE_RADIUS;
    status = bento_ui_panel_create(panel->ui, &args, &zone_panel);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%s\nปกติ", ZONES[zone]);
        status = add_label(panel->ui, text,
                           (label_look_t){(int16_t)(x + ZONE_PAD),
                                          (int16_t)(y + ZONE_PAD),
                                          COLOR_DIM, TEXT_PX},
                           &zone_text);
    }
    if (zone == LIVE) {
        panel->zone_panel = zone_panel;
        panel->zone_text.id = zone_text;
    }
    return status;
}

/* The building plan: two columns by three floors, the top floor on
 * top. */
static bento_status_t add_plan(safety_t *panel)
{
    bento_ui_id_t unused = 0U;
    int32_t zone;
    bento_status_t status =
        add_card(panel->ui, (place_t){MARGIN_X, CARD_Y, PLAN_W, PLAN_H},
                 CARD_RADIUS);

    for (zone = 0; (zone < ZONE_COUNT) && (status == BENTO_OK);
         zone++) {
        status = add_zone(panel, zone);
    }
    if (status == BENTO_OK) {
        status = add_label(
            panel->ui, "ไมค์ของบอร์ด = ห้องคอม 202",
            (label_look_t){CARD_TEXT_X, MIC_Y, COLOR_DIM, TEXT_PX},
            &unused);
    }
    return status;
}

/* WINDOW_N lamps for the latest results (lit = danger). */
static bento_status_t add_dots(safety_t *panel)
{
    bento_ui_widget_args_t args;
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < WINDOW_N) && (status == BENTO_OK); i++) {
        const int16_t x = (int16_t)(STATE_TEXT_X + (i * DOT_STEP));

        place_args(&args, (place_t){x, DOT_Y, DOT_SIZE, DOT_SIZE});
        args.color = COLOR_BAD;
        args.font_px = 0;
        status = bento_ui_led_create(panel->ui, &args, &panel->dots[i]);
    }
    return status;
}

/* The state card: the word, the sub line, the raw result, the lamps
 * and the count. */
static bento_status_t add_state_card(safety_t *panel)
{
    bento_ui_t *ui = panel->ui;
    bento_status_t status = add_card(
        ui, (place_t){STATE_CARD_X, CARD_Y, STATE_CARD_W, STATE_CARD_H},
        CARD_RADIUS);

    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){STATE_TEXT_X, WORD_Y, COLOR_OK, BIG_PX},
            &panel->word.id);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){STATE_TEXT_X, SUB_Y, COLOR_DIM, TEXT_PX},
            &panel->sub.id);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "-",
            (label_look_t){STATE_TEXT_X, RAW_Y, COLOR_DIM, TEXT_PX},
            &panel->raw.id);
    }
    if (status == BENTO_OK) {
        status = add_dots(panel);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){COUNT_X, COUNT_Y, COLOR_TEXT, TEXT_PX},
            &panel->count_label.id);
    }
    return status;
}

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 place_t place, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    place_args(&args, place);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.color = COLOR_KEY;
    args.font_px = TEXT_PX;
    return bento_ui_button_create(ui, &args, out_id);
}

/* The note above the two buttons, then the buttons. */
static bento_status_t add_keys(safety_t *panel)
{
    bento_status_t status = add_label(
        panel->ui, HELP,
        (label_look_t){STATE_CARD_X, NOTE_Y, COLOR_DIM, TEXT_PX},
        &panel->note_label.id);

    if (status == BENTO_OK) {
        status = add_button(
            panel->ui, "รับทราบ",
            (place_t){STATE_CARD_X, KEY_Y, ACKNOWLEDGE_W, KEY_H},
            &panel->acknowledge.id);
    }
    if (status == BENTO_OK) {
        status = add_button(panel->ui, "รีเซ็ต",
                            (place_t){RESET_X, KEY_Y, RESET_W, KEY_H},
                            &panel->reset.id);
    }
    return status;
}

static bento_status_t build_screen(safety_t *panel)
{
    char text[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(panel->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            panel->ui, "แผงแจ้งเหตุ อาคารเรียน 3 ชั้น",
            (label_look_t){MARGIN_X, TITLE_Y, COLOR_TEXT, TEXT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "AI: %s", panel->model_name);
        status = add_label(
            panel->ui, text,
            (label_look_t){AI_X, AI_Y, COLOR_INFO, SMALL_PX}, &unused);
    }
    if (status == BENTO_OK) {
        status = add_plan(panel);
    }
    if (status == BENTO_OK) {
        status = add_state_card(panel);
    }
    if (status == BENTO_OK) {
        status = add_keys(panel);
    }
    if (status == BENTO_OK) {
        poll_drop(panel->ui);
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
static void alert_rule(safety_t *panel, bool danger)
{
    const int32_t hits =
        push_flag(panel->hist, &panel->hist_count, WINDOW_N, danger);

    panel->alerting =
        (hits >= CONFIRM_N) || (panel->alerting && (hits > 0));
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
static void print_snapshot(const safety_t *panel)
{
    static const char *const state_names[STATE_COUNT] = {
        "normal", "alert", "ack"};

    (void)printf(
        "{\"id\": \"%s\", \"kit\": \"%s\", \"model\": \"%s\", "
        "\"label\": \"%s\", \"conf\": %" PRId32 ", \"state\": "
        "\"%s\", \"events\": %" PRId32 ", \"n\": %" PRIu32 "}\n",
        TEAM, KIT, panel->model_name, panel->label, panel->conf,
        state_names[panel->state], panel->events, panel->sequence);
}

/* The sub line: all zones normal, or which zone alarms and for how
 * long. */
static void draw_sub(safety_t *panel, uint32_t now_ms)
{
    char text[TEXT_CHARS];

    if (panel->state == STATE_NORMAL) {
        put(panel->ui, &panel->sub, "ทุกโซนปกติ", COLOR_DIM);
    } else {
        int32_t alarm_s =
            bento_time_ticks_diff(now_ms, panel->alarm_ms) / MS_PER_S;

        if (alarm_s > ALARM_S_MAX) {
            alarm_s = ALARM_S_MAX;
        }
        (void)snprintf(
            text, sizeof(text), "%s\nเตือนมาแล้ว %" PRId32 ":%02" PRId32,
            ZONES[LIVE], alarm_s / S_PER_MIN, alarm_s % S_PER_MIN);
        put(panel->ui, &panel->sub, text, COLOR_TEXT);
    }
}

/* The live zone stays red until the reset (a latch). */
static void draw_zone(safety_t *panel, bool alerting)
{
    const bool normal = panel->state == STATE_NORMAL;
    const uint32_t zone_color = normal ? COLOR_KEY : COLOR_BAD;
    const char *heard = "เสียงหยุดแล้ว";
    char text[TEXT_CHARS];

    if (normal) {
        heard = "ปกติ";
    } else if (alerting) {
        heard = "ได้ยินเสียง";
    } else {
        /* acknowledged or alerting, and the sound has stopped */
    }
    (void)snprintf(text, sizeof(text), "%s\n%s", ZONES[LIVE], heard);
    put(panel->ui, &panel->zone_text, text,
        normal ? COLOR_DIM : COLOR_TEXT);
    if (!panel->zone_known || (panel->zone_color != zone_color)) {
        panel->zone_known = true;
        panel->zone_color = zone_color;
        (void)bento_ui_set_color(panel->ui, panel->zone_panel,
                                 zone_color);
    }
}

/* The whole panel; the word blinks while nobody has acknowledged. */
static void draw(safety_t *panel, bool alerting, uint32_t now_ms)
{
    static const char *const words[STATE_COUNT] = {
        "ระบบปกติ", "ได้ยินเสียงเตือน", "รับทราบแล้ว"};
    static const uint32_t word_colors[STATE_COUNT] = {
        COLOR_OK, COLOR_BAD, COLOR_WARN};
    const panel_state_t state = panel->state;
    const bool dim = (state == STATE_ALERT) && panel->blink;
    const bool can_reset = (state == STATE_ACKNOWLEDGED) && !alerting;
    char text[TEXT_CHARS];

    put(panel->ui, &panel->word, words[state],
        dim ? COLOR_DIM : word_colors[state]);
    draw_sub(panel, now_ms);
    draw_zone(panel, alerting);
    put(panel->ui, &panel->acknowledge, "รับทราบ",
        (state == STATE_ALERT) ? COLOR_WARN : COLOR_KEY);
    put(panel->ui, &panel->reset, "รีเซ็ต",
        can_reset ? COLOR_INFO : COLOR_KEY);
    (void)snprintf(text, sizeof(text), "เหตุ %" PRId32 " ครั้ง",
                   panel->events);
    put(panel->ui, &panel->count_label, text, COLOR_TEXT);
    put(panel->ui, &panel->note_label,
        (panel->note != NULL) ? panel->note : HELP,
        (panel->note != NULL) ? COLOR_BAD : COLOR_DIM);
}

/* The last tap on one of the two buttons in this poll. */
static tap_t read_tap(const safety_t *panel)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    tap_t tap = TAP_NONE;

    /* ui.poll() answers no events when it fails. */
    if (bento_ui_poll(panel->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        const bool clicked =
            events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED;

        if (clicked && (events[i].id == panel->acknowledge.id)) {
            tap = TAP_ACKNOWLEDGE;
        } else if (clicked && (events[i].id == panel->reset.id)) {
            tap = TAP_RESET;
        } else {
            /* another widget or kind: the last tap stays */
        }
    }
    return tap;
}

/* The danger class's own score, not only the winning label: a siren
 * at 45 % may lose to "unlabelled". */
static int32_t danger_score(const safety_t *panel,
                            const bento_edgeai_result_t *result,
                            int32_t pct)
{
    int32_t score = is_danger(panel->label) ? pct : 0;

    if ((panel->danger_class != NO_CLASS) &&
        (panel->danger_class < (int32_t)result->class_count) &&
        (panel->danger_class < (int32_t)BENTO_EDGEAI_MAX_CLASSES)) {
        score = (int32_t)result->score_pct[panel->danger_class];
    }
    return score;
}

/* A new result of this model with a label feeds the rule. A failed
 * read is passed over this round. */
static bool take_result(safety_t *panel)
{
    bento_edgeai_result_t result;
    bool fresh;

    bento_edgeai_result_init(&result);
    fresh = (bento_edgeai_result(&result) == BENTO_OK) &&
            result.has_verdict && (result.index == panel->model) &&
            (!panel->has_sequence ||
             (result.seq != panel->last_sequence)) &&
            (result.top < BENTO_EDGEAI_MAX_CLASSES);
    if (fresh) {
        const int32_t pct = (int32_t)result.score_pct[result.top];

        panel->has_sequence = true;
        panel->last_sequence = result.seq;
        if (bento_edgeai_label(result.index, result.top, panel->label,
                               (uint32_t)sizeof(panel->label),
                               NULL) != BENTO_OK) {
            panel->label[0] = '\0';
        }
        alert_rule(panel, danger_score(panel, &result, pct) >=
                              panel->conf_min);
        panel->conf = pct;
        panel->sequence = result.seq;
    }
    return fresh;
}

/* Acknowledge works only while alerting, reset only after it; a reset
 * during the alert says why it did nothing. */
static bool tap_acts(safety_t *panel, tap_t tap, uint32_t now_ms)
{
    bool act = false;

    if (tap == TAP_ACKNOWLEDGE) {
        act = panel->state == STATE_ALERT;
    } else if (tap == TAP_RESET) {
        act = panel->state == STATE_ACKNOWLEDGED;
        if (panel->state == STATE_ALERT) {
            panel->note = "ต้องรับทราบก่อน";
            panel->note_ms = now_ms;
        }
    } else {
        /* no tap */
    }
    return act;
}

/* A new result or a tap moves the state; a change is printed. */
static void update_state(safety_t *panel, tap_t tap, bool act,
                         uint32_t now_ms)
{
    const panel_state_t was = panel->state;
    bool refused = false;

    panel->state = step(was, panel->alerting, act, &refused);
    if (refused) {
        panel->note = "ยังได้ยินเสียง รีเซ็ตไม่ได้";
        panel->note_ms = now_ms;
    }
    if (panel->state != was) {
        if (panel->state == STATE_ALERT) {
            panel->events++;
            panel->alarm_ms = now_ms;
        }
        if (panel->state != STATE_ALERT) {
            panel->note = NULL;
        }
        print_snapshot(panel);
    }
    if ((tap != TAP_NONE) || (panel->state != was)) {
        draw(panel, panel->alerting, now_ms);
    }
}

/* Only the lamps that changed are sent. */
static void draw_dots(safety_t *panel)
{
    int32_t i;

    for (i = 0; i < WINDOW_N; i++) {
        const int32_t value =
            (i < panel->hist_count) ? (int32_t)panel->hist[i] : 0;

        if (value != panel->lit[i]) {
            (void)bento_ui_set_value(panel->ui, panel->dots[i], value);
            panel->lit[i] = value;
        }
    }
}

/* Every DRAW_MS: the blink, the note's time, the panel, the raw result
 * and the lamps. */
static void redraw(safety_t *panel, uint32_t now_ms)
{
    char text[TEXT_CHARS];

    panel->drawn_ms = now_ms;
    panel->blink = !panel->blink;
    if ((panel->note != NULL) &&
        (bento_time_ticks_diff(now_ms, panel->note_ms) >= NOTE_MS)) {
        panel->note = NULL;
    }
    draw(panel, panel->alerting, now_ms);
    (void)snprintf(text, sizeof(text), "ได้ยิน: %s %" PRId32 "%%",
                   panel->label, panel->conf);
    put(panel->ui, &panel->raw, text, COLOR_DIM);
    draw_dots(panel);
}

/* The panel's loop; only Stop or an error ends it. */
static bento_status_t run_panel(safety_t *panel)
{
    bento_status_t status = BENTO_OK;

    draw(panel, panel->alerting, panel->drawn_ms);
    while (status == BENTO_OK) {
        const uint32_t now_ms = bento_time_ticks_ms();
        const tap_t tap = read_tap(panel);
        const bool act = tap_acts(panel, tap, now_ms);
        const bool fresh = take_result(panel);

        if (fresh || (tap != TAP_NONE)) {
            update_state(panel, tap, act, now_ms);
        }
        if (bento_time_ticks_diff(now_ms, panel->drawn_ms) >= DRAW_MS) {
            redraw(panel, now_ms);
        }
        status = pause_ms(LOOP_MS);
    }
    return status;
}

/* The screen, then the model under the finally that releases it. */
static bento_status_t run_safety(safety_t *panel)
{
    bool started = false;
    bento_status_t status = build_screen(panel);

    if (status == BENTO_OK) {
        panel->drawn_ms = bento_time_ticks_ms();
        panel->alarm_ms = panel->drawn_ms;
        put(panel->ui, &panel->note_label, "กำลังโหลดโมเดล รอสักครู่",
            COLOR_WARN);
        poll_drop(panel->ui);
        status = start_model(panel, &started);
        if ((status == BENTO_OK) && started) {
            status = run_panel(panel);
        }
        stop_model();
    }
    return status;
}

int main(void)
{
    safety_t *panel = &g_safety;
    bool supported = false;
    bool in_emulator = false;
    bool found = false;
    bento_status_t status;

    (void)memset(panel, 0, sizeof(*panel));
    panel->ui = bento_runtime_get_ui();
    panel->state = STATE_NORMAL;
    panel->danger_class = NO_CLASS;
    (void)snprintf(panel->label, sizeof(panel->label), "-");
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
        status = find_model(panel, &found);
    }
    if ((status == BENTO_OK) && !found) {
        (void)printf(
            "none of these models is on the board: ('Alarm "
            "Detection', 'Siren Detection', 'SirenDetection')\n");
        status = hold_no_model(panel->ui, in_emulator);
    } else if (status == BENTO_OK) {
        panel->conf_min =
            (strcmp(panel->model_name, "Siren Detection") == 0)
                ? (int32_t)SIREN_CONF
                : (int32_t)CONF_MIN;
        (void)printf("found %s at index %" PRIu32 " | conf_min %" PRId32
                     "\n",
                     panel->model_name, panel->model, panel->conf_min);
        status = run_safety(panel);
    } else {
        /* an OSError from edge_ai.models() ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
