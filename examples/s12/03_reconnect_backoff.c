/*
 * 03_reconnect_backoff.c -- ต่อใหม่แบบถอยห่าง
 * (reconnect with backoff)
 *
 * ต่อ Wi-Fi และ MQTT broker ของห้องเรียน ถ้าหลุดจะลองใหม่โดยเพิ่ม
 * ระยะห่างเป็นเท่าตัวทุกครั้งที่ไม่สำเร็จ (2000 4000 8000 ... ms)
 * จนถึงเพดานหนึ่งนาที แล้วรีเซ็ตกลับค่าเริ่มต้นทันทีที่ต่อกลับได้
 * ป้ายบนซ้ายบอกสถานะสาย Seg7 บอกระยะห่างครั้งถัดไปและจำนวนครั้ง
 * แถบนับถอยหลังถึงเวลานัด กราฟคือตารางนัดหมายทั้งชุด ปิดเราเตอร์
 * หรือ broker สักพักเพื่อดูการถอยห่าง
 *
 * Joins the classroom Wi-Fi and MQTT broker. When the link drops it
 * retries, doubling the wait after each failure (2000, 4000, 8000 ...
 * ms) up to a one-minute ceiling, and goes back to the first wait as
 * soon as it is online again. The card shows the link, the next wait
 * and the attempts; a bar counts down to the next try and a chart
 * shows the whole schedule. Turn the router or the broker off for a
 * while to watch the backoff.
 *
 * Twin of 03_reconnect_backoff.py
 * (embedded-systems-for-aiot-developer/examples/s12/03_reconnect_backoff.py),
 * merged from the library twin aiot-s12-03-reconnect-backoff. Style: CLEAN_C.md.
 *
 * Differs from the original, where C or the C API forces it:
 *  - wifi.connect() and mqtt.connect() block in MicroPython; here
 *    they are started and polled every 50 ms, and a Stop cancels the
 *    wait. The binding's own console lines ("[WiFi] ...",
 *    "[MQTT] ...") are not printed: the C libraries log nothing
 *    (bento_wifi/README.md). The credentials are saved on a good
 *    join, as the binding does.
 *  - the original's finally (mqtt.disconnect(), the link shown
 *    offline) runs here after the loop, whatever ended it.
 *  - int(left * 100 / backoff) is integer division here: both are
 *    whole milliseconds, the quotient at most 100, so the float
 *    division never rounds across a whole number.
 *  - on the reTerminal there is no wifi or mqtt module (ImportError
 *    at the start); here the screen is drawn and the first Wi-Fi call
 *    ends the program with BENTO_E_UNSUPPORTED, after the finally.
 *  - In the BENTO Emulator, MQTT from C is not available yet: the program
 *    says so and goes on offline, backing off as the original does when
 *    the broker cannot be reached.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_mqtt.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
static const char WIFI_SSID[] = "AIoT-Class";
static const char WIFI_PASS[] = "<รหัสผ่านของห้องเรียน>";
static const char BROKER[] = "192.168.1.50";
static const char DEVICE_ID[] = "team03";
/* ---- end of settings ---- */

/* Times, in milliseconds, and the backoff. */
enum {
    SETTLE_MS = 200,   /* let the new screen settle */
    WAIT_STEP_MS = 50, /* how often a join is polled */
    LOOP_MS = 200,
    BACKOFF_START_MS = 2000, /* the first wait */
    BACKOFF_MAX_MS = 60000,  /* the ceiling: one minute */
    SCHEDULE_N = 7,          /* tries drawn, each 7 points wide */
    MS_PER_S = 1000,
    FULL_PERCENT = 100,
    MQTT_PORT = 1883,
    CLIENT_ID_CHARS = 24,        /* "team03-" and four hex digits */
    CLIENT_SUFFIX_MASK = 0xFFFF, /* the low 16 bits of the clock */
    TEXT_CHARS = 160
};

/* The widgets the program changes; ROLE_FIXED holds the rest. */
enum {
    ROLE_FIXED,
    ROLE_LED,
    ROLE_BROKER_TEXT,
    ROLE_LINK,
    ROLE_BACKOFF,
    ROLE_TRIES,
    ROLE_WAIT_BAR,
    ROLE_CHART,
    ROLE_FOOT_LEFT,
    ROLE_FOOT_RIGHT,
    ROLE_COUNT
};

