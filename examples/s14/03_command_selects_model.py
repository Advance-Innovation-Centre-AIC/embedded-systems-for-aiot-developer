# 03_command_selects_model.py - แอปสั่งเปลี่ยนโมเดล/หยุด ผ่าน MQTT อย่างปลอดภัย
# ชุดตัวอย่างประจำคาบ 12
# ทดสอบแล้ว : Dev Kit เฟิร์มแวร์ 2.4.4 ผ่าน 3/3 แบบออฟไลน์ (บอร์ดไม่ได้ต่อ WiFi) · รับคำสั่งจาก broker จริงยังไม่ได้ทดสอบ
#
# ไฟล์นี้สอน: subscribe .../ai/cmd แล้วถาม mqtt.get_message() ในลูปหลัก
#             {"cmd":"select","model":"HumanActivity"} = เปลี่ยนโมเดล · {"cmd":"stop"} · {"cmd":"run"}
#             ทำตามเฉพาะชื่อใน ALLOWED ที่มีบนบอร์ดจริง · รับคำสั่งห่างกันอย่างน้อย CMD_GAP_MS
#             เปลี่ยนเสร็จส่งสถานะที่ .../ai ทันที (ช่อง model = คำตอบว่าทำแล้ว · หยุดอยู่ = "")
# ดูที่จอ   : โมเดลที่รันอยู่ · ผลล่าสุด · คำสั่งล่าสุด (ทำ / ไม่ทำ เพราะอะไร)
# ลองส่ง    : mosquitto_pub -h broker.hivemq.com -t 'bento-aiot/<TEAM>/ai/cmd'
#               -m '{"cmd":"select","model":"HumanActivity"}'
# กับดัก    : broker สาธารณะไม่เข้ารหัส ใครก็ส่งคำสั่งเข้าหัวข้อนี้ได้ - ต้องกรองทุกช่อง
#             บอร์ดเก็บข้อความรอไว้แค่ข้อความเดียว ข้อความใหม่ทับของเก่า
#             ถ้ามีคนส่งแบบ retain ไว้ คำสั่งนั้นจะมาใหม่ทุกครั้งที่รันโปรแกรม
#             select() รอจนแกน AI ยืนยัน (ช้าได้หลายวินาที) จอค้างช่วงนั้น จึงขึ้นป้ายก่อนเรียก
# Emulator  : MQTT ไม่ออกนอกเบราว์เซอร์ ใช้ mosquitto_pub ไม่ได้
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
TOPIC_CMD = TOPIC + "/cmd"
ALLOWED = ("AnomalousVibration", "HumanActivity", "Motion Detection")   # รับเฉพาะชื่อเหล่านี้
CMD_GAP_MS = 3000        # คำสั่งที่มาถี่กว่านี้ไม่ทำ
MAX_CMD_BYTES = 200      # ข้อความยาวกว่านี้ไม่อ่าน
ALIVE_EVERY_MS = 2000
GAP_MS = 200
WIFI_WAIT_S = 5
DRAW_MS = 500
RUN_S = 600

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF


def known_models():
    # {ชื่อ: ลำดับ} เฉพาะชื่อใน ALLOWED ที่อยู่บนบอร์ดตอนนี้
    return {m["name"]: m["index"] for m in edge_ai.models() if m["name"] in ALLOWED}


def parse_cmd(raw, known):
    # คืน ("model", ชื่อ) / ("stop",) / ("run",) หรือ (None, เหตุผล)
    if len(raw) > MAX_CMD_BYTES:
        return None, "too long"
    try:
        c = json.loads(raw.decode())
    except Exception:            # ข้อความจากใครก็ได้: พังแบบไหนก็ต้องไม่ล้มทั้งโปรแกรม
        return None, "not JSON"
    if not isinstance(c, dict):
        return None, "not a JSON object"
    cmd = c.get("cmd")
    if cmd == "select":          # เช็กชนิดก่อน: {"model": [1]} จะทำให้ in known ล้ม
        m = c.get("model")
        return ("model", m) if isinstance(m, str) and m in known else (None, "unknown model")
    if cmd in ("stop", "run"):
        return (cmd,)
    return None, "unknown command"


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
        if mqtt.connect(BROKER, port=1883, client_id=CLIENT_ID, keepalive=60) and mqtt.subscribe(TOPIC_CMD):
            say("Listening: " + TOPIC_CMD, COL_OK)
            return True
    except OSError as e:
        print("network:", e)
    say("Broker failed - offline (no commands)", COL_BAD)
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


