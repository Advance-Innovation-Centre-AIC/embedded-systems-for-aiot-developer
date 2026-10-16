# 06_hmi_machine.py - Smart HMI โรงงาน: จอซ่อมบำรุงปั๊ม ที่ AI ฟังการสั่นของเครื่อง
# ชุดตัวอย่างประจำคาบ 11
# ทดสอบแล้ว : Eva Kit fw 1.7.8 ปุ่ม Program to Device ของ IDE (2026-10-16) เปิดจอ หาโมเดลเจอ อ่านผลสดได้ · ยังไม่ได้ลองให้เกิดเหตุจริง
#
# ไฟล์นี้สอน: เอาผล AI ไปขับจอ HMI ของโรงงาน ไม่ใช่แค่พิมพ์ป้าย
#             1) กรองผลด้วย conf + "k ใน n" ของไฟล์ 04 ก่อนเชื่อ
#             2) สถานะสามขั้นแบบแผงควบคุม: normal (ปกติ) -> alert (เตือน ยังไม่มีใครรับเรื่อง)
#                -> ack (สร้างใบสั่งซ่อมแล้ว) -> normal เมื่อกด "ซ่อมเสร็จ" และเครื่องสั่นปกติแล้วเท่านั้น
#             3) snapshot() = ผลทั้งหมดใน dict เดียว json.dumps ได้ทันที - คาบ 12 ส่ง dict นี้ขึ้น MQTT
# ดูที่จอ   : ซ้าย = สภาพเครื่อง ผลดิบ ไฟ 5 ดวง (ติด = สั่นผิดปกติ) จำนวนครั้ง ปุ่มใบสั่งซ่อม
#             ขวา = กราฟ AI ว่าผิดปกติกี่ % ทุกครึ่งวินาที (เส้นเทา = เกณฑ์) · แถบสุขภาพจาก 30 ผลล่าสุด
# ลองเล่น  : วางนิ่ง -> เขย่าแรงค้างไว้ 3 วินาที -> วางนิ่ง -> กด "สร้างใบสั่งซ่อม" -> กด "ซ่อมเสร็จ"
# กับดัก    : เครื่องกลับมาสั่นปกติเองแล้ว คำเตือนก็ยังค้าง จนกว่าจะมีคนรับเรื่อง - ตั้งใจให้เป็นแบบนี้
#             กด "ซ่อมเสร็จ" ตอนเครื่องยังสั่นผิดปกติ = ปิดงานไม่ได้
#             AnomalousVibration มาจาก Store (deploy ก่อน) ไม่มีจะใช้ Motion Detection ที่ติดมากับบอร์ด
#             ท่าไหนที่ AnomalousVibration ตอบว่า anomaly ยังไม่ได้ลองบนบอร์ด - ดูผลดิบบนจอ
#             select() รอได้ถึง 15 วินาที · เปิดหลายโมเดลในรอบเดียวอาจได้ "select not confirmed" = กด RESET
# สาธิต    : DEMO = True (ปกติ False) ผู้สอนสาธิตการเตือนได้โดยไม่ต้องเขย่า: หลัง select() เรียก edge_ai.inject()
#             ครั้งเดียว ให้คลาสอันตราย DEMO_PCT% ยาว DEMO_STEPS ผล (กี่วินาทีขึ้นกับโมเดล ยังไม่ได้วัดบน Eva)
#             กับดัก: เหตุการณ์จาก inject เป็นของจำลอง (synthetic) inject ป้อนแค่ตัวตรวจเหตุการณ์ของบอร์ด
#             result() ยังเป็นผลจริง จอจึงอ่าน edge_ai.events() และข้ามกฎ k ใน n · มุมขวาบนขึ้นคำว่า DEMO
#             วัดแล้ว 2026-10-16: Eva 1.7.8 และ Dev Kit 2.5.8 ยังรับ inject ไม่ได้ Emulator ก็ไม่มี - โปรแกรมแจ้งแล้วทำงานต่อตามปกติ
# ต่อยอดจาก: BENTO HMI Studio แบบ "ซ่อมบำรุงเชิงพยากรณ์" (ตัวอย่าง hmi-industry-predictive)
#             ตัดภาพ bento_assets และค่าจำลองออก ใช้ผล AI จริงแทน · กฎ 3 ใน 5 จาก 04_confidence_gate.py
# บอร์ด     : Eva Kit 1.7.8 (เขียนให้ Eva) · Dev Kit ยังไม่ได้ทดสอบ · ไม่มีเสียงโดยตั้งใจ

