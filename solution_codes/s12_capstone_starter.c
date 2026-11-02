/*
 * s12_capstone_starter.c -- คำตอบหนึ่งที่เป็นไปได้: Tilt Alarm สำหรับ
 * นั่งร้าน/ชั้นวาง
 *
 * วัดมุมเอียงจาก IMU เทียบกับท่าตั้งต้นที่จำไว้ตอนเริ่มรัน กรองด้วย
 * EMA แล้วตัดสินเป็นสามระดับ ปกติ เฝ้าระวัง ผิดปกติ ระดับใหม่ต้องซ้ำ
 * ครบสามรอบก่อนเปลี่ยนสถานะ ผิดปกติแล้วไฟเตือนหน้างานติดและค้างไว้
 * จนคนกด "รับทราบ" ส่ง JSON ขึ้น MQTT broker ตอนเกิดเหตุ ตอนกลับปกติ
 * ตอนรับทราบ ตอนเน็ตกลับมา และทุก 30 วินาที เน็ตหลุดแล้วจอยังทำงาน
 * และนับครั้งที่ส่งไม่ออก สั่งปิดไฟเตือนต้องผ่านกล่องยืนยันก่อน
 * วิธีใช้: แตะการ์ด Playground บนหน้า Home แก้ค่าบนหัวไฟล์ให้เป็นของ
 * ทีม แล้ววางบอร์ดให้นิ่งตอนเริ่มรัน
 *
 * Measures the tilt from the IMU against the pose remembered at the
 * start, smooths it with an EMA and decides one of three levels:
 * normal, watch, alarm. A new level must repeat three rounds before
 * the state changes. An alarm lights the site beacon and stays
 * latched until someone taps "acknowledge". JSON goes to the MQTT
 * broker on an event, on clearing, on acknowledge, when the network
 * comes back and every 30 seconds. When the network drops, the
 * screen keeps working and counts the messages that did not get out.
 * Turning the beacon off asks for a confirmation first. Open the
 * Playground card, edit the values at the top for your team and keep
 * the board still when it starts.
 *
 * Twin of s12_capstone_starter.py
 * (embedded-systems-for-aiot-developer/solution_codes/s12_capstone_starter.py).
 * Style: CLEAN_C.md.
 *
 * Differs from the original, where C or the C API forces it:
 *   - There is no json module in C: the payload is written with
 *     snprintf as json.dumps writes it, keys in the PSoC Edge
 *     MicroPython dict order (state, v, kind, t, id, unit: py/map.c
 *     with one hash byte), ", " and ": " between them. round(v, 1) is
 *     nearbyint(v * 10) / 10 in the board's float width
 *     (py/modbuiltins.c), printed as repr() prints it (bento_fmt_real).
 *   - wifi.connect() and mqtt.connect() block in MicroPython; here they
 *     are started and polled every 50 ms, and Stop cancels the wait.
 *     The bindings' own console lines are not printed: the C libraries
 *     log nothing. The C library holds at most two MQTT clients, so a
 *     reconnect closes the old client before it opens a new one.
 *   - mqtt.publish() raising OSError (the link dropped between the
 *     check and the send) is BENTO_E_SESSION here and ends the program
 *     after the finally, as the uncaught OSError does; any other
 *     refusal is the binding's False, which send() ignores.
 *   - x ** 0.5 is pow(x, 0.5) in the board's float width, as
 *     py/objfloat.c computes it. dsp.EMA is the C filter state with the
 *     binding's arithmetic (bento_dsp_ema_state_update).
 *   - sensors.bmi270.motion() raises OSError on a failed read; every
 *     failing status takes the except path, and BENTO_E_UNSUPPORTED
 *     (no such sensor: an AttributeError nothing catches) ends the
 *     program. On the reTerminal there is no wifi or mqtt module
 *     (ImportError at the start); here the screen is drawn and the
 *     join ends the program with BENTO_E_UNSUPPORTED, after the
 *     finally.
 *   - The finally (mqtt.disconnect(), then the link shown offline)
 *     runs after the loop, whatever ended it.
 *   - In the BENTO Emulator, MQTT from C is not available yet: the
 *     program says so (a label in the title bar and a printed line) and
 *     then works offline, as it does whenever the broker cannot be
 *     reached: the screen keeps running, the link is retried every
 *     10 s and every send that cannot get out is counted as missed.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_dsp.h"
#include "bento_fmt.h"
#include "bento_gpio.h"
#include "bento_mqtt.h"
#include "bento_runtime.h"
#include "bento_sensors.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* Each multiply and add is rounded on its own, as MicroPython does. */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

/* ---- CONFIG ---- */
/* The Wi-Fi, the broker, the topic and the beacon's LED name are also
 * CONFIG; each lives in the one function that reads it (MISRA 8.9):
 * join_wifi(), connect_broker(), send_message() and find_beacon(). */
static const char DEVICE_ID[] = "team01";
static const char UNIT[] = "deg";
static const bento_real_t WARN_LIMIT =
    (bento_real_t)8.0; /* from the start pose */
static const bento_real_t ALERT_LIMIT =
    (bento_real_t)15.0; /* above this = alarm */

enum {
    SCALE_MAX = 45, /* the end of the meter on screen */
    CONFIRM_N = 3,  /* rounds in a row before a level is believed */
    HEARTBEAT_MS = 30000,
    ALERT_GAP_MS = 15000,
    RETRY_MS = 10000,
    LOOP_MS = 200,
    SETTLE_MS = 200,
    BASELINE_READS = 10,
    BASELINE_STEP_MS = 100,
    NET_STEP_MS = 50, /* how often a blocking connect is asked */
    MQTT_PORT = 1883,
    CLIENT_SUFFIX_MASK = 0xFFFF,
    MS_PER_S = 1000,
    DECIMAL_BASE = 10,
    SCALE_TICKS = 10,
    SCALE_MAJOR_EVERY = 3
};

