/*
 * 01_models_on_board.c -- บนบอร์ดมีโมเดล AI อะไรบ้าง
 * (which AI models are on this board)
 *
 * อ่านรายการโมเดล Edge AI ทั้งหมดบนบอร์ด พิมพ์ลง Console หนึ่งบรรทัดต่อหนึ่งโมเดล
 * พร้อมชื่อคลาสครบ แล้วขึ้นจอหนึ่งแถวต่อหนึ่งโมเดล (สองคอลัมน์ คอลัมน์ละเก้าแถว)
 * ลำดับ | ชื่อ | ที่มา | เซนเซอร์ | จำนวนคลาส เขียว = ส่งมาจาก Store ฟ้า = ติดมากับ
 * บอร์ด ลำดับไม่ใช่ชื่อถาวร โปรแกรมจริงจึงหาโมเดลด้วยชื่อ กด Stop เพื่อจบ
 *
 * Reads every Edge AI model on the board and prints one line per model
 * on the Console with all its class names, then shows one row per
 * model on the screen (two columns of nine rows): index | name |
 * source | sensor | class count, green for a model from the Store and
 * blue for one built into the board. The index is not a lasting name,
 * so real programs find a model by its name. Press Stop to end.
 *
 * Twin of 01_models_on_board.py
 * (embedded-systems-for-aiot-developer/examples/s13/
 *  01_models_on_board.py).
 * Style: CLEAN_C.md.
 * What differs from MicroPython only because the language forces it:
 *  - edge_ai.models() is bento_edgeai_count() and model_info() per
 *    row, read again for each pass instead of kept in a list; a row's
 *    index is its place in the table;
 *  - the "builtin" key is not in the C model info: a row below the
 *    board's count of compiled models (edge_ai.diag) is built-in, a
 *    later row is from the Store, and "?" is shown when that count
 *    cannot be read (as the Emulator, which has no builtin key);
 *  - the labels list is printed as print() writes a list of str;
 *  - import edge_ai fails where there is no model link: here
 *    bento_edgeai_supported() false ends with BENTO_E_UNSUPPORTED;
 *  - Stop raises KeyboardInterrupt in MicroPython; here the sleep
 *    returns and the program ends at that point.
 * In the BENTO Emulator, Edge AI from C is not available yet: the
 * program says so and ends.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bento_edgeai.h"
#include "bento_runtime.h"

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

/* Rows, timing and text sizes. */
enum {
    ROWS = 9,         /* rows per column, two columns */
    SENSOR_COUNT = 3, /* the sensor field's values 0, 1 and 2 */
    SETTLE_MS = 150,  /* let the cleared screen settle */
    POLL_MS = 200,
    UNKNOWN_COUNT = -1, /* the board cannot say how many are compiled */
    TEXT_CHARS = 160
};

/* Colours, 0xRRGGBB. */
enum {
    COLOR_TEXT = 0xE8EAED,
    COLOR_DIM = 0x9AA3AF,
    COLOR_STORE = 0x30A46C,
    COLOR_BUILTIN = 0x4A9EFF
};

/* Layout on the 792 x 398 canvas. */
enum {
    LEFT_X = 12,
    COLUMN_W = 392,
    TITLE_Y = 6,
    HEADER_Y = 36,
    FIRST_ROW_Y = 64,
    ROW_H = 30,
    MORE_Y = 340,
    TITLE_PX = 20,
    ROW_PX = 16
};

/* Where a model came from. */
typedef enum {
    SOURCE_UNKNOWN = 0,
    SOURCE_BUILTIN = 1,
    SOURCE_STORE = 2
} source_t;

/* Where a label sits and how it looks. */
typedef struct {
    int16_t x;
    int16_t y;
    uint32_t color;
    int32_t size_px;
} label_look_t;

/* The board's model table as the passes read it. */
typedef struct {
    int32_t count;
    int32_t compiled; /* UNKNOWN_COUNT when the board cannot say */
    int32_t from_store;
} model_table_t;

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
                                label_look_t look)
{
    bento_ui_widget_args_t args;
    bento_ui_id_t unused = 0U;

    bento_ui_widget_args_init(&args);
    args.text = text;
    args.text_len = (uint32_t)strlen(text);
    args.x = look.x;
    args.y = look.y;
    args.color = look.color;
    args.font_px = look.size_px;
    return bento_ui_label_create(ui, &args, &unused);
}

/* A position on the canvas, as the widget API takes it. */
static int16_t px(int32_t value)
{
    return (int16_t)value;
}

