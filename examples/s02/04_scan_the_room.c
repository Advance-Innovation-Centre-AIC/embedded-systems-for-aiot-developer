/*
 * 04_scan_the_room.c -- หาทุกวงในห้องนี้
 *
 * ให้บอร์ดฟังคลื่นทั้งห้องด้วย wifi.scan() ไม่ต้องต่อเน็ตก่อน จอบอก
 * จำนวนวงที่เจอ เวลาที่ใช้สแกน และห้าวงที่แรงที่สุดเรียงจากบนลงล่าง
 * (สีบอกความแรง) แถบขวาบนคือความแรงของวงที่ชนะ วงที่เหลือลง Console
 * แล้วเทียบกับ wifi.status(): rssi ของ status() มีค่าเมื่อบอร์ดต่อวง
 * อยู่เท่านั้น ไม่งั้นเป็น 0 ตายตัว ความแรงจริงมาจาก scan()
 *
 * Lets the board listen to the whole room with wifi.scan(), with no
 * network joined. The screen shows how many networks it heard, how
 * long the scan took, and the five strongest, strongest first, in a
 * colour for their strength; the bar shows the winner's strength.
 * Every network also goes to the console. Then wifi.status() is
 * shown beside it: its rssi is 0 unless the board is linked, so
 * real strength comes from scan() only.
 *
 * Twin of 04_scan_the_room.py
 * (embedded-systems-for-aiot-developer/examples/s02/04_scan_the_room.py).
 * Style: CLEAN_C.md.
 * Differs from the original:
 *   - nets.sort() is MicroPython's quicksort, which is not stable:
 *     networks of equal strength come out in its order, so the twin
 *     runs the same quicksort (py/objlist.c), on indices and without
 *     recursion (MISRA 17.2).
 *   - wifi.status() is a dict walked in hash-table order: ip, rssi,
 *     connected, mode, ssid on the PSoC Edge (1-byte qstr hash),
 *     mode, ip, rssi, ssid, connected on the reTerminal (2 bytes).
 *     The texts are built in that order. On the reTerminal the C
 *     Wi-Fi module answers BENTO_E_UNSUPPORTED, so the program ends
 *     at the scan there.
 *   - A str is bytes on the PSoC Edge (its MicroPython has no
 *     unicode str): ssid[:18] cuts bytes, and repr() keeps bytes of
 *     0x80 and up. On the reTerminal it is characters (asked at
 *     run time: on_reterminal() in scan_logic.c).
 *   - wifi.scan() raises OSError when it fails; here its status ends
 *     the program.
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

/* The course palette, 0xRRGGBB (SPEC S7.13). */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_CARD = 0x171B22,
    COLOR_ACCENT = 0x4A9EFF,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* Strength, rows and sizes. */
enum {
    ROWS = 5,         /* rows on screen; the rest go to the console */
    STRONG_DBM = -60, /* the colour thresholds */
    WEAK_DBM = -75,
    EMPTY_DBM = -90, /* 0 % of the bar; -40 dBm is 100 % */
    PERCENT_MAX = 100,
    ROW_CHARS = 18, /* a neighbour's name can be any length */
    BEST_CHARS = 14,
    TABLE_CHARS = 24,
    SETTLE_MS = 200,    /* let the cleared screen settle */
    WAIT_SLICE_MS = 50, /* the scan is asked about this often */
    TEXT_BYTES = 160,
    /* A name is cut from an SSID (32 bytes at most) or is
     * "(ไม่ประกาศชื่อ)", 41 bytes; with the NUL, 42 do. */
    NAME_BYTES = 42,
    /* str() of a status value: "off", an IP or the SSID, at most
     * 32 bytes, and the NUL. */
    VALUE_BYTES = (int)BENTO_WIFI_SSID_MAX + 1
};

/* The widgets the program changes after it has drawn the screen. */
typedef struct {
    bento_ui_t *ui;
    bento_ui_id_t headline;
    bento_ui_id_t count;
    bento_ui_id_t took;
    bento_ui_id_t best;
    bento_ui_id_t bar;
    bento_ui_id_t trap;
    bento_ui_id_t hint;
    bento_ui_id_t rows[ROWS];
} scan_screen_t;