enum { /* text sizes */
    TEXT_BYTES = 128,
    PAYLOAD_BYTES = 160,
    NUMBER_BYTES = BENTO_FMT_BUF_SIZE,
    CLIENT_ID_BYTES = (int32_t)BENTO_MQTT_CLIENT_ID_MAX + 1
};

enum { /* the two labels whose text is built at run time */
    TITLE_X = 24,
    TITLE_Y = 8,
    LIMITS_X = 40,
    LIMITS_Y = 208,
    LABEL_PX = 20
};

enum { /* colours, 0xRRGGBB */
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_RUN = 0x4A9EFF,
    COLOR_QUIET = 0x3A4150
};

/* The three levels, mildest first. */
enum {
    LEVEL_OK = 0,
    LEVEL_WARN = 1,
    LEVEL_ALERT = 2,
    LEVEL_COUNT = 3
};

/* Which ui factory a row of a widget table calls. */
typedef enum {
    KIND_LABEL = 0,
    KIND_LED,
    KIND_CARD,
    KIND_BUTTON,
    KIND_BAR,
    KIND_SCALE,
    KIND_MSGBOX
} widget_kind_t;

/* A widget as the original creates it; w and h -1 = automatic. */
typedef struct {
    widget_kind_t kind;
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

/* The widgets the program changes after it has drawn them. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t mqtt_lamp;
    bento_ui_id_t mqtt_label;
    bento_ui_id_t value_label;
    bento_ui_id_t quality_label;
    bento_ui_id_t value_bar;
    bento_ui_id_t value_scale;
    bento_ui_id_t latch_label;
    bento_ui_id_t lamp_ok;
    bento_ui_id_t lamp_warn;
    bento_ui_id_t lamp_bad;
    bento_ui_id_t state_label;
    bento_ui_id_t acknowledge_button;
    bento_ui_id_t beacon_lamp;
    bento_ui_id_t beacon_state;
    bento_ui_id_t on_button;
    bento_ui_id_t off_button;
    bento_ui_id_t net_label;
    bento_ui_id_t sent_label;
    bento_ui_id_t box;
    bento_ui_id_t yes_button;
    bento_ui_id_t no_button;
} alarm_screen_t;

/* The tilt measurement: the filter, the start pose, the last good
 * reading and whether this one is stale. */
typedef struct {
    bento_dsp_ema_state_t smooth;
    bento_real_t base_roll;
    bento_real_t base_pitch;
    bento_real_t last_roll;
    bento_real_t last_pitch;
    bool stale;
} tilt_t;

/* The broker link. */
typedef struct {
    char client_id[CLIENT_ID_BYTES];
    uint32_t handle;
    bool open;
    bool online;
    bool no_broker; /* MQTT unsupported here: the BENTO Emulator */
} link_t;

/* What has been decided and counted. */
typedef struct {
    int32_t state;
    int32_t pending;
    int32_t streak;
    bool latched;
    bool asking;
    bool has_alert;
    int32_t missed;
    int32_t sent;
    uint32_t last_alert_ms;
    uint32_t beat_ms;
    uint32_t retry_ms;
    bento_real_t value;
} decision_t;

/* What show() last wrote, so it writes only what changed. */
typedef struct {
    int32_t last_second;
    bool has_shown;
    int32_t shown_state;
    bool shown_latched;
} shown_t;

/* The whole program. */
typedef struct {
    alarm_screen_t screen;
    tilt_t tilt;
    link_t link;
    decision_t decision;
    shown_t shown;
    uint32_t beacon_led;
} tilt_alarm_t;

/* Turns Stop into BENTO_E_CANCELLED after a sleep. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static void set_value(bento_ui_t *ui, bento_ui_id_t widget,
                      int32_t value)
{
    (void)bento_ui_set_value(ui, widget, value);
}

/* show() and hide() are setters too: a refusal is ignored. */
static void set_shown(bento_ui_t *ui, bento_ui_id_t widget, bool shown)
{
    (void)bento_ui_set_visible(ui, widget, shown ? 1U : 0U);
}

/* A real number as '%.<digits>f' or repr() writes it on this board. */
static void real_text(bento_real_t value, int32_t digits, char *text)
{
    uint32_t length = 0U;
    const bento_status_t status =
        (digits < 0)
            ? bento_fmt_real(text, NUMBER_BYTES, value, &length)
            : bento_fmt_real_precision(text, NUMBER_BYTES, value,
                                       digits, &length);

    if (status != BENTO_OK) {
        text[0] = '\0';
    }
}

/* One widget of a table. */
static bento_status_t add_widget(bento_ui_t *ui,
                                 const widget_spec_t *spec,
                                 bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;
    bento_status_t status = BENTO_E_INVALID_ARGUMENT;

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
    args.min_val = spec->min;
    args.max_val = spec->max;
    switch (spec->kind) {
    case KIND_LABEL:
        status = bento_ui_label_create(ui, &args, out_id);
        break;
    case KIND_LED:
        status = bento_ui_led_create(ui, &args, out_id);
        break;
    case KIND_CARD:
        status = bento_ui_panel_create(ui, &args, out_id);
        break;
    case KIND_BUTTON:
        status = bento_ui_button_create(ui, &args, out_id);
        break;
    case KIND_BAR:
        status = bento_ui_bar_create(ui, &args, out_id);
        break;
    case KIND_SCALE:
        status = bento_ui_scale_create(ui, &args, out_id);
        break;
    case KIND_MSGBOX:
        status = bento_ui_msgbox_create(ui, &args, out_id);
        break;
    default:
        /* every kind is listed above */
        break;
    }
    return status;
}

/* A table of widgets in the original's order; outs[i] NULL = a widget
 * the program never changes. */
static bento_status_t add_widgets(bento_ui_t *ui,
                                  const widget_spec_t *specs,
                                  bento_ui_id_t *const *outs,
                                  size_t count)
{
    bento_ui_id_t unused = 0U;
    bento_status_t status = BENTO_OK;
    size_t i;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = add_widget(ui, &specs[i],
                            (outs[i] != NULL) ? outs[i] : &unused);
    }
    return status;
}

