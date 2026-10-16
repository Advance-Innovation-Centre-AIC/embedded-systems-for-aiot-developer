# 09_hmi_voice.py - Smart HMI บ้าน: สั่งไฟด้วยเสียง ด้วยโมเดล Voice Commands จาก Store
# ชุดตัวอย่างประจำคาบ 11
# ทดสอบแล้ว : Eva Kit fw 1.7.8 ปุ่ม Program to Device ของ IDE (2026-10-16) เปิดจอ หาโมเดลเจอ อ่านผลสดได้ · ยังไม่ได้ลองให้เกิดเหตุจริง
#
# ไฟล์นี้สอน: เอาผล AI ที่เป็น "คำสั่ง" ไปขับจอ HMI ของบ้าน (ไม่ใช่ "เหตุ" แบบ 06 07 08)
#             left / right = เลือกห้อง · up / down = ปรับความสว่างทีละ STEP_PCT% · go = เปิดไฟ · stop = ปิดไฟ
#             กรองแบบไฟล์ 04: conf >= CONF_MIN และคำเดียวกันชนะ CONFIRM_N ใน WINDOW_N ผลล่าสุด
#             พูดค้าง = นับครั้งเดียว คำเดิมจะนับใหม่ได้เมื่อหายจากหน้าต่างไปก่อน (แบบ "หายเตือน" ของ 04)
#             snapshot() แบบเดียวกับ 06 07 08: kit "voice" · state = "on"/"off" ไฟห้องที่เลือก
#             events = รับคำสั่งไปกี่ครั้ง · คาบ 12 ส่ง dict นี้ขึ้น MQTT
# ดูที่จอ   : บน = 3 ห้อง (แตะเลือกได้) · ซ้าย = ไฟของห้องที่เลือก (แตะการ์ด = เปิด/ปิด) แถบความสว่าง
#             ขวา = ได้ยินคำว่าอะไร กี่ % ไฟ 5 ดวง (ติด = ผลนั้นผ่านเกณฑ์) คำสั่งล่าสุด จำนวนครั้ง
# ลองเล่น  : พูด right -> up -> up -> stop -> go ชัด ๆ ใกล้บอร์ด ทีละคำ เว้นช่วงสั้น ๆ
# กับดัก    : Voice Commands มาจาก Store ต้อง deploy ก่อน ไม่มีจะขึ้นจอบอก ไม่ล้ม · ป้าย Store เขียน "unlabeled"
#             พูดแล้วไม่ติด: คำหนึ่งกินเวลาไม่กี่ผลของโมเดล 3 ใน 5 อาจเข้มไป ลอง CONFIRM_N = 2 (ยังไม่ได้วัดบน Eva)
#             select() รอได้ถึง 15 วินาที · เปิดหลายโมเดลในรอบเดียวอาจได้ "select not confirmed" = กด RESET
# สาธิต    : DEMO = True (ปกติ False): หลัง select() เรียก edge_ai.inject() ครั้งเดียว ให้คำว่า go DEMO_PCT%
#             ยาว DEMO_STEPS ผล ไฟห้องที่เลือกจะเปิด (ปิดไฟก่อนด้วยการแตะการ์ด จะเห็นชัด)
#             กับดัก: เหตุการณ์จาก inject เป็นของจำลอง (synthetic) inject ป้อนแค่ตัวตรวจเหตุการณ์ของบอร์ด
#             result() ยังเป็นผลจริง จอจึงอ่าน edge_ai.events() และข้ามกฎ k ใน n · มุมขวาบนขึ้นคำว่า DEMO
#             วัดแล้ว 2026-10-16: Eva 1.7.8 และ Dev Kit 2.5.8 ยังรับ inject ไม่ได้ Emulator ก็ไม่มี - โปรแกรมแจ้งแล้วทำงานต่อตามปกติ
# ต่อยอดจาก: BENTO HMI Studio แบบ "บ้านอัจฉริยะ" (ตัวอย่าง hmi-living-home) ห้อง 3 ห้อง การ์ดทั้งใบคือปุ่ม
#             สีอำพันเฉพาะไฟที่เปิด · ตัดภาพ bento_assets แอร์ และ ui.mode เต็มจอออก (Eva 1.7.8 ไม่มี)
# บอร์ด     : Eva Kit 1.7.8 (เขียนให้ Eva) · Dev Kit ยังไม่ได้ทดสอบ · ไม่ใช้เสียงจากบอร์ด (ลำโพงอาจเข้าไมค์)

