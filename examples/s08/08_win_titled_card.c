/*
 * 08_win_titled_card.c -- การ์ดที่มีชื่อกำกับมาในตัว และหัวเรื่องที่แก้ไม่ได้
 * (คาบ 08)
 *
 * การ์ดสองใบเรื่องเดียวกัน ซ้ายเป็น ui.Win ที่มีแถบหัวเรื่องมาจากเฟิร์มแวร์
 * ขวาเป็น Panel กับ Label แบบเดิม ทั้งคู่โชว์ระดับเสียงจากไมค์ตัวเดียวกัน
 * อ่านทุก 200 ms 90 รอบ และอัปเดตตัวเลขทุก 1 วินาที หัวเรื่องของใบขวาพาค่าล่าสุด
 * ไปด้วยได้ ส่วนใบซ้ายสั่งเปลี่ยนหัวเรื่องทุกครั้งแต่จอไม่ขยับ ป้ายล่างนับจำนวน
 * คำสั่งที่หายไปเงียบ ๆ จบแล้วปิดไมค์เสมอ บอร์ด: Eva Kit
 *
 * Two cards about the same thing: on the left a ui.Win, whose title bar
 * comes from the firmware; on the right the usual Panel and Label. Both
 * show the sound level of the same microphone, read every 200 ms for 90
 * rounds and shown once a second. The right card's title can carry the
 * latest value; the left card is told to change its title every time,
 * and the screen does not move. The bottom line counts those silently
 * lost commands. The microphone is always closed at the end. Board: Eva
 * Kit.
 *
 * Twin of 08_win_titled_card.py
 * (embedded-systems-for-aiot-developer/examples/s08/08_win_titled_card.py).
 * Style: CLEAN_C.md.
 * The C API has no mic module, so the few calls of the board's frozen
 * mic.py (ports/psoc-edge/freeze/mic.py) are reproduced here on
 * bento_machine_pdm: start(sens) opens and starts PDM0 (16 kHz,
 * decimation 96, MONO_RIGHT, the sens-to-gain table) and throws away
 * two reads 20 ms apart; level() is rms() of a fresh window on the
 * module's integer octave scale. bento_machine_pdm is a PSoC Edge API:
 * the reTerminal has no PDM microphone (its port answers unsupported).
 * Python's try/finally becomes close_microphone(), which main() calls
 * once.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_machine.h"
#include "bento_runtime.h"

/* The program's settings. */
enum {
    SENSITIVITY = 3, /* the same as 02_mic_sound_level_meter */
    ROUNDS = 90,
    PERIOD_MS = 200,  /* one round */
    SHOW_EVERY = 5,   /* 5 rounds of 200 ms: a number a person reads */
    SETTLE_MS = 200,  /* let the cleared screen settle */
    TEXT_BYTES = 128, /* Thai text and an int32_t number */
    NUMBER_BYTES = 12 /* any int32_t: a sign, 10 digits and the NUL */
};

/* mic.py's numbers (ports/psoc-edge/freeze/mic.py). */
enum {
    MIC_RATE = 16000,
    MIC_DECIMATION = 96,
    MIC_BITS = 16,
    MIC_SAMPLES = 256, /* start()'s default samples= */
    SAMPLE_BYTES = 2,
    MIC_BUFFER_BYTES = MIC_SAMPLES * SAMPLE_BYTES,
    WARM_UP_READS = 2,
    WARM_UP_MS = 20,
    READ_LIMIT_MS = 2000, /* the binding's read time-out */
    SENSITIVITY_COUNT = 5,
    LEVEL_FLOOR = 16,     /* an rms below this is level 0 */
    LEVEL_LOW_Q8 = 1024,  /* log2(16) times 256 */
    LEVEL_SPAN_Q8 = 2816, /* log2(32768) times 256, less LEVEL_LOW_Q8 */
    LEVEL_MAX = 100,
    Q8_TOP_BITS = 9, /* keep 9 bits: a mantissa of 256 to 511 */
    Q8_ONE = 256
};

/* Screen layout on the 792 x 398 canvas: the Win on the left, the
 * Panel on the right, the counting line at the bottom. */
enum {
    TITLE_X = 24,
    TITLE_Y = 8,
    WIN_X = 24,
    PANEL_X = 408,
    CARD_Y = 56,
    CARD_W = 360,
    CARD_H = 304,
    PANEL_RADIUS = 8,
    HEAD_X = 432,
    HEAD_Y = 72,
    BODY_X = 24, /* inside the Win's body, under its title bar */
    BODY_CAPTION_Y = 16,
    BODY_VALUE_Y = 48,
    BODY_BAR_Y = 104,
    BODY_RANGE_Y = 152,
    PANEL_CAPTION_Y = 120,
    PANEL_VALUE_Y = 152,
    PANEL_BAR_Y = 216,
    PANEL_RANGE_Y = 264,
    BAR_W = 296,
    BAR_H = 32,
    LINE_X = 24,
    LINE_Y = 368
};

