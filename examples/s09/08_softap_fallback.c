/*
 * 08_softap_fallback.c -- ไม่มีวงให้ต่อ ก็ปล่อยวงเอง
 * (SoftAP fallback)
 *
 * สแกนหาวง Wi-Fi ก่อน แสดงสามวงที่แรงที่สุดบนจอ ถ้าเจอวงของห้องเรียน
 * (AIoT-Class) ก็ต่อแบบลูกข่ายตามปกติ ถ้าไม่เจอ บอร์ดจะปล่อยวงของ
 * ตัวเอง (bento-team01) ที่ 192.168.4.1 แถวบนของจอบอกว่าบอร์ด
 * ตัดสินใจทำอะไร แถวล่างคือวงที่สแกนเจอจริง
 *
 * Scans for Wi-Fi networks first and shows the three strongest. When
 * the classroom network (AIoT-Class) is there, the board joins it as
 * a station; when it is not, the board starts its own network
 * (bento-team01) at 192.168.4.1. The upper card says what the board
 * decided, the lower one lists what the scan really found.
 *
 * Twin of 08_softap_fallback.py
 * (embedded-systems-for-aiot-developer/examples/s09/08_softap_fallback.py),
 * merged from the library twin aiot-s09-08-softap-fallback. Style:
 * CLEAN_C.md.
 *
 * Differs from the original, where C or the C API forces it:
 *  - wifi.scan() and wifi.connect() block in MicroPython; here they
 *    are started and polled every 50 ms, and a Stop cancels the wait.
 *    The binding's own console lines ("[WiFi] ...") are not printed:
 *    the C library logs nothing (bento_wifi/README.md). The
 *    credentials are saved on a good join, as the binding does.
 *  - nets.sort(key=rssi, reverse=True) is MicroPython's own quicksort
 *    (py/objlist.c mp_quicksort), which is not stable: networks of
 *    equal strength keep the order that sort gives, not the scan's.
 *    This file runs the same quicksort, with a stack of ranges in
 *    place of the recursion.
 *  - wifi.ip() answers "0.0.0.0" when there is no address, as the
 *    binding does.
 *  - a scan that fails raises OSError and ends the original; here
 *    the program ends with the scan's status. On the reTerminal there
 *    is no wifi module (ImportError at the start); here the screen is
 *    drawn and the scan ends the program with BENTO_E_UNSUPPORTED.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_runtime.h"
#include "bento_wifi.h"

/* The networks, as the original names them. */
static const char WIFI_SSID[] = "AIoT-Class";
static const char AP_SSID[] = "bento-team01";
static const char AP_PASSWORD[] = "12345678";

/* The scan's answer, at most 20 networks: too big for the 8 KB
 * program stack, so file scope, zeroed by the loader and set again at
 * the start of main() (CLEAN_C.md C9). */
extern bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];

/* Times, counts and sizes. */
enum {
    SETTLE_MS = 200,   /* let the new screen settle */
    WAIT_STEP_MS = 50, /* how often a scan or a join is polled */
    TITLE_PX = 24,
    TEXT_PX = 20, /* Thai below 20 px loses its strokes */
    ROW_Y = 200,
    ROW_STEP_Y = 32,
    CARD_EDGE_PX = 12, /* max= of the card panels */
    CARD_BORDER = 1,   /* value= of the card panels */
    AUTO_PX = -1,
    TEXT_CHARS = 160
};

/* Colours, 0xRRGGBB: the course palette. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* The labels the program changes; SHOW_ROWS network rows are shown. */
enum {
    SHOW_ROWS = 3,
    ROLE_FIXED = 0,
    ROLE_FOUND = 1,
    ROLE_ACTION = 2,
    ROLE_IP = 3,
    ROLE_ROW = 4, /* SHOW_ROWS of them from here */
    ROLE_HINT = ROLE_ROW + SHOW_ROWS,
    ROLE_COUNT = ROLE_HINT + 1
};

/* The two cards on the 792 x 398 canvas. */
enum {
    CARD_X = 20,
    CARD_W = 652,
    DECISION_Y = 52,
    DECISION_H = 120,
    SCAN_Y = 188,
    SCAN_H = 140,
    ROW_X = 40
};

/* A label, or a card panel when text is NULL. */
typedef struct {
    int32_t role;
    const char *text;
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_spec_t;

typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t widgets[ROLE_COUNT];
    uint32_t count; /* networks the scan found */
} fallback_t;

/* ---- Small helpers ---- */

