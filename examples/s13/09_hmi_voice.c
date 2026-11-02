/*
 * 09_hmi_voice.c -- Smart HMI บ้าน: สั่งไฟด้วยเสียง ด้วยโมเดล Voice Commands
 * (a home HMI: lights by voice with the Voice Commands model)
 *
 * เอาผล AI ที่เป็น "คำสั่ง" ไปขับจอ HMI ของบ้าน left / right = เลือกห้อง
 * up / down = ปรับความสว่างทีละ 10% go = เปิดไฟ stop = ปิดไฟ กรองด้วยความมั่นใจ
 * และคำเดียวกันต้องชนะ 3 ใน 5 ผลล่าสุด พูดค้างนับครั้งเดียว บนคือ 3 ห้อง (แตะเลือก
 * ได้) ซ้ายคือไฟของห้องที่เลือก (แตะการ์ด = เปิด/ปิด) และแถบความสว่าง ขวาคือคำที่
 * ได้ยิน ไฟ 5 ดวง คำสั่งล่าสุด และจำนวนครั้ง ทุกคำสั่งหรือการแตะจะพิมพ์ JSON
 * หนึ่งบรรทัด กด Stop เพื่อจบ
 *
 * AI results that are commands drive a home HMI: left / right pick the
 * room, up / down change the brightness in 10 % steps, go turns the
 * light on and stop turns it off. A word counts after a confidence
 * floor and when it wins 3 of the 5 latest results; a word held long
 * counts once. Top: three rooms (tap to pick). Left: the chosen room's
 * light (tap the card to switch it) and its brightness bar. Right: the
 * word heard, five lamps, the last command and the count. Every
 * command or tap prints one JSON line. Press Stop to end.
 *
 * Twin of 09_hmi_voice.py
 * (embedded-systems-for-aiot-developer/examples/s13/09_hmi_voice.py).
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - DEMO is False in the original, and edge_ai.inject() and events()
 *    have no C API yet (bento_edgeai README: to do), so the demo
 *    section is left out: this is the program with DEMO = False;
 *  - edge_ai.models() is bento_edgeai_count() and model_info() per
 *    row; a row's index is its place, and its labels (printed as the
 *    list's repr) are bento_edgeai_label() for each class;
 *  - a result's conf is score_pct[top] in percent, so int(conf * 100)
 *    is that number; its label is bento_edgeai_label() ("" when the
 *    board cannot name it);
 *  - the window of words keeps each word as its place in the command
 *    list, -1 for None;
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

/* The note for the BENTO Emulator, whose C runtime has no Edge AI yet. */
static const char EMULATOR_NOTE[] =
    "Edge AI จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";

/* The rule, the screen's rhythm and the house's limits. */
enum {
    CONF_MIN = 60, /* a result less sure than this does not count */
    CONFIRM_N = 3, /* the same word must win this many times ... */
    WINDOW_N = 5,  /* ... among this many latest results */
    STEP_PCT = 10, /* up and down change the brightness this much */
    FULL_PCT = 100,
    DIM_PCT = 10, /* the lowest brightness down reaches */
    ROOM_COUNT = 3,
    COMMAND_COUNT = 6,
    NO_WORD = -1, /* a result that did not pass */
    DRAW_MS = 500,
    HOLD_S = 30, /* how long an error screen stays */
    HOLD_POLLS_PER_S = 5,
    HOLD_POLL_MS = 200,
    LOOP_MS = 50,
    SETTLE_MS = 150, /* let the cleared screen settle */
    OP_POLL_MS = 100,
    HOLD_LINES = 4,
    SHOWN_CHARS = 128,
    TEXT_CHARS = 160,
    LABELS_CHARS = 448 /* the repr of up to 8 labels of 48 bytes */
};

/* The commands, in the order of COMMANDS. */
enum {
    WORD_LEFT = 0,
    WORD_RIGHT = 1,
    WORD_UP = 2,
    WORD_DOWN = 3,
    WORD_GO = 4,
    WORD_STOP = 5
};

