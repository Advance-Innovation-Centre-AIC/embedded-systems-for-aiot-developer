/*
 * s05_pot_capsense.c -- ลูกบิด + แถบสัมผัส + ฟิลเตอร์ EMA (ฉบับฝึกเติมโค้ด)
 *
 * วิธีรัน: บนจอบอร์ด แตะการ์ด Playground ค้างไว้ตลอดคาบ เติมช่องว่างทีละจุด รันทีละครั้ง
 * แล้วดูว่าจอ "ตื่น" ขึ้นทีละส่วน ค่าเริ่มต้นตั้งไว้ให้แล้ว จึงรันได้ตั้งแต่ยังไม่เติมอะไร
 * แต่ตัวเลขจะนิ่งสนิท หน้าจอเขียนไว้ให้ครบแล้ว งานของเราคือทำให้ค่าไหลเข้าไปในนั้น
 * ตัวอย่างของคาบอยู่ที่ examples/s05/ ฝั่งลูกบิดคือ 03 04 05 ฝั่งแถบสัมผัสคือ 01 02
 * และฝั่งตัวกรองคือ 06 07 ติดตรงไหนให้เปิดกองนั้นกองเดียวพอ
 * กด Program to Device แล้วหมุนลูกบิด กับลากนิ้วบนแถบทองแดง CapSense
 * หมุนลูกบิดช้า ๆ ดูแถบกับไม้บรรทัดขยับพร้อมกัน กดปุ่มเพิ่ม/ลดเกณฑ์ แล้วหมุนลูกบิด
 * ข้ามเกณฑ์ ดูไฟสองดวงสลับกันติด หนึ่งค่าที่วัดได้ถูกเล่าสามแบบ: ค่าดิบ เปอร์เซ็นต์ และโวลต์
 * การ์ดบนซ้ายคือลูกบิดบนไม้บรรทัด การ์ดบนขวาคือเกณฑ์ที่ตั้งเองพร้อมไฟสองดวง
 * การ์ดล่างซ้ายคือแถบสัมผัสกับปุ่มทองแดง และค่าดิบเทียบค่าที่กรองด้วย EMA (alpha 0.2)
 * ui.Scale เป็นไม้บรรทัด ไม่มีเข็ม ตัวที่ขยับคือ ui.Bar ที่วางทับ ไฟที่ดับจะหรี่ ไม่หาย
 *
 * The knob, the CapSense slider and an EMA filter on one panel. The
 * knob's value is shown as raw counts, a percentage and volts, on a bar
 * laid over its own scale. A user-set alarm threshold (Spinbox with
 * up/down buttons) drives two status lamps from the filtered value. The
 * slider and the two copper buttons are shown below, with the raw and
 * filtered percentages one under the other and a value-quality line. Numbers
 * are rewritten once a second; bars and lamps every 200 ms round.
 *
 * This is the practice skeleton: the blanks marked "เติม" do nothing
 * yet, as the pass lines of the original do nothing. Until they are
 * filled the percentage and the slider stay 0, the knob's bar and the
 * filtered value stay still, and the up/down buttons are never read.
 *
 * Twin of s05_pot_capsense.py
 * (embedded-systems-for-aiot-developer/practise_codes/s05_pot_capsense.py).
 * Style: CLEAN_C.md. The solution is solution_codes/s05_pot_capsense.c.
 * Differs from the original only where C must:
 *   - The screen is one table of widgets, made in the original's order.
 *   - The try/except OSError around the five reads is a status chain: a
 *     value keeps its last good reading when its read (or one before
 *     it) fails, as the Python names keep theirs, and the buttons read
 *     0, 0 that round.
 *   - The voltage is still computed by the board from the percentage
 *     times 3.3 V, not measured: a trend, not a meter reading.
 *   - Stop ends the loop with BENTO_E_CANCELLED and main() returns 0,
 *     where MicroPython raises KeyboardInterrupt.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_dsp.h"
#include "bento_fmt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"

enum { /* the threshold the user sets, percent */
    THRESHOLD_MIN = 10,
    THRESHOLD_MAX = 95,
    THRESHOLD_STEP = 5,
    THRESHOLD_START = 70,
    PERCENT_MAX = 100
};

