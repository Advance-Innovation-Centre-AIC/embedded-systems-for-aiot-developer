# 03_read_realtime.py - อ่านผล AI สด ๆ ให้ถูกวิธี
# ชุดตัวอย่างประจำคาบ 13
# ทดสอบแล้ว : Dev Kit เฟิร์มแวร์ 2.4.4 ผ่าน 3/3 ทางปุ่ม Run ของ IDE · Emulator ตรวจเฉพาะบนคอมด้วยโมดูลของ Emulator · Eva Kit ยังไม่ได้ทดสอบ
#
# ไฟล์นี้สอน: edge_ai.result() คืน dict ของผลล่าสุด หรือ None ถ้ายังไม่มีผล
#             ช่องที่ใช้: label (ชื่อคลาสที่ชนะ) conf (0-1) scores seq latency_ms running index
#             ผลใหม่ดูจาก seq ที่เปลี่ยน - อ่านถี่กว่าที่โมเดลตอบ จะได้ผลเดิมซ้ำ
# ดูที่จอ   : ผลล่าสุด + นับว่าอ่านไปกี่ครั้ง ได้ None / ผลเดิม / ผลใหม่ / ผลของโมเดลอื่น
#             ครบ RUN_S วินาที โปรแกรม stop() แล้วอ่านอีกครั้ง ดูช่อง running
# กับดัก    : หลัง select() ผลแรกเป็น None · หลัง stop() ยังได้ผลเก่าค้างอยู่
#             ถ้ามีคนเปลี่ยนโมเดลจากเมนู Edge AI ระหว่างรัน ผลจะเป็นของตัวอื่น - เช็ก r["index"]
# บอร์ด     : Dev Kit · Eva Kit · Emulator (ผลจำลอง)

import edge_ai
import time
import ui

MODEL_KEYS = ("AnomalousVibration", "Motion Detection")
RUN_S = 20               # อ่านกี่วินาทีแล้วหยุด
READ_MS = 50             # อ่านทุกกี่ ms (ถี่กว่าที่โมเดลตอบโดยตั้งใจ)
DRAW_MS = 500            # เขียนจอทุกกี่ ms


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
    ui.Label("Read results: " + name, x=12, y=6, color=0xE8EAED, value=20)
    res = ui.Label("-", x=12, y=56, color=0x30A46C, value=28)
    info = ui.Label(" ", x=12, y=110, color=0x9AA3AF, value=16)
    cnt = ui.Label(" ", x=12, y=150, color=0xE8EAED, value=16)
    end = ui.Label(" ", x=12, y=200, color=0xF5A623, value=16)
    ui.poll()

    n = {"None": 0, "same": 0, "new": 0, "other": 0}
    last_seq, lat, fresh = None, 0.0, None
    ok = None
    t0 = drawn = time.ticks_ms()
    try:
        edge_ai.select(idx)                      # อยู่ใน try: กด Stop ตอนโหลดก็ยังหยุดโมเดล
        t0 = time.ticks_ms()
        while time.ticks_diff(time.ticks_ms(), t0) < RUN_S * 1000:
            r = edge_ai.result()
            if r is None:
                n["None"] += 1                   # ยังไม่มีผลเลย
            elif r["index"] != idx:
                n["other"] += 1                  # ผลของโมเดลอื่น
            elif r["seq"] == last_seq:
                n["same"] += 1                   # ผลเดิม ยังไม่มีผลใหม่
            else:
                n["new"] += 1
                last_seq, lat, fresh = r["seq"], lat + r["latency_ms"], r
            now = time.ticks_ms()
            if time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn = now
                if fresh:
                    res.text("%s  %d%%" % (fresh["label"], int(fresh["conf"] * 100)))
                    info.text("seq %d | latency %.2f ms" % (fresh["seq"], fresh["latency_ms"]))
                    fresh = None
                cnt.text("reads: None %d  same %d  new %d  other %d" % (n["None"], n["same"], n["new"], n["other"]))
            ui.poll()
            time.sleep_ms(READ_MS)
    finally:
        try:
            ok = edge_ai.stop()
        except OSError:
            pass
    s = time.ticks_diff(time.ticks_ms(), t0) / 1000
    print("reads:", n)
    if n["new"]:
        print("new results %.1f /s | mean latency %.2f ms" % (n["new"] / s, lat / n["new"]))
    r = edge_ai.result()                         # อ่านอีกครั้งหลังหยุด
    after = "None" if r is None else "label %s running %s" % (r["label"], r["running"])
    print("stop() ->", ok, "| result() after stop:", after)
    end.text("After stop(): " + after)
    for i in range(50):                          # ค้างจอไว้ 10 วินาทีให้อ่าน
        ui.poll()
        time.sleep_ms(200)


main()

# ลองต่อ: 1) เปลี่ยน READ_MS เป็น 500 - ช่อง same ลดลง แต่ new หายไปด้วยไหม?
# 2) new ต่อวินาทีของ IMU กับของไมค์ต่างกันไหม - ใช้ตั้งความถี่ส่ง MQTT ในคาบ 14
