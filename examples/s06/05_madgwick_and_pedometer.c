/*
 * 05_madgwick_and_pedometer.c -- Madgwick กับ Pedometer: หน่วยคือกับดัก
 * (คาบ 06)
 *
 * อ่าน BMI270 ทุก 100 ms ป้อน Madgwick (accel + gyro ที่แปลงเป็นเรเดียนต่อวินาทีแล้ว)
 * เทียบมุม roll pitch yaw กับ dsp.tilt() ที่ใช้ accel อย่างเดียว พร้อม quaternion
 * และนับก้าวด้วย Pedometer สองตัวที่ต่างกันแค่หน่วยที่ป้อน: ตัวเขียวป้อน g
 * ตัวส้มป้อน m/s2 กับค่าตั้งต้นของไลบรารี จึงนับขึ้นทั้งที่บอร์ดวางนิ่ง ปุ่ม "reset"
 * ล้างทั้งสามตัว ปุ่ม "ออก" จบโปรแกรม บอร์ด: Dev Kit และ Eva Kit
 *
 * Reads the BMI270 every 100 ms and feeds Madgwick (accel plus the
 * gyro converted to rad/s); its roll, pitch and yaw stand beside
 * dsp.tilt(), which uses the accelerometer alone, with the quaternion.
 * Two pedometers differ only in the unit they are fed: the green one
 * gets g, the orange one m/s2 with the library's default threshold, so
 * it counts while the board lies still. "reset" clears all three;
 * "exit" ends the program. Boards: Dev Kit and Eva Kit.
 *
 * Twin of 05_madgwick_and_pedometer.py
 * (embedded-systems-for-aiot-developer/examples/s06/05_madgwick_and_pedometer.py),
 * rewritten from the library twin aiot-s06-05-madgwick-and-pedometer
 * (the same program). Style: CLEAN_C.md.
 * What C changes only because the language forces it: a float is
 * bento_real_t, the width each board's MicroPython uses; dsp.Madgwick
 * and dsp.Pedometer are bento_dsp's by-value states, and the pedometer
 * is handed bento_time_ticks_ms() as its clock; the original's except
 * OSError around the reading is any failed BMI270 read here, except
 * BENTO_E_UNSUPPORTED (no sensors.bmi270 at all), which ends the
 * program as the AttributeError would; "{:+7.1f}" and its kin are
 * bento_fmt_real_precision with the sign and the right alignment added
 * as format() adds them, "{:5d}" is "%5u" of the step count and "{}"
 * of a flag is True or False; the buttons react to any event, as the
 * original reads only ev['handle'].
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
    SLOT_MADGWICK_ROLL = 0,
    SLOT_MADGWICK_PITCH = 1,
    SLOT_MADGWICK_YAW = 2,
    SLOT_TILT_ROLL = 3,
    SLOT_TILT_PITCH = 4,
    SLOT_STEPS_G = 5,
    SLOT_STEPS_MS2 = 6,
    SLOT_ACTIVE = 7,
    SLOT_QUATERNION = 8,
    SLOT_MAGNITUDE = 9,
    SLOT_COUNT = 10
};

/* Timing, filters and sizes. */
enum {
    LOOP_MS = 100, /* 10 Hz: Madgwick must be told the same rate */
    RETRY_MS = 50, /* after a failed reading */
    MIN_INTERVAL_MS = 300,
    HALF_TURN = 180,
    COLUMN_COUNT = 15,
    CARD_COUNT = 4,
    TEXT_BYTES = 128, /* a label or console text, at most 126 bytes */
    /* "q ", four numbers of up to 36 bytes, three spaces, the NUL */
    QUATERNION_BYTES = 150
};

/* Screen layout on the 792 x 398 canvas: three bands, 24 px margins;
 * the buttons end at y = 332, clear of the Console button's corner. */
enum {
    CARD_X = 24,
    CARD_Y = 204,
    CARD_W = 456,
    CARD_H = 128,
    BUTTON_Y = 244,
    BUTTON_W = 116,
    BUTTON_H = 88,
    RESET_X = 504,
    EXIT_X = 652
};

