# 07_button_to_broker.py - ปุ่มกับไฟขึ้น broker ให้หน้าเว็บอ่านได้
#
# What: กดปุ่มส่ง event ทันที ทุก 2 วินาทีส่ง telemetry และรับคำสั่งจากเว็บมาจุดไฟ
# ดูที่จอ: เลขใหญ่คือจำนวนกด การ์ดล่างคือ leds ที่ส่งออกกับคำสั่งล่าสุด
#          เปิด examples/web/my_first_reader.html ตั้ง TEAM ให้ตรง แล้วกดปุ่ม
# กับดัก : ส่ง leds จากตัวแปร ไม่ใช่ gpio.led(i).value() ที่ตอบระดับขา
#          ไม่กันเด้งก่อน publish กดครั้งเดียวจะเป็นหลายใบ
# broker สาธารณะ พอร์ต 1883 ไม่เข้ารหัส ห้ามส่งของลับ
# บน Emulator mqtt เป็นของจำลอง ไม่มีอะไรออกไปจริง

import gpio
import json
import lcd
import sensors
import time
import ui
import wifi
import mqtt

# แก้สี่บรรทัดนี้ให้ตรงกับที่ผู้สอนแจก
WIFI_SSID = "bento-teamXX"            # Hotspot มือถือ (WiFi คณะต้อง login บอร์ดใช้ไม่ได้)
WIFI_PASS = "<รหัส Hotspot ของทีม>"     # อย่างน้อย 8 ตัว
BROKER = "broker.hivemq.com"      # สำรอง: "test.mosquitto.org"
TEAM = "teamXX"                   # team01 ถึง team19 ไม่แก้ก็ไม่ยอมรัน

# สองบรรทัดนี้ห้ามแก้
ROOT = "bento-aiot"
DEVICE_ID = "bento-aiot-" + TEAM  # เฟิร์มแวร์ตัด client_id ที่ 31

TOPIC_EVENT = ROOT + "/" + TEAM + "/event"
TOPIC_STATE = ROOT + "/" + TEAM + "/telemetry"
TOPIC_CMD = ROOT + "/" + TEAM + "/cmd"

DEBOUNCE_MS = 40
POLL_MS = 5
STATE_MS = 2000
RUN_MS = 180000

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

class Stop(Exception):
    # จบโปรแกรมแบบปกติ (SystemExit ทำให้บอร์ดเริ่มระบบใหม่ และอาจค้างจนต้องถอดสาย)
    pass

N = gpio.num_leds()
btn = gpio.button(0)

# ดับทุกดวงก่อน หลอดจริงจะได้ตรงกับตัวแปร leds
for i in range(N):
    gpio.led(i).off()

leds = [0] * N

ui.screen()
time.sleep_ms(200)

ui.Label("ปุ่มกับไฟขึ้น broker", x=24, y=8, color=COL_TEXT, value=24)
status = ui.Label("กำลังจะเริ่ม", x=344, y=16, color=COL_DIM, value=20)

