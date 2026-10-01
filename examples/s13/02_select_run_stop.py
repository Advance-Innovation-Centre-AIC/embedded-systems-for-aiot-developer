# 02_select_run_stop.py - เลือกโมเดลด้วยชื่อ แล้วรัน/หยุดด้วยโค้ด
# ชุดตัวอย่างประจำคาบ 13
# ทดสอบแล้ว : Dev Kit เฟิร์มแวร์ 2.4.4 ผ่าน 3/3 ทางปุ่ม Run ของ IDE · Emulator ตรวจเฉพาะบนคอมด้วยโมดูลของ Emulator · Eva Kit ยังไม่ได้ทดสอบ
#
# ไฟล์นี้สอน: หาโมเดลด้วย "ชื่อ" -> edge_ai.select(index) = โหลดแล้วเริ่มรันทันที
#             edge_ai.active() = ลำดับของโมเดลที่รันอยู่ (ติดลบ = ไม่มีตัวไหนรัน)
#             edge_ai.stop() = หยุด (บอร์ดคืน True เมื่อแกน AI หยุดจริง)
# ดูที่จอ   : ปุ่ม Run/Stop (หรือ SW6 ปุ่มบน) · select() ใช้เวลากี่ ms · active() ตอนนี้
#             ปุ่ม "start()" = เรียก edge_ai.start() แบบไม่ใส่เลข ดูว่าได้โมเดลไหน
# กับดัก    : start() เปล่า ๆ หลัง stop() รันโมเดลลำดับ 0 ไม่ใช่ตัวที่เลือกไว้
#             (ถ้าตัวที่เลือกอยู่ลำดับ 0 พอดี เช่นใน Emulator จะดูเหมือนถูก - ดูชื่อบนจอ)
#             ชื่อต้องตรงทุกตัวอักษร: "SirenDetection" (Store) กับ "Siren Detection"
#             (ติดมากับบอร์ด) เป็นคนละตัว
# บอร์ด     : Dev Kit · Emulator (ใช้ Motion Detection) · Eva Kit ไม่มีปุ่ม SW6 ใช้ปุ่มบนจอ

try:
    import buttons                   # Eva Kit ไม่มีโมดูลนี้
except ImportError:
    buttons = None
import edge_ai
import time
import ui

MODEL_KEYS = ("AnomalousVibration", "Motion Detection")   # Store ก่อน ไม่มีค่อยใช้ตัวในตัว
DRAW_MS = 500

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF


def find_model(keys):
    # (index, name) ของตัวแรกที่ชื่อตรง ไม่เจอ = None
    ms = edge_ai.models()
    for key in keys:
        for m in ms:
            if m["name"] == key:
                return m["index"], m["name"]
    return None


def name_of(i):
    try:
        return edge_ai.model(i)["name"] if i >= 0 else "(none)"
    except Exception:
        return "?"


def build_screen(name):
    ui.screen()
    time.sleep_ms(150)
    ui.Label("Select / Run / Stop", x=12, y=6, color=COL_TEXT, value=24)
    ui.Label("Model: " + name, x=12, y=44, color=COL_INFO, value=20)
    w = {"go": ui.Button("Run", x=12, y=84, w=220, h=64, color=COL_OK, value=24),
         "st": ui.Button("start()", x=248, y=84, w=220, h=64, color=COL_DIM, value=24)}
    w["took"] = ui.Label(" ", x=12, y=164, color=COL_DIM, value=16)
    w["act"] = ui.Label(" ", x=12, y=196, color=COL_TEXT, value=20)
    w["res"] = ui.Label(" ", x=12, y=236, color=COL_TEXT, value=28)
    hint = "Tap Run (or press SW6)" if buttons else "Tap Run"
    w["say"] = ui.Label(hint, x=12, y=300, color=COL_WARN, value=16)
    ui.poll()
    return w


def press(was):
    # SW6 (ปุ่มบน): True ครั้งเดียวต่อการกด
    if buttons is None:
        return False, False
    try:
        now = buttons.pressed(1)
    except Exception:
        now = False
    return now and not was, now


def main():
    found = find_model(MODEL_KEYS)
    if not found:
        print("none of these models is on the board:", MODEL_KEYS)
        return
    idx, name = found
    print("found", name, "at index", idx)
    w = build_screen(name)
    running, sw_was = False, False
    shown, last_res = 0, None
    try:
        while True:
            want = None
            for e in ui.poll() or ():
                if e["type"] == "clicked":
                    want = {w["go"].id(): "toggle", w["st"].id(): "start"}.get(e["handle"])
            hit, sw_was = press(sw_was)
            if hit:
                want = "toggle"
            if want == "toggle" and running:
                ok = edge_ai.stop()
                running = False
                w["say"].text("stop() -> %s" % ok)
            elif want == "toggle":
                w["say"].text("select(%d) ..." % idx)
                ui.poll()
                t0 = time.ticks_ms()
                try:
                    edge_ai.select(idx)          # รอจนแกน AI ยืนยัน
                    running = True
                    w["took"].text("select() took %d ms" % time.ticks_diff(time.ticks_ms(), t0))
                    w["say"].text("Running")
                except OSError as ex:
                    w["say"].text("select failed: %s" % ex)
            elif want == "start":
                try:
                    edge_ai.start()              # ไม่ใส่เลข: ดูว่าได้ตัวไหน
                    running = True
                    w["say"].text("start() with no index")
                except OSError as ex:
                    w["say"].text("start failed: %s" % ex)
            if want:
                w["go"].text("Stop" if running else "Run")
                w["go"].color(COL_BAD if running else COL_OK)
            now = time.ticks_ms()
            if want or time.ticks_diff(now, shown) >= DRAW_MS:
                shown = now
                a = edge_ai.active()
                w["act"].text("active() = %d  %s" % (a, name_of(a)))
                w["act"].color(COL_TEXT if a == idx else (COL_DIM if a < 0 else COL_BAD))
                r = edge_ai.result()
                res = "-"
                if running and r and r["running"] and r["label"] is not None:
                    res = "%s  %d%%" % (r["label"], int(r["conf"] * 100))
                if res != last_res:
                    w["res"].text(res)
                    last_res = res
            time.sleep_ms(30)
    finally:                                     # จบโปรแกรม = ปล่อยแกน AI เสมอ
        try:
            edge_ai.stop()
        except OSError:
            pass


main()

# ลองต่อ: 1) Run แล้ว Stop แล้วกด start() - active() บอกชื่ออะไร ตรงกับที่เลือกไว้ไหม?
# 2) เปลี่ยน MODEL_KEYS เป็น ("HumanActivity",) แล้ววัดว่า select() ใช้เวลาต่างจากเดิมไหม