/* Where a label goes and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t font_px;
} look_t;

/* wifi.status()'s keys, and the order its dict walks them: the
 * hash-table order of MicroPython's dict (py/map.c, py/qstr.c),
 * which follows the qstr hash size: 1 byte on the PSoC Edge,
 * 2 bytes on the reTerminal. */
typedef enum {
    KEY_MODE = 0,
    KEY_CONNECTED,
    KEY_IP,
    KEY_SSID,
    KEY_RSSI,
    KEY_COUNT
} status_key_t;

/* What wifi.status() answers, as the binding builds it. */
typedef struct {
    const char *mode;
    bool connected;
    char ip[BENTO_WIFI_IPV4_TEXT_MAX];
    char ssid[BENTO_WIFI_SSID_MAX + 1U];
    int32_t rssi;
} radio_status_t;

static const char *const KEY_NAMES[KEY_COUNT] = {"mode", "connected",
                                                 "ip", "ssid", "rssi"};

/* wifi.status() in its dict's order: the reTerminal's MicroPython
 * hashes a qstr in 2 bytes, the PSoC Edge's in 1 (main.c). */
static const status_key_t STATUS_ORDER_RETERMINAL[KEY_COUNT] = {
    KEY_MODE, KEY_IP, KEY_RSSI, KEY_SSID, KEY_CONNECTED};
static const status_key_t STATUS_ORDER_PSOC[KEY_COUNT] = {
    KEY_IP, KEY_RSSI, KEY_CONNECTED, KEY_MODE, KEY_SSID};

/* The reTerminal's MicroPython has unicode str; the PSoC Edge's
 * (CORE_FEATURES) keeps a str as bytes, so a slice cuts bytes and
 * repr() prints bytes of 0x80 and up as they are. */
static const bool UNICODE_TEXT_RETERMINAL = true;
static const bool UNICODE_TEXT_PSOC = false;

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

/* A str of this board: characters, or bytes. */
static bool unicode_text(void)
{
    return on_reterminal() ? UNICODE_TEXT_RETERMINAL
                           : UNICODE_TEXT_PSOC;
}