typedef struct {
    int32_t kind;
    int32_t role;
    const char *text; /* NULL: the widget has none */
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t min_value;
    int32_t max_value;
    int32_t value; /* value=: a start value or a font size */
} widget_spec_t;

/* The broker link: the binding keeps one client and drops the old one
 * on each connect. */
typedef struct {
    bool open;
    uint32_t handle;
} broker_link_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t widgets[ROLE_COUNT];
    uint8_t series; /* the schedule's line on the chart */
    char client_id[CLIENT_ID_CHARS];
    broker_link_t link;
    bool emulator_told; /* the Emulator note is shown once */
} backoff_screen_t;

/* What the loop keeps between rounds. */
typedef struct {
    bool online;
    int32_t backoff_ms;
    int32_t attempts;
    uint32_t next_try_ms;
} retry_t;

/* Layout sizes and fonts. */
enum {
    AUTO_PX = -1,
    TITLE_PX = 28,
    TEXT_PX = 20, /* Thai below 20 px loses its strokes */
    STATE_PX = 28,
    CARD_EDGE_PX = 12, /* max= of the card panel */
    CARD_BORDER = 1,   /* value= of the card panel */
    CHART_TOP_S = (BACKOFF_MAX_MS / MS_PER_S) + 10
};

/* Colours, 0xRRGGBB: the course palette, one role each. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

enum {
    WIDGET_LABEL,
    WIDGET_LED,
    WIDGET_PANEL,
    WIDGET_SEG7,
    WIDGET_BAR,
    WIDGET_CHART,
    WIDGET_KIND_COUNT
};

typedef bento_ui_status_t (*widget_factory_t)(
    bento_ui_t *ui, const bento_ui_widget_args_t *args,
    bento_ui_id_t *out_id);

/* ---- Small helpers ---- */

static bento_status_t stop_raised_in(bento_status_t status)
{
    const bool stopped = !bento_runtime_is_running();

    return ((status == BENTO_OK) && stopped) ? BENTO_E_CANCELLED
                                             : status;
}

static bento_status_t wait_step(void)
{
    return stop_raised_in(bento_time_sleep_ms(WAIT_STEP_MS));
}

/* The widget setters: MicroPython's ignore a refusal (modui.c). */
static void show_text(const backoff_screen_t *screen, int32_t role,
                      const char *text)
{
    (void)bento_ui_set_text(screen->ui, screen->widgets[role], text,
                            (uint32_t)strlen(text));
}

static void show_number(const backoff_screen_t *screen, int32_t role,
                        int32_t number)
{
    char text[TEXT_CHARS];

    (void)snprintf(text, sizeof(text), "%d", (int)number);
    show_text(screen, role, text);
}

static void show_color(const backoff_screen_t *screen, int32_t role,
                       uint32_t color)
{
    (void)bento_ui_set_color(screen->ui, screen->widgets[role], color);
}

static void show_value(const backoff_screen_t *screen, int32_t role,
                       int32_t value)
{
    (void)bento_ui_set_value(screen->ui, screen->widgets[role], value);
}

/* lcd.print() ignores a refused message (modlcd.c). */
static void lcd_line(const char *text)
{
    (void)bento_lcd_print(text);
}

/* ui.poll(), whose events the original does not read. */
static bento_status_t drop_events(const backoff_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* a failed poll answers no events, which are not read anyway */
    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
    return stop_raised_in(BENTO_OK);
}

/* ---- The screen ---- */

static bento_status_t add_widgets(backoff_screen_t *screen,
                                  const widget_spec_t *specs,
                                  size_t count)
{
    static const widget_factory_t factories[WIDGET_KIND_COUNT] = {
        bento_ui_label_create, bento_ui_led_create,
        bento_ui_panel_create, bento_ui_seg7_create,
        bento_ui_bar_create,   bento_ui_chart_create};
    bento_ui_widget_args_t args;
    size_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        const widget_spec_t *spec = &specs[i];

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
        args.min_val = spec->min_value;
        args.max_val = spec->max_value;
        args.font_px = spec->value;
        status = stop_raised_in(factories[spec->kind](
            screen->ui, &args, &screen->widgets[spec->role]));
    }
    return status;
}

