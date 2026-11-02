/*
 * s07_accel_chart.c -- กราฟความเร่งสามแกนแบบสด + ตารางสรุปสามแกน
 * (Live three-axis acceleration chart with a summary table)
 *
 * โยกบอร์ดเบา ๆ ดูสามเส้นบนกราฟแยกกันชัดเจน ตารางขวาเก็บค่าต่ำสุด สูงสุด และล่าสุด
 * ของแต่ละแกน (กราฟเก็บได้แค่ 50 จุด ตารางจึงตอบคนละคำถาม) กด "หยุดบันทึก" ค้างภาพ
 * ไว้อ่านตาราง แล้วกด "เริ่มบันทึก" ให้กราฟเดินต่อ ตัวเลขคาบลูปบนจอคือหลักฐานว่า
 * คาบ 200 ms เกิดขึ้นจริง
 *
 * Rock the board gently and watch three separate lines on the chart.
 * The table on the right keeps each axis's lowest, highest and latest
 * value (the chart holds only 50 points, so the table answers another
 * question). The stop button freezes the chart so the table can be
 * read; the start button runs it again and clears the old extremes.
 * The loop period on the screen proves the 200 ms period really
 * happens. The program runs until Stop.
 *
 * Twin of s07_accel_chart.py
 * (embedded-systems-for-aiot-developer/solution_codes/s07_accel_chart.py).
 * Style: CLEAN_C.md.
 * Differs from the original only where C must:
 *   - every failure of sensors.bmi270.motion() is the original's OSError
 *     (the last reading stays);
 *   - Python's None for "no extreme yet" is a has_range flag per axis;
 *   - "{:+.2f}" is bento_fmt_real_precision() with the '+' put in front;
 *   - Stop ends the loop at its next sleep (KeyboardInterrupt in
 *     MicroPython, which nothing catches).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_time.h"

/* colours, 0xRRGGBB: the three lines come from the data palette, with no
 * status colour among them, so a red line never means "wrong" */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_AX = 0x4A9EFF,
    COLOR_AY = 0x8E7BFF,
    COLOR_AZ = 0x2FB6A8,
    COLOR_RUN = 0x4A9EFF,
    COLOR_START = 0x30A46C,
    COLOR_STOP = 0x3A4150
};

/* timing, the chart's axis and the table */
enum {
    PERIOD_MS =
        200, /* 5 samples a second: hand speed, and CM55 keeps up */
    SCREEN_SETTLE_MS = 200,
    MS_PER_SECOND = 1000,
    CHART_LIMIT =
        20, /* gravity is 9.81: room to rock it as much again */
    AXIS_COUNT = 3,
    TABLE_COLUMNS = 4,
    TABLE_FONT_PX = 20, /* Thai text on the board is 20 px or larger */
    COLUMN_LOW = 1,
    COLUMN_HIGH = 2,
    COLUMN_LATEST = 3,
    FONT_TITLE = 28,
    FONT_BODY = 20,
    NUMBER_BYTES = 40,
    TEXT_BYTES = 64
};

/* Every widget, in the order the original creates it. */
typedef enum {
    W_TITLE,
    W_CHART,
    W_TABLE,
    W_RECORD_LAMP,
    W_RECORD_TEXT,
    W_PERIOD_TEXT,
    W_START,
    W_STOP,
    W_COUNT
} widget_index_t;

/* One widget as ui.<Factory>(...) makes it. */
typedef struct {
    uint8_t type;
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value;
    int32_t min;
    int32_t max;
} widget_spec_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t widgets[W_COUNT];
    uint8_t
        series[AXIS_COUNT]; /* series 0 comes with the chart itself */
    bento_real_t accel[AXIS_COUNT];
    bento_real_t
        low[AXIS_COUNT]; /* what the chart forgets after 50 points */
    bento_real_t high[AXIS_COUNT];
    bool has_range[AXIS_COUNT];
    const char *record_text; /* changes only when a button is pressed */
    uint32_t last_ms;
    int32_t period_ms;
    int32_t last_second; /* -1: no number written yet */
    bool running;
} recorder_t;

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* ui.poll() here only keeps the queue empty; it answers no events when
 * it fails. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* A setter's refusal was ignored before too (modui.c). */
static void set_text(const recorder_t *recorder, widget_index_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(recorder->ui, recorder->widgets[widget],
                            text, (uint32_t)strlen(text));
}

