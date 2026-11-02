/*
 * 03_mic_clap_trigger.c -- ตบมือแล้วไฟสลับ (คาบ 08)
 *
 * วัดพื้นเสียงของห้อง 25 หน้าต่างก่อน แล้วตั้งเกณฑ์เป็นสามเท่าของค่าที่ดังที่สุด
 * ตอนเงียบ (ไม่ต่ำกว่า 900) จากนั้นสร้างกราฟที่แกน Y ตั้งจากเกณฑ์นั้น อ่าน peak()
 * 1200 รอบ เส้นฟ้าคือค่าสูงสุดของแต่ละหน้าต่าง เส้นแดงคือเกณฑ์ ตบมือข้ามเกณฑ์
 * แล้วไฟเขียวสลับติดหรือดับ นับจำนวนครั้งบนตัวเลข 7 ส่วน ห้ามนับซ้ำใน 250 ms
 * อยู่เงียบ ๆ ตอนเริ่ม แล้วตบมือใส่บอร์ด บอร์ด: Eva Kit (ไฟ LED2) และ Dev Kit
 * (ไฟ RGB_GREEN)
 *
 * Measures the room's noise floor over 25 windows first, then sets the
 * threshold to three times the loudest quiet window (at least 900) and
 * builds a chart whose Y axis comes from it. It reads peak() for 1200
 * rounds: blue is each window's peak, red the threshold. A clap above
 * the threshold toggles the green LED; the seven-segment digits count
 * the claps, with no second count within 250 ms. Keep quiet at the
 * start, then clap at the board. Boards: Eva Kit (LED2) and Dev Kit
 * (RGB_GREEN).
 *
 * Twin of 03_mic_clap_trigger.py
 * (embedded-systems-for-aiot-developer/examples/s08/03_mic_clap_trigger.py).
 * Style: CLEAN_C.md.
 * The C API has no mic module, so the few calls of the board's frozen
 * mic.py (ports/psoc-edge/freeze/mic.py) are reproduced here on
 * bento_machine_pdm: start(sens) opens and starts PDM0 (16 kHz,
 * decimation 96, MONO_RIGHT, the sens-to-gain table) and throws away
 * two reads 20 ms apart; peak() is the peak of a fresh window from
 * PDM_PCM.stats. bento_machine_pdm is a PSoC Edge API: the reTerminal
 * has no PDM microphone (its port answers unsupported). led_named()
 * reads the LED names one index at a time (bento_gpio_led_name), as
 * the C board info has no name list; an index whose name cannot be
 * read matches no name. Python's try/finally becomes
 * close_microphone(), which main() calls once; the LED is switched off
 * only after a finished run, as in the original. The loop has no sleep
 * (peak() waits for the microphone), so Stop is checked every round.
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
    SENSITIVITY = 3,    /* 1 to 5 */
    FLOOR_WINDOWS = 25, /* quiet windows measured before listening */
    THRESHOLD_TIMES =
        3, /* threshold = the quiet room's loudest, times */
    MIN_THRESHOLD = 900, /* a very quiet room still gets this much */
    REFRACT_MS = 250,    /* no second count within this time */
    ROUNDS = 1200,
    TEXT_BYTES = 128 /* Thai text and two int32_t numbers */
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
    NAMED_LEDS = 2 /* the LED names tried, Dev Kit's first */
};

/* Screen layout on the 792 x 398 canvas. */
enum {
    TITLE_X = 12,
    TITLE_Y = 8,
    BUSY_Y = 246,
    CHART_X = 12,
    CHART_Y = 40,
    CHART_W = 472,
    CHART_H = 200,
    RIGHT_X = 496,
    BLUE_Y = 40,
    RED_Y = 68,
    CLAPS_Y = 102,
    DIGITS_Y = 128,
    DIGITS_W = 180,
    DIGITS_H = 44,
    STATE_Y = 188,
    PANEL_X = 12,
    PANEL_Y = 256,
    PANEL_W = 472,
    PANEL_H = 72,
    ROOM_X = 340,
    ROOM_Y = 264,
    AGAIN_X = 24,
    AGAIN_Y = 292
};

/* Font sizes, in pixels, and colours, 0xRRGGBB. */
enum {
    FONT_TITLE = 24,
    FONT_TEXT = 20,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_THRESHOLD = 0xFF5555,
    COLOR_WAITING = 0x9AA3AF,
    COLOR_ON = 0x55DD55,
    COLOR_OFF = 0x9AA0A6
};

