/*
 * 05_sound_feedback.c -- เสียงตอบรับตอนแตะปุ่ม ให้จอเป็นพยานแทนหู
 *
 * เสียงเป็นเอาต์พุตชนิดเดียวที่มองไม่เห็น ui.sfx(id) เล่นเสียงสำเร็จรูป ส่วน
 * ui.tone() ให้เราแต่งเสียงเอง ทั้งคู่ยิงแล้วลืม ทุกครั้งที่โปรแกรมสั่งเสียง
 * ตัวนับบนจอจึงต้องเดินขึ้นให้เห็น จอมีตัวนับตัวโต ชื่อเสียงหรือโน้ตล่าสุด
 * และปุ่มสามใบ (ยืนยัน ปฏิเสธ ไล่โน้ต) ตัวอย่างเดิน 30 วินาที
 * กับดัก: ui.tone() รับโน้ต MIDI 0-127 ไม่ใช่ความถี่เป็นเฮิรตซ์
 *
 * Sound is the one output you cannot see. ui.sfx(id) plays a ready-made
 * sound and ui.tone() a note of your own; both are fire-and-forget, so
 * the screen is the witness: every sound command moves a big counter
 * and names the last sound. Three buttons play confirm, deny and a
 * four-note scale. The example runs for 30 seconds. The trap:
 * ui.tone() takes a MIDI note 0-127, not hertz.
 *
 * Twin of 05_sound_feedback.py
 * (embedded-systems-for-aiot-developer/examples/s04/05_sound_feedback.py),
 * rewritten from the library twin aiot-s04-05-sound-feedback. Style:
 * CLEAN_C.md. hasattr(ui, "tone") becomes the BENTO_UI_FEATURE_SFX bit
 * of bento_ui_get_capabilities(), which the library sets under the
 * same build flags as MicroPython's ui.sfx and ui.tone.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    LOOP_MS = 50,
    /* the note plays on the CM55 while our call returns at once;
     * without the gap the four notes would pile up */
    NOTE_GAP_MS = 140,
    RUN_MS = 30000
};

/* Sound: the arguments are positional only (note, wave, velocity,
 * length in ms). */
enum {
    NOTE_VELOCITY = 90,
    NOTE_LENGTH_MS = 120,
    MELODY_NOTES = 4
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 8,
    CARD_Y = 48,
    CARD_W = 656,
    CARD_H = 112,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CAPTION_Y = 58,
    COUNT_X = 40,
    COUNT_Y = 88,
    COUNT_W = 144,
    COUNT_H = 48,
    LAST_X = 360,
    BUTTON_Y = 176,
    BUTTON_W = 216,
    BUTTON_H = 88, /* the smallest touch target */
    BUTTON_PITCH = 232,
    STATUS_Y = 280,
    HINT_Y = 326,
    FONT_TITLE_PX = 24,
    FONT_TEXT_PX = 20,
    TEXT_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_BAD =
        0xE5484D /* really wrong: this board has no sound chip */
};

/* The three buttons, left to right. */
enum {
    BUTTON_CONFIRM = 0,
    BUTTON_DENY = 1,
    BUTTON_SCALE = 2,
    BUTTON_COUNT = 3
};

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

typedef struct {
    bento_ui_t *ui;
    bool has_sound;
    int32_t played;
    bento_ui_id_t count;
    bento_ui_id_t last;
    bento_ui_id_t status;
    bento_ui_id_t buttons[BUTTON_COUNT];
} sound_screen_t;