/* The title, the MQTT lamp and the link card. */
static bento_status_t build_card(backoff_screen_t *screen)
{
    static const widget_spec_t layout[] = {
        {WIDGET_LABEL, ROLE_FIXED, "คาบ 12 - ถอยห่างแล้วรีเซ็ต", 24, 8,
         AUTO_PX, AUTO_PX, COLOR_TEXT, 0, 0, TITLE_PX},
        {WIDGET_LED, ROLE_LED, NULL, 606, 12, 18, 18, COLOR_OK, 0, 0,
         0},
        {WIDGET_LABEL, ROLE_BROKER_TEXT, "MQTT: ออฟไลน์", 628, 8,
         AUTO_PX, AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_PANEL, ROLE_FIXED, NULL, 24, 56, 744, 168, COLOR_CARD,
         COLOR_CARD, CARD_EDGE_PX, CARD_BORDER},
        {WIDGET_LABEL, ROLE_FIXED, "สายตอนนี้", 40, 72, AUTO_PX, AUTO_PX,
         COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_LABEL, ROLE_LINK, "กำลังต่อ", 40, 104, AUTO_PX, AUTO_PX,
         COLOR_WARN, 0, 0, STATE_PX},
        {WIDGET_LABEL, ROLE_FIXED, "ครั้งถัดไปรออีก (ms)", 264, 72, AUTO_PX,
         AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_SEG7, ROLE_BACKOFF, "2000", 264, 104, 192, 56,
         COLOR_ACCENT, 0, 0, 0},
        {WIDGET_LABEL, ROLE_FIXED, "ลองมาแล้ว (ครั้ง)", 528, 72, AUTO_PX,
         AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_SEG7, ROLE_TRIES, "0", 528, 104, 120, 56, COLOR_TEXT, 0,
         0, 0},
        {WIDGET_LABEL, ROLE_FIXED, "นับถอยหลังถึงเวลานัด", 40, 172, AUTO_PX,
         AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_BAR, ROLE_WAIT_BAR, NULL, 288, 168, 456, 32, 0U, 0,
         FULL_PERCENT, 0}};

    return add_widgets(screen, layout,
                       sizeof(layout) / sizeof(layout[0]));
}

/* The schedule chart, its line, and the notes under it. */
static bento_status_t build_chart(backoff_screen_t *screen)
{
    static const widget_spec_t chart = {
        WIDGET_CHART, ROLE_CHART, NULL, 24,          240, 744,
        88,           COLOR_CARD, 0,    CHART_TOP_S, 0};
    static const widget_spec_t notes[] = {
        {WIDGET_LABEL, ROLE_FOOT_LEFT, "กราฟคือสิ่งที่จะเกิดตอนสายหลุด", 24,
         336, AUTO_PX, AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_LABEL, ROLE_FOOT_RIGHT, "ไม่ใช่สิ่งที่กำลังเกิด", 384, 336,
         AUTO_PX, AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX},
        {WIDGET_LABEL, ROLE_FIXED, "กราฟ = ระยะห่างที่นัดไว้ (วินาที)", 24,
         368, AUTO_PX, AUTO_PX, COLOR_DIM, 0, 0, TEXT_PX}};
    bento_status_t status = add_widgets(screen, &chart, 1U);

    if (status == BENTO_OK) {
        status = stop_raised_in(bento_ui_chart_add_series(
            screen->ui, screen->widgets[ROLE_CHART], COLOR_ACCENT,
            &screen->series));
    }
    if (status == BENTO_OK) {
        status = add_widgets(screen, notes,
                             sizeof(notes) / sizeof(notes[0]));
    }
    return status;
}

static bento_status_t build_screen(backoff_screen_t *screen)
{
    bento_status_t status =
        stop_raised_in(bento_ui_screen_default(screen->ui));

    if (status == BENTO_OK) {
        status = stop_raised_in(bento_time_sleep_ms(SETTLE_MS));
    }
    if (status == BENTO_OK) {
        status = build_card(screen);
    }
    if (status == BENTO_OK) {
        status = build_chart(screen);
    }
    if (status == BENTO_OK) {
        status = drop_events(screen);
    }
    return status;
}

