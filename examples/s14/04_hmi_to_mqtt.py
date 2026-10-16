# 04_hmi_to_mqtt.py - ผล AI ของจอ Smart HMI (คาบ 11 ไฟล์ 06 07 08 09) ขึ้น MQTT ไปให้แดชบอร์ด
# ชุดตัวอย่างประจำคาบ 12
# ทดสอบแล้ว : Eva Kit fw 1.7.8 ปุ่ม Program to Device ของ IDE (2026-10-16) แบบออฟไลน์ พิมพ์ JSON ถูกรูป · ส่งขึ้น broker จริงยังไม่ได้ทดสอบ
#
# ไฟล์นี้สอน: ไฟล์เดียวทั้งห้อง: แก้ TEAM กับ KIT สองบรรทัด ทุกทีมส่ง JSON หน้าตาเดียวกัน แดชบอร์ดอ่านแบบเดียว
#             JSON = snapshot() ตัวเดียวกับคาบ 11: {"id","kit","model","label","conf","state","events","n"}
#             state : "normal" ปกติ · "alert" ยืนยันแล้ว ยังไม่มีใครรับ · "ack" มีคนรับทราบแล้ว
#             events = เข้าสู่ alert มาแล้วกี่ครั้ง · n = seq ของผล AI ล่าสุด (ไม่ใช่ตัวนับข้อความ)
#             ส่งเมื่อ label / state / events เปลี่ยน + heartbeat ทุก ALIVE_EVERY_MS + ห่างกันอย่างน้อย GAP_MS
# ดูที่จอ   : สถานะตัวใหญ่ ผลดิบ ไฟ 5 ดวง ปุ่มเดียว (รับทราบ แล้วรีเซ็ต) · ไฟ MQTT · ส่งไปกี่ข้อความ · JSON ล่าสุด
# ดูฝั่งคอม : mosquitto_sub -h broker.hivemq.com -t 'bento-aiot/<TEAM>/ai' -v
# กับดัก    : client id ซ้ำ = broker เตะตัวเก่าหลุด จึงต่อท้ายด้วยเลขจากนาฬิกาบอร์ด (แบบไฟล์ 01)
#             "id" ใน JSON คือชื่อทีม ไม่ใช่ id ของโมเดล · กฎเตือนและสถานะคัดลอกจากคาบ 11 ทุกตัวอักษร
#             กดปุ่มบนจอ = state เปลี่ยน = ส่งทันที แดชบอร์ดเห็นว่า "มีคนรับทราบแล้ว"
#             KIT voice ในไฟล์นี้: คำว่า stop = เหตุ (หยุดฉุกเฉินด้วยเสียง) ส่ง state normal/alert/ack
#             ส่วนจอบ้านของ 09 ส่ง state เป็น on/off (ไฟห้องที่เลือก) แดชบอร์ดขึ้นคำนั้นตรง ๆ
#             select() รอได้ถึง 15 วินาที · ได้ "select not confirmed" = จอบอกให้กด RESET แล้ว Run ใหม่
# Emulator  : MQTT ของ Emulator ออกไปถึง broker จริง (broker.hivemq.com) - วัดบน IDE rc.9.25 เมื่อ 15 ต.ค. 2026
# ต้องแก้ก่อนรัน: TEAM และ KIT · บอร์ดที่ต่อ WiFi ที่บันทึกไว้อยู่แล้วไม่ต้องแก้ WIFI_SSID WIFI_PASS
#             ยังไม่แก้ TEAM = ทำงานออฟไลน์ พิมพ์ JSON ลง Console แทน

import edge_ai
import json
import mqtt
import time
import ui
import wifi

WIFI_SSID = "<Hotspot name>"
WIFI_PASS = "<Hotspot password>"
TEAM = "teamXX"                  # เลขทีมที่ผู้สอนแจก เช่น team05
KIT = "machine"                  # machine (06) | safety (07) | nursery (08) | voice (09) - ตรงกับไฟล์ของคาบ 11
BROKER = "broker.hivemq.com"     # สำรอง: test.mosquitto.org
CLIENT_ID = "bento-ai-" + TEAM + "-%04x" % (time.ticks_ms() & 0xFFFF)
TOPIC = "bento-aiot/" + TEAM + "/ai"
KITS = {                         # KIT: (MODEL_KEYS, DANGER, MANUAL_RESET, ชื่อบนจอ) - ค่าเดียวกับไฟล์ 06 07 08
    "machine": (("AnomalousVibration", "Motion Detection"), ("anomaly", "shaking"), True, "ปั๊มหล่อเย็น P-101"),
    "safety": (("Alarm Detection", "Siren Detection", "SirenDetection"), ("alarm", "sirens"), True, "แผงแจ้งเหตุ ห้องคอม 202"),
    "nursery": (("Baby Cry Detection", "HomeSounds", "Environment Sounds"), ("baby_cry", "crying_baby"), False,
                "ห้องเด็กอ่อน เตียง 4"),
    "voice": (("Voice Commands",), ("stop",), False, "บ้านสั่งด้วยเสียง  stop = หยุด"),   # Store: deploy ก่อน
}
CONF_MIN = 60
CONF_FOR = {"Siren Detection": 45}   # เหมือน 07: เกณฑ์ยืนยันของเฟิร์มแวร์สำหรับตัวนี้ (ai_engine.c)
CONFIRM_N = 3
WINDOW_N = 5
ALIVE_EVERY_MS = 2000            # ไม่มีอะไรเปลี่ยนก็ส่งซ้ำทุกเท่านี้
GAP_MS = 200                     # ห้ามส่งถี่กว่านี้
WIFI_WAIT_S = 5                  # รอ WiFi ที่บันทึกไว้กี่วินาทีก่อนต่อเอง
DRAW_MS = 500
RUN_S = 300
HOLD_S = 30                      # ข้อความผิดพลาดค้างจอกี่วินาที

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF
COL_KEY = 0x212431
WORD = {"normal": ("ปกติ", COL_OK), "alert": ("เตือน ยังไม่มีใครรับ", COL_BAD), "ack": ("รับทราบแล้ว", COL_WARN)}

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


