# 08_hmi_nursery.py - Smart HMI โรงพยาบาล: จอเรียกพยาบาลห้องเด็กอ่อน ที่ AI ฟังเสียงเด็กร้อง
# ชุดตัวอย่างประจำคาบ 11
# ทดสอบแล้ว : Eva Kit fw 1.7.8 ปุ่ม Program to Device ของ IDE (2026-10-16) เปิดจอ หาโมเดลเจอ อ่านผลสดได้ · ยังไม่ได้ลองให้เกิดเหตุจริง
#
# ไฟล์นี้สอน: AI แทน "ปุ่มเรียกพยาบาล" ที่เด็กกดเองไม่ได้
#             normal (ทุกเตียงปกติ) -> alert (เด็กร้อง ยังไม่มีใครรับ) -> ack (รับทราบ กำลังไปดู)
#             -> normal เองเมื่อเด็กหยุดร้อง (ต่างจาก 06 และ 07 ที่ต้องกดรีเซ็ต: MANUAL_RESET = False)
#             เชื่อผลด้วย conf + "k ใน n" ของไฟล์ 04 · snapshot() = dict เดียวที่คาบ 12 ส่งขึ้น MQTT
# ดูที่จอ   : ซ้าย = เคสด่วน เวลารอ (นาที:วินาที) ผลดิบ ไฟ 5 ดวง ปุ่มรับทราบ
#             ขวา = 6 เตียง (เตียง 4 = ไมค์ของบอร์ด) สีเตียงบอกสถานะ
# ลองเล่น  : เปิดเสียงเด็กร้องจากมือถือใกล้บอร์ด 5 วินาที แล้วปิด - ดูว่าเวลารอยังเดินต่อจนกดรับทราบ
#             กดรับทราบแล้ว เวลาหยุดนิ่ง = เวลาที่ใช้ตอบสนองเคสนี้
# กับดัก    : เด็กหยุดร้องเองแล้ว เตียงก็ยังแดง จนกว่ามีคนรับทราบ - เวลารอคือตัวเลขที่หัวหน้าพยาบาลดู
#             ชื่อคลาสต่างกัน: Baby Cry Detection / HomeSounds ตอบ "baby_cry" แต่ Environment Sounds ตอบ "crying_baby"
#             ไม่ใช้เสียงจากบอร์ด: ลำโพงอาจเข้าไมค์เอง · ตัวอย่างเพื่อการเรียน ไม่ใช่อุปกรณ์การแพทย์
#             select() รอได้ถึง 15 วินาที · เปิดหลายโมเดลในรอบเดียวอาจได้ "select not confirmed" = กด RESET
# สาธิต    : DEMO = True (ปกติ False) ผู้สอนสาธิตการเตือนได้โดยไม่ต้องเปิดเสียง: หลัง select() เรียก
#             edge_ai.inject() ครั้งเดียว ให้คลาสเด็กร้อง DEMO_PCT% ยาว DEMO_STEPS ผล (กี่วินาทีขึ้นกับโมเดล)
#             กับดัก: เหตุการณ์จาก inject เป็นของจำลอง (synthetic) inject ป้อนแค่ตัวตรวจเหตุการณ์ของบอร์ด
#             result() ยังเป็นผลจริง จอจึงอ่าน edge_ai.events() และข้ามกฎ k ใน n · มุมขวาบนขึ้นคำว่า DEMO
#             วัดแล้ว 2026-10-16: Eva 1.7.8 และ Dev Kit 2.5.8 ยังรับ inject ไม่ได้ Emulator ก็ไม่มี - โปรแกรมแจ้งแล้วทำงานต่อตามปกติ
# ต่อยอดจาก: BENTO HMI Studio แบบ "เรียกพยาบาล" (ตัวอย่าง hmi-healthcare-nurse-call)
#             ตัดภาพ bento_assets เสียง และเหตุการณ์จำลองออก เตียงอื่นแสดงอย่างเดียว ไม่มีเซ็นเซอร์
# บอร์ด     : Eva Kit 1.7.8 (เขียนให้ Eva) · Dev Kit ยังไม่ได้ทดสอบ

import edge_ai
import json
import time
import ui

