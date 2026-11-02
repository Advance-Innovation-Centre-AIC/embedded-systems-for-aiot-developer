/*
 * 02_rank_by_rssi.c -- เรียงวงตามความแรง (Rank by RSSI)
 *
 * สแกน WiFi แล้วเรียงวงจากแรงไปอ่อน ห้าอันดับแรกขึ้นจอ แต่ละอันดับมี
 * ชื่อวง ค่า dBm คำตัดสินสามระดับ และแท่งความแรงที่มีสีตามคำตัดสิน
 * เพราะคำว่า -67 dBm ไม่ตอบคำถาม "ตรงนี้วางบอร์ดได้ไหม" ให้ใคร
 * RSSI เป็นเลขติดลบ ค่าที่มากกว่าคือแรงกว่า ดูอย่างเดียว ไม่ต้องกด
 *
 * Scans Wi-Fi and ranks the networks from strongest to weakest. The
 * top five show on screen, each with its name, dBm, a three-level
 * verdict and a strength bar coloured by that verdict, because
 * "-67 dBm" answers nobody's question "can the board go here?".
 * RSSI is negative: the larger number is the stronger signal. Just
 * watch; nothing to tap.
 *
 * Twin of 02_rank_by_rssi.py
 * (embedded-systems-for-aiot-developer/examples/s09/02_rank_by_rssi.py),
 * merged from the library twin aiot-s09-02-rank-by-rssi. Style: CLEAN_C.md.
 * Differs from the original: wifi.scan() is bento_wifi_scan_start
 * and its poll. The sort is MicroPython's own quicksort written
 * without recursion, so networks of equal strength come out in the
 * same order as on the board (its sort is not stable).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_lcd.h"
#include "bento_runtime.h"
#include "bento_time.h"
#include "bento_wifi.h"

/* The ranking and the field rule of thumb: -90 dBm is almost
 * nothing, -40 dBm full; better than -60 is comfortable, -60 to -75
 * works but stutters. A linear map is no physics, but it compares
 * one network with another well enough. */
enum {
    TOP_N = 5, /* limited by the screen, not by the networks */
    FLOOR_DBM = -90,
    PERCENT_PER_DB = 2,
    PERCENT = 100,
    COMFORT_DBM = -60,
    USABLE_DBM = -75
};

/* How many characters of a name each place shows. */
enum {
    LABEL_CHARS = 16,
    CONSOLE_CHARS = 18,
    PRINT_CHARS = 24
};

/* Timing, in milliseconds. */
enum {
    SETTLE_MS = 200, /* let the cleared screen settle */
    WIFI_SLICE_MS = 50
};

/* The course palette, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_OK = 0x30A46C,
    COLOR_WARN = 0xF5A623,
    COLOR_BAD = 0xE5484D
};

/* Layout on the 792 x 398 canvas. */
enum {
    FONT_PX = 20,
    TITLE_PX = 24,
    LEFT_X = 20,
    TITLE_Y = 12,
    HEADER_Y = 46,
    ROW_Y = 76,
    ROW_STEP = 46,
    BAR_DY = 28,
    BAR_W = 640,
    BAR_H = 16,
    NOTE_Y = 316,
    ADVICE_Y = 342,
    ADVICE2_X = 180,
    LINE_BYTES = 160, /* a label or console line, cut at 126/127 */
    NAME_BYTES = 64   /* a 32-byte name padded to 24 characters */
};

/* The networks found, strongest first, as indexes into g_networks. */
typedef struct {
    uint8_t order[BENTO_WIFI_SCAN_MAX];
    uint32_t count;
} ranking_t;

static bento_status_t check_running(bento_status_t status)
{
    bento_status_t result = status;

    if ((result == BENTO_OK) && !bento_runtime_is_running()) {
        result = BENTO_E_CANCELLED;
    }
    return result;
}

/* lcd.print() ignores a console that is not ready (modlcd.c). */
static void console_line(const char *text)
{
    (void)bento_lcd_print(text);
}

static bool starts_character(char byte)
{
    const uint8_t top_bits = 0xC0U;     /* marks a UTF-8 byte's kind */
    const uint8_t continuation = 0x80U; /* 10xxxxxx: not a first byte */

    return ((uint8_t)byte & top_bits) != continuation;
}

/* Python's name[:chars] and then ljust(width): characters, not
 * bytes, so a Thai name is cut and padded as the original does.
 * width 0 pads nothing. */
static void cut_name(char *out, const char *name, int32_t chars,
                     int32_t width)
{
    int32_t taken = 0;
    size_t length = 0U;

    while ((name[length] != '\0') &&
           !(starts_character(name[length]) && (taken == chars))) {
        if (starts_character(name[length])) {
            taken++;
        }
        length++;
    }
    (void)memcpy(out, name, length);
    while (taken < width) {
        out[length] = ' ';
        length++;
        taken++;
    }
    out[length] = '\0';
}