/* The keys of wifi.status() in this board's dict order. */
static const status_key_t *status_order(void)
{
    return on_reterminal() ? STATUS_ORDER_RETERMINAL
                           : STATUS_ORDER_PSOC;
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

/* 0-100 for the eye: -90 is almost nothing, -40 is full. */
static int32_t percent_of(int32_t rssi)
{
    const int32_t percent = (rssi - EMPTY_DBM) * 2;

    return (percent < 0)             ? 0
           : (percent > PERCENT_MAX) ? PERCENT_MAX
                                     : percent;
}

/* A usable network is normal, so it stays in the text colour; green
 * on every good row would drown the orange and red ones. */
static uint32_t color_of(int32_t rssi)
{
    return (rssi >= STRONG_DBM) ? COLOR_TEXT
           : (rssi >= WEAK_DBM) ? COLOR_WARN
                                : COLOR_BAD;
}

/* ---- texts ------------------------------------------------------- */

/* The first `limit` characters of a name, as a slice of a str
 * takes them: bytes on the PSoC Edge, characters on the reTerminal. */
static void first_chars(const char *text, int32_t limit, char *out,
                        uint32_t cap)
{
    const uint8_t top_bits = 0xC0U;
    const uint8_t continuation = 0x80U;
    uint32_t used = 0U;
    int32_t count = 0;
    bool full = false;

    while ((text[used] != '\0') && ((used + 1U) < cap) && !full) {
        const bool starts =
            !unicode_text() ||
            (((uint8_t)text[used] & top_bits) != continuation);

        full = starts && (count == limit);
        if (!full) {
            count += starts ? 1 : 0;
            out[used] = text[used];
            used++;
        }
    }
    out[used] = '\0';
}

/* UTF-8 and repr(): the lead byte tells how many bytes follow, and
 * the ranges repr() writes as they are or escapes. */
enum {
    LEAD_FOUR = 0xF0,  /* 11110xxx: three more bytes */
    LEAD_THREE = 0xE0, /* 1110xxxx: two more */
    LEAD_TWO = 0xC0,   /* 110xxxxx: one more */
    EXTRA_MAX = 3,
    LEAD_BITS = 0x7F,
    MORE_SHIFT = 6, /* each following byte carries six bits */
    MORE_BITS = 0x3F,
    FIRST_PRINTABLE = 0x20,
    LAST_PRINTABLE = 0x7E,
    DELETE_CHAR = 0x7F,
    BYTE_END = 0x100,    /* below: \xNN */
    PLANE_END = 0x10000, /* below: \uNNNN, else \UNNNNNNNN */
    REPR_WIDEST = 11     /* \U00000000 and a quote */
};

/* The bytes that follow a lead byte; none on the PSoC Edge, whose
 * str is bytes. */
static uint32_t extra_bytes(uint8_t lead)
{
    uint32_t extra = 0U;

    if (unicode_text()) {
        extra = (lead >= (uint32_t)LEAD_FOUR)    ? (uint32_t)EXTRA_MAX
                : (lead >= (uint32_t)LEAD_THREE) ? 2U
                : (lead >= (uint32_t)LEAD_TWO)   ? 1U
                                                 : 0U;
    }
    return extra;
}

/* One character: a code point of UTF-8 text, or one byte on the
 * PSoC Edge; *at moves past it. */
static uint32_t next_char(const char *text, uint32_t *at)
{
    const uint8_t lead = (uint8_t)text[*at];
    uint32_t extra = extra_bytes(lead);
    uint32_t code =
        (extra == 0U)
            ? lead
            : ((uint32_t)lead & ((uint32_t)LEAD_BITS >> extra));

    *at += 1U;
    for (; (extra > 0U) && (text[*at] != '\0'); extra--) {
        code = (code << (uint32_t)MORE_SHIFT) |
               ((uint32_t)(uint8_t)text[*at] & (uint32_t)MORE_BITS);
        *at += 1U;
    }
    return code;
}

/* A character repr() writes as it is. */
static bool is_plain(uint32_t code)
{
    const bool above_controls = code >= (uint32_t)FIRST_PRINTABLE;

    return above_controls &&
           (unicode_text() ? (code <= (uint32_t)LAST_PRINTABLE)
                           : (code != (uint32_t)DELETE_CHAR));
}

static const char *short_escape(uint32_t code)
{
    const char *escape = NULL;

    if (code == (uint32_t)'\n') {
        escape = "\\n";
    } else if (code == (uint32_t)'\r') {
        escape = "\\r";
    } else if (code == (uint32_t)'\t') {
        escape = "\\t";
    } else {
        escape = NULL;
    }
    return escape;
}

/* One character of repr() into out (room bytes); answers its length. */
static uint32_t repr_char(uint32_t code, char quote, char *out,
                          uint32_t room)
{
    const char *escape = short_escape(code);
    int written;

    if ((code == (uint32_t)quote) || (code == (uint32_t)'\\')) {
        written = snprintf(out, room, "\\%c", (char)code);
    } else if (is_plain(code)) {
        written = snprintf(out, room, "%c", (char)code);
    } else if (escape != NULL) {
        written = snprintf(out, room, "%s", escape);
    } else if (code < (uint32_t)BYTE_END) {
        written = snprintf(out, room, "\\x%02" PRIx32, code);
    } else if (code < (uint32_t)PLANE_END) {
        written = snprintf(out, room, "\\u%04" PRIx32, code);
    } else {
        written = snprintf(out, room, "\\U%08" PRIx32, code);
    }
    return (uint32_t)written;
}

/* repr() of a str, as MicroPython writes it (objstrunicode.c on
 * the reTerminal, objstr.c on the PSoC Edge). */
static void repr_text(const char *text, char *out, uint32_t cap)
{
    const char quote =
        ((strchr(text, '\'') != NULL) && (strchr(text, '"') == NULL))
            ? '"'
            : '\'';
    uint32_t at = 0U;
    uint32_t used = (uint32_t)snprintf(out, (size_t)cap, "%c", quote);

    while ((text[at] != '\0') &&
           ((used + (uint32_t)REPR_WIDEST) < cap)) {
        const uint32_t code = next_char(text, &at);

        used += repr_char(code, quote, &out[used], cap - used);
    }
    (void)snprintf(&out[used], cap - used, "%c", quote);
}

/* ---- the results ------------------------------------------------- */

static const char *lock_of(const bento_wifi_ap_t *network)
{
    /* 0 is open; any other number is some kind of encryption */
    return (network->security == 0U) ? "เปิดโล่ง" : "มีรหัส";
}

/* wifi.status() as the binding fills its dict: ssid and rssi only
 * while linked, "" and 0 otherwise. */
static void read_status(radio_status_t *radio)
{
    bento_wifi_status_t status;
    uint32_t length = 0U;
    bool known;

    bento_wifi_status_init(&status);
    known = bento_wifi_status(&status) == BENTO_OK;
    radio->connected = known && status.connected;
    radio->mode = (!known || (status.mode == BENTO_WIFI_MODE_OFF))
                      ? "off"
                  : radio->connected ? "sta"
                                     : "idle";
    if (!radio->connected || (status.ip_v4 == 0U) ||
        (bento_wifi_ipv4_format(status.ip_v4, radio->ip,
                                sizeof(radio->ip),
                                &length) != BENTO_OK)) {
        (void)snprintf(radio->ip, sizeof(radio->ip), "0.0.0.0");
    }
    (void)snprintf(radio->ssid, sizeof(radio->ssid), "%s",
                   radio->connected ? status.ssid : "");
    radio->rssi = radio->connected ? status.rssi_dbm : 0;
}

/* The text keys of the dict: ip, mode and ssid. */
static const char *text_of(const radio_status_t *radio,
                           status_key_t key)
{
    const char *text = radio->ssid;

    if (key == KEY_IP) {
        text = radio->ip;
    } else if (key == KEY_MODE) {
        text = radio->mode;
    } else {
        text = radio->ssid;
    }
    return text;
}

/* One value of the dict: as str() prints it, or as repr() does. */
static void status_value(const radio_status_t *radio, status_key_t key,
                         bool as_repr, char *out, uint32_t cap)
{
    if (key == KEY_RSSI) {
        (void)snprintf(out, (size_t)cap, "%" PRId32, radio->rssi);
    } else if (key == KEY_CONNECTED) {
        (void)snprintf(out, (size_t)cap, "%s",
                       radio->connected ? "True" : "False");
    } else if (as_repr) {
        repr_text(text_of(radio, key), out, cap);
    } else {
        (void)snprintf(out, (size_t)cap, "%s", text_of(radio, key));
    }
}

/* for k in st: one console line per key, in the dict's order. */
static void print_status_keys(const radio_status_t *radio)
{
    char value[VALUE_BYTES];
    char text[TEXT_BYTES];
    const status_key_t *order = status_order();
    uint32_t i;

    (void)bento_lcd_console(
        "<span class=muted>--- wifi.status() ---</span>");
    for (i = 0U; i < (uint32_t)KEY_COUNT; i++) {
        status_value(radio, order[i], false, value, sizeof(value));
        (void)snprintf(text, sizeof(text), "  %s = %s",
                       KEY_NAMES[order[i]], value);
        (void)bento_lcd_print(text);
    }
}

/* print("status() =", st): the dict's repr, in the same order. */
static void print_status_dict(const radio_status_t *radio)
{
    char value[TEXT_BYTES];
    const status_key_t *order = status_order();
    uint32_t i;

    (void)printf("status() = {");
    for (i = 0U; i < (uint32_t)KEY_COUNT; i++) {
        status_value(radio, order[i], true, value, sizeof(value));
        (void)printf("%s'%s': %s", (i == 0U) ? "" : ", ",
                     KEY_NAMES[order[i]], value);
    }
    (void)printf("}\n");
}

/* Layout. The result rows can be about 550 px wide, so they take
 * the full width at the bottom. */
enum {
    MARGIN_X = 24,
    INNER_X = 40,
    TITLE_Y = 8,
    HEADLINE_X = 304,
    HEADLINE_Y = 16,
    CARD_Y = 56,
    CARD_W = 744,
    CARD_H = 176,
    CARD_RADIUS = 12,
    CARD_BORDER_PX = 1,
    CAPTION_Y = 70,
    VALUE_Y = 104,
    COUNT_W = 112,
    TIME_X = 192,
    TIME_W = 144,
    SEG_H = 56,
    BEST_X = 400,
    BAR_Y = 144,
    BAR_W = 344,
    BAR_H = 24,
    TRAP_Y = 184,
    ROWS_Y = 240,
    ROW_STEP = 32, /* a 20 px Thai line is about 27 px with marks */
    TITLE_PX = 24,
    TEXT_PX = 20
};

/* ---- the screen -------------------------------------------------- */

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                look_t look, bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = look.font_px;
    return bento_ui_label_create(ui, &args, out_id);
}

