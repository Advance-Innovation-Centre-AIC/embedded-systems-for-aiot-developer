/*
 * s09_network_status.c -- จอสถานะเครือข่ายของทีม (ฉบับฝึกเติมโค้ด)
 *
 * วิธีรัน: 1) บนจอบอร์ด แตะการ์ด BENTO Playground ค้างหน้านี้ไว้
 *          2) แก้ WIFI_SSID / WIFI_PASS ให้ตรงกับเครือข่ายที่ผู้สอนแจก
 *          3) เติมช่องว่างทั้ง 6 จุดตามคำใบ้ แล้วกด "Program to Device"
 * สแกนคลื่นรอบตัว เรียงแรงไปอ่อน ลงตารางสี่คอลัมน์ ต่อเครือข่ายของทีม แล้วเปิด
 * หน้าสถานะที่วัด ping สองปลายทางสดทุก 3 วินาที พร้อมปุ่มสั่งสแกนใหม่
 * ระวัง: การต่อ WiFi รอได้นานถึง ~85 วินาทีถ้ารหัสผ่านผิด ให้รอจนขึ้นผล
 * ไฟล์นี้เดินสี่เรื่องต่อกัน สแกน - เรียง - ต่อ - วัด ตัวอย่างของคาบนี้แยกไว้
 * เรื่องละไฟล์ที่ examples/s09/ ไล่ตามเลข 01 ถึง 06 ก็จะได้เส้นทางเดียวกับ
 * ไฟล์นี้พอดี ติดตรงไหนให้เปิดเฉพาะไฟล์ของท่านั้น ไม่ต้องอ่านรวด
 * หน้าจอถูกวางไว้ให้ครบแล้ว ไม่ต้องแก้ - งานของเราคือทำให้ข้อมูลจริงไหลเข้าไปในนั้น
 * ก่อนเติมครบ โปรแกรมวาดหน้าจอ ตารางมีแต่หัว และจบที่ "ต่อไม่ติด"
 *
 * The practice version of the team's network status screen: the
 * screen is built for you; fill in the six blanks (scan, sort, read a
 * row, connect, poll the screen, ping the gateway) so real data flows
 * into it. Before they are filled in, the table has only its header
 * and the program ends at "ต่อไม่ติด", as the Python skeleton does.
 *
 * Twin of practise_codes/s09_network_status.py
 * (embedded-systems-for-aiot-developer/practise_codes/s09_network_status.py).
 * Style: CLEAN_C.md.
 *   - A blank does nothing here, as the skeleton's pass does. Each
 *     blank names, in its comment, the C call that fills it. A helper
 *     only a blank calls is referenced once with (void) so the
 *     unfilled file compiles under -Werror; the line can stay after
 *     the blank is filled in.
 * Differs from the original:
 *   - wifi.scan(), wifi.connect() and wifi.ping() block there; here
 *     each is started and polled, so Stop ends the wait.
 *   - wifi.scan() gives a list of tuples; here bento_wifi_ap_t records
 *     with the same four values, at most BENTO_WIFI_SCAN_MAX (20), and
 *     the sort is a stable insertion sort, strongest first, as
 *     list.sort(reverse=True) keeps equal values in order.
 *   - ssid[:12] cuts 12 bytes, as the PSoC Edge's MicroPython does (no
 *     unicode str there); the reTerminal counts characters.
 *   - wifi.ping() answers -1 for no answer and raises OSError when the
 *     link fails: here a ping that times out is -1, and any other
 *     failure is the OSError (both values -1, the second ping skipped).
 *   - The binding's own lines "[WiFi] Connecting..." and "[WiFi]
 *     Connected! IP=..." are printed by the program; its per-attempt
 *     lines are not, the C API does not report them.
 *   - A failed scan raises OSError there; here its status ends the
 *     program. Stop ends it quietly, where MicroPython raises
 *     KeyboardInterrupt.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* The team's values: set these before you run it. */
static const char WIFI_SSID[] = "AIoT-Class";
static const char WIFI_PASS[] = "changeme";
static const char NET_TEST_IP[] = "8.8.8.8"; /* Google Public DNS */

