# 01_ai_to_mqtt.py - ผล AI บนบอร์ดขึ้น MQTT ทันที
# ชุดตัวอย่างประจำคาบ 12
# ทดสอบแล้ว : Dev Kit เฟิร์มแวร์ 2.4.4 ผ่าน 3/3 แบบออฟไลน์ (บอร์ดไม่ได้ต่อ WiFi) · ส่งขึ้น broker จริงยังไม่ได้ทดสอบ
#
# ไฟล์นี้สอน: ท่อ อ่านผล AI -> JSON -> mqtt.publish() ไปที่ bento-aiot/<TEAM>/ai
#             ส่งเมื่อป้ายเปลี่ยน + heartbeat ทุก ALIVE_EVERY_MS + ห่างกันอย่างน้อย GAP_MS
#             ไม่ส่งทุกผลดิบ: โมเดลตอบได้หลายครั้งต่อวินาที broker สาธารณะใช้ร่วมกันทั้งห้อง
# ดูที่จอ   : ป้ายผลตัวใหญ่ · ไฟ MQTT (เขียว = ต่อ broker อยู่) · ส่งไปกี่ข้อความ · JSON ล่าสุด
# ดูฝั่งคอม : mosquitto_sub -h broker.hivemq.com -t 'bento-aiot/<TEAM>/ai' -v
# กับดัก    : client id ซ้ำ = broker เตะตัวเก่าหลุด จึงต่อท้ายด้วยเลขจากนาฬิกาบอร์ด
#             "id" ใน JSON คือชื่อทีม ไม่ใช่ id ของโมเดล - ส่งแค่ "ชื่อ" โมเดลเท่านั้น
# ต่อจากคาบ 10: คาบ 10 ใช้ broker ในวง LAN - คาบนี้ใช้ broker.hivemq.com แบบคาบ 2-4
# Emulator  : MQTT จำลองอยู่ในเบราว์เซอร์ ข้อความไม่ออกไปถึง broker จริง
# ต้องแก้ก่อนรัน: TEAM · บอร์ดที่ต่อ WiFi ที่บันทึกไว้อยู่แล้วไม่ต้องแก้ WIFI_SSID WIFI_PASS
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
BROKER = "broker.hivemq.com"     # สำรอง: test.mosquitto.org
CLIENT_ID = "bento-ai-" + TEAM + "-%04x" % (time.ticks_ms() & 0xFFFF)
TOPIC = "bento-aiot/" + TEAM + "/ai"
MODEL_KEYS = ("AnomalousVibration", "Motion Detection")
ALIVE_EVERY_MS = 2000              # ป้ายไม่เปลี่ยนก็ส่งซ้ำทุกเท่านี้
GAP_MS = 200                     # ห้ามส่งถี่กว่านี้
WIFI_WAIT_S = 5                  # รอ WiFi ที่บันทึกไว้กี่วินาทีก่อนต่อเอง
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


def build_screen():
    ui.screen()
    time.sleep_ms(150)
    ui.Label("AI results to MQTT", x=12, y=6, color=COL_TEXT, value=24)
    w = {"model": ui.Label(" ", x=12, y=44, color=COL_INFO, value=20)}
    w["label"] = ui.Label("-", x=12, y=90, color=COL_TEXT, value=28)
    w["conf"] = ui.Label(" ", x=12, y=136, color=COL_DIM, value=20)
    ui.Label("MQTT", x=560, y=48, color=COL_DIM, value=16)
    w["led"] = ui.Led(x=560, y=74, w=36, h=36, color=COL_OK, value=0)
    w["link"] = ui.Label("offline", x=606, y=82, color=COL_DIM, value=16)
    w["sent"] = ui.Label(" ", x=12, y=190, color=COL_TEXT, value=16)
    w["j1"] = ui.Label(" ", x=12, y=222, color=COL_DIM, value=16)
    w["j2"] = ui.Label(" ", x=12, y=246, color=COL_DIM, value=16)
    w["note"] = ui.Label(" ", x=12, y=300, color=COL_WARN, value=16)
    ui.poll()
    return w


def show_link(w, online):
    w["led"].value(1 if online else 0)
    w["link"].text("connected" if online else "offline")


def main():
    w = build_screen()

    def say(text, col):
        w["note"].color(col)
        w["note"].text(text)
        ui.poll()

    found = find_model(MODEL_KEYS)
    if not found:
        say("No model named: " + " / ".join(MODEL_KEYS), COL_BAD)
        return
    idx, name = found
    w["model"].text("Model: " + name)
    online = False
    seq, lab, conf, sent_lab, n, sent = None, None, 0, None, 0, 0
    fresh, last_msg = None, None
    t0 = t_sent = drawn = time.ticks_ms()
    try:
        online = go_online(say)
        show_link(w, online)
        edge_ai.select(idx)
        t0 = time.ticks_ms()
        while time.ticks_diff(time.ticks_ms(), t0) < RUN_S * 1000:
            now = time.ticks_ms()
            r = edge_ai.result()
            if r and r["index"] == idx and r["seq"] != seq and r["label"] is not None:
                seq, lab, conf = r["seq"], r["label"], int(r["conf"] * 100)
                fresh = r
            if lab is not None and should_send(lab != sent_lab, time.ticks_diff(now, t_sent)):
                n += 1
                last_msg = {"id": TEAM, "n": n, "model": name, "label": lab, "conf": conf}
                sent += 1 if send(online, last_msg) else 0
                t_sent, sent_lab = now, lab
            if online and not mqtt.is_connected():
                online = False
                show_link(w, False)
                say("Network lost - AI keeps running", COL_WARN)
            if time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn = now
                if fresh:
                    w["label"].text(lab)
                    w["conf"].text("%d%%  latency %.1f ms" % (conf, fresh["latency_ms"]))
                    fresh = None
                if last_msg:
                    w["sent"].text("messages %d | reached broker %d" % (n, sent))
                    line = json.dumps(last_msg)
                    w["j1"].text(line[:60])
                    w["j2"].text(line[60:120] or " ")
                    last_msg = None
            ui.poll()
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

# ลองต่อ: 1) ตั้ง ALIVE_EVERY_MS = 10000 วางบอร์ดนิ่ง 30 วินาที - ข้อความลดลงเท่าไร?
# 2) ตั้ง GAP_MS = 0 แล้วเขย่า - ข้อความต่อวินาทีเพิ่มขึ้นแค่ไหน ทำไมห้องทั้งห้องถึงเดือดร้อน
