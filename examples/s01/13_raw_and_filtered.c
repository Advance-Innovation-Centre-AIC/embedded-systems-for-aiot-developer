/*
 * 13_raw_and_filtered.c -- ค่าดิบกับค่าที่กรองแล้ว บนกราฟใบเดียว (Raw and
 * filtered on one chart)
 *
 * ค่าจากเซนเซอร์จริงไม่เคยนิ่ง การกรองไม่ได้ทำให้ค่าถูกขึ้น แต่ทำให้ค่าที่
 * รายงานออกไปใช้ตัดสินใจได้ สามเส้นบนกราฟใบเดียว เทาคือค่าดิบ เขียวคือ EMA
 * ส้มคือ Median วางบอร์ดนิ่ง ๆ สามเส้นทาบกัน พอเคาะโต๊ะหรือเขย่า เส้นเทาพุ่ง
 * อีกสองเส้นค่อย ๆ ตามขึ้นแล้วกลับลงอย่างเรียบ
 * กับดัก: Chart รับเฉพาะจำนวนเต็ม ค่าเร่งราว 9.8 จึงต้องคูณสิบก่อนส่ง
 *
 * Real sensor values are never still. Filtering does not make them
 * more correct; it makes the value you report usable for a decision.
 * Three lines on one chart: grey (blue here) is the raw magnitude,
 * green the EMA, orange the Median. Keep the board still and the three
 * lie on top of each other; knock the table and the raw line jumps
 * while the other two follow smoothly. Trap: a Chart takes integers
 * only, so the acceleration (about 9.8) is multiplied by ten first,
 * and the chart's vertical range is fixed when it is made.
 *
 * Twin of 13_raw_and_filtered.py
 * (embedded-systems-for-aiot-developer/examples/s01/13_raw_and_filtered.py).
 * Style: CLEAN_C.md.
 * Where C differs: dsp.EMA and dsp.Median are state structs passed by
 * pointer; floats are bento_real_t (32-bit on the PSoC Edge, as its
 * MicroPython) and str(ALPHA) is bento_fmt_real(); the except OSError
 * around the IMU read is a status test that counts a miss.
 *
 * ตาคุณ: ตั้ง ALPHA = 0.9 แล้วเคาะโต๊ะ จากนั้นลอง 0.02 ดูว่าเส้นเขียวเปลี่ยนไป
 * ทางไหน
 * Your turn: set ALPHA to 0.9 and knock the table, then try 0.02, and
 * say how the green line changes (hint: ALPHA is the weight of the
 * newest value).
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_dsp.h"
#include "bento_fmt.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_time.h"

enum {
    WINDOW = 7,      /* the Median's window, odd */
    CHART_MAX = 300, /* acceleration x 10; flat and still is about 98 */
    SCALE_X10 = 10,  /* the chart reads ten times m/s^2 */
    BUMP_X10 = 150,  /* above this the board was knocked */
    RAW_SERIES = 0,  /* the line every chart is born with */
    TEXT_BYTES = 128,
    REAL_BYTES = 32
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the CM55 core finish clearing the screen */
    RUN_MS = 40000,
    TICK_MS = 100
};

/* Layout on the 792 x 398 canvas. */
enum {
    FONT_PX = 20,
    SMALL_FONT_PX = 16, /* Latin-only captions; Thai needs 20 */
    TITLE_FONT_PX = 24,
    LEFT_X = 24,
    TITLE_Y = 24,
    STATUS_Y = 72,
    CHART_Y = 112,
    CHART_W = 448,
    CHART_H = 200,
    CARD_X = 488,
    CARD_W = 280,
    CARD_H = 228,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    CAPTION_X = 504,
    DIGITS_X = 632,
    DIGITS_W = 120,
    DIGITS_H = 48,
    RAW_CAPTION_Y = 138,
    RAW_DIGITS_Y = 128,
    EMA_CAPTION_Y = 196,
    EMA_DIGITS_Y = 184,
    MEDIAN_CAPTION_Y = 252,
    MEDIAN_DIGITS_Y = 240,
    TILT_Y = 292,
    LEGEND_Y = 320,
    LEGEND_EMA_X = 176,
    LEGEND_MEDIAN_X = 328
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_RAW = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* The smaller, the steadier and the slower to follow (dsp's own
 * default is 0.1). */
static const bento_real_t ALPHA = (bento_real_t)0.15;

typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} text_style_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t status;
    bento_ui_id_t chart;
    uint8_t ema_series;
    uint8_t median_series;
    bento_ui_id_t raw_digits;
    bento_ui_id_t ema_digits;
    bento_ui_id_t median_digits;
    bento_ui_id_t tilt;
} filter_screen_t;