import edge_ai
import json
import time
import ui

TEAM = "teamXX"                  # เลขทีมที่ผู้สอนแจก ใช้ในคาบ 12 (คาบนี้ไม่ต้องแก้)
KIT = "machine"
MODEL_KEYS = ("AnomalousVibration", "Motion Detection")   # Store ก่อน ไม่มีค่อยใช้ตัวในตัว
DANGER = ("anomaly", "shaking")  # คลาสที่นับเป็นสั่นผิดปกติ (ชื่อตามที่บอร์ดรายงาน)
MANUAL_RESET = True              # กลับ normal ได้ด้วยปุ่ม "ซ่อมเสร็จ" เท่านั้น
CONF_MIN = 60                    # มั่นใจไม่ถึงกี่ % ไม่นับ
CONFIRM_N = 3                    # ต้องชนะกี่ครั้ง ...
WINDOW_N = 5                     # ... ในกี่ผลล่าสุด
HEALTH_N = 30                    # สุขภาพเครื่องคิดจากกี่ผลล่าสุด
CHART_N = 40                     # จุดในกราฟ 1 จุดทุก DRAW_MS = 20 วินาที
DRAW_MS = 500
NOTE_MS = 3000                   # ข้อความ "ปิดงานไม่ได้" ค้างกี่ ms
HOLD_S = 30                      # ข้อความผิดพลาดค้างจอกี่วินาที
DEMO = False                     # True = สาธิตด้วยผลจำลอง (ดูหัวไฟล์) งานจริงต้องเป็น False
DEMO_PCT, DEMO_STEPS = 95, 60    # ผลจำลองกี่ % ยาวกี่ผลของโมเดล
NO_MODEL = ("ไม่พบโมเดลบนบอร์ด", "AnomalousVibration หรือ Motion Detection", "deploy จาก Store ก่อน")

COL_TEXT, COL_DIM, COL_CARD, COL_LINE = 0xE8EAED, 0x9AA3AF, 0x171B22, 0x293441
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF
COL_KEY = 0x212431

WORD = {"normal": ("ปกติ", COL_OK), "alert": ("สั่นผิดปกติ", COL_BAD),
        "ack": ("ส่งใบสั่งซ่อมแล้ว", COL_WARN)}
KEY = {"normal": ("เครื่องปกติ", COL_KEY), "alert": ("สร้างใบสั่งซ่อม", COL_WARN),
       "ack": ("ซ่อมเสร็จ ปิดงาน", COL_INFO)}

S = {"model": "-", "label": "-", "conf": 0, "state": "normal", "events": 0, "n": 0}


# ---- ส่วนกลาง: เหมือนกันทุกตัวอักษรในไฟล์ 06 07 08 ของคาบ 11 และ 04 ของคาบ 12 ----
def find_model(keys):
    # (index, name, labels) ของตัวแรกที่ชื่อตรง ไม่เจอ = None
    ms = edge_ai.models()
    for key in keys:
        for m in ms:
            if m["name"] == key:
                return m["index"], m["name"], m.get("labels", [])
    return None


def alert_rule(hist, alerting, danger):
    # hist = ผลล่าสุดไม่เกิน WINDOW_N ตัว (1 = อันตราย) · คืน (hist ใหม่, ยืนยันว่าอันตรายไหม)
    hist = (hist + [int(danger)])[-WINDOW_N:]
    hits = sum(hist)
    return hist, hits >= CONFIRM_N or (alerting and hits > 0)


def step(state, alerting, act, manual):
    # สถานะถัดไป · act = แตะปุ่มหลัก · manual = ต้องกดรีเซ็ตเอง · คืน (สถานะใหม่, ถูกปฏิเสธไหม)
    if state == "normal":
        return ("alert" if alerting else "normal"), False
    if state == "alert":                         # เหตุหายเองก็ยังเตือน จนกว่ามีคนรับทราบ
        return ("ack" if act else "alert"), False
    if not alerting and (act or not manual):     # state == "ack"
        return "normal", False
    return "ack", act and alerting               # ยังพบเหตุอยู่ = รีเซ็ตไม่ได้


