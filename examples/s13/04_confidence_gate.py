# 04_confidence_gate.py - อย่าเชื่อผล AI ทีละผล: กรองด้วยความมั่นใจ + "k ใน n"
# ชุดตัวอย่างประจำคาบ 13
# ทดสอบแล้ว : Dev Kit เฟิร์มแวร์ 2.4.4 ผ่าน 3/3 ทางปุ่ม Run ของ IDE · Emulator ตรวจเฉพาะบนคอมด้วยโมดูลของ Emulator · Eva Kit ยังไม่ได้ทดสอบ
#
# ไฟล์นี้สอน: กรองผลดิบสองชั้นก่อนเตือน
#             1) conf ต่ำกว่า CONF_MIN ไม่นับ
#             2) คลาสอันตรายต้องชนะ CONFIRM_N ครั้งใน WINDOW_N ผลล่าสุดจึงเตือน
#             หายเตือนเมื่อใน WINDOW_N ผลล่าสุดไม่มีคลาสอันตรายเลย
# ดูที่จอ   : แถวบน = ผลดิบ · วงกลม = สถานะหลังกรอง (เขียวปกติ / แดงเตือน)
#             ไฟ 5 ดวง = WINDOW_N ผลล่าสุด (ติด = อันตราย) · ตัวนับ ป้ายดิบเปลี่ยน vs เตือนจริง
# ลองเล่น  : วางนิ่ง -> เขย่าเบา ๆ สั้น ๆ -> เขย่าแรงค้างไว้ 3 วินาที · เทียบตัวนับสองตัว
# Emulator  : shaking มั่นใจได้ไม่ถึง 55% ตั้ง CONF_MIN = 50 ก่อนลอง ไม่งั้นไม่เตือนเลย
# ต่อยอดจาก: short_courses/smart_farm/s3/sf3_04_ai_pump_doctor.py (กฎ 3 ใน 5 ตัวเดียวกัน)

import edge_ai
import time
import ui

MODEL_KEYS = ("AnomalousVibration", "Motion Detection")
DANGER = ("anomaly", "shaking")      # คลาสที่นับเป็นอันตราย (ชื่อตามที่บอร์ดรายงาน)
CONF_MIN = 60            # มั่นใจไม่ถึงกี่ % ไม่นับ
CONFIRM_N = 3            # ต้องชนะกี่ครั้ง ...
WINDOW_N = 5             # ... ในกี่ผลล่าสุด (ต้องไม่น้อยกว่า CONFIRM_N)
RUN_S = 120
DRAW_MS = 500

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_OK, COL_BAD = 0x30A46C, 0xE5484D


def find_model(keys):
    ms = edge_ai.models()
    for key in keys:
        for m in ms:
            if m["name"] == key:
                return m["index"], m["name"]
    return None


def alert_rule(hist, alerting, danger):
    # hist = ผลล่าสุดไม่เกิน WINDOW_N ตัว (1 = อันตราย) · คืน (hist ใหม่, เตือนไหม)
    hist = (hist + [int(danger)])[-WINDOW_N:]
    hits = sum(hist)
    return hist, hits >= CONFIRM_N or (alerting and hits > 0)


def build_screen(name):
    ui.screen()
    time.sleep_ms(150)
    ui.Label("Confidence gate: " + name, x=12, y=6, color=COL_TEXT, value=20)
    w = {"raw": ui.Label("raw: -", x=12, y=44, color=COL_DIM, value=20)}
    w["disc"] = ui.Panel(x=12, y=88, w=96, h=96, color=COL_OK, min=COL_DIM, max=48, value=0)
    w["state"] = ui.Label("OK", x=128, y=116, color=COL_OK, value=28)
    ui.Label("last %d (lit = danger)" % WINDOW_N, x=12, y=200, color=COL_DIM, value=16)
    w["dots"] = [ui.Led(x=12 + i * 40, y=226, w=28, h=28, color=COL_BAD, value=0) for i in range(WINDOW_N)]
    w["cnt"] = ui.Label(" ", x=12, y=276, color=COL_TEXT, value=16)
    ui.Label("rule: conf >= %d%% and %d of last %d" % (CONF_MIN, CONFIRM_N, WINDOW_N),
             x=12, y=310, color=COL_DIM, value=16)
    ui.poll()
    return w


def draw(w, raw, hist, lit):
    w["raw"].text(raw)
    for i, d in enumerate(w["dots"]):
        v = hist[i] if i < len(hist) else 0
        if v != lit[i]:                          # ส่งเฉพาะดวงที่เปลี่ยน
            d.value(v)
            lit[i] = v


def main():
    found = find_model(MODEL_KEYS)
    if not found:
        print("none of these models is on the board:", MODEL_KEYS)
        return
    idx, name = found
    w = build_screen(name)
    hist, alerting, lit = [], False, [0] * WINDOW_N
    flips = alarms = 0
    last_seq = last_label = raw = None
    t0 = drawn = time.ticks_ms()
    try:
        edge_ai.select(idx)
        while time.ticks_diff(time.ticks_ms(), t0) < RUN_S * 1000:
            r = edge_ai.result()
            if r and r["index"] == idx and r["seq"] != last_seq and r["label"] is not None:
                last_seq = r["seq"]
                pct = int(r["conf"] * 100)
                if r["label"] != last_label:
                    flips += 1                   # ป้ายดิบเปลี่ยน
                    last_label = r["label"]
                was = alerting
                hist, alerting = alert_rule(hist, alerting, r["label"] in DANGER and pct >= CONF_MIN)
                raw = "raw: %s  %d%%" % (r["label"], pct)
                if alerting != was:              # สถานะเปลี่ยน: ขึ้นจอทันที
                    if alerting:
                        alarms += 1
                        print("ALARM #%d  %s %d%%" % (alarms, r["label"], pct))
                    w["disc"].color(COL_BAD if alerting else COL_OK)
                    w["state"].text("ALARM" if alerting else "OK")
                    w["state"].color(COL_BAD if alerting else COL_OK)
            now = time.ticks_ms()
            if raw and time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn = now
                draw(w, raw, hist, lit)
                w["cnt"].text("raw label changes %d | alarms %d" % (flips, alarms))
            ui.poll()
            time.sleep_ms(100)
    finally:
        try:
            edge_ai.stop()
        except OSError:
            pass
    print("label changes", flips, "| alarms", alarms)


main()

# ลองต่อ: 1) ตั้ง CONFIRM_N = 1 แล้วเขย่าเบา ๆ - เตือนกี่ครั้ง เทียบกับ 3?
# 2) ตั้ง CONF_MIN = 90 - ต้องเขย่าแรงแค่ไหนถึงเตือน? ค่าไหนเหมาะกับปั๊มจริง?