# ---- ส่วนเครือข่าย: คัดลอกจาก 01_ai_to_mqtt.py ----
def should_send(changed, since_ms):
    # ห่างพอ และ (ป้ายต่างจากที่ส่งล่าสุด หรือ ถึง heartbeat)
    if since_ms < GAP_MS:
        return False
    return changed or since_ms >= ALIVE_EVERY_MS


def team_ok():
    return len(TEAM) == 6 and TEAM[:4] == "team" and TEAM[4:].isdigit() and TEAM != "team00"


def wifi_up(say):
    # ใช้ WiFi ที่บอร์ดต่อไว้แล้วก่อน · ต่อเองเฉพาะเมื่อแก้ WIFI_SSID แล้ว
    for i in range(WIFI_WAIT_S * 10):
        if wifi.is_connected():
            return True
        time.sleep_ms(100)
    if WIFI_SSID.startswith("<"):
        say("No WiFi - set WIFI_SSID (offline)", COL_WARN)
        return False
    say("Connecting WiFi ...", COL_WARN)
    return wifi.connect(WIFI_SSID, WIFI_PASS)


def go_online(say):
    if not team_ok():
        say("Set TEAM first (team01-team99) - offline", COL_WARN)
        return False
    try:
        if not wifi_up(say):
            return False
        say("Connecting broker ...", COL_WARN)
        if mqtt.connect(BROKER, port=1883, client_id=CLIENT_ID, keepalive=60):
            say("Online: " + TOPIC, COL_OK)
            return True
    except OSError as e:
        print("network:", e)
    say("Broker failed - offline", COL_BAD)
    return False


def send(online, obj):
    line = json.dumps(obj)
    print(line)
    if online:
        try:
            return bool(mqtt.publish(TOPIC, line))
        except OSError:
            pass
    return False
# ---- จบส่วนเครือข่าย ----


def build_screen():
    ui.screen()
    time.sleep_ms(150)
    ui.Label("Smart HMI to MQTT", x=12, y=6, color=COL_TEXT, value=24)
    w = {"model": ui.Label(" ", x=12, y=44, color=COL_INFO, value=20)}
    w["state"] = ui.Label(" ", x=12, y=84, color=COL_OK, value=28)
    w["raw"] = ui.Label("-", x=12, y=128, color=COL_DIM, value=20)
    w["dots"] = [ui.Led(x=12 + i * 40, y=164, w=28, h=28, color=COL_BAD, value=0) for i in range(WINDOW_N)]
    w["key"] = ui.Button("รับทราบ", x=12, y=204, w=300, h=48, color=COL_KEY, value=20)
    ui.Label("MQTT", x=560, y=48, color=COL_DIM, value=16)
    w["led"] = ui.Led(x=560, y=74, w=36, h=36, color=COL_OK, value=0)
    w["link"] = ui.Label("offline", x=606, y=82, color=COL_DIM, value=16)
    w["note"] = ui.Label(" ", x=12, y=262, color=COL_WARN, value=16)
    w["sent"] = ui.Label(" ", x=12, y=288, color=COL_TEXT, value=16)
    w["j"] = [ui.Label(" ", x=12, y=312 + i * 22, color=COL_DIM, value=16) for i in range(3)]
    ui.poll()
    return w


def show_link(w, online):
    w["led"].value(1 if online else 0)
    w["link"].text("connected" if online else "offline")


def draw_state(w, blink, manual):
    st = S["state"]
    word, col = WORD[st]
    w["state"].text(word)
    w["state"].color(COL_DIM if (st == "alert" and blink) else col)
    if st == "alert":
        w["key"].text("รับทราบ")
        w["key"].color(COL_WARN)
    elif st == "ack" and manual:
        w["key"].text("รีเซ็ต")
        w["key"].color(COL_INFO)
    else:
        w["key"].text("รับทราบ" if st == "normal" else "รอให้เหตุหาย")
        w["key"].color(COL_KEY)