/* The title and the MQTT lamp. */
static bento_status_t add_title(alarm_screen_t *screen)
{
    static const widget_spec_t specs[] = {
        {KIND_LED, NULL, 606, 12, 18, 18, COLOR_OK, 0, 0, 0},
        {KIND_LABEL, "MQTT: ออฟไลน์", 632, 10, -1, -1, COLOR_DIM, 20, 0,
         0},
    };
    bento_ui_id_t *const outs[] = {&screen->mqtt_lamp,
                                   &screen->mqtt_label};
    char text[TEXT_BYTES];
    widget_spec_t title = {KIND_LABEL, NULL, TITLE_X,    TITLE_Y,
                           -1,         -1,   COLOR_TEXT, LABEL_PX,
                           0,          0};
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    (void)snprintf(text, sizeof(text), "นั่งร้านเอียง - %s", DEVICE_ID);
    title.text = text;
    status = add_widget(screen->ui, &title, &unused);
    if (status == BENTO_OK) {
        status = add_widgets(screen->ui, specs, outs,
                             sizeof(specs) / sizeof(specs[0]));
    }
    return status;
}

/* The top-left card: the value against its range. The card comes
 * first, or it paints over what sits on it. */
static bento_status_t add_value_card(alarm_screen_t *screen)
{
    static const widget_spec_t specs[] = {
        {KIND_CARD, NULL, 24, 48, 464, 184, COLOR_CARD, 1, COLOR_DIM,
         12},
        {KIND_LABEL, "ค่าที่วัดได้ เทียบกับเกณฑ์", 40, 56, -1, -1, COLOR_DIM,
         20, 0, 0},
        {KIND_LABEL, "--", 40, 88, -1, -1, COLOR_TEXT, 28, 0, 0},
        {KIND_LABEL, "รอค่าแรก", 248, 96, -1, -1, COLOR_DIM, 20, 0, 0},
        {KIND_BAR, NULL, 40, 136, 432, 16, COLOR_RUN, 0, 0, SCALE_MAX},
        {KIND_SCALE, NULL, 40, 156, 432, 48, COLOR_TEXT, 0, 0,
         SCALE_MAX},
    };
    bento_ui_id_t *const outs[] = {NULL,
                                   NULL,
                                   &screen->value_label,
                                   &screen->quality_label,
                                   &screen->value_bar,
                                   &screen->value_scale};
    char warn[NUMBER_BYTES];
    char alert[NUMBER_BYTES];
    char text[TEXT_BYTES];
    widget_spec_t limits = {KIND_LABEL, NULL, LIMITS_X,  LIMITS_Y,
                            -1,         -1,   COLOR_DIM, LABEL_PX,
                            0,          0};
    bento_ui_id_t unused = 0U;
    bento_status_t status = add_widgets(
        screen->ui, specs, outs, sizeof(specs) / sizeof(specs[0]));

    if (status == BENTO_OK) {
        (void)bento_ui_set_ticks(screen->ui, screen->value_scale,
                                 SCALE_TICKS, SCALE_MAJOR_EVERY);
        real_text(WARN_LIMIT, -1, warn);
        real_text(ALERT_LIMIT, -1, alert);
        (void)snprintf(text, sizeof(text), "เฝ้าระวัง %s - ผิดปกติ %s %s",
                       warn, alert, UNIT);
        limits.text = text;
        status = add_widget(screen->ui, &limits, &unused);
    }
    return status;
}

/* The bottom-left card: the decided state as three lamps, one lit. */
static bento_status_t add_state_card(alarm_screen_t *screen)
{
    static const widget_spec_t specs[] = {
        {KIND_CARD, NULL, 24, 240, 464, 152, COLOR_CARD, 1, COLOR_DIM,
         12},
        {KIND_LABEL, "สถานะที่ตัดสินแล้ว", 40, 248, -1, -1, COLOR_DIM, 20, 0,
         0},
        {KIND_LABEL, "รอคนรับทราบ", 336, 244, -1, -1, COLOR_WARN, 20, 0,
         0},
        {KIND_LED, NULL, 40, 276, 48, 48, COLOR_OK, 1, 0, 0},
        {KIND_LED, NULL, 144, 276, 48, 48, COLOR_WARN, 0, 0, 0},
        {KIND_LED, NULL, 248, 276, 48, 48, COLOR_BAD, 0, 0, 0},
        {KIND_LABEL, "ปกติ", 40, 328, -1, -1, COLOR_DIM, 20, 0, 0},
        {KIND_LABEL, "เฝ้าระวัง", 144, 328, -1, -1, COLOR_DIM, 20, 0, 0},
        {KIND_LABEL, "ผิดปกติ", 248, 328, -1, -1, COLOR_DIM, 20, 0, 0},
        {KIND_LABEL, "กำลังวัดท่าตั้งต้น", 40, 356, -1, -1, COLOR_TEXT, 20, 0,
         0},
        {KIND_BUTTON, "รับทราบ", 368, 276, 104, 88, COLOR_QUIET, 20, 0,
         0},
    };
    bento_ui_id_t *const outs[] = {NULL,
                                   NULL,
                                   &screen->latch_label,
                                   &screen->lamp_ok,
                                   &screen->lamp_warn,
                                   &screen->lamp_bad,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &screen->state_label,
                                   &screen->acknowledge_button};
    const size_t latch_row = 2U;
    bento_status_t status =
        add_widgets(screen->ui, specs, outs, latch_row + 1U);

    if (status == BENTO_OK) { /* hidden before the lamps, as drawn */
        set_shown(screen->ui, screen->latch_label, false);
        status = add_widgets(
            screen->ui, &specs[latch_row + 1U], &outs[latch_row + 1U],
            (sizeof(specs) / sizeof(specs[0])) - (latch_row + 1U));
    }
    return status;
}