static void set_cell(const recorder_t *recorder, int32_t row,
                     int32_t column, const char *text)
{
    (void)bento_ui_set_cell(recorder->ui, recorder->widgets[W_TABLE],
                            (uint8_t)row, (uint8_t)column, text,
                            (uint32_t)strlen(text));
}

/* Creates specs[0..count) into widgets[first..]. */
static bento_status_t add_widgets(recorder_t *recorder,
                                  const widget_spec_t *specs,
                                  int32_t first, int32_t count)
{
    bento_ui_widget_args_t args;
    bento_status_t status = BENTO_OK;
    int32_t i;

    for (i = 0; (i < count) && (status == BENTO_OK); i++) {
        const widget_spec_t *spec = &specs[i];

        bento_ui_widget_args_init(&args);
        args.text = spec->text;
        args.text_len =
            (spec->text != NULL) ? (uint32_t)strlen(spec->text) : 0U;
        args.x = spec->x;
        args.y = spec->y;
        args.w = spec->w;
        args.h = spec->h;
        args.color = spec->color;
        args.font_px = spec->value;
        args.min_val = spec->min;
        args.max_val = spec->max;
        status = bento_ui_create(recorder->ui, spec->type, &args,
                                 &recorder->widgets[first + i]);
    }
    return status;
}

/* The title and one chart with three lines, -20..20 m/s2. */
static bento_status_t add_chart(recorder_t *recorder)
{
    static const widget_spec_t specs[W_TABLE] = {
        {UI_WIDGET_LABEL, "กราฟความเร่งสามแกน หน่วย m/s2", 24, 8, -1, -1,
         COLOR_TEXT, FONT_TITLE, 0, 0},
        {UI_WIDGET_CHART, NULL, 24, 52, 292, 144, COLOR_AX, 0,
         -CHART_LIMIT, CHART_LIMIT}};
    bento_status_t status =
        add_widgets(recorder, specs, W_TITLE, W_TABLE);

    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            recorder->ui, recorder->widgets[W_CHART], COLOR_AY,
            &recorder->series[1]);
    }
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(
            recorder->ui, recorder->widgets[W_CHART], COLOR_AZ,
            &recorder->series[2]);
    }
    return status;
}

/* A table lines its columns up by itself. The widths were measured on the
 * rendered screen: narrower ones wrap a cell, every row doubles in
 * height and row Z falls out of the 280 px table. Rows and widths are
 * sent and forgotten, as add_row() and col_width() are. */
static bento_status_t add_table(recorder_t *recorder)
{
    static const widget_spec_t spec = {
        UI_WIDGET_TABLE, NULL,          332, 52, 436, 280, 0U,
        TABLE_FONT_PX,   TABLE_COLUMNS, 0};
    static const int32_t widths_px[TABLE_COLUMNS] = {100, 108, 108,
                                                     108};
    static const char header[] = "แกน\0"
                                 "ต่ำสุด\0"
                                 "สูงสุด\0"
                                 "ล่าสุด";
    static const char *const rows[AXIS_COUNT] = {"X\0"
                                                 "-\0"
                                                 "-\0"
                                                 "-",
                                                 "Y\0"
                                                 "-\0"
                                                 "-\0"
                                                 "-",
                                                 "Z\0"
                                                 "-\0"
                                                 "-\0"
                                                 "-"};
    const uint32_t row_bytes =
        7U; /* one letter and three "-" cells, NUL between them */
    bento_status_t status = add_widgets(recorder, &spec, W_TABLE, 1);
    const bento_ui_id_t table = recorder->widgets[W_TABLE];
    int32_t i;

    if (status == BENTO_OK) {
        for (i = 0; i < TABLE_COLUMNS; i++) {
            (void)bento_ui_set_col_width(recorder->ui, table, i,
                                         widths_px[i]);
        }
        (void)bento_ui_add_row(recorder->ui, table, 0U, header,
                               (uint32_t)sizeof(header) - 1U);
        for (i = 0; i < AXIS_COUNT; i++) {
            const int32_t row = i + 1; /* row 0 is the header */

            (void)bento_ui_add_row(recorder->ui, table, (uint8_t)row,
                                   rows[i], row_bytes);
        }
    }
    return status;
}

/* A lamp says the state; start and stop are two buttons, so nobody has to
 * guess what one toggling button means right now. */
