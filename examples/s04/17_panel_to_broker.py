# 17_panel_to_broker.py - แตะบนจอบอร์ด แล้วหน้าเว็บของเพื่อนเห็น สั่งจากเว็บ แล้วแถบบนจอขยับ
#
# ส่วนขยายของคาบ 4 ไม่อยู่ในเกณฑ์ผ่าน M4 ใช้ของจากคาบ 2 (wifi + mqtt) กับของจากคาบนี้
# (ปุ่ม แถบเลื่อน และ ui.poll) มาต่อกันเป็นวงเดียว ชื่อ broker ทีม และหัวข้อ
# เหมือนไฟล์ของคาบ 2 และคาบ 3 ทุกตัวอักษร หน้าเว็บ examples/web/mqtt_dashboard.html
# จึงเห็นบอร์ดนี้โดยไม่ต้องตั้งอะไรเพิ่ม
#
# ไฟล์นี้สอน: เหตุการณ์บนจอมีสองชนิด ปุ่มคือ "เรื่องที่เกิดขึ้นครั้งหนึ่ง" จึงส่งเป็น event
#             ทันทีที่แตะ ส่วนแถบเลื่อนคือ "ค่าที่เป็นอยู่" จึงไปกับ telemetry ทุก 2 วินาที
#             ไม่ใช่ส่งทุก value_changed ที่แถบยิงถี่ ๆ ระหว่างนิ้วลาก
# ดูที่จอ   : การ์ดบนคือปุ่มกับแถบเลื่อนของเรา การ์ดล่างคือสาย จำนวนใบที่ส่ง และคำสั่งล่าสุด
#             ส่ง {"cmd":"set","v":80} จากหน้าเว็บ แล้วแถบกับตัวเลขบนจอจะขยับเอง
# กับดัก    : เฟิร์มแวร์นี้ส่ง retain ไม่ได้ (modmqtt.c ตั้ง retain=false ตายตัว)
#             หน้าเว็บที่เปิดทีหลังจะไม่เห็นอะไรเลยจนกว่าจะมีใบใหม่ จึงต้องส่งซ้ำเป็นจังหวะ
#             และข้อความจากเว็บเป็นของคนอื่น ต้องกรองก่อนขึ้นจอ อักขระที่จอวาดไม่ได้จะเป็นกล่อง
#
# พอร์ต 1883 ไม่เข้ารหัส และ broker.hivemq.com เป็นของสาธารณะ ใครก็ subscribe หัวข้อเราได้
# ห้ามส่งอะไรที่เป็นความลับ คาบ 11 เราจะย้ายไปทางที่เข้ารหัส

import json
import lcd
import time
import ui
import wifi
import mqtt

# แก้บรรทัดเหล่านี้ให้ตรงกับที่ผู้สอนแจก ชื่อชุดนี้ใช้เหมือนกันทั้งคอร์ส
WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
BROKER = "broker.hivemq.com"      # สำรอง: "test.mosquitto.org"
TEAM = "teamXX"                   # แก้เป็นเลขทีมที่ผู้สอนแจก team01 ถึง team12
ROOT = "bento-aiot"               # คำนำหน้าเดียวของทั้งห้อง
DEVICE_ID = "bento-aiot-" + TEAM  # client_id ยาว 17 ตัว ไม่เกิน 31
TOPIC_TELE = ROOT + "/" + TEAM + "/telemetry"
TOPIC_EVENT = ROOT + "/" + TEAM + "/event"
TOPIC_CMD = ROOT + "/" + TEAM + "/cmd"

RUN_MS = 120000      # เปิดแผงนานเท่าไร
TELE_MS = 2000       # ส่งค่าที่เป็นอยู่ซ้ำทุกกี่ ms
POLL_MS = 100        # ถามจอกับกล่องรับถี่แค่ไหน

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC S7.13
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

HAS_SOUND = hasattr(ui, "tone")

ui.screen()
time.sleep_ms(200)

ui.Label("แผงของเราบน broker", x=24, y=8, color=COL_TEXT, value=24)
status = ui.Label("กำลังจะเริ่ม", x=384, y=16, color=COL_DIM, value=20)

