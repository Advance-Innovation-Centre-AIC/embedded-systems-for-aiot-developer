/*
 * 03_tilt_from_gravity.c -- dsp.tilt() คืออะไรกันแน่ (คาบ 06)
 *
 * dsp.tilt() คือ atan2 สองบรรทัดจากแรงโน้มถ่วงล้วน ๆ ไม่ใช้ gyro เลย จอมีสามคอลัมน์:
 * ค่าความเร่งดิบกับขนาด คำตอบของเฟิร์มแวร์ และสูตรเดียวกันที่คำนวณเองพร้อมผลต่าง
 * กราฟ roll กับ pitch และการ์ดกับดักที่อ่านค่าคู่เดิมสลับลำดับ ตัวเลขยังดูน่าเชื่อทุกประการ
 * อ่าน BMI270 ทุก 200 ms จนกว่าจะแตะปุ่ม "ออก" รอบที่อ่านพลาดใช้ค่าเดิมไปก่อน
 * บอร์ด: Dev Kit และ Eva Kit
 *
 * dsp.tilt() is two atan2 lines on gravity alone; it needs no gyro.
 * The screen has three columns: the raw acceleration and its
 * magnitude, the firmware's answer, and the same formula computed here
 * with the difference; a roll and pitch chart, and a trap card that
 * reads the same pair in swapped order, where the numbers still look
 * right. The BMI270 is read every 200 ms until the "exit" button is
 * tapped; a failed read keeps the old values. Boards: Dev Kit and Eva
 * Kit.
 *
 * Twin of 03_tilt_from_gravity.py
 * (embedded-systems-for-aiot-developer/examples/s06/03_tilt_from_gravity.py),
 * started from the library twin aiot-s06-03-tilt-from-gravity. Style:
 * CLEAN_C.md.
 * What C changes only because the language forces it: a float is
 * bento_real_t, the width each board's MicroPython uses, and math.atan2
 * and math.sqrt are the C functions of that width; the original's
 * except OSError around the reading is any failed BMI270 read here,
 * except BENTO_E_UNSUPPORTED (no sensors.bmi270 at all), which ends the
 * program as the AttributeError would; "{:+7.3f}" and its kin are
 * bento_fmt_real_precision with the sign and the right alignment added
 * as MicroPython's format() adds them; the exit button reacts to any
 * event, as the original reads only ev['handle'].
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_dsp.h"
#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

/* Every real-number step is rounded as MicroPython rounds it: the
 * compiler must not fuse a multiply and an add into one. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

/* The labels the loop rewrites, by slot. */
enum {
    SLOT_NONE = -1,
    SLOT_AX = 0,
    SLOT_AY = 1,
    SLOT_AZ = 2,
    SLOT_MAGNITUDE = 3,
    SLOT_ROLL = 4,
    SLOT_PITCH = 5,
    SLOT_MY_ROLL = 6,
    SLOT_MY_PITCH = 7,
    SLOT_DIFFERENCE = 8,
    SLOT_WRONG_PITCH = 9,
    SLOT_WRONG_ROLL = 10,
    SLOT_COUNT = 11
};

/* Timing and sizes. */
enum {
    STEP_MS = 200,    /* one reading and screen update */
    RETRY_MS = 50,    /* after a failed reading */
    ANGLE_LIMIT = 90, /* the chart runs from -90 to 90 degrees */
    COLUMN_COUNT = 12,
    CARD_COUNT = 4,
    TEXT_BYTES = 128 /* a label text, at most 126 bytes */
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    EXIT_X = 672,
    EXIT_Y = 64,
    EXIT_SIZE = 88,
    CHART_X = 24,
    CHART_Y = 216,
    CHART_W = 440,
    CHART_H = 120,
    CHART_NOTE_Y = 342,
    CARD_X = 480,
    CARD_Y = 216,
    CARD_W = 288,
    CARD_H = 124
};