static bento_status_t add_caption(bento_ui_t *ui, const char *text,
                                  int16_t x)
{
    const look_t look = {x, CAPTION_Y, COLOR_DIM, TEXT_PX};
    bento_ui_id_t unused;

    return add_label(ui, text, look, &unused);
}

static bento_status_t add_seg(bento_ui_t *ui, int16_t x, int16_t w,
                              bento_ui_id_t *out_id)
{
    static const char dashes[] = "--";
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = dashes;
    args.text_len = (uint32_t)strlen(dashes);
    args.x = x;
    args.y = VALUE_Y;
    args.w = w;
    args.h = SEG_H;
    args.color = COLOR_ACCENT;
    return bento_ui_seg7_create(ui, &args, out_id);
}

static bento_status_t add_card(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t card;

    bento_ui_widget_args_init(&args);
    args.x = MARGIN_X;
    args.y = CARD_Y;
    args.w = CARD_W;
    args.h = CARD_H;
    args.color = COLOR_CARD;
    args.min_val = COLOR_CARD; /* the border colour */
    args.max_val = CARD_RADIUS;
    args.font_px = CARD_BORDER_PX;
    return bento_ui_panel_create(ui, &args, &card);
}

/* The winner's strength. A Bar takes no colour when it is made, so
 * .color() follows. */