import edge_ai
import json
import time
import ui

TEAM = "teamXX"                  # เลขทีมที่ผู้สอนแจก ใช้ในคาบ 12 (คาบนี้ไม่ต้องแก้)
KIT = "voice"
MODEL_KEYS = ("Voice Commands",)  # จาก Store
COMMANDS = ("left", "right", "up", "down", "go", "stop")   # ชื่อตามที่บอร์ดรายงาน
CONF_MIN = 60                    # มั่นใจไม่ถึงกี่ % ไม่นับ
CONFIRM_N = 3                    # คำเดียวกันต้องชนะกี่ครั้ง ...
WINDOW_N = 5                     # ... ในกี่ผลล่าสุด
STEP_PCT = 10                    # up / down ปรับทีละกี่ %
DRAW_MS = 500
HOLD_S = 30                      # ข้อความผิดพลาดค้างจอกี่วินาที
DEMO = False                     # True = สาธิตด้วยผลจำลอง (ดูหัวไฟล์) งานจริงต้องเป็น False
DEMO_PCT, DEMO_STEPS = 95, 60    # ผลจำลองกี่ % ยาวกี่ผลของโมเดล
DEMO_WORD = "go"                 # คำที่จำลองตอน DEMO
NO_MODEL = ("ไม่พบโมเดล Voice Commands", "deploy Voice Commands จาก Store ก่อน", "แล้วกด Run ใหม่")

COL_TEXT, COL_DIM, COL_CARD, COL_LINE = 0xE6EDF3, 0x9BA7B4, 0x181C20, 0x293441
COL_RAISED, COL_SEL, COL_AMBER = 0x202430, 0x586870, 0xF0B040   # สีจากแบบ hmi-living-home
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x38B880, 0xF0B440, 0xE5484D, 0x4A9EFF

ROOMS = ("ห้องนั่งเล่น", "ห้องนอน", "ครัว")
LAMP = ("ไฟเพดาน", "ไฟหัวเตียง", "ไฟครัว")
SAY = {"left": "ห้องซ้าย", "right": "ห้องขวา", "up": "สว่างขึ้น", "down": "หรี่ลง", "go": "เปิดไฟ", "stop": "ปิดไฟ"}

S = {"model": "-", "label": "-", "conf": 0, "state": "off", "events": 0, "n": 0}


# ---- find_model hold start snapshot: คัดลอกจากส่วนกลางของ 06 ทุกตัวอักษร ----
def find_model(keys):
    # (index, name, labels) ของตัวแรกที่ชื่อตรง ไม่เจอ = None
    ms = edge_ai.models()
    for key in keys:
        for m in ms:
            if m["name"] == key:
                return m["index"], m["name"], m.get("labels", [])
    return None


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
# ---- จบส่วนที่คัดลอก ----


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


def gate(hist, latched, word):
    # hist = คำที่ผ่าน CONF_MIN ใน WINDOW_N ผลล่าสุด (None = ไม่ผ่าน) · latched = คำที่รับไปแล้ว
    # คืน (hist ใหม่, latched ใหม่, คำสั่งใหม่หรือ None)
    hist = (hist + [word])[-WINDOW_N:]
    if latched and latched not in hist:
        latched = None                           # คำเดิมหายจากหน้าต่างแล้ว พูดซ้ำได้
    if word and word != latched and hist.count(word) >= CONFIRM_N:
        return hist, word, word
    return hist, latched, None