/* The whole schedule on the console and as steps on the chart, each
 * try drawn SCHEDULE_N points wide so the steps can be read. */
static void draw_schedule(const backoff_screen_t *screen)
{
    char line[TEXT_CHARS];
    int32_t wait_ms = BACKOFF_START_MS;
    int32_t total_ms = 0;
    int32_t attempt;
    int32_t point;

    for (attempt = 1; attempt <= SCHEDULE_N; attempt++) {
        total_ms += wait_ms;
        (void)snprintf(line, sizeof(line),
                       "ครั้งที่ %d รอ %d ms (สะสม %d "
                       "วินาที)",
                       (int)attempt, (int)wait_ms,
                       (int)(total_ms / MS_PER_S));
        lcd_line(line);
        for (point = 0; point < SCHEDULE_N; point++) {
            /* set_next() sends without an answer (modui.c) */
            (void)bento_ui_chart_set_next(
                screen->ui, screen->widgets[ROLE_CHART], screen->series,
                wait_ms / MS_PER_S);
        }
        wait_ms = ((wait_ms * 2) < BACKOFF_MAX_MS) ? (wait_ms * 2)
                                                   : BACKOFF_MAX_MS;
    }
}

/* The link label, its colour and the MQTT lamp change only here, so
 * they never disagree. */
static void show_link(const backoff_screen_t *screen, bool online)
{
    if (online) {
        show_text(screen, ROLE_LINK, "online");
        show_color(screen, ROLE_LINK, COLOR_OK);
        show_value(screen, ROLE_LED, 1);
        show_text(screen, ROLE_BROKER_TEXT, "MQTT: เชื่อมต่อแล้ว");
    } else {
        show_text(screen, ROLE_LINK, "offline");
        show_color(screen, ROLE_LINK, COLOR_BAD);
        show_value(screen, ROLE_LED, 0);
        show_text(screen, ROLE_BROKER_TEXT, "MQTT: ออฟไลน์");
    }
}

/* The countdown bar: empty while online, else the share of the wait
 * already passed. */
static void show_countdown(const backoff_screen_t *screen,
                           const retry_t *retry, uint32_t now_ms)
{
    int32_t left_ms;

    if (retry->online) {
        show_value(screen, ROLE_WAIT_BAR, 0);
    } else {
        left_ms = bento_time_ticks_diff(retry->next_try_ms, now_ms);
        left_ms = (left_ms > 0) ? left_ms : 0;
        show_value(screen, ROLE_WAIT_BAR,
                   FULL_PERCENT -
                       ((left_ms * FULL_PERCENT) / retry->backoff_ms));
    }
}

/* ---- The link ---- */

/* wifi.connect(): true when joined. A board without Wi-Fi ends the
 * program. */
static bento_status_t join_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
    bento_status_t answer = BENTO_E_IN_PROGRESS;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = WIFI_PASS;
    params.pass_len = (uint32_t)strlen(WIFI_PASS);
    params.flags = BENTO_WIFI_F_SAVE_CREDS; /* as the binding does */
    status = bento_wifi_connect_start(&params);
    while ((status == BENTO_OK) && (answer == BENTO_E_IN_PROGRESS)) {
        answer = bento_wifi_connect_poll();
        if (answer == BENTO_E_IN_PROGRESS) {
            status = wait_step();
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_connect_cancel(); /* Stop: give up */
    }
    *joined = (status == BENTO_OK) && (answer == BENTO_OK);
    return ((status == BENTO_E_CANCELLED) ||
            (status == BENTO_E_UNSUPPORTED))
               ? status
               : BENTO_OK;
}

/* The BENTO Emulator's C runtime has no MQTT yet: bento_mqtt_open_start
 * answers BENTO_E_UNSUPPORTED there. The first time, the footer line says
 * so and the console too; after that it is a broker that cannot be
 * reached, and the backoff goes on. */