/* A ui.poll() whose events the original throws away. */
static void poll_drop(bento_ui_t *ui)
{
    bento_ui_event_t events[BENTO_UI_POLL_EVENTS_MAX];
    uint32_t count = 0U;

    (void)bento_ui_poll(ui, events, BENTO_UI_POLL_EVENTS_MAX, &count,
                        NULL); /* the events are not read, as before */
}

static source_t source_of(const model_table_t *table, int32_t index)
{
    source_t source = SOURCE_UNKNOWN;

    if (table->compiled != UNKNOWN_COUNT) {
        source =
            (index < table->compiled) ? SOURCE_BUILTIN : SOURCE_STORE;
    }
    return source;
}

static const char *source_text(source_t source)
{
    static const char *const names[] = {"?", "built-in", "Store"};

    return names[source];
}

static uint32_t source_color(source_t source)
{
    static const uint32_t colors[] = {COLOR_DIM, COLOR_BUILTIN,
                                      COLOR_STORE};

    return colors[source];
}

/* The row count, how many rows the firmware compiled in, and so how
 * many came from the Store. */
static bento_status_t read_table(model_table_t *table)
{
    bento_edgeai_diag_t diag;
    bento_status_t status = bento_edgeai_count(&table->count);

    table->compiled = UNKNOWN_COUNT;
    table->from_store = 0;
    bento_edgeai_diag_init(&diag);
    if ((status == BENTO_OK) &&
        (bento_edgeai_diag(&diag) == BENTO_OK)) {
        table->compiled = (int32_t)diag.compiled_models;
    }
    if ((status == BENTO_OK) && (table->compiled != UNKNOWN_COUNT) &&
        (table->count > table->compiled)) {
        table->from_store = table->count - table->compiled;
    }
    return status;
}

/* One row: index, name, source, sensor and class count. */
static void row_text(char *text, uint32_t capacity,
                     const model_table_t *table, int32_t index,
                     const bento_edgeai_model_info_t *info)
{
    static const char *const sensor_names[SENSOR_COUNT] = {
        "IMU", "radar", "mic"};
    const char *sensor = (info->sensor < (uint8_t)SENSOR_COUNT)
                             ? sensor_names[info->sensor]
                             : "?";

    (void)snprintf(text, capacity, "%2" PRId32 "  %s  %s  %s  %u",
                   index, info->name,
                   source_text(source_of(table, index)), sensor,
                   (unsigned int)info->class_count);
}

/* repr() of a str: in quotes, ' unless it holds a ' and no ", with
 * the quote, the backslash and control bytes escaped. */
static void print_repr(const char *text)
{
    const char quote =
        ((strchr(text, '\'') != NULL) && (strchr(text, '"') == NULL))
            ? '"'
            : '\'';
    const uint8_t printable_min = 0x20U;
    const uint8_t delete_byte = 0x7FU;
    size_t i;

    (void)printf("%c", quote);
    for (i = 0U; text[i] != '\0'; i++) {
        const uint8_t byte = (uint8_t)text[i];

        if ((text[i] == quote) || (text[i] == '\\')) {
            (void)printf("\\%c", text[i]);
        } else if ((byte >= printable_min) && (byte != delete_byte)) {
            (void)printf("%c", text[i]);
        } else {
            (void)printf("\\x%02x", (unsigned int)byte);
        }
    }
    (void)printf("%c", quote);
}

/* The model's class names as print() writes a list: ['a', 'b']; a
 * name the board does not give is "". */
static void print_labels(uint32_t index, uint32_t class_count)
{
    char label[BENTO_EDGEAI_LABEL_LEN];
    uint32_t length = 0U;
    uint32_t i;

    (void)printf("[");
    for (i = 0U; i < class_count; i++) {
        if (bento_edgeai_label(index, i, label, (uint32_t)sizeof(label),
                               &length) != BENTO_OK) {
            label[0] = '\0';
        }
        (void)printf("%s", (i > 0U) ? ", " : "");
        print_repr(label);
    }
    (void)printf("]\n");
}

/* The Console gets every row with all its class names. */
static bento_status_t print_models(const model_table_t *table)
{
    bento_edgeai_model_info_t info;
    char text[TEXT_CHARS];
    bento_status_t status = BENTO_OK;
    int32_t index;

    (void)printf("models: %" PRId32 " | from Store: %" PRId32 "\n",
                 table->count, table->from_store);
    for (index = 0; (index < table->count) && (status == BENTO_OK);
         index++) {
        bento_edgeai_model_info_init(&info);
        status = bento_edgeai_model_info((uint32_t)index, &info);
        if (status == BENTO_OK) {
            row_text(text, (uint32_t)sizeof(text), table, index, &info);
            (void)printf("%s ", text);
            print_labels((uint32_t)index, info.class_count);
        }
    }
    return status;
}