def apply(cmd, room, light):
    # ทำตามคำสั่ง · light[ห้อง] = [เปิดไหม, ความสว่าง %] · คืนห้องที่เลือกอยู่
    if cmd == "left":
        return (room - 1) % len(ROOMS)
    if cmd == "right":
        return (room + 1) % len(ROOMS)
    if cmd == "up":
        light[room][1] = min(100, light[room][1] + STEP_PCT)
    elif cmd == "down":
        light[room][1] = max(10, light[room][1] - STEP_PCT)
    elif cmd in ("go", "stop"):
        light[room][0] = int(cmd == "go")
    return room


def put(w, s, c, seen):
    # ส่งให้จอเฉพาะเมื่อข้อความหรือสีเปลี่ยน
    if seen.get(id(w)) != (s, c):
        seen[id(w)] = (s, c)
        w.text(s)
        w.color(c)


def build_screen(name):
    ui.screen()
    time.sleep_ms(150)
    w = {"title": ui.Label("บ้านอัจฉริยะ  สั่งไฟด้วยเสียง", x=24, y=10, color=COL_TEXT, value=20)}
    w["ai"] = ui.Label("AI: " + name + ("  DEMO" if DEMO else ""), x=480, y=14,
                       color=COL_WARN if DEMO else COL_INFO, value=16)
    w["chips"] = [ui.Button(n, x=24 + i * 136, y=44, w=128, h=48, color=COL_CARD, value=20)
                  for i, n in enumerate(ROOMS)]
    w["card"] = ui.Button("", x=24, y=104, w=464, h=232, color=COL_CARD)   # การ์ดทั้งใบคือปุ่ม
    w["room"] = ui.Label(" ", x=44, y=116, color=COL_TEXT, value=28)
    w["lamp"] = ui.Label(" ", x=44, y=160, color=COL_DIM, value=20)
    w["state"] = ui.Label(" ", x=44, y=196, color=COL_DIM, value=28)
    w["bar"] = ui.Bar(x=44, y=244, w=424, h=20, min=0, max=100, value=0)
    ui.Label("แตะการ์ด = เปิด / ปิด", x=44, y=284, color=COL_DIM, value=20)
    ui.Panel(x=504, y=104, w=264, h=232, color=COL_CARD, min=COL_LINE, max=16)
    ui.Label("คำสั่งเสียง", x=520, y=116, color=COL_DIM, value=20)
    w["heard"] = ui.Label("-", x=520, y=148, color=COL_DIM, value=20)
    w["dots"] = [ui.Led(x=520 + i * 40, y=184, w=28, h=28, color=COL_AMBER, value=0) for i in range(WINDOW_N)]
    w["done"] = ui.Label(" ", x=520, y=222, color=COL_AMBER, value=24)
    w["cnt"] = ui.Label(" ", x=520, y=262, color=COL_TEXT, value=20)
    ui.Label("เกณฑ์ %d%% และ %d ใน %d" % (CONF_MIN, CONFIRM_N, WINDOW_N), x=520, y=296, color=COL_DIM, value=20)
    ui.Label("left right = ห้อง  up down = ความสว่าง  go stop = เปิด ปิด",
             x=24, y=350, color=COL_DIM, value=20)   # จบก่อน x 706: มุมขวาล่างเป็นของปุ่ม Console
    ui.poll()
    return w


def show(w, seen, room, light, done):
    # วาดเฉพาะที่เปลี่ยน และตั้ง S["state"] = ไฟของห้องที่เลือก
    on, pct = light[room]
    S["state"] = "on" if on else "off"
    for i, c in enumerate(w["chips"]):
        if seen.get(id(c)) != (i == room):
            seen[id(c)] = i == room
            c.color(COL_SEL if i == room else COL_CARD)
    put(w["room"], ROOMS[room], COL_TEXT, seen)
    put(w["lamp"], LAMP[room], COL_DIM, seen)
    put(w["state"], ("เปิด %d%%" % pct) if on else "ปิด", COL_AMBER if on else COL_DIM, seen)
    if seen.get("bar") != (pct, on):
        seen["bar"] = (pct, on)
        w["bar"].value(pct)
        w["bar"].color(COL_AMBER if on else COL_LINE)   # Bar ไม่สน color= ตอนสร้าง ต้องสั่ง .color()
    if done:
        put(w["done"], "%s = %s" % (done, SAY[done]), COL_AMBER, seen)
    put(w["cnt"], "รับคำสั่ง %d ครั้ง" % S["events"], COL_TEXT, seen)