/* The right card: the site beacon with separate on and off buttons
 * (a toggle cannot tell which state it is in), and the network. */
static bento_status_t add_beacon_card(alarm_screen_t *screen)
{
    static const widget_spec_t specs[] = {
        {KIND_CARD, NULL, 504, 48, 264, 344, COLOR_CARD, 1, COLOR_DIM,
         12},
        {KIND_LABEL, "ไฟเตือนหน้างาน", 520, 56, -1, -1, COLOR_DIM, 20, 0,
         0},
        {KIND_LED, NULL, 520, 88, 48, 48, COLOR_BAD, 0, 0, 0},
        {KIND_LABEL, "ไฟดับอยู่", 576, 100, -1, -1, COLOR_DIM, 20, 0, 0},
        {KIND_LABEL, "สั่งปิดต้องยืนยันก่อน", 520, 144, -1, -1, COLOR_DIM, 20,
         0, 0},
        {KIND_BUTTON, "เปิดไฟ", 520, 176, 96, 88, COLOR_OK, 20, 0, 0},
        {KIND_BUTTON, "ปิดไฟ", 648, 176, 96, 88, COLOR_QUIET, 20, 0, 0},
        {KIND_LABEL, "net: starting", 520, 296, -1, -1, COLOR_DIM, 20,
         0, 0},
        {KIND_LABEL, "ส่งแล้ว 0 ใบ", 520, 336, -1, -1, COLOR_DIM, 20, 0,
         0},
    };
    bento_ui_id_t *const outs[] = {NULL,
                                   NULL,
                                   &screen->beacon_lamp,
                                   &screen->beacon_state,
                                   NULL,
                                   &screen->on_button,
                                   &screen->off_button,
                                   &screen->net_label,
                                   &screen->sent_label};

    return add_widgets(screen->ui, specs, outs,
                       sizeof(specs) / sizeof(specs[0]));
}

/* The confirmation says what will happen, not "are you sure". The
 * MsgBox's own buttons send no events yet, so two ui buttons stand
 * in. */
static bento_status_t add_confirm_box(alarm_screen_t *screen)
{
    static const widget_spec_t box = {
        KIND_MSGBOX, "ปิดไฟเตือน\nไฟหน้างานจะดับทันที",
        112,         96,
        568,         136,
        COLOR_CARD,  0,
        0,           0};
    static const widget_spec_t buttons[] = {
        {KIND_BUTTON, "ยืนยัน", 144, 248, 200, 88, COLOR_QUIET, 20, 0, 0},
        {KIND_BUTTON, "ยกเลิก", 376, 248, 200, 88, COLOR_QUIET, 20, 0,
         0},
    };
    bento_ui_id_t *const outs[] = {&screen->yes_button,
                                   &screen->no_button};
    bento_status_t status = add_widget(screen->ui, &box, &screen->box);

    if (status == BENTO_OK) {
        set_shown(screen->ui, screen->box, false);
        status = add_widgets(screen->ui, buttons, outs,
                             sizeof(buttons) / sizeof(buttons[0]));
    }
    if (status == BENTO_OK) {
        set_shown(screen->ui, screen->yes_button, false);
        set_shown(screen->ui, screen->no_button, false);
    }
    return status;
}

/* The bottom-right corner belongs to the Console button: no button
 * is placed there. */
static bento_status_t build_screen(alarm_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_title(screen);
    }
    if (status == BENTO_OK) {
        status = add_value_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_state_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_beacon_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_confirm_box(screen);
    }
    if (status ==
        BENTO_OK) { /* this poll only keeps the queue moving */
        (void)bento_ui_poll(screen->ui, events,
                            BENTO_UI_POLL_EVENTS_MAX, &count, NULL);
    }
    return status;
}

/* The LED by its name in the firmware's table: the index differs from
 * board to board, the name does not. Not found: LED 0. */
static bento_status_t find_beacon(uint32_t *led)
{
    static const char beacon_led[] =
        "RGB_RED"; /* by name: the Eva Kit uses LED1 */
    bento_gpio_board_info_t info;
    const char *wanted = "LED1";
    const char *name = NULL;
    uint32_t i;
    bento_status_t status;

    bento_gpio_board_info_init(&info);
    status = bento_gpio_board_info(&info);
    for (i = 0U; (i < info.leds) && (status == BENTO_OK); i++) {
        if ((bento_gpio_led_name(i, &name) == BENTO_OK) &&
            (strcmp(name, "RGB_GREEN") == 0)) {
            wanted = beacon_led; /* red on both boards */
        }
    }
    *led = 0U;
    for (i = info.leds; (i > 0U) && (status == BENTO_OK); i--) {
        if ((bento_gpio_led_name(i - 1U, &name) == BENTO_OK) &&
            (strcmp(name, wanted) == 0)) {
            *led =
                i - 1U; /* the first match wins: the search runs down */
        }
    }
    return status;
}

/* One IMU read as (roll, pitch). A failed read keeps the last tilt and
 * marks it stale; BENTO_E_UNSUPPORTED (no such sensor) ends the
 * program. */