static bento_status_t stop_raised_in(bento_status_t status)
{
    const bool stopped = !bento_runtime_is_running();

    return ((status == BENTO_OK) && stopped) ? BENTO_E_CANCELLED
                                             : status;
}

/* Sleeps one polling step; a Stop ends the wait. */
static bento_status_t wait_step(void)
{
    return stop_raised_in(bento_time_sleep_ms(WAIT_STEP_MS));
}

/* wifi.ip(): the address, or "0.0.0.0" when there is none. */
static void read_ip(char text[BENTO_WIFI_IPV4_TEXT_MAX])
{
    uint32_t address = 0U;
    uint32_t length = 0U;

    if ((bento_wifi_ip(BENTO_WIFI_IF_ANY, &address) != BENTO_OK) ||
        (bento_wifi_ipv4_format(address, text, BENTO_WIFI_IPV4_TEXT_MAX,
                                &length) != BENTO_OK)) {
        (void)snprintf(text, BENTO_WIFI_IPV4_TEXT_MAX, "0.0.0.0");
    }
}

/* The widget setters: MicroPython's ignore a refusal (modui.c). */
static void show_text(const fallback_t *board, int32_t role,
                      const char *text)
{
    (void)bento_ui_set_text(board->ui, board->widgets[role], text,
                            (uint32_t)strlen(text));
}

static void show_color(const fallback_t *board, int32_t role,
                       uint32_t color)
{
    (void)bento_ui_set_color(board->ui, board->widgets[role], color);
}

/* lcd.print() ignores a refused message (modlcd.c). */
static void lcd_line(const char *text)
{
    (void)bento_lcd_print(text);
}

/* ui.poll(), whose events the original does not read. */
static bento_status_t drop_events(const fallback_t *board)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* a failed poll answers no events, which are not read anyway */
    (void)bento_ui_poll(board->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
    return stop_raised_in(BENTO_OK);
}

/* ---- The screen ---- */

static bento_status_t add_card(const fallback_t *board, int16_t y,
                               int16_t h)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = CARD_X;
    args.y = y;
    args.w = CARD_W;
    args.h = h;
    args.color = COLOR_CARD;
    args.min_val = COLOR_DIM; /* a panel's min= is its border colour */
    args.max_val = CARD_EDGE_PX;
    args.font_px = CARD_BORDER;
    return stop_raised_in(
        bento_ui_panel_create(board->ui, &args, &card));
}

static bento_status_t add_label(fallback_t *board,
                                const label_spec_t *spec)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = spec->text;
    args.text_len = (uint32_t)strlen(spec->text);
    args.x = spec->x;
    args.y = spec->y;
    args.color = spec->color;
    args.font_px = spec->size_px;
    return stop_raised_in(bento_ui_label_create(
        board->ui, &args, &board->widgets[spec->role]));
}

static bento_status_t
add_labels(fallback_t *board, const label_spec_t *labels, size_t count)
{
    size_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0U; (i < count) && (status == BENTO_OK); i++) {
        status = add_label(board, &labels[i]);
    }
    return status;
}

/* The decision card: what was found, what is done, the address. */
static bento_status_t build_decision_card(fallback_t *board)
{
    static const label_spec_t title = {
        ROLE_FIXED, "แผนสำรองเมื่อหาวงไม่เจอ", 20, 12, COLOR_TEXT,
        TITLE_PX};
    static const label_spec_t labels[] = {
        {ROLE_FOUND, "กำลังสแกน รอสักครู่", 40, 68, COLOR_WARN, TEXT_PX},
        {ROLE_ACTION, "-", 40, 100, COLOR_TEXT, TEXT_PX},
        {ROLE_IP, "-", 40, 136, COLOR_DIM, TEXT_PX}};
    bento_status_t status = add_label(board, &title);

    if (status == BENTO_OK) {
        status = add_card(board, DECISION_Y, DECISION_H);
    }
    if (status == BENTO_OK) {
        status = add_labels(board, labels,
                            sizeof(labels) / sizeof(labels[0]));
    }
    return status;
}