/* The two filters fed the same raw value, and the run's tally. The
 * highest value each line reached is the evidence of what filtering
 * really did. */
typedef struct {
    bento_dsp_ema_state_t ema;
    bento_dsp_median_state_t median;
    int32_t rounds;
    int32_t misses;
    int32_t peak_raw;
    int32_t peak_ema;
} filter_run_t;

typedef struct {
    float ax;
    float ay;
    float az;
} acceleration_t;

static bento_status_t sleep_checked(uint32_t ms)
{
    bento_status_t status = bento_time_sleep_ms(ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* The widget setters ignore a refusal in MicroPython (modui.c), so
 * they do here too. */
static void set_text(const filter_screen_t *screen,
                     bento_ui_id_t widget, const char *text)
{
    (void)bento_ui_set_text(screen->ui, widget, text,
                            (uint32_t)strlen(text));
}

static void set_number(const filter_screen_t *screen,
                       bento_ui_id_t widget, int32_t number)
{
    char text[TEXT_BYTES];

    (void)snprintf(text, sizeof(text), "%" PRId32, number);
    set_text(screen, widget, text);
}

static void poll_once(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* ui.poll() answers no events when it fails; none are read */
    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const text_style_t *style,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = style->x;
    args.y = style->y;
    args.color = style->color;
    args.font_px = style->font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_digits(bento_ui_t *ui, int16_t y,
                                 uint32_t color, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = "0";
    args.text_len = 1U;
    args.x = DIGITS_X;
    args.y = y;
    args.w = DIGITS_W;
    args.h = DIGITS_H;
    args.color = color;
    return bento_ui_seg7_create(ui, &args, out_id);
}

/* The colour given when a Chart is made is line 0's, a line every
 * chart is born with, so the first add_series() answers 1, not 0:
 * keep the number it answers instead of guessing. */
static bento_status_t add_chart(filter_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = LEFT_X;
    args.y = CHART_Y;
    args.w = CHART_W;
    args.h = CHART_H;
    args.color = COLOR_RAW;
    args.min_val = 0;
    args.max_val = CHART_MAX;
    status = bento_ui_chart_create(screen->ui, &args, &screen->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            screen->ui, screen->chart, COLOR_OK, &screen->ema_series);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(screen->ui, screen->chart,
                                           COLOR_WARN,
                                           &screen->median_series);
    }
    return status;
}

/* The card is made before what sits on it: LVGL draws in the order of
 * creation, and a card made later would paint over the numbers. */
static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = CARD_X;
    args.y = CHART_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border colour */
    args.max_val = CARD_RADIUS; /* and max= its corner radius */
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &card);
}

/* One row of the card: the caption on the left, the number right. */
static bento_status_t add_row(bento_ui_t *ui, const char *caption,
                              const text_style_t *style,
                              int16_t digits_y, bento_ui_id_t *out_id)
{
    bento_ui_id_t unused;
    const text_style_t dim = {CAPTION_X, style->y, COLOR_DIM,
                              style->font_px};
    bento_status_t status = add_label(ui, caption, &dim, &unused);

    if (status == BENTO_OK) {
        status = add_digits(ui, digits_y, style->color, out_id);
    }
    return status;
}

