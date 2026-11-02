/*
 * 04_compass_and_magnetometer.c -- เข็มทิศ BMM350: ทิศถูก ขนาดยังไม่มีข้อสรุป
 * (คาบ 06)
 *
 * อ่านสนามแม่เหล็กสามแกนกับทิศจาก BMM350 ทุก 200 ms จอมีสามคอลัมน์: สนามแม่เหล็ก
 * กับขนาด ทิศจาก heading() ของไดรเวอร์เทียบกับ dsp.compass() พร้อมผลต่างแบบวงกลม
 * และสถานะการสอบเทียบ hard-iron การ์ดด้านล่างบอกตรง ๆ ว่าขนาดที่บอร์ดรายงาน
 * อยู่นอกช่วง 25-65 uT ของโลก ปุ่ม "ล้างค่า" เริ่มสอบเทียบใหม่ (หมุนบอร์ดครบรอบ)
 * ปุ่ม "ออก" จบโปรแกรม บอร์ด: Dev Kit และ Eva Kit
 *
 * Reads the BMM350's three magnetic axes and its heading every 200 ms.
 * The screen has three columns: the field and its magnitude, the
 * driver's heading() against dsp.compass() with their circular
 * difference, and the hard-iron calibration state. The card below says
 * plainly that the magnitude the board reports lies outside the
 * Earth's 25-65 uT. The "reset" button starts the calibration again
 * (turn the board a full circle); "exit" ends the program. Boards: Dev
 * Kit and Eva Kit.
 *
 * Twin of 04_compass_and_magnetometer.py
 * (embedded-systems-for-aiot-developer/examples/s06/04_compass_and_magnetometer.py),
 * started from the library twin aiot-s06-04-compass-and-magnetometer.
 * Style: CLEAN_C.md.
 * What C changes only because the language forces it: a float is
 * bento_real_t, the width each board's MicroPython uses; the
 * original's except OSError around chip_id() and around magnetic() and
 * heading() is any failed read here, except BENTO_E_UNSUPPORTED (no
 * sensors.bmm350 at all), which ends the program as the AttributeError
 * would; hex() is "0x%x" and str(bool) is True or False; "{:+9.2f}"
 * and its kin are bento_fmt_real_precision with the sign and the right
 * alignment added as format() adds them; the binding's cal_reset()
 * prints its own console line, which bento_sensors_bmm350_cal_reset()
 * does not, so this program prints it; the buttons react to any event,
 * as the original reads only ev['handle'].
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
    SLOT_MX = 0,
    SLOT_MY = 1,
    SLOT_MZ = 2,
    SLOT_MAGNITUDE = 3,
    SLOT_DRIVER = 4,
    SLOT_DSP = 5,
    SLOT_GAP = 6,
    SLOT_VALID = 7,
    SLOT_OFFSET_X = 8,
    SLOT_OFFSET_Y = 9,
    SLOT_CLAIM = 10,
    SLOT_COUNT = 11
};

/* Timing, angles and sizes. */
enum {
    LOOP_MS = 200,
    HALF_TURN = 180,
    FULL_TURN = 360,
    COLUMN_COUNT = 15,
    CARD_COUNT = 4,
    TEXT_BYTES = 128 /* a label or console text, at most 126 bytes */
};

/* Screen layout on the 792 x 398 canvas: three bands, 24 px margins;
 * the buttons end at y = 320, clear of the Console button's corner. */
enum {
    CARD_X = 24,
    CARD_Y = 192,
    CARD_W = 456,
    CARD_H = 128,
    BUTTON_Y = 232,
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

/* The Earth's field in the textbooks, a ruler for the screen (uT). */
static const bento_real_t EARTH_MIN = (bento_real_t)25.0;
static const bento_real_t EARTH_MAX = (bento_real_t)65.0;

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
} magnet_screen_t;

/* One good reading: the three axes and the driver's heading. */
typedef struct {
    float mx;
    float my;
    float mz;
    float heading;
} reading_t;

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
static void set_slot_text(const magnet_screen_t *screen, int32_t slot,
                          const char *text)
{
    (void)bento_ui_set_text(screen->ui, screen->labels[slot], text,
                            (uint32_t)strlen(text));
}

