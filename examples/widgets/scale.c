/*
 * scale.c -- ไม้บรรทัด ui.Scale (Widget: ui.Scale)
 *
 * ไม้บรรทัด 0 ถึง 100 มีขีดและตัวเลขกำกับ min= max= คือพิสัย .ticks(ขีดทั้งหมด,
 * ทุกกี่ขีด จึงเป็นขีดใหญ่) กำหนดความถี่ของขีด ui.Scale ไม่มีเข็มและไม่รับ .value()
 * ตัวที่ขยับคือ แถบ ui.Bar ที่วางไว้เหนือไม้บรรทัด ตอนจบโปรแกรมสั่ง .value(50)
 * ให้ไม้บรรทัดเพื่อพิสูจน์ว่า มันนิ่งเหมือนเดิม ตัวอย่างจบเองใน 24 วินาที ไม่ต้องต่อเน็ต
 * ไม่ต้องมีเซนเซอร์
 *
 * A ruler from 0 to 100 with ticks and numbers: min= and max= are its
 * range, and .ticks(total, major_every) sets how dense the ticks are. A
 * ui.Scale has no needle and takes no .value(); what moves is the
 * ui.Bar placed above it. At the end the program calls the ruler's
 * .value(50) to show that it stays still. The example ends by itself
 * after 24 seconds and needs no network and no sensor.
 *
 * Twin of scale.py
 * (embedded-systems-for-aiot-developer/examples/widgets/scale.py),
 * merged into one file from the library twin aiot-widget-scale.
 * Style: CLEAN_C.md. Every division here has
 * non-negative operands, so C's / and % give what Python's floor
 * division and modulo give.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* The range, the ticks and the run. */
enum {
    RANGE_MAX = 100,  /* the bar and the ruler both run from 0 to 100 */
    TICKS_TOTAL = 51, /* 51 ticks over 0..100: one every 2 */
    /* a major tick, with its number, every 10 ticks */
    MAJOR_EVERY = 10,
    IGNORED_VALUE = 50, /* the .value() the ruler does not take */
    STEPS = 60,         /* 60 steps of STEP_MS: 24 seconds */
    /* the value text changes every 5 steps: 2 seconds */
    STEPS_PER_TEXT = 5,
    SETTLE_MS = 200,  /* let the cleared screen settle */
    STEP_MS = 400,    /* one step of the loop */
    VALUE_BYTES = 128 /* a SET_TEXT carries at most 126 bytes */
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t bar;
    bento_ui_id_t ruler;
    bento_ui_id_t value_label;
} scale_screen_t;

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    BAR_Y = 96,
    BAR_H = 24,
    RULER_Y = 128,
    RULER_H = 64,
    WIDE_W = 744, /* the bar and the ruler are equally wide */
    VALUE_Y = 224,
    HINT_Y = 272,
    FOOTER_Y = 344
};

/* Font sizes, in pixels. */
enum {
    TITLE_PX = 28,
    VALUE_PX = 24,
    CAPTION_PX = 20
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF
};

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t y, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = MARGIN_X;
    args.y = y;
    args.color = color;
    args.font_px = font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* The bar sits above the ruler because the ruler has no needle of
 * its own. */
static bento_status_t add_bar(scale_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = BAR_Y;
    args.w = WIDE_W;
    args.h = BAR_H;
    args.color = COLOR_ACCENT;
    args.min_val = 0;
    args.max_val = RANGE_MAX;
    args.font_px = 0; /* value=0: the bar starts empty */
    return bento_ui_bar_create(screen->ui, &args, &screen->bar);
}

static bento_status_t add_ruler(scale_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = RULER_Y;
    args.w = WIDE_W;
    args.h = RULER_H;
    args.color = COLOR_TEXT;
    args.min_val = 0;
    args.max_val = RANGE_MAX;
    status = bento_ui_scale_create(screen->ui, &args, &screen->ruler);
    if (status == BENTO_OK) {
        /* .ticks() is sent without waiting for an answer (modui.c), so
         * a refusal is never seen there and is not seen here either. */
        (void)bento_ui_set_ticks(screen->ui, screen->ruler, TICKS_TOTAL,
                                 MAJOR_EVERY);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(scale_screen_t *screen)
{
    bento_ui_t *ui = screen->ui;
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(SETTLE_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ui.Scale", TITLE_Y, COLOR_TEXT,
                           TITLE_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_bar(screen);
    }
    if (status == BENTO_OK) {
        status = add_ruler(screen);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ค่า 0 จาก 100", VALUE_Y, COLOR_TEXT,
                           VALUE_PX, &screen->value_label);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ไม้บรรทัดนิ่ง แถบคือตัวที่ขยับ", HINT_Y,
                           COLOR_DIM, CAPTION_PX, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "เรียก sc.value(50) แล้วไม่มีอะไรเกิด",
                           FOOTER_Y, COLOR_DIM, CAPTION_PX, &unused);
    }
    return status;
}

/* The bar moves from 0 to 100 over the run; the text follows it
 * every STEPS_PER_TEXT steps. */
static void show_step(const scale_screen_t *screen, int32_t step)
{
    const int32_t value = (step * RANGE_MAX) / (STEPS - 1);
    char text[VALUE_BYTES];

    /* MicroPython's widget setters ignore a refusal (modui.c), so
     * the twin does too. */
    (void)bento_ui_set_value(screen->ui, screen->bar, value);
    if ((step % STEPS_PER_TEXT) == 0) {
        (void)snprintf(text, sizeof(text), "ค่า %" PRId32 " จาก 100",
                       value);
        (void)bento_ui_set_text(screen->ui, screen->value_label, text,
                                (uint32_t)strlen(text));
    }
}

/* The proof of the last point, by the ruler itself: it is told a
 * value and stays still. */
static void show_ruler_ignores_value(const scale_screen_t *screen)
{
    static const char text_done[] = "สั่ง sc.value(50) แล้ว - "
                                    "ไม้บรรทัดนิ่ง";

    /* MicroPython's widget setters ignore a refusal (modui.c), so
     * the twin does too. */
    (void)bento_ui_set_value(screen->ui, screen->ruler, IGNORED_VALUE);
    (void)bento_ui_set_text(screen->ui, screen->value_label, text_done,
                            (uint32_t)strlen(text_done));
}

/* The original asks for the touch events every step and reads none of
 * them; ui.poll() answers no events when it fails, so its status is not
 * an error here either. */
static void skip_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* STEPS steps of STEP_MS, or until Stop is pressed. */
static bento_status_t run_steps(const scale_screen_t *screen)
{
    bento_status_t status = BENTO_OK;
    int32_t step;

    for (step = 0; (step < STEPS) && (status == BENTO_OK); step++) {
        show_step(screen, step);
        skip_events(screen->ui);
        status = bento_time_sleep_ms(STEP_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
    }
    return status;
}

int main(void)
{
    scale_screen_t screen = {0};
    bento_status_t status;

    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        status = run_steps(&screen);
    }
    if (status == BENTO_OK) {
        show_ruler_ignores_value(&screen);
        (void)printf("ui.Scale: ไม้บรรทัด ไม่มีเข็ม ไม่รับ .value() - "
                     "ตัวที่ขยับคือ ui.Bar\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
