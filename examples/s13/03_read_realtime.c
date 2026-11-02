/*
 * 03_read_realtime.c -- อ่านผล AI สด ๆ ให้ถูกวิธี
 * (reading live AI results the right way)
 *
 * หาโมเดลด้วยชื่อ (AnomalousVibration ก่อน ไม่มีค่อยใช้ Motion Detection) แล้ว
 * select() ให้รัน อ่าน result() ทุก 50 ms ซึ่งถี่กว่าที่โมเดลตอบโดยตั้งใจ แล้วนับว่า
 * อ่านได้ None (ยังไม่มีผล) ผลเดิม (seq เท่าเดิม) ผลใหม่ หรือผลของโมเดลอื่น จอแสดง
 * ผลล่าสุดกับ seq และ latency ทุกครึ่งวินาที ครบ 20 วินาทีโปรแกรม stop() แล้วอ่าน
 * อีกครั้ง ดูช่อง running พิมพ์สรุปจำนวนผลใหม่ต่อวินาที แล้วค้างจอไว้ 10 วินาที
 *
 * Finds a model by name (AnomalousVibration first, else Motion
 * Detection) and select()s it. It reads result() every 50 ms, faster
 * than the model answers on purpose, and counts the reads that gave
 * None (no result yet), the same result (seq unchanged), a new result,
 * or another model's result. Every half second the screen shows the
 * latest result with its seq and latency. After 20 seconds it calls
 * stop(), reads once more to show the running field, prints the new
 * results per second and the mean latency, and keeps the screen 10 s.
 *
 * Twin of 03_read_realtime.py
 * (embedded-systems-for-aiot-developer/examples/s13/03_read_realtime.py).
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - edge_ai.models() is bento_edgeai_count() and model_info() per
 *    row; a row's index is its place in the table;
 *  - result() is None when the board has no verdict (has_verdict
 *    false) or the read fails; conf is score_pct[top] in percent, the
 *    label is bento_edgeai_label(), latency_ms is latency_us / 1000;
 *  - select() and stop() wait inside the binding; here they are
 *    started and polled. stop() answers True when the core stopped,
 *    False on its time limit, None when the OSError was passed over;
 *  - print() of the dict n writes its keys in the order they were
 *    written (MicroPython's dict may print another order);
 *  - floats are bento_real_t and "%.1f" / "%.2f" go through bento_fmt,
 *    the text the board's MicroPython writes;
 *  - an OSError from select() ends the program after the finally; here
 *    its status is returned after the model is stopped;
 *  - import edge_ai fails where there is no model link: here
 *    bento_edgeai_supported() false ends with BENTO_E_UNSUPPORTED;
 *  - Stop raises KeyboardInterrupt in MicroPython; here the wait
 *    returns and the program ends after the same finally.
 * In the BENTO Emulator, Edge AI from C is not available yet: the
 * program says so, then goes on as the original does when no
 * model is on the board.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_edgeai.h"
#include "bento_fmt.h"
#include "bento_runtime.h"

/* ---- แก้ก่อนรัน: settings (MISRA deviation CT02) ---- */
enum {
    RUN_S = 20,   /* read this many seconds, then stop */
    READ_MS = 50, /* faster than the model answers, on purpose */
    DRAW_MS = 500 /* write the screen every ... ms */
};
/* ---- end of settings ---- */

/* The note for the BENTO Emulator, whose C runtime has no Edge AI yet. */
static const char EMULATOR_NOTE[] =
    "Edge AI จาก C ยังไม่มีใน BENTO Emulator ให้รันบน Dev Kit หรือ Eva Kit";

/* Where that note sits on the screen. */
enum {
    EMULATOR_NOTE_X = 12,
    EMULATOR_NOTE_Y = 180,
    EMULATOR_NOTE_PX = 20,
    EMULATOR_NOTE_COLOR = 0xF5A623
};