/* Font sizes, in pixels, and colours, 0xRRGGBB. */
enum {
    FONT_TITLE = 28,
    FONT_HEAD = 24,
    FONT_TEXT = 20,
    FONT_VALUE = 28,
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22
};

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * and set again at the start of main() (CLEAN_C.md C9). */
extern uint8_t g_mic_buffer[MIC_BUFFER_BYTES];
uint8_t g_mic_buffer[MIC_BUFFER_BYTES]; /* one window of samples */

/* Where one card's four lines sit, from its parent's corner. */
typedef struct {
    int16_t x;
    int16_t caption_y;
    int16_t value_y;
    int16_t bar_y;
    int16_t range_y;
} card_place_t;

/* A label's corner in its parent, colour and font size. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} label_place_t;

/* The two widgets of a card that show the level. */
typedef struct {
    bento_ui_id_t value;
    bento_ui_id_t bar;
} card_t;

/* The PDM microphone and whether it needs closing. */
typedef struct {
    uint32_t handle;
    bool open;
} microphone_t;

/* The widgets the program changes, and what the loop remembers. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t window;
    card_t window_card;
    bento_ui_id_t panel_head;
    card_t panel_card;
    bento_ui_id_t line;
    microphone_t microphone;
    int32_t asked;
} cards_t;

/* mic.py's sens-to-gain table; the chip's gain runs backwards, and a
 * sens outside 1 to 5 is sens 3. */
static uint32_t gain_for(int32_t sensitivity)
{
    static const uint32_t gains[SENSITIVITY_COUNT] = {17U, 15U, 13U,
                                                      11U, 9U};
    int32_t place = sensitivity - 1;

    if ((place < 0) || (place >= SENSITIVITY_COUNT)) {
        place = SENSITIVITY - 1;
    }
    return gains[place];
}

/* After a sleep: Stop is a status. */
static bento_status_t sleep_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* start() throws away two windows 20 ms apart: the first audio after
 * opening is not settled. A failed read is ignored there too. */
static bento_status_t warm_up(uint32_t handle)
{
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < WARM_UP_READS) && (status == BENTO_OK); i++) {
        uint32_t got = 0U;

        (void)bento_machine_pdm_read(
            handle, g_mic_buffer, MIC_BUFFER_BYTES, READ_LIMIT_MS,
            &got); /* start() ignores OSError */
        status = sleep_ms(WARM_UP_MS);
    }
    return status;
}

/* mic.start(sens): PDM0 configured, capturing and warmed up. */
static bento_status_t open_microphone(microphone_t *microphone,
                                      int32_t sensitivity)
{
    bento_machine_pdm_cfg_t config;
    const uint32_t gain = gain_for(sensitivity);
    bento_status_t status;

    bento_machine_pdm_cfg_init(&config);
    config.sample_rate = MIC_RATE;
    config.decimation_rate = MIC_DECIMATION;
    config.bits = MIC_BITS;
    config.format = BENTO_MACHINE_PDM_MONO_RIGHT;
    config.left_gain = gain;
    config.right_gain = gain;
    status = bento_machine_pdm_open(&config, &microphone->handle);
    if (status == BENTO_OK) {
        microphone->open = true;
        status = bento_machine_pdm_start(microphone->handle);
    }
    if (status == BENTO_OK) {
        status = warm_up(microphone->handle);
    }
    return status;
}

/* mic.stop(): the cleanup, whatever ended the program. */
static void close_microphone(microphone_t *microphone)
{
    if (microphone->open) {
        (void)bento_machine_pdm_close(
            microphone->handle); /* stop() swallows every error */
        microphone->open = false;
    }
}

/* log2(value) times 256 in integers: the octave from the bit count,
 * its fraction read linearly from the top 9 bits. */
static int32_t log2_q8(uint32_t value)
{
    uint32_t rest = value;
    int32_t bits = 0;
    uint32_t mantissa;

    while (rest != 0U) {
        rest >>= 1U;
        bits++;
    }
    if (bits <= Q8_TOP_BITS) {
        mantissa = value << (uint32_t)(Q8_TOP_BITS - bits);
    } else {
        mantissa = value >> (uint32_t)(bits - Q8_TOP_BITS);
    }
    return ((bits - 1) * Q8_ONE) + ((int32_t)mantissa - Q8_ONE);
}