static void show_emulator_note(backoff_screen_t *screen)
{
    static const char note[] =
        "MQTT จาก C ยังไม่มีใน BENTO Emulator\nให้รันบน Dev Kit หรือ Eva Kit";

    if (!screen->emulator_told) {
        screen->emulator_told = true;
        show_text(screen, ROLE_FOOT_RIGHT, "");
        show_color(screen, ROLE_FOOT_LEFT, (uint32_t)COLOR_WARN);
        show_text(screen, ROLE_FOOT_LEFT, note);
        (void)printf("MQTT จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit "
                     "หรือ Eva Kit / MQTT from C is not in the BENTO "
                     "Emulator yet: run it on a Dev Kit or Eva Kit.\n");
    }
}

/* mqtt.connect(BROKER, port=1883, client_id=CLIENT_ID): the old
 * client goes first, as the binding's cleanup does. */
static bento_status_t connect_broker(backoff_screen_t *screen,
                                     bool *connected)
{
    broker_link_t *link = &screen->link;
    bento_mqtt_config_t config;
    bento_status_t answer = BENTO_E_IN_PROGRESS;
    bento_status_t status;

    if (link->open) {
        (void)bento_mqtt_close(link->handle); /* already dropped */
        link->open = false;
    }
    bento_mqtt_config_init(&config);
    config.host = BROKER;
    config.host_len = (uint32_t)strlen(BROKER);
    config.port = MQTT_PORT;
    config.client_id = screen->client_id;
    config.client_id_len = (uint32_t)strlen(screen->client_id);
    status = bento_mqtt_open_start(&config, &link->handle);
    link->open = status == BENTO_OK;
    if (status == BENTO_E_UNSUPPORTED) {
        show_emulator_note(screen);
    }
    while ((status == BENTO_OK) && (answer == BENTO_E_IN_PROGRESS)) {
        answer = bento_mqtt_open_poll(link->handle);
        if (answer == BENTO_E_IN_PROGRESS) {
            status = wait_step();
        }
    }
    *connected = (status == BENTO_OK) && (answer == BENTO_OK);
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* Wi-Fi first (without an address MQTT has nothing to reach), then
 * the broker. */
static bento_status_t go_online(backoff_screen_t *screen, bool *online)
{
    bool joined = false;
    bento_status_t status = bento_wifi_is_connected(&joined);

    *online = false;
    if ((status == BENTO_OK) && !joined) {
        status = join_wifi(&joined);
    }
    if ((status == BENTO_OK) && joined) {
        status = connect_broker(screen, online);
    }
    return status;
}

/* Whether the broker still holds the link, as the binding answers. */
static bool broker_connected(const backoff_screen_t *screen)
{
    uint32_t state = BENTO_MQTT_STATE_CLOSED;
    const bool answered =
        screen->link.open &&
        (bento_mqtt_state(screen->link.handle, &state) == BENTO_OK);

    return answered && (state == BENTO_MQTT_STATE_CONNECTED);
}

/* ---- The retry steps the loop runs ---- */

/* A link that was good is no proof of now: ask the broker. */
static void notice_drop(const backoff_screen_t *screen, retry_t *retry,
                        uint32_t now_ms)
{
    char line[TEXT_CHARS];

    if (retry->online && !broker_connected(screen)) {
        retry->online = false;
        /* ticks_add, not +: the tick counter wraps */
        retry->next_try_ms =
            bento_time_ticks_add(now_ms, retry->backoff_ms);
        show_link(screen, false);
        show_number(screen, ROLE_BACKOFF, retry->backoff_ms);
        (void)snprintf(line, sizeof(line),
                       "<span class=error>หลุด จะลองใหม่ในอีก %d ms"
                       "</span>",
                       (int)retry->backoff_ms);
        lcd_line(line);
    }
}

/* Back online: the wait goes back to its start at once, or the next
 * drop would begin at the ceiling. */
static void reset_backoff(const backoff_screen_t *screen,
                          retry_t *retry)
{
    char line[TEXT_CHARS];

    (void)snprintf(line, sizeof(line),
                   "<span class=ok>กลับมาที่ครั้งที่ %d รีเซ็ต %d"
                   "</span>",
                   (int)retry->attempts, BACKOFF_START_MS);
    lcd_line(line);
    retry->backoff_ms = BACKOFF_START_MS;
    retry->attempts = 0;
    show_text(screen, ROLE_TRIES, "0");
    show_number(screen, ROLE_BACKOFF, retry->backoff_ms);
}

/* Still offline: double the wait, never past the ceiling. */
static void double_backoff(const backoff_screen_t *screen,
                           retry_t *retry)
{
    char line[TEXT_CHARS];
    const int32_t doubled = retry->backoff_ms * 2;

    retry->backoff_ms =
        (doubled < BACKOFF_MAX_MS) ? doubled : BACKOFF_MAX_MS;
    retry->next_try_ms =
        bento_time_ticks_add(bento_time_ticks_ms(), retry->backoff_ms);
    show_number(screen, ROLE_BACKOFF, retry->backoff_ms);
    (void)snprintf(line, sizeof(line),
                   "<span class=warn>ยังไม่ได้ ครั้งที่ %d รออีก %d ms"
                   "</span>",
                   (int)retry->attempts, (int)retry->backoff_ms);
    lcd_line(line);
}

/* The try's answer: the link on screen, then the wait reset or doubled. */
static void follow_try(const backoff_screen_t *screen, retry_t *retry)
{
    show_link(screen, retry->online);
    if (retry->online) {
        reset_backoff(screen, retry);
    } else {
        double_backoff(screen, retry);
    }
}

/* When the time has come, one more try. */
static bento_status_t retry_when_due(backoff_screen_t *screen,
                                     retry_t *retry, uint32_t now_ms)
{
    bento_status_t status = BENTO_OK;

    if (!retry->online &&
        (bento_time_ticks_diff(now_ms, retry->next_try_ms) >= 0)) {
        retry->attempts++;
        show_number(screen, ROLE_TRIES, retry->attempts);
        status = go_online(screen, &retry->online);
        if (status == BENTO_OK) {
            follow_try(screen, retry);
        }
    }
    return status;
}

static bento_status_t run_loop(backoff_screen_t *screen, retry_t *retry)
{
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        const uint32_t now_ms = bento_time_ticks_ms();

        notice_drop(screen, retry, now_ms);
        status = retry_when_due(screen, retry, now_ms);
        if (status == BENTO_OK) {
            show_countdown(screen, retry, now_ms);
            status = drop_events(screen);
        }
        if (status == BENTO_OK) {
            status = stop_raised_in(bento_time_sleep_ms(LOOP_MS));
        }
    }
    return status;
}