/* Font sizes, in pixels, and colours, 0xRRGGBB. */
enum {
    FONT_TITLE = 24,
    FONT_TEXT = 20,
    FONT_CAPTION = 16,
    COLOR_DEFAULT = 0,
    COLOR_ROLL = 0xE5484D,
    COLOR_PITCH = 0x30A46C,
    COLOR_GRAY = 0x4A9EFF,
    COLOR_EXIT = 0x9AA3AF,
    COLOR_WARN = 0xF5A623
};

/* A label to draw: its text, place, look, and the slot it fills. */
typedef struct {
    const char *text;
    int16_t x;
    int16_t y;
    int32_t font_px;
    uint32_t color;
    int32_t slot;
} label_spec_t;

/* "{:[+][width].<digits>f}" */
typedef struct {
    int32_t digits;
    bool plus;
    int32_t width;
} number_format_t;

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t labels[SLOT_COUNT];
    bento_ui_id_t exit;
    bento_ui_id_t chart;
    uint8_t roll_series; /* series 0 comes with the chart, red */
    uint8_t pitch_series;
} tilt_screen_t;

/* The three angles of one reading: the firmware's and the formula's. */
typedef struct {
    bento_real_t roll;
    bento_real_t pitch;
    bento_real_t my_roll;
    bento_real_t my_pitch;
} angles_t;

/* math.atan2 and math.sqrt in the board's width. */
static bento_real_t board_atan2(bento_real_t y, bento_real_t x)
{
#if BENTO_FMT_REAL_DOUBLE
    return atan2(y, x);
#else
    return atan2f(y, x);
#endif
}

static bento_real_t board_sqrt(bento_real_t x)
{
#if BENTO_FMT_REAL_DOUBLE
    return sqrt(x);
#else
    return sqrtf(x);
#endif
}

static bento_real_t absolute(bento_real_t value)
{
    return (value < (bento_real_t)0) ? -value : value;
}

/* int(max(-90, min(90, v))): each bound replaces v only when v passes
 * it, as min() and max() compare. */
static int32_t clamp_angle(bento_real_t value)
{
    bento_real_t clamped = (bento_real_t)ANGLE_LIMIT;

    if (value < clamped) {
        clamped = value;
    }
    if (!(clamped > (bento_real_t)-ANGLE_LIMIT)) {
        clamped = (bento_real_t)-ANGLE_LIMIT;
    }
    return (int32_t)clamped;
}

/* The digits of '%.Nf', the '+' in front of a text without '-', then
 * right-aligned in the width with spaces. */
static void format_number(char *out, size_t capacity,
                          bento_real_t value,
                          const number_format_t *format)
{
    char digits[BENTO_FMT_BUF_SIZE];
    char signed_text[BENTO_FMT_BUF_SIZE + 1U];
    uint32_t length = 0U;

    (void)bento_fmt_real_precision(digits, (uint32_t)sizeof(digits),
                                   value, format->digits, &length);
    if (format->plus && (digits[0] != '-')) {
        (void)snprintf(signed_text, sizeof(signed_text), "+%s", digits);
    } else {
        (void)snprintf(signed_text, sizeof(signed_text), "%s", digits);
    }
    (void)snprintf(out, capacity, "%*s", (int)format->width,
                   signed_text);
}

/* A label's text becomes the prefix and the number. MicroPython's
 * widget setters ignore a refusal (modui.c), so the twin does too. */
static void show_number(const tilt_screen_t *screen, int32_t slot,
                        const char *prefix, bento_real_t value,
                        const number_format_t *format)
{
    char number[BENTO_FMT_BUF_SIZE +
                1U]; /* format_number(): a real and its sign */
    char text[TEXT_BYTES];

    format_number(number, sizeof(number), value, format);
    (void)snprintf(text, sizeof(text), "%s%s", prefix, number);
    (void)bento_ui_set_text(screen->ui, screen->labels[slot], text,
                            (uint32_t)strlen(text));
}