enum {
    PING_TIMEOUT_MS = 1500, /* wait this long for one ping answer */
    TOP_N = 3, /* a 288 px table: the header and three rows */
    PING_EVERY_MS = 3000,
    RSSI_FLOOR = -90, /* the meter's range: next to nothing */
    RSSI_CEIL = -40,  /* ... to a full bar */
    RSSI_GOOD = -60,
    RSSI_FAIR = -75,
    PERCENT = 100,
    SSID_CUT = 12, /* the first column holds 12 characters */
    SETTLE_MS = 200,
    LOOP_MS = 200,
    SLICE_MS = 20, /* how often a scan, join or ping is asked */
    MS_PER_SECOND = 1000,
    TABLE_COLS = 4,
    SCALE_TICKS = 11,
    SCALE_MAJOR = 5,
    TEXT_BYTES = 96,
    CELLS_BYTES = 64,
    NUMBER_BYTES = 12 /* any int32_t with its sign and NUL */
};

enum { /* colours, 0xRRGGBB (the session 8 dashboard set) */
    COL_TEXT = 0xE8EAED,
    COL_DIM = 0x9AA3AF,
    COL_CARD = 0x171B22,
    COL_OK = 0x30A46C,
    COL_WARN = 0xF5A623,
    COL_BAD = 0xE5484D,
    COL_RUN = 0x4A9EFF,
    COL_BUTTON = 0x3A4150
};

/* One widget's place and look. */
typedef struct {
    const char *text;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t color;
    int32_t value;
} spec_t;

/* The widgets the program changes later. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t ssid;
    bento_ui_id_t address;
    bento_ui_id_t tick;
    bento_ui_id_t scan;
    bento_ui_id_t table;
    bento_ui_id_t led_up;
    bento_ui_id_t led_down;
    bento_ui_id_t gateway;
    bento_ui_id_t internet;
    bento_ui_id_t rssi_text;
    bento_ui_id_t rssi_bar;
} net_screen_t;

/* The live loop's state. */
typedef struct {
    uint32_t gateway_ip;
    uint32_t internet_ip;
    uint32_t pinged_ms;
    int32_t last_second;
    int32_t gateway_ms;
    int32_t internet_ms;
} link_state_t;

/* The scan results: too big for the 8 KB program stack, so at file
 * scope, zeroed by the loader on every load and set again at the
 * start of main() (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Floor division, as Python's operator rounds toward minus infinity. */
static int32_t floor_div(int32_t dividend, int32_t divisor)
{
    int32_t quotient = dividend / divisor;

    if (((dividend % divisor) != 0) &&
        ((dividend < 0) != (divisor < 0))) {
        quotient--;
    }
    return quotient;
}

/* The widget setters ignore a refusal, as MicroPython's do (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static void set_color(bento_ui_t *ui, bento_ui_id_t widget,
                      uint32_t color)
{
    (void)bento_ui_set_color(ui, widget, color);
}

static void set_value(bento_ui_t *ui, bento_ui_id_t widget,
                      int32_t value)
{
    (void)bento_ui_set_value(ui, widget, value);
}

/* ui.poll() only to keep the screen alive; a failed poll answers none. */
static void keep_alive(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

static void args_from(const spec_t *spec, bento_ui_widget_args_t *args)
{
    bento_ui_widget_args_init(args);
    if (spec->text != NULL) {
        args->text = spec->text;
        args->text_len = (uint32_t)strlen(spec->text);
    }
    args->x = spec->x;
    args->y = spec->y;
    args->w = spec->w;
    args->h = spec->h;
    args->color = spec->color;
    args->font_px = spec->value;
}

static bento_status_t add_label(bento_ui_t *ui, const spec_t *spec,
                                bento_ui_id_t *out)
{
    bento_ui_widget_args_t args;

    args_from(spec, &args);
    return bento_ui_label_create(ui, &args, out);
}

static bento_status_t add_led(bento_ui_t *ui, const spec_t *spec,
                              bento_ui_id_t *out)
{
    bento_ui_widget_args_t args;

    args_from(spec, &args);
    return bento_ui_led_create(ui, &args, out);
}

/* The title, SSID, IP, the tick line and the scan button. */
static bento_status_t add_header(net_screen_t *screen)
{
    static const spec_t title = {
        "สถานะเครือข่ายของทีม", 24, 8, -1, -1, COL_TEXT, 24};
    static const spec_t ssid = {"SSID: -", 336,      12, -1,
                                -1,        COL_TEXT, 20};
    static const spec_t address = {"IP: -", 24,       48, -1,
                                   -1,      COL_TEXT, 20};
    static const spec_t tick = {"กำลังสแกน", 336,     48, -1,
                                -1,         COL_DIM, 20};
    static const spec_t scan = {"สแกนใหม่", 552,        8, 216,
                                88,        COL_BUTTON, 20};
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status = add_label(screen->ui, &title, &unused);

    if (status == BENTO_OK) {
        status = add_label(screen->ui, &ssid, &screen->ssid);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, &address, &screen->address);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, &tick, &screen->tick);
    }
    if (status == BENTO_OK) {
        args_from(&scan, &args);
        status =
            bento_ui_button_create(screen->ui, &args, &screen->scan);
    }
    return status;
}