enum {                /* the rulers and the threshold box */
    SCALE_TICKS = 11, /* 0 20 40 60 80 100 under every second tick */
    SCALE_MAJOR = 2,
    THRESHOLD_DIGITS = 2, /* else the box shows 0070 */
    CARD_RADIUS = 12
};

enum { /* decimals in the texts */
    PERCENT_DIGITS = 1,
    LINE_DIGITS = 2, /* two on both lines keep the jitter visible */
    VOLT_DIGITS = 3
};

enum { /* times, ms */
    CLEAR_MS = 200,
    LOOP_MS = 200, /* faster loops send more draws than CM55 can draw */
    MS_PER_SECOND = 1000
};

enum { /* colours, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_BAR = 0x4A9EFF,
    COLOR_BUTTON = 0x3A4150
};

enum { /* a prefix, a number of up to BENTO_FMT_BUF_SIZE and a suffix */
    TEXT_BYTES = 96
};

/* The widgets the loop changes: their place in screen_t.ids. */
typedef enum {
    SLOT_POT_BAR,
    SLOT_PERCENT,
    SLOT_RAW,
    SLOT_VOLT,
    SLOT_SPIN,
    SLOT_UP,
    SLOT_DOWN,
    SLOT_LED_OK,
    SLOT_LED_BAD,
    SLOT_TOUCH_BAR,
    SLOT_LED_BUTTON0,
    SLOT_LED_BUTTON1,
    SLOT_HEALTH,
    SLOT_RAW_LINE,
    SLOT_EMA_LINE,
    SLOT_COUNT,
    SLOT_NONE /* a widget the loop never changes */
} slot_t;

typedef enum {
    KIND_LABEL = 0,
    KIND_PANEL,
    KIND_BAR,
    KIND_SCALE,
    KIND_SPINBOX,
    KIND_BUTTON,
    KIND_LED
} kind_t;

/* One widget of the screen, as the original creates it. */
typedef struct {
    kind_t kind;
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value; /* font size, initial value or border width */
    int32_t low;   /* min=: a range start or a border colour */
    int32_t high;  /* max=: a range end or a corner radius */
    slot_t slot;
} widget_spec_t;

/* The last good readings. */
typedef struct {
    uint16_t raw;
    float volts;
    float percent;
    uint8_t slider;
    uint8_t button0;
    uint8_t button1;
    bool fresh;
} reading_t;

/* The screen, the filter and the program's state. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ids[SLOT_COUNT];
    bento_dsp_ema_state_t ema;
    reading_t reading;
    float ema_percent;
    int32_t threshold;
    int32_t last_second;
    bool over;
    char health[TEXT_BYTES];
} knob_panel_t;

/* A setter's refusal was ignored by MicroPython too (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static void set_value(const knob_panel_t *panel, slot_t slot,
                      int32_t value)
{
    (void)bento_ui_set_value(panel->ui, panel->ids[slot], value);
}

static bento_status_t create_kind(bento_ui_t *ui, kind_t kind,
                                  const bento_ui_widget_args_t *args,
                                  bento_ui_id_t *out_id)
{
    bento_status_t status;

    switch (kind) {
    case KIND_PANEL:
        status = bento_ui_panel_create(ui, args, out_id);
        break;
    case KIND_BAR:
        status = bento_ui_bar_create(ui, args, out_id);
        break;
    case KIND_SCALE:
        status = bento_ui_scale_create(ui, args, out_id);
        break;
    case KIND_SPINBOX:
        status = bento_ui_spinbox_create(ui, args, out_id);
        break;
    case KIND_BUTTON:
        status = bento_ui_button_create(ui, args, out_id);
        break;
    case KIND_LED:
        status = bento_ui_led_create(ui, args, out_id);
        break;
    default:
        status = bento_ui_label_create(ui, args, out_id);
        break;
    }
    return status;
}

/* One widget; a ruler gets its ticks, the threshold box its digits. */
static bento_status_t create_widget(knob_panel_t *panel,
                                    const widget_spec_t *spec)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t widget;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    if (spec->text != NULL) {
        args.text = spec->text;
        args.text_len = (uint32_t)strlen(spec->text);
    }
    args.x = spec->x;
    args.y = spec->y;
    args.w = spec->w;
    args.h = spec->h;
    args.color = spec->color;
    args.font_px = spec->value;
    args.min_val = spec->low;
    args.max_val = spec->high;
    status = create_kind(panel->ui, spec->kind, &args, &widget);
    if ((status == BENTO_OK) && (spec->slot != SLOT_NONE)) {
        panel->ids[spec->slot] = widget;
    }
    if ((status == BENTO_OK) && (spec->kind == KIND_SCALE)) {
        (void)bento_ui_set_ticks(panel->ui, widget, SCALE_TICKS,
                                 SCALE_MAJOR);
    }
    if ((status == BENTO_OK) && (spec->kind == KIND_SPINBOX)) {
        (void)bento_ui_set_digits(panel->ui, widget, THRESHOLD_DIGITS,
                                  0);
    }
    return status;
}