/* The scan card: the strongest networks, a hint and the trade-off. */
static bento_status_t build_scan_card(fallback_t *board)
{
    static const label_spec_t labels[] = {
        {ROLE_HINT, "-", 40, 294, COLOR_DIM, TEXT_PX},
        {ROLE_FIXED, "วิทยุมีชุดเดียว เป็น AP แล้วออกเน็ตไม่ได้", 20, 338,
         COLOR_DIM, TEXT_PX}};
    label_spec_t row = {ROLE_ROW, "-",       ROW_X,
                        ROW_Y,    COLOR_DIM, TEXT_PX};
    int32_t i;
    bento_status_t status = add_card(board, SCAN_Y, SCAN_H);

    for (i = 0; (i < SHOW_ROWS) && (status == BENTO_OK); i++) {
        row.role = ROLE_ROW + i;
        row.y = (int16_t)(ROW_Y + (i * ROW_STEP_Y));
        status = add_label(board, &row);
    }
    if (status == BENTO_OK) {
        status = add_labels(board, labels,
                            sizeof(labels) / sizeof(labels[0]));
    }
    return status;
}

static bento_status_t build_screen(fallback_t *board)
{
    bento_status_t status =
        stop_raised_in(bento_ui_screen_default(board->ui));

    if (status == BENTO_OK) {
        status = stop_raised_in(bento_time_sleep_ms(SETTLE_MS));
    }
    if (status == BENTO_OK) {
        status = build_decision_card(board);
    }
    if (status == BENTO_OK) {
        status = build_scan_card(board);
    }
    if (status == BENTO_OK) {
        status = drop_events(board);
    }
    return status;
}

/* ---- What the scan found ---- */

static void print_count(const fallback_t *board)
{
    const int count = (int)board->count;
    char line[TEXT_CHARS];

    (void)printf("สแกนเจอ %d วง\n", count);
    (void)snprintf(line, sizeof(line), "สแกนเจอ <b>%d</b> วง", count);
    lcd_line(line);
    if (board->count >= BENTO_WIFI_SCAN_MAX) {
        (void)printf("เตือน: ได้ผลเต็มเพดาน 20 วง "
                     "อาจมีวงที่ถูกตัดทิ้ง\n");
        lcd_line("<span class=warn>ผลเต็มเพดาน 20 วง "
                 "อาจมีวงตกหล่น</span>");
    }
}

static void show_joined(const fallback_t *board)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    char text[TEXT_CHARS];

    show_color(board, ROLE_ACTION, COLOR_OK);
    show_text(board, ROLE_ACTION, "ต่อสำเร็จ โหมด sta");
    read_ip(address);
    (void)snprintf(text, sizeof(text), "IP ที่ได้จาก DHCP: %s", address);
    show_text(board, ROLE_IP, text);
    show_text(board, ROLE_HINT,
              "โหมดนี้ออกเน็ตได้ ลอง wifi.ping() ต่อได้เลย");
    read_ip(address);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ต่อสำเร็จ IP %s</span>", address);
    lcd_line(text);
}

static void show_refused(const fallback_t *board)
{
    char address[BENTO_WIFI_IPV4_TEXT_MAX];
    char text[TEXT_CHARS];

    show_color(board, ROLE_ACTION, COLOR_BAD);
    show_text(board, ROLE_ACTION,
              "เจอวงแต่ต่อไม่ติด - "
              "รหัสผ่านผิดหรือเปล่า");
    read_ip(address);
    (void)snprintf(text, sizeof(text), "ip() = %s  (ยังไม่ได้ต่อ)",
                   address);
    show_text(board, ROLE_IP, text);
    show_text(board, ROLE_HINT,
              "เจอวงแต่ต่อไม่ติด = "
              "ปัญหาอยู่ที่รหัสผ่าน");
    lcd_line("<span class=err>เจอวงแต่ต่อไม่ติด</span>");
}

/* ---- The scan ---- */

/* wifi.scan() into g_networks: an error is the binding's OSError. */
static bento_status_t scan_networks(fallback_t *board)
{
    bool finished = false;
    bento_status_t status = bento_wifi_scan_start();

    while ((status == BENTO_OK) && !finished) {
        const bento_status_t answer = bento_wifi_scan_poll(
            g_networks, BENTO_WIFI_SCAN_MAX, &board->count);

        finished = answer != BENTO_E_IN_PROGRESS;
        status = finished ? answer : wait_step();
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_scan_cancel(); /* Stop: give up */
    }
    return status;
}

static void swap_networks(int32_t first, int32_t second)
{
    const bento_wifi_ap_t kept = g_networks[first];

    g_networks[first] = g_networks[second];
    g_networks[second] = kept;
}

/* One partition of mp_quicksort with reverse=True: the pivot is the
 * last network; it ends at the index returned. */
