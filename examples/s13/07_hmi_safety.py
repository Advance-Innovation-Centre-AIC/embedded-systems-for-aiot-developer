# 07_hmi_safety.py - Smart HMI อาคาร: แผงแจ้งเหตุ ที่ AI ฟังเสียงสัญญาณเตือนในห้อง
# ชุดตัวอย่างประจำคาบ 11
# ทดสอบแล้ว : Eva Kit fw 1.7.8 ปุ่ม Program to Device ของ IDE (2026-10-16) เปิดจอ หาโมเดลเจอ อ่านผลสดได้ · ยังไม่ได้ลองให้เกิดเหตุจริง
#
# ไฟล์นี้สอน: แผงแจ้งเหตุจริงแยก "รับทราบ" ออกจาก "รีเซ็ต" - คนละปุ่ม คนละความหมาย
#             normal (ปกติ) -> alert (ได้ยินเสียงเตือน คำกะพริบ) -> ack (รับทราบ หยุดกะพริบ โซนยังแดง)
#             -> normal เมื่อกด "รีเซ็ต" และไม่ได้ยินเสียงแล้วเท่านั้น
#             เชื่อผลด้วย conf + "k ใน n" ของไฟล์ 04 · snapshot() = dict เดียวที่คาบ 12 ส่งขึ้น MQTT
# ดูที่จอ   : ซ้าย = ผังอาคาร 6 โซน (ห้องคอม 202 = ไมค์ของบอร์ด) · ขวา = สถานะระบบ ผลดิบ ไฟ 5 ดวง
#             ปุ่ม "รับทราบ" และ "รีเซ็ต" · บรรทัดเหนือปุ่มบอกเหตุผลเมื่อกดแล้วไม่เกิดอะไร
# ลองเล่น  : เปิดเสียงสัญญาณเตือนควัน (Alarm) หรือไซเรน (Siren) จากมือถือใกล้บอร์ด 3-5 วินาที
#             -> กดรีเซ็ตขณะเสียงยังดัง -> กดรับทราบ -> ปิดเสียง -> กดรีเซ็ต
# กับดัก    : เสียงหยุดเองแล้ว โซนก็ยังแดงค้าง (latch) จนกว่าจะรีเซ็ต - แผงจริงต้องเป็นแบบนี้
#             ชื่อคลาสต่างกัน: Alarm Detection ตอบ "alarm" แต่ Siren Detection ตอบ "sirens"
#             Siren Detection มั่นใจต่ำ: หมายเหตุใน ai_engine.c ว่าไซเรนจริงได้ราว 35-55% จึงมีเกณฑ์ของมันเอง
#             ใน CONF_FOR และเทียบคะแนนของคลาสอันตรายใน r["scores"] ไม่ใช่แค่ป้ายที่ชนะ (ไซเรน 45% อาจแพ้
#             unlabelled) · ยังไม่ได้วัดบน Eva · ไม่ใช้เสียงจากบอร์ด: ลำโพงอาจเข้าไมค์เอง
#             select() รอได้ถึง 15 วินาที · เปิดหลายโมเดลในรอบเดียวอาจได้ "select not confirmed" = กด RESET
# สาธิต    : DEMO = True (ปกติ False) ผู้สอนสาธิตการเตือนได้โดยไม่ต้องเปิดเสียง: หลัง select() เรียก
#             edge_ai.inject() ครั้งเดียว ให้คลาสอันตราย DEMO_PCT% ยาว DEMO_STEPS ผล (กี่วินาทีขึ้นกับโมเดล)
#             กับดัก: เหตุการณ์จาก inject เป็นของจำลอง (synthetic) inject ป้อนแค่ตัวตรวจเหตุการณ์ของบอร์ด
#             result() ยังเป็นผลจริง จอจึงอ่าน edge_ai.events() และข้ามกฎ k ใน n · มุมขวาบนขึ้นคำว่า DEMO
#             วัดแล้ว 2026-10-16: Eva 1.7.8 และ Dev Kit 2.5.8 ยังรับ inject ไม่ได้ Emulator ก็ไม่มี - โปรแกรมแจ้งแล้วทำงานต่อตามปกติ
# ต่อยอดจาก: BENTO HMI Studio แบบ "แผงแจ้งเหตุเพลิงไหม้" (ตัวอย่าง hmi-security-fire-panel)
#             ตัดภาพ bento_assets กริ่ง และแผนจำลองออก โซนอื่นแสดงอย่างเดียว ไม่มีเซ็นเซอร์
# บอร์ด     : Eva Kit 1.7.8 (เขียนให้ Eva) · Dev Kit ยังไม่ได้ทดสอบ