/* Widgets in the original's order (a later one covers an earlier one
 * where they meet). A card's low is its border colour, high its corner
 * radius, value its border width. */
static bento_status_t create_widgets(knob_panel_t *panel,
                                     const widget_spec_t *widgets,
                                     uint32_t count)
{
    bento_status_t status = BENTO_OK;
    uint32_t i;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = create_widget(panel, &widgets[i]);
    }
    return status;
}

/* The title and the knob on its own ruler, with raw counts and volts. */
static bento_status_t build_knob_card(knob_panel_t *panel)
{
    static const widget_spec_t widgets[] = {
        {KIND_LABEL, "แผงคุมลูกบิดกับแถบสัมผัส", 16, 8, -1, -1, COLOR_TEXT,
         24, 0, 0, SLOT_NONE},
        {KIND_PANEL, NULL, 12, 40, 468, 200, COLOR_CARD, 1, COLOR_DIM,
         CARD_RADIUS, SLOT_NONE},
        {KIND_LABEL, "ลูกบิดเทียบพิสัย 0-100", 28, 48, -1, -1, COLOR_DIM, 20,
         0, 0, SLOT_NONE},
        {KIND_BAR, NULL, 28, 80, 428, 16, COLOR_BAR, 0, 0, PERCENT_MAX,
         SLOT_POT_BAR},
        {KIND_SCALE, NULL, 28, 92, 428, 44, COLOR_TEXT, 0, 0,
         PERCENT_MAX, SLOT_NONE},
        {KIND_LABEL, "เปอร์เซ็นต์ที่วัดได้", 28, 152, -1, -1, COLOR_DIM, 20, 0,
         0, SLOT_NONE},
        {KIND_LABEL, "0.0 %", 28, 180, -1, -1, COLOR_TEXT, 28, 0, 0,
         SLOT_PERCENT},
        {KIND_LABEL, "ค่าดิบ 0", 252, 144, -1, -1, COLOR_DIM, 20, 0, 0,
         SLOT_RAW},
        {KIND_LABEL, "โวลต์ 0.000 V", 252, 166, -1, -1, COLOR_DIM, 20, 0,
         0, SLOT_VOLT},
    };

    return create_widgets(
        panel, widgets,
        (uint32_t)(sizeof(widgets) / sizeof(widgets[0])));
}

/* The threshold the user sets, and the two lamps that compare with it. */
static bento_status_t build_threshold_card(knob_panel_t *panel)
{
    static const widget_spec_t widgets[] = {
        {KIND_PANEL, NULL, 492, 40, 288, 224, COLOR_CARD, 1, COLOR_DIM,
         CARD_RADIUS, SLOT_NONE},
        {KIND_LABEL, "เกณฑ์เตือน (เปอร์เซ็นต์)", 508, 48, -1, -1, COLOR_DIM,
         20, 0, 0, SLOT_NONE},
        {KIND_SPINBOX, NULL, 508, 76, 256, 88, COLOR_TEXT,
         THRESHOLD_START, THRESHOLD_MIN, THRESHOLD_MAX, SLOT_SPIN},
        {KIND_BUTTON, "เพิ่ม", 644, 172, 120, 88, COLOR_BUTTON, 20, 0, 0,
         SLOT_UP},
        {KIND_BUTTON, "ลด", 508, 172, 120, 88, COLOR_BUTTON, 20, 0, 0,
         SLOT_DOWN},
        {KIND_LABEL, "สถานะเทียบเกณฑ์", 492, 280, -1, -1, COLOR_DIM, 20,
         0, 0, SLOT_NONE},
        {KIND_LED, NULL, 492, 304, 48, 48, COLOR_OK, 1, 0, 0,
         SLOT_LED_OK},
        {KIND_LABEL, "ต่ำกว่าเกณฑ์", 548, 320, -1, -1, COLOR_DIM, 20, 0, 0,
         SLOT_NONE},
        {KIND_LED, NULL, 492, 348, 48, 48, COLOR_BAD, 0, 0, 0,
         SLOT_LED_BAD},
        {KIND_LABEL, "เกิน", 548, 364, -1, -1, COLOR_DIM, 20, 0, 0,
         SLOT_NONE},
    };

    return create_widgets(
        panel, widgets,
        (uint32_t)(sizeof(widgets) / sizeof(widgets[0])));
}