def stop_ai():
    try:
        edge_ai.stop()
    except OSError:
        pass


def build_screen():
    ui.screen()
    time.sleep_ms(150)
    ui.Label("Commands pick the model", x=12, y=6, color=COL_TEXT, value=24)
    w = {"model": ui.Label(" ", x=12, y=48, color=COL_INFO, value=20)}
    w["res"] = ui.Label("-", x=12, y=88, color=COL_TEXT, value=28)
    ui.Label("last command", x=12, y=150, color=COL_DIM, value=16)
    w["cmd"] = ui.Label("(none yet)", x=12, y=174, color=COL_TEXT, value=16)
    w["n"] = ui.Label(" ", x=12, y=214, color=COL_DIM, value=16)
    w["note"] = ui.Label(" ", x=12, y=300, color=COL_WARN, value=16)
    ui.poll()
    return w


def main():
    w = build_screen()

    def say(text, col):
        w["note"].color(col)
        w["note"].text(text)
        ui.poll()

    known = known_models()
    if not known:
        say("None of ALLOWED is on this board", COL_BAD)
        return
    print("models I accept:", sorted(known))
    name = [a for a in ALLOWED if a in known][0]
    want, running, online = ("model", name), False, False
    seq, lab, conf, sent_lab, n = None, None, 0, None, 0
    done = refused = 0
    shown = None
    t_cmd = time.ticks_add(time.ticks_ms(), -CMD_GAP_MS)
    t0 = t_sent = drawn = time.ticks_ms()
    try:
        online = go_online(say)
        t0 = time.ticks_ms()
        while time.ticks_diff(time.ticks_ms(), t0) < RUN_S * 1000:
            now = time.ticks_ms()
            msg = mqtt.get_message() if online else None
            if msg:
                c = parse_cmd(msg[1], known)
                if time.ticks_diff(now, t_cmd) < CMD_GAP_MS:
                    c = (None, "too soon")
                if c[0]:
                    want, t_cmd = c, now
                    done += 1
                    w["cmd"].text("do: %s" % " ".join(c))
                else:
                    refused += 1
                    w["cmd"].text("ignored: " + c[1])
                w["n"].text("commands done %d | ignored %d" % (done, refused))
            if want:                             # ทำคำสั่งในลูปหลักเท่านั้น
                if want[0] == "stop":
                    stop_ai()
                    running = False
                else:
                    name = want[1] if want[0] == "model" else name
                    w["model"].text("Loading " + name + " ...")
                    ui.poll()
                    try:
                        edge_ai.select(known[name])
                        running = True
                    except OSError as e:
                        running = False
                        say("select failed: %s" % e, COL_BAD)
                want, seq, lab, sent_lab = None, None, None, None
                w["model"].text(("Running: " if running else "Stopped: ") + name)
                t_sent = time.ticks_add(now, -ALIVE_EVERY_MS)   # ส่งสถานะใหม่ทันที
            r = edge_ai.result() if running else None
            if r and r["index"] == known[name] and r["seq"] != seq and r["label"] is not None:
                seq, lab, conf = r["seq"], r["label"], int(r["conf"] * 100)
            if should_send(lab != sent_lab, time.ticks_diff(now, t_sent)):
                n += 1
                send(online, {"id": TEAM, "n": n, "model": name if running else "",
                              "label": lab or "-", "conf": conf if lab else 0})   # "-" = ไม่มีผล
                t_sent, sent_lab = now, lab
            if online and not mqtt.is_connected():
                online = False
                say("Network lost - AI keeps running", COL_WARN)
            res = "%s  %d%%" % (lab, conf) if lab else "-"
            if res != shown and time.ticks_diff(now, drawn) >= DRAW_MS:
                drawn, shown = now, res
                w["res"].text(res)
            ui.poll()
            time.sleep_ms(50)
    finally:
        stop_ai()
        try:
            mqtt.disconnect()                    # เรียกเสมอ แม้เน็ตหลุดหรือ subscribe ไม่สำเร็จ
        except OSError:
            pass


main()

# ลองต่อ: 1) ส่ง {"cmd":"select","model":"SirenDetection"} - ทำไมบอร์ดไม่ทำ ทั้งที่มีโมเดลนี้อยู่?
# 2) ส่งคำสั่งติดกันสองครั้งภายใน 1 วินาที - ครั้งที่สองเป็นอย่างไร ถ้าไม่มีกฎนี้จะเกิดอะไร