/* A sleep that turns the IDE's Stop into BENTO_E_CANCELLED. */
static bento_status_t pause_ms(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* MicroPython's widget setters ignore a refusal (modui.c), so the
 * twin does too. */
static void show_text(bento_ui_t *ui, bento_ui_id_t widget,
                      const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                point_t at, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = at.x;
    args.y = at.y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* the border colour */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER;
    return bento_ui_panel_create(ui, &args, &card);
}

/* Seg7 takes text, not a number, so the count is always turned into
 * text first. */
static bento_status_t add_count(sound_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0";
    args.text_len = 1U;
    args.x = COUNT_X;
    args.y = COUNT_Y;
    args.w = COUNT_W;
    args.h = COUNT_H;
    args.color = COLOR_ACCENT;
    return bento_ui_seg7_create(screen->ui, &args, &screen->count);
}

/* The original gives these buttons no value=, so their font is the
 * theme's. */
static bento_status_t add_buttons(sound_screen_t *screen)
{
    static const char *const texts[BUTTON_COUNT] = {"ยืนยัน", "ปฏิเสธ",
                                                    "ไล่โน้ต"};
    bento_ui_widget_args_t args;
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < (uint32_t)BUTTON_COUNT) && (status == BENTO_OK);
         i++) {
        bento_ui_widget_args_init(&args);
        args.text = texts[i];
        args.text_len = (uint32_t)strlen(texts[i]);
        args.x = (int16_t)(MARGIN_X + ((int32_t)i * BUTTON_PITCH));
        args.y = BUTTON_Y;
        args.w = BUTTON_W;
        args.h = BUTTON_H;
        args.color = COLOR_ACCENT;
        status = bento_ui_button_create(screen->ui, &args,
                                        &screen->buttons[i]);
    }
    return status;
}

static bento_status_t build_card(sound_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    const point_t title = {MARGIN_X, TITLE_Y};
    const point_t count_caption = {COUNT_X, CAPTION_Y};
    const point_t last_caption = {LAST_X, CAPTION_Y};
    const point_t last = {LAST_X, COUNT_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        add_label(ui, "เสียงตอบรับ - ให้จอเป็นพยานแทนหู", title, COLOR_TEXT,
                  FONT_TITLE_PX, &unused);

    if (status == BENTO_OK) {
        status = add_card(ui);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เล่นเสียงไปแล้ว (ครั้ง)", count_caption,
                           COLOR_DIM, FONT_TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_count(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เสียงล่าสุดที่สั่งไป", last_caption, COLOR_DIM,
                           FONT_TEXT_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ยังไม่มี", last, COLOR_TEXT, FONT_TITLE_PX,
                           &screen->last);
    }
    return status;
}

static bento_status_t build_screen(sound_screen_t *screen)
{
    const point_t status_line = {MARGIN_X, STATUS_Y};
    const point_t hint = {MARGIN_X, HINT_Y};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = build_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_buttons(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "แตะปุ่มแล้วฟัง พร้อมกับมองตัวนับ",
                           status_line, COLOR_DIM, FONT_TEXT_PX,
                           &screen->status);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "ui.tone รับโน้ต MIDI 0-127 - 60=C4 - 69=A4",
                           hint, COLOR_DIM, FONT_TEXT_PX, &unused);
    }
    return status;
}

/* A board without a sound chip has no ui.sfx or ui.tone at all; ask
 * first rather than fail in the middle of a lesson. */
static bool board_has_sound(const bento_ui_t *ui)
{
    bento_ui_caps_t caps;

    return (bento_ui_get_capabilities(ui, &caps) == BENTO_OK) &&
           ((caps.feature_bits & (uint32_t)BENTO_UI_FEATURE_SFX) != 0U);
}

/* Counts one sound and writes it at once: the sound is invisible, so
 * the counter must be seen. */
static void played_one(sound_screen_t *screen, const char *name)
{
    char text[TEXT_BYTES];

    screen->played++;
    (void)snprintf(text, sizeof(text), "%" PRId32, screen->played);
    show_text(screen->ui, screen->count, text);
    show_text(screen->ui, screen->last, name);
    (void)snprintf(text, sizeof(text), "สั่งเสียง %s (ครั้งที่ %" PRId32 ")",
                   name, screen->played);
    /* lcd.print() ignores a console that is not ready */
    (void)bento_lcd_print(text);
}

/* ui.sfx() is fire-and-forget: nothing answers whether it played. */
static void play_effect(sound_screen_t *screen, uint8_t effect,
                        const char *name)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "ui.sfx(ui.%s)", name);
    show_text(screen->ui, screen->status, text);
    if (screen->has_sound) {
        (void)bento_ui_sfx_play(screen->ui, effect);
    }
    played_one(screen, name);
}

/* C4 E4 G4 C5: do, mi, sol, high do, which fit in any order. The poll
 * between two notes throws its events away, as the original does. */