/* Timing. */
enum {
    SETTLE_MS = 150, /* let the cleared screen settle */
    OP_POLL_MS = 100,
    HOLD_POLLS = 50, /* the screen stays 50 x 200 ms to read */
    HOLD_POLL_MS = 200,
    MS_PER_S = 1000,
    US_PER_MS = 1000,
    MODEL_KEY_COUNT = 2,
    TEXT_CHARS = 160,
    /* "label " + a label (47 bytes) + " running False" + NUL */
    AFTER_CHARS = 72,
    /* "After stop(): " + the AFTER_CHARS text */
    END_CHARS = 88
};

/* What a read gave, in the order of the dict n. */
enum {
    READ_NONE = 0,
    READ_SAME = 1,
    READ_NEW = 2,
    READ_OTHER = 3,
    READ_KINDS = 4
};

/* What stop() answered. */
enum {
    STOP_NONE = 0, /* the OSError was passed over */
    STOP_TRUE = 1,
    STOP_FALSE = 2
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_OK = 0x30A46C,
    COLOR_DIM = 0x9AA3AF,
    COLOR_WARN = 0xF5A623
};

/* Layout on the 792 x 398 canvas. */
enum {
    LEFT_X = 12,
    TITLE_Y = 6,
    RESULT_Y = 56,
    INFO_Y = 110,
    COUNT_Y = 150,
    END_Y = 200,
    SMALL_PX = 16,
    TEXT_PX = 20,
    BIG_PX = 28
};

/* One edge_ai.result(), or none (present false). */
typedef struct {
    bool present;
    bool running;
    uint8_t index;
    uint32_t sequence;
    int32_t conf_pct;
    bento_real_t latency_ms;
    char label[BENTO_EDGEAI_LABEL_LEN];
} reading_t;

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

/* The model, the widgets and the counts. */
typedef struct {
    bento_ui_t *ui;
    uint32_t model;
    char name[BENTO_EDGEAI_NAME_LEN];
    bento_ui_id_t result_label;
    bento_ui_id_t info_label;
    bento_ui_id_t count_label;
    bento_ui_id_t end_label;
    int32_t reads[READ_KINDS];
    bool has_sequence;
    uint32_t last_sequence;
    bento_real_t latency_sum_ms;
    bool has_fresh;
    reading_t fresh;
    uint32_t start_ms;
} reader_t;

/* time.sleep_ms(): BENTO_E_CANCELLED when Stop arrives, as the
 * original's KeyboardInterrupt. */
static bento_status_t pause_ms(uint32_t duration_ms)
{
    bento_status_t status = bento_time_sleep_ms(duration_ms);

    if ((status == BENTO_OK) && !bento_runtime_is_running()) {
        status = BENTO_E_CANCELLED;
    }
    return status;
}

static bento_status_t add_label(bento_ui_t *ui, const char *text,
                                label_look_t look,
                                bento_ui_id_t *out_id)
{
    bento_ui_widget_args_t args;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, out_id);
}

/* MicroPython's widget setters ignore a refusal (modui.c). */
static void set_text(bento_ui_t *ui, bento_ui_id_t widget,
                     const char *text)
{
    (void)bento_ui_set_text(ui, widget, text, (uint32_t)strlen(text));
}

/* A ui.poll() whose events the original throws away. */
static void poll_drop(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL); /* the events are not read, as before */
}

/* The first model whose name matches, in the order of the names. */
static bento_status_t find_model(reader_t *reader, bool *found)
{
    static const char *const keys[MODEL_KEY_COUNT] = {
        "AnomalousVibration", "Motion Detection"};
    bento_edgeai_model_info_t info;
    int32_t count = 0;
    uint32_t key;
    int32_t row;
    bento_status_t status = bento_edgeai_count(&count);

    *found = false;
    for (key = 0U; (key < (uint32_t)MODEL_KEY_COUNT) && !*found &&
                   (status == BENTO_OK);
         key++) {
        for (row = 0; (row < count) && !*found && (status == BENTO_OK);
             row++) {
            bento_edgeai_model_info_init(&info);
            status = bento_edgeai_model_info((uint32_t)row, &info);
            *found = (status == BENTO_OK) &&
                     (strcmp(info.name, keys[key]) == 0);
            reader->model = (uint32_t)row;
        }
    }
    if (*found) {
        (void)snprintf(reader->name, sizeof(reader->name), "%s",
                       info.name);
    }
    return status;
}