/* The numbers are Seg7 (always 28 px); the tilt is a plain label. */
static bento_status_t build_card(filter_screen_t *screen)
{
    const text_style_t raw = {CAPTION_X, RAW_CAPTION_Y, COLOR_RAW,
                              FONT_PX};
    const text_style_t ema = {CAPTION_X, EMA_CAPTION_Y, COLOR_OK,
                              SMALL_FONT_PX};
    const text_style_t median = {CAPTION_X, MEDIAN_CAPTION_Y,
                                 COLOR_WARN, SMALL_FONT_PX};
    const text_style_t tilt_caption = {CAPTION_X, TILT_Y, COLOR_DIM,
                                       FONT_PX};
    const text_style_t tilt = {DIGITS_X, TILT_Y, COLOR_TEXT, FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status = add_card(screen->ui);

    if (status == BENTO_OK) {
        status = add_row(screen->ui, "ค่าดิบ x10", &raw, RAW_DIGITS_Y,
                         &screen->raw_digits);
    }
    if (status == BENTO_OK) {
        status = add_row(screen->ui, "EMA x10", &ema, EMA_DIGITS_Y,
                         &screen->ema_digits);
    }
    if (status == BENTO_OK) {
        status = add_row(screen->ui, "Median x10", &median,
                         MEDIAN_DIGITS_Y, &screen->median_digits);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "เอียง องศา", &tilt_caption, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "รอค่า", &tilt, &screen->tilt);
    }
    return status;
}

static bento_status_t build_legend(bento_ui_t *ui)
{
    const text_style_t raw = {LEFT_X, LEGEND_Y, COLOR_RAW, FONT_PX};
    const text_style_t ema = {LEGEND_EMA_X, LEGEND_Y, COLOR_OK,
                              FONT_PX};
    const text_style_t median = {LEGEND_MEDIAN_X, LEGEND_Y, COLOR_WARN,
                                 FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status = add_label(ui, "เทา = ค่าดิบ", &raw, &unused);

    if (status == BENTO_OK) {
        status = add_label(ui, "เขียว = EMA", &ema, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "ส้ม = Median", &median, &unused);
    }
    return status;
}

static bento_status_t build_screen(filter_screen_t *screen)
{
    const text_style_t title = {LEFT_X, TITLE_Y, COLOR_TEXT,
                                TITLE_FONT_PX};
    const text_style_t status_style = {LEFT_X, STATUS_Y, COLOR_WARN,
                                       FONT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = sleep_checked(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "ค่าดิบกับค่าที่กรองแล้ว บนกราฟใบเดียว",
                           &title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังขอค่าชุดแรก", &status_style,
                           &screen->status);
    }
    if (status == BENTO_OK) {
        status = add_chart(screen);
    }
    if (status == BENTO_OK) {
        status = build_card(screen);
    }
    if (status == BENTO_OK) {
        status = build_legend(screen->ui);
    }
    return status;
}

static bento_status_t init_filters(filter_run_t *run)
{
    bento_dsp_ema_config_t ema_config;
    bento_dsp_median_config_t median_config;
    bento_status_t status = bento_dsp_ema_config_init(&ema_config);

    if (status == BENTO_OK) {
        ema_config.alpha = (float)ALPHA;
        status = bento_dsp_ema_state_init(&run->ema, &ema_config);
    }
    if (status == BENTO_OK) {
        status = bento_dsp_median_config_init(&median_config);
    }
    if (status == BENTO_OK) {
        median_config.window = WINDOW;
        status =
            bento_dsp_median_state_init(&run->median, &median_config);
    }
    return status;
}

static void print_settings(void)
{
    char alpha[REAL_BYTES];
    char text[TEXT_BYTES];
    uint32_t length = 0U;

    (void)bento_fmt_real(alpha, sizeof(alpha), ALPHA, &length);
    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>ค่าดิบกับค่าที่กรองแล้ว</h2>");
    (void)snprintf(text, sizeof(text),
                   "EMA alpha = %s | Median window = %d", alpha,
                   WINDOW);
    (void)bento_lcd_print(text);
    (void)bento_lcd_print(
        "<span class=muted>เขย่าบอร์ด แล้วดูสามเส้นแยกกัน</span>");
}

/* After a reset the IMU does not answer for a while (the Eva Kit waits
 * about 13 s for CM55); without this the program would die on its
 * first read. */
static bento_status_t show_miss(const filter_screen_t *screen,
                                filter_run_t *run)
{
    char text[TEXT_BYTES];

    run->misses++;
    (void)bento_ui_set_color(screen->ui, screen->status, COLOR_WARN);
    (void)snprintf(text, sizeof(text),
                   "IMU ยังไม่ตอบ - พลาดไป %" PRId32 " รอบ", run->misses);
    set_text(screen, screen->status, text);
    poll_once(screen->ui);
    return sleep_checked(TICK_MS);
}

/* Both filters get the very same raw value: that is the controlled
 * variable of this whole lesson. The chart takes integers, so every
 * value is multiplied by ten first. */
static bento_status_t show_filters(const filter_screen_t *screen,
                                   filter_run_t *run,
                                   bento_real_t magnitude,
                                   int32_t *out_raw)
{
    float ema = 0.0F;
    float median = 0.0F;
    bento_status_t status =
        bento_dsp_ema_state_update(&run->ema, (float)magnitude, &ema);

    if (status == BENTO_OK) {
        status = bento_dsp_median_state_update(
            &run->median, (float)magnitude, &median);
    }
    if (status == BENTO_OK) {
        const int32_t raw_x10 = (int32_t)(magnitude * SCALE_X10);
        const int32_t ema_x10 = (int32_t)(ema * SCALE_X10);
        const int32_t median_x10 = (int32_t)(median * SCALE_X10);

        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      RAW_SERIES, raw_x10);
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->ema_series, ema_x10);
        (void)bento_ui_chart_set_next(screen->ui, screen->chart,
                                      screen->median_series,
                                      median_x10);
        set_number(screen, screen->raw_digits, raw_x10);
        set_number(screen, screen->ema_digits, ema_x10);
        set_number(screen, screen->median_digits, median_x10);
        run->peak_raw =
            (raw_x10 > run->peak_raw) ? raw_x10 : run->peak_raw;
        run->peak_ema =
            (ema_x10 > run->peak_ema) ? ema_x10 : run->peak_ema;
        *out_raw = raw_x10;
    }
    return status;
}