/* The try block: the first connect, then the loop forever. */
static bento_status_t stay_online(backoff_screen_t *screen)
{
    char line[TEXT_CHARS];
    retry_t retry = {false, BACKOFF_START_MS, 0, 0U};
    bento_status_t status = go_online(screen, &retry.online);

    if (status == BENTO_OK) {
        retry.next_try_ms = bento_time_ticks_ms();
        show_link(screen, retry.online);
        (void)snprintf(line, sizeof(line), "<b>สถานะเริ่มต้น</b> %s",
                       retry.online ? "online" : "offline");
        lcd_line(line);
        status = run_loop(screen, &retry);
    }
    return status;
}

/* The finally: leave the broker, so the name is not held for a
 * minute, and show the lamp off. */
static void say_goodbye(const backoff_screen_t *screen)
{
    if (screen->link.open) {
        (void)bento_mqtt_close(screen->link.handle); /* answers None */
    }
    show_link(screen, false);
}

int main(void)
{
    backoff_screen_t screen;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    screen.ui = bento_runtime_get_ui();
    /* a new suffix each run: a quick rerun never meets its old id */
    (void)snprintf(screen.client_id, sizeof(screen.client_id),
                   "%s-%04x", DEVICE_ID,
                   (unsigned)(bento_time_ticks_ms() &
                              (uint32_t)CLIENT_SUFFIX_MASK));
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        (void)bento_lcd_clear(); /* ignored refusal (modlcd.c) */
        (void)bento_lcd_console(
            "<h2>คาบ 12 - ตารางนัดหมายการลองใหม่</h2>");
        draw_schedule(&screen);
        status = drop_events(&screen);
    }
    if (status == BENTO_OK) {
        status = stay_online(&screen);
        say_goodbye(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