/* The title and the column names. */
static bento_status_t add_heading(bento_ui_t *ui,
                                  const model_table_t *table)
{
    char text[TEXT_CHARS];
    bento_status_t status;

    (void)snprintf(text, sizeof(text),
                   "Models on this board: %" PRId32 "  (Store %" PRId32
                   ")",
                   table->count, table->from_store);
    status = add_label(
        ui, text,
        (label_look_t){LEFT_X, TITLE_Y, COLOR_TEXT, TITLE_PX});
    if (status == BENTO_OK) {
        status = add_label(
            ui, "#  name  source  sensor  classes",
            (label_look_t){LEFT_X, HEADER_Y, COLOR_DIM, ROW_PX});
    }
    return status;
}

/* One row per model, at most two columns of ROWS, then a note for the
 * rest. */
static bento_status_t add_rows(bento_ui_t *ui,
                               const model_table_t *table)
{
    bento_edgeai_model_info_t info;
    char text[TEXT_CHARS];
    const int32_t shown =
        (table->count < (ROWS * 2)) ? table->count : (ROWS * 2);
    bento_status_t status = BENTO_OK;
    int32_t index;

    for (index = 0; (index < shown) && (status == BENTO_OK); index++) {
        const int32_t column = index / (int32_t)ROWS;
        const int32_t row = index % (int32_t)ROWS;
        const int16_t x =
            px((int32_t)LEFT_X + (column * (int32_t)COLUMN_W));
        const int16_t y =
            px((int32_t)FIRST_ROW_Y + (row * (int32_t)ROW_H));

        bento_edgeai_model_info_init(&info);
        status = bento_edgeai_model_info((uint32_t)index, &info);
        if (status == BENTO_OK) {
            row_text(text, (uint32_t)sizeof(text), table, index, &info);
            status = add_label(
                ui, text,
                (label_look_t){x, y,
                               source_color(source_of(table, index)),
                               ROW_PX});
        }
    }
    if ((status == BENTO_OK) && (table->count > (ROWS * 2))) {
        (void)snprintf(text, sizeof(text),
                       "+%" PRId32 " more - see Console",
                       table->count - (ROWS * 2));
        status = add_label(
            ui, text,
            (label_look_t){LEFT_X, MORE_Y, COLOR_DIM, ROW_PX});
    }
    return status;
}

/* Pressing Stop in the IDE ends the program. */
static bento_status_t wait_for_stop(bento_ui_t *ui)
{
    bento_status_t status = BENTO_OK;

    while (status == BENTO_OK) {
        poll_drop(ui);
        status = pause_ms(POLL_MS);
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
    bento_ui_t *ui = bento_runtime_get_ui();
    model_table_t table = {0, UNKNOWN_COUNT, 0};
    bool supported = false;
    bool in_emulator = false;
    bento_status_t status = bento_edgeai_supported(&supported);

    in_emulator = status == BENTO_E_UNSUPPORTED;
    if (in_emulator) {
        (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                              BENTO_RUNTIME_SCREEN_HEIGHT_PX);
        add_emulator_note(ui);
        print_emulator_note();
    }

    if ((status == BENTO_OK) && !supported) {
        status = BENTO_E_UNSUPPORTED; /* import edge_ai fails */
    }
    if (status == BENTO_OK) {
        status = read_table(&table);
    }
    if (status == BENTO_OK) {
        status = print_models(&table);
    }
    if (status == BENTO_OK) {
        /* ui.screen() ignores a refusal in MicroPython, so the twin
         * does too. */
        (void)bento_ui_screen(ui, BENTO_RUNTIME_SCREEN_WIDTH_PX,
                              BENTO_RUNTIME_SCREEN_HEIGHT_PX);
        status = pause_ms(SETTLE_MS);
    }
    if (status == BENTO_OK) {
        status = add_heading(ui, &table);
    }
    if (status == BENTO_OK) {
        status = add_rows(ui, &table);
    }
    if (status == BENTO_OK) {
        status = wait_for_stop(ui);
    }
    return (in_emulator || (status == BENTO_E_CANCELLED)) ? 0
                                                          : (int)status;
}