/* dsp.tilt() turns the three axes into two angles in degrees; written
 * by hand it would be slower and less right. */
static bento_status_t show_tilt(const filter_screen_t *screen,
                                const acceleration_t *acceleration)
{
    char text[TEXT_BYTES];
    float roll = 0.0F;
    float pitch = 0.0F;
    bento_status_t status =
        bento_dsp_tilt(acceleration->ax, acceleration->ay,
                       acceleration->az, &roll, &pitch);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "r %" PRId32 "  p %" PRId32,
                       (int32_t)roll, (int32_t)pitch);
        set_text(screen, screen->tilt, text);
    }
    return status;
}

static void show_verdict(const filter_screen_t *screen,
                         const filter_run_t *run, int32_t raw_x10)
{
    char text[TEXT_BYTES];

    if (raw_x10 > BUMP_X10) {
        (void)bento_ui_set_color(screen->ui, screen->status, COLOR_BAD);
        set_text(screen, screen->status,
                 "โดนกระแทก - ดูว่าเส้นเขียวกับส้มพุ่งตามไหม");
    } else {
        (void)bento_ui_set_color(screen->ui, screen->status, COLOR_OK);
        (void)snprintf(text, sizeof(text),
                       "รอบที่ %" PRId32 " - สามเส้นทาบกันตอนวางนิ่ง",
                       run->rounds);
        set_text(screen, screen->status, text);
    }
}

/* The magnitude of the three axes: about 9.8 flat and still, gravity
 * alone. It ignores which way the board faces and tells only how hard
 * it was knocked. */