/* -90 dBm is 0 %, -40 dBm and stronger 100 %. */
static int32_t percent_of(int32_t rssi_dbm)
{
    int32_t percent = (rssi_dbm - FLOOR_DBM) * PERCENT_PER_DB;

    if (percent < 0) {
        percent = 0;
    } else if (percent > PERCENT) {
        percent = PERCENT;
    } else {
        /* inside the scale */
    }
    return percent;
}

static const char *verdict_of(int32_t rssi_dbm)
{
    const char *verdict = "อ่อนเกินไป";

    if (rssi_dbm >= COMFORT_DBM) {
        verdict = "ใช้ได้สบาย";
    } else if (rssi_dbm >= USABLE_DBM) {
        verdict = "พอใช้ได้";
    } else {
        /* too weak */
    }
    return verdict;
}

/* The colour follows the same rule as the verdict, or the screen
 * and the words would argue. */
static uint32_t color_of(int32_t rssi_dbm)
{
    uint32_t color = COLOR_BAD;

    if (rssi_dbm >= COMFORT_DBM) {
        color = COLOR_OK;
    } else if (rssi_dbm >= USABLE_DBM) {
        color = COLOR_WARN;
    } else {
        /* too weak */
    }
    return color;
}

static void swap_places(ranking_t *ranking, int32_t first,
                        int32_t second)
{
    const uint8_t kept = ranking->order[first];

    ranking->order[first] = ranking->order[second];
    ranking->order[second] = kept;
}

/* The console and the REPL get the same place, cut to their own
 * widths. */
static void report_place(int32_t place, const char *ssid,
                         int32_t rssi_dbm)
{
    char name[NAME_BYTES];
    char line[LINE_BYTES];
    const int32_t percent = percent_of(rssi_dbm);

    cut_name(name, ssid, CONSOLE_CHARS, 0);
    (void)snprintf(line, sizeof(line),
                   "%" PRId32 ". %s %" PRId32 " dBm %" PRId32 "%%",
                   place, name, rssi_dbm, percent);
    console_line(line);
    cut_name(name, ssid, PRINT_CHARS, PRINT_CHARS);
    (void)printf("  %" PRId32 "    %s %4" PRId32 "  %3" PRId32
                 "%%  %s\n",
                 place, name, rssi_dbm, percent, verdict_of(rssi_dbm));
}

/* ui.poll() here only lets the screen catch up; its events are not
 * used, and a failed poll answers none. */
static void flush_screen(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL);
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
    args.font_px = FONT_PX;
    return bento_ui_label_create(ui, &args, out_id);
}

/* The note is up before the scan: the screen does not move while
 * it runs. */
static bento_status_t build_screen(bento_ui_t *ui, bento_ui_id_t *note)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused;
    bento_status_t status =
        bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                        BENTO_RUNTIME_SCREEN_HEIGHT_PX);

    if (status == BENTO_OK) {
        status = check_running(bento_time_sleep_ms(SETTLE_MS));
    }
    if (status == BENTO_OK) {
        bento_ui_widget_args_init(&args);
        args.text = "คาบ 9 - เรียงวงตามความแรง";
        args.text_len = (uint32_t)strlen(args.text);
        args.x = LEFT_X;
        args.y = TITLE_Y;
        args.color = COLOR_TEXT;
        args.font_px = TITLE_PX;
        status = bento_ui_label_create(ui, &args, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "อันดับ - ชื่อวง - dBm - คำตัดสิน", LEFT_X,
                           HEADER_Y, COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(ui, "กำลังสแกน... รอสักครู่", LEFT_X, NOTE_Y,
                           COLOR_WARN, note);
    }
    if (status == BENTO_OK) {
        flush_screen(ui);
    }
    return status;
}

/* The bar's length is the strength and its colour the verdict, the
 * same colour as the text: a Bar's color() paints the track, not
 * the fill. It is made at 0 and then set: a Bar made with a value
 * blanked the whole screen. */
static bento_status_t add_bar(bento_ui_t *ui, int16_t y,
                              int32_t rssi_dbm)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t bar;
    bento_status_t status;

    bento_ui_widget_args_init(&args);
    args.x = LEFT_X;
    args.y = y;
    args.w = BAR_W;
    args.h = BAR_H;
    args.max_val = PERCENT;
    args.color = color_of(rssi_dbm);
    status = bento_ui_bar_create(ui, &args, &bar);
    if (status == BENTO_OK) { /* setters ignore a refusal (modui.c) */
        (void)bento_ui_set_value(ui, bar, percent_of(rssi_dbm));
    }
    return status;
}