/* The slider and the copper buttons, the quality line, raw against filtered. */
static bento_status_t build_touch_card(knob_panel_t *panel)
{
    static const widget_spec_t widgets[] = {
        {KIND_PANEL, NULL, 12, 248, 468, 140, COLOR_CARD, 1, COLOR_DIM,
         CARD_RADIUS, SLOT_NONE},
        {KIND_LABEL, "แถบสัมผัสและปุ่มทองแดง", 28, 256, -1, -1, COLOR_DIM,
         20, 0, 0, SLOT_NONE},
        {KIND_BAR, NULL, 28, 284, 428, 16, COLOR_BAR, 0, 0, PERCENT_MAX,
         SLOT_TOUCH_BAR},
        {KIND_SCALE, NULL, 28, 300, 428, 40, COLOR_TEXT, 0, 0,
         PERCENT_MAX, SLOT_NONE},
        {KIND_LED, NULL, 28, 344, 48, 48, COLOR_OK, 0, 0, 0,
         SLOT_LED_BUTTON0},
        {KIND_LABEL, "ปุ่ม 0", 84, 356, -1, -1, COLOR_DIM, 20, 0, 0,
         SLOT_NONE},
        {KIND_LED, NULL, 160, 344, 48, 48, COLOR_OK, 0, 0, 0,
         SLOT_LED_BUTTON1},
        {KIND_LABEL, "ปุ่ม 1", 216, 356, -1, -1, COLOR_DIM, 20, 0, 0,
         SLOT_NONE},
        {KIND_PANEL, NULL, 280, 336, 196, 52, COLOR_CARD, 1, COLOR_DIM,
         CARD_RADIUS, SLOT_NONE},
        {KIND_LABEL, "คุณภาพของค่า", 292, 340, -1, -1, COLOR_DIM, 20, 0,
         0, SLOT_NONE},
        {KIND_LABEL, "อ่านค่าปกติ", 292, 364, -1, -1, COLOR_TEXT, 20, 0, 0,
         SLOT_HEALTH},
        {KIND_LABEL, "ดิบ 0.00 %", 252, 188, -1, -1, COLOR_TEXT, 20, 0,
         0, SLOT_RAW_LINE},
        {KIND_LABEL, "กรอง 0.00 %", 252, 210, -1, -1, COLOR_TEXT, 20, 0,
         0, SLOT_EMA_LINE},
        {KIND_LABEL, "อัลฟา 0.2", 380, 210, -1, -1, COLOR_DIM, 20, 0, 0,
         SLOT_NONE},
    };

    return create_widgets(
        panel, widgets,
        (uint32_t)(sizeof(widgets) / sizeof(widgets[0])));
}

static bento_status_t build_widgets(knob_panel_t *panel)
{
    bento_status_t status = build_knob_card(panel);

    if (status == BENTO_OK) {
        status = build_threshold_card(panel);
    }
    if (status == BENTO_OK) {
        status = build_touch_card(panel);
    }
    return status;
}

static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* ui.poll() here only keeps the queue moving; a failure is no events. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* The first read after a reset may wait a while for the display core:
 * warm up here, before the screen, not in the loop. */
