# 02_state_and_event.py - แยก "สถานะ" กับ "เหตุการณ์เตือน" คนละหัวข้อ
# ชุดตัวอย่างประจำคาบ 12
# ทดสอบแล้ว : Dev Kit เฟิร์มแวร์ 2.4.4 ผ่าน 3/3 แบบออฟไลน์ (บอร์ดไม่ได้ต่อ WiFi) · ส่งขึ้น broker จริงยังไม่ได้ทดสอบ
#
# ไฟล์นี้สอน: .../ai     = สถานะ ส่งเมื่อป้ายเปลี่ยน + heartbeat (แบบไฟล์ 01) + "alarm": 0/1
#             .../event  = ส่งครั้งเดียวตอน "เริ่มเตือน" (ai_alarm_start) และตอน "หายเตือน" (ai_alarm_end)
#             (หัวข้อ event เดิมของทีม แดชบอร์ดของห้องฟังอยู่แล้ว · ขึ้นต้น ai_ กันชนกับ event อื่น)
#             เตือนใช้กฎ k ใน n ของคาบ 11 (04_confidence_gate.py) ไม่ใช่ผลดิบผลเดียว
# ดูที่จอ   : สถานะ OK/ALARM · จำนวนข้อความแต่ละหัวข้อ · event ล่าสุด
# ดูฝั่งคอม : mosquitto_sub -h broker.hivemq.com -t 'bento-aiot/<TEAM>/#' -v
# ลองเล่น  : วางนิ่ง -> เขย่าแรงค้าง 3 วินาที -> วางนิ่ง · นับว่าการเขย่าหนึ่งครั้งได้ event กี่คู่
# Emulator  : MQTT ไม่ออกนอกเบราว์เซอร์ · shaking มั่นใจไม่ถึง 55% ตั้ง CONF_MIN = 50 ก่อนลอง
# ต้องแก้ก่อนรัน: TEAM · บอร์ดที่ต่อ WiFi ที่บันทึกไว้อยู่แล้วไม่ต้องแก้ WIFI_SSID WIFI_PASS

import edge_ai
import json
import mqtt
import time
import ui
import wifi

WIFI_SSID = "<Hotspot name>"
WIFI_PASS = "<Hotspot password>"
TEAM = "teamXX"
BROKER = "broker.hivemq.com"     # สำรอง: test.mosquitto.org
CLIENT_ID = "bento-ai-" + TEAM + "-%04x" % (time.ticks_ms() & 0xFFFF)
TOPIC = "bento-aiot/" + TEAM + "/ai"
TOPIC_EVENT = "bento-aiot/" + TEAM + "/event"
MODEL_KEYS = ("AnomalousVibration", "Motion Detection")
DANGER = ("anomaly", "shaking")
CONF_MIN = 60
CONFIRM_N = 3
WINDOW_N = 5
ALIVE_EVERY_MS = 2000
GAP_MS = 200
WIFI_WAIT_S = 5
DRAW_MS = 500
RUN_S = 300

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF


def find_model(keys):
    ms = edge_ai.models()
    for key in keys:
        for m in ms:
            if m["name"] == key:
                return m["index"], m["name"]
    return None


def alert_rule(hist, alerting, danger):
    hist = (hist + [int(danger)])[-WINDOW_N:]
    hits = sum(hist)
    return hist, hits >= CONFIRM_N or (alerting and hits > 0)


def should_send(changed, since_ms):
    if since_ms < GAP_MS:
        return False
    return changed or since_ms >= ALIVE_EVERY_MS


def wifi_up(say):
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
    if not (len(TEAM) == 6 and TEAM[:4] == "team" and TEAM[4:].isdigit() and TEAM != "team00"):
        say("Set TEAM first (team01-team99) - offline", COL_WARN)
        return False
    try:
        if not wifi_up(say):
            return False
        say("Connecting broker ...", COL_WARN)
        if mqtt.connect(BROKER, port=1883, client_id=CLIENT_ID, keepalive=60):
            say("Online: bento-aiot/" + TEAM + "/#", COL_OK)
            return True
    except OSError as e:
        print("network:", e)
    say("Broker failed - offline", COL_BAD)
    return False