static bento_status_t play_scale(sound_screen_t *screen)
{
    static const uint8_t melody[MELODY_NOTES] = {60U, 64U, 67U, 72U};
    static const char *const note_names[MELODY_NOTES] = {"C4", "E4",
                                                         "G4", "C5"};
    bento_ui_event_t thrown_away[BENTO_UI_POLL_EVENTS_MAX];
    char name[TEXT_BYTES];
    bento_status_t status = BENTO_OK;
    uint32_t i;

    show_text(screen->ui, screen->status,
              "ui.tone(note, wave, velocity, dur_ms)");
    for (i = 0U; (i < (uint32_t)MELODY_NOTES) && (status == BENTO_OK);
         i++) {
        uint32_t count = 0U;

        if (screen->has_sound) {
            (void)bento_ui_sfx_tone(
                screen->ui, melody[i], (uint8_t)BENTO_UI_WAVE_TRIANGLE,
                (uint8_t)NOTE_VELOCITY, (uint16_t)NOTE_LENGTH_MS);
        }
        (void)snprintf(name, sizeof(name), "tone %u = %s",
                       (unsigned int)melody[i], note_names[i]);
        played_one(screen, name);
        (void)bento_ui_poll(screen->ui, thrown_away,
                            BENTO_UI_POLL_EVENTS_MAX, &count, NULL);
        status = pause_ms(NOTE_GAP_MS);
    }
    return status;
}

static bento_status_t handle_click(sound_screen_t *screen,
                                   bento_ui_id_t widget)
{
    bento_status_t status = BENTO_OK;

    if (widget == screen->buttons[BUTTON_CONFIRM]) {
        play_effect(screen, (uint8_t)BENTO_UI_SFX_UI_SELECT,
                    "SFX_UI_SELECT");
    } else if (widget == screen->buttons[BUTTON_DENY]) {
        play_effect(screen, (uint8_t)BENTO_UI_SFX_UI_DENY,
                    "SFX_UI_DENY");
    } else if (widget == screen->buttons[BUTTON_SCALE]) {
        status = play_scale(screen);
    } else {
        /* another widget: nothing to play */
    }
    return status;
}

/* True when the event is of this kind; the kind passes through a
 * uint8_t, so both sides of the comparison have one type. */
static bool type_is(const bento_ui_event_t *event, uint8_t kind)
{
    return event->type == kind;
}

/* ui.poll() answers at most BENTO_UI_POLL_EVENTS_MAX events, possibly
 * none; a failed poll answers none, so it is not an error here. */
static bento_status_t handle_events(sound_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    bento_status_t status = BENTO_OK;
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        if (type_is(&events[i], BENTO_UI_EVENT_CLICKED)) {
            status = handle_click(screen, events[i].id);
        }
    }
    return status;
}

static bento_status_t run_loop(sound_screen_t *screen)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(), start_ms) <
            RUN_MS)) {
        status = handle_events(screen);
        if (status == BENTO_OK) {
            status = pause_ms(LOOP_MS);
        }
    }
    return status;
}

static void show_end(const sound_screen_t *screen)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text),
                   "หมดเวลา - สั่งเสียงไปทั้งหมด %" PRId32 " ครั้ง",
                   screen->played);
    show_text(screen->ui, screen->status, text);
    show_text(screen->ui, screen->last, "จบแล้ว");
    (void)snprintf(text, sizeof(text), "สรุป: สั่งเสียง %" PRId32 " ครั้ง",
                   screen->played);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print("ยิงแล้วลืม ไม่มีทางรู้ว่าออกจริงไหม");
}

int main(void)
{
    sound_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    screen.has_sound = board_has_sound(screen.ui);
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        if (!screen.has_sound) {
            show_text(screen.ui, screen.status,
                      "บอร์ดนี้ไม่มีชิปเสียง - ตัวนับจะไม่เดิน");
            (void)bento_ui_set_color(screen.ui, screen.status,
                                     COLOR_BAD);
        }
        /* lcd.print cuts at 127 bytes and a Thai letter is 3 bytes,
         * so the lines stay short */
        (void)bento_lcd_print("แตะปุ่มแล้วฟัง พร้อมมองตัวนับบนจอ");
        (void)bento_lcd_print("ตัวนับเดินแต่เงียบ = ปัญหาอยู่ที่ลำโพง");
        status = run_loop(&screen);
    }
    if (status == BENTO_OK) {
        show_end(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