static bento_status_t raw_tilt(tilt_t *tilt, bento_real_t *roll,
                               bento_real_t *pitch)
{
    bento_sensors_imu6_t motion;
    float roll_deg = 0.0F;
    float pitch_deg = 0.0F;
    bento_status_t status;

    bento_sensors_imu6_init(&motion);
    status = bento_sensors_bmi270_motion(&motion);
    if (status == BENTO_OK) { /* roll comes first */
        status = bento_dsp_tilt(motion.ax, motion.ay, motion.az,
                                &roll_deg, &pitch_deg);
    }
    tilt->stale = status != BENTO_OK;
    if (!tilt->stale) {
        tilt->last_roll = (bento_real_t)roll_deg;
        tilt->last_pitch = (bento_real_t)pitch_deg;
    }
    *roll = tilt->last_roll;
    *pitch = tilt->last_pitch;
    return (status == BENTO_E_UNSUPPORTED) ? status : BENTO_OK;
}

/* The start pose: every later reading is measured against it. */
static bento_status_t measure_baseline(tilt_t *tilt)
{
    const bento_real_t reads = (bento_real_t)10.0;
    bento_real_t roll = (bento_real_t)0;
    bento_real_t pitch = (bento_real_t)0;
    int32_t i;
    bento_status_t status = BENTO_OK;

    tilt->base_roll = (bento_real_t)0;
    tilt->base_pitch = (bento_real_t)0;
    for (i = 0; (i < BASELINE_READS) && (status == BENTO_OK); i++) {
        status = raw_tilt(tilt, &roll, &pitch);
        if (status == BENTO_OK) {
            tilt->base_roll += roll / reads;
            tilt->base_pitch += pitch / reads;
            status = wait_ms(BASELINE_STEP_MS);
        }
    }
    return status;
}

/* A tilt in any direction counts: the length of (roll, pitch) from
 * the start pose, smoothed. */
static bento_status_t read_value(tilt_t *tilt, bento_real_t *value)
{
    const bento_real_t half = (bento_real_t)0.5;
    bento_real_t roll = (bento_real_t)0;
    bento_real_t pitch = (bento_real_t)0;
    bento_real_t roll_off;
    bento_real_t pitch_off;
    bento_real_t square;
    bento_real_t length;
    float smoothed = 0.0F;
    bento_status_t status = raw_tilt(tilt, &roll, &pitch);

    roll_off = roll - tilt->base_roll;
    pitch_off = pitch - tilt->base_pitch;
    square = roll_off * roll_off;
    square += pitch_off * pitch_off;
#if BENTO_FMT_REAL_DOUBLE
    length = pow(square, half);
#else
    length = powf(square, half);
#endif
    if (status == BENTO_OK) {
        status = bento_dsp_ema_state_update(&tilt->smooth,
                                            (float)length, &smoothed);
    }
    *value = (bento_real_t)smoothed;
    return status;
}

/* The ifs run from the strictest down; swapped, ALERT is never
 * reached. */
static int32_t level_of(bento_real_t value)
{
    int32_t level = LEVEL_OK;

    if (value > ALERT_LIMIT) {
        level = LEVEL_ALERT;
    } else if (value > WARN_LIMIT) {
        level = LEVEL_WARN;
    } else {
        /* within the limits */
    }
    return level;
}

/* The latch and the state text change only when they change. */
static void show_state(const alarm_screen_t *screen, shown_t *shown,
                       const decision_t *decision)
{
    static const char *const level_texts[LEVEL_COUNT] = {
        "ปกติ ไม่ต้องทำอะไร", "เฝ้าดู ยังไม่ต้องเข้าไป", "ผิดปกติ ต้องมีคนไปดู"};

    if (!shown->has_shown || (decision->state != shown->shown_state) ||
        (decision->latched != shown->shown_latched)) {
        shown->has_shown = true;
        shown->shown_state = decision->state;
        shown->shown_latched = decision->latched;
        set_text(screen->ui, screen->state_label,
                 level_texts[decision->state]);
        set_shown(screen->ui, screen->latch_label, decision->latched);
    }
}

/* The numbers once a second. */
static void show_numbers(const alarm_screen_t *screen,
                         const tilt_alarm_t *monitor,
                         const char *net_text)
{
    char number[NUMBER_BYTES];
    char text[TEXT_BYTES];

    real_text(monitor->decision.value, 1, number);
    (void)snprintf(text, sizeof(text), "%s %s", number, UNIT);
    set_text(screen->ui, screen->value_label, text);
    set_text(screen->ui, screen->quality_label,
             monitor->tilt.stale ? "ค่าค้าง อ่านไม่ได้" : "ค่าปกติ");
    (void)bento_ui_set_color(screen->ui, screen->quality_label,
                             monitor->tilt.stale ? (uint32_t)COLOR_WARN
                                                 : (uint32_t)COLOR_DIM);
    set_text(screen->ui, screen->net_label, net_text);
    (void)snprintf(text, sizeof(text), "ส่งแล้ว %ld ใบ",
                   (long)monitor->decision.sent);
    set_text(screen->ui, screen->sent_label, text);
}

/* The meter and the lamps every round; the numbers once a second. */
static void show(tilt_alarm_t *monitor, uint32_t now_ms)
{
    const alarm_screen_t *screen = &monitor->screen;
    const decision_t *decision = &monitor->decision;
    const uint32_t whole_seconds = now_ms / (uint32_t)MS_PER_S;
    const int32_t second = (int32_t)whole_seconds;
    char net_text[TEXT_BYTES];

    set_value(screen->ui, screen->value_bar,
              (decision->value < (bento_real_t)SCALE_MAX)
                  ? (int32_t)decision->value
                  : SCALE_MAX);
    set_value(screen->ui, screen->lamp_ok,
              (decision->state == LEVEL_OK) ? 1 : 0);
    set_value(screen->ui, screen->lamp_warn,
              (decision->state == LEVEL_WARN) ? 1 : 0);
    set_value(screen->ui, screen->lamp_bad,
              (decision->state == LEVEL_ALERT) ? 1 : 0);
    show_state(screen, &monitor->shown, decision);
    if (second != monitor->shown.last_second) {
        monitor->shown.last_second = second;
        if (monitor->link.online) {
            (void)snprintf(net_text, sizeof(net_text), "online");
        } else {
            (void)snprintf(net_text, sizeof(net_text),
                           "offline - missed %ld",
                           (long)decision->missed);
        }
        show_numbers(screen, monitor, net_text);
    }
}