/* Waits for a started select or stop; Stop cancels it. */
static bento_status_t finish_op(void)
{
    bento_status_t status = BENTO_E_IN_PROGRESS;

    while (status == BENTO_E_IN_PROGRESS) {
        status = pause_ms(OP_POLL_MS);
        if (status == BENTO_OK) {
            status = bento_edgeai_op_poll(NULL);
        } else {
            (void)bento_edgeai_op_cancel(); /* Stop wins */
        }
    }
    return status;
}

/* The finally: the AI core is always released. It waits even after
 * Stop, as the binding does; an OSError is passed over (STOP_NONE). */
static int32_t stop_model(void)
{
    int32_t answer = STOP_NONE;
    bento_status_t status = bento_edgeai_stop_start();

    if (status == BENTO_OK) {
        status = BENTO_E_IN_PROGRESS;
    }
    while (status == BENTO_E_IN_PROGRESS) {
        /* the IDE's Stop may already be in: the wait is not checked */
        (void)bento_time_sleep_ms(OP_POLL_MS);
        status = bento_edgeai_op_poll(NULL);
    }
    if (status == BENTO_OK) {
        answer = STOP_TRUE;
    } else if (status == BENTO_E_TIMEOUT) {
        answer = STOP_FALSE;
    } else {
        answer = STOP_NONE;
    }
    return answer;
}

static const char *stop_text(int32_t answer)
{
    const char *text = "None";

    if (answer == STOP_TRUE) {
        text = "True";
    } else if (answer == STOP_FALSE) {
        text = "False";
    } else {
        text = "None";
    }
    return text;
}

/* edge_ai.result(): present false is Python's None. */
static void read_result(reading_t *reading)
{
    bento_edgeai_result_t result;

    bento_edgeai_result_init(&result);
    reading->present = (bento_edgeai_result(&result) == BENTO_OK) &&
                       result.has_verdict &&
                       (result.top < BENTO_EDGEAI_MAX_CLASSES);
    if (reading->present) {
        reading->running = result.running;
        reading->index = result.index;
        reading->sequence = result.seq;
        reading->conf_pct = (int32_t)result.score_pct[result.top];
        reading->latency_ms =
            (bento_real_t)result.latency_us / (bento_real_t)US_PER_MS;
        if (bento_edgeai_label(result.index, result.top, reading->label,
                               (uint32_t)sizeof(reading->label),
                               NULL) != BENTO_OK) {
            reading->label[0] = '\0';
        }
    }
}

static bento_status_t build_screen(reader_t *reader)
{
    char title[TEXT_CHARS];
    bento_ui_id_t unused = 0U;
    bento_status_t status;

    /* ui.screen() ignores a refusal in MicroPython, so the twin does
     * too. */
    (void)bento_ui_screen(reader->ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                          BENTO_RUNTIME_SCREEN_HEIGHT_PX);
    status = pause_ms(SETTLE_MS);
    if (status == BENTO_OK) {
        (void)snprintf(title, sizeof(title), "Read results: %s",
                       reader->name);
        status = add_label(
            reader->ui, title,
            (label_look_t){LEFT_X, TITLE_Y, COLOR_TEXT, TEXT_PX},
            &unused);
    }
    if (status == BENTO_OK) {
        status = add_label(
            reader->ui, "-",
            (label_look_t){LEFT_X, RESULT_Y, COLOR_OK, BIG_PX},
            &reader->result_label);
    }
    if (status == BENTO_OK) {
        status = add_label(
            reader->ui, " ",
            (label_look_t){LEFT_X, INFO_Y, COLOR_DIM, SMALL_PX},
            &reader->info_label);
    }
    if (status == BENTO_OK) {
        status = add_label(
            reader->ui, " ",
            (label_look_t){LEFT_X, COUNT_Y, COLOR_TEXT, SMALL_PX},
            &reader->count_label);
    }
    if (status == BENTO_OK) {
        status = add_label(
            reader->ui, " ",
            (label_look_t){LEFT_X, END_Y, COLOR_WARN, SMALL_PX},
            &reader->end_label);
    }
    if (status == BENTO_OK) {
        poll_drop(reader->ui);
    }
    return status;
}