import edge_ai
import json
import time
import ui

TEAM = "teamXX"                  # เลขทีมที่ผู้สอนแจก ใช้ในคาบ 12 (คาบนี้ไม่ต้องแก้)
KIT = "safety"
MODEL_KEYS = ("Alarm Detection", "Siren Detection", "SirenDetection")   # ติดมากับบอร์ดก่อน ตัวท้ายมาจาก Store
DANGER = ("alarm", "sirens")     # คลาสที่นับเป็นเสียงเตือน (ชื่อตามที่บอร์ดรายงาน)
MANUAL_RESET = True              # กลับ normal ได้ด้วยปุ่ม "รีเซ็ต" เท่านั้น
CONF_MIN = 60
CONF_FOR = {"Siren Detection": 45}   # เกณฑ์ยืนยันของเฟิร์มแวร์สำหรับตัวนี้ (ai_engine.c k_th_default)
CONFIRM_N = 3
WINDOW_N = 5
LIVE = 3                         # โซนที่ใช้ไมค์ของบอร์ด (ลำดับใน ZONES)
DRAW_MS = 500
NOTE_MS = 3000
HOLD_S = 30                      # ข้อความผิดพลาดค้างจอกี่วินาที
DEMO = False                     # True = สาธิตด้วยผลจำลอง (ดูหัวไฟล์) งานจริงต้องเป็น False
DEMO_PCT, DEMO_STEPS = 95, 60    # ผลจำลองกี่ % ยาวกี่ผลของโมเดล
NO_MODEL = ("ไม่พบโมเดลบนบอร์ด", "Alarm Detection หรือ Siren Detection", "deploy SirenDetection จาก Store")

COL_TEXT, COL_DIM, COL_CARD, COL_LINE = 0xE8EAED, 0x9AA3AF, 0x171B22, 0x293441
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF
COL_KEY = 0x212431

ZONES = ("ห้องสมุด 301", "ห้องประชุม 302", "ห้องเรียน 201", "ห้องคอม 202",
         "โรงอาหาร 101", "ทางหนีไฟ 102")          # ชั้น 3 ลงมาชั้น 1
WORD = {"normal": ("ระบบปกติ", COL_OK), "alert": ("ได้ยินเสียงเตือน", COL_BAD),
        "ack": ("รับทราบแล้ว", COL_WARN)}
HELP = "รับทราบก่อน แล้วรีเซ็ต"

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
    # ส่งให้จอเฉพาะเมื่อข้อความหรือสีเปลี่ยน
    if seen.get(id(w)) != (s, c):
        seen[id(w)] = (s, c)
        w.text(s)
        w.color(c)