def snapshot():
    # ผล AI ทั้งหมดใน dict เดียว json.dumps ได้ทันที - คาบ 12 ส่ง dict นี้ขึ้น MQTT
    return {"id": TEAM, "kit": KIT, "model": S["model"], "label": S["label"], "conf": S["conf"],
            "state": S["state"], "events": S["events"], "n": S["n"]}


def hold(lines, col):
    # ล้างจอ บอกเหตุทีละบรรทัด ค้างไว้ HOLD_S วินาทีให้อ่าน แล้วจบโปรแกรมเองโดยไม่ล้ม
    ui.screen()
    time.sleep_ms(150)
    for i, s in enumerate(lines):
        ui.Label(s, x=24, y=24 + i * 44, color=col if i == 0 else COL_TEXT, value=24)
    for i in range(HOLD_S * 5):
        ui.poll()
        time.sleep_ms(200)


def start(idx):
    # select() รอจนแกน AI ยืนยัน · ไม่ยืนยัน = ขึ้นจอให้กด RESET แทนการล้ม · คืน True ถ้าเริ่มได้
    try:
        edge_ai.select(idx)
        return True
    except OSError as e:
        print("select failed:", e)
        hold(("เปิดโมเดลไม่สำเร็จ", str(e), "กดปุ่ม RESET บนบอร์ด", "แล้วกด Run ใหม่อีกครั้ง"), COL_BAD)
        return False
# ---- จบส่วนกลาง ----


# ---- ส่วนสาธิต DEMO: เหมือนกันทุกตัวอักษรในไฟล์ 06 07 08 09 ของคาบ 11 ----
def demo_start(idx, cls):
    # ฉีดผลจำลองครั้งเดียว · คืน (eid ล่าสุดก่อนฉีด, None) หรือ (None, ข้อความไทยบอกว่าทำไมไม่ได้)
    if not (hasattr(edge_ai, "inject") and hasattr(edge_ai, "events")):
        print("DEMO: no edge_ai.inject / edge_ai.events here (Emulator) - running without DEMO")
        return None, "โหมดสาธิตใช้ได้บนบอร์ดจริงเท่านั้น"
    if not cls:
        return None, "โมเดลนี้ไม่มีคลาสให้สาธิต"
    try:
        edge_ai.inject(None)                     # ล้างผลจำลองที่ค้างจากรอบก่อน
        base = max([e["eid"] for e in edge_ai.events()] or [0])
        edge_ai.inject(idx, cls, bytes([0] * 5 + [DEMO_PCT] * DEMO_STEPS + [0] * 5))
        print("DEMO: inject model", idx, "class", cls, "- events are synthetic")
        return base, None
    except (OSError, ValueError, TypeError) as e:
        print("DEMO: inject failed:", e)
        return None, "โหมดสาธิตเริ่มไม่ได้ ดู Console"


def demo_hot(idx, cls, base, was):
    # คืน (hot, base) · confirm จำลองใหม่ (eid > base) = hot อย่างน้อยหนึ่งรอบ แม้ exit มาถึงพร้อมกัน
    # exit ของ confirm นั้น = หายเตือน · อ่านไม่ได้รอบนี้ = คืนค่าเดิม
    try:
        hot, fresh = was, False
        for e in edge_ai.events():               # เรียงเก่าไปใหม่
            if e["model"] != idx or e["cls"] != cls or not e["synthetic"]:
                continue
            if e["edge"] == "confirm" and e["eid"] > base:
                base, hot, fresh = e["eid"], True, True
            elif e["edge"] == "exit" and e["eid"] == base and not fresh:
                hot = False
        return hot, base
    except OSError:
        return was, base


def demo_stop(base):
    # จบโปรแกรม: ยกเลิกผลจำลองที่เหลือ ไม่ให้ค้างไปรอบหน้า (บอร์ดรับพร้อมกันได้ 4 รายการ)
    if base is not None:
        try:
            edge_ai.inject(None)
        except OSError:
            pass
# ---- จบส่วนสาธิต ----


def put(w, s, c, seen):
    # ส่งให้จอเฉพาะเมื่อข้อความหรือสีเปลี่ยน (ทุกคำสั่งข้ามแกนไปจอ ส่งซ้ำ = จอช้า)
    if seen.get(id(w)) != (s, c):
        seen[id(w)] = (s, c)
        w.text(s)
        w.color(c)