static bento_status_t add_bar(scan_screen_t *screen)
{
    bento_ui_widget_args_t args;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = BEST_X;
    args.y = BAR_Y;
    args.w = BAR_W;
    args.h = BAR_H;
    args.min_val = 0;
    args.max_val = PERCENT_MAX;
    args.font_px = 0;
    status = bento_ui_bar_create(screen->ui, &args, &screen->bar);
    if (status == BENTO_OK) {
        /* MicroPython's widget setters ignore a refusal (modui.c) */
        (void)bento_ui_set_color(screen->ui, screen->bar, COLOR_DIM);
    }
    return status;
}

/* The summary card: count, time, the strongest and its bar. */
static bento_status_t add_summary(scan_screen_t *screen)
{
    const look_t best = {BEST_X, VALUE_Y, COLOR_TEXT, TEXT_PX};
    bento_status_t status = add_card(screen->ui);

    if (status == BENTO_OK) {
        status = add_caption(screen->ui, "เจอกี่วง", INNER_X);
    }
    if (status == BENTO_OK) {
        status = add_seg(screen->ui, INNER_X, COUNT_W, &screen->count);
    }
    if (status == BENTO_OK) {
        status = add_caption(screen->ui, "ใช้เวลา (ms)", TIME_X);
    }
    if (status == BENTO_OK) {
        status = add_seg(screen->ui, TIME_X, TIME_W, &screen->took);
    }
    if (status == BENTO_OK) {
        status = add_caption(screen->ui, "แรงที่สุด", BEST_X);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "รอผลสแกน", best, &screen->best);
    }
    if (status == BENTO_OK) {
        status = add_bar(screen);
    }
    return status;
}

/* The two trap lines sit in the card's bottom row; each run-time
 * text stays under half the width so they do not meet. */
static bento_status_t add_trap_lines(scan_screen_t *screen)
{
    const look_t trap = {MARGIN_X, TRAP_Y, COLOR_DIM, TEXT_PX};
    const look_t hint = {BEST_X, TRAP_Y, COLOR_DIM, TEXT_PX};
    bento_status_t status =
        add_label(screen->ui, "status() ให้ rssi ที่เป็นของปลอม", trap,
                  &screen->trap);

    if (status == BENTO_OK) {
        status = add_label(screen->ui,
                           "ความแรงจริงมาจาก scan() "
                           "เท่านั้น",
                           hint, &screen->hint);
    }
    return status;
}