/* The scan results are a table from the start: ui.Table lays out the
 * columns, no pixel counting. */
static bento_status_t add_table(net_screen_t *screen)
{
    static const spec_t table = {NULL, 24, 104, 480, 288, 0U, 0};
    static const int32_t widths_px[TABLE_COLS] = {144, 96, 80, 128};
    bento_ui_widget_args_t args;
    int32_t i;
    bento_status_t status;

    args_from(&table, &args);
    args.min_val = TABLE_COLS; /* a Table takes cols= in min */
    status = bento_ui_table_create(screen->ui, &args, &screen->table);
    for (i = 0; (i < TABLE_COLS) && (status == BENTO_OK); i++) {
        (void)bento_ui_set_col_width(screen->ui, screen->table, i,
                                     widths_px[i]);
    }
    return status;
}

/* The card first: LVGL draws in creation order. Two lights, one lit at
 * a time; the other dims, it never vanishes. */
static bento_status_t add_link_card(net_screen_t *screen)
{
    static const spec_t card = {NULL, 520, 104, 248, 288, COL_CARD, 1};
    static const spec_t led_up = {NULL, 536, 112, 48, 48, COL_OK, 0};
    static const spec_t up_text = {"ต่ออยู่", 592,     124, -1,
                                   -1,     COL_DIM, 16};
    static const spec_t led_down = {NULL, 536, 168, 48, 48, COL_BAD, 1};
    static const spec_t down_text = {"ยังไม่ต่อ", 592,     180, -1,
                                     -1,       COL_DIM, 16};
    static const spec_t gateway = {"เกตเวย์ -", 536,     224, -1,
                                   -1,         COL_DIM, 20};
    static const spec_t internet = {"อินเทอร์เน็ต -", 536, 256, -1, -1,
                                    COL_DIM,       20};
    const int32_t radius_px = 12;
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    args_from(&card, &args);
    args.min_val = (int32_t)COL_DIM; /* the border colour */
    args.max_val = radius_px;
    status = bento_ui_panel_create(screen->ui, &args, &unused);
    if (status == BENTO_OK) {
        status = add_led(screen->ui, &led_up, &screen->led_up);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, &up_text, &unused);
    }
    if (status == BENTO_OK) {
        status = add_led(screen->ui, &led_down, &screen->led_down);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, &down_text, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, &gateway, &screen->gateway);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, &internet, &screen->internet);
    }
    return status;
}

/* A dBm number alone does not say strong or weak: the range sits
 * beside it. The ruler ends at 688: from x=690, y=340 the corner is
 * the Console button's. */
static bento_status_t add_meter(net_screen_t *screen)
{
    static const spec_t text = {"ความแรง - dBm", 536, 288, -1, -1,
                                COL_TEXT,        20};
    static const spec_t bar = {NULL, 536,     320,       152,
                               12,   COL_RUN, RSSI_FLOOR};
    static const spec_t scale = {NULL, 536, 340, 152, 44, COL_TEXT, 0};
    bento_ui_widget_args_t args;
    bento_ui_id_t ruler = 0U;
    bento_status_t status =
        add_label(screen->ui, &text, &screen->rssi_text);

    if (status == BENTO_OK) {
        args_from(&bar, &args);
        args.min_val = RSSI_FLOOR;
        args.max_val = RSSI_CEIL;
        status =
            bento_ui_bar_create(screen->ui, &args, &screen->rssi_bar);
    }
    if (status == BENTO_OK) {
        args_from(&scale, &args);
        args.min_val = RSSI_FLOOR;
        args.max_val = RSSI_CEIL;
        status = bento_ui_scale_create(screen->ui, &args, &ruler);
    }
    if (status ==
        BENTO_OK) { /* six numbers on a short ruler overlap: three */
        (void)bento_ui_set_ticks(screen->ui, ruler, SCALE_TICKS,
                                 SCALE_MAJOR);
        keep_alive(screen->ui);
    }
    return status;
}