def build_screen(name):
    ui.screen()
    time.sleep_ms(150)
    w = {"title": ui.Label("ปั๊มหล่อเย็น P-101  AI ฟังการสั่น", x=24, y=10, color=COL_TEXT, value=20)}
    w["ai"] = ui.Label("AI: " + name + ("  DEMO" if DEMO else ""), x=480, y=14,
                       color=COL_WARN if DEMO else COL_INFO, value=16)
    ui.Panel(x=24, y=48, w=280, h=328, color=COL_CARD, min=COL_LINE, max=16)
    ui.Panel(x=320, y=48, w=448, h=296, color=COL_CARD, min=COL_LINE, max=16)   # จบ y 344: เว้นปุ่ม Console
    ui.Label("สภาพเครื่อง", x=40, y=60, color=COL_DIM, value=20)
    w["state"] = ui.Label(" ", x=40, y=90, color=COL_OK, value=28)
    w["sub"] = ui.Label(" ", x=40, y=132, color=COL_DIM, value=20)
    w["raw"] = ui.Label("-", x=40, y=168, color=COL_DIM, value=20)
    ui.Label("%d ผลล่าสุด" % WINDOW_N, x=40, y=200, color=COL_DIM, value=20)
    w["dots"] = [ui.Led(x=40 + i * 40, y=230, w=28, h=28, color=COL_BAD, value=0) for i in range(WINDOW_N)]
    w["events"] = ui.Label(" ", x=40, y=270, color=COL_TEXT, value=20)
    w["key"] = ui.Button("เครื่องปกติ", x=40, y=312, w=248, h=52, color=COL_KEY, value=20)
    ui.Label("AI ว่าผิดปกติกี่ %  เส้นเทา = เกณฑ์", x=336, y=60, color=COL_DIM, value=20)
    ch = ui.Chart(x=336, y=92, w=416, h=136, min=0, max=100, color=COL_BAD)
    ch.prop(ui.PROP_CHART_POINTS, CHART_N)
    w["th"] = ch.add_series(COL_DIM)             # เส้นเกณฑ์ CONF_MIN
    w["chart"] = ch
    ui.Label("สุขภาพเครื่อง จาก %d ผลล่าสุด" % HEALTH_N, x=336, y=244, color=COL_DIM, value=20)
    w["bar"] = ui.Bar(x=336, y=282, w=300, h=24, min=0, max=100, value=100)
    w["bar"].color(COL_OK)                       # Bar ไม่สน color= ตอนสร้าง ต้องสั่ง .color() ทีหลัง
    w["hp"] = ui.Label("100%", x=652, y=276, color=COL_TEXT, value=28)
    ui.Label("ลองเขย่าบอร์ด ดูผลดิบด้านซ้าย", x=336, y=312, color=COL_DIM, value=20)
    ui.poll()
    return w


def draw_state(w, seen, alerting, blink, note):
    st = S["state"]
    word, col = WORD[st]
    put(w["state"], word, COL_DIM if (st == "alert" and blink) else col, seen)   # ยังไม่มีใครรับ = กะพริบ
    if note:
        sub, sc = note, COL_BAD
    elif st == "normal":
        sub, sc = "AI ไม่พบการสั่นผิดปกติ", COL_DIM
    elif st == "alert":
        sub, sc = "ยังไม่มีใครรับเรื่อง", COL_BAD
    elif alerting:
        sub, sc = "รอช่าง เครื่องยังสั่นอยู่", COL_WARN
    else:
        sub, sc = "สั่นปกติแล้ว กดปิดงานได้", COL_OK
    put(w["sub"], sub, sc, seen)
    put(w["key"], KEY[st][0], KEY[st][1], seen)
    put(w["events"], "สั่นผิดปกติ %d ครั้ง" % S["events"], COL_TEXT, seen)