/* All five rows now, filled later: made inside the loop, the widget
 * count would follow the room, and a busy room would pass the 64
 * widget ceiling. */
static bento_status_t add_rows(scan_screen_t *screen)
{
    int32_t i;
    bento_status_t status = BENTO_OK;

    for (i = 0; (i < ROWS) && (status == BENTO_OK); i++) {
        const int16_t y = (int16_t)(ROWS_Y + (i * ROW_STEP));
        const look_t row = {MARGIN_X, y, COLOR_DIM, TEXT_PX};

        status = add_label(screen->ui, "-", row, &screen->rows[i]);
    }
    return status;
}

static bento_status_t build_screen(scan_screen_t *screen)
{
    const look_t title = {MARGIN_X, TITLE_Y, COLOR_TEXT, TITLE_PX};
    const look_t headline = {HEADLINE_X, HEADLINE_Y, COLOR_WARN,
                             TEXT_PX};
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(screen->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = wait_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "หาทุกวงในห้องนี้", title, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(screen->ui, "กำลังจะสแกน", headline,
                           &screen->headline);
    }
    if (status == BENTO_OK) {
        status = add_summary(screen);
    }
    if (status == BENTO_OK) {
        status = add_trap_lines(screen);
    }
    if (status == BENTO_OK) {
        status = add_rows(screen);
    }
    return status;
}

/* ui.poll() whose events are not wanted; it also pushes the labels
 * onto the screen before a call that blocks. */
static void drop_events(const scan_screen_t *screen)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    /* a failed poll answers no events in MicroPython too */
    (void)bento_ui_poll(screen->ui, events, BENTO_UI_POLL_EVENTS_MAX,
                        &count, NULL);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void show(const scan_screen_t *screen, bento_ui_id_t label,
                 uint32_t color, const char *text)
{
    (void)bento_ui_set_color(screen->ui, label, color);
    (void)bento_ui_set_text(screen->ui, label, text,
                            (uint32_t)strlen(text));
}

static void set_text(const scan_screen_t *screen, bento_ui_id_t label,
                     const char *text)
{
    (void)bento_ui_set_text(screen->ui, label, text,
                            (uint32_t)strlen(text));
}

/* One network: on screen when it is in the top five, on the console
 * always, and in the wide table print() is for. */
static void show_network(const scan_screen_t *screen, int32_t rank,
                         const bento_wifi_ap_t *network)
{
    const int32_t rssi = (int32_t)network->rssi_dbm;
    const int32_t channel = (int32_t)network->channel;
    char name[NAME_BYTES];
    char text[TEXT_BYTES];

    first_chars(network->ssid, ROW_CHARS, name, sizeof(name));
    if (name[0] == '\0') {
        (void)snprintf(name, sizeof(name), "(ไม่ประกาศชื่อ)");
    }
    if (rank < ROWS) {
        (void)snprintf(text, sizeof(text),
                       "%" PRId32 ".  %s   %" PRId32 " dBm   ch%" PRId32
                       "   %s",
                       rank + 1, name, rssi, channel, lock_of(network));
        show(screen, screen->rows[rank], color_of(rssi), text);
    }
    (void)snprintf(text, sizeof(text),
                   "%" PRId32 ". %s %" PRId32 " dBm ch%" PRId32 " %s",
                   rank + 1, name, rssi, channel, lock_of(network));
    /* lcd.print() ignores a console that is not ready (modlcd.c) */
    (void)bento_lcd_print(text);
    first_chars(network->ssid, TABLE_CHARS, name, sizeof(name));
    (void)printf("%-24s %5" PRId32 " dBm  ch%-4" PRId32 " %s\n", name,
                 rssi, channel, lock_of(network));
}