/* Colours, 0xRRGGBB, from the hmi-living-home design. */
enum {
    COLOR_TEXT = 0xE6EDF3,
    COLOR_DIM = 0x9BA7B4,
    COLOR_CARD = 0x181C20,
    COLOR_LINE = 0x293441,
    COLOR_SELECTED = 0x586870,
    COLOR_AMBER = 0xF0B040,
    COLOR_WARN = 0xF0B440,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

/* Layout on the 792 x 398 canvas: the room chips on top, the light
 * card on the left, the voice card on the right; the bottom line ends
 * before x 706, clear of the Console button. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 10,
    AI_X = 480,
    AI_Y = 14,
    CHIP_Y = 44,
    CHIP_STEP = 136,
    CHIP_W = 128,
    CHIP_H = 48,
    CARD_Y = 104,
    LIGHT_W = 464,
    CARD_H = 232,
    LIGHT_TEXT_X = 44,
    ROOM_Y = 116,
    LAMP_Y = 160,
    STATE_Y = 196,
    BAR_Y = 244,
    BAR_W = 424,
    BAR_H = 20,
    TAP_HINT_Y = 284,
    VOICE_X = 504,
    VOICE_W = 264,
    CARD_RADIUS = 16,
    VOICE_TEXT_X = 520,
    VOICE_TITLE_Y = 116,
    HEARD_Y = 148,
    DOT_Y = 184,
    DOT_STEP = 40,
    DOT_SIZE = 28,
    DONE_Y = 222,
    COUNT_Y = 262,
    RULE_Y = 296,
    HELP_Y = 350,
    HOLD_X = 24,
    HOLD_Y = 24,
    HOLD_STEP = 44,
    SMALL_PX = 16,
    TEXT_PX = 20,
    HOLD_PX = 24,
    DONE_PX = 24,
    BIG_PX = 28
};

/* The command words, as the board reports them. */
static const char *const COMMANDS[COMMAND_COUNT] = {
    "left", "right", "up", "down", "go", "stop"};

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

/* One room's light: on or off, and its brightness. */
typedef struct {
    bool on;
    int32_t pct;
} light_t;

/* The model, the widgets, the house, the gate's memory and the
 * snapshot. */
typedef struct {
    bento_ui_t *ui;
    uint32_t model;
    char model_name[BENTO_EDGEAI_NAME_LEN];
    bento_ui_id_t chips[ROOM_COUNT];
    bool chip_known[ROOM_COUNT];
    bool chip_selected[ROOM_COUNT];
    bento_ui_id_t card;
    shown_t room_label;
    shown_t lamp_label;
    shown_t state_label;
    bento_ui_id_t bar;
    bool bar_known;
    int32_t bar_pct;
    bool bar_on;
    shown_t heard;
    bento_ui_id_t dots[WINDOW_N];
    shown_t done_label;
    shown_t count_label;
    int32_t hist[WINDOW_N];
    int32_t hist_count;
    int32_t latched; /* the word taken already, or NO_WORD */
    int32_t lit[WINDOW_N];
    bool has_sequence;
    uint32_t last_sequence;
    uint32_t drawn_ms;
    int32_t room;
    light_t light[ROOM_COUNT];
    int32_t done; /* the last command, or NO_WORD */
    char label[BENTO_EDGEAI_LABEL_LEN];
    int32_t conf;
    int32_t events;
    uint32_t sequence;
} voice_t;

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * on every load and set again at the start of main() (CLEAN_C.md C9,
 * deviation BENTO-MISRA-E03/E04). */
extern voice_t g_voice;
voice_t g_voice;

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

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 place_t place, int32_t size_px,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    place_args(&args, place);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.color = COLOR_CARD;
    args.font_px = size_px;
    return bento_ui_button_create(ui, &args, out_id);
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

/* The model's labels as print() shows the list: ['a', 'b']. */
static void print_labels(uint32_t model, uint32_t class_count)
{
    char labels[LABELS_CHARS];
    char label[BENTO_EDGEAI_LABEL_LEN];
    uint32_t used = 0U;
    uint32_t i;

    labels[0] = '\0';
    for (i = 0U; (i < class_count) && (used < sizeof(labels)); i++) {
        if (bento_edgeai_label(model, i, label, (uint32_t)sizeof(label),
                               NULL) != BENTO_OK) {
            label[0] = '\0';
        }
        (void)snprintf(&labels[used], sizeof(labels) - used, "%s'%s'",
                       (i == 0U) ? "" : ", ", label);
        used = (uint32_t)strlen(labels);
    }
    (void)printf("[%s]\n", labels);
}

/* The model by its name; it comes from the Store. */
static bento_status_t find_model(voice_t *voice, bool *found)
{
    bento_edgeai_model_info_t info;
    int32_t count = 0;
    int32_t row;
    bento_status_t status = bento_edgeai_count(&count);

    *found = false;
    for (row = 0; (row < count) && !*found && (status == BENTO_OK);
         row++) {
        bento_edgeai_model_info_init(&info);
        status = bento_edgeai_model_info((uint32_t)row, &info);
        *found = (status == BENTO_OK) &&
                 (strcmp(info.name, "Voice Commands") == 0);
        voice->model = (uint32_t)row;
    }
    if (*found) {
        (void)snprintf(voice->model_name, sizeof(voice->model_name),
                       "%s", info.name);
        (void)printf("found %s at index %" PRIu32 " labels ",
                     voice->model_name, voice->model);
        print_labels(voice->model, info.class_count);
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
    add_line(&text, "ไม่พบโมเดล Voice Commands");
    add_line(&text, "deploy Voice Commands จาก Store ก่อน");
    add_line(&text, "แล้วกด Run ใหม่");
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
static bento_status_t start_model(const voice_t *voice, bool *started)
{
    hold_text_t text;
    const char *reason = "edge_ai: select not confirmed";
    bento_status_t status = bento_edgeai_select_start(voice->model);

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
        status = hold(voice->ui, &text, COLOR_BAD);
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

/* The three room chips; the whole light card is a button too. */
static bento_status_t add_rooms(voice_t *voice)
{
    static const char *const rooms[ROOM_COUNT] = {"ห้องนั่งเล่น", "ห้องนอน",
                                                  "ครัว"};
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < ROOM_COUNT) && (status == BENTO_OK); i++) {
        const int16_t x = (int16_t)(MARGIN_X + (i * CHIP_STEP));

        status = add_button(voice->ui, rooms[i],
                            (place_t){x, CHIP_Y, CHIP_W, CHIP_H},
                            TEXT_PX, &voice->chips[i]);
    }
    if (status == BENTO_OK) {
        status = add_button(
            voice->ui, "", (place_t){MARGIN_X, CARD_Y, LIGHT_W, CARD_H},
            0, &voice->card);
    }
    return status;
}

/* The light card's texts and its brightness bar. */
static bento_status_t add_light(voice_t *voice)
{
    bento_ui_t *ui = voice->ui;
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        ui, " ",
        (label_look_t){LIGHT_TEXT_X, ROOM_Y, COLOR_TEXT, BIG_PX},
        &voice->room_label.id);

    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){LIGHT_TEXT_X, LAMP_Y, COLOR_DIM, TEXT_PX},
            &voice->lamp_label.id);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){LIGHT_TEXT_X, STATE_Y, COLOR_DIM, BIG_PX},
            &voice->state_label.id);
    }
    if (status == BENTO_OK) {
        place_args(&args, (place_t){LIGHT_TEXT_X, BAR_Y, BAR_W, BAR_H});
        args.min_val = 0;
        args.max_val = FULL_PCT;
        args.font_px = 0;
        status = bento_ui_bar_create(ui, &args, &voice->bar);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "แตะการ์ด = เปิด / ปิด",
                           (label_look_t){LIGHT_TEXT_X, TAP_HINT_Y,
                                          COLOR_DIM, TEXT_PX},
                           &unused);
    }
    return status;
}