static void warm_up_sensor(void)
{
    uint16_t raw = 0U;

    if (bento_sensors_pot_read(&raw) != BENTO_OK) {
        (void)printf("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ในลูป\n");
    }
}

static bento_status_t build_screen(knob_panel_t *panel)
{
    /* ui.screen() and ui.clear() are both sent, as the original does */
    bento_status_t status =
        bento_ui_screen(panel->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = bento_ui_clear(panel->ui);
    }
    if (status == BENTO_OK) {
        status = wait_ms(CLEAR_MS);
    }
    if (status == BENTO_OK) {
        warm_up_sensor();
        status = build_widgets(panel);
    }
    if (status == BENTO_OK) {
        drain_events(panel->ui);
    }
    return status;
}

/* Each read asks the display core once. A failed read keeps the last
 * good values and marks them stale; the buttons read 0 that round. */
static void read_sensors(reading_t *reading)
{
    uint16_t raw = 0U;
    float volts = 0.0F;
    float percent = 0.0F;
    uint8_t slider = 0U;
    uint8_t button0 = 0U;
    uint8_t button1 = 0U;
    bento_status_t status = bento_sensors_pot_read(&raw);

    if (status == BENTO_OK) {
        reading->raw = raw; /* 0 - 65535 ADC counts */
        status = bento_sensors_pot_voltage(&volts);
    }
    if (status == BENTO_OK) {
        reading->volts = volts; /* from the percentage, not measured */
        /* เติม: อ่าน percent ด้วย bento_sensors_pot_percent() แล้วเก็บลง reading->percent
         * สงสัยว่าเลขดิบกับเปอร์เซ็นต์กับโวลต์ผูกกันอย่างไร: examples/s05/05_adc_counts_to_volts.py
         * ลากเส้นทั้งสองเส้นคู่กัน ค่าจริงที่เรียบ กับค่าที่ ADC เห็นซึ่งเป็นบันได
         * และชี้กับดักตัวหารสองตัว 2**n กับ 2**n - 1 ที่สลับกันแล้วเพี้ยนน้อยจนจับไม่ได้ */
        (void)percent; /* used once this blank is filled in */

        /* เติม: อ่าน slider ด้วย bento_sensors_capsense_slider() แล้วเก็บลง reading->slider
         * ปล่อยนิ้วแล้วค่าไม่กลับเป็นศูนย์ นั่นไม่ใช่บั๊ก: examples/s05/01_capsense_dimmer.py
         * วาดค่าดิบคู่กับระดับที่สั่งไฟจริง ให้เห็นว่าตอนไม่มีนิ้วแตะ ค่าดิบลงไปติดลบ
         * และเตือนว่า slider เป็น 0 ไม่ได้แปลว่าปล่อยนิ้ว มันแปลว่าแตะที่ปลายซ้ายก็ได้ */
        (void)slider; /* used once this blank is filled in */

        status = bento_sensors_capsense_buttons(&button0, &button1);
    }
    reading->fresh = status == BENTO_OK;
    reading->button0 = reading->fresh ? button0 : 0U;
    reading->button1 = reading->fresh ? button1 : 0U;
}

/* int(max(0, min(100, value))): clamp first, so an out-of-range
 * driver value never breaks the screen. */
static int32_t clamp_percent(float value)
{
    float clamped = value;

    if (clamped > (float)PERCENT_MAX) {
        clamped = (float)PERCENT_MAX;
    }
    if (clamped < 0.0F) {
        clamped = 0.0F;
    }
    return (int32_t)clamped;
}

/* Bars and lamps move every round: the eye reads a position at once.
 * One lamp of the pair is lit, never both. */