static void show_winner(const scan_screen_t *screen,
                        const bento_wifi_ap_t *winner,
                        int32_t open_count, uint32_t count)
{
    const int32_t rssi = (int32_t)winner->rssi_dbm;
    char name[NAME_BYTES];
    char text[TEXT_BYTES];

    (void)bento_ui_set_value(screen->ui, screen->bar, percent_of(rssi));
    (void)bento_ui_set_color(screen->ui, screen->bar, color_of(rssi));
    first_chars(winner->ssid, BEST_CHARS, name, sizeof(name));
    (void)snprintf(text, sizeof(text), "%s  %" PRId32 " dBm", name,
                   rssi);
    show(screen, screen->best, color_of(rssi), text);
    /* a good scan is normal: back to the text colour, no green */
    (void)bento_ui_set_color(screen->ui, screen->count, COLOR_TEXT);
    (void)snprintf(text, sizeof(text),
                   "เจอ %" PRIu32 " วง | เปิดโล่ง %" PRId32
                   " วง | แสดง %d แถวแรก",
                   count, open_count, ROWS);
    show(screen, screen->headline, COLOR_TEXT, text);
    drop_events(screen);
}

/* The trap, proven with real numbers side by side. */
static void show_trap(const scan_screen_t *screen, int32_t top_rssi,
                      const char *top_name)
{
    radio_status_t radio;
    char text[TEXT_BYTES];

    read_status(&radio);
    print_status_keys(&radio);
    (void)snprintf(text, sizeof(text),
                   "status() rssi = %" PRId32 " แต่ของจริง %" PRId32
                   " dBm",
                   radio.rssi, top_rssi);
    show(screen, screen->trap, COLOR_BAD, text);
    (void)snprintf(text, sizeof(text), "mode = %s | ip = %s",
                   radio.mode, radio.ip);
    show(screen, screen->hint, COLOR_TEXT, text);
    drop_events(screen);
    (void)snprintf(text, sizeof(text),
                   "<span class=error>status() rssi = %" PRId32
                   " ซึ่งเป็นค่าตายตัว</span>",
                   radio.rssi);
    (void)bento_lcd_print(text);
    (void)snprintf(text, sizeof(text),
                   "<span class=ok>ของจริงคือ %" PRId32
                   " dBm จาก scan()</span>",
                   top_rssi);
    (void)bento_lcd_print(text);
    print_status_dict(&radio);
    (void)printf("แรงที่สุด: %s %" PRId32 " dBm | %" PRId32 " %%\n",
                 top_name, top_rssi, percent_of(top_rssi));
}

/* An empty list is not a bug: the antenna heard nothing (a steel
 * cabinet, a basement). Say so rather than stay silent. */
static void show_nothing_heard(const scan_screen_t *screen)
{
    (void)bento_ui_set_color(screen->ui, screen->count, COLOR_BAD);
    show(screen, screen->headline, COLOR_BAD,
         "ไม่เจอวงไหนเลย ลองย้ายบอร์ดออกที่โล่ง");
    drop_events(screen);
    (void)bento_lcd_print("<span class=error>ไม่เจอวงไหนเลย</span>");
}

/* Up to BENTO_WIFI_SCAN_MAX networks: too big for the 8 KB stack,
 * so at file scope and cleared at the start of main (C9). */
extern bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];

/* ---- the scan ---------------------------------------------------- */

/* wifi.scan(): blocks until every channel is swept, about 3 to 10 s
 * (5 GHz has many more channels). Asked in slices so Stop ends it. */