def send(online, topic, obj):
    line = json.dumps(obj)
    print(topic, line)
    if online:
        try:
            return bool(mqtt.publish(topic, line))
        except OSError:
            pass
    return False


def event_msg(start, name, lab, conf):
    return {"id": TEAM, "event": "ai_alarm_start" if start else "ai_alarm_end",
            "model": name, "label": lab, "conf": conf}


def build_screen(name):
    ui.screen()
    time.sleep_ms(150)
    ui.Label("State + event: " + name, x=12, y=6, color=COL_TEXT, value=20)
    w = {"disc": ui.Panel(x=12, y=50, w=96, h=96, color=COL_OK, min=COL_DIM, max=48, value=0)}
    w["state"] = ui.Label("OK", x=128, y=78, color=COL_OK, value=28)
    w["raw"] = ui.Label(" ", x=12, y=164, color=COL_DIM, value=16)
    w["n"] = ui.Label(" ", x=12, y=200, color=COL_TEXT, value=16)
    w["ev"] = ui.Label(" ", x=12, y=236, color=COL_INFO, value=16)
    w["note"] = ui.Label(" ", x=12, y=300, color=COL_WARN, value=16)
    ui.poll()
    return w


def main():
    found = find_model(MODEL_KEYS)
    if not found:
        print("none of these models is on the board:", MODEL_KEYS)
        return
    idx, name = found
    w = build_screen(name)

    def say(text, col):
        w["note"].color(col)
        w["note"].text(text)
        ui.poll()

    online, hist, alerting = False, [], False
    seq, lab, conf, sent_lab = None, None, 0, None
    n_state = n_event = 0
    raw = None
    t0 = t_sent = drawn = time.ticks_ms()
    try:
        online = go_online(say)
        edge_ai.select(idx)
        t0 = time.ticks_ms()
        while time.ticks_diff(time.ticks_ms(), t0) < RUN_S * 1000:
            now = time.ticks_ms()
            r = edge_ai.result()
            if r and r["index"] == idx and r["seq"] != seq and r["label"] is not None:
                seq, lab, conf = r["seq"], r["label"], int(r["conf"] * 100)
                was = alerting
                hist, alerting = alert_rule(hist, alerting, lab in DANGER and conf >= CONF_MIN)
                raw = "raw: %s %d%%" % (lab, conf)
                if alerting != was:              # เหตุการณ์: ส่งครั้งเดียวตอนสถานะเปลี่ยน
                    ev = event_msg(alerting, name, lab, conf)
                    n_event += 1
                    send(online, TOPIC_EVENT, ev)
                    w["ev"].text("event: %s (%s %d%%)" % (ev["event"], lab, conf))
                    w["disc"].color(COL_BAD if alerting else COL_OK)
                    w["state"].text("ALARM" if alerting else "OK")
                    w["state"].color(COL_BAD if alerting else COL_OK)
            if lab is not None and should_send(lab != sent_lab, time.ticks_diff(now, t_sent)):
                n_state += 1
                send(online, TOPIC, {"id": TEAM, "n": n_state, "model": name, "label": lab,
                                     "conf": conf, "alarm": int(alerting)})
                t_sent, sent_lab = now, lab
            if online and not mqtt.is_connected():
                online = False
                say("Network lost - AI keeps running", COL_WARN)
            if raw and time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn = now
                w["raw"].text(raw)
                w["n"].text("sent: state %d | event %d" % (n_state, n_event))
            ui.poll()
            time.sleep_ms(100)
    finally:
        try:
            edge_ai.stop()
        except OSError:
            pass
        if alerting and online:                  # จบขณะเตือน: บอกแดชบอร์ดว่าหายเตือนแล้ว
            send(online, TOPIC_EVENT, event_msg(False, name, lab, conf))
        try:
            mqtt.disconnect()
        except OSError:
            pass


main()

# ลองต่อ: 1) ถ้าส่งเตือนทุกผลที่เป็น anomaly แทน แอปจะเด้งเตือนกี่ครั้งต่อการเขย่า 3 วินาที?
# 2) แอปที่เพิ่งเปิดทีหลังจะไม่รู้ว่ากำลังเตือนอยู่ - ช่องไหนในข้อความ .../ai ช่วยได้?