ui.Panel(x=24, y=56, w=744, h=120, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
ui.Label("กดไปแล้ว (ครั้ง)", x=40, y=72, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=40, y=104, w=144, h=56, color=COL_ACCENT)
ui.Label("ส่งออกไปแล้ว", x=224, y=72, color=COL_DIM, value=16)
sent_lbl = ui.Label("event 0 | telemetry 0", x=224, y=104, color=COL_TEXT,
                    value=20)
err_lbl = ui.Label("ส่งไม่ผ่าน 0 | คำสั่งเสีย 0", x=224, y=140, color=COL_DIM,
                   value=16)

ui.Panel(x=24, y=192, w=744, h=128, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
ui.Label("leds ที่ส่งออก (จากตัวแปร)", x=40, y=208, color=COL_DIM, value=16)
leds_lbl = ui.Label("leds = " + str(leds), x=40, y=232, color=COL_TEXT,
                    value=24)
ui.Label("คำสั่งล่าสุดจากเว็บ", x=400, y=208, color=COL_DIM, value=16)
last_lbl = ui.Label("ยังไม่มี", x=400, y=232, color=COL_DIM, value=24)
topic_lbl = ui.Label("ยังไม่ได้ต่อ broker", x=40, y=280, color=COL_DIM,
                     value=16)
az_lbl = ui.Label("az ยังไม่ได้อ่าน", x=400, y=280, color=COL_DIM, value=16)

# สร้างป้ายรับได้ 95 ไบต์ .text() รับได้ 126
note = ui.Label("ห้ามส่งของลับ", x=24, y=336, color=COL_WARN, value=16)
note.text("broker สาธารณะ พอร์ต 1883 ไม่เข้ารหัส ห้ามส่งของลับ")
ui.poll()

lcd.clear()
lcd.console("<h2>ปุ่มกับไฟขึ้น broker</h2>")


def stop_here(screen_msg, log_msg):
    """ปิดงานอย่างสุภาพ บอกบนจอว่าไปไม่ถึงไหน แล้วจบ ไม่ค้างรอ"""
    status.color(COL_BAD)
    status.text(screen_msg)
    note.color(COL_BAD)
    note.text(log_msg)
    ui.poll()
    lcd.print("<span class=error>" + log_msg + "</span>")
    print("หยุดที่:", log_msg)
    raise Stop


# --- ก่อนบันไดสามขั้น: ชื่อทีมต้องเป็นของเราจริง ---
# ใช้ชื่อทีมคนอื่น broker จะเตะทีมนั้นหลุด team00 ของผู้สอน ห้ามใช้
try:
    if len(TEAM) != 6 or TEAM[:4] != "team" or not TEAM[4:].isdigit() or TEAM == "team00":
        stop_here("ยังไม่ได้ตั้งชื่อทีม", "แก้ TEAM เป็นเลขทีมของคุณก่อน เช่น team03")

    # --- บันไดสามขั้นของคาบ 2: WiFi -> IP -> broker ---
    # ป้ายต้องขึ้นก่อน connect() ที่บล็อกได้ราว 85 วินาที
    status.color(COL_WARN)
    status.text("กำลังต่อ WiFi จอจะนิ่งสักครู่")
    ui.poll()
    lcd.print("1) กำลังต่อ WiFi", WIFI_SSID)

    if not wifi.connect(WIFI_SSID, WIFI_PASS):
        heard = False
        try:
            for net in wifi.scan():
                if net[0] == WIFI_SSID:
                    heard = True
        except OSError:
            pass              # สแกนไม่ได้ก็ต้องบอกเหตุบนจอ
        # .text() รับได้ 126 ไบต์ ไทยตัวละ 3 จึงต้องสั้น
        if heard:
            why = "ได้ยินวง " + WIFI_SSID + " แต่ต่อไม่ผ่าน ตรวจรหัสผ่าน"
        else:
            why = "ไม่ได้ยินวง " + WIFI_SSID + " ตรวจชื่อวง"
        stop_here("ต่อ WiFi ไม่ติด", why)

    # "0.0.0.0" ไม่ใช่สตริงว่าง if wifi.ip(): จึงผ่านทั้งที่ไม่มีที่อยู่
    ip = wifi.ip()
    for _ in range(15):
        if ip != "0.0.0.0":
            break
        ui.poll()
        time.sleep_ms(200)
        ip = wifi.ip()
    if ip == "0.0.0.0":
        stop_here("ไม่มีเลข IP", "ได้ลิงก์แต่ DHCP ไม่ให้เลข IP")

    status.text("IP " + ip + " กำลังต่อ broker")
    ui.poll()
    lcd.print("2) ได้ IP", ip, "กำลังต่อ", BROKER)

    # client_id ซ้ำ อีกตัวถูกเตะ ทีมละบอร์ดเดียว
    try:
        linked = mqtt.connect(BROKER, 1883, client_id=DEVICE_ID)
    except OSError:
        stop_here("broker ไม่ตอบ", "ต่อ " + BROKER + " ไม่ได้ ลองตัวสำรอง")

    if not linked:
        stop_here("broker ปฏิเสธ", "ตรวจชื่อ broker กับพอร์ต 1883")

    # subscribe หลัง connect ฟังเฉพาะ cmd ของทีมเรา
    if not mqtt.subscribe(TOPIC_CMD):
        stop_here("subscribe ไม่ผ่าน", "broker ไม่ยอมให้ฟัง " + TOPIC_CMD)

    status.color(COL_OK)
    status.text("ต่อแล้ว กดปุ่มบนบอร์ดได้เลย")
    topic_lbl.color(COL_TEXT)
    topic_lbl.text(ROOT + "/" + TEAM + "/#")
    ui.poll()
    lcd.print("<span class=ok>3) ต่อ broker แล้ว ฟัง", TOPIC_CMD, "</span>")


    def show_counts():
        """ตัวนับทั้งหมดอยู่ในสองป้าย เขียนใหม่เฉพาะตอนที่เลขเปลี่ยน"""
        sent_lbl.text("event " + str(n_event) + " | telemetry " + str(n_state))
        err_lbl.text("ส่งไม่ผ่าน " + str(n_fail) + " | คำสั่งเสีย " + str(n_bad))


    def send(topic, payload):
        """ส่งหนึ่งใบ คืน False ถ้าสายหลุด

        publish() ตอบ False เมื่อชั้นเครือข่ายไม่รับใบนี้ ใบนั้นหายแต่สายยังอยู่ นับแล้วไปต่อ
        แต่ถ้าสายหลุดไปแล้ว มันไม่คืน False มันโยน OSError ต้องดักทั้งสองทาง
        """
        global n_msg, n_fail
        n_msg = n_msg + 1
        payload["id"] = TEAM
        payload["n"] = n_msg
        try:
            ok = mqtt.publish(topic, json.dumps(payload))
        except OSError:
            return False
        if not ok:
            n_fail = n_fail + 1
        return True


    def read_az():
        """ความเร่งแกน z เป็น m/s2 ปัดสองตำแหน่ง อ่านไม่ได้รอบนี้คืน None"""
        try:
            ax, ay, az = sensors.bmi270.acceleration()
            return round(az, 2)
        except OSError:
            return None


    def screen_safe(text, n):
        """เก็บเฉพาะอักขระที่จอวาดได้ (ASCII กับไทย) แล้วตัดให้ไม่เกิน n ตัว

        ข้อความจากเว็บเป็นของคนอื่น อิโมจิหนึ่งตัวทำให้ทั้งบรรทัดกลายเป็นกล่องเปล่า
        แบบเดียวกับ examples/s04/17
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


    def handle(raw):
        """แปลคำสั่งหนึ่งใบจากหน้าเว็บ คนส่งพิมพ์มั่วได้เสมอ ต้องไม่ทำให้ลูปตาย"""
        global n_bad
        try:
            cmd = json.loads(raw.decode())
            action = cmd.get("cmd", "")
        except (ValueError, AttributeError):
            # ไม่ใช่ JSON หรือไม่ใช่ dict เช่น [1,2]
            n_bad = n_bad + 1
            last_lbl.color(COL_BAD)
            last_lbl.text("ไม่ใช่ JSON")
            return

        if action == "led":
            i = cmd.get("n", 0)
            # เลขดวงจากคนอื่นเชื่อไม่ได้ Eva มี 3 Dev Kit มี 5
            if not isinstance(i, int) or i < 0 or i >= N:
                n_bad = n_bad + 1
                last_lbl.color(COL_BAD)
                last_lbl.text("led ไม่มีดวง " + str(i)[:8])
                return
            on = 1 if cmd.get("on", 1) else 0
            if on:
                gpio.led(i).on()
            else:
                gpio.led(i).off()
            leds[i] = on               # สั่งหลอดแล้วจดลงตัวแปรทันที
            leds_lbl.text("leds = " + str(leds))
            last_lbl.color(COL_OK)
            last_lbl.text("led " + str(i) + (" ติด" if on else " ดับ"))
        elif action == "beep":
            # ui.tone รับโน้ต MIDI ไม่ใช่ความถี่ และรับแบบตำแหน่งเท่านั้น
            ui.tone(69, ui.WAVE_SQUARE, 90, 150)
            last_lbl.color(COL_OK)
            last_lbl.text("beep")
        elif action == "say":
            # ข้อความจากคนอื่น ตัดที่ 24 ตัวและกรองก่อนขึ้นจอ
            text = screen_safe(str(cmd.get("text", "")), 24)
            last_lbl.color(COL_ACCENT)
            last_lbl.text(text if text != "" else "say ว่าง")
        else:
            n_bad = n_bad + 1
            last_lbl.color(COL_WARN)
            last_lbl.text("ไม่รู้จัก " + str(action)[:16])
        lcd.print("cmd:", raw)


    presses = 0
    n_msg = 0
    n_event = 0
    n_state = 0
    n_fail = 0
    n_bad = 0
    lost = False

    stable = btn.is_pressed()
    last_raw = stable
    t0 = time.ticks_ms()
    last_change = t0
    last_state = time.ticks_add(t0, -STATE_MS)
    last_sec = -1

    while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
        now = time.ticks_ms()
        raw = btn.is_pressed()

        # --- งานที่ 1: ปุ่ม กันเด้ง ส่งเฉพาะขอบ "เริ่มกด" ---
        if raw != last_raw:
            last_raw = raw
            last_change = now
        elif raw != stable and time.ticks_diff(now, last_change) >= DEBOUNCE_MS:
            stable = raw
            if stable:
                presses = presses + 1
                seg.text(str(presses))
                if not send(TOPIC_EVENT, {"ev": "press", "presses": presses}):
                    lost = True
                    break
                n_event = n_event + 1
                show_counts()
                lcd.print("กดครั้งที่", presses, "-> event")

        # --- งานที่ 2: telemetry ตามนาฬิกา ---
        # ส่ง retain ไม่ได้ หน้าเว็บที่เพิ่งเปิดต้องรอใบถัดไป
        if time.ticks_diff(now, last_state) >= STATE_MS:
            last_state = now
            state = {"presses": presses,
                     "btn": 1 if stable else 0,
                     "leds": leds}
            az = read_az()
            if az is not None:
                state["az"] = az
                az_lbl.text("az = " + str(az) + " m/s2")
            if not send(TOPIC_STATE, state):
                lost = True
                break
            n_state = n_state + 1
            show_counts()

        # --- งานที่ 3: คำสั่งจากหน้าเว็บ ---
        # กล่องรับมีช่องเดียว ใบใหม่ทับใบเก่า
        msg = mqtt.get_message()
        if msg is not None:
            handle(msg[1])
            show_counts()

        # สายหลุดได้เสมอ เฟิร์มแวร์ไม่ต่อใหม่ให้
        if not mqtt.is_connected():
            lost = True
            break

        # เขียนสถานะวินาทีละครั้งพอ ทุก 5 ms คือข้ามคอร์ 200 ครั้งต่อวินาที
        sec = time.ticks_diff(now, t0) // 1000
        if sec != last_sec:
            last_sec = sec
            status.text("ส่งอยู่ - เหลืออีก " + str(RUN_MS // 1000 - sec) + " วินาที")

        ui.poll()
        time.sleep_ms(POLL_MS)

    # ดับแล้วจดลงตัวแปรด้วย ไม่งั้นจอรายงานไม่จริง
    for i in range(N):
        gpio.led(i).off()
        leds[i] = 0
    leds_lbl.text("leds = " + str(leds))

    if lost:
        status.color(COL_BAD)
        status.text("สายหลุด หยุดส่งแล้ว")
        note.color(COL_BAD)
        note.text("หลุดวินาทีที่ " + str(time.ticks_diff(time.ticks_ms(), t0) // 1000) +
                  " กด " + str(presses) + " ครั้ง รันใหม่")
        lcd.print("<span class=error>สายหลุด ต้องรันใหม่</span>")
    else:
        status.color(COL_OK)
        status.text("ครบเวลาแล้ว - ดับไฟครบทุกดวง")
        note.color(COL_DIM)
        note.text("กด " + str(presses) + " ครั้ง | ส่ง " + str(n_msg) + " ใบ | ส่งไม่ผ่าน " +
                  str(n_fail))
    ui.poll()

    lcd.console("<span class=muted>------------------------</span>")
    lcd.print("<span class=ok>กด", presses, "ครั้ง | ส่ง", n_msg, "ใบ</span>")
    print("กด", presses, "ครั้ง | event", n_event, "| telemetry", n_state,
          "| ส่งไม่ผ่าน", n_fail, "| คำสั่งเสีย", n_bad)
except Stop:
    pass

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# ตั้ง DEBOUNCE_MS = 0 กดสิบครั้ง หน้าเว็บเห็น event กี่ใบ
# แล้วเพิ่มค่าหนึ่งตัวลงใน state ให้หน้าเว็บแสดง
# ใบ้: หน้าเว็บวาดหนึ่งกล่องต่อหนึ่ง key