static bento_status_t scan_networks(uint32_t *count)
{
    bento_status_t status = bento_wifi_scan_start();
    bento_status_t scan = BENTO_E_IN_PROGRESS;

    while ((status == BENTO_OK) && (scan == BENTO_E_IN_PROGRESS)) {
        scan = bento_wifi_scan_poll(g_networks, BENTO_WIFI_SCAN_MAX,
                                    count);
        if (scan == BENTO_E_IN_PROGRESS) {
            status = wait_ms(WAIT_SLICE_MS);
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_scan_cancel(); /* Stop wins either way */
    }
    return (status == BENTO_OK) ? scan : status;
}

static int32_t rssi_at(const uint8_t *order, int32_t i)
{
    return (int32_t)g_networks[order[i]].rssi_dbm;
}

/* One partition of MicroPython's mp_quicksort with reverse=True:
 * the pivot is the range's last item. Returns h; *t_out gets t. */
static int32_t partition(uint8_t *order, int32_t head, int32_t tail,
                         int32_t *t_out)
{
    const int32_t pivot = rssi_at(order, tail);
    int32_t h = head;
    int32_t t = tail;
    uint8_t moved;

    for (;;) {
        do {
            h++;
        } while ((h < t) && !(rssi_at(order, h) < pivot));
        do {
            t--;
        } while ((h < t) && !(pivot < rssi_at(order, t)));
        if (h >= t) {
            break;
        }
        moved = order[h];
        order[h] = order[t];
        order[t] = moved;
    }
    moved = order[h];
    order[h] = order[tail];
    order[tail] = moved;
    *t_out = t;
    return h;
}

/* nets.sort(key=rssi, reverse=True), strongest first. The ranges
 * wait on a small stack instead of a recursive call. */
static void rank_networks(uint8_t *order, uint32_t count)
{
    int32_t heads[BENTO_WIFI_SCAN_MAX];
    int32_t tails[BENTO_WIFI_SCAN_MAX];
    int32_t waiting = 1;

    heads[0] = -1; /* the slot before the first item, as objlist.c */
    tails[0] = (int32_t)count - 1;
    while (waiting > 0) {
        int32_t head;
        int32_t tail;

        waiting--;
        head = heads[waiting];
        tail = tails[waiting];
        while ((tail - head) > 1) {
            int32_t t = 0;
            const int32_t h = partition(order, head, tail, &t);

            heads[waiting] = head;
            tails[waiting] = t;
            waiting++;
            head = h;
        }
    }
}

static void show_results(const scan_screen_t *screen, uint32_t count)
{
    uint8_t order[BENTO_WIFI_SCAN_MAX];
    int32_t open_count = 0;
    uint32_t i;

    for (i = 0U; i < count; i++) {
        order[i] = (uint8_t)i;
    }
    rank_networks(order, count);
    for (i = 0U; i < count; i++) {
        const bento_wifi_ap_t *network = &g_networks[order[i]];

        open_count += (network->security == 0U) ? 1 : 0;
        show_network(screen, (int32_t)i, network);
    }
    show_winner(screen, &g_networks[order[0]], open_count, count);
    show_trap(screen, (int32_t)g_networks[order[0]].rssi_dbm,
              g_networks[order[0]].ssid);
}

static bento_status_t scan_and_report(const scan_screen_t *screen)
{
    char text[TEXT_BYTES];
    const uint32_t start_ms = bento_time_ticks_ms();
    uint32_t count = 0U;
    bento_status_t status = scan_networks(&count);
    const int32_t took_ms =
        bento_time_ticks_diff(bento_time_ticks_ms(), start_ms);

    if (status == BENTO_OK) {
        (void)snprintf(text, sizeof(text), "%" PRIu32, count);
        set_text(screen, screen->count, text);
        (void)snprintf(text, sizeof(text), "%" PRId32, took_ms);
        set_text(screen, screen->took, text);
        (void)snprintf(text, sizeof(text),
                       "สแกนเสร็จใน %" PRId32 " ms เจอ %" PRIu32 " วง",
                       took_ms, count);
        (void)bento_lcd_print(text);
        if (count == 0U) {
            show_nothing_heard(screen);
        } else {
            show_results(screen, count);
        }
    }
    return status;
}

int main(void)
{
    scan_screen_t screen = {0};
    bento_status_t status;

    (void)memset(g_networks, 0, sizeof(g_networks));
    screen.ui = bento_runtime_get_ui();
    status = build_screen(&screen);
    if (status == BENTO_OK) {
        drop_events(&screen);
        (void)bento_lcd_clear();
        (void)bento_lcd_console("<h2>หาทุกวงในห้องนี้</h2>");
        /* the label must reach the screen before the scan blocks */
        set_text(&screen, screen.headline,
                 "กำลังสแกน จอจะนิ่งสักครู่ อย่ากดรีเซ็ต");
        drop_events(&screen);
        (void)bento_lcd_print("กำลังสแกน...");
        status = scan_and_report(&screen);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