static bento_status_t add_labels(tilt_screen_t *screen,
                                 const label_spec_t *specs,
                                 int32_t count)
{
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < count) && (status == BENTO_OK); i++) {
        bento_ui_widget_args_t args;
        bento_ui_id_t made;

        bento_ui_widget_args_init(&args);
        args.text = specs[i].text;
        args.text_len = (uint32_t)strlen(specs[i].text);
        args.x = specs[i].x;
        args.y = specs[i].y;
        args.color = specs[i].color;
        args.font_px = specs[i].font_px;
        status = bento_ui_label_create(screen->ui, &args, &made);
        if ((status == BENTO_OK) && (specs[i].slot != SLOT_NONE)) {
            screen->labels[specs[i].slot] = made;
        }
    }
    return status;
}

/* The title and the three columns: raw values, the firmware's answer,
 * the formula computed here. */
static bento_status_t add_columns(tilt_screen_t *screen)
{
    static const label_spec_t columns[COLUMN_COUNT] = {
        {"dsp.tilt() = atan2 สองบรรทัด", 24, 24, FONT_TITLE,
         COLOR_DEFAULT, SLOT_NONE},
        {"ความเร่ง (m/s2)", 24, 62, FONT_TEXT, COLOR_GRAY, SLOT_NONE},
        {"ax  ----", 24, 92, FONT_TEXT, COLOR_DEFAULT, SLOT_AX},
        {"ay  ----", 24, 120, FONT_TEXT, COLOR_DEFAULT, SLOT_AY},
        {"az  ----", 24, 148, FONT_TEXT, COLOR_DEFAULT, SLOT_AZ},
        {"|a| ----", 24, 176, FONT_TEXT, COLOR_GRAY, SLOT_MAGNITUDE},
        {"dsp.tilt()", 224, 64, FONT_CAPTION, COLOR_GRAY, SLOT_NONE},
        {"roll   ----", 224, 92, FONT_TEXT, COLOR_ROLL, SLOT_ROLL},
        {"pitch  ----", 224, 120, FONT_TEXT, COLOR_PITCH, SLOT_PITCH},
        {"คำนวณเองด้วย atan2", 456, 62, FONT_TEXT, COLOR_GRAY,
         SLOT_NONE},
        {"roll   ----", 456, 92, FONT_TEXT, COLOR_ROLL, SLOT_MY_ROLL},
        {"pitch  ----", 456, 120, FONT_TEXT, COLOR_PITCH,
         SLOT_MY_PITCH}};
    static const label_spec_t difference = {
        "ต่างกัน ----", 456, 148, FONT_TEXT, COLOR_GRAY, SLOT_DIFFERENCE};
    bento_status_t status = add_labels(screen, columns, COLUMN_COUNT);

    if (status == BENTO_OK) {
        status = add_labels(screen, &difference, 1);
    }
    return status;
}

/* The exit button sits in the top right corner, the only 88 x 88 gap
 * this screen has left. */
static bento_status_t add_exit(tilt_screen_t *screen)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "ออก";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = EXIT_X;
    args.y = EXIT_Y;
    args.w = EXIT_SIZE;
    args.h = EXIT_SIZE;
    args.color = COLOR_EXIT;
    args.font_px = FONT_TEXT;
    return bento_ui_button_create(screen->ui, &args, &screen->exit);
}

/* Two lines, so tilting one way at a time shows the axes apart. */
static bento_status_t add_chart(tilt_screen_t *screen)
{
    static const label_spec_t note = {"กราฟ: แดง roll / เขียว pitch",
                                      CHART_X,
                                      CHART_NOTE_Y,
                                      FONT_TEXT,
                                      COLOR_GRAY,
                                      SLOT_NONE};
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CHART_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.min_val = -ANGLE_LIMIT;
    args.max_val = ANGLE_LIMIT;
    args.color = COLOR_ROLL;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    screen->roll_series = 0U;
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_PITCH,
                                           &screen->pitch_series);
    }
    if (status == BENTO_OK) {
        status = add_labels(screen, &note, 1);
    }
    return status;
}

/* The trap card: the same pair read in the wrong order. The card ends
 * at y = 340; below it is the corner the console button keeps. */