static bento_status_t add_record_controls(recorder_t *recorder)
{
    static const widget_spec_t specs[W_COUNT - W_RECORD_LAMP] = {
        {UI_WIDGET_LED, NULL, 24, 212, 48, 48, COLOR_RUN, 1, 0, 0},
        {UI_WIDGET_LABEL, "กำลังบันทึก", 88, 216, -1, -1, COLOR_TEXT,
         FONT_BODY, 0, 0},
        {UI_WIDGET_LABEL, "คาบลูป -- ms", 88, 252, -1, -1, COLOR_DIM,
         FONT_BODY, 0, 0},
        {UI_WIDGET_BUTTON, "เริ่มบันทึก", 24, 296, 128, 88, COLOR_START,
         FONT_BODY, 0, 0},
        {UI_WIDGET_BUTTON, "หยุดบันทึก", 184, 296, 128, 88, COLOR_STOP,
         FONT_BODY, 0, 0}};

    return add_widgets(recorder, specs, W_RECORD_LAMP,
                       W_COUNT - W_RECORD_LAMP);
}

/* "{:+.2f}": two decimals, with a '+' in front of a positive number. */
static void signed_text(bento_real_t value, char *text, size_t cap)
{
    char digits[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;

    if (bento_fmt_real_precision(digits, sizeof(digits), value, 2,
                                 &length) != BENTO_OK) {
        digits[0] = '\0';
    }
    (void)snprintf(text, cap, "%s%s", (digits[0] == '-') ? "" : "+",
                   digits);
}

/* The extremes of one axis: the chart cannot remember them. */
static void note_extremes(recorder_t *recorder, int32_t axis)
{
    const bento_real_t value = recorder->accel[axis];

    if (!recorder->has_range[axis] || (value < recorder->low[axis])) {
        recorder->low[axis] = value;
    }
    if (!recorder->has_range[axis] || (value > recorder->high[axis])) {
        recorder->high[axis] = value;
    }
    recorder->has_range[axis] = true;
}

/* Six axes from one snapshot feed three lines. A failed read keeps the
 * last values: one miss must not blank the chart. set_next() takes
 * whole numbers, so each value is truncated as int() does. */
static void record_sample(recorder_t *recorder)
{
    bento_sensors_imu6_t imu;
    int32_t axis;

    bento_sensors_imu6_init(&imu);
    if (bento_sensors_bmi270_motion(&imu) == BENTO_OK) {
        recorder->accel[0] = (bento_real_t)imu.ax;
        recorder->accel[1] = (bento_real_t)imu.ay;
        recorder->accel[2] = (bento_real_t)imu.az;
    }
    for (axis = 0; axis < AXIS_COUNT; axis++) {
        (void)bento_ui_chart_set_next(
            recorder->ui, recorder->widgets[W_CHART],
            recorder->series[axis], (int32_t)recorder->accel[axis]);
    }
    for (axis = 0; axis < AXIS_COUNT; axis++) {
        note_extremes(recorder, axis);
    }
}

/* Start or stop recording. A new run forgets the old extremes, or the
 * table would mix two runs. A dark lamp dims, it does not vanish. */
static void set_recording(recorder_t *recorder, bool running)
{
    int32_t axis;

    recorder->running = running;
    if (running) {
        for (axis = 0; axis < AXIS_COUNT; axis++) {
            recorder->has_range[axis] = false;
        }
    }
    (void)bento_ui_set_value(recorder->ui,
                             recorder->widgets[W_RECORD_LAMP],
                             running ? 1 : 0);
    (void)bento_ui_set_color(recorder->ui,
                             recorder->widgets[W_RECORD_TEXT],
                             running ? COLOR_TEXT : COLOR_DIM);
    recorder->record_text = running ? "กำลังบันทึก" : "หยุดแล้ว ค่าล่าสุด";
}

/* Polled every pass, recording or not: a stop button that also stops the
 * polling could never be undone. The original reads only ev['handle'],
 * so the id alone is compared. */
static void handle_events(recorder_t *recorder)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;

    if (bento_ui_poll(recorder->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                      &count, NULL) != BENTO_OK) {
        count = 0U; /* ui.poll() answers no events when it fails */
    }
    for (i = 0U; i < count; i++) {
        if (events[i].id == recorder->widgets[W_START]) {
            set_recording(recorder, true);
        } else if (events[i].id == recorder->widgets[W_STOP]) {
            set_recording(recorder, false);
        } else {
            /* any other widget changes nothing */
        }
    }
}

