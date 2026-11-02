/*
 * 01_scan_tuples.c -- ผลของ wifi.scan() หน้าตาเป็นอย่างไร (คาบ 09)
 *
 * สแกนหาวง WiFi รอบตัวหนึ่งครั้ง แล้วแสดงจำนวนวงตัวโต แท่งความแรง
 * ของวงแรก และรายชื่อวงสี่แถวแรกตามลำดับที่ชิปเจอ ทุกวงขึ้นบน
 * console ของจอและพิมพ์เป็นตารางทาง serial ปิดท้ายด้วยการอ่านวงแรก
 * ทีละช่อง (ssid, rssi, security, channel)
 *
 * Scans the Wi-Fi networks around once, then shows how many there
 * are in big digits, the first network's strength as a bar and the
 * first four networks in the order the chip found them. Every
 * network goes to the screen's console and, as a table, to the
 * serial console. It ends by reading the first network field by
 * field (ssid, rssi, security, channel).
 *
 * Twin of 01_scan_tuples.py
 * (embedded-systems-for-aiot-developer/examples/s09/01_scan_tuples.py),
 * merged from the library twin aiot-s09-01-scan-tuples. Style:
 * CLEAN_C.md.
 * Differs from the original:
 *   - wifi.scan() gives a list of tuples; here the scan fills
 *     bento_wifi_ap_t records with the same four values.
 *   - The C scan keeps the first 20 results the radio reports,
 *     hidden networks included, and drops the hidden ones after;
 *     the binding drops them before counting to 20
 *     (bento_wifi_port_psoc.c:112-123, modwifi.c:106-112). With
 *     more than 20 networks around, the C list can be shorter.
 *   - Text is bytes on the PSoC Edge (its MicroPython has no
 *     unicode str), so ssid[:18] and the {:<24} padding count
 *     bytes there and characters on the reTerminal; both are kept.
 *   - A failed scan raises OSError there and ends the program; here
 *     its status ends the program. Stop ends it quietly, where
 *     MicroPython raises KeyboardInterrupt.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_gpio.h"
#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D,
    COLOR_INFO = 0x4A9EFF
};

/* Rows, strength and sizes. */
enum {
    ROWS = 4,          /* what a 4.3-inch screen holds unpressed */
    SETTLE_MS = 200,   /* let the cleared screen settle */
    POLL_MS = 20,      /* how often the scan is asked */
    RSSI_EMPTY = -90,  /* dBm: next to nothing left */
    PERCENT_MAX = 100, /* -40 dBm and above: full */
    RSSI_GOOD = -60,
    RSSI_FAIR = -75,
    SSID_ROW_MAX = 18, /* ssid[:18] on a screen row */
    SSID_LCD_MAX = 20,
    SSID_TABLE_W = 24, /* "{:<24}" in the serial table */
    SSID_FIRST_MAX = 16,
    TEXT_BYTES = 160, /* a line with a 32-byte SSID */
    /* An SSID's cut (32 bytes at most) and, in the serial table,
     * up to 24 spaces of padding, and the NUL. */
    SSID_BYTES = (int)BENTO_WIFI_SSID_MAX + SSID_TABLE_W + 1
};

/* The widgets the program changes after it has drawn them. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t count_digits;
    bento_ui_id_t first_line;
    bento_ui_id_t bar;
    bento_ui_id_t rows[ROWS];
    bento_ui_id_t status_line;
} scan_screen_t;

/* A position on the screen: the widget arguments are 16-bit. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

/* Sleeps, and turns Stop into BENTO_E_CANCELLED. */
static bento_status_t wait_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

/* Is this the reTerminal? Asked at run time, by the name the board
 * gives (gpio.board_info()["name"]), so one Emulator build serves
 * both of its board models. A board that cannot say keeps the build
 * target's answer, as the old #if did. */
static bool on_reterminal(void)
{
    bento_gpio_board_info_t info;
#if defined(BENTO_TARGET_RETERMINAL)
    bool answer = true;
#else
    bool answer = false;
#endif

    bento_gpio_board_info_init(&info);
    if ((bento_gpio_board_info(&info) == BENTO_OK) &&
        (info.name != NULL)) {
        answer = (strcmp(info.name, "Seeed reTerminal") == 0);
    }
    return answer;
}

/* True for the first byte of a UTF-8 character (not 10xxxxxx). */
static bool starts_character(char byte)
{
    const uint8_t top_bits = 0xC0U;
    const uint8_t continuation = 0x80U;

    return ((uint8_t)byte & top_bits) != continuation;
}