/* mic.level(): the rms of a fresh window on the ear's scale, 0 to 100,
 * one point per equal step in octaves from rms 16 to full scale. */
static bento_status_t read_level(const microphone_t *microphone,
                                 int32_t *level)
{
    bento_machine_pdm_stats_t stats;
    bento_status_t status;

    bento_machine_pdm_stats_init(&stats);
    status = bento_machine_pdm_stats(microphone->handle, g_mic_buffer,
                                     MIC_BUFFER_BYTES, true, &stats);
    *level = 0;
    if ((status == BENTO_OK) && (stats.rms >= (uint32_t)LEVEL_FLOOR)) {
        const int32_t above = log2_q8(stats.rms) - LEVEL_LOW_Q8;
        const int32_t scaled = (above * LEVEL_MAX) / LEVEL_SPAN_Q8;

        *level = (scaled > LEVEL_MAX) ? LEVEL_MAX : scaled;
    }
    return status;
}

static void set_text(const cards_t *cards, bento_ui_id_t id,
                     const char *text)
{
    /* a setter's refusal was ignored before too (modui.c) */
    (void)bento_ui_set_text(cards->ui, id, text,
                            (uint32_t)strlen(text));
}

/* ui.poll() is called every round, as the original does; the events are
 * not used, and it answers no events when it fails. */
static void skip_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* A label at (x, y) of its parent (0 = the screen). */
static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const label_place_t *place,
                                bento_ui_id_t parent,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.color = place->color;
    args.font_px = place->font_px;
    args.parent = parent;
    return bento_ui_label_create(ui, &args, out_id);
}

/* A card's caption, value, bar and range note, in its parent. */
static bento_status_t add_reading(bento_ui_t *ui, bento_ui_id_t parent,
                                  const card_place_t *place,
                                  card_t *card)
{
    const label_place_t caption = {place->x, place->caption_y,
                                   COLOR_DIM, FONT_TEXT};
    const label_place_t value = {place->x, place->value_y, COLOR_TEXT,
                                 FONT_VALUE};
    const label_place_t range = {place->x, place->range_y, COLOR_DIM,
                                 FONT_TEXT};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "ค่าล่าสุด", &caption, parent, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "--", &value, parent, &card->value);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_t args;

        bento_ui_widget_args_init(&args);
        args.x = place->x;
        args.y = place->bar_y;
        args.w = BAR_W;
        args.h = BAR_H;
        args.min_val = 0;
        args.max_val = LEVEL_MAX;
        args.font_px = 0;
        args.parent = parent;
        status = bento_ui_bar_create(ui, &args, &card->bar);
    }
    if (status == BENTO_OK) {
        status =
            add_label(ui, "ช่วงปกติ 0 ถึง 60", &range, parent, &unused);
    }
    return status;
}

/* The left card: a Win, whose title bar the firmware draws, and the
 * widgets in its body, placed from the body's corner. */
static bento_status_t add_window_card(cards_t *cards)
{
    static const card_place_t place = {
        BODY_X, BODY_CAPTION_Y, BODY_VALUE_Y, BODY_BAR_Y, BODY_RANGE_Y};
    bento_ui_widget_args_t args;
    bento_ui_id_t body = 0U;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "ระดับเสียงในห้อง";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = WIN_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    status = bento_ui_win_create(cards->ui, &args, &cards->window);
    if (status == BENTO_OK) {
        status = bento_ui_win_content(cards->ui, cards->window, &body);
    }
    if (status == BENTO_OK) {
        status =
            add_reading(cards->ui, body, &place, &cards->window_card);
    }
    return status;
}