static int32_t partition(int32_t head, int32_t tail, int32_t *left_end)
{
    const int16_t pivot = g_networks[tail].rssi_dbm;
    int32_t low = head;
    int32_t high = tail;
    bool crossed = false;

    while (!crossed) {
        low++;
        while ((low < high) && (g_networks[low].rssi_dbm >= pivot)) {
            low++;
        }
        high--;
        while ((low < high) && (pivot >= g_networks[high].rssi_dbm)) {
            high--;
        }
        crossed = low >= high;
        if (!crossed) {
            swap_networks(low, high);
        }
    }
    swap_networks(low, tail);
    *left_end = high;
    return low;
}

/* nets.sort(key=rssi, reverse=True) as MicroPython sorts: its
 * quicksort, the parts kept on a stack instead of recursion. The
 * parts never overlap, so the order they are done in is the same. */
static void sort_by_strength(uint32_t count)
{
    int32_t heads[BENTO_WIFI_SCAN_MAX];
    int32_t tails[BENTO_WIFI_SCAN_MAX];
    int32_t pending = 1;

    heads[0] = -1; /* one before the first, as the binding passes */
    tails[0] = (int32_t)count - 1;
    while (pending > 0) {
        int32_t head;
        int32_t tail;

        pending--;
        head = heads[pending];
        tail = tails[pending];
        while ((tail - head) > 1) {
            int32_t left_end = 0;
            const int32_t right_start =
                partition(head, tail, &left_end);

            heads[pending] = right_start;
            tails[pending] = tail;
            pending++;
            tail = left_end;
        }
    }
}

static void show_strongest(const fallback_t *board)
{
    char text[TEXT_CHARS];
    int32_t i;

    for (i = 0; (i < SHOW_ROWS) && ((uint32_t)i < board->count); i++) {
        (void)snprintf(text, sizeof(text), "%4d dBm  ch %-3d %s",
                       (int)g_networks[i].rssi_dbm,
                       (int)g_networks[i].channel, g_networks[i].ssid);
        show_text(board, ROLE_ROW + i, text);
        show_color(board, ROLE_ROW + i, COLOR_TEXT);
    }
}

/* The first network named WIFI_SSID, or -1. */
static int32_t find_target(const fallback_t *board)
{
    int32_t found = -1;
    int32_t i;

    for (i = (int32_t)board->count - 1; i >= 0; i--) {
        if (strcmp(g_networks[i].ssid, WIFI_SSID) == 0) {
            found = i;
        }
    }
    return found;
}

/* ---- Joining the classroom network ---- */

/* wifi.connect(): true when joined; a failed join is False. */
static bento_status_t join_wifi(bool *joined)
{
    /* the classroom password, WIFI_PASSWORD in the .py: set it before
     * you run */
    static const char wifi_password[] = "<รหัสผ่านของห้องเรียน>";
    bento_wifi_connect_params_t params;
    bento_status_t answer = BENTO_E_IN_PROGRESS;
    bento_status_t status;

    bento_wifi_connect_params_init(&params);
    params.ssid = WIFI_SSID;
    params.ssid_len = (uint32_t)strlen(WIFI_SSID);
    params.pass = wifi_password;
    params.pass_len = (uint32_t)strlen(wifi_password);
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
    return (status == BENTO_E_CANCELLED) ? status : BENTO_OK;
}

/* The usual way: the classroom network is there, join it. */
static bento_status_t join_classroom(const fallback_t *board,
                                     int32_t target)
{
    const int rssi = (int)g_networks[target].rssi_dbm;
    char text[TEXT_CHARS];
    bool joined = false;
    bento_status_t status;

    show_color(board, ROLE_FOUND, COLOR_OK);
    (void)snprintf(text, sizeof(text), "เจอ %s ที่ %d dBm", WIFI_SSID,
                   rssi);
    show_text(board, ROLE_FOUND, text);
    show_text(board, ROLE_ACTION, "กำลังต่อแบบลูกข่าย (โหมด sta)");
    status = drop_events(board);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "เจอวง <b>%s</b> ความแรง %d dBm", WIFI_SSID,
                       rssi);
        lcd_line(text);
        status = join_wifi(&joined);
    }
    if ((status == BENTO_OK) && joined) {
        show_joined(board);
    } else if (status == BENTO_OK) {
        show_refused(board);
    } else {
        /* a Stop ended the join */
    }
    return status;
}

/* ---- Starting our own network ---- */