/* The reTerminal's str counts characters: the byte where character
 * `keep` starts (UTF-8), or the whole text. *units: how many
 * characters come before that byte. */
static uint32_t utf8_prefix_bytes(const char *text, uint32_t length,
                                  uint32_t keep, uint32_t *units)
{
    uint32_t characters = 0U;
    uint32_t cut = length;
    uint32_t i;

    for (i = 0U; (i < length) && (cut == length); i++) {
        if (starts_character(text[i]) && (characters == keep)) {
            cut = i;
        } else if (starts_character(text[i])) {
            characters++;
        } else {
            /* a continuation byte belongs to the character before */
        }
    }
    *units = characters;
    return cut;
}

/* The bytes that make up a text's first `keep` units: bytes on the
 * PSoC Edge, characters on the reTerminal (header). *units: how
 * many units those bytes are. */
static uint32_t prefix_bytes(const char *text, uint32_t keep,
                             uint32_t *units)
{
    const uint32_t length = (uint32_t)strlen(text);
    uint32_t cut = (length < keep) ? length : keep;

    if (on_reterminal()) {
        cut = utf8_prefix_bytes(text, length, keep, units);
    } else {
        *units = cut;
    }
    return cut;
}

/* text[:keep] into out. */
static void copy_prefix(char *out, size_t cap, const char *text,
                        uint32_t keep)
{
    uint32_t units = 0U;
    const uint32_t cut = prefix_bytes(text, keep, &units);

    (void)snprintf(out, cap, "%.*s", (int)cut, text);
}

/* -90 dBm is next to nothing, -40 dBm full: good enough to compare
 * one network with another. */
static int32_t percent_of(int32_t rssi_dbm)
{
    const int32_t percent = (rssi_dbm - RSSI_EMPTY) * 2;
    int32_t clamped = percent;

    if (percent < 0) {
        clamped = 0;
    } else if (percent > PERCENT_MAX) {
        clamped = PERCENT_MAX;
    } else {
        /* already between 0 and 100 */
    }
    return clamped;
}

/* "{:<24}": the SSID cut to 24 units and padded to 24. */
static void table_ssid(char *out, size_t cap, const char *ssid)
{
    uint32_t units = 0U;
    const uint32_t cut = prefix_bytes(ssid, SSID_TABLE_W, &units);

    (void)snprintf(out, cap, "%.*s%*s", (int)cut, ssid,
                   (int)(SSID_TABLE_W - units), "");
}

/* Layout on the 792 x 398 canvas; the Console button covers about
 * x > 690, y > 340, so nothing goes there. */
enum {
    MARGIN_X = 20,
    INNER_X = 40,
    TITLE_Y = 12,
    CARD_Y = 52,
    CARD_W = 652,
    CARD_H = 120,
    CARD_RADIUS = 12,
    CARD_BORDER = 1,
    CAPTION_Y = 66,
    DIGITS_Y = 92,
    DIGITS_W = 132,
    DIGITS_H = 64,
    FIRST_X = 200,
    FIRST_Y = 92,
    BAR_Y = 124,
    BAR_W = 440,
    BAR_H = 24,
    LIST_CAPTION_Y = 182,
    ROW_Y = 212,
    ROW_STEP = 30,
    STATUS_Y = 336,
    TITLE_PX = 24,
    TEXT_PX = 20
};

/* ui.poll() here only keeps the queue empty; it answers no events
 * when it fails. */
static void drain_events(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                int16_t x, int16_t y, uint32_t color,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = x;
    args.y = y;
    args.color = color;
    args.font_px = TEXT_PX;
    return bento_ui_label_create(ui, &args, out_id);
}

/* The title and the card: the count, the first network, its bar. */
static bento_status_t add_card(scan_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "คาบ 9 - ผลของ wifi.scan()";
    args.text_len = (uint32_t)strlen(args.text);
    args.x = MARGIN_X;
    args.y = TITLE_Y;
    args.color = COLOR_TEXT;
    args.font_px = TITLE_PX;
    status = bento_ui_label_create(screen->ui, &args, &unused);
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = MARGIN_X;
        args.y = CARD_Y;
        args.w = CARD_W;
        args.h = CARD_H;
        args.color = COLOR_CARD;
        args.min_val = COLOR_DIM; /* a panel's min= is its border */
        args.max_val = CARD_RADIUS;
        args.font_px = CARD_BORDER;
        status = bento_ui_panel_create(screen->ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "จำนวนวงที่เจอ", INNER_X,
                           CAPTION_Y, COLOR_DIM, &unused);
    }
    return status;
}

