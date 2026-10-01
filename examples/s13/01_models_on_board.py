# 01_models_on_board.py - บนบอร์ดมีโมเดล AI อะไรบ้าง
# ชุดตัวอย่างประจำคาบ 11
# ทดสอบแล้ว : Dev Kit เฟิร์มแวร์ 2.4.3 ผ่าน 3/3 ทางปุ่ม Run ของ IDE · Emulator ตรวจเฉพาะบนคอมด้วยโมดูลของ Emulator · Eva Kit ยังไม่ได้ทดสอบ
#
# ไฟล์นี้สอน: edge_ai.models() คืนรายการ dict หนึ่งตัวต่อหนึ่งโมเดล
#             อ่านได้ ชื่อ (name) เซนเซอร์ (sensor) ชื่อคลาส (labels)
#             และ builtin: True = ติดมากับบอร์ด, False = ส่งมาจาก Store
# ดูที่จอ   : หนึ่งแถวต่อหนึ่งโมเดล  ลำดับ | ชื่อ | ที่มา | เซนเซอร์ | จำนวนคลาส
#             เขียว = จาก Store, ฟ้า = ติดมากับบอร์ด  (ชื่อคลาสครบอยู่ใน Console)
# กับดัก    : ลำดับ (index) ไม่ใช่ชื่อถาวร ส่งโมเดลเพิ่มแล้วลำดับเลื่อนได้
#             (วัดบน Dev Kit: AnomalousVibration อยู่ลำดับ 8 หลังส่งเพิ่มอีก 2 ตัวย้ายไป 10)
#             โปรแกรมจริงจึงหาโมเดลด้วยชื่อทุกครั้ง - ดูไฟล์ 02
# ห้าม      : พิมพ์หรือเก็บ id ของโมเดล ใช้แค่ชื่อ
# Emulator  : มีเฉพาะโมเดลในตัว และไม่มีช่อง builtin จึงขึ้น "?"

import edge_ai
import time
import ui

SENSOR = ("IMU", "radar", "mic")     # ค่าช่อง sensor 0 1 2
ROWS = 9                             # แถวต่อคอลัมน์ (2 คอลัมน์)

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_STORE, COL_BUILTIN = 0x30A46C, 0x4A9EFF


def source(m):
    b = m.get("builtin")             # Emulator ไม่มีช่องนี้ ได้ None
    return "built-in" if b else ("Store" if b is False else "?")


def row_text(m):
    s = m.get("sensor", -1)
    return "%2d  %s  %s  %s  %d" % (m["index"], m["name"], source(m),
                                    SENSOR[s] if 0 <= s < 3 else "?", len(m.get("labels") or []))


def main():
    ms = edge_ai.models()
    n_store = sum(1 for m in ms if m.get("builtin") is False)
    print("models:", len(ms), "| from Store:", n_store)
    for m in ms:                     # Console: ชื่อคลาสครบทุกตัว
        print(row_text(m), m.get("labels"))

    ui.screen()
    time.sleep_ms(150)
    ui.Label("Models on this board: %d  (Store %d)" % (len(ms), n_store),
             x=12, y=6, color=COL_TEXT, value=20)
    ui.Label("#  name  source  sensor  classes", x=12, y=36, color=COL_DIM, value=16)
    for i, m in enumerate(ms[:2 * ROWS]):
        col = COL_STORE if m.get("builtin") is False else (COL_BUILTIN if m.get("builtin") else COL_DIM)
        ui.Label(row_text(m), x=12 + (i // ROWS) * 392, y=64 + (i % ROWS) * 30, color=col, value=16)
    if len(ms) > 2 * ROWS:
        ui.Label("+%d more - see Console" % (len(ms) - 2 * ROWS), x=12, y=340, color=COL_DIM, value=16)
    while True:                      # กด Stop ใน IDE เพื่อจบ
        ui.poll()
        time.sleep_ms(200)


main()

# ลองต่อ: 1) นับว่าโมเดลที่ฟังไมค์มีกี่ตัว (sensor == edge_ai.SENSOR_MIC)
# 2) ส่งโมเดลจาก Store เพิ่มหนึ่งตัว แล้วรันไฟล์นี้อีกครั้ง - ลำดับของตัวเดิมเลื่อนไหม?