/* A label's text becomes the prefix and the number. */
static void show_number(const magnet_screen_t *screen, int32_t slot,
                        const char *prefix, bento_real_t value,
                        const number_format_t *format)
{
    char number[TEXT_BYTES];
    char text[TEXT_BYTES];

    format_number(number, sizeof(number), value, format);
    (void)snprintf(text, sizeof(text), "%s%s", prefix, number);
    set_slot_text(screen, slot, text);
}

static bento_status_t add_labels(magnet_screen_t *screen,
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

/* The two headings are told apart by the function's name on the same
 * line, not by colour, so both still read in black and white. */
static bento_status_t add_columns(magnet_screen_t *screen)
{
    static const label_spec_t columns[COLUMN_COUNT] = {
        {"BMM350: ทิศถูก ขนาดยังไม่มีข้อสรุป", 24, 8, FONT_TITLE, COLOR_TEXT,
         SLOT_NONE},
        {"สนามแม่เหล็ก", 24, 44, FONT_TEXT, COLOR_DIM, SLOT_NONE},
        {"mx  ----", 24, 72, FONT_TEXT, COLOR_TEXT, SLOT_MX},
        {"my  ----", 24, 100, FONT_TEXT, COLOR_TEXT, SLOT_MY},
        {"mz  ----", 24, 128, FONT_TEXT, COLOR_TEXT, SLOT_MZ},
        {"|m| ----", 24, 156, FONT_TEXT, COLOR_WARN, SLOT_MAGNITUDE},
        {"ทิศ (องศา)", 264, 44, FONT_TEXT, COLOR_DIM, SLOT_NONE},
        {"heading()  ----", 264, 72, FONT_TEXT, COLOR_TEXT,
         SLOT_DRIVER},
        {"compass()  ----", 264, 100, FONT_TEXT, COLOR_TEXT, SLOT_DSP},
        {"ต่างกัน ----", 264, 128, FONT_TEXT, COLOR_TEXT, SLOT_GAP},
        {"คนละสูตร จึงคนละคำตอบ", 264, 156, FONT_TEXT, COLOR_DIM,
         SLOT_NONE},
        {"การสอบเทียบ", 504, 44, FONT_TEXT, COLOR_DIM, SLOT_NONE},
        {"valid  ----", 504, 72, FONT_TEXT, COLOR_TEXT, SLOT_VALID},
        {"off_x  ----", 504, 100, FONT_TEXT, COLOR_DIM, SLOT_OFFSET_X},
        {"off_y  ----", 504, 128, FONT_TEXT, COLOR_DIM, SLOT_OFFSET_Y}};

    return add_labels(screen, columns, COLUMN_COUNT);
}

/* The flaw card says what is still open instead of hiding it; its
 * lines share the left edge x = 40, the card's 24 plus 16 inside. */
static bento_status_t add_flaw_card(magnet_screen_t *screen)
{
    static const label_spec_t card[CARD_COUNT] = {
        {"สนามแม่เหล็กโลกจริงอยู่ที่ 25-65 uT", 40, 212, FONT_TEXT, COLOR_WARN,
         SLOT_NONE},
        {"บอร์ดรายงาน ---- นอกช่วงนั้นมาก", 40, 248, FONT_TEXT, COLOR_WARN,
         SLOT_CLAIM},
        {"ทิศถูก แต่ขนาดกับหน่วยยังไม่มีข้อสรุป", 40, 284, FONT_TEXT, COLOR_BAD,
         SLOT_NONE},
        {"cal_reset แล้วหมุน 360", 504, 192, FONT_TEXT, COLOR_DIM,
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
static bento_status_t build_screen(magnet_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_columns(screen);
    }
    if (status == BENTO_OK) {
        status = add_flaw_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_button(screen->ui, "ล้างค่า", RESET_X, COLOR_ACCENT,
                            &screen->reset);
    }
    if (status == BENTO_OK) {
        status = add_button(screen->ui, "ออก", EXIT_X, COLOR_DIM,
                            &screen->exit);
    }
    return status;
}

/* The BMM350 is on its own I3C bus, which the display core does not
 * hold, so it answers directly on both boards. lcd calls are sent
 * without an answer (modlcd.c). */
static bento_status_t open_console(void)
{
    uint8_t chip = 0U;
    bento_status_t status;

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>เข็มทิศ BMM350</h2>");
    status = bento_sensors_bmm350_chip_id(&chip);
    if (status == BENTO_OK) {
        char text[TEXT_BYTES];

        (void)snprintf(text, sizeof(text), "chip_id = 0x%x",
                       (unsigned int)chip);
        (void)bento_lcd_print(text);
    } else if (status != BENTO_E_UNSUPPORTED) {
        (void)bento_lcd_print(
            "อ่าน chip_id ไม่ได้ - ตรวจว่าบอร์ดมี BMM350 จริงไหม");
        status = BENTO_OK;
    } else {
        /* no BMM350 path at all: the original ends here too */
    }
    return status;
}

/* The three axes, then the driver's heading, which subtracts the
 * hard-iron offset (once calibrated) and averages the last ten values
 * on a circle, so it is calmer than dsp.compass(). */
static bento_status_t read_magnet(reading_t *reading, bool *ok)
{
    bento_status_t status = bento_sensors_bmm350_magnetic(
        &reading->mx, &reading->my, &reading->mz);

    if (status == BENTO_OK) {
        status = bento_sensors_bmm350_heading(&reading->heading);
    }
    *ok = (status == BENTO_OK);
    if ((status != BENTO_OK) && (status != BENTO_E_UNSUPPORTED)) {
        status = BENTO_OK; /* the original skips this round */
    }
    return status;
}

/* The field's axes and magnitude, and the flaw card's claim. */
static void show_field(const magnet_screen_t *screen,
                       const reading_t *reading)
{
    const number_format_t axis = {2, true, 9};
    const number_format_t size = {2, false, 9};
    const number_format_t whole = {0, false, 0};
    const bento_real_t mx = (bento_real_t)reading->mx;
    const bento_real_t my = (bento_real_t)reading->my;
    const bento_real_t mz = (bento_real_t)reading->mz;
    const bento_real_t squares = (mx * mx) + (my * my) + (mz * mz);
    const bento_real_t magnitude = (bento_real_t)sqrt((double)squares);
    const char *note = "อยู่ในช่วงของโลกพอดี";
    char number[TEXT_BYTES];
    char text[TEXT_BYTES];

    show_number(screen, SLOT_MX, "mx  ", mx, &axis);
    show_number(screen, SLOT_MY, "my  ", my, &axis);
    show_number(screen, SLOT_MZ, "mz  ", mz, &axis);
    show_number(screen, SLOT_MAGNITUDE, "|m| ", magnitude, &size);
    if (magnitude < EARTH_MIN) {
        note = "ต่ำกว่าช่วงของโลก";
    } else if (magnitude > EARTH_MAX) {
        note = "สูงกว่าช่วงของโลก";
    } else {
        /* inside the Earth's range: the note above stays */
    }
    format_number(number, sizeof(number), magnitude, &whole);
    (void)snprintf(text, sizeof(text), "รายงาน %s - %s", number, note);
    set_slot_text(screen, SLOT_CLAIM, text);
}

/* The two headings and their circular difference: 350 and 10 are 20
 * apart, not 340. dsp.compass() takes three values but drops mz, so it
 * does not correct for tilt yet. */
static bento_status_t show_headings(const magnet_screen_t *screen,
                                    const reading_t *reading)
{
    const number_format_t angle = {1, false, 6};
    const number_format_t tenth = {1, false, 0};
    float compass = 0.0F;
    const bento_status_t status = bento_dsp_compass(
        reading->mx, reading->my, reading->mz, &compass);

    if (status == BENTO_OK) {
        const bento_real_t driver = (bento_real_t)reading->heading;
        const bento_real_t computed = (bento_real_t)compass;
        bento_real_t gap = driver - computed;
        char number[TEXT_BYTES];
        char text[TEXT_BYTES];

        show_number(screen, SLOT_DRIVER, "heading()  ", driver, &angle);
        show_number(screen, SLOT_DSP, "compass()  ", computed, &angle);
        gap = (gap < (bento_real_t)0) ? -gap : gap;
        if (gap > (bento_real_t)HALF_TURN) {
            gap = (bento_real_t)FULL_TURN - gap;
        }
        format_number(number, sizeof(number), gap, &tenth);
        (void)snprintf(text, sizeof(text), "ต่างกัน %s องศา", number);
        set_slot_text(screen, SLOT_GAP, text);
    }
    return status;
}

/* valid turns True by itself: the board feeds the collector in the
 * background, and it needs 50 samples spanning more than 15 units on
 * both axes. */
static bento_status_t show_calibration(const magnet_screen_t *screen)
{
    const number_format_t offset = {2, true, 8};
    bento_sensors_bmm350_calibration_t calibration;
    bento_status_t status;

    bento_sensors_bmm350_cal_init(&calibration);
    status = bento_sensors_bmm350_cal_status(&calibration);
    if (status == BENTO_OK) {
        const bool valid = (calibration.valid != 0U);

        set_slot_text(screen, SLOT_VALID,
                      valid ? "valid  True" : "valid  False");
        (void)bento_ui_set_color(screen->ui, screen->labels[SLOT_VALID],
                                 valid ? COLOR_OK : COLOR_WARN);
        show_number(screen, SLOT_OFFSET_X, "off_x  ",
                    (bento_real_t)calibration.offset_x, &offset);
        show_number(screen, SLOT_OFFSET_Y, "off_y  ",
                    (bento_real_t)calibration.offset_y, &offset);
    }
    return status;
}

/* cal_reset() clears the collected min and max; the board must turn a
 * full circle so both axes see their extremes again. */
static bento_status_t reset_calibration(void)
{
    const bento_status_t status = bento_sensors_bmm350_cal_reset();

    if (status == BENTO_OK) {
        (void)printf("BMM350: calibration reset. Rotate board 360 "
                     "deg.\n");
        (void)bento_lcd_print("ล้างค่าสอบเทียบ - หมุนบอร์ดช้า ๆ ครบรอบ");
    }
    return status;
}

/* The buttons react to any event kind; ui.poll() answers no events
 * when it fails. */
static bento_status_t handle_events(const magnet_screen_t *screen,
                                    bool *running)
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
            status = reset_calibration();
        } else if (events[i].id == screen->exit) {
            *running = false;
        } else {
            /* another widget: nothing to do */
        }
    }
    return status;
}