/* The digits, the first network's line and its strength bar. */
static bento_status_t add_count_and_bar(scan_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.text = "--"; /* a Seg7 takes a text, not a number */
    args.text_len = (uint32_t)strlen(args.text);
    args.x = INNER_X;
    args.y = DIGITS_Y;
    args.w = DIGITS_W;
    args.h = DIGITS_H;
    args.color = COLOR_INFO;
    status =
        bento_ui_seg7_create(screen->ui, &args, &screen->count_digits);
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "วงแรก: -", FIRST_X, FIRST_Y,
                           COLOR_TEXT, &screen->first_line);
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.x = FIRST_X;
        args.y = BAR_Y;
        args.w = BAR_W;
        args.h = BAR_H;
        args.color = COLOR_DIM;
        args.min_val = 0;
        args.max_val = PERCENT_MAX;
        args.font_px = 0; /* the bar starts empty */
        status = bento_ui_bar_create(screen->ui, &args, &screen->bar);
    }
    return status;
}

/* The four rows and the status line under them. */
static bento_status_t add_rows(scan_screen_t *screen)
{
    bento_ui_id_t unused;
    int32_t row;
    bento_status_t status =
        add_label(screen->ui, "สี่แถวแรก ตามลำดับที่ชิปเจอ", MARGIN_X,
                  LIST_CAPTION_Y, COLOR_DIM, &unused);

    for (row = 0; (row < ROWS) && (status == BENTO_OK); row++) {
        status = add_label(screen->ui, "-", INNER_X,
                           px(ROW_Y + (row * ROW_STEP)), COLOR_DIM,
                           &screen->rows[row]);
    }
    if (status == BENTO_OK) {
        status =
            add_label(screen->ui, "กำลังสแกน... จอจะนิ่งไปครู่หนึ่ง", MARGIN_X,
                      STATUS_Y, COLOR_WARN, &screen->status_line);
    }
    return status;
}

/* Draws the whole screen before the scan, which freezes it, in the
 * original's order (the widgets' handles follow it). */
static bento_status_t build_screen(scan_screen_t *screen)
{
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_card(screen);
    }
    if (status == BENTO_OK) {
        status = add_count_and_bar(screen);
    }
    if (status == BENTO_OK) {
        status = add_rows(screen);
    }
    if (status == BENTO_OK) {
        drain_events(screen->ui);
    }
    return status;
}

/* One network: a screen row (the first four), a console line and a
 * row of the serial table. security 0 is open, anything else is
 * locked: no need to know which number is WPA2 or WPA3. */
static void show_network(const scan_screen_t *screen, int32_t index,
                         const bento_wifi_ap_t *network)
{
    const char *lock = (network->security == 0U) ? "open" : "lock";
    const int32_t rssi_dbm = network->rssi_dbm;
    char ssid[SSID_BYTES];
    char text[TEXT_BYTES];

    if (index < ROWS) {
        copy_prefix(ssid, sizeof(ssid), network->ssid, SSID_ROW_MAX);
        (void)bento_ui_set_color(screen->ui, screen->rows[index],
                                 (rssi_dbm >= RSSI_FAIR) ? COLOR_TEXT
                                                         : COLOR_DIM);
        (void)snprintf(text, sizeof(text),
                       "%" PRId32 ". %s  %" PRId32 " dBm  ch%u  %s",
                       index + 1, ssid, rssi_dbm,
                       (unsigned int)network->channel, lock);
        set_text(screen->ui, screen->rows[index], text);
    }
    copy_prefix(ssid, sizeof(ssid), network->ssid, SSID_LCD_MAX);
    (void)snprintf(text, sizeof(text),
                   "%" PRId32 ". %s %" PRId32 " dBm ch%u %s", index + 1,
                   ssid, rssi_dbm, (unsigned int)network->channel,
                   lock);
    (void)bento_lcd_print(text); /* lcd.print() ignores a console
                                    that is not ready (modlcd.c) */
    table_ssid(ssid, sizeof(ssid), network->ssid);
    (void)printf("%s %5" PRId32 " dBm  ch%-4u %s\n", ssid, rssi_dbm,
                 (unsigned int)network->channel, lock);
}

/* The first network: its bar, its line, and its four fields read
 * one by one, which are the same values as above. */
