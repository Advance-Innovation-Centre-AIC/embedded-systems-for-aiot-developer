/*
 * 08_hmi_nursery.c -- Smart HMI โรงพยาบาล: จอเรียกพยาบาลห้องเด็กอ่อน
 * (a hospital HMI: a nursery nurse call driven by an AI that hears
 * babies cry)
 *
 * AI แทนปุ่มเรียกพยาบาลที่เด็กกดเองไม่ได้ สถานะ ทุกเตียงปกติ -> เด็กร้อง
 * (ยังไม่มีใครรับ คำกะพริบ) -> รับทราบแล้ว (กำลังไปดู) -> ปกติเองเมื่อเด็กหยุดร้อง
 * เชื่อผลด้วยความมั่นใจและกฎ 3 ใน 5 ซ้ายคือเคสด่วน เวลารอ (นาที:วินาที) ผลดิบ
 * ไฟ 5 ดวง และปุ่มรับทราบ ขวาคือ 6 เตียง (เตียง 4 = ไมค์ของบอร์ด) สีเตียงบอก
 * สถานะ ทุกครั้งที่สถานะเปลี่ยนจะพิมพ์ JSON หนึ่งบรรทัด กด Stop เพื่อจบ
 * ตัวอย่างเพื่อการเรียน ไม่ใช่อุปกรณ์การแพทย์
 *
 * The AI stands in for the call button a baby cannot press. All beds
 * normal -> baby crying (nobody has taken it yet, the word blinks) ->
 * acknowledged (a nurse is on the way) -> normal again by itself once
 * the crying stops. A result is trusted after a confidence floor and
 * a 3-of-5 rule. Left: the urgent case, the waiting time (min:s), the
 * raw result, five lamps and the acknowledge button. Right: six beds
 * (bed 4 is the board's microphone), coloured by state. Every state
 * change prints one JSON line. Press Stop to end. A teaching example,
 * not a medical device.
 *
 * Twin of 08_hmi_nursery.py
 * (embedded-systems-for-aiot-developer/examples/s13/08_hmi_nursery.py).
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
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_edgeai.h"
#include "bento_runtime.h"

/* The rule, the screen's rhythm and the panel's limits. */
enum {
    CONF_MIN = 60, /* a result less sure than this does not count */
    CONFIRM_N = 3, /* wins needed ... */
    WINDOW_N = 5,  /* ... among this many latest results */
    BED_COUNT = 6,
    LIVE = 3, /* the bed on the board's microphone (bed 4) */
    BED_COLUMNS = 2,
    DRAW_MS = 500,
    HOLD_S = 30, /* how long an error screen stays */
    HOLD_POLLS_PER_S = 5,
    HOLD_POLL_MS = 200,
    LOOP_MS = 50,
    SETTLE_MS = 150, /* let the cleared screen settle */
    OP_POLL_MS = 100,
    MS_PER_S = 1000,
    S_PER_MIN = 60,
    WAIT_S_MAX = 599, /* the clock stops at 9:59 */
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
    COLOR_KEY = 0x212431,
    COLOR_TEAL = 0x1E6B66
};

/* Layout on the 792 x 398 canvas: the case card on the left, the six
 * beds on the right in two columns, ending at y 304. */
enum {
    MARGIN_X = 24,
    CARD_TEXT_X = 40,
    TITLE_Y = 10,
    AI_X = 480,
    AI_Y = 14,
    CARD_Y = 48,
    CASE_W = 296,
    CASE_H = 328,
    CARD_RADIUS = 16,
    HEAD_Y = 60,
    WAIT_CAPTION_Y = 152,
    WAIT_X = 150,
    WAIT_Y = 146,
    RAW_Y = 192,
    DOT_Y = 226,
    DOT_STEP = 40,
    DOT_SIZE = 28,
    EVENTS_Y = 266,
    ACKNOWLEDGE_Y = 304,
    ACKNOWLEDGE_W = 264,
    ACKNOWLEDGE_H = 56,
    BED_X = 336,
    BED_STEP_X = 224,
    BED_STEP_Y = 88,
    BED_W = 208,
    BED_H = 80,
    BED_RADIUS = 12,
    BED_PAD = 16,
    BED_TEXT_DY = 12,
    NOTE_Y = 316,
    NOTE2_Y = 344,
    HOLD_X = 24,
    HOLD_Y = 24,
    HOLD_STEP = 44,
    SMALL_PX = 16,
    TEXT_PX = 20,
    HOLD_PX = 24,
    BIG_PX = 28
};

