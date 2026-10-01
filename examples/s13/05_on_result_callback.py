# 05_on_result_callback.py - ให้บอร์ดเรียกเราเมื่อผลเปลี่ยน (callback) เทียบกับวนอ่านเอง
# ชุดตัวอย่างประจำคาบ 11
# ทดสอบแล้ว : Dev Kit เฟิร์มแวร์ 2.4.4 ผ่าน 3/3 ทางปุ่ม Run ของ IDE · Emulator ตรวจเฉพาะบนคอมด้วยโมดูลของ Emulator · Eva Kit ยังไม่ได้ทดสอบ
# วัดจริง    : AnomalousVibration วางนิ่ง 10 วินาที callback 10 ครั้ง (ราววินาทีละครั้ง)
#             วนอ่านได้ผลใหม่ 142 ผล (ราว 14 ผลต่อวินาที) · ผลเท่ากันทั้ง 3 รอบ
#
# ไฟล์นี้สอน: edge_ai.on_result(fn) = บอร์ดเรียก fn(r) เอง · on_result(None) = เลิก
#             บอร์ดไม่ได้เรียกทุกผล: เรียกเมื่อคลาสที่ชนะเปลี่ยน ถ้าไม่เปลี่ยนเรียกไม่เกินวินาทีละครั้ง
#             และไม่เรียกเลยตอนรันหลายโมเดลพร้อมกัน (ชุดโมเดล)
#             callback ต้องสั้น: เก็บผลไว้แล้วกลับทันที งานจอทำในลูปหลัก
# ดูที่จอ   : สองคอลัมน์ในช่วง WINDOW_S วินาทีเดียวกัน
#             ซ้าย = callback ถูกเรียกกี่ครั้ง · ขวา = วนอ่านได้ผลใหม่ (seq เปลี่ยน) กี่ครั้ง
#             ลองวางนิ่ง แล้วลองเขย่า - ตัวเลขสองฝั่งต่างกันแค่ไหน
# กับดัก    : ถ้าบอร์ดดึงผลเต็มไม่สำเร็จ r ใน callback มีแค่ top conf label
#             (label เป็นตัวเลข ไม่มี seq) - อ่านด้วย r.get() เสมอ
# Emulator  : callback ถูกเรียกทุกผลใหม่ ตอนโปรแกรมเรียก edge_ai ครั้งถัดไป ตัวเลขจึงไม่เหมือนบอร์ด

import edge_ai
import time
import ui

MODEL_KEYS = ("AnomalousVibration", "Motion Detection")
WINDOW_S = 10            # นับช่วงละกี่วินาที
RUNS = 6                 # กี่ช่วงแล้วจบ
DRAW_MS = 500

got = {"n": 0, "last": None}


def on_result(r):
    # เรียกจากบอร์ด: เก็บแล้วกลับ (ห้ามวาดจอ ห้ามรอ)
    got["n"] += 1
    got["last"] = r


def find_model(keys):
    ms = edge_ai.models()
    for key in keys:
        for m in ms:
            if m["name"] == key:
                return m["index"], m["name"]
    return None


def main():
    found = find_model(MODEL_KEYS)
    if not found:
        print("none of these models is on the board:", MODEL_KEYS)
        return
    idx, name = found
    ui.screen()
    time.sleep_ms(150)
    ui.Label("Callback vs polling: " + name, x=12, y=6, color=0xE8EAED, value=20)
    ui.Label("on_result() calls", x=12, y=48, color=0x4A9EFF, value=16)
    ui.Label("new seq by polling", x=404, y=48, color=0x30A46C, value=16)
    cb_l = ui.Label("-", x=12, y=76, color=0x4A9EFF, value=28)
    po_l = ui.Label("-", x=404, y=76, color=0x30A46C, value=28)
    last_l = ui.Label(" ", x=12, y=130, color=0xE8EAED, value=16)
    hist = ui.Label(" ", x=12, y=170, color=0x9AA3AF, value=16)
    ui.poll()
    rows = []
    try:
        edge_ai.select(idx)
        edge_ai.on_result(on_result)
        for k in range(RUNS):
            got["n"], polled, last_seq = 0, 0, None
            t0 = drawn = time.ticks_ms()
            while time.ticks_diff(time.ticks_ms(), t0) < WINDOW_S * 1000:
                r = edge_ai.result()
                if r and r["index"] == idx and r["seq"] != last_seq:
                    polled += 1
                    last_seq = r["seq"]
                now = time.ticks_ms()
                if time.ticks_diff(now, drawn) >= DRAW_MS:
                    drawn = now
                    cb_l.text(str(got["n"]))
                    po_l.text(str(polled))
                    c = got["last"]
                    if c:
                        last_l.text("last callback: label %s  conf %d%%  seq %s" % (
                            c.get("label"), int(c.get("conf", 0) * 100), c.get("seq", "-")))
                ui.poll()
                time.sleep_ms(50)
            rows.append("%d/%d" % (got["n"], polled))
            print("window", k + 1, "callback", got["n"], "| polling", polled)
            hist.text("callback/polling per %d s: %s" % (WINDOW_S, " ".join(rows)))
    finally:
        edge_ai.on_result(None)                  # เลิก callback ก่อนหยุดโมเดล
        try:
            edge_ai.stop()
        except OSError:
            pass


main()

# ลองต่อ: 1) ช่วงไหน callback น้อยกว่าวนอ่านมาก? ตอนนั้นป้ายเปลี่ยนบ่อยไหม?
# 2) ถ้าจะส่งผลขึ้น MQTT (คาบ 12) ควรส่งจาก callback หรือจากลูปหลัก? เพราะอะไร