# การ์ดบน: ของที่นิ้วเราแตะ ปุ่มกับแถบเลื่อนสูงยาวพอให้นิ้วตามเกณฑ์ 88 px
ui.Panel(x=24, y=56, w=744, h=128, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
btn = ui.Button("ส่ง event", x=40, y=72, w=200, h=88, color=COL_ACCENT,
                value=24)
btn_id = btn.id()
ui.Label("ค่าที่ตั้ง 0-100", x=272, y=72, color=COL_DIM, value=16)
sld = ui.Slider(x=272, y=120, w=320, h=24, color=COL_ACCENT, min=0, max=100,
                value=50)
sld_id = sld.id()
seg = ui.Seg7(text="50", x=624, y=88, w=128, h=56, color=COL_ACCENT)

# การ์ดกลาง: สามแถว แถวละเรื่อง สาย ใบที่ส่ง และคำสั่งล่าสุดจากเว็บ
# แต่ละแถวได้ความกว้างเต็มการ์ด เพราะชื่อ broker กับข้อความจากเว็บยาวได้ถึงราว 400 px
ui.Panel(x=24, y=200, w=744, h=128, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
link_lbl = ui.Label("สาย: ยังไม่ได้ต่อ", x=40, y=208, color=COL_WARN, value=20)
sent_lbl = ui.Label("ส่งแล้ว telemetry 0  event 0  หาย 0", x=40, y=248,
                    color=COL_TEXT, value=20)
last_lbl = ui.Label("ยังไม่มีคำสั่งจากเว็บ", x=40, y=288, color=COL_DIM,
                    value=20)

# บรรทัดล่างจบก่อน x=690 เพราะมุมขวาล่างเป็นของปุ่ม Console
note = ui.Label("หัวข้อ " + ROOT + "/" + TEAM + "/...", x=24, y=344,
                color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>แผงของเราบน broker</h2>")


def stop_here(screen_msg, log_msg):
    """ปิดงานอย่างสุภาพ บอกบนจอว่าไปไม่ถึงไหน แล้วจบ ไม่ค้างรอ"""
    link_lbl.color(COL_BAD)
    link_lbl.text(screen_msg)
    note.color(COL_BAD)
    note.text(log_msg)
    status.color(COL_BAD)
    status.text("จบตรงนี้")
    ui.poll()
    lcd.print("<span class=error>" + log_msg + "</span>")
    print("หยุดที่:", log_msg)
    raise SystemExit


def screen_safe(text, n):
    """เก็บเฉพาะอักขระที่จอวาดได้ (ASCII กับไทย) และตัดให้ไม่เกิน n ตัว

    ข้อความจากเว็บเป็นของคนอื่น อิโมจิหรือเครื่องหมายแปลก ๆ หนึ่งตัว
    ทำให้ทั้งบรรทัดกลายเป็นกล่องเปล่า และไทยตัวละ 3 ไบต์ ป้ายรับได้ 126 ไบต์
    """
    out = ""
    for ch in text:
        c = ord(ch)
        if 0x20 <= c <= 0x7E or 0x0E00 <= c <= 0x0E7F:
            out = out + ch
        else:
            out = out + "?"
        if len(out) >= n:
            break
    return out


# --- ขั้นที่ 0: ชื่อทีมต้องเป็นของเราจริง ---
# ค่าตั้งต้น teamXX จงใจให้รันไม่ผ่าน ถ้าปล่อยเป็นเลขทีมจริงไว้ ทุกบอร์ดที่ลืมแก้จะใช้
# client_id เดียวกัน แล้ว broker จะเตะบอร์ดของทีมนั้นออกโดยไม่มีคำเตือน
# team00 สงวนไว้ให้บอร์ดของผู้สอน
if len(TEAM) != 6 or TEAM[:4] != "team" or not TEAM[4:].isdigit() or TEAM == "team00":
    stop_here("ยังไม่ได้ตั้งชื่อทีม", "แก้ TEAM เป็นเลขทีมของคุณก่อน เช่น team03")

# --- ขั้นที่ 1: WiFi ต้องได้ IP ก่อน --- (บันไดเดียวกับ examples/s02/05)
status.color(COL_WARN)
status.text("กำลังต่อ WiFi จอจะนิ่ง")
ui.poll()
if not wifi.connect(WIFI_SSID, WIFI_PASS):
    heard = False
    try:
        # scan() เองก็ล้มได้ ทางที่ใช้อธิบายความล้มเหลวต้องไม่ล้มซ้อนเสียเอง
        for net in wifi.scan():
            if net[0] == WIFI_SSID:
                heard = True
    except OSError:
        pass
    if heard:
        stop_here("ต่อ WiFi ไม่ติด", "ได้ยินวง " + WIFI_SSID + " แต่ต่อไม่ผ่าน ตรวจรหัส")
    stop_here("ต่อ WiFi ไม่ติด", "ไม่ได้ยินวง " + WIFI_SSID + " เลย")
ip = wifi.ip()
if ip == "0.0.0.0":
    stop_here("ไม่มีเลข IP", "ลิงก์ขึ้นแต่ DHCP ไม่ให้เลข ส่งอะไรออกไม่ได้")

# --- ขั้นที่ 2: ต่อ broker แล้วขอฟังหัวข้อคำสั่งของทีมเรา ---
# client_id ชนกับบอร์ดอื่นเมื่อไร broker จะเตะตัวเก่าออกโดยไม่มีคำเตือน
link_lbl.text("สาย: IP " + ip + " กำลังต่อ " + BROKER)
ui.poll()
try:
    linked = mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID, keepalive=60)
except OSError:
    stop_here("broker ไม่ตอบ", BROKER + " ไม่ตอบ เครือข่ายอาจปิดพอร์ต 1883")
if not linked:
    stop_here("broker ปฏิเสธ", "ลองสำรอง test.mosquitto.org")
if not mqtt.subscribe(TOPIC_CMD):
    stop_here("subscribe ไม่ผ่าน", "broker ไม่ยอมให้ฟัง " + TOPIC_CMD)

link_lbl.color(COL_OK)
link_lbl.text("สาย: ต่อแล้ว " + BROKER)
status.color(COL_DIM)
status.text("แตะปุ่ม ลากแถบ หรือสั่งจากเว็บ")
ui.poll()
lcd.print("<span class=ok>ฟังคำสั่งที่", TOPIC_CMD, "</span>")
lcd.print('ลองส่ง {"cmd":"set","v":80} หรือ {"cmd":"say","text":"hi"}')

# --- ขั้นที่ 3: ลูปเดียวทำสามงาน อ่านจอ อ่านกล่องรับ ส่งค่าตามจังหวะ ---
value = 50           # ค่าของแถบ ตัวแปรนี้คือความจริง แถบบนจอเป็นแค่หน้าตาของมัน
presses = 0
n_tele = 0
n_event = 0
t0 = time.ticks_ms()
t_tele = t0


n_lost = 0           # ใบที่ publish() ตอบ False ใบนั้นหาย แต่สายยังอยู่


def send(topic, obj):
    """publish หนึ่งใบ คืน False เฉพาะเมื่อสายหลุด

    publish() มีสองทางล้ม ตอบ False คือชั้นเครือข่ายไม่รับใบนี้ ใบนั้นหายแต่สายยังอยู่
    นับไว้แล้วไปต่อ ส่วน OSError คือสายหลุดแล้ว อันนี้จึงจบงาน (แบบเดียวกับ s03/07)
    """
    global n_lost
    try:
        if not mqtt.publish(topic, json.dumps(obj)):
            n_lost = n_lost + 1
        return True
    except OSError:
        return False


def set_value(v):
    """ประตูเดียวที่เปลี่ยนค่าแถบ ทั้งนิ้วบนจอและคำสั่งจากเว็บต้องผ่านที่นี่"""
    global value
    # ใครก็ส่งเข้าหัวข้อนี้ได้ 1e999 กลายเป็น inf ให้ OverflowError ส่วน NaN ให้ ValueError
    # ค่าที่แปลงไม่ได้ ไม่เปลี่ยนอะไรเลย ค่าที่แปลงได้ถูกหนีบไว้ในพิสัยของแถบ
    try:
        v = int(v)
    except (ValueError, OverflowError, TypeError):
        return False
    value = max(0, min(100, v))
    sld.value(value)
    seg.text(str(value))
    return True


alive = True
while alive and time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    t_work = time.ticks_ms()

    # จอ: ปุ่มคือเหตุการณ์ ส่งทันที · แถบคือค่า จำไว้แล้วรอไปกับ telemetry
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == btn_id:
            presses = presses + 1
            n_event = n_event + 1
            if not send(TOPIC_EVENT, {"id": TEAM, "n": n_event, "ev": "press",
                                      "v": value}):
                alive = False
        elif ev["type"] == "value_changed" and ev["handle"] == sld_id:
            set_value(ev["value"])

    # กล่องรับมีช่องเดียว ข้อความใหม่ทับข้อความเก่า จึงถามทุกรอบ
    msg = mqtt.get_message()
    if msg is not None:
        try:
            cmd = json.loads(msg[1].decode())
        except ValueError:
            cmd = {}
        action = cmd.get("cmd", "") if isinstance(cmd, dict) else ""
        if action == "set":
            v = cmd.get("v", value)
            if isinstance(v, (int, float)) and set_value(v):
                last_lbl.color(COL_ACCENT)
                last_lbl.text("เว็บตั้งค่าเป็น " + str(value))
            else:
                last_lbl.color(COL_WARN)
                last_lbl.text("set ได้ค่าที่ใช้ไม่ได้ ไม่เปลี่ยนอะไร")
        elif action == "say":
            last_lbl.color(COL_ACCENT)
            last_lbl.text("เว็บบอกว่า " + screen_safe(str(cmd.get("text", "")), 24))
        elif action == "beep":
            # ui.tone รับโน้ต MIDI ไม่ใช่เฮิรตซ์ และเฟิร์มแวร์บางรุ่นไม่มีเสียง จึงถามก่อน
            if HAS_SOUND:
                ui.tone(69, ui.WAVE_SQUARE, 90, 150)
            last_lbl.color(COL_ACCENT)
            last_lbl.text("beep" if HAS_SOUND else "beep แต่บอร์ดนี้ไม่มีเสียง")
        else:
            last_lbl.color(COL_WARN)
            last_lbl.text("ไม่รู้จักคำสั่งนี้")

    # ค่าที่เป็นอยู่ส่งซ้ำตามจังหวะ หน้าเว็บที่เพิ่งเปิดจึงเห็นภายในสองวินาที
    # ใบแรกส่งทันที (n_tele ยังเป็น 0) ไม่ต้องรอให้ครบสองวินาทีก่อน
    if alive and (n_tele == 0 or time.ticks_diff(t_work, t_tele) >= TELE_MS):
        t_tele = t_work
        n_tele = n_tele + 1
        if not send(TOPIC_TELE, {"id": TEAM, "n": n_tele, "v": value,
                                 "presses": presses}):
            alive = False

    if not alive or not mqtt.is_connected():
        alive = False
        link_lbl.color(COL_BAD)
        link_lbl.text("สาย: หลุดแล้ว เฟิร์มแวร์ไม่ต่อใหม่ให้เอง")
    sent_lbl.text("ส่งแล้ว telemetry " + str(n_tele) + "  event " + str(n_event) +
                  "  หาย " + str(n_lost))
    # ไม่เรียก ui.poll() ซ้ำตรงนี้ เหตุการณ์ที่มันคืนมาจะถูกทิ้งโดยไม่มีใครอ่าน
    # การแตะปุ่มที่ตกลงในจังหวะนั้นจะหายไปเงียบ ๆ poll ต้นลูปรอบหน้าวาดจอให้อยู่แล้ว

    rest = POLL_MS - time.ticks_diff(time.ticks_ms(), t_work)
    if rest > 0:
        time.sleep_ms(rest)

status.color(COL_DIM)
status.text("จบแล้ว ส่ง " + str(n_tele + n_event) + " ใบ")
ui.poll()
lcd.print("<span class=ok>ส่ง telemetry", n_tele, "ใบ event", n_event, "ใบ</span>")
print("ส่ง telemetry", n_tele, "ใบ | event", n_event, "ใบ | หาย", n_lost, "ใบ")

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# เปิด examples/web/mqtt_dashboard.html บนคอม เลือกทีมเรา แล้วลากแถบบนจอบอร์ด
# นับดูว่าเลขบนเว็บตามทันภายในกี่วินาที แล้วลองเปลี่ยน TELE_MS เป็น 500 กับ 5000
# ใบ้: ถ้าอยากให้เว็บเห็นทุกครั้งที่นิ้วขยับ ให้คิดก่อนว่าทั้งห้องสิบสองทีมจะส่งกี่ใบต่อวินาที