static void show_first(const scan_screen_t *screen,
                       const bento_wifi_ap_t *first)
{
    const int32_t rssi_dbm = first->rssi_dbm;
    const int32_t percent = percent_of(rssi_dbm);
    uint32_t color = COLOR_BAD;
    char ssid[SSID_BYTES];
    char text[TEXT_BYTES];

    if (rssi_dbm >= RSSI_GOOD) {
        color = COLOR_OK;
    } else if (rssi_dbm >= RSSI_FAIR) {
        color = COLOR_WARN;
    } else {
        /* weak: stays red */
    }
    (void)bento_ui_set_color(screen->ui, screen->bar, color);
    (void)bento_ui_set_value(screen->ui, screen->bar, percent);
    copy_prefix(ssid, sizeof(ssid), first->ssid, SSID_FIRST_MAX);
    (void)snprintf(text, sizeof(text),
                   "วงแรก: %s  %" PRId32 " dBm  (%" PRId32 "%%)", ssid,
                   rssi_dbm, percent);
    set_text(screen->ui, screen->first_line, text);
    (void)printf("แถวแรกอ่านด้วยหมายเลขช่อง:\n");
    (void)printf("  net[0] ssid     = %s\n", first->ssid);
    (void)printf("  net[1] rssi     = %" PRId32 "\n", rssi_dbm);
    (void)printf("  net[2] security = %" PRIu32 "\n", first->security);
    (void)printf("  net[3] channel  = %u\n",
                 (unsigned int)first->channel);
    (void)snprintf(text, sizeof(text), "net[0]=%s net[1]=%" PRId32,
                   first->ssid, rssi_dbm);
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text), "net[2]=%" PRIu32 " net[3]=%u",
                   first->security, (unsigned int)first->channel);
    (void)bento_lcd_print(text);
}

/* A scan's results: too big for the 8 KB program stack, so at file
 * scope, zeroed by the loader on every load and set again at the
 * start of main() (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];

/* wifi.scan(): every channel, 3-10 s (5 GHz has many more). */
static bento_status_t scan_networks(uint32_t *count)
{
    bento_status_t status = bento_wifi_scan_start();

    *count = 0U;
    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        status = wait_ms(POLL_MS);
        if (status == BENTO_OK) {
            status = bento_wifi_scan_poll(g_networks,
                                          BENTO_WIFI_SCAN_MAX, count);
        } else {                            /* Stop between two polls */
            (void)bento_wifi_scan_cancel(); /* nothing to report */
        }
    }
    return status;
}

/* The count and how long the scan took, then every network. */
static void show_results(const scan_screen_t *screen, uint32_t count,
                         int32_t took_ms)
{
    char text[TEXT_BYTES];
    uint32_t i;

    (void)snprintf(text, sizeof(text), "%" PRIu32, count);
    set_text(screen->ui, screen->count_digits, text);
    (void)bento_ui_set_color(screen->ui, screen->status_line, COLOR_OK);
    (void)snprintf(text, sizeof(text),
                   "สแกนเสร็จใน %" PRId32 " ms - เจอ %" PRIu32 " วง",
                   took_ms, count);
    set_text(screen->ui, screen->status_line, text);
    (void)snprintf(text, sizeof(text),
                   "สแกนเสร็จใน %" PRId32 " ms เจอ %" PRIu32 " วง",
                   took_ms, count);
    (void)bento_lcd_print(text);
    for (i = 0U; i < count; i++) {
        show_network(screen, (int32_t)i, &g_networks[i]);
    }
    if (count > 0U) {
        show_first(screen, &g_networks[0]);
    } else { /* the antenna heard nothing: a steel box, a basement */
        (void)bento_ui_set_color(screen->ui, screen->status_line,
                                 COLOR_BAD);
        set_text(screen->ui, screen->status_line,
                 "ไม่เจอวงไหนเลย ลองย้ายบอร์ดออกที่โล่ง");
        (void)bento_lcd_print("<span class=err>ไม่เจอวงไหนเลย</span>");
    }
    drain_events(screen->ui);
}

int main(void)
{
    scan_screen_t screen = {0};
    uint32_t count = 0U;
    bento_status_t status;

    (void)memset(g_networks, 0, sizeof(g_networks));
    screen.ui = bento_runtime_get_ui();
    (void)bento_lcd_clear(); /* lcd.* ignore a console that is not
                                ready (modlcd.c) */
    (void)bento_lcd_console("<h2>คาบ 9 - รูปร่างของผล scan()</h2>");
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        const uint32_t start_ms = bento_time_ticks_ms();

        status = scan_networks(&count);
        if (status == BENTO_OK) {
            show_results(
                &screen, count,
                bento_time_ticks_diff(bento_time_ticks_ms(), start_ms));
        }
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