/* Counts one read: none yet, another model's, the same seq, or new. */
static void count_read(reader_t *reader, const reading_t *reading)
{
    if (!reading->present) {
        reader->reads[READ_NONE]++;
    } else if (reading->index != reader->model) {
        reader->reads[READ_OTHER]++;
    } else if (reader->has_sequence &&
               (reading->sequence == reader->last_sequence)) {
        reader->reads[READ_SAME]++;
    } else {
        reader->reads[READ_NEW]++;
        reader->has_sequence = true;
        reader->last_sequence = reading->sequence;
        reader->latency_sum_ms += reading->latency_ms;
        reader->fresh = *reading;
        reader->has_fresh = true;
    }
}

/* The newest result (once) and the counts. */
static void draw_reads(reader_t *reader)
{
    char text[TEXT_CHARS];
    char latency[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;

    if (reader->has_fresh) {
        (void)snprintf(text, sizeof(text), "%s  %" PRId32 "%%",
                       reader->fresh.label, reader->fresh.conf_pct);
        set_text(reader->ui, reader->result_label, text);
        (void)bento_fmt_real_precision(
            latency, (uint32_t)sizeof(latency),
            reader->fresh.latency_ms, 2, &length);
        (void)snprintf(text, sizeof(text),
                       "seq %" PRIu32 " | latency %s ms",
                       reader->fresh.sequence, latency);
        set_text(reader->ui, reader->info_label, text);
        reader->has_fresh = false;
    }
    (void)snprintf(text, sizeof(text),
                   "reads: None %" PRId32 "  same %" PRId32
                   "  new %" PRId32 "  other %" PRId32,
                   reader->reads[READ_NONE], reader->reads[READ_SAME],
                   reader->reads[READ_NEW], reader->reads[READ_OTHER]);
    set_text(reader->ui, reader->count_label, text);
}

/* RUN_S of reads every READ_MS, the screen every DRAW_MS. */
static bento_status_t read_for_a_while(reader_t *reader)
{
    reading_t reading;
    uint32_t drawn_ms = bento_time_ticks_ms();
    bento_status_t status = BENTO_OK;

    reader->start_ms = bento_time_ticks_ms();
    while ((status == BENTO_OK) &&
           (bento_time_ticks_diff(bento_time_ticks_ms(),
                                  reader->start_ms) <
            ((int32_t)RUN_S * (int32_t)MS_PER_S))) {
        const uint32_t now_ms = bento_time_ticks_ms();

        read_result(&reading);
        count_read(reader, &reading);
        if (bento_time_ticks_diff(now_ms, drawn_ms) >= DRAW_MS) {
            drawn_ms = now_ms;
            draw_reads(reader);
        }
        poll_drop(reader->ui);
        status = pause_ms(READ_MS);
    }
    return status;
}

/* The model, read under the finally that stops it. *stopped is what
 * stop() answered. */
static bento_status_t run_model(reader_t *reader, int32_t *stopped)
{
    bento_status_t status = bento_edgeai_select_start(reader->model);

    if (status == BENTO_OK) {
        status = finish_op();
    }
    if (status == BENTO_OK) {
        status = read_for_a_while(reader);
    }
    *stopped = stop_model();
    return status;
}

/* The counts, and the rate and mean latency of the new results. */
static void print_summary(const reader_t *reader)
{
    const bento_real_t elapsed_s =
        (bento_real_t)bento_time_ticks_diff(bento_time_ticks_ms(),
                                            reader->start_ms) /
        (bento_real_t)MS_PER_S;
    const int32_t fresh = reader->reads[READ_NEW];
    char rate[BENTO_FMT_BUF_SIZE];
    char mean[BENTO_FMT_BUF_SIZE];
    uint32_t length = 0U;

    (void)printf("reads: {'None': %" PRId32 ", 'same': %" PRId32
                 ", 'new': %" PRId32 ", 'other': %" PRId32 "}\n",
                 reader->reads[READ_NONE], reader->reads[READ_SAME],
                 fresh, reader->reads[READ_OTHER]);
    if (fresh != 0) {
        (void)bento_fmt_real_precision(rate, (uint32_t)sizeof(rate),
                                       (bento_real_t)fresh / elapsed_s,
                                       1, &length);
        (void)bento_fmt_real_precision(
            mean, (uint32_t)sizeof(mean),
            reader->latency_sum_ms / (bento_real_t)fresh, 2, &length);
        (void)printf("new results %s /s | mean latency %s ms\n", rate,
                     mean);
    }
}

/* One more read after stop(): an old result may still be there, with
 * running False. */
static void show_after_stop(const reader_t *reader, int32_t stopped)
{
    char after[AFTER_CHARS];
    char text[END_CHARS];
    reading_t reading;

    read_result(&reading);
    (void)snprintf(after, sizeof(after), "None");
    if (reading.present) {
        (void)snprintf(after, sizeof(after), "label %s running %s",
                       reading.label,
                       reading.running ? "True" : "False");
    }
    (void)printf("stop() -> %s | result() after stop: %s\n",
                 stop_text(stopped), after);
    (void)snprintf(text, sizeof(text), "After stop(): %s", after);
    set_text(reader->ui, reader->end_label, text);
}

/* The screen stays HOLD_POLLS x HOLD_POLL_MS (10 s) to read. */
static bento_status_t hold_screen(bento_ui_t *ui)
{
    bento_status_t status = BENTO_OK;
    int32_t poll;

    for (poll = 0; (poll < HOLD_POLLS) && (status == BENTO_OK);
         poll++) {
        poll_drop(ui);
        status = pause_ms(HOLD_POLL_MS);
    }
    return status;
}

/* The BENTO Emulator's C runtime answers BENTO_E_UNSUPPORTED to every
 * Edge AI call: the console says so. */
static void print_emulator_note(void)
{
    (void)printf(
        "%s / Edge AI from C is not in the BENTO Emulator yet: "
        "run it on a Dev Kit or Eva Kit.\n",
        EMULATOR_NOTE);
}

/* The same note as a label on the current screen. It is best effort,
 * as a widget setter is, so its status is not checked. */
static void add_emulator_note(bento_ui_t *ui)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    bento_ui_widget_args_init(&args);
    args.text = EMULATOR_NOTE;
    args.text_len = (uint32_t)strlen(EMULATOR_NOTE);
    args.x = EMULATOR_NOTE_X;
    args.y = EMULATOR_NOTE_Y;
    args.color = EMULATOR_NOTE_COLOR;
    args.font_px = EMULATOR_NOTE_PX;
    (void)bento_ui_label_create(ui, &args, &unused);
}