def build_screen(name):
    ui.screen()
    time.sleep_ms(150)
    w = {"title": ui.Label("แผงแจ้งเหตุ อาคารเรียน 3 ชั้น", x=24, y=10, color=COL_TEXT, value=20)}
    w["ai"] = ui.Label("AI: " + name + ("  DEMO" if DEMO else ""), x=480, y=14,
                       color=COL_WARN if DEMO else COL_INFO, value=16)
    ui.Panel(x=24, y=48, w=416, h=328, color=COL_CARD, min=COL_LINE, max=16)
    w["zp"], w["zt"] = [], []
    for i, n in enumerate(ZONES):                # 2 คอลัมน์ x 3 ชั้น ชั้นบนอยู่บน
        x, y = 40 + i % 2 * 196, 60 + i // 2 * 92
        w["zp"].append(ui.Panel(x=x, y=y, w=188, h=84, color=COL_KEY, min=COL_LINE, max=12))
        w["zt"].append(ui.Label(n + "\nปกติ", x=x + 12, y=y + 12, color=COL_DIM, value=20))
    ui.Label("ไมค์ของบอร์ด = ห้องคอม 202", x=40, y=340, color=COL_DIM, value=20)
    ui.Panel(x=456, y=48, w=312, h=180, color=COL_CARD, min=COL_LINE, max=16)
    w["word"] = ui.Label(" ", x=472, y=60, color=COL_OK, value=28)
    w["sub"] = ui.Label(" ", x=472, y=100, color=COL_DIM, value=20)
    w["raw"] = ui.Label("-", x=472, y=160, color=COL_DIM, value=20)
    w["dots"] = [ui.Led(x=472 + i * 32, y=196, w=24, h=24, color=COL_BAD, value=0) for i in range(WINDOW_N)]
    w["cnt"] = ui.Label(" ", x=640, y=194, color=COL_TEXT, value=20)
    w["note"] = ui.Label(HELP, x=456, y=236, color=COL_DIM, value=20)   # เหนือปุ่ม ห่างมุมปุ่ม Console
    w["ack"] = ui.Button("รับทราบ", x=456, y=272, w=200, h=52, color=COL_KEY, value=20)
    w["rst"] = ui.Button("รีเซ็ต", x=664, y=272, w=104, h=52, color=COL_KEY, value=20)
    ui.poll()
    return w