static bento_status_t build_screen(net_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_header(screen);
    }
    if (status == BENTO_OK) {
        status = add_table(screen);
    }
    if (status == BENTO_OK) {
        status = add_link_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_meter(screen);
    }
    return status;
}

/* RSSI is a negative dBm: -90 next to nothing, -40 a full bar. */
static int32_t signal_percent(int32_t rssi)
{
    int32_t percent = floor_div((rssi - RSSI_FLOOR) * PERCENT,
                                RSSI_CEIL - RSSI_FLOOR);

    if (percent < 0) {
        percent = 0;
    } else if (percent > PERCENT) {
        percent = PERCENT;
    } else {
        /* already 0..100 */
    }
    return percent;
}

static uint32_t signal_color(int32_t rssi)
{
    uint32_t color = COL_BAD;

    if (rssi >= RSSI_GOOD) {
        color = COL_RUN;
    } else if (rssi >= RSSI_FAIR) {
        color = COL_WARN;
    } else {
        /* weak */
    }
    return color;
}

/* Most home and classroom LANs have their gateway at .1 of the same
 * network. */
static uint32_t gateway_of(uint32_t ip_v4)
{
    const uint32_t network_mask = 0xFFFFFF00U;

    return (ip_v4 & network_mask) | 1U;
}

/* ping answers -1 for no answer: say it in words, never "-1". */
static void ms_text(const char *name, int32_t ms, char *text,
                    size_t cap)
{
    if (ms < 0) {
        (void)snprintf(text, cap, "%s ไม่ตอบ", name);
    } else {
        (void)snprintf(text, cap, "%s %" PRId32 " ms", name, ms);
    }
}

/* wifi.scan(): every channel, 3-10 s (5 GHz has many more channels). */
static bento_status_t scan_networks(uint32_t *count)
{
    bento_status_t status = bento_wifi_scan_start();

    *count = 0U;
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(SLICE_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_scan_poll(g_networks,
                                          BENTO_WIFI_SCAN_MAX, count);
        } else {
            (void)
                bento_wifi_scan_cancel(); /* Stop: nothing to report */
        }
    }
    return status;
}

/* A larger (less negative) RSSI is stronger: strongest first, equal
 * values in the order found. */
static void sort_by_rssi(uint32_t count)
{
    uint32_t i;

    for (i = 1U; i < count; i++) {
        const bento_wifi_ap_t moving = g_networks[i];
        uint32_t place = i;

        while ((place > 0U) &&
               (g_networks[place - 1U].rssi_dbm < moving.rssi_dbm)) {
            g_networks[place] = g_networks[place - 1U];
            place--;
        }
        g_networks[place] = moving;
    }
}

/* One table row: the four cells, each ended by a NUL. */
static void add_row(const net_screen_t *screen, uint8_t row,
                    const char *const cells[TABLE_COLS])
{
    char packed[CELLS_BYTES];
    size_t used = 0U;
    int32_t i;

    for (i = 0; i < TABLE_COLS; i++) {
        const int written = snprintf(
            &packed[used], sizeof(packed) - used, "%s", cells[i]);

        if ((written >= 0) &&
            ((used + (size_t)written + 1U) < sizeof(packed))) {
            used += (size_t)written + 1U;
        }
    }
    (void)bento_ui_add_row(screen->ui, screen->table, row, packed,
                           (uint32_t)used);
}

/* One network as a row: the name cut at 12 so it never wraps. */
static void add_network_row(const net_screen_t *screen, uint8_t row,
                            const bento_wifi_ap_t *network)
{
    char ssid[SSID_CUT + 1];
    char rssi[NUMBER_BYTES];
    char channel[NUMBER_BYTES];
    const char *cells[TABLE_COLS];

    (void)snprintf(ssid, sizeof(ssid), "%.*s", (int)SSID_CUT,
                   network->ssid);
    (void)snprintf(rssi, sizeof(rssi), "%d", (int)network->rssi_dbm);
    (void)snprintf(channel, sizeof(channel), "%u",
                   (unsigned int)network->channel);
    cells[0] = ssid;
    cells[1] = rssi;
    cells[2] = channel;
    cells[3] = (network->security == 0U) ? "เปิด" : "มีรหัส";
    add_row(screen, row, cells);
    keep_alive(screen->ui);
}