/* Font sizes, in pixels, and the course palette, one role each. */
enum {
    FONT_TITLE = 24,
    FONT_TEXT = 20,
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_OK = 0x30A46C
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

/* The widgets the program changes or listens to. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t labels[SLOT_COUNT];
    bento_ui_id_t reset;
    bento_ui_id_t exit;
} motion_screen_t;

/* The three filters: Madgwick, the pedometer fed in g and the one fed
 * in m/s2 with the library's defaults (the unit trap). */
typedef struct {
    bento_dsp_madgwick_state_t ahrs;
    bento_dsp_pedometer_state_t pedometer_g;
    bento_dsp_pedometer_state_t pedometer_ms2;
} filters_t;

/* What the two pedometers answered this round. */
typedef struct {
    uint32_t steps_g;
    uint32_t steps_ms2;
    bool active_g;
    bool active_ms2;
} steps_t;

static bento_real_t board_sqrt(bento_real_t x)
{
#if BENTO_FMT_REAL_DOUBLE
    return sqrt(x);
#else
    return sqrtf(x);
#endif
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

/* MicroPython's widget setters ignore a refusal (modui.c), so the twin
 * does too. */
static void set_slot_text(const motion_screen_t *screen, int32_t slot,
                          const char *text)
{
    (void)bento_ui_set_text(screen->ui, screen->labels[slot], text,
                            (uint32_t)strlen(text));
}

/* "{:+7.1f}" of an angle after its name. */
static void show_angle(const motion_screen_t *screen, int32_t slot,
                       const char *name, float degrees)
{
    const number_format_t angle = {1, true, 7};
    char number[BENTO_FMT_BUF_SIZE +
                1U]; /* format_number(): a real and its sign */
    char text[TEXT_BYTES];

    format_number(number, sizeof(number), (bento_real_t)degrees,
                  &angle);
    (void)snprintf(text, sizeof(text), "%s%s", name, number);
    set_slot_text(screen, slot, text);
}

static bento_status_t add_labels(motion_screen_t *screen,
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

/* The three angles are told apart by the name before each value, not
 * by colour, so they still read in black and white. The quaternion is
 * too long for a column and gets its own row. */
static bento_status_t add_columns(motion_screen_t *screen)
{
    static const label_spec_t columns[COLUMN_COUNT] = {
        {"Madgwick + Pedometer: หน่วยคือกับดัก", 24, 8, FONT_TITLE,
         COLOR_TEXT, SLOT_NONE},
        {"Madgwick (accel+gyro)", 24, 44, FONT_TEXT, COLOR_DIM,
         SLOT_NONE},
        {"roll   ----", 24, 72, FONT_TEXT, COLOR_TEXT,
         SLOT_MADGWICK_ROLL},
        {"pitch  ----", 24, 100, FONT_TEXT, COLOR_TEXT,
         SLOT_MADGWICK_PITCH},
        {"yaw    ----", 24, 128, FONT_TEXT, COLOR_TEXT,
         SLOT_MADGWICK_YAW},
        {"dsp.tilt() เทียบกัน", 280, 44, FONT_TEXT, COLOR_DIM, SLOT_NONE},
        {"roll   ----", 280, 72, FONT_TEXT, COLOR_TEXT, SLOT_TILT_ROLL},
        {"pitch  ----", 280, 100, FONT_TEXT, COLOR_TEXT,
         SLOT_TILT_PITCH},
        {"tilt ไม่มี yaw ให้", 280, 128, FONT_TEXT, COLOR_DIM, SLOT_NONE},
        {"Pedometer สองตัว", 520, 44, FONT_TEXT, COLOR_DIM, SLOT_NONE},
        {"g    ----", 520, 72, FONT_TEXT, COLOR_OK, SLOT_STEPS_G},
        {"m/s2 ----", 520, 100, FONT_TEXT, COLOR_WARN, SLOT_STEPS_MS2},
        {"active ----", 520, 128, FONT_TEXT, COLOR_DIM, SLOT_ACTIVE},
        {"q ----", 24, 164, FONT_TEXT, COLOR_DIM, SLOT_QUATERNION},
        {"|a| ----", 520, 164, FONT_TEXT, COLOR_DIM, SLOT_MAGNITUDE}};

    return add_labels(screen, columns, COLUMN_COUNT);
}

/* The unit-trap card; its lines share the left edge x = 40, the card's
 * 24 plus 16 inside. Then the caption of the button row. */
static bento_status_t add_trap_card(motion_screen_t *screen)
{
    static const label_spec_t card[CARD_COUNT] = {
        {"ตัวส้มนับขึ้นทั้งที่บอร์ดวางนิ่ง", 40, 224, FONT_TEXT, COLOR_WARN,
         SLOT_NONE},
        {"9.81 มากกว่าเกณฑ์ 1.5 ตั้งแต่ยังไม่ขยับ", 40, 260, FONT_TEXT,
         COLOR_WARN, SLOT_NONE},
        {"ค่าตั้งต้นของไลบรารี ไม่ใช่ค่าที่ถูก", 40, 296, FONT_TEXT, COLOR_BAD,
         SLOT_NONE},
        {"reset ทั้งสามตัว", 504, 204, FONT_TEXT, COLOR_DIM, SLOT_NONE}};
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

static bento_status_t add_button(bento_ui_t *ui, const char *text,
                                 int16_t x, uint32_t color,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = BUTTON_Y;
    args.w = BUTTON_W;
    args.h = BUTTON_H;
    args.color = color;
    args.font_px = FONT_TEXT;
    return bento_ui_button_create(ui, &args, out_id);
}

/* Draws the whole screen in the original's order (the widgets'
 * handles follow it). */
static bento_status_t build_screen(motion_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_columns(screen);
    }
    if (status == BENTO_OK) {
        status = add_trap_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_button(screen->ui, "reset", RESET_X, COLOR_ACCENT,
                            &screen->reset);
    }
    if (status == BENTO_OK) {
        status = add_button(screen->ui, "ออก", EXIT_X, COLOR_DIM,
                            &screen->exit);
    }
    return status;
}

/* beta says how much to trust the accelerometer against the gyro: high
 * pulls back to gravity fast but shakes with it (default 0.1); fs must
 * match the loop. The first pedometer's threshold is in g, the unit it
 * is fed; the second keeps the defaults and is fed m/s2. */
static bento_status_t start_filters(filters_t *filters)
{
    const float beta = 0.15F;
    const float rate_hz = 10.0F;
    const float threshold_g = 1.25F;
    bento_dsp_madgwick_config_t ahrs;
    bento_dsp_pedometer_config_t pedometer;
    bento_status_t status = bento_dsp_madgwick_config_init(&ahrs);

    if (status == BENTO_OK) {
        ahrs.beta = beta;
        ahrs.fs_hz = rate_hz;
        status = bento_dsp_madgwick_state_init(&filters->ahrs, &ahrs);
    }
    if (status == BENTO_OK) {
        status = bento_dsp_pedometer_config_init(&pedometer);
    }
    if (status == BENTO_OK) {
        pedometer.threshold = threshold_g;
        pedometer.min_interval_ms = MIN_INTERVAL_MS;
        status = bento_dsp_pedometer_state_init(&filters->pedometer_g,
                                                &pedometer);
    }
    if (status == BENTO_OK) {
        status = bento_dsp_pedometer_config_init(&pedometer);
    }
    if (status == BENTO_OK) {
        status = bento_dsp_pedometer_state_init(&filters->pedometer_ms2,
                                                &pedometer);
    }
    return status;
}

/* One warm-up reading, so the long first wait happens before the
 * screen is built. lcd calls are sent without an answer (modlcd.c). */
static bento_status_t warm_up(void)
{
    bento_sensors_imu6_t motion;
    bento_status_t status;

    bento_sensors_imu6_init(&motion);
    status = bento_sensors_bmi270_motion(&motion);
    if ((status != BENTO_OK) && (status != BENTO_E_UNSUPPORTED)) {
        (void)bento_lcd_print("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ในลูป");
        status = BENTO_OK;
    }
    return status;
}

/* Madgwick's three angles, in degrees. The gyro is converted to rad/s
 * first: fed degrees per second, the angles would turn about 57 times
 * too fast. */
static bento_status_t show_madgwick(const motion_screen_t *screen,
                                    filters_t *filters,
                                    const bento_sensors_imu6_t *motion)
{
    const bento_real_t pi = (bento_real_t)3.14159265358979323846;
    const bento_real_t to_radians = pi / (bento_real_t)HALF_TURN;
    bento_dsp_imu6_t input;
    bento_dsp_euler_t euler = {0.0F, 0.0F, 0.0F};
    bento_status_t status;

    input.ax = motion->ax;
    input.ay = motion->ay;
    input.az = motion->az;
    input.gx = (float)((bento_real_t)motion->gx * to_radians);
    input.gy = (float)((bento_real_t)motion->gy * to_radians);
    input.gz = (float)((bento_real_t)motion->gz * to_radians);
    status =
        bento_dsp_madgwick_state_update(&filters->ahrs, &input, &euler);
    if (status == BENTO_OK) {
        show_angle(screen, SLOT_MADGWICK_ROLL, "roll  ",
                   euler.roll_deg);
        show_angle(screen, SLOT_MADGWICK_PITCH, "pitch ",
                   euler.pitch_deg);
        show_angle(screen, SLOT_MADGWICK_YAW, "yaw   ", euler.yaw_deg);
    }
    return status;
}

/* The quaternion (w, x, y, z) is the state the filter really keeps;
 * Euler angles are only its readable form, with a dead point at pitch
 * 90 degrees. */
static bento_status_t show_quaternion(const motion_screen_t *screen,
                                      const filters_t *filters)
{
    const number_format_t part = {2, true, 0};
    bento_dsp_quat_t quaternion = {0.0F, 0.0F, 0.0F, 0.0F};
    const bento_status_t status = bento_dsp_madgwick_state_quaternion(
        &filters->ahrs, &quaternion);

    if (status == BENTO_OK) {
        char w_text[BENTO_FMT_BUF_SIZE + 1U];
        char x_text[BENTO_FMT_BUF_SIZE + 1U];
        char y_text[BENTO_FMT_BUF_SIZE + 1U];
        char z_text[BENTO_FMT_BUF_SIZE + 1U];
        char text[QUATERNION_BYTES];

        format_number(w_text, sizeof(w_text), quaternion.w, &part);
        format_number(x_text, sizeof(x_text), quaternion.x, &part);
        format_number(y_text, sizeof(y_text), quaternion.y, &part);
        format_number(z_text, sizeof(z_text), quaternion.z, &part);
        (void)snprintf(text, sizeof(text), "q %s %s %s %s", w_text,
                       x_text, y_text, z_text);
        set_slot_text(screen, SLOT_QUATERNION, text);
    }
    return status;
}

/* dsp.tilt() uses the accelerometer alone, so it has no yaw; lying
 * still it agrees with Madgwick, shaken it is noisier. */
static bento_status_t show_tilt(const motion_screen_t *screen,
                                const bento_sensors_imu6_t *motion)
{
    float roll = 0.0F;
    float pitch = 0.0F;
    const bento_status_t status = bento_dsp_tilt(
        motion->ax, motion->ay, motion->az, &roll, &pitch);

    if (status == BENTO_OK) {
        show_angle(screen, SLOT_TILT_ROLL, "roll  ", roll);
        show_angle(screen, SLOT_TILT_PITCH, "pitch ", pitch);
    }
    return status;
}

/* The same pedometer twice; only the unit fed differs. */
static bento_status_t count_steps(filters_t *filters,
                                  const bento_sensors_imu6_t *motion,
                                  steps_t *steps)
{
    const bento_real_t gravity = (bento_real_t)9.80665;
    const bento_real_t ax = (bento_real_t)motion->ax;
    const bento_real_t ay = (bento_real_t)motion->ay;
    const bento_real_t az = (bento_real_t)motion->az;
    const bento_real_t ax_in_g = ax / gravity;
    const bento_real_t ay_in_g = ay / gravity;
    const bento_real_t az_in_g = az / gravity;
    bento_status_t status = bento_dsp_pedometer_state_update(
        &filters->pedometer_g, (float)ax_in_g, (float)ay_in_g,
        (float)az_in_g, bento_time_ticks_ms(), &steps->steps_g,
        &steps->active_g);

    if (status == BENTO_OK) {
        status = bento_dsp_pedometer_state_update(
            &filters->pedometer_ms2, motion->ax, motion->ay, motion->az,
            bento_time_ticks_ms(), &steps->steps_ms2,
            &steps->active_ms2);
    }
    return status;
}

/* The two counts, the active flags (True only in the round that just
 * counted a step) and the magnitude. */
static void show_steps(const motion_screen_t *screen,
                       const steps_t *steps,
                       const bento_sensors_imu6_t *motion)
{
    const number_format_t size = {2, false, 5};
    const bento_real_t ax = (bento_real_t)motion->ax;
    const bento_real_t ay = (bento_real_t)motion->ay;
    const bento_real_t az = (bento_real_t)motion->az;
    const bento_real_t squares = (ax * ax) + (ay * ay) + (az * az);
    char number[BENTO_FMT_BUF_SIZE +
                1U]; /* format_number(): a real and its sign */
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "g    %5u ก้าว",
                   (unsigned int)steps->steps_g);
    set_slot_text(screen, SLOT_STEPS_G, text);
    (void)snprintf(text, sizeof(text), "m/s2 %5u ก้าว",
                   (unsigned int)steps->steps_ms2);
    set_slot_text(screen, SLOT_STEPS_MS2, text);
    (void)snprintf(text, sizeof(text), "active %s / %s",
                   steps->active_g ? "True" : "False",
                   steps->active_ms2 ? "True" : "False");
    set_slot_text(screen, SLOT_ACTIVE, text);
    format_number(number, sizeof(number), board_sqrt(squares), &size);
    (void)snprintf(text, sizeof(text), "|a| %s m/s2", number);
    set_slot_text(screen, SLOT_MAGNITUDE, text);
}

/* Madgwick goes back to (1, 0, 0, 0), the reference pose; each
 * pedometer drops its count and latch. */
static bento_status_t reset_filters(filters_t *filters)
{
    bento_status_t status =
        bento_dsp_madgwick_state_reset(&filters->ahrs);

    if (status == BENTO_OK) {
        status = bento_dsp_pedometer_state_reset(&filters->pedometer_g);
    }
    if (status == BENTO_OK) {
        status =
            bento_dsp_pedometer_state_reset(&filters->pedometer_ms2);
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_print(
            "reset แล้ว - yaw กลับไป 0 และตัวนับทั้งสองเริ่มใหม่");
    }
    return status;
}

/* The buttons react to any event kind; ui.poll() answers no events
 * when it fails. */
static bento_status_t handle_events(const motion_screen_t *screen,
                                    filters_t *filters, bool *running)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    if (bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U;
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        if (events[i].id == screen->reset) {
            status = reset_filters(filters);
        } else if (events[i].id == screen->exit) {
            *running = false;
        } else {
            /* another widget: nothing to do */
        }
    }
    return status;
}

/* One good reading, through every filter and onto the screen. */
static bento_status_t show_reading(const motion_screen_t *screen,
                                   filters_t *filters,
                                   const bento_sensors_imu6_t *motion)
{
    steps_t steps = {0U, 0U, false, false};
    bento_status_t status = show_madgwick(screen, filters, motion);

    if (status == BENTO_OK) {
        status = show_quaternion(screen, filters);
    }
    if (status == BENTO_OK) {
        status = show_tilt(screen, motion);
    }
    if (status == BENTO_OK) {
        status = count_steps(filters, motion, &steps);
    }
    if (status == BENTO_OK) {
        show_steps(screen, &steps, motion);
    }
    return status;
}

/* One pass; a failed reading polls, waits a little and tries again. */
static bento_status_t run_pass(const motion_screen_t *screen,
                               filters_t *filters, bool *running)
{
    bento_sensors_imu6_t motion;
    uint32_t wait_ms = LOOP_MS;
    bento_status_t status;

    bento_sensors_imu6_init(&motion);
    status = bento_sensors_bmi270_motion(&motion);
    if (status == BENTO_OK) {
        status = show_reading(screen, filters, &motion);
        if (status == BENTO_OK) {
            status = handle_events(screen, filters, running);
        }
    } else if (status != BENTO_E_UNSUPPORTED) {
        bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
        uint32_t count = 0U;

        /* the original drops these events */
        (void)bento_ui_poll(screen->ui, events,
                            BENTO_UI_POLL_EVENTS_MAX, &count, NULL);
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
    motion_screen_t screen;
    filters_t filters;
    bool running = true;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(&filters, 0, sizeof(filters));
    screen.ui = bento_runtime_get_ui();
    (void)bento_lcd_clear(); /* sent without an answer (modlcd.c) */
    (void)bento_lcd_console("<h2>Madgwick กับ Pedometer</h2>");
    status = start_filters(&filters);
    if (status == BENTO_OK) {
        status = warm_up();
    }
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    while ((status == BENTO_OK) && running) {
        status = run_pass(&screen, &filters, &running);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_clear(screen.ui); /* a setter: not checked */
        (void)printf("yaw ของ Madgwick จะค่อย ๆ หนีเมื่อวางนิ่งนาน ๆ "
                     "เพราะไม่มี accel ตัวไหน\n");
        (void)printf("บอกทิศเหนือได้ ต้องมีเข็มทิศเข้ามาช่วย - "
                     "ดู 04_compass_and_magnetometer.py\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