/* The right card: a Panel and our own Label as its title. */
static bento_status_t add_panel_card(cards_t *cards)
{
    static const card_place_t place = {HEAD_X, PANEL_CAPTION_Y,
                                       PANEL_VALUE_Y, PANEL_BAR_Y,
                                       PANEL_RANGE_Y};
    static const label_place_t head = {HEAD_X, HEAD_Y, COLOR_TEXT,
                                       FONT_HEAD};
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = PANEL_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.max_val = PANEL_RADIUS;
    status = bento_ui_panel_create(cards->ui, &args, &panel);
    if (status == BENTO_OK) {
        status = add_label(cards->ui, "ระดับเสียงในห้อง", &head, 0U,
                           &cards->panel_head);
    }
    if (status == BENTO_OK) {
        status = add_reading(cards->ui, 0U, &place, &cards->panel_card);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(cards_t *cards)
{
    static const label_place_t title = {TITLE_X, TITLE_Y, COLOR_TEXT,
                                        FONT_TITLE};
    static const label_place_t line = {LINE_X, LINE_Y, COLOR_DIM,
                                       FONT_TEXT};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(cards->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(cards->ui, "การ์ดที่มีชื่อกำกับในตัว", &title, 0U,
                           &unused);
    }
    if (status == BENTO_OK) {
        status = add_window_card(cards);
    }
    if (status == BENTO_OK) {
        status = add_panel_card(cards);
    }
    if (status == BENTO_OK) {
        status = add_label(cards->ui, "ยังไม่ได้สั่งเปลี่ยนหัวเรื่อง", &line, 0U,
                           &cards->line);
    }
    return status;
}

/* Both cards get the same value; only the right card's title can
 * carry it. The Win is told the same and nothing happens: the firmware
 * has no title setter for a Win, so the line counts the lost commands. */
static void show_level(cards_t *cards, int32_t level)
{
    char number[NUMBER_BYTES];
    char text[TEXT_BYTES];

    (void)snprintf(number, sizeof(number), "%" PRId32, level);
    set_text(cards, cards->window_card.value, number);
    (void)bento_ui_set_value(cards->ui, cards->window_card.bar, level);
    set_text(cards, cards->panel_card.value, number);
    (void)bento_ui_set_value(cards->ui, cards->panel_card.bar, level);
    (void)snprintf(text, sizeof(text), "ระดับเสียง %s", number);
    set_text(cards, cards->panel_head, text);
    set_text(cards, cards->window, text);
    cards->asked++;
    (void)snprintf(text, sizeof(text),
                   "สั่งเปลี่ยนหัว Win %" PRId32 " ครั้ง หัวยังเหมือนเดิม",
                   cards->asked);
    set_text(cards, cards->line, text);
}

/* One round: the level, shown every SHOW_EVERY rounds, then the poll
 * and the sleep. */
static bento_status_t run_round(cards_t *cards, int32_t round_index)
{
    int32_t level = 0;
    bento_status_t status = read_level(&cards->microphone, &level);

    if ((status == BENTO_OK) && ((round_index % SHOW_EVERY) == 0)) {
        show_level(cards, level);
    }
    if (status == BENTO_OK) {
        skip_events(cards->ui);
        status = sleep_ms(PERIOD_MS);
    }
    return status;
}

/* ROUNDS rounds of PERIOD_MS, or until Stop or a failed read. */
static bento_status_t run_cards(cards_t *cards)
{
    char line[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    int32_t i;

    (void)snprintf(line, sizeof(line),
                   "เปิดไมค์ที่ความไว %d จาก 5 - พูดใส่บอร์ดได้เลย",
                   SENSITIVITY);
    (void)bento_lcd_print(line); /* sent without an answer */
    for (i = 0; (i < ROUNDS) && (status == BENTO_OK); i++) {
        status = run_round(cards, i);
    }
    return status;
}

/* The console's first lines; lcd calls are sent without an answer
 * (modlcd.c). */
static void open_console(void)
{
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>ui.Win - การ์ดที่มีหัวเรื่องมาให้</h2>");
    (void)bento_lcd_print("ซ้าย Win + content() = 2 แฮนเดิล / "
                          "ขวา Panel + Label = 2 แฮนเดิล");
    (void)bento_lcd_print("ราคาเท่ากัน ต่างกันที่ใครเป็นคนจัดตำแหน่งหัวเรื่อง");
}

/* After the loop: how many title changes the Win swallowed. */
static void report_end(const cards_t *cards)
{
    char line[TEXT_BYTES];

    (void)snprintf(line, sizeof(line),
                   "<span class=warn>สั่งเปลี่ยนหัว Win %" PRId32
                   " ครั้ง จอไม่ขยับ</span>",
                   cards->asked);
    (void)bento_lcd_print(line);
    (void)printf("Win: text= ตอนสร้างเท่านั้น, .content() "
                 "คือที่ของข้างใน\n");
}

int main(void)
{
    cards_t cards;
    bento_status_t status;

    (void)memset(&cards, 0, sizeof(cards));
    (void)memset(g_mic_buffer, 0, sizeof(g_mic_buffer));
    cards.ui = bento_runtime_get_ui();
    status = build_screen(&cards);
    if (status == BENTO_OK) {
        open_console();
        status = open_microphone(&cards.microphone, SENSITIVITY);
    }
    if (status == BENTO_OK) {
        status = run_cards(&cards);
    }
    close_microphone(&cards.microphone);
    if (status == BENTO_OK) {
        report_end(&cards);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