/* The meter shows the team network, or says it was not found. Bar's
 * colour() would paint the track, not the filled part, so only the
 * text changes colour. */
static void show_team_signal(const net_screen_t *screen, uint32_t count)
{
    char text[TEXT_BYTES];
    bool found = false;
    uint32_t i;

    for (i = 0U; (i < count) && !found; i++) {
        if (strcmp(g_networks[i].ssid, WIFI_SSID) == 0) {
            const int32_t rssi = g_networks[i].rssi_dbm;

            found = true;
            set_value(screen->ui, screen->rssi_bar, rssi);
            set_color(screen->ui, screen->rssi_text,
                      signal_color(rssi));
            (void)snprintf(text, sizeof(text),
                           "ความแรง %" PRId32 " dBm (%" PRId32 "%%)",
                           rssi, signal_percent(rssi));
            set_text(screen->ui, screen->rssi_text, text);
        }
    }
    if (!found) {
        set_value(screen->ui, screen->rssi_bar, RSSI_FLOOR);
        set_text(screen->ui, screen->rssi_text, "ความแรง ไม่เจอวงของทีม");
    }
}

/* Scan, strongest first, into the table. A short header, because a
 * header wider than its column wraps and pushes the last row out. */
static bento_status_t rescan(const net_screen_t *screen)
{
    static const char *const header[TABLE_COLS] = {"SSID", "dBm", "ช่อง",
                                                   "รหัส"};
    /* ตั้งไว้ว่างก่อน เพื่อให้กด Run ดูโครงหน้าจอได้ตั้งแต่ยังไม่ได้เติมอะไร
     * พอเติมบรรทัดถัดไปเสร็จ บรรทัดนี้จะถูกผลสแกนเขียนทับทันที */
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    /* การสแกนรอราว 3-10 วินาที (ย่าน 5 GHz นานกว่า เพราะช่องสัญญาณเยอะกว่ามาก)
     * เติม: status = scan_networks(&count); */
    (void)scan_networks; /* called once the blank above is filled in */

    /* RSSI ติดลบ ค่าที่มากกว่าคือแรงกว่า จึงเรียงมากไปน้อยด้วยช่อง rssi_dbm
     * ของแต่ละแถว
     * เติม: sort_by_rssi(count); */
    (void)sort_by_rssi; /* called once the blank above is filled in */

    if (status == BENTO_OK) {
        /* ล้างของเก่าก่อนเสมอ ไม่งั้นแถวของการสแกนรอบก่อนจะค้างอยู่ใต้แถวใหม่ */
        (void)bento_ui_items_clear(screen->ui, screen->table);
        add_row(screen, 0U, header);
        for (i = 0U; (i < count) && (i < (uint32_t)TOP_N); i++) {
            bento_wifi_ap_t network;

            (void)memset(&network, 0, sizeof(network));
            /* หนึ่งแถวของผลสแกน = ssid, rssi_dbm, security, channel อ่านทั้งแถวพร้อมกันได้เลย
             * เติม: network = g_networks[i]; */
            add_network_row(screen, (uint8_t)(i + 1U), &network);
        }
        show_team_signal(screen, count);
    }
    return status;
}

/* wifi.ip() as text; "0.0.0.0" when there is no address. */
static void read_ip(uint32_t *ip_v4, char *text, uint32_t cap)
{
    uint32_t length = 0U;

    if (bento_wifi_ip(BENTO_WIFI_IF_ANY, ip_v4) != BENTO_OK) {
        *ip_v4 = 0U;
    }
    if (bento_wifi_ipv4_format(*ip_v4, text, cap, &length) !=
        BENTO_OK) {
        (void)snprintf(text, cap, "0.0.0.0");
    }
}

/* wifi.connect(): *joined is its True or False. A refused or timed-out
 * join is False; another failure ends the program. */
static bento_status_t connect_wifi(bool *joined)
{
    bento_wifi_connect_params_t params;
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    uint32_t ip_v4 = 0U;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = WIFI_PASS;
    params.pass_len = (uint32_t)strlen(WIFI_PASS);
    params.flags = BENTO_WIFI_F_SAVE_CREDS; /* the binding saves too */
    (void)printf("[WiFi] Connecting...\n");
    status = bento_wifi_connect_start(&params);
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(SLICE_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_connect_poll();
        } else {
            (void)
                bento_wifi_connect_cancel(); /* Stop: nothing to report */
        }
    }
    *joined = status == BENTO_OK;
    if (*joined) {
        read_ip(&ip_v4, address, (uint32_t)sizeof(address));
        (void)printf("[WiFi] Connected! IP=%s\n", address);
    }
    if (*joined || (status == BENTO_E_TIMEOUT) ||
        (status == BENTO_E_FAILED)) {
        status = BENTO_OK;
    }
    return status;
}