static bento_status_t add_trap_card(tilt_screen_t *screen)
{
    static const label_spec_t card[CARD_COUNT] = {
        {"ถ้าแกะสลับลำดับ", 496, 230, FONT_TEXT, COLOR_WARN, SLOT_NONE},
        {"pitch อ่านเป็น ----", 496, 260, FONT_TEXT, COLOR_WARN,
         SLOT_WRONG_PITCH},
        {"roll อ่านเป็น ----", 496, 292, FONT_TEXT, COLOR_WARN,
         SLOT_WRONG_ROLL},
        {"ไม่มี error ให้เห็นเลย", 480, 342, FONT_TEXT, COLOR_ROLL,
         SLOT_NONE}};
    bento_ui_widget_args_t args;
    bento_ui_id_t panel;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = CARD_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    status = bento_ui_panel_create(screen->ui, &args, &panel);
    if (status == BENTO_OK) {
        status = add_labels(screen, card, CARD_COUNT);
    }
    return status;
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(tilt_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_columns(screen);
    }
    if (status == BENTO_OK) {
        status = add_exit(screen);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = add_trap_card(screen);
    }
    return status;
}

/* The console explains the formula; one warm-up reading lets the long
 * first wait happen before the screen is built. lcd calls are sent
 * without an answer (modlcd.c). */
static bento_status_t open_console(void)
{
    bento_sensors_imu6_t motion;
    bento_status_t status;

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>dsp.tilt() คืออะไรกันแน่</h2>");
    (void)bento_lcd_print("roll  = atan2(ay, az)");
    (void)bento_lcd_print("pitch = atan2(-ax, sqrt(ay*ay + az*az))");
    (void)bento_lcd_print("คูณ 180/pi เพื่อเปลี่ยนเรเดียนเป็นองศา");
    bento_sensors_imu6_init(&motion);
    status = bento_sensors_bmi270_motion(&motion);
    if ((status != BENTO_OK) && (status != BENTO_E_UNSUPPORTED)) {
        (void)bento_lcd_print("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ในลูป");
        status = BENTO_OK;
    }
    return status;
}

/* The firmware's roll and pitch, and the same two lines written here:
 * roll is gravity's lean towards y against z; pitch is how far x
 * sinks below the yz plane, and the minus makes "nose up" positive. */
static bento_status_t compute_angles(const bento_sensors_imu6_t *motion,
                                     angles_t *angles)
{
    const bento_real_t pi = (bento_real_t)3.14159265358979323846;
    const bento_real_t to_degrees = (bento_real_t)180.0 / pi;
    const bento_real_t ax = (bento_real_t)motion->ax;
    const bento_real_t ay = (bento_real_t)motion->ay;
    const bento_real_t az = (bento_real_t)motion->az;
    const bento_real_t yz_squares = (ay * ay) + (az * az);
    float roll = 0.0F;
    float pitch = 0.0F;
    const bento_status_t status = bento_dsp_tilt(
        motion->ax, motion->ay, motion->az, &roll, &pitch);

    angles->roll = (bento_real_t)roll;
    angles->pitch = (bento_real_t)pitch;
    angles->my_roll = board_atan2(ay, az) * to_degrees;
    angles->my_pitch =
        board_atan2(-ax, board_sqrt(yz_squares)) * to_degrees;
    return status;
}

/* The raw column; |a| lying still should be about 9.81, otherwise the
 * board is moving and tilt cannot be trusted. */
static void show_raw(const tilt_screen_t *screen,
                     const bento_sensors_imu6_t *motion)
{
    const number_format_t axis = {3, true, 7};
    const number_format_t size = {3, false, 6};
    const bento_real_t ax = (bento_real_t)motion->ax;
    const bento_real_t ay = (bento_real_t)motion->ay;
    const bento_real_t az = (bento_real_t)motion->az;
    const bento_real_t squares = ((ax * ax) + (ay * ay)) + (az * az);

    show_number(screen, SLOT_AX, "ax  ", ax, &axis);
    show_number(screen, SLOT_AY, "ay  ", ay, &axis);
    show_number(screen, SLOT_AZ, "az  ", az, &axis);
    show_number(screen, SLOT_MAGNITUDE, "|a| ", board_sqrt(squares),
                &size);
}