/* WINDOW_N lamps for the latest results (lit = the result passed). */
static bento_status_t add_dots(voice_t *voice)
{
    bento_ui_widget_args_t args;
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < WINDOW_N) && (status == BENTO_OK); i++) {
        const int16_t x = (int16_t)(VOICE_TEXT_X + (i * DOT_STEP));

        place_args(&args, (place_t){x, DOT_Y, DOT_SIZE, DOT_SIZE});
        args.color = COLOR_AMBER;
        args.font_px = 0;
        status = bento_ui_led_create(voice->ui, &args, &voice->dots[i]);
    }
    return status;
}

/* The voice card's upper half: the card, its title, the word heard and
 * the lamps. */
static bento_status_t add_voice_head(voice_t *voice)
{
    bento_ui_t *ui = voice->ui;
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    place_args(&args, (place_t){VOICE_X, CARD_Y, VOICE_W, CARD_H});
    args.color = COLOR_CARD;
    args.min_val = COLOR_LINE;
    args.max_val = CARD_RADIUS;
    status = bento_ui_panel_create(ui, &args, &unused);
    if (status == BENTO_OK) {
        status = add_label(ui, "คำสั่งเสียง",
                           (label_look_t){VOICE_TEXT_X, VOICE_TITLE_Y,
                                          COLOR_DIM, TEXT_PX},
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "-",
            (label_look_t){VOICE_TEXT_X, HEARD_Y, COLOR_DIM, TEXT_PX},
            &voice->heard.id);
    }
    if (status == BENTO_OK) {
        status = add_dots(voice);
    }
    return status;
}