/* wifi.ping(): the round trip, or -1 for no answer. *raised is the
 * OSError: the ping could not run. Only Stop is an error. */
static bento_status_t ping_ms(uint32_t target, int32_t *answer_ms,
                              bool *raised)
{
    uint32_t rtt_ms = 0U;
    bento_status_t status =
        bento_wifi_ping_start(target, PING_TIMEOUT_MS);

    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(SLICE_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_ping_poll(&rtt_ms);
        } else {
            (void)
                bento_wifi_ping_cancel(); /* Stop: nothing to report */
        }
    }
    *answer_ms = (status == BENTO_OK) ? (int32_t)rtt_ms : -1;
    *raised = (status != BENTO_OK) && (status != BENTO_E_TIMEOUT);
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* Both pings; a link that drops on the way is no answer for both. */
static bento_status_t measure(const net_screen_t *screen,
                              link_state_t *state)
{
    char text[TEXT_BYTES];
    bool raised = false;
    bento_status_t status = BENTO_OK;

    /* ping รับเฉพาะ IP เท่านั้น ส่งเลข IP แบบ uint32_t
     * เติม: status = ping_ms(state->gateway_ip, &state->gateway_ms, &raised); */

    if ((status == BENTO_OK) && !raised) {
        status =
            ping_ms(state->internet_ip, &state->internet_ms, &raised);
    }
    if (raised) {
        state->gateway_ms = -1;
        state->internet_ms = -1;
    }
    if (status == BENTO_OK) {
        set_color(screen->ui, screen->gateway,
                  (state->gateway_ms >= 0) ? COL_TEXT : COL_BAD);
        set_color(screen->ui, screen->internet,
                  (state->internet_ms >= 0) ? COL_TEXT : COL_BAD);
        ms_text("เกตเวย์", state->gateway_ms, text, sizeof(text));
        set_text(screen->ui, screen->gateway, text);
        ms_text("อินเทอร์เน็ต", state->internet_ms, text, sizeof(text));
        set_text(screen->ui, screen->internet, text);
    }
    return status;
}

static void show_link_lights(const net_screen_t *screen, bool up)
{
    set_value(screen->ui, screen->led_up, up ? 1 : 0);
    set_value(screen->ui, screen->led_down, up ? 0 : 1);
}

/* The scan button: say so first, the scan blocks for seconds. */
static bento_status_t read_touches(const net_screen_t *screen,
                                   link_state_t *state)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;
    uint32_t i;
    bento_status_t status = BENTO_OK;

    (void)memset(events, 0, sizeof(events));
    /* ถ้าลืมบรรทัดนี้ เฟิร์มแวร์จะซ่อน widget ทิ้งภายในราวสองวินาที และปุ่มจะไม่ตอบ
     * เติม: เรียก bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
     * &count, NULL) และถ้ามันไม่ตอบ BENTO_OK ให้ตั้ง count = 0U */
    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        if ((events[i].type == (uint8_t)BENTO_UI_EVENT_CLICKED) &&
            (events[i].id == screen->scan)) {
            set_text(screen->ui, screen->tick, "กำลังสแกน");
            keep_alive(screen->ui);
            status = rescan(screen);
            state->pinged_ms = bento_time_ticks_add(
                bento_time_ticks_ms(), -PING_EVERY_MS);
            state->last_second = -1;
        }
    }
    return status;
}

/* One round of the live loop: touches, the link, the countdown. This
 * loop asks one question at a time (connected, then ping), which is
 * enough for this screen. */