static bento_status_t show_reading(const filter_screen_t *screen,
                                   filter_run_t *run,
                                   const acceleration_t *acceleration)
{
    const bento_real_t ax = (bento_real_t)acceleration->ax;
    const bento_real_t ay = (bento_real_t)acceleration->ay;
    const bento_real_t az = (bento_real_t)acceleration->az;
    bento_real_t sum = ax * ax;
    bento_real_t magnitude;
    int32_t raw_x10 = 0;
    bento_status_t status;

    sum += ay * ay;
    sum += az * az;
    magnitude = (bento_real_t)sqrt((double)sum);
    run->rounds++;
    status = show_filters(screen, run, magnitude, &raw_x10);
    if (status == BENTO_OK) {
        status = show_tilt(screen, acceleration);
    }
    if (status == BENTO_OK) {
        show_verdict(screen, run, raw_x10);
        poll_once(screen->ui);
    }
    return status;
}

/* One round, then a sleep for what is left of TICK_MS. A miss sleeps
 * the whole tick, as the original's continue does. */
static bento_status_t read_round(const filter_screen_t *screen,
                                 filter_run_t *run,
                                 uint32_t work_start_ms)
{
    acceleration_t acceleration = {0.0F, 0.0F, 0.0F};
    bento_status_t status = bento_sensors_bmi270_acceleration(
        &acceleration.ax, &acceleration.ay, &acceleration.az);

    if (status != BENTO_OK) {
        status = show_miss(screen, run);
    } else {
        status = show_reading(screen, run, &acceleration);
        if (status == BENTO_OK) {
            const int32_t left_ms =
                TICK_MS - bento_time_ticks_diff(bento_time_ticks_ms(),
                                                work_start_ms);

            status = (left_ms > 0) ? sleep_checked((uint32_t)left_ms)
                                   : BENTO_OK;
        }
    }
    return status;
}

static bento_status_t run_filters(const filter_screen_t *screen,
                                  filter_run_t *run)
{
    const uint32_t start_ms = bento_time_ticks_ms();
    uint32_t work_start_ms = start_ms;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(work_start_ms, start_ms) < RUN_MS)) {
        status = read_round(screen, run, work_start_ms);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
        work_start_ms = bento_time_ticks_ms();
    }
    return status;
}

/* The last values stay on the screen to be read. The two peaks answer
 * what filtering traded: the EMA cuts a sharp peak, good when it is
 * noise and bad when it is a real event. */
static void show_end(const filter_screen_t *screen,
                     const filter_run_t *run)
{
    char text[TEXT_BYTES];

    (void)bento_ui_set_color(screen->ui, screen->status, COLOR_DIM);
    (void)snprintf(text, sizeof(text),
                   "จบแล้ว - ยอดดิบ %" PRId32 " ยอด EMA %" PRId32,
                   run->peak_raw, run->peak_ema);
    set_text(screen, screen->status, text);
    poll_once(screen->ui);
    (void)bento_lcd_console(
        "<span class=muted>------------------------</span>");
    (void)snprintf(text, sizeof(text),
                   "อ่าน %" PRId32 " รอบ | พลาด %" PRId32 " รอบ",
                   run->rounds, run->misses);
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ยอดสูงสุด ดิบ %" PRId32
                   " | EMA %" PRId32 " </span>",
                   run->peak_raw, run->peak_ema);
    (void)bento_lcd_print(text);
    (void)printf("ยอดดิบ %" PRId32 " | ยอด EMA %" PRId32
                 " | EMA กดยอดลง %" PRId32 "\n",
                 run->peak_raw, run->peak_ema,
                 (run->peak_raw - run->peak_ema));
}

int main(void)
{
    filter_screen_t screen;
    filter_run_t run;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(&run, 0, sizeof(run));
    screen.ui = bento_runtime_get_ui();
    /* the filters are made first, as the original does at its top */
    status = init_filters(&run);
    if (status == BENTO_OK) {
        status = build_screen(&screen);
    }
    if (status == BENTO_OK) {
        poll_once(screen.ui);
        print_settings();
        status = run_filters(&screen, &run);
    }
    if (status == BENTO_OK) {
        show_end(&screen, &run);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