TEAM = "teamXX"                  # เลขทีมที่ผู้สอนแจก ใช้ในคาบ 12 (คาบนี้ไม่ต้องแก้)
KIT = "nursery"
MODEL_KEYS = ("Baby Cry Detection", "HomeSounds", "Environment Sounds")   # ติดมากับบอร์ดก่อน
DANGER = ("baby_cry", "crying_baby")   # คลาสที่นับเป็นเด็กร้อง (ชื่อตามที่บอร์ดรายงาน)
MANUAL_RESET = False             # รับทราบแล้ว เด็กหยุดร้อง = กลับ normal เอง
CONF_MIN = 60
CONFIRM_N = 3
WINDOW_N = 5
LIVE = 3                         # เตียงที่ใช้ไมค์ของบอร์ด (ลำดับเริ่ม 0 = เตียง 4)
DRAW_MS = 500
HOLD_S = 30                      # ข้อความผิดพลาดค้างจอกี่วินาที
DEMO = False                     # True = สาธิตด้วยผลจำลอง (ดูหัวไฟล์) งานจริงต้องเป็น False
DEMO_PCT, DEMO_STEPS = 95, 60    # ผลจำลองกี่ % ยาวกี่ผลของโมเดล
NO_MODEL = ("ไม่พบโมเดลบนบอร์ด", "Baby Cry Detection หรือ HomeSounds", "deploy HomeSounds จาก Store")

COL_TEXT, COL_DIM, COL_CARD, COL_LINE = 0xE8EAED, 0x9AA3AF, 0x171B22, 0x293441
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF
COL_KEY, COL_TEAL = 0x212431, 0x1E6B66

HEAD = {"normal": ("ทุกเตียงปกติ", COL_OK), "alert": ("เตียง %d\nเด็กร้อง" % (LIVE + 1), COL_BAD),
        "ack": ("เตียง %d\nรับทราบแล้ว" % (LIVE + 1), COL_TEAL)}