/* The beacon on the site and its picture on the screen. */
static bento_status_t set_beacon(const tilt_alarm_t *monitor, bool on)
{
    const alarm_screen_t *screen = &monitor->screen;
    const bento_status_t status =
        on ? bento_gpio_led_on(monitor->beacon_led)
           : bento_gpio_led_off(monitor->beacon_led);

    if (status == BENTO_OK) {
        set_value(screen->ui, screen->beacon_lamp, on ? 1 : 0);
        set_text(screen->ui, screen->beacon_state,
                 on ? "ไฟติดอยู่" : "ไฟดับอยู่");
    }
    return status;
}

/* round(value, 1) as py/modbuiltins.c computes it, then repr(). */
static void round_text(bento_real_t value, char *text)
{
    const bento_real_t mult = (bento_real_t)DECIMAL_BASE;
    const bento_real_t scaled = value * mult;
    bento_real_t rounded;

#if BENTO_FMT_REAL_DOUBLE
    rounded = nearbyint(scaled) / mult;
#else
    rounded = nearbyintf(scaled) / mult;
#endif
    real_text(rounded, -1, text);
}

/* Whether the broker still holds the link, as the binding answers. */
static bool broker_connected(const link_t *link)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;
    const bool answered =
        link->open &&
        (bento_mqtt_state(link->handle, &state) == BENTO_OK);

    return answered && (state == BENTO_MQTT_STATE_CONNECTED);
}

/* send(): *sent false instead of an error, so a dead link never takes
 * the screen down. An OSError from publish() ends the program. */
static bento_status_t send_message(tilt_alarm_t *monitor,
                                   const char *kind, bool *sent)
{
    static const char topic[] = "bento/team01/telemetry";
    static const char *const level_names[LEVEL_COUNT] = {"OK", "WARN",
                                                         "ALERT"};
    const decision_t *decision = &monitor->decision;
    char number[NUMBER_BYTES];
    char payload[PAYLOAD_BYTES];
    bento_status_t status = BENTO_OK;

    *sent = broker_connected(&monitor->link);
    if (*sent) {
        round_text(decision->value, number);
        (void)snprintf(payload, sizeof(payload),
                       "{\"state\": \"%s\", \"v\": %s, \"kind\": "
                       "\"%s\", \"t\": %lu, "
                       "\"id\": \"%s\", \"unit\": \"%s\"}",
                       level_names[decision->state], number, kind,
                       (unsigned long)bento_time_ticks_ms(), DEVICE_ID,
                       UNIT);
        status = bento_mqtt_publish(monitor->link.handle, topic,
                                    (uint32_t)strlen(topic),
                                    (const uint8_t *)payload,
                                    (uint32_t)strlen(payload), 0U, 0U);
        if (status != BENTO_E_SESSION) {
            status =
                BENTO_OK; /* the binding's False, ignored by send() */
        }
    }
    return status;
}

/* send_message(), counting what got out. */
static bento_status_t send_counted(tilt_alarm_t *monitor,
                                   const char *kind)
{
    bool sent = false;
    const bento_status_t status = send_message(monitor, kind, &sent);

    if (sent) {
        monitor->decision.sent++;
    }
    return status;
}

/* send_message(), counting what got out and what did not. */
static bento_status_t send_or_miss(tilt_alarm_t *monitor,
                                   const char *kind)
{
    bool sent = false;
    const bento_status_t status = send_message(monitor, kind, &sent);

    if (sent) {
        monitor->decision.sent++;
    } else {
        monitor->decision.missed++;
    }
    return status;
}