/* The voice card's lower half: the last command, the count, the rule,
 * and the help line under both cards. */
static bento_status_t add_voice_foot(voice_t *voice)
{
    bento_ui_t *ui = voice->ui;
    char text[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_label(
        ui, " ",
        (label_look_t){VOICE_TEXT_X, DONE_Y, COLOR_AMBER, DONE_PX},
        &voice->done_label.id);

    if (status == BENTO_OK) {
        status = add_label(
            ui, " ",
            (label_look_t){VOICE_TEXT_X, COUNT_Y, COLOR_TEXT, TEXT_PX},
            &voice->count_label.id);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "เกณฑ์ %d%% และ %d ใน %d",
                       CONF_MIN, CONFIRM_N, WINDOW_N);
        status = add_label(
            ui, text,
            (label_look_t){VOICE_TEXT_X, RULE_Y, COLOR_DIM, TEXT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui,
            "left right = ห้อง  up down = ความสว่าง  go stop = เปิด ปิด",
            (label_look_t){MARGIN_X, HELP_Y, COLOR_DIM, TEXT_PX},
            &unused);
    }
    return status;
}

static bento_status_t build_screen(voice_t *voice)
{
    char text[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(voice->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            voice->ui, "บ้านอัจฉริยะ  สั่งไฟด้วยเสียง",
            (label_look_t){MARGIN_X, TITLE_Y, COLOR_TEXT, TEXT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "AI: %s", voice->model_name);
        status = add_label(
            voice->ui, text,
            (label_look_t){AI_X, AI_Y, COLOR_INFO, SMALL_PX}, &unused);
    }
    if (status == BENTO_OK) {
        status = add_rooms(voice);
    }
    if (status == BENTO_OK) {
        status = add_light(voice);
    }
    if (status == BENTO_OK) {
        status = add_voice_head(voice);
    }
    if (status == BENTO_OK) {
        status = add_voice_foot(voice);
    }
    if (status == BENTO_OK) {
        poll_drop(voice->ui);
    }
    return status;
}

/* How many times this word is in the window. */
static int32_t count_word(const voice_t *voice, int32_t word)
{
    int32_t hits = 0;
    int32_t i;

    for (i = 0; i < voice->hist_count; i++) {
        if (voice->hist[i] == word) {
            hits++;
        }
    }
    return hits;
}

/* The k-of-n gate for words: the window keeps the latest WINDOW_N
 * words (NO_WORD = did not pass). A word taken already counts again
 * only after it has left the window. Answers the new command, or
 * NO_WORD. */
static int32_t gate(voice_t *voice, int32_t word)
{
    int32_t command = NO_WORD;
    int32_t i;

    if (voice->hist_count == WINDOW_N) {
        for (i = 1; i < WINDOW_N; i++) {
            voice->hist[i - 1] = voice->hist[i];
        }
        voice->hist_count--;
    }
    voice->hist[voice->hist_count] = word;
    voice->hist_count++;
    if ((voice->latched != NO_WORD) &&
        (count_word(voice, voice->latched) == 0)) {
        voice->latched =
            NO_WORD; /* gone from the window: say it again */
    }
    if ((word != NO_WORD) && (word != voice->latched) &&
        (count_word(voice, word) >= CONFIRM_N)) {
        voice->latched = word;
        command = word;
    }
    return command;
}

/* Carries out a command on the chosen room's light, or moves the room.
 */
static void apply(voice_t *voice, int32_t command)
{
    light_t *light = &voice->light[voice->room];
    const int32_t brighter = light->pct + STEP_PCT;
    const int32_t dimmer = light->pct - STEP_PCT;

    if (command == WORD_LEFT) {
        /* floor modulo: one room back from the first is the last */
        voice->room = ((voice->room + ROOM_COUNT) - 1) % ROOM_COUNT;
    } else if (command == WORD_RIGHT) {
        voice->room = (voice->room + 1) % ROOM_COUNT;
    } else if (command == WORD_UP) {
        light->pct = (brighter < FULL_PCT) ? brighter : FULL_PCT;
    } else if (command == WORD_DOWN) {
        light->pct = (dimmer > DIM_PCT) ? dimmer : DIM_PCT;
    } else {
        light->on = command == WORD_GO;
    }
}

/* Every AI result in one JSON line; session 12 publishes it on MQTT.
 * state is the chosen room's light. */
static void print_snapshot(const voice_t *voice)
{
    static const char team[] =
        "teamXX"; /* the team id the teacher gives */
    static const char kit[] = "voice";
    const bool on = voice->light[voice->room].on;

    (void)printf(
        "{\"id\": \"%s\", \"kit\": \"%s\", \"model\": \"%s\", "
        "\"label\": \"%s\", \"conf\": %" PRId32 ", \"state\": "
        "\"%s\", \"events\": %" PRId32 ", \"n\": %" PRIu32 "}\n",
        team, kit, voice->model_name, voice->label, voice->conf,
        on ? "on" : "off", voice->events, voice->sequence);
}

/* The chips: the chosen room is lighter; sent only when it changes. */
static void draw_chips(voice_t *voice)
{
    int32_t i;

    for (i = 0; i < ROOM_COUNT; i++) {
        const bool selected = i == voice->room;

        if (!voice->chip_known[i] ||
            (voice->chip_selected[i] != selected)) {
            voice->chip_known[i] = true;
            voice->chip_selected[i] = selected;
            (void)bento_ui_set_color(voice->ui, voice->chips[i],
                                     selected ? COLOR_SELECTED
                                              : COLOR_CARD);
        }
    }
}

/* The bar keeps no colour from its creation: .color() sets it, amber
 * only while the light is on. */
static void draw_bar(voice_t *voice, const light_t *light)
{
    if (!voice->bar_known || (voice->bar_pct != light->pct) ||
        (voice->bar_on != light->on)) {
        voice->bar_known = true;
        voice->bar_pct = light->pct;
        voice->bar_on = light->on;
        (void)bento_ui_set_value(voice->ui, voice->bar, light->pct);
        (void)bento_ui_set_color(voice->ui, voice->bar,
                                 light->on ? COLOR_AMBER : COLOR_LINE);
    }
}

/* Draws what changed: the chips, the chosen room's light, the last
 * command and the count. */
static void show(voice_t *voice)
{
    static const char *const rooms[ROOM_COUNT] = {"ห้องนั่งเล่น", "ห้องนอน",
                                                  "ครัว"};
    static const char *const lamps[ROOM_COUNT] = {"ไฟเพดาน", "ไฟหัวเตียง",
                                                  "ไฟครัว"};
    static const char *const say[COMMAND_COUNT] = {
        "ห้องซ้าย", "ห้องขวา", "สว่างขึ้น", "หรี่ลง", "เปิดไฟ", "ปิดไฟ"};
    const light_t *light = &voice->light[voice->room];
    char text[TEXT_CHARS];

    draw_chips(voice);
    put(voice->ui, &voice->room_label, rooms[voice->room], COLOR_TEXT);
    put(voice->ui, &voice->lamp_label, lamps[voice->room], COLOR_DIM);
    if (light->on) {
        (void)snprintf(text, sizeof(text), "เปิด %" PRId32 "%%",
                       light->pct);
        put(voice->ui, &voice->state_label, text, COLOR_AMBER);
    } else {
        put(voice->ui, &voice->state_label, "ปิด", COLOR_DIM);
    }
    draw_bar(voice, light);
    if (voice->done != NO_WORD) {
        (void)snprintf(text, sizeof(text), "%s = %s",
                       COMMANDS[voice->done], say[voice->done]);
        put(voice->ui, &voice->done_label, text, COLOR_AMBER);
    }
    (void)snprintf(text, sizeof(text), "รับคำสั่ง %" PRId32 " ครั้ง",
                   voice->events);
    put(voice->ui, &voice->count_label, text, COLOR_TEXT);
}

/* One clicked event: a chip picks the room, the card switches the
 * chosen room's light. Answers whether it was one of them. */
static bool take_click(voice_t *voice, bento_ui_id_t widget)
{
    bool tap = false;
    int32_t i;

    for (i = 0; i < ROOM_COUNT; i++) {
        if (widget == voice->chips[i]) {
            voice->room = i;
            tap = true;
        }
    }
    if (widget == voice->card) {
        voice->light[voice->room].on = !voice->light[voice->room].on;
        tap = true;
    }
    return tap;
}

/* The taps of this poll, in order. */
static bool read_taps(voice_t *voice)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bool tap = false;

    /* ui.poll() answers no events when it fails. */
    if (bento_ui_poll(voice->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if ((events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) &&
            take_click(voice, events[i].id)) {
            tap = true;
        }
    }
    return tap;
}

/* The place of a label in COMMANDS, or NO_WORD. */
static int32_t command_of(const char *label)
{
    int32_t word = NO_WORD;
    int32_t i;

    for (i = 0; (i < COMMAND_COUNT) && (word == NO_WORD); i++) {
        if (strcmp(label, COMMANDS[i]) == 0) {
            word = i;
        }
    }
    return word;
}

/* A new result of this model with a label goes through the gate.
 * Answers the new command, or NO_WORD; a failed read is passed over
 * this round. */
static int32_t take_result(voice_t *voice)
{
    bento_edgeai_result_t result;
    int32_t command = NO_WORD;
    bool fresh;

    bento_edgeai_result_init(&result);
    fresh = (bento_edgeai_result(&result) == BENTO_OK) &&
            result.has_verdict && (result.index == voice->model) &&
            (!voice->has_sequence ||
             (result.seq != voice->last_sequence)) &&
            (result.top < BENTO_EDGEAI_MAX_CLASSES);
    if (fresh) {
        const int32_t pct = (int32_t)result.score_pct[result.top];
        int32_t word;

        voice->has_sequence = true;
        voice->last_sequence = result.seq;
        if (bento_edgeai_label(result.index, result.top, voice->label,
                               (uint32_t)sizeof(voice->label),
                               NULL) != BENTO_OK) {
            voice->label[0] = '\0';
        }
        word = (pct >= CONF_MIN) ? command_of(voice->label) : NO_WORD;
        command = gate(voice, word);
        voice->conf = pct;
        voice->sequence = result.seq;
    }
    return command;
}

/* Only the lamps that changed are sent. */
static void draw_dots(voice_t *voice)
{
    int32_t i;

    for (i = 0; i < WINDOW_N; i++) {
        const int32_t value =
            ((i < voice->hist_count) && (voice->hist[i] != NO_WORD))
                ? 1
                : 0;

        if (value != voice->lit[i]) {
            (void)bento_ui_set_value(voice->ui, voice->dots[i], value);
            voice->lit[i] = value;
        }
    }
}

/* Every DRAW_MS: the word heard and the lamps. */
static void redraw(voice_t *voice, uint32_t now_ms)
{
    char text[TEXT_CHARS];

    voice->drawn_ms = now_ms;
    (void)snprintf(text, sizeof(text), "ได้ยิน: %s  %" PRId32 "%%",
                   voice->label, voice->conf);
    put(voice->ui, &voice->heard, text, COLOR_DIM);
    draw_dots(voice);
}

/* The house's loop; only Stop or an error ends it. */
static bento_status_t run_house(voice_t *voice)
{
    bento_status_t status = BENTO_OK;

    show(voice);
    while (status == BENTO_OK) {
        const uint32_t now_ms = bento_time_ticks_ms();
        const bool tap = read_taps(voice);
        const int32_t command = take_result(voice);

        if (command != NO_WORD) {
            apply(voice, command);
            voice->events++;
            voice->done = command;
        }
        if ((command != NO_WORD) || tap) {
            show(voice);
            print_snapshot(voice);
        }
        if (bento_time_ticks_diff(now_ms, voice->drawn_ms) >= DRAW_MS) {
            redraw(voice, now_ms);
        }
        status = pause_ms(LOOP_MS);
    }
    return status;
}

/* The screen, then the model under the finally that releases it. */
static bento_status_t run_voice(voice_t *voice)
{
    bool started = false;
    bento_status_t status = build_screen(voice);

    if (status == BENTO_OK) {
        voice->drawn_ms = bento_time_ticks_ms();
        put(voice->ui, &voice->heard, "กำลังโหลดโมเดล รอสักครู่",
            COLOR_WARN);
        poll_drop(voice->ui);
        status = start_model(voice, &started);
        if ((status == BENTO_OK) && started) {
            status = run_house(voice);
        }
        stop_model();
    }
    return status;
}

/* The house as it starts, from the hmi-living-home design. */
static void reset_house(voice_t *voice)
{
    static const light_t start_lights[ROOM_COUNT] = {
        {true, 70}, {true, 30}, {false, 100}};
    int32_t i;

    for (i = 0; i < ROOM_COUNT; i++) {
        voice->light[i] = start_lights[i];
    }
    voice->room = 0;
    voice->latched = NO_WORD;
    voice->done = NO_WORD;
    (void)snprintf(voice->label, sizeof(voice->label), "-");
}

int main(void)
{
    voice_t *voice = &g_voice;
    bool supported = false;
    bool in_emulator = false;
    bool found = false;
    bento_status_t status;

    (void)memset(voice, 0, sizeof(*voice));
    voice->ui = bento_runtime_get_ui();
    reset_house(voice);
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
        status = find_model(voice, &found);
    }
    if ((status == BENTO_OK) && !found) {
        (void)printf("none of these models is on the board: ('Voice "
                     "Commands',)\n");
        status = hold_no_model(voice->ui, in_emulator);
    } else if (status == BENTO_OK) {
        status = run_voice(voice);
    } else {
        /* an OSError from edge_ai.models() ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