/* The nurse call's three states. */
typedef enum {
    STATE_NORMAL = 0,
    STATE_ALERT = 1,
    STATE_ACKNOWLEDGED = 2,
    STATE_COUNT = 3
} call_state_t;

/* The live bed's first line: its name and its microphone. */
static const char BED_LIVE[] = "เตียง 4  ไมค์ AI\n";

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
    shown_t head;
    shown_t wait;
    shown_t raw;
    shown_t events_label;
    shown_t acknowledge;
    shown_t bed_text;
    bento_ui_id_t bed_panel;
    bool bed_known;
    uint32_t bed_color;
    bento_ui_id_t dots[WINDOW_N];
    uint8_t hist[WINDOW_N];
    int32_t hist_count;
    bool alerting;
    int32_t lit[WINDOW_N];
    bool blink;
    bool has_sequence;
    uint32_t last_sequence;
    uint32_t drawn_ms;
    uint32_t cry_ms;
    uint32_t acknowledged_ms;
    char label[BENTO_EDGEAI_LABEL_LEN];
    int32_t conf;
    call_state_t state;
    int32_t events;
    uint32_t sequence;
} nursery_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern nursery_t g_nursery;
nursery_t g_nursery;

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

/* A panel with a rounded border of COLOR_LINE. */
static bento_status_t add_panel(bento_ui_t *ui, place_t place,
                                uint32_t color, int32_t radius,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    place_args(&args, place);
    args.color = color;
    args.min_val = COLOR_LINE;
    args.max_val = radius;
    return bento_ui_panel_create(ui, &args, out_id);
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

/* A crying class, named as the board reports it: Baby Cry Detection
 * and HomeSounds say "baby_cry", Environment Sounds "crying_baby". */
static bool is_danger(const char *label)
{
    static const char *const danger[DANGER_COUNT] = {"baby_cry",
                                                     "crying_baby"};

    return (strcmp(label, danger[0]) == 0) ||
           (strcmp(label, danger[1]) == 0);
}

/* The first model whose name matches, in the order of the names: the
 * built-in models first. */
static bento_status_t find_model(nursery_t *nursery, bool *found)
{
    static const char *const keys[MODEL_KEY_COUNT] = {
        "Baby Cry Detection", "HomeSounds", "Environment Sounds"};
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
            nursery->model = (uint32_t)row;
        }
    }
    if (*found) {
        (void)snprintf(nursery->model_name, sizeof(nursery->model_name),
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
         (poll < (HOLD_S * HOLD_POLLS_PER_S)) && (status == BENTO_OK);
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

static bento_status_t hold_no_model(bento_ui_t *ui)
{
    hold_text_t text;

    text.count = 0U;
    add_line(&text, "ไม่พบโมเดลบนบอร์ด");
    add_line(&text, "Baby Cry Detection หรือ HomeSounds");
    add_line(&text, "deploy HomeSounds จาก Store");
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
static bento_status_t start_model(const nursery_t *nursery,
                                  bool *started)
{
    hold_text_t text;
    const char *reason = "edge_ai: select not confirmed";
    bento_status_t status = bento_edgeai_select_start(nursery->model);

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
        status = hold(nursery->ui, &text, COLOR_BAD);
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

/* One bed: its card and its name with its state. */
static bento_status_t add_bed(nursery_t *nursery, int32_t bed)
{
    const int32_t columns = BED_COLUMNS;
    const int16_t x = (int16_t)(BED_X + ((bed % columns) * BED_STEP_X));
    const int16_t y =
        (int16_t)(CARD_Y + ((bed / columns) * BED_STEP_Y));
    char text[TEXT_CHARS];
    bento_ui_id_t bed_panel = 0U;
    bento_ui_id_t bed_text = 0U;
    bento_status_t status =
        add_panel(nursery->ui, (place_t){x, y, BED_W, BED_H}, COLOR_KEY,
                  BED_RADIUS, &bed_panel);

    if (status == BENTO_OK) {
        if (bed == LIVE) {
            (void)snprintf(text, sizeof(text), "%sปกติ", BED_LIVE);
        } else {
            (void)snprintf(text, sizeof(text), "เตียง %" PRId32 "\nปกติ",
                           bed + 1);
        }
        status = add_label(nursery->ui, text,
                           (label_look_t){(int16_t)(x + BED_PAD),
                                          (int16_t)(y + BED_TEXT_DY),
                                          COLOR_DIM, TEXT_PX},
                           &bed_text);
    }
    if (bed == LIVE) {
        nursery->bed_panel = bed_panel;
        nursery->bed_text.id = bed_text;
    }
    return status;
}

/* The six beds, two columns by three rows, and the two notes. */
static bento_status_t add_beds(nursery_t *nursery)
{
    bento_ui_id_t unused = 0U;
    bento_status_t status = BENTO_OK;
    int32_t bed;

    for (bed = 0; (bed < BED_COUNT) && (status == BENTO_OK); bed++) {
        status = add_bed(nursery, bed);
    }
    if (status == BENTO_OK) {
        status = add_label(
            nursery->ui, "ตัวอย่างเพื่อการเรียน",
            (label_look_t){BED_X, NOTE_Y, COLOR_DIM, TEXT_PX}, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            nursery->ui, "ไม่ใช่อุปกรณ์การแพทย์",
            (label_look_t){BED_X, NOTE2_Y, COLOR_DIM, TEXT_PX},
            &unused);
    }
    return status;
}

/* WINDOW_N lamps for the latest results (lit = crying). */
static bento_status_t add_dots(nursery_t *nursery)
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
            bento_ui_led_create(nursery->ui, &args, &nursery->dots[i]);
    }
    return status;
}

/* The case card's upper half: the head and the waiting time. */
static bento_status_t add_case_head(nursery_t *nursery)
{
    bento_ui_t *ui = nursery->ui;
    bento_ui_id_t unused = 0U;
    bento_status_t status =
        add_panel(ui, (place_t){MARGIN_X, CARD_Y, CASE_W, CASE_H},
                  COLOR_CARD, CARD_RADIUS, &unused);

    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){CARD_TEXT_X, HEAD_Y, COLOR_OK, BIG_PX},
            &nursery->head.id);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "รอมาแล้ว",
                           (label_look_t){CARD_TEXT_X, WAIT_CAPTION_Y,
                                          COLOR_DIM, TEXT_PX},
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "-:--",
            (label_look_t){WAIT_X, WAIT_Y, COLOR_TEXT, BIG_PX},
            &nursery->wait.id);
    }
    return status;
}