/* wifi.softap(ssid, password): true when it started. */
static bool start_access_point(void)
{
    bento_wifi_softap_config_t config;

    bento_wifi_softap_config_init(&config);
    config.ssid = AP_SSID;
    config.ssid_len = (uint32_t)strlen(AP_SSID);
    config.pass = AP_PASSWORD;
    config.pass_len = (uint32_t)strlen(AP_PASSWORD);
    return bento_wifi_softap_start(&config) == BENTO_OK;
}

static void show_access_point(const fallback_t *board, bool started)
{
    char text[TEXT_CHARS];

    if (started) {
        show_color(board, ROLE_ACTION, COLOR_OK);
        (void)snprintf(text, sizeof(text), "ปล่อยวง %s แล้ว", AP_SSID);
        show_text(board, ROLE_ACTION, text);
        show_color(board, ROLE_IP, COLOR_OK);
        (void)snprintf(text, sizeof(text),
                       "บอร์ดอยู่ที่ 192.168.4.1  รหัส "
                       "%s",
                       AP_PASSWORD);
        show_text(board, ROLE_IP, text);
        show_text(board, ROLE_HINT,
                  "เอามือถือต่อวงนี้ "
                  "แล้วจะคุยกับบอร์ดได้");
        (void)snprintf(text, sizeof(text),
                       "<span class=ok>ปล่อยวง %s ที่ 192.168.4.1"
                       "</span>",
                       AP_SSID);
        lcd_line(text);
        lcd_line("โหมดนี้ไม่มีทางออกเน็ต ping() ออกนอกใช้ไม่ได้");
    } else {
        show_color(board, ROLE_ACTION, COLOR_BAD);
        show_text(board, ROLE_ACTION, "เปิด softap ไม่สำเร็จ");
        show_text(board, ROLE_HINT, "ลองรีเซ็ตบอร์ดแล้วรันใหม่");
        lcd_line("<span class=err>เปิด softap ไม่สำเร็จ</span>");
    }
}

/* The fallback: no classroom network, so start our own. */
static bento_status_t become_access_point(const fallback_t *board)
{
    char text[TEXT_CHARS];
    bool started = false;
    bento_status_t status;

    show_color(board, ROLE_FOUND, COLOR_BAD);
    (void)snprintf(text, sizeof(text), "ไม่เจอ %s ในการสแกนครั้งนี้",
                   WIFI_SSID);
    show_text(board, ROLE_FOUND, text);
    show_color(board, ROLE_ACTION, COLOR_WARN);
    show_text(board, ROLE_ACTION, "เปลี่ยนไปปล่อยวงเอง (โหมด softap)");
    status = drop_events(board);
    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text),
                       "<span class=warn>ไม่เจอ %s - ปล่อยวงเอง</span>",
                       WIFI_SSID);
        lcd_line(text);
        /* drop a link left from an earlier run; answers None */
        (void)bento_wifi_disconnect();
        started = start_access_point();
        (void)printf("softap() คืนค่า %s\n", started ? "True" : "False");
        show_access_point(board, started);
    }
    return status;
}

static bento_status_t decide(fallback_t *board)
{
    int32_t target;
    bento_status_t status;

    lcd_line("กำลังสแกน...");
    status = scan_networks(board);
    if (status == BENTO_OK) {
        print_count(board);
        sort_by_strength(board->count);
        show_strongest(board);
        target = find_target(board);
        status = (target >= 0) ? join_classroom(board, target)
                               : become_access_point(board);
    }
    if (status == BENTO_OK) {
        status = drop_events(board);
    }
    return status;
}

int main(void)
{
    fallback_t board;
    bento_status_t status;

    (void)memset(&board, 0, sizeof(board));
    (void)memset(g_networks, 0, sizeof(g_networks));
    board.ui = bento_runtime_get_ui();
    (void)bento_lcd_clear(); /* ignored refusal (modlcd.c) */
    (void)bento_lcd_console("<h2>คาบ 9 - ไม่มีวงให้ต่อ "
                            "ก็ปล่อยวงเอง</h2>");
    status = build_screen(&board);
    if (status == BENTO_OK) {
        status = decide(&board);
    }
    if (status == BENTO_OK) {
        (void)printf("สรุป: อย่าเรียก connect() โดยไม่รู้ว่าวงนั้น"
                     "มีอยู่จริงหรือเปล่า\n");
        (void)printf("      scan() ตอบคำถามนั้นได้ใน 10 วินาที "
                     "ส่วน connect() ที่เดาผิดกินไป 85\n");
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