static bento_status_t link_round(const net_screen_t *screen,
                                 link_state_t *state)
{
    char text[TEXT_BYTES];
    bool connected = false;
    int32_t left_s;
    const uint32_t now_ms = bento_time_ticks_ms();
    bento_status_t status = read_touches(screen, state);

    if ((status == BENTO_OK) &&
        (bento_wifi_is_connected(&connected) != BENTO_OK)) {
        connected = false; /* a state that cannot be read is no link */
    }
    if ((status == BENTO_OK) && connected) {
        show_link_lights(screen, true);
        if (bento_time_ticks_diff(now_ms, state->pinged_ms) >=
            PING_EVERY_MS) {
            state->pinged_ms = now_ms;
            status = measure(screen, state);
        }
    } else if (status == BENTO_OK) {
        show_link_lights(screen, false);
        set_color(screen->ui, screen->gateway, COL_BAD);
        set_color(screen->ui, screen->internet, COL_BAD);
        set_text(screen->ui, screen->gateway, "ลิงก์หลุด");
        set_text(screen->ui, screen->internet, "เลขล่าสุด ไม่ใช่ตอนนี้");
    } else {
        /* Stop during a scan */
    }
    left_s = floor_div(
        PING_EVERY_MS - bento_time_ticks_diff(now_ms, state->pinged_ms),
        MS_PER_SECOND);
    if ((status == BENTO_OK) && (left_s != state->last_second)) {
        state->last_second = left_s;
        (void)snprintf(text, sizeof(text), "วัดใหม่ใน %" PRId32 " วิ",
                       left_s);
        set_text(screen->ui, screen->tick, text);
    }
    if (status == BENTO_OK) {
        status = wait_ms(LOOP_MS);
    }
    return status;
}

/* Joined: the address, the gateway, then the live loop until Stop. */
static bento_status_t run_link(const net_screen_t *screen)
{
    link_state_t state;
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    char gateway[BENTO_WIFI_IPV4_TEXT_MAX];
    char text[TEXT_BYTES];
    uint32_t ip_v4 = 0U;
    uint32_t length = 0U;
    bento_status_t status;

    (void)memset(&state, 0, sizeof(state));
    read_ip(&ip_v4, address, (uint32_t)sizeof(address));
    state.gateway_ip = gateway_of(ip_v4);
    status = bento_wifi_ipv4_format(state.gateway_ip, gateway,
                                    (uint32_t)sizeof(gateway), &length);
    if (status == BENTO_OK) {
        status = bento_wifi_ipv4_parse(NET_TEST_IP,
                                       (uint32_t)strlen(NET_TEST_IP),
                                       &state.internet_ip);
    }
    if (status == BENTO_OK) {
        show_link_lights(screen, true);
        (void)snprintf(text, sizeof(text), "SSID: %s", WIFI_SSID);
        set_text(screen->ui, screen->ssid, text);
        (void)snprintf(text, sizeof(text), "IP: %s", address);
        set_text(screen->ui, screen->address, text);
        (void)printf("connected, ip = %s gateway = %s\n", address,
                     gateway);
        state.pinged_ms =
            bento_time_ticks_add(bento_time_ticks_ms(), -PING_EVERY_MS);
        state.last_second = -1;
        state.gateway_ms = -1;
        state.internet_ms = -1;
    }
    while (status == BENTO_OK) {
        status = link_round(screen, &state);
    }
    return status;
}

/* The join failed: say what to check, and end. */
static void show_join_failed(const net_screen_t *screen)
{
    set_color(screen->ui, screen->ssid, COL_BAD);
    set_text(screen->ui, screen->ssid, "ต่อไม่ติด");
    set_text(screen->ui, screen->tick, "ตรวจ SSID/รหัสผ่าน");
    keep_alive(screen->ui);
}

int main(void)
{
    net_screen_t screen;
    bool joined = false;
    bento_status_t status;

    (void)memset(&screen, 0, sizeof(screen));
    (void)memset(g_networks, 0, sizeof(g_networks));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        status = rescan(&screen);
    }
    if (status == BENTO_OK) {
        set_text(screen.ui, screen.tick, "กำลังต่อ 85 วิ");
        keep_alive(screen.ui);
        /* joined ตั้ง false ไว้ก่อน เพื่อให้กด Run ดูโครงหน้าจอได้ตั้งแต่ยังไม่ได้เติมอะไร
         * การต่อรับชื่อวงกับรหัสผ่าน และรอจนกว่าจะรู้ผล
         * เติม: status = connect_wifi(&joined); */
        (void)
            connect_wifi; /* called once the blank above is filled in */
    }
    if ((status == BENTO_OK) && !joined) {
        show_join_failed(&screen);
    } else if (status == BENTO_OK) {
        status = run_link(&screen);
    } else {
        /* the status ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