/* The case card's lower half: the raw result, the lamps, the count and
 * the acknowledge button. */
static bento_status_t add_case_foot(nursery_t *nursery)
{
    bento_ui_t *ui = nursery->ui;
    bento_ui_widget_args_t args;
    bento_status_t status = add_label(
        ui, "-", (label_look_t){CARD_TEXT_X, RAW_Y, COLOR_DIM, TEXT_PX},
        &nursery->raw.id);

    if (status == BENTO_OK) {
        status = add_dots(nursery);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){CARD_TEXT_X, EVENTS_Y, COLOR_TEXT, TEXT_PX},
            &nursery->events_label.id);
    }
    if (status == BENTO_OK) {
        place_args(&args, (place_t){CARD_TEXT_X, ACKNOWLEDGE_Y,
                                    ACKNOWLEDGE_W, ACKNOWLEDGE_H});
        args.text = "รับทราบ";
        args.text_len = (uint32_t)strlen(args.text);
        args.color = COLOR_KEY;
        args.font_px = TEXT_PX;
        status =
            bento_ui_button_create(ui, &args, &nursery->acknowledge.id);
    }
    return status;
}

static bento_status_t build_screen(nursery_t *nursery)
{
    char text[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(nursery->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            nursery->ui, "ห้องเด็กอ่อน  AI ฟังเสียงเด็กร้อง",
            (label_look_t){MARGIN_X, TITLE_Y, COLOR_TEXT, TEXT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "AI: %s",
                       nursery->model_name);
        status = add_label(
            nursery->ui, text,
            (label_look_t){AI_X, AI_Y, COLOR_INFO, SMALL_PX}, &unused);
    }
    if (status == BENTO_OK) {
        status = add_case_head(nursery);
    }
    if (status == BENTO_OK) {
        status = add_case_foot(nursery);
    }
    if (status == BENTO_OK) {
        status = add_beds(nursery);
    }
    if (status == BENTO_OK) {
        poll_drop(nursery->ui);
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
static void alert_rule(nursery_t *nursery, bool danger)
{
    const int32_t hits = push_flag(nursery->hist, &nursery->hist_count,
                                   WINDOW_N, danger);

    nursery->alerting =
        (hits >= CONFIRM_N) || (nursery->alerting && (hits > 0));
}

/* The next state. act: the main button was tapped. Normal comes back
 * by itself once acknowledged and the crying stopped (manual reset
 * off); a refused close needs nothing here. */
static call_state_t step(call_state_t state, bool alerting, bool act)
{
    const bool manual = false; /* MANUAL_RESET of the original */
    call_state_t next = STATE_ACKNOWLEDGED;

    if (state == STATE_NORMAL) {
        next = alerting ? STATE_ALERT : STATE_NORMAL;
    } else if (state == STATE_ALERT) {
        /* crying that stopped still alerts until someone takes it */
        next = act ? STATE_ACKNOWLEDGED : STATE_ALERT;
    } else if (!alerting && (act || !manual)) {
        next = STATE_NORMAL;
    } else {
        /* still crying: stays acknowledged */
    }
    return next;
}

/* Every AI result in one JSON line; session 12 publishes it on MQTT. */
static void print_snapshot(const nursery_t *nursery)
{
    static const char *const state_names[STATE_COUNT] = {
        "normal", "alert", "ack"};
    static const char team[] =
        "teamXX"; /* the team id the teacher gives */
    static const char kit[] = "nursery";

    (void)printf("{\"id\": \"%s\", \"kit\": \"%s\", \"model\": \"%s\", "
                 "\"label\": \"%s\", \"conf\": %" PRId32 ", \"state\": "
                 "\"%s\", \"events\": %" PRId32 ", \"n\": %" PRIu32
                 "}\n",
                 team, kit, nursery->model_name, nursery->label,
                 nursery->conf, state_names[nursery->state],
                 nursery->events, nursery->sequence);
}

/* The waiting time and the live bed's words. Once acknowledged the
 * clock stops at the response time. */
static const char *draw_wait(nursery_t *nursery, bool alerting,
                             uint32_t now_ms)
{
    const call_state_t state = nursery->state;
    const char *bed = "ปกติ";
    char text[TEXT_CHARS];

    if (state == STATE_NORMAL) {
        put(nursery->ui, &nursery->wait, "-:--", COLOR_DIM);
    } else {
        const uint32_t end_ms = (state == STATE_ACKNOWLEDGED)
                                    ? nursery->acknowledged_ms
                                    : now_ms;
        int32_t wait_s =
            bento_time_ticks_diff(end_ms, nursery->cry_ms) / MS_PER_S;

        if (wait_s > WAIT_S_MAX) {
            wait_s = WAIT_S_MAX;
        }
        (void)snprintf(text, sizeof(text), "%" PRId32 ":%02" PRId32,
                       wait_s / S_PER_MIN, wait_s % S_PER_MIN);
        put(nursery->ui, &nursery->wait, text, COLOR_TEXT);
        if (state == STATE_ACKNOWLEDGED) {
            bed = "พยาบาลกำลังไป";
        } else {
            bed = alerting ? "เด็กร้อง" : "รอรับทราบ";
        }
    }
    return bed;
}

/* The live bed's card colour, sent only when it changes. */
static void draw_bed_panel(nursery_t *nursery)
{
    static const uint32_t bed_colors[STATE_COUNT] = {
        COLOR_KEY, COLOR_BAD, COLOR_TEAL};
    const uint32_t color = bed_colors[nursery->state];

    if (!nursery->bed_known || (nursery->bed_color != color)) {
        nursery->bed_known = true;
        nursery->bed_color = color;
        (void)bento_ui_set_color(nursery->ui, nursery->bed_panel,
                                 color);
    }
}

/* The whole panel; the head blinks while nobody has acknowledged. */
static void draw(nursery_t *nursery, bool alerting, uint32_t now_ms)
{
    static const char *const heads[STATE_COUNT] = {
        "ทุกเตียงปกติ", "เตียง 4\nเด็กร้อง", "เตียง 4\nรับทราบแล้ว"};
    static const uint32_t head_colors[STATE_COUNT] = {
        COLOR_OK, COLOR_BAD, COLOR_TEAL};
    const call_state_t state = nursery->state;
    const bool dim = (state == STATE_ALERT) && nursery->blink;
    const char *bed;
    char text[TEXT_CHARS];

    put(nursery->ui, &nursery->head, heads[state],
        dim ? COLOR_DIM : head_colors[state]);
    bed = draw_wait(nursery, alerting, now_ms);
    (void)snprintf(text, sizeof(text), "%s%s", BED_LIVE, bed);
    put(nursery->ui, &nursery->bed_text, text,
        (state == STATE_NORMAL) ? COLOR_DIM : COLOR_TEXT);
    draw_bed_panel(nursery);
    put(nursery->ui, &nursery->acknowledge,
        (state == STATE_ACKNOWLEDGED) ? "กำลังไปดู" : "รับทราบ",
        (state == STATE_ALERT) ? COLOR_WARN : COLOR_KEY);
    (void)snprintf(text, sizeof(text), "เด็กร้อง %" PRId32 " ครั้ง",
                   nursery->events);
    put(nursery->ui, &nursery->events_label, text, COLOR_TEXT);
}

/* Whether the acknowledge button was tapped in this poll. */
static bool read_tap(const nursery_t *nursery)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bool act = false;

    /* ui.poll() answers no events when it fails. */
    if (bento_ui_poll(nursery->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if ((events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) &&
            (events[i].id == nursery->acknowledge.id)) {
            act = true;
        }
    }
    return act;
}

/* A new result of this model with a label feeds the rule. A failed
 * read is passed over this round. */
static bool take_result(nursery_t *nursery)
{
    bento_edgeai_result_t result;
    bool fresh;

    bento_edgeai_result_init(&result);
    fresh = (bento_edgeai_result(&result) == BENTO_OK) &&
            result.has_verdict && (result.index == nursery->model) &&
            (!nursery->has_sequence ||
             (result.seq != nursery->last_sequence)) &&
            (result.top < BENTO_EDGEAI_MAX_CLASSES);
    if (fresh) {
        const int32_t pct = (int32_t)result.score_pct[result.top];

        nursery->has_sequence = true;
        nursery->last_sequence = result.seq;
        if (bento_edgeai_label(result.index, result.top, nursery->label,
                               (uint32_t)sizeof(nursery->label),
                               NULL) != BENTO_OK) {
            nursery->label[0] = '\0';
        }
        alert_rule(nursery,
                   is_danger(nursery->label) && (pct >= CONF_MIN));
        nursery->conf = pct;
        nursery->sequence = result.seq;
    }
    return fresh;
}

/* A new result or a tap moves the state; a change is printed. */
static void update_state(nursery_t *nursery, bool act, uint32_t now_ms)
{
    const call_state_t was = nursery->state;

    nursery->state = step(was, nursery->alerting, act);
    if (nursery->state != was) {
        if (nursery->state == STATE_ALERT) {
            nursery->events++;
            nursery->cry_ms = now_ms;
        }
        if (nursery->state == STATE_ACKNOWLEDGED) {
            nursery->acknowledged_ms = now_ms;
        }
        print_snapshot(nursery);
        draw(nursery, nursery->alerting, now_ms);
    }
}

/* Only the lamps that changed are sent. */
static void draw_dots(nursery_t *nursery)
{
    int32_t i;

    for (i = 0; i < WINDOW_N; i++) {
        const int32_t value =
            (i < nursery->hist_count) ? (int32_t)nursery->hist[i] : 0;

        if (value != nursery->lit[i]) {
            (void)bento_ui_set_value(nursery->ui, nursery->dots[i],
                                     value);
            nursery->lit[i] = value;
        }
    }
}

/* Every DRAW_MS: the blink, the panel, the raw result and the lamps. */
static void redraw(nursery_t *nursery, uint32_t now_ms)
{
    char text[TEXT_CHARS];

    nursery->drawn_ms = now_ms;
    nursery->blink = !nursery->blink;
    draw(nursery, nursery->alerting, now_ms);
    (void)snprintf(text, sizeof(text), "ได้ยิน: %s  %" PRId32 "%%",
                   nursery->label, nursery->conf);
    put(nursery->ui, &nursery->raw, text, COLOR_DIM);
    draw_dots(nursery);
}

/* The nurse call's loop; only Stop or an error ends it. */
static bento_status_t run_call(nursery_t *nursery)
{
    bento_status_t status = BENTO_OK;

    draw(nursery, nursery->alerting, nursery->drawn_ms);
    while (status == BENTO_OK) {
        const uint32_t now_ms = bento_time_ticks_ms();
        const bool act = read_tap(nursery);
        const bool fresh = take_result(nursery);

        if (fresh || act) {
            update_state(nursery, act, now_ms);
        }
        if (bento_time_ticks_diff(now_ms, nursery->drawn_ms) >=
            DRAW_MS) {
            redraw(nursery, now_ms);
        }
        status = pause_ms(LOOP_MS);
    }
    return status;
}

/* The screen, then the model under the finally that releases it. */
static bento_status_t run_nursery(nursery_t *nursery)
{
    bool started = false;
    bento_status_t status = build_screen(nursery);

    if (status == BENTO_OK) {
        nursery->drawn_ms = bento_time_ticks_ms();
        nursery->cry_ms = nursery->drawn_ms;
        nursery->acknowledged_ms = nursery->drawn_ms;
        put(nursery->ui, &nursery->raw, "กำลังโหลดโมเดล รอสักครู่",
            COLOR_WARN);
        poll_drop(nursery->ui);
        status = start_model(nursery, &started);
        if ((status == BENTO_OK) && started) {
            status = run_call(nursery);
        }
        stop_model();
    }
    return status;
}

int main(void)
{
    nursery_t *nursery = &g_nursery;
    bool supported = false;
    bool found = false;
    bento_status_t status;

    (void)memset(nursery, 0, sizeof(*nursery));
    nursery->ui = bento_runtime_get_ui();
    nursery->state = STATE_NORMAL;
    (void)snprintf(nursery->label, sizeof(nursery->label), "-");
    status = bento_edgeai_supported(&supported);
    if ((status == BENTO_OK) && !supported) {
        status = BENTO_E_UNSUPPORTED; /* import edge_ai fails */
    }
    if (status == BENTO_OK) {
        status = find_model(nursery, &found);
    }
    if ((status == BENTO_OK) && !found) {
        (void)printf(
            "none of these models is on the board: ('Baby Cry "
            "Detection', 'HomeSounds', 'Environment Sounds')\n");
        status = hold_no_model(nursery->ui);
    } else if (status == BENTO_OK) {
        (void)printf("found %s at index %" PRIu32 "\n",
                     nursery->model_name, nursery->model);
        status = run_nursery(nursery);
    } else {
        /* an OSError from edge_ai.models() ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