/* One axis's row: lowest and highest ("-" before the first reading)
 * and the latest value. Row 0 is the header, so axis 0 is row 1. */
static void show_axis_row(const recorder_t *recorder, int32_t axis)
{
    const int32_t row = axis + 1;
    char text[TEXT_BYTES];

    if (recorder->has_range[axis]) {
        signed_text(recorder->low[axis], text, sizeof(text));
        set_cell(recorder, row, COLUMN_LOW, text);
        signed_text(recorder->high[axis], text, sizeof(text));
        set_cell(recorder, row, COLUMN_HIGH, text);
    } else {
        set_cell(recorder, row, COLUMN_LOW, "-");
        set_cell(recorder, row, COLUMN_HIGH, "-");
    }
    signed_text(recorder->accel[axis], text, sizeof(text));
    set_cell(recorder, row, COLUMN_LATEST, text);
}

/* The numbers a person reads change at most once a second; row 0 is the
 * header, so X, Y and Z are rows 1, 2 and 3. */
static void show_numbers(recorder_t *recorder)
{
    const uint32_t elapsed_seconds =
        bento_time_ticks_ms() / (uint32_t)MS_PER_SECOND;
    const int32_t second = (int32_t)elapsed_seconds;
    char text[TEXT_BYTES];
    int32_t axis;

    if (second != recorder->last_second) {
        recorder->last_second = second;
        for (axis = 0; axis < AXIS_COUNT; axis++) {
            show_axis_row(recorder, axis);
        }
        (void)snprintf(text, sizeof(text), "คาบลูป %d ms",
                       (int)recorder->period_ms);
        set_text(recorder, W_PERIOD_TEXT, text);
    }
}

/* One pass. ticks_diff() handles the counter wrapping round. The record
 * line is sent again every pass on purpose: a text keeps the display
 * core in its fast mode, set_next() does not. The same rest whether
 * recording or not, so CM55 is not flooded with polls. */
static bento_status_t record_once(recorder_t *recorder)
{
    uint32_t now_ms;

    if (recorder->running) {
        record_sample(recorder);
    }
    now_ms = bento_time_ticks_ms();
    recorder->period_ms =
        bento_time_ticks_diff(now_ms, recorder->last_ms);
    recorder->last_ms = now_ms;
    handle_events(recorder);
    set_text(recorder, W_RECORD_TEXT, recorder->record_text);
    show_numbers(recorder);
    return wait_ms(PERIOD_MS);
}

/* The first read on the Eva Kit can wait up to 16 s after a reset: let
 * that wait happen before the chart is built. */
static void warm_up_sensor(void)
{
    bento_sensors_imu6_t imu;

    bento_sensors_imu6_init(&imu);
    if (bento_sensors_bmi270_motion(&imu) != BENTO_OK) {
        (void)printf("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ในลูป\n");
    }
}

static bento_status_t build_screen(recorder_t *recorder)
{
    bento_status_t status = bento_ui_screen_default(recorder->ui);

    if (status == BENTO_OK) {
        warm_up_sensor();
        (void)bento_ui_clear(
            recorder
                ->ui); /* a refused CLEAR_ALL is ignored, as modui.c does */
        status = wait_ms(SCREEN_SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_chart(recorder);
    }
    if (status == BENTO_OK) {
        status = add_table(recorder);
    }
    if (status == BENTO_OK) {
        status = add_record_controls(recorder);
    }
    if (status == BENTO_OK) {
        drain_events(recorder->ui);
    }
    return status;
}

int main(void)
{
    const bento_real_t gravity =
        (bento_real_t)9.81; /* until the first read answers */
    recorder_t recorder;
    bento_status_t status;

    (void)memset(&recorder, 0, sizeof(recorder));
    recorder.ui = bento_runtime_get_ui();
    recorder.accel[2] = gravity;
    recorder.record_text = "กำลังบันทึก";
    recorder.period_ms = PERIOD_MS;
    recorder.last_second = -1;
    recorder.running = true;
    status = build_screen(&recorder);
    recorder.last_ms = bento_time_ticks_ms();
    while (status == BENTO_OK) {
        status = record_once(&recorder);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