static bento_status_t show_round(knob_panel_t *panel)
{
    const reading_t *reading = &panel->reading;
    const bento_status_t status = BENTO_OK;

    /* เติม: ส่งค่า clamp_percent(reading->percent) เข้าแถบ SLOT_POT_BAR ด้วย set_value()
     * แถบสั่นยิบ ๆ ทั้งที่ไม่มีใครแตะลูกบิด: examples/s05/03_pot_setpoint_deadband.py
     * เพิ่มกติกาข้อเดียวว่า "ขยับไม่ถึงเท่านี้ ถือว่าไม่ได้ขยับ" แล้วค่าที่รายงานกลายเป็น
     * ขั้นบันไดที่นิ่ง ใช้ตอนต้องส่งค่าออกเครือข่าย จะลดจำนวนข้อความลงหลายเท่า */

    /* ครอบขอบเขตก่อนส่งเข้า widget เสมอ ค่าที่มาจากภายนอกโปรแกรมเราไม่มีใครรับประกันให้ */
    set_value(panel, SLOT_TOUCH_BAR,
              clamp_percent((float)reading->slider));

    /* เติม: ป้อน reading->percent เข้า bento_dsp_ema_state_update() กับ panel->ema
     * ได้ค่าที่กรองแล้วกลับมาใน panel->ema_percent แล้วเก็บผลไว้ใน status
     * (เอา const ออกจากบรรทัดประกาศด้วย) ป้อนเข้า เอาค่าที่กรองแล้วออก - แค่นั้นจริง ๆ
     * แต่ EMA ไม่ได้ชนะทุกงาน examples/s05/07_median_beats_mean.py ยิงค่าหลุดเข้าไปหนึ่งค่า
     * แล้วให้ดูว่าค่าเฉลี่ยถูกลากตามไปหลายรอบ ส่วน median แบนสนิท
     * ถ้างานของทีมเจอค่าหลุดบ่อย ให้เลือกจากภาพนั้น */

    panel->over = panel->ema_percent >= (float)panel->threshold;
    set_value(panel, SLOT_LED_OK, panel->over ? 0 : 1);
    set_value(panel, SLOT_LED_BAD, panel->over ? 1 : 0);
    set_value(panel, SLOT_LED_BUTTON0,
              (reading->button0 != 0U) ? 1 : 0);
    set_value(panel, SLOT_LED_BUTTON1,
              (reading->button1 != 0U) ? 1 : 0);
    return status;
}

/* Up and down are two buttons, one per direction, never one toggle. */
static void handle_click(knob_panel_t *panel, bento_ui_id_t widget)
{
    if (widget == panel->ids[SLOT_UP]) {
        panel->threshold += THRESHOLD_STEP;
        if (panel->threshold > THRESHOLD_MAX) {
            panel->threshold = THRESHOLD_MAX;
        }
        set_value(panel, SLOT_SPIN, panel->threshold);
    } else if (widget == panel->ids[SLOT_DOWN]) {
        panel->threshold -= THRESHOLD_STEP;
        if (panel->threshold < THRESHOLD_MIN) {
            panel->threshold = THRESHOLD_MIN;
        }
        set_value(panel, SLOT_SPIN, panel->threshold);
    } else {
        /* any other widget sets nothing */
    }
}

static void read_buttons(knob_panel_t *panel)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX] = {0};
    uint32_t count = 0U;
    uint32_t i;

    /* เติม: อ่านเหตุการณ์ด้วย bento_ui_poll() ลงใน events และ count (0 เมื่อ poll ไม่สำเร็จ)
     * กฎเหล็กข้อ 1: ต้องเรียกทุกรอบ ไม่งั้น widget บางตัวจะถูกซ่อนไว้นานถึงสองวินาที
     * และปุ่มเพิ่ม/ลดเกณฑ์จะกดไม่ติดเลย เพราะ event ไม่มีทางเดินทางกลับมาถึงเรา */
    for (i = 0U; i < count; i++) {
        if (events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) {
            handle_click(panel, events[i].id);
        }
    }
}

/* "<prefix><number with digits decimals><suffix>" into one label. */
static void show_real(const knob_panel_t *panel, slot_t slot,
                      const char *prefix, float value, int32_t digits,
                      const char *suffix)
{
    char number[BENTO_FMT_BUF_SIZE];
    char text[TEXT_BYTES];
    uint32_t length = 0U;

    if (bento_fmt_real_precision(number, sizeof(number),
                                 (bento_real_t)value, digits,
                                 &length) != BENTO_OK) {
        number[0] = '\0';
    }
    (void)snprintf(text, sizeof(text), "%s%s%s", prefix, number,
                   suffix);
    set_text(panel->ui, panel->ids[slot], text);
}

/* The quality line: its colour first, then its text, so a new text is
 * never shown in the old state's colour. */