int main(void)
{
    reader_t reader;
    int32_t stopped = STOP_NONE;
    bool supported = false;
    bool in_emulator = false;
    bool found = false;
    bento_status_t status;

    (void)memset(&reader, 0, sizeof(reader));
    reader.ui = bento_runtime_get_ui();
    status = bento_edgeai_supported(&supported);
    in_emulator = status == BENTO_E_UNSUPPORTED;
    if (in_emulator) {
        (void)bento_ui_screen(reader.ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                              BENTO_RUNTIME_SCREEN_HEIGHT_PX);
        add_emulator_note(reader.ui);
        print_emulator_note();
        status = BENTO_OK; /* on as if no model were on the board */
    }
    if ((status == BENTO_OK) && !supported && !in_emulator) {
        status = BENTO_E_UNSUPPORTED; /* import edge_ai fails */
    }
    if ((status == BENTO_OK) && !in_emulator) {
        status = find_model(&reader, &found);
    }
    if ((status == BENTO_OK) && !found) {
        (void)printf("none of these models is on the board: "
                     "('AnomalousVibration', 'Motion Detection')\n");
    } else if (status == BENTO_OK) {
        status = build_screen(&reader);
        reader.start_ms = bento_time_ticks_ms();
        if (status == BENTO_OK) {
            status = run_model(&reader, &stopped);
        }
        if (status == BENTO_OK) {
            print_summary(&reader);
            show_after_stop(&reader, stopped);
            status = hold_screen(reader.ui);
        }
    } else {
        /* an OSError from edge_ai.models() ends the program */
    }
    return (status == BENTO_E_CANCELLED) ? 0 : (int)status;
}