def main():
    found = find_model(MODEL_KEYS)
    if not found:
        print("none of these models is on the board:", MODEL_KEYS)
        hold(NO_MODEL, COL_BAD)
        return
    idx, name, labels = found
    cls = ([i for i, s in enumerate(labels) if s == DEMO_WORD] + [None])[0]   # ลำดับคลาสของคำ go (DEMO)
    S["model"] = name
    print("found", name, "at index", idx, "labels", labels)
    w = build_screen(name)
    room, light = 0, [[1, 70], [1, 30], [0, 100]]   # ค่าตั้งต้นจากแบบ hmi-living-home
    hist, latched, lit, seen = [], None, [0] * WINDOW_N, {}
    last_seq, hot, done, base = None, False, None, None
    drawn = time.ticks_ms()
    put(w["heard"], "กำลังโหลดโมเดล รอสักครู่", COL_WARN, seen)
    ui.poll()
    try:
        if not start(idx):                       # อยู่ใน try: กด Stop ตอนโหลดก็ยังหยุดโมเดล
            return
        if DEMO:                                 # ฉีดไม่ได้ = หัวจอบอกเหตุ แล้วทำงานต่อแบบไม่สาธิต
            base, why = demo_start(idx, cls)
            if why:
                put(w["title"], why, COL_WARN, seen)
                put(w["ai"], "AI: " + name, COL_INFO, seen)
        show(w, seen, room, light, done)
        while True:
            now = time.ticks_ms()
            cmd, tap = None, False
            for e in ui.poll() or ():
                if e["type"] != "clicked":
                    continue
                for i, c in enumerate(w["chips"]):
                    if e["handle"] == c.id():
                        room, tap = i, True
                if e["handle"] == w["card"].id():
                    light[room][0], tap = 1 - light[room][0], True
            try:
                r = edge_ai.result()
            except OSError:                      # สายข้ามแกนช้าเกินรอบนี้: ข้ามไป ไม่ล้ม
                r = None
            if r and r["index"] == idx and r["seq"] != last_seq and r["label"] is not None:
                last_seq = r["seq"]
                pct = int(r["conf"] * 100)
                word = r["label"] if (r["label"] in COMMANDS and pct >= CONF_MIN) else None
                hist, latched, cmd = gate(hist, latched, word)
                S["label"], S["conf"], S["n"] = r["label"], pct, r["seq"]
            if base is not None and time.ticks_diff(now, drawn) >= DRAW_MS:
                was_hot = hot
                hot, base = demo_hot(idx, cls, base, hot)
                if hot and not was_hot:
                    cmd = DEMO_WORD                  # เหตุการณ์จำลอง = พูด go หนึ่งครั้ง
            if cmd:
                room = apply(cmd, room, light)
                S["events"] += 1
                done = cmd
            if cmd or tap:
                show(w, seen, room, light, done)
                print(json.dumps(snapshot()))            # คาบ 12: บรรทัดนี้กลายเป็น mqtt.publish
            if time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn = now
                put(w["heard"], "ได้ยิน: %s  %d%%" % (S["label"], S["conf"]), COL_DIM, seen)
                for i, d in enumerate(w["dots"]):
                    v = 1 if (i < len(hist) and hist[i]) else 0
                    if v != lit[i]:              # ส่งเฉพาะดวงที่เปลี่ยน
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

# ลองต่อ: 1) ตั้ง CONFIRM_N = 1 แล้วคุยกันใกล้บอร์ด 1 นาที - ไฟเปิดปิดเองกี่ครั้ง เทียบกับ 3?
# 2) พูด up ค้างไว้ยาว ๆ - ความสว่างขึ้นกี่ขั้น? ทำไมไม่ขึ้นทุกผล (ดู latched ใน gate())
# 3) ดู Console: รับคำสั่งหรือแตะจอทุกครั้งจะพิมพ์ JSON หนึ่งบรรทัด = snapshot() ที่คาบ 12 ส่งขึ้น MQTT
