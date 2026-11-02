/*
 * buttonmatrix.c -- ui.ButtonMatrix ตัวเดียว (one ButtonMatrix)
 *
 * ปุ่มเก้าปุ่มเรียงสามแถวในแฮนเดิลใบเดียว แตะปุ่มแล้วได้ event พร้อมลำดับปุ่ม
 * คอนโซลพิมพ์ลำดับและชื่อปุ่ม รันจบเองใน 20 วินาที
 *
 * Nine buttons in three rows behind one handle: a tap gives an event
 * with the button's number, and the console prints the number and the
 * button's name. The example ends by itself after 20 seconds.
 *
 * Twin of buttonmatrix.py
 * (embedded-systems-for-aiot-developer/examples/widgets/buttonmatrix.py),
 * merged into one file from the library twin wref-buttonmatrix.
 * Style: CLEAN_C.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"

/* Timing: 100 rounds of 200 ms (20 s). */
enum {
    SETTLE_MS = 200,
    ROUND_MS = 200, /* read the taps, then sleep this long */
    ROUNDS = 100,
    KEY_COUNT = 9
};

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

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF
};

/* Layout on the 792 x 398 canvas. */
enum {
    MARGIN_X = 24,
    TITLE_Y = 16,
    TITLE_PX = 28,
    MATRIX_Y = 72,
    MATRIX_W = 744,
    MATRIX_H = 264,
    KEYS_PER_ROW = 3,
    FOOT_Y = 344,
    FOOT_PX = 20
};

static const char *const KEYS[KEY_COUNT] = {
    "เดินหน้า", "หยุด",  "ถอยหลัง", "เร็วขึ้น", "คงที่",
    "ช้าลง",   "บันทึก", "ล้างค่า",  "ออก",
};

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* The matrix: a new row starts before every third key; a key's refusal
 * is ignored, as modui.c does. */
static bento_status_t add_matrix(bento_ui_t *ui, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;
    size_t i;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = MATRIX_Y;
    args.w = MATRIX_W;
    args.h = MATRIX_H;
    args.color = COLOR_TEXT;
    status = bento_ui_buttonmatrix_create(ui, &args, out_id);
    for (i = 0U; (i < (size_t)KEY_COUNT) && (status == BENTO_OK); i++) {
        const bool new_row =
            (i > 0U) && ((i % (size_t)KEYS_PER_ROW) == 0U);

        (void)bento_ui_add_button(ui, *out_id, KEYS[i],
                                  (uint32_t)strlen(KEYS[i]), new_row,
                                  0U);
    }
    return status;
}

static bento_status_t build_screen(bento_ui_t *ui,
                                   bento_ui_id_t *matrix)
{
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        status = add_label(
            ui, "ui.ButtonMatrix",
            (label_look_t){MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_matrix(ui, matrix);
    }
    if (status == BENTO_OK) {
        status = add_label(
            ui, "เก้าปุ่ม แต่ใช้แฮนเดิลใบเดียว",
            (label_look_t){MARGIN_X, FOOT_Y, COLOR_DIM, FOOT_PX},
            &unused);
    }
    return status;
}

/* Any event from the matrix, whatever its kind (the original reads only
 * the handle), is counted and printed with the key's name, or "?" for a
 * number past the keys. */
static int32_t print_taps(bento_ui_t *ui, bento_ui_id_t matrix)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    int32_t taps = 0;

    if (bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (events[i].id == matrix) {
            const int32_t key = events[i].value;
            const bool known = (key >= 0) && (key < KEY_COUNT);

            taps++;
            (void)printf("แตะปุ่มลำดับที่ %ld %s\n", (long)key,
                         known ? KEYS[key] : "?");
        }
    }
    return taps;
}

/* ROUNDS rounds: read the taps, then sleep ROUND_MS (20 s in all). */
static bento_status_t run_rounds(bento_ui_t *ui, bento_ui_id_t matrix,
                                 int32_t *taps)
{
    int32_t round;
    bento_status_t status = BENTO_OK;

    for (round = 0; (round < ROUNDS) && (status == BENTO_OK); round++) {
        *taps += print_taps(ui, matrix);
        status = pause_ms(ROUND_MS);
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    bento_ui_id_t matrix = 0U;
    int32_t taps = 0;
    bento_status_t status = build_screen(ui, &matrix);

    if (status == BENTO_OK) {
        status = run_rounds(ui, matrix, &taps);
    }
    if (status == BENTO_OK) {
        (void)printf("ui.ButtonMatrix: แตะไป %ld ครั้ง - event คือ "
                     "value_changed พร้อมลำดับปุ่ม\n",
                     (long)taps);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