static bento_status_t run_round(const magnet_screen_t *screen,
                                bool *running)
{
    reading_t reading = {0.0F, 0.0F, 0.0F, 0.0F};
    bool ok = false;
    bento_status_t status = read_magnet(&reading, &ok);

    if ((status == BENTO_OK) && ok) {
        show_field(screen, &reading);
        status = show_headings(screen, &reading);
        if (status == BENTO_OK) {
            status = show_calibration(screen);
        }
    }
    if (status == BENTO_OK) {
        status = handle_events(screen, running);
    }
    if (status == BENTO_OK) {
        status = bento_time_sleep_ms(LOOP_MS);
    }
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

int main(void)
{
    magnet_screen_t screen;
    bool running = true;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    status = open_console();
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    while ((status == BENTO_OK) && running) {
        status = run_round(&screen, &running);
    }
    if (status == BENTO_OK) {
        (void)bento_ui_clear(screen.ui); /* a setter: not checked */
        (void)printf("สิ่งที่ไฟล์นี้พิสูจน์ได้: ทิศเชื่อได้ "
                     "และสองฟังก์ชันทิศคนละสูตรกันจริง\n");
        (void)printf("สิ่งที่ไฟล์นี้พิสูจน์ไม่ได้: ตัวเลขขนาดกับหน่วย uT "
                     "ตัวไหนคือตัวที่ผิด\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