/* Too big for the 8 KB program stack: file scope, zeroed by the loader
 * and set again at the start of main() (CLEAN_C.md C9). */
extern uint8_t g_mic_buffer[MIC_BUFFER_BYTES];
uint8_t g_mic_buffer[MIC_BUFFER_BYTES]; /* one window of samples */

/* A place on the screen; a label uses only x and y. */
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} rect_t;

/* The PDM microphone and whether it needs closing. */
typedef struct {
    uint32_t handle;
    bool open;
} microphone_t;

/* The widgets, the threshold and what the loop remembers. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t busy;
    bento_ui_id_t chart;
    uint8_t threshold_series;
    bento_ui_id_t digits;
    bento_ui_id_t state;
    microphone_t microphone;
    uint32_t light;
    int32_t threshold;
    int32_t claps;
    bool lit;
    uint32_t last_hit_ms;
} clap_t;

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
        status = bento_time_sleep_ms(WARM_UP_MS);
        if ((status == BENTO_OK) && !bento_runtime_is_running()) {
            status = BENTO_E_CANCELLED;
        }
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

/* mic.peak(): the largest distance from the mean in a fresh window, 0
 * to 32768. A read that brings nothing is the binding's OSError. */
static bento_status_t read_peak(const microphone_t *microphone,
                                int32_t *peak)
{
    bento_machine_pdm_stats_t stats;
    bento_status_t status;

    bento_machine_pdm_stats_init(&stats);
    status = bento_machine_pdm_stats(microphone->handle, g_mic_buffer,
                                     MIC_BUFFER_BYTES, true, &stats);
    *peak = (int32_t)stats.peak;
    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* True when the LED table has this name; *led is its index then. */
static bool led_with_name(const char *name, uint32_t *led)
{
    const uint32_t count = bento_gpio_num_leds();
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < count) && !found; i++) {
        const char *own = NULL;

        if ((bento_gpio_led_name(i, &own) == BENTO_OK) &&
            (own != NULL)) {
            found = strcmp(own, name) == 0;
        }
        if (found) {
            *led = i;
        }
    }
    return found;
}

/* The green LED both boards show: Dev Kit's RGB_GREEN, else Eva's
 * LED2; LED 0 when the board has neither (the indexes differ by board,
 * the names do not). */
static uint32_t green_led(void)
{
    static const char *const names[NAMED_LEDS] = {"RGB_GREEN", "LED2"};
    uint32_t led = 0U;
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < (uint32_t)NAMED_LEDS) && !found; i++) {
        found = led_with_name(names[i], &led);
    }
    return led;
}

static void set_text(const clap_t *clap, bento_ui_id_t id,
                     const char *text)
{
    /* a setter's refusal was ignored before too (modui.c) */
    (void)bento_ui_set_text(clap->ui, id, text, (uint32_t)strlen(text));
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

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                const rect_t *place, uint32_t color,
                                int32_t font_px, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = place->x;
    args.y = place->y;
    args.font_px = font_px;
    args.color = color;
    return bento_ui_label_create(ui, &args, out_id);
}

static void box_args(bento_ui_widget_args_t *args, const rect_t *box)
{
    bento_ui_widget_args_init(args);
    args->x = box->x;
    args->y = box->y;
    args->w = box->w;
    args->h = box->h;
}

/* The first screen: the title, and the note in the bottom strip, so it
 * does not sit where the chart will be built. */