/* The strongest network is not the one to join: it may be the shop
 * next door. Choose by name, and let the strength say whether the
 * board's place works. Two labels keep each under the 126-byte
 * ceiling of a ui.Label (a Thai letter takes 3 bytes). */
static bento_status_t show_strongest(bento_ui_t *ui, bento_ui_id_t note,
                                     const bento_wifi_ap_t *best)
{
    const int32_t rssi_dbm = (int32_t)best->rssi_dbm;
    char name[NAME_BYTES];
    char line[LINE_BYTES];
    bento_ui_id_t unused;
    bento_status_t status;

    (void)bento_ui_set_color(ui, note, color_of(rssi_dbm));
    cut_name(name, best->ssid, CONSOLE_CHARS, 0);
    (void)snprintf(line, sizeof(line), "แรงที่สุด: %s  %" PRId32 " dBm",
                   name, rssi_dbm);
    (void)bento_ui_set_text(ui, note, line, (uint32_t)strlen(line));
    status = add_label(ui, "เลือกวงด้วยชื่อ", LEFT_X, ADVICE_Y, COLOR_DIM,
                       &unused);
    if (status == BENTO_OK) {
        status = add_label(ui, "ไม่ใช่เลือกวงที่แรงสุดอัตโนมัติ", ADVICE2_X,
                           ADVICE_Y, COLOR_DIM, &unused);
    }
    if (status == BENTO_OK) {
        (void)snprintf(line, sizeof(line),
                       "แรงที่สุดคือ %s ที่ %" PRId32 " dBm", best->ssid,
                       rssi_dbm);
        console_line(line);
        console_line("เลือกด้วยชื่อวง ไม่ใช่ด้วยความแรง");
    }
    return status;
}

/* The scan's answers: too big for the 8 KB program stack, so at
 * file scope, zeroed by the loader and set again in main()
 * (CLEAN_C.md C9, deviation BENTO-MISRA-E03/E04). */
extern bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];
bento_wifi_ap_t g_networks[BENTO_WIFI_SCAN_MAX];

/* wifi.scan(): a failed scan raised there and ends the run here. */
static bento_status_t scan(uint32_t *count)
{
    bento_status_t status = bento_wifi_scan_start();
    bool waiting = status == BENTO_OK;

    *count = 0U;
    while (waiting) {
        status = bento_wifi_scan_poll(g_networks, BENTO_WIFI_SCAN_MAX,
                                      count);
        waiting = status == BENTO_E_IN_PROGRESS;
        if (waiting) {
            status = check_running(bento_time_sleep_ms(WIFI_SLICE_MS));
            waiting = status == BENTO_OK;
        }
    }
    if (status == BENTO_E_CANCELLED) {
        (void)bento_wifi_scan_cancel(); /* answers BENTO_OK always */
    }
    return status;
}

/* The key the original sorts by: the network's rssi. */
static int32_t rssi_at(const ranking_t *ranking, int32_t place)
{
    return (int32_t)g_networks[ranking->order[place]].rssi_dbm;
}

/* One partition step of MicroPython's quicksort with reverse=True
 * (py/objlist.c, mp_quicksort): the last item is the pivot, and the
 * pivot ends at *split. Items before it are not weaker, items after
 * it not stronger. */
static void partition(ranking_t *ranking, int32_t head, int32_t tail,
                      int32_t *split, int32_t *last_left)
{
    const int32_t pivot = rssi_at(ranking, tail);
    int32_t left = head;
    int32_t right = tail;
    bool crossed = false;

    while (!crossed) {
        do {
            left++;
        } while ((left < right) && (rssi_at(ranking, left) >= pivot));
        do {
            right--;
        } while ((left < right) && (rssi_at(ranking, right) <= pivot));
        crossed = left >= right;
        if (!crossed) {
            swap_places(ranking, left, right);
        }
    }
    swap_places(ranking, left, tail);
    *split = left;
    *last_left = right;
}

/* The ranges still to sort: MicroPython's recursion, written as a
 * stack (MISRA 17.2). */
typedef struct {
    int32_t heads[BENTO_WIFI_SCAN_MAX];
    int32_t tails[BENTO_WIFI_SCAN_MAX];
    uint32_t pending;
} range_stack_t;

/* Sorts one range down to its last piece, parking the smaller side
 * of each split on the stack. head is one before the first item. */