BED = "เตียง %d  ไมค์ AI\n" % (LIVE + 1)

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
    w = {"title": ui.Label("ห้องเด็กอ่อน  AI ฟังเสียงเด็กร้อง", x=24, y=10, color=COL_TEXT, value=20)}
    w["ai"] = ui.Label("AI: " + name + ("  DEMO" if DEMO else ""), x=480, y=14,
                       color=COL_WARN if DEMO else COL_INFO, value=16)
    ui.Panel(x=24, y=48, w=296, h=328, color=COL_CARD, min=COL_LINE, max=16)
    w["head"] = ui.Label(" ", x=40, y=60, color=COL_OK, value=28)
    ui.Label("รอมาแล้ว", x=40, y=152, color=COL_DIM, value=20)
    w["wait"] = ui.Label("-:--", x=150, y=146, color=COL_TEXT, value=28)
    w["raw"] = ui.Label("-", x=40, y=192, color=COL_DIM, value=20)
    w["dots"] = [ui.Led(x=40 + i * 40, y=226, w=28, h=28, color=COL_BAD, value=0) for i in range(WINDOW_N)]
    w["events"] = ui.Label(" ", x=40, y=266, color=COL_TEXT, value=20)
    w["ack"] = ui.Button("รับทราบ", x=40, y=304, w=264, h=56, color=COL_KEY, value=20)
    w["bp"], w["bt"] = [], []
    for i in range(6):                           # 2 คอลัมน์ x 3 แถว จบที่ y 304
        x, y = 336 + 224 * (i % 2), 48 + 88 * (i // 2)
        w["bp"].append(ui.Panel(x=x, y=y, w=208, h=80, color=COL_KEY, min=COL_LINE, max=12))
        t = BED if i == LIVE else "เตียง %d\n" % (i + 1)
        w["bt"].append(ui.Label(t + "ปกติ", x=x + 16, y=y + 12, color=COL_DIM, value=20))
    ui.Label("ตัวอย่างเพื่อการเรียน", x=336, y=316, color=COL_DIM, value=20)
    ui.Label("ไม่ใช่อุปกรณ์การแพทย์", x=336, y=344, color=COL_DIM, value=20)
    ui.poll()
    return w


def draw(w, seen, alerting, blink, t_cry, t_ack, now):
    st = S["state"]
    head, col = HEAD[st]
    put(w["head"], head, COL_DIM if (st == "alert" and blink) else col, seen)
    if st == "normal":
        put(w["wait"], "-:--", COL_DIM, seen)
        bed = "ปกติ"
    else:
        end = t_ack if st == "ack" else now      # รับทราบแล้ว = หยุดที่เวลาตอบสนอง
        s = min(599, time.ticks_diff(end, t_cry) // 1000)   # สูงสุด 9:59
        put(w["wait"], "%d:%02d" % (s // 60, s % 60), COL_TEXT, seen)
        bed = "พยาบาลกำลังไป" if st == "ack" else ("เด็กร้อง" if alerting else "รอรับทราบ")
    put(w["bt"][LIVE], BED + bed, COL_DIM if st == "normal" else COL_TEXT, seen)
    bp = w["bp"][LIVE]
    bc = {"normal": COL_KEY, "alert": COL_BAD, "ack": COL_TEAL}[st]
    if seen.get(id(bp)) != bc:
        seen[id(bp)] = bc
        bp.color(bc)
    put(w["ack"], "กำลังไปดู" if st == "ack" else "รับทราบ", COL_WARN if st == "alert" else COL_KEY, seen)
    put(w["events"], "เด็กร้อง %d ครั้ง" % S["events"], COL_TEXT, seen)


def main():
    found = find_model(MODEL_KEYS)
    if not found:
        print("none of these models is on the board:", MODEL_KEYS)
        hold(NO_MODEL, COL_BAD)
        return
    idx, name, labels = found
    cls = ([i for i, s in enumerate(labels) if s in DANGER] + [None])[0]   # ลำดับคลาสเด็กร้อง (ใช้กับ DEMO)
    S["model"] = name
    print("found", name, "at index", idx)
    w = build_screen(name)
    hist, alerting, lit = [], False, [0] * WINDOW_N
    seen, blink, last_seq, hot, base = {}, False, None, False, None
    drawn = t_cry = t_ack = time.ticks_ms()
    put(w["raw"], "กำลังโหลดโมเดล รอสักครู่", COL_WARN, seen)
    ui.poll()
    try:
        if not start(idx):                       # อยู่ใน try: กด Stop ตอนโหลดก็ยังหยุดโมเดล
            return
        if DEMO:                                 # ฉีดไม่ได้ = หัวจอบอกเหตุ แล้วทำงานต่อแบบไม่สาธิต
            base, why = demo_start(idx, cls)
            if why:
                put(w["title"], why, COL_WARN, seen)
                put(w["ai"], "AI: " + name, COL_INFO, seen)
        draw(w, seen, alerting, blink, t_cry, t_ack, drawn)
        while True:
            now = time.ticks_ms()
            act = flip = False
            for e in ui.poll() or ():
                if e["type"] == "clicked" and e["handle"] == w["ack"].id():
                    act = True
            try:
                r = edge_ai.result()
            except OSError:                      # สายข้ามแกนช้าเกินรอบนี้: ข้ามไป ไม่ล้ม
                r = None
            fresh = r and r["index"] == idx and r["seq"] != last_seq and r["label"] is not None
            if fresh:
                last_seq = r["seq"]
                pct = int(r["conf"] * 100)
                hist, alerting = alert_rule(hist, alerting, r["label"] in DANGER and pct >= CONF_MIN)
                S["label"], S["conf"], S["n"] = r["label"], pct, r["seq"]
            if base is not None and time.ticks_diff(now, drawn) >= DRAW_MS:
                was_hot = hot
                hot, base = demo_hot(idx, cls, base, hot)
                flip = hot != was_hot
            if fresh or act or flip:
                was = S["state"]
                S["state"], refused = step(was, alerting or hot, act, MANUAL_RESET)   # refused: กดซ้ำตอนยังร้อง ไม่ต้องทำอะไร
                if S["state"] != was:
                    if S["state"] == "alert":
                        S["events"] += 1
                        t_cry = now
                    if S["state"] == "ack":
                        t_ack = now
                    print(json.dumps(snapshot()))            # คาบ 12: บรรทัดนี้กลายเป็น mqtt.publish
                    draw(w, seen, alerting or hot, blink, t_cry, t_ack, now)
            if time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn = now
                blink = not blink
                draw(w, seen, alerting or hot, blink, t_cry, t_ack, now)
                put(w["raw"], "ได้ยิน: %s  %d%%" % (S["label"], S["conf"]), COL_DIM, seen)
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

# ลองต่อ: 1) ตั้ง MANUAL_RESET = True - ตอนนี้ใครเป็นคนบอกว่า "จบเคส" ระหว่าง AI กับพยาบาล?
# 2) เปิดเสียงไอ หรือเสียงก๊อกน้ำจากมือถือ - Baby Cry Detection เตือนผิดไหม? ลอง HomeSounds ขึ้นก่อน
# 3) ดู Console: สถานะเปลี่ยนทุกครั้งจะพิมพ์ JSON หนึ่งบรรทัด = snapshot() ที่คาบ 12 ส่งขึ้น MQTT