static bento_status_t build_first_screen(clap_t *clap)
{
    static const rect_t title = {TITLE_X, TITLE_Y, 0, 0};
    static const rect_t busy = {TITLE_X, BUSY_Y, 0, 0};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(clap->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = add_label(clap->ui, "สวิตช์ไฟตบมือ", &title, 0U,
                           FONT_TITLE, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(clap->ui, "กำลังวัดพื้นเสียง อยู่เงียบ ๆ", &busy,
                           COLOR_WARN, FONT_TEXT, &clap->busy);
    }
    if (status == BENTO_OK) {
        skip_events(clap->ui);
    }
    return status;
}

/* The loudest of FLOOR_WINDOWS quiet windows: the threshold must stand
 * above the room's own loudest moment, not above its average. */
static bento_status_t measure_floor(const clap_t *clap,
                                    int32_t *noise_floor)
{
    bento_status_t status = BENTO_OK;
    int32_t window;

    *noise_floor = 0;
    for (window = 0; (window < FLOOR_WINDOWS) && (status == BENTO_OK);
         window++) {
        int32_t peak = 0;

        status = read_peak(&clap->microphone, &peak);
        if ((status == BENTO_OK) && (peak > *noise_floor)) {
            *noise_floor = peak;
        }
        skip_events(clap->ui);
    }
    return status;
}

/* The chart, built only now: its Y axis comes from the threshold. */
static bento_status_t add_chart(clap_t *clap)
{
    static const rect_t chart = {CHART_X, CHART_Y, CHART_W, CHART_H};
    static const rect_t blue = {RIGHT_X, BLUE_Y, 0, 0};
    static const rect_t red = {RIGHT_X, RED_Y, 0, 0};
    bento_ui_widget_args_t args;
    char text[TEXT_BYTES];
    bento_ui_id_t unused;
    bento_status_t status;

    box_args(&args, &chart);
    args.min_val = 0;
    args.max_val = clap->threshold * 2;
    status = bento_ui_chart_create(clap->ui, &args, &clap->chart);
    if (status == BENTO_OK) {
        status = bento_ui_chart_add_series(clap->ui, clap->chart,
                                           COLOR_THRESHOLD,
                                           &clap->threshold_series);
    }
    if (status == BENTO_OK) {
        status = add_label(clap->ui, "ฟ้า = ค่าสูงสุดต่อหน้าต่าง", &blue,
                           COLOR_ACCENT, FONT_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "แดง = เกณฑ์ %" PRId32,
                       clap->threshold);
        status = add_label(clap->ui, text, &red, COLOR_BAD, FONT_TEXT,
                           &unused);
    }
    return status;
}

/* The clap counter, its state and the note about this room. */
static bento_status_t add_counter(clap_t *clap)
{
    static const rect_t claps = {RIGHT_X, CLAPS_Y, 0, 0};
    static const rect_t digits = {RIGHT_X, DIGITS_Y, DIGITS_W,
                                  DIGITS_H};
    static const rect_t state = {RIGHT_X, STATE_Y, 0, 0};
    static const rect_t panel = {PANEL_X, PANEL_Y, PANEL_W, PANEL_H};
    static const rect_t room = {ROOM_X, ROOM_Y, 0, 0};
    static const rect_t again = {AGAIN_X, AGAIN_Y, 0, 0};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(clap->ui, "ตบไปแล้ว (ครั้ง)", &claps,
                                      0U, FONT_TEXT, &unused);

    if (status == BENTO_OK) {
        box_args(&args, &digits);
        status = bento_ui_seg7_create(clap->ui, &args, &clap->digits);
    }
    if (status == BENTO_OK) {
        status = add_label(clap->ui, "รอเสียงตบ", &state, COLOR_WAITING,
                           FONT_TEXT, &clap->state);
    }
    if (status == BENTO_OK) {
        box_args(&args, &panel);
        status = bento_ui_panel_create(clap->ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(clap->ui, "เกณฑ์นี้เป็นของห้องนี้เท่านั้น", &room,
                           COLOR_WARN, FONT_TEXT, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(clap->ui, "ย้ายห้องแล้วต้องรันใหม่", &again,
                           COLOR_WARN, FONT_TEXT, &unused);
    }
    return status;
}

/* The noise floor, the threshold from it, and the listening screen. */
static bento_status_t prepare(clap_t *clap)
{
    char line[TEXT_BYTES];
    int32_t noise_floor = 0;
    bento_status_t status = measure_floor(clap, &noise_floor);

    if (status == BENTO_OK) {
        const int32_t scaled = noise_floor * THRESHOLD_TIMES;

        clap->threshold =
            (scaled > MIN_THRESHOLD) ? scaled : MIN_THRESHOLD;
        (void)bento_ui_set_visible(clap->ui, clap->busy,
                                   0U); /* hide() is a setter */
        (void)snprintf(line, sizeof(line),
                       "พื้นเสียง %" PRId32 " | เกณฑ์ %" PRId32,
                       noise_floor, clap->threshold);
        (void)bento_lcd_print(line);
        status = add_chart(clap);
    }
    if (status == BENTO_OK) {
        status = add_counter(clap);
    }
    if (status == BENTO_OK) {
        set_text(clap, clap->digits, "0");
        skip_events(clap->ui);
        clap->light = green_led();
        status = bento_gpio_led_off(clap->light);
    }
    return status;
}

/* A clap: the count goes up and the light toggles. */
static bento_status_t count_clap(clap_t *clap, int32_t peak)
{
    char line[TEXT_BYTES];
    bento_status_t status;

    clap->claps++;
    clap->lit = !clap->lit;
    (void)snprintf(line, sizeof(line), "%" PRId32, clap->claps);
    set_text(clap, clap->digits, line);
    status = clap->lit ? bento_gpio_led_on(clap->light)
                       : bento_gpio_led_off(clap->light);
    if ((status == BENTO_OK) && clap->lit) {
        set_text(clap, clap->state, "ไฟติด");
        (void)bento_ui_set_color(clap->ui, clap->state, COLOR_ON);
        (void)snprintf(line, sizeof(line),
                       "<span class=ok>ตบครั้งที่ %" PRId32
                       " - ไฟติด (%" PRId32 ")</span>",
                       clap->claps, peak);
    } else if (status == BENTO_OK) {
        set_text(clap, clap->state, "ไฟดับ");
        (void)bento_ui_set_color(clap->ui, clap->state, COLOR_OFF);
        (void)snprintf(line, sizeof(line),
                       "ตบครั้งที่ %" PRId32 " - ไฟดับ (%" PRId32 ")",
                       clap->claps, peak);
    } else {
        line[0] = '\0';
    }
    if (status == BENTO_OK) {
        (void)bento_lcd_print(line); /* sent without an answer */
    }
    return status;
}

/* One window: its peak onto the chart, and a clap when it crosses the
 * threshold outside the echo time of the last one. */
static bento_status_t listen_once(clap_t *clap)
{
    int32_t peak = 0;
    bento_status_t status = read_peak(&clap->microphone, &peak);

    if (status == BENTO_OK) {
        const uint32_t now_ms = bento_time_ticks_ms();
        const int32_t top = clap->threshold * 2;

        (void)bento_ui_chart_set_next(clap->ui, clap->chart, 0U,
                                      (peak < top) ? peak : top);
        (void)bento_ui_chart_set_next(clap->ui, clap->chart,
                                      clap->threshold_series,
                                      clap->threshold);
        if ((peak >= clap->threshold) &&
            (bento_time_ticks_diff(now_ms, clap->last_hit_ms) >
             REFRACT_MS)) {
            clap->last_hit_ms = now_ms;
            status = count_clap(clap, peak);
        }
        skip_events(clap->ui);
    }
    return status;
}

/* The noise floor, then ROUNDS windows, or until Stop or a failure. */
static bento_status_t run_listener(clap_t *clap)
{
    bento_status_t status = prepare(clap);
    int32_t i;

    for (i = 0; (i < ROUNDS) && (status == BENTO_OK); i++) {
        status = listen_once(clap);
    }
    return status;
}

/* The console's first lines; lcd calls are sent without an answer
 * (modlcd.c). */
static void open_console(void)
{
    char line[TEXT_BYTES];

    (void)bento_lcd_clear();
    (void)bento_lcd_console("<h2>สวิตช์ไฟตบมือ</h2>");
    (void)snprintf(line, sizeof(line),
                   "วัดพื้นเสียง %d หน้าต่าง อยู่เงียบ ๆ ก่อน...", FLOOR_WINDOWS);
    (void)bento_lcd_print(line);
}

/* After a finished run: the light off, and the summary. */
static bento_status_t report_end(const clap_t *clap)
{
    char line[TEXT_BYTES];
    const bento_status_t status = bento_gpio_led_off(clap->light);

    if (status == BENTO_OK) {
        (void)snprintf(line, sizeof(line),
                       "ตบทั้งหมด %" PRId32 " ครั้ง | เกณฑ์ที่ใช้ %" PRId32,
                       clap->claps, clap->threshold);
        (void)bento_lcd_print(line);
        (void)bento_lcd_print("<span class=muted>ลอง K = 1 "
                              "แล้วดูว่าเสียงพิมพ์ผ่านไหม</span>");
        (void)printf("เกณฑ์มาจากพื้นเสียงของห้องนี้ ไม่ใช่ตัวเลขที่เดามา "
                     "ย้ายห้องแล้วรันใหม่\n");
    }
    return status;
}

int main(void)
{
    clap_t clap;
    bento_status_t status;

    (void)memset(&clap, 0, sizeof(clap));
    (void)memset(g_mic_buffer, 0, sizeof(g_mic_buffer));
    clap.ui = bento_runtime_get_ui();
    status = build_first_screen(&clap);
    if (status == BENTO_OK) {
        open_console();
        status = open_microphone(&clap.microphone, SENSITIVITY);
    }
    if (status == BENTO_OK) {
        status = run_listener(&clap);
    }
    close_microphone(&clap.microphone);
    if (status == BENTO_OK) {
        status = report_end(&clap);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