def main():
    found = find_model(MODEL_KEYS)
    if not found:
        print("none of these models is on the board:", MODEL_KEYS)
        hold(NO_MODEL, COL_BAD)
        return
    idx, name, labels = found
    cls = ([i for i, s in enumerate(labels) if s in DANGER] + [None])[0]   # ลำดับคลาสอันตราย (ใช้กับ DEMO)
    S["model"] = name
    print("found", name, "at index", idx)
    w = build_screen(name)
    hist, alerting, lit, health = [], False, [0] * WINDOW_N, []
    seen, peak, blink, hot, base = {}, 0, False, False, None
    last_seq, note, t_note, drawn = None, None, 0, time.ticks_ms()
    put(w["sub"], "กำลังโหลดโมเดล รอสักครู่", COL_WARN, seen)
    ui.poll()
    try:
        if not start(idx):                       # อยู่ใน try: กด Stop ตอนโหลดก็ยังหยุดโมเดล
            return
        if DEMO:                                 # ฉีดไม่ได้ = หัวจอบอกเหตุ แล้วทำงานต่อแบบไม่สาธิต
            base, why = demo_start(idx, cls)
            if why:
                put(w["title"], why, COL_WARN, seen)
                put(w["ai"], "AI: " + name, COL_INFO, seen)
        draw_state(w, seen, alerting, blink, note)
        while True:
            now = time.ticks_ms()
            act = flip = False
            for e in ui.poll() or ():
                if e["type"] == "clicked" and e["handle"] == w["key"].id():
                    act = True
            try:
                r = edge_ai.result()
            except OSError:                      # สายข้ามแกนช้าเกินรอบนี้: ข้ามไป ไม่ล้ม
                r = None
            fresh = r and r["index"] == idx and r["seq"] != last_seq and r["label"] is not None
            if fresh:
                last_seq = r["seq"]
                pct = int(r["conf"] * 100)
                bad = r["label"] in DANGER and pct >= CONF_MIN
                hist, alerting = alert_rule(hist, alerting, bad)
                health = (health + [int(bad)])[-HEALTH_N:]
                if r["label"] in DANGER:
                    peak = max(peak, pct)
                S["label"], S["conf"], S["n"] = r["label"], pct, r["seq"]
            if base is not None and time.ticks_diff(now, drawn) >= DRAW_MS:
                was_hot = hot
                hot, base = demo_hot(idx, cls, base, hot)
                flip = hot != was_hot
            if fresh or act or flip:
                was = S["state"]
                S["state"], refused = step(was, alerting or hot, act, MANUAL_RESET)
                if refused:
                    note, t_note = "ยังสั่นผิดปกติ ปิดงานไม่ได้", now
                if S["state"] != was:
                    if S["state"] == "alert":
                        S["events"] += 1
                    note = None
                    print(json.dumps(snapshot()))            # คาบ 12: บรรทัดนี้กลายเป็น mqtt.publish
                if act or S["state"] != was:
                    draw_state(w, seen, alerting or hot, blink, note)
            if time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn = now
                blink = not blink
                if note and time.ticks_diff(now, t_note) >= NOTE_MS:
                    note = None
                draw_state(w, seen, alerting or hot, blink, note)
                put(w["raw"], "ผลดิบ: %s  %d%%" % (S["label"], S["conf"]), COL_DIM, seen)
                for i, d in enumerate(w["dots"]):
                    v = hist[i] if i < len(hist) else 0
                    if v != lit[i]:              # ส่งเฉพาะดวงที่เปลี่ยน
                        d.value(v)
                        lit[i] = v
                w["chart"].set_next(0, peak)     # จุดละครึ่งวินาที: ค่าสูงสุดในช่วงนั้น
                w["chart"].set_next(w["th"], CONF_MIN)
                peak = 0
                if health:
                    hp = 100 - 100 * sum(health) // len(health)
                    col = COL_OK if hp >= 80 else (COL_WARN if hp >= 50 else COL_BAD)
                    if seen.get("hp") != (hp, col):
                        seen["hp"] = (hp, col)
                        w["bar"].value(hp)
                        w["bar"].color(col)
                        w["hp"].text("%d%%" % hp)
            time.sleep_ms(50)
    finally:                                     # จบโปรแกรม = ปล่อยแกน AI เสมอ
        demo_stop(base)
        try:
            edge_ai.stop()
        except OSError:
            pass


main()

# ลองต่อ: 1) ตั้ง CONFIRM_N = 1 แล้วเคาะโต๊ะเบา ๆ - ใบสั่งซ่อมผิด ๆ เกิดกี่ใบ เทียบกับ 3?
# 2) ตั้ง MANUAL_RESET = False - คำเตือนหายเองหลังรับเรื่อง ดีหรือเสียกับโรงงานจริง?
# 3) ดู Console: สถานะเปลี่ยนทุกครั้งจะพิมพ์ JSON หนึ่งบรรทัด = snapshot() ที่คาบ 12 ส่งขึ้น MQTT ทั้งก้อน