/* Polls a started connect until it answers; Stop cancels it. */
static bento_status_t finish_join(bool *joined)
{
    bento_status_t answer = BENTO_E_IN_PROGRESS;
    bento_status_t status = BENTO_OK;

    while ((status == BENTO_OK) && (answer == BENTO_E_IN_PROGRESS)) {
        answer = bento_wifi_connect_poll();
        if (answer == BENTO_E_IN_PROGRESS) {
            status = wait_ms(NET_STEP_MS);
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* Stop: give up the join */
    }
    *joined = (status == BENTO_OK) && (answer == BENTO_OK);
    return status;
}

/* wifi.connect(): true when joined. A board without Wi-Fi ends the
 * program. */
static bento_status_t join_wifi(bool *joined)
{
    static const char wifi_ssid[] = "AIoT-Class";
    static const char wifi_pass[] = "aiot12345";
    bento_wifi_connect_params_t params;
    bool connected = false;
    bento_status_t status = bento_wifi_is_connected(&connected);

    *joined = (status == BENTO_OK) && connected;
    if (!*joined && (status != BENTO_E_UNSUPPORTED)) {
        bento_wifi_connect_params_init(&params);
        params.ssid = wifi_ssid;
        params.ssid_len = (uint32_t)strlen(wifi_ssid);
        params.pass = wifi_pass;
        params.pass_len = (uint32_t)strlen(wifi_pass);
        params.flags =
            BENTO_WIFI_F_SAVE_CREDS; /* as the binding does */
        status = bento_wifi_connect_start(&params);
        if (status == BENTO_OK) {
            status = finish_join(joined);
        }
    }
    return ((status == BENTO_E_CANCELLED) ||
            (status == BENTO_E_UNSUPPORTED))
               ? status
               : BENTO_OK;
}

/* mqtt.connect(BROKER, port=1883, client_id=CLIENT_ID). */
static bento_status_t connect_broker(link_t *link, bool *connected)
{
    static const char broker[] = "test.mosquitto.org";
    bento_mqtt_config_t config;
    bento_status_t answer = BENTO_E_IN_PROGRESS;
    bento_status_t status;

    if (link->open) { /* two clients at most: the old one goes first */
        (void)bento_mqtt_close(link->handle); /* answers None */
        link->open = false;
    }
    bento_mqtt_config_init(&config);
    config.host = broker;
    config.host_len = (uint32_t)strlen(broker);
    config.port = MQTT_PORT;
    config.client_id = link->client_id;
    config.client_id_len = (uint32_t)strlen(link->client_id);
    status = bento_mqtt_open_start(&config, &link->handle);
    link->open = status == BENTO_OK;
    link->no_broker = status == BENTO_E_UNSUPPORTED;
    while ((status == BENTO_OK) && (answer == BENTO_E_IN_PROGRESS)) {
        answer = bento_mqtt_open_poll(link->handle);
        if (answer == BENTO_E_IN_PROGRESS) {
            status = wait_ms(NET_STEP_MS);
        }
    }
    *connected = (status == BENTO_OK) && (answer == BENTO_OK);
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* go_online(): the Wi-Fi first, then the broker. */
static bento_status_t go_online(link_t *link)
{
    bool joined = false;
    bento_status_t status = join_wifi(&joined);

    link->online = false;
    if ((status == BENTO_OK) && joined) {
        status = connect_broker(link, &link->online);
    }
    return status;
}

static void show_link(const alarm_screen_t *screen, bool connected)
{
    set_value(screen->ui, screen->mqtt_lamp, connected ? 1 : 0);
    set_text(screen->ui, screen->mqtt_label,
             connected ? "MQTT: เชื่อมต่อแล้ว" : "MQTT: ออฟไลน์");
}

/* A new level must repeat CONFIRM_N rounds before it is believed. */
static bool confirm_level(decision_t *decision, int32_t level)
{
    if (level == decision->pending) {
        decision->streak++;
    } else {
        decision->pending = level;
        decision->streak = 1;
    }
    return (decision->streak >= CONFIRM_N) &&
           (decision->pending != decision->state);
}

/* The alarm event, at most every ALERT_GAP_MS. */
static bento_status_t send_alert(tilt_alarm_t *monitor, uint32_t now_ms)
{
    decision_t *decision = &monitor->decision;
    bool sent = false;
    const bento_status_t status = send_message(monitor, "event", &sent);

    if (sent) {
        decision->has_alert = true;
        decision->last_alert_ms = now_ms;
        decision->sent++;
    } else {
        decision
            ->missed++; /* ตั้งใจทิ้ง ไม่เก็บย้อนหลัง: ค่าที่ค้างนานไม่มีประโยชน์ */
    }
    return status;
}

/* An alarm latches the beacon and is sent, at most every
 * ALERT_GAP_MS. A missed event is dropped on purpose: a value held
 * back for long is no use. Back to normal is sent only when nobody
 * still has to acknowledge. */
static bento_status_t enter_state(tilt_alarm_t *monitor,
                                  uint32_t now_ms)
{
    decision_t *decision = &monitor->decision;
    bento_status_t status = BENTO_OK;

    decision->state = decision->pending;
    if (decision->state == LEVEL_ALERT) {
        decision->latched = true;
        status = set_beacon(monitor, true);
        if ((status == BENTO_OK) &&
            (!decision->has_alert ||
             (bento_time_ticks_diff(now_ms, decision->last_alert_ms) >=
              ALERT_GAP_MS))) {
            status = send_alert(monitor, now_ms);
        }
    } else if ((decision->state == LEVEL_OK) && !decision->latched) {
        status = send_counted(monitor, "clear");
    } else {
        /* WARN, or OK while still latched: nothing to send */
    }
    return status;
}

/* A dropped link is noticed at once and retried every RETRY_MS; the
 * screen never waits for it. */
static bento_status_t keep_link(tilt_alarm_t *monitor, uint32_t now_ms)
{
    link_t *link = &monitor->link;
    bento_status_t status = BENTO_OK;

    if (link->online && !broker_connected(link)) {
        link->online = false;
        show_link(&monitor->screen, false);
        monitor->decision.retry_ms = now_ms;
    }
    if (!link->online &&
        (bento_time_ticks_diff(now_ms, monitor->decision.retry_ms) >=
         RETRY_MS)) {
        status = go_online(link);
        if (status == BENTO_OK) {
            show_link(&monitor->screen, link->online);
            monitor->decision.retry_ms = now_ms;
        }
        if ((status == BENTO_OK) && link->online) {
            status = send_counted(monitor, "back");
        }
    }
    return status;
}

static void show_box(const alarm_screen_t *screen, bool shown)
{
    set_shown(screen->ui, screen->box, shown);
    set_shown(screen->ui, screen->yes_button, shown);
    set_shown(screen->ui, screen->no_button, shown);
}

/* One click: turning the beacon off goes through the box first. */
static bento_status_t on_click(tilt_alarm_t *monitor,
                               bento_ui_id_t widget)
{
    const alarm_screen_t *screen = &monitor->screen;
    decision_t *decision = &monitor->decision;
    bento_status_t status = BENTO_OK;

    if (widget == screen->on_button) {
        status = set_beacon(monitor, true);
    } else if ((widget == screen->off_button) && !decision->asking) {
        decision->asking = true;
        show_box(screen, true);
    } else if ((widget == screen->yes_button) && decision->asking) {
        decision->asking = false;
        status = set_beacon(monitor, false);
        show_box(screen, false);
    } else if ((widget == screen->no_button) && decision->asking) {
        decision->asking = false;
        show_box(screen, false);
    } else if ((widget == screen->acknowledge_button) &&
               decision->latched) {
        decision->latched = false;
        status = send_counted(monitor, "ack");
    } else {
        /* another widget, or a button that is not live now */
    }
    return status;
}

/* Polled every round, or the screen hides its widgets. */
static bento_status_t read_buttons(tilt_alarm_t *monitor)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    if (bento_ui_poll(monitor->screen.ui, events,
                      BENTO_UI_POLL_EVENTS_MAX, &count,
                      NULL) != BENTO_OK) {
        count = 0U; /* a failed poll answers no events */
    }
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        if (events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) {
            status = on_click(monitor, events[i].id);
        }
    }
    return status;
}