def main():
    w = build_screen()

    def say(text, col):
        w["note"].color(col)
        w["note"].text(text)
        ui.poll()

    if KIT not in KITS:
        hold(("KIT ไม่ถูกต้อง: " + KIT, "machine safety nursery voice"), COL_BAD)
        return
    keys, danger, manual, title = KITS[KIT]
    found = find_model(keys)
    if not found:
        print("none of these models is on the board:", keys)
        hold(("ไม่พบโมเดลของ KIT " + KIT, " / ".join(keys), "deploy จาก Store ก่อน"), COL_BAD)
        return
    idx, name, labels = found
    cls = ([i for i, s in enumerate(labels) if s in danger] + [None])[0]   # ลำดับคลาสอันตราย
    conf_min = CONF_FOR.get(name, CONF_MIN)
    S["model"] = name
    w["model"].text(title + "  AI: " + name)
    online, blink = False, False
    hist, alerting, lit, last_seq = [], False, [0] * WINDOW_N, None
    sent_key, n_msg, sent, last_msg = None, 0, 0, None
    t0 = t_sent = drawn = time.ticks_ms()
    draw_state(w, blink, manual)
    try:
        online = go_online(say)
        show_link(w, online)
        w["raw"].text("กำลังโหลดโมเดล รอสักครู่")
        ui.poll()
        if not start(idx):                       # ไม่ยืนยัน: จอบอกให้กด RESET แล้วจบ (finally ยังปิด MQTT)
            return
        t0 = time.ticks_ms()
        while time.ticks_diff(time.ticks_ms(), t0) < RUN_S * 1000:
            now = time.ticks_ms()
            act = False
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
                sc = r.get("scores") or ()       # คะแนนทุกคลาส: ดูคลาสอันตรายเอง ไม่ใช่แค่ป้ายที่ชนะ
                p = int(sc[cls] * 100 + 0.5) if (cls is not None and cls < len(sc)) else (
                    pct if r["label"] in danger else 0)
                hist, alerting = alert_rule(hist, alerting, p >= conf_min)
                S["label"], S["conf"], S["n"] = r["label"], pct, r["seq"]
            if fresh or act:
                was = S["state"]
                S["state"], refused = step(was, alerting, act, manual)
                if refused:                      # ไม่ใช้ say(): ui.poll() ในนั้นจะกลืนการแตะครั้งถัดไป
                    w["note"].color(COL_WARN)
                    w["note"].text("Still detected - cannot reset yet")
                if S["state"] != was and S["state"] == "alert":
                    S["events"] += 1
                if act or S["state"] != was:
                    draw_state(w, blink, manual)
            snap = snapshot()
            key = (snap["label"], snap["state"], snap["events"])
            if S["label"] != "-" and should_send(key != sent_key, time.ticks_diff(now, t_sent)):
                n_msg += 1
                last_msg = snap
                sent += 1 if send(online, snap) else 0
                t_sent, sent_key = now, key
            if online and not mqtt.is_connected():
                online = False
                show_link(w, False)
                say("Network lost - AI keeps running", COL_WARN)
            if time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn = now
                blink = not blink
                if S["state"] == "alert":
                    draw_state(w, blink, manual)
                w["raw"].text("ผลดิบ: %s  %d%%  เหตุ %d ครั้ง" % (S["label"], S["conf"], S["events"]))
                for i, d in enumerate(w["dots"]):
                    v = hist[i] if i < len(hist) else 0
                    if v != lit[i]:
                        d.value(v)
                        lit[i] = v
                if last_msg:
                    w["sent"].text("messages %d | reached broker %d" % (n_msg, sent))
                    line = json.dumps(last_msg)
                    for i, j in enumerate(w["j"]):
                        j.text(line[i * 50:(i + 1) * 50] or " ")   # 3 x 50 ตัว: JSON ยาวสุดราว 150
                    last_msg = None
            time.sleep_ms(100)
    finally:
        try:
            edge_ai.stop()
        except OSError:
            pass
        try:
            mqtt.disconnect()                # เรียกเสมอ แม้เน็ตหลุดหรือยังไม่เคยต่อ
        except OSError:
            pass


main()

# ลองต่อ: 1) สองทีมตั้ง KIT ต่างกัน ส่งพร้อมกัน - แดชบอร์ดแยกด้วยช่องไหน "id" หรือ "kit"?
# 2) วางนิ่ง 30 วินาที นับข้อความ - ทำไมยังมีข้อความทุก 2 วินาที ทั้งที่ไม่มีอะไรเปลี่ยน?
# 3) เพิ่มหัวข้อ .../event แบบไฟล์ 02 ส่งครั้งเดียวตอน state เปลี่ยน แทนการดูทุกข้อความ