/* The two angle columns, their difference, the trap card and the
 * chart. */
static void show_angles(const tilt_screen_t *screen,
                        const angles_t *angles)
{
    const number_format_t angle = {2, true, 7};
    const number_format_t tenth = {1, true, 0};
    const number_format_t fine = {4, false, 0};
    const bento_real_t roll_gap =
        absolute(angles->roll - angles->my_roll);
    const bento_real_t pitch_gap =
        absolute(angles->pitch - angles->my_pitch);
    const bento_real_t gap =
        (pitch_gap > roll_gap) ? pitch_gap : roll_gap;
    char number[BENTO_FMT_BUF_SIZE +
                1U]; /* format_number(): a real and its sign */
    char text[TEXT_BYTES];

    show_number(screen, SLOT_ROLL, "roll  ", angles->roll, &angle);
    show_number(screen, SLOT_PITCH, "pitch ", angles->pitch, &angle);
    show_number(screen, SLOT_MY_ROLL, "roll  ", angles->my_roll,
                &angle);
    show_number(screen, SLOT_MY_PITCH, "pitch ", angles->my_pitch,
                &angle);
    format_number(number, sizeof(number), gap, &fine);
    (void)snprintf(text, sizeof(text), "ต่างกัน %s องศา", number);
    (void)bento_ui_set_text(screen->ui, screen->labels[SLOT_DIFFERENCE],
                            text, (uint32_t)strlen(text));
    show_number(screen, SLOT_WRONG_PITCH, "pitch อ่านเป็น ", angles->roll,
                &tenth);
    show_number(screen, SLOT_WRONG_ROLL, "roll อ่านเป็น ", angles->pitch,
                &tenth);
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  screen->roll_series,
                                  clamp_angle(angles->roll));
    (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                  screen->pitch_series,
                                  clamp_angle(angles->pitch));
}

/* True when any event came from the exit button (the original reads
 * only ev['handle'], whatever the kind). ui.poll() answers no events
 * when it fails. */
static bool exit_tapped(const tilt_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bool tapped = false;

    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; i < count; i++) {
        if (events[i].id == screen->exit) {
            tapped = true;
        }
    }
    return tapped;
}

/* One pass; a failed reading should not blank the screen: it keeps the
 * old values and tries again after a short wait. */
static bento_status_t run_pass(const tilt_screen_t *screen,
                               bool *running)
{
    bento_sensors_imu6_t motion;
    angles_t angles;
    uint32_t wait_ms = STEP_MS;
    bento_status_t status;

    bento_sensors_imu6_init(&motion);
    status = bento_sensors_bmi270_motion(&motion);
    if (status == BENTO_OK) {
        status = compute_angles(&motion, &angles);
        if (status == BENTO_OK) {
            show_raw(screen, &motion);
            show_angles(screen, &angles);
            *running = !exit_tapped(screen);
        }
    } else if (status != BENTO_E_UNSUPPORTED) {
        (void)exit_tapped(
            screen); /* the original ignores events here */
        wait_ms = RETRY_MS;
        status = BENTO_OK;
    } else {
        /* no BMI270 path at all: the original ends here too */
    }
    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(wait_ms);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

int main(void)
{
    tilt_screen_t screen;
    bool running = true;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = open_console();
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    while ((status == BENTO_OK) && running) {
        status = run_pass(&screen, &running);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_clear(screen.ui); /* a setter: not checked */
        (void)printf("ช่อง 'ต่างกัน' อยู่ระดับ 0.0001 องศา "
                     "เพราะเฟิร์มแวร์คิดด้วย float 32 บิต\n");
        (void)printf("ส่วน Python คิดด้วย 64 บิต - "
                     "ต่างกันแค่นั้น ไม่ใช่สูตรคนละตัว\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