/* One round: sense, decide, keep the link, the heartbeat, the
 * buttons, show, rest. */
static bento_status_t alarm_round(tilt_alarm_t *monitor)
{
    decision_t *decision = &monitor->decision;
    const uint32_t now_ms = bento_time_ticks_ms();
    bento_status_t status =
        read_value(&monitor->tilt, &decision->value);

    if ((status == BENTO_OK) &&
        confirm_level(decision, level_of(decision->value))) {
        status = enter_state(monitor, now_ms);
    }
    if (status == BENTO_OK) {
        status = keep_link(monitor, now_ms);
    }
    if ((status == BENTO_OK) &&
        (bento_time_ticks_diff(now_ms, decision->beat_ms) >=
         HEARTBEAT_MS)) {
        decision->beat_ms = now_ms;
        status = send_or_miss(monitor, "beat");
    }
    if (status == BENTO_OK) {
        status = read_buttons(monitor);
    }
    if (status == BENTO_OK) {
        show(monitor, now_ms);
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* The BENTO Emulator has no MQTT from C yet: say so on the screen
 * and in the Console, then carry on offline. */
static bento_status_t add_emulator_notice(const alarm_screen_t *screen)
{
    /* The line breaks are in the text: the Emulator does not wrap a
     * label at a width, so all of it stays on the screen. */
    static const widget_spec_t notice = {
        KIND_LABEL,
        "MQTT จาก C ยังไม่มีใน BENTO Emulator\nให้รันบน Dev Kit หรือ Eva Kit",
        232,
        0,
        -1,
        -1,
        COLOR_WARN,
        20,
        0,
        0};
    bento_ui_id_t unused = 0U;

    (void)printf("MQTT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ "
                 "Eva Kit / MQTT from C is not in the BENTO Emulator "
                 "yet: run it on a Dev Kit or Eva Kit.\n");
    return add_widget(screen->ui, &notice, &unused);
}

/* The try block: online, then the rounds until Stop. The finally
 * always says goodbye to the broker, so the name is not left held. */
static bento_status_t run_alarm(tilt_alarm_t *monitor)
{
    bento_status_t status = go_online(&monitor->link);

    if ((status == BENTO_OK) && monitor->link.no_broker) {
        status = add_emulator_notice(&monitor->screen);
    }
    if (status == BENTO_OK) {
        show_link(&monitor->screen, monitor->link.online);
        monitor->decision.beat_ms = bento_time_ticks_ms();
        monitor->decision.retry_ms = monitor->decision.beat_ms;
    }
    while (status == BENTO_OK) {
        status = alarm_round(monitor);
    }
    if (monitor->link.open) {
        (void)bento_mqtt_close(monitor->link.handle); /* answers None */
    }
    show_link(&monitor->screen, false);
    return status;
}

/* Sense: on the Eva Kit sensors.init() must not be called; a first
 * read thrown away covers its up to 16 s first wait after a reset. */
static bento_status_t start_sensing(tilt_t *tilt)
{
    bento_sensors_imu6_t motion;
    const float smoothing = 0.2F; /* the original filter strength */
    bento_dsp_ema_config_t config;
    bento_status_t status;

    bento_sensors_imu6_init(&motion);
    status = bento_sensors_bmi270_motion(&motion);
    if ((status != BENTO_OK) && (status != BENTO_E_UNSUPPORTED)) {
        (void)printf("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ตอนวัดท่าตั้งต้น\n");
        status = BENTO_OK;
    }
    if (status == BENTO_OK) {
        status = bento_dsp_ema_config_init(&config);
    }
    if (status == BENTO_OK) {
        config.alpha = smoothing;
        status = bento_dsp_ema_state_init(&tilt->smooth, &config);
    }
    return status;
}

int main(void)
{
    tilt_alarm_t monitor;
    bento_status_t status;

    (void)memset(&monitor, 0, sizeof(monitor));
    monitor.screen.ui = bento_runtime_get_ui();
    /* a new suffix each run: a quick rerun never meets its old id */
    (void)snprintf(monitor.link.client_id,
                   sizeof(monitor.link.client_id), "%s-%04x", DEVICE_ID,
                   (unsigned int)(bento_time_ticks_ms() &
                                  (uint32_t)CLIENT_SUFFIX_MASK));
    monitor.decision.state = LEVEL_OK;
    monitor.decision.pending = LEVEL_OK;
    monitor.shown.last_second = -1;
    status = find_beacon(&monitor.beacon_led);
    if (status == BENTO_OK) {
        status = start_sensing(&monitor.tilt);
    }
    if (status == BENTO_OK) {
        status = measure_baseline(&monitor.tilt);
    }
    if (status == BENTO_OK) {
        status = build_screen(&monitor.screen);
    }
    if (status == BENTO_OK) {
        status = set_beacon(&monitor, false);
    }
    if (status == BENTO_OK) {
        status = run_alarm(&monitor);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