static void pick_health(knob_panel_t *panel)
{
    const bento_ui_id_t label = panel->ids[SLOT_HEALTH];

    if (!panel->reading.fresh) {
        (void)bento_ui_set_color(panel->ui, label, COLOR_WARN);
        (void)snprintf(panel->health, sizeof(panel->health), "%s",
                       "ค่าค้าง - เลขคือค่าล่าสุด");
    } else if (panel->over) {
        (void)bento_ui_set_color(panel->ui, label, COLOR_BAD);
        (void)snprintf(panel->health, sizeof(panel->health),
                       "เกินเกณฑ์ %" PRId32 " %%", panel->threshold);
    } else {
        (void)bento_ui_set_color(panel->ui, label, COLOR_DIM);
        (void)snprintf(panel->health, sizeof(panel->health),
                       "ปกติ ต่ำกว่า %" PRId32 " %%", panel->threshold);
    }
}

/* Numbers people read are rewritten at most once a second, always in
 * the same place; two decimals on both lines keep the jitter visible. */
static void show_numbers(knob_panel_t *panel)
{
    const reading_t *reading = &panel->reading;
    const uint32_t seconds_now =
        bento_time_ticks_ms() / (uint32_t)MS_PER_SECOND;
    const int32_t second = (int32_t)seconds_now;
    char text[TEXT_BYTES];

    if (second != panel->last_second) {
        panel->last_second = second;
        show_real(panel, SLOT_PERCENT, "", reading->percent,
                  PERCENT_DIGITS, " %");
        (void)snprintf(text, sizeof(text), "ค่าดิบ %u",
                       (unsigned int)reading->raw);
        set_text(panel->ui, panel->ids[SLOT_RAW], text);
        show_real(panel, SLOT_VOLT, "โวลต์ ", reading->volts,
                  VOLT_DIGITS, " V");
        show_real(panel, SLOT_RAW_LINE, "ดิบ    ", reading->percent,
                  LINE_DIGITS, " %");
        show_real(panel, SLOT_EMA_LINE, "กรอง  ", panel->ema_percent,
                  LINE_DIGITS, " %");
        pick_health(panel);
    }
}

/* One 200 ms round. The quality line is sent every round on purpose:
 * a text keeps the display core in its fast mode, a bar value does
 * not, and an unchanged text is not redrawn. */
static bento_status_t run_round(knob_panel_t *panel)
{
    bento_status_t status;

    read_sensors(&panel->reading);
    status = show_round(panel);
    if (status == BENTO_OK) {
        read_buttons(panel);
        show_numbers(panel);
        set_text(panel->ui, panel->ids[SLOT_HEALTH], panel->health);
        status = wait_ms(LOOP_MS);
    }
    return status;
}

int main(void)
{
    knob_panel_t panel = {0};
    bento_status_t status;

    panel.ui = bento_runtime_get_ui();
    panel.threshold = THRESHOLD_START;
    panel.last_second = -1;
    (void)snprintf(panel.health, sizeof(panel.health), "%s",
                   "อ่านค่าปกติ");
    status = build_screen(&panel);
    /* ฟิลเตอร์ต้องเกิดนอกลูป เพราะมันต้องจำอดีต - สร้างในลูปเมื่อไร ค่าที่ได้จะเท่าค่าดิบเป๊ะ
     * เติม: สร้าง EMA alpha 0.2 ด้วย bento_dsp_ema_config_init() ตั้ง alpha แล้ว
     * bento_dsp_ema_state_init() ลงใน panel.ema เมื่อ status ยังเป็น BENTO_OK
     * 0.2 นี้แปลว่ากรองแรงแค่ไหน: examples/s05/06_ema_time_constant.py แปลง alpha เป็นวินาที
     * ด้วย tau = dt * (1-alpha)/alpha แล้ววาดเทียบกับ alpha ของขั้นก่อนหน้าให้เห็นบนกราฟเดียวกัน
     * ที่นั่นยังเตือนไว้ด้วยว่า ถ้าไม่ใส่ alpha ค่าตั้งต้นคือ 0.1 ไม่ใช่ 0.5 อย่างที่หลายคนเดา */
    while (status == BENTO_OK) {
        status = run_round(&panel);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