def draw(w, seen, alerting, blink, t_alarm, now, note):
    st = S["state"]
    word, col = WORD[st]
    put(w["word"], word, COL_DIM if (st == "alert" and blink) else col, seen)
    if st == "normal":
        put(w["sub"], "ทุกโซนปกติ", COL_DIM, seen)
    else:
        s = min(599, time.ticks_diff(now, t_alarm) // 1000)
        put(w["sub"], "%s\nเตือนมาแล้ว %d:%02d" % (ZONES[LIVE], s // 60, s % 60), COL_TEXT, seen)
    z = "ปกติ" if st == "normal" else ("ได้ยินเสียง" if alerting else "เสียงหยุดแล้ว")
    put(w["zt"][LIVE], ZONES[LIVE] + "\n" + z, COL_DIM if st == "normal" else COL_TEXT, seen)
    zp = w["zp"][LIVE]                           # โซนแดงค้างจนกว่าจะรีเซ็ต (latch)
    zc = COL_KEY if st == "normal" else COL_BAD
    if seen.get(id(zp)) != zc:
        seen[id(zp)] = zc
        zp.color(zc)
    put(w["ack"], "รับทราบ", COL_WARN if st == "alert" else COL_KEY, seen)
    put(w["rst"], "รีเซ็ต", COL_INFO if (st == "ack" and not alerting) else COL_KEY, seen)
    put(w["cnt"], "เหตุ %d ครั้ง" % S["events"], COL_TEXT, seen)
    put(w["note"], note or HELP, COL_BAD if note else COL_DIM, seen)


def main():
    found = find_model(MODEL_KEYS)
    if not found:
        print("none of these models is on the board:", MODEL_KEYS)
        hold(NO_MODEL, COL_BAD)
        return
    idx, name, labels = found
    cls = ([i for i, s in enumerate(labels) if s in DANGER] + [None])[0]   # ลำดับคลาสอันตราย (ใช้กับ DEMO)
    conf_min = CONF_FOR.get(name, CONF_MIN)
    S["model"] = name
    print("found", name, "at index", idx, "| conf_min", conf_min)
    w = build_screen(name)
    hist, alerting, lit = [], False, [0] * WINDOW_N
    seen, blink, hot, base = {}, False, False, None
    last_seq, note, t_note = None, None, 0
    drawn = t_alarm = time.ticks_ms()
    put(w["note"], "กำลังโหลดโมเดล รอสักครู่", COL_WARN, seen)
    ui.poll()
    try:
        if not start(idx):                       # อยู่ใน try: กด Stop ตอนโหลดก็ยังหยุดโมเดล
            return
        if DEMO:                                 # ฉีดไม่ได้ = หัวจอบอกเหตุ แล้วทำงานต่อแบบไม่สาธิต
            base, why = demo_start(idx, cls)
            if why:
                put(w["title"], why, COL_WARN, seen)
                put(w["ai"], "AI: " + name, COL_INFO, seen)
        draw(w, seen, alerting, blink, t_alarm, drawn, note)
        while True:
            now = time.ticks_ms()
            act = tap = None
            flip = False
            for e in ui.poll() or ():
                if e["type"] == "clicked":
                    tap = {w["ack"].id(): "ack", w["rst"].id(): "rst"}.get(e["handle"], tap)
            st = S["state"]
            if tap == "ack":                     # รับทราบได้เฉพาะตอนกำลังเตือน
                act = st == "alert"
            elif tap == "rst":                   # รีเซ็ตได้เฉพาะหลังรับทราบ
                act = st == "ack"
                if st == "alert":
                    note, t_note = "ต้องรับทราบก่อน", now
            try:
                r = edge_ai.result()
            except OSError:                      # สายข้ามแกนช้าเกินรอบนี้: ข้ามไป ไม่ล้ม
                r = None
            fresh = r and r["index"] == idx and r["seq"] != last_seq and r["label"] is not None
            if fresh:
                last_seq = r["seq"]
                pct = int(r["conf"] * 100)
                sc = r.get("scores") or ()       # คะแนนทุกคลาส: ดูคลาสอันตรายเอง ไม่ใช่แค่ป้ายที่ชนะ
                p = int(sc[cls] * 100 + 0.5) if (cls is not None and cls < len(sc)) else (
                    pct if r["label"] in DANGER else 0)
                hist, alerting = alert_rule(hist, alerting, p >= conf_min)
                S["label"], S["conf"], S["n"] = r["label"], pct, r["seq"]
            if base is not None and time.ticks_diff(now, drawn) >= DRAW_MS:
                was_hot = hot
                hot, base = demo_hot(idx, cls, base, hot)
                flip = hot != was_hot
            if fresh or tap or flip:
                S["state"], refused = step(st, alerting or hot, bool(act), MANUAL_RESET)
                if refused:
                    note, t_note = "ยังได้ยินเสียง รีเซ็ตไม่ได้", now
                if S["state"] != st:
                    if S["state"] == "alert":
                        S["events"] += 1
                        t_alarm = now
                    if S["state"] != "alert":
                        note = None
                    print(json.dumps(snapshot()))            # คาบ 12: บรรทัดนี้กลายเป็น mqtt.publish
                if tap or S["state"] != st:
                    draw(w, seen, alerting or hot, blink, t_alarm, now, note)
            if time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn = now
                blink = not blink
                if note and time.ticks_diff(now, t_note) >= NOTE_MS:
                    note = None
                draw(w, seen, alerting or hot, blink, t_alarm, now, note)
                put(w["raw"], "ได้ยิน: %s %d%%" % (S["label"], S["conf"]), COL_DIM, seen)
                for i, d in enumerate(w["dots"]):
                    v = hist[i] if i < len(hist) else 0
                    if v != lit[i]:
                        d.value(v)
                        lit[i] = v
            time.sleep_ms(50)
    finally:                                     # จบโปรแกรม = ปล่อยแกน AI เสมอ
        demo_stop(base)
        try:
            edge_ai.stop()
        except OSError:
            pass


main()

# ลองต่อ: 1) สลับ MODEL_KEYS ให้ "Siren Detection" ขึ้นก่อน แล้วเปิดเสียงเตือนควัน - ยังเตือนไหม ทำไม?
# 2) เปิดเพลงหรือคุยเสียงดังใกล้บอร์ด 1 นาที - เตือนผิดกี่ครั้ง? ลอง CONFIRM_N = 4
# 3) ดู Console: สถานะเปลี่ยนทุกครั้งจะพิมพ์ JSON หนึ่งบรรทัด = snapshot() ที่คาบ 12 ส่งขึ้น MQTT