static void sort_range(ranking_t *ranking, range_stack_t *stack,
                       int32_t first_head, int32_t first_tail)
{
    int32_t head = first_head;
    int32_t tail = first_tail;

    while ((tail - head) > 1) {
        const uint32_t top = stack->pending;
        int32_t split;
        int32_t last_left;

        partition(ranking, head, tail, &split, &last_left);
        if ((last_left - head) < (tail - split)) {
            stack->heads[top] = head; /* the smaller side waits */
            stack->tails[top] = last_left;
            head = split;
        } else {
            stack->heads[top] = split;
            stack->tails[top] = tail;
            tail = last_left;
        }
        /* a side of one item or none is sorted already; the rest
         * are disjoint, so at most count / 2 wait */
        if ((stack->tails[top] - stack->heads[top]) > 1) {
            stack->pending++;
        }
    }
}

/* Strongest first, in MicroPython's own order: its sort is a
 * quicksort, not stable, so networks of equal strength come out as
 * there. The ranges are disjoint, so the order they are done in does
 * not change the result. */
static void rank(ranking_t *ranking, uint32_t count)
{
    range_stack_t stack;
    uint32_t i;

    ranking->count = count;
    for (i = 0U; i < count; i++) {
        ranking->order[i] = (uint8_t)i;
    }
    stack.heads[0] = -1;
    stack.tails[0] = (int32_t)count - 1;
    stack.pending = 1U;
    while (stack.pending > 0U) {
        stack.pending--;
        sort_range(ranking, &stack, stack.heads[stack.pending],
                   stack.tails[stack.pending]);
    }
}

/* One place on screen: the text, its bar, and the two reports. */
static bento_status_t
show_place(bento_ui_t *ui, const ranking_t *ranking, uint32_t index)
{
    const bento_wifi_ap_t *network = &g_networks[ranking->order[index]];
    const int32_t rssi_dbm = (int32_t)network->rssi_dbm;
    const int32_t place = (int32_t)index + 1;
    const int32_t y = ROW_Y + ((int32_t)index * ROW_STEP);
    char name[NAME_BYTES];
    char line[LINE_BYTES];
    bento_ui_id_t unused;
    bento_status_t status;

    cut_name(name, network->ssid, LABEL_CHARS, 0);
    (void)snprintf(line, sizeof(line),
                   "%" PRId32 ". %s  %" PRId32 " dBm  %s", place, name,
                   rssi_dbm, verdict_of(rssi_dbm));
    status = add_label(ui, line, LEFT_X, (int16_t)y, color_of(rssi_dbm),
                       &unused);
    if (status == BENTO_OK) {
        status = add_bar(ui, (int16_t)(y + BAR_DY), rssi_dbm);
    }
    if (status == BENTO_OK) {
        flush_screen(ui);
        report_place(place, network->ssid, rssi_dbm);
    }
    return status;
}

/* The top TOP_N places; fewer when fewer networks answered, which
 * also keeps the widget quota (a Label and a Bar per place). */
static bento_status_t show_ranking(bento_ui_t *ui, bento_ui_id_t note,
                                   const ranking_t *ranking)
{
    const uint32_t shown = (ranking->count < (uint32_t)TOP_N)
                               ? ranking->count
                               : (uint32_t)TOP_N;
    const char *empty = "ไม่เจอวงไหนเลย ลองย้ายบอร์ดออกที่โล่ง";
    bento_status_t status = BENTO_OK;
    uint32_t i;

    (void)printf("อันดับ  ชื่อวง                    dBm  ความแรง\n");
    for (i = 0U; (i < shown) && (status == BENTO_OK); i++) {
        status = show_place(ui, ranking, i);
    }
    if ((status == BENTO_OK) && (ranking->count > 0U)) {
        status =
            show_strongest(ui, note, &g_networks[ranking->order[0]]);
    } else if (status == BENTO_OK) {
        (void)bento_ui_set_color(ui, note, COLOR_BAD);
        (void)bento_ui_set_text(ui, note, empty,
                                (uint32_t)strlen(empty));
    } else {
        /* a widget could not be made: its status ends the run */
    }
    if (status == BENTO_OK) {
        flush_screen(ui);
    }
    return status;
}

int main(void)
{
    bento_ui_t *ui = bento_runtime_get_ui();
    ranking_t ranking = {0};
    bento_ui_id_t note = BENTO_UI_ID_INVALID;
    uint32_t count = 0U;
    bento_status_t status;

    (void)memset(g_networks, 0, sizeof(g_networks));
    (void)bento_lcd_clear(); /* lcd calls ignore their result */
    (void)bento_lcd_console("<h2>คาบ 9 - จัดอันดับความแรง</h2>");
    status = build_screen(ui, &note);
    if (status == BENTO_OK) {
        status = scan(&count);
    }
    if (status == BENTO_OK) {
        rank(&ranking, count);
        status = show_ranking(ui, note, &ranking);
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
