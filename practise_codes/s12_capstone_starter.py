# s12_capstone_starter.py - โครงเริ่มต้นของ mini-product: Sense -> Decide -> Show -> Send
# วิธีรัน: 1) แตะการ์ด Playground บนหน้า Home แล้วค้างหน้านี้ไว้
#          2) แก้บล็อก CONFIG ให้เป็นของทีม (device id, WiFi, broker, topic, เกณฑ์)
#          3) กด Program to Device แล้วมองจอบอร์ด
# ตรรกะวัดมุมเอียงเป็นแค่ตัวอย่าง จุด "ทีมเขียนเอง" คือที่ของงานทีม
# หน้าจอต้องมีครบสี่ข้อ = เกณฑ์ให้คะแนน
#   ค่า + พิสัย (ui.Bar ทับ ui.Scale)
#   สถานะเป็นไฟ (ui.Led)
#   ปุ่มเปิด/ปิดแยกกัน
#   กล่องยืนยันก่อนสั่งของจริง (ui.MsgBox)

import time
import json
import gpio
import wifi
import mqtt
import sensors
import dsp
import ui

# ---- CONFIG: แก้ก่อนรัน ----
DEVICE_ID = "team01"    # ห้ามซ้ำทีมอื่น (broker สาธารณะ)
CLIENT_ID = DEVICE_ID + "-%04x" % (time.ticks_ms() & 0xFFFF)   # สุ่มตัวท้าย รันซ้ำทันทีก็ไม่ชน id เดิม
WIFI_SSID = "AIoT-Class"
WIFI_PASS = "aiot12345"
BROKER = "test.mosquitto.org"
TOPIC = "bento/team01/telemetry"
UNIT = "deg"            # หน่วยต้องตรงกับของจริง
SCALE_MAX = 45          # ปลายพิสัยของมาตรวัดบนจอ
WARN_LIMIT = 8.0        # เกินเท่านี้ = เฝ้าดู
LIMIT = 15.0            # เกินเท่านี้ = ผิดปกติ
BEACON_LED = "RGB_RED"  # ไฟเตือนหน้างาน ระบุเป็น "ชื่อ" (Eva ใช้ LED1 แทน)
HEARTBEAT_MS = 30000    # ส่ง "ยังอยู่ดี" ทุก 30 วิ
ALERT_GAP_MS = 15000    # เตือนซ้ำได้ถี่สุดทุก 15 วิ
RETRY_MS = 10000        # ลองต่อเน็ตใหม่ทุก 10 วิ
LOOP_MS = 200           # เร็วกว่านี้เฟรมหายเงียบ ๆ

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD, COL_OK, COL_WARN, COL_BAD, COL_RUN = (0x171B22, 0x30A46C, 0xF5A623,
                                                0xE5484D, 0x4A9EFF)

STATE_TEXT = {"OK": "ปกติ ไม่ต้องทำอะไร",
              "WARN": "เฝ้าดู ยังไม่ต้องเข้าไป",
              "ALERT": "ผิดปกติ ต้องมีคนไปดู"}

# --- ไฟเตือนหน้างาน ---
LED_NAMES = gpio.board_info()["led_names"]


def led_named(*names, fallback=0):
    """หา LED จากชื่อในตารางเฟิร์มแวร์ - เลขดัชนีต่างกันตามบอร์ด ชื่อไม่ต่าง"""
    for n in names:
        if n in LED_NAMES:
            return gpio.led(LED_NAMES.index(n))
    return gpio.led(fallback)


beacon_lamp = led_named(BEACON_LED if "RGB_GREEN" in LED_NAMES else "LED1")   # แดงทั้งสองบอร์ด

# --- ท่าที่ 1: Sense ---
# บน Eva ห้ามเรียก sensors.init() (ได้ OSError)
# อ่านทิ้งก่อน: บน Eva การอ่านแรกหลังรีเซ็ตรอได้ถึง 16 วิ
try:
    sensors.bmi270.motion()
except OSError:
    print("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ในลูป")
smooth = dsp.EMA(alpha=0.2)

last_value = 0.0
stale = False


def read_value():
    global last_value, stale
    try:
        ax, ay, az, gx, gy, gz = sensors.bmi270.motion()
    except OSError:
        stale = True
        return last_value
    stale = False
    roll, pitch = dsp.tilt(ax, ay, az)   # คืน (roll, pitch) - roll มาก่อน
    # ทีมเขียนเอง: เปลี่ยนบรรทัดล่างเป็นปริมาณที่โจทย์ของทีมสนใจจริง ๆ
    last_value = smooth.update(abs(roll))
    return last_value


# --- ท่าที่ 2: Decide ---
# ไล่ if จากเข้มสุดลงมา สลับแล้วไม่เข้า ALERT
def decide(value):
    if value > LIMIT:
        return "ALERT"
    if value > WARN_LIMIT:
        return "WARN"
    return "OK"


def on_state_change(old, new, value):
    pass  # ทีมเขียนเอง: ตอนสถานะเปลี่ยนให้เกิดอะไร (นับจำนวนครั้ง จดเวลา สั่งของอย่างอื่น)


# --- ท่าที่ 3: Show ---
# มุมขวาล่างเป็นของปุ่ม Console ห้ามวางปุ่มตรงนั้น
ui.screen()
time.sleep_ms(200)
ui.Label("จอเฝ้าระวัง - " + DEVICE_ID, x=24, y=8, color=COL_TEXT, value=20)
led_mqtt = ui.Led(x=606, y=12, w=18, h=18, color=COL_OK, value=0)
lbl_mqtt = ui.Label("MQTT: ออฟไลน์", x=632, y=10, color=COL_DIM, value=16)

# การ์ดซ้ายบน: ค่าที่วัดได้ + พิสัย
# สร้างการ์ดก่อนของที่วางบนมัน ไม่งั้นการ์ดทับจนหาย
ui.Panel(x=24, y=48, w=464, h=184, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ค่าที่วัดได้ เทียบกับเกณฑ์", x=40, y=56, color=COL_DIM, value=16)
lbl_value = ui.Label("--", x=40, y=88, color=COL_TEXT, value=28)
lbl_quality = ui.Label("รอค่าแรก", x=248, y=96, color=COL_DIM, value=16)
bar_value = ui.Bar(x=40, y=136, w=432, h=16, color=COL_RUN,
                   min=0, max=SCALE_MAX, value=0)
sc_value = ui.Scale(x=40, y=156, w=432, h=48, color=COL_TEXT, min=0, max=SCALE_MAX)
sc_value.ticks(10, 3)
ui.Label("เฝ้าระวัง " + str(WARN_LIMIT) + " - ผิดปกติ " + str(LIMIT) + " " + UNIT,
         x=40, y=208, color=COL_DIM, value=16)

# การ์ดซ้ายล่าง: สถานะเป็นไฟสามดวง ติดทีละดวง
ui.Panel(x=24, y=240, w=464, h=152, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("สถานะที่ตัดสินแล้ว", x=40, y=248, color=COL_DIM, value=16)
lbl_latch = ui.Label("รอคนรับทราบ", x=336, y=244, color=COL_WARN, value=20)
lbl_latch.hide()
led_ok = ui.Led(x=40, y=276, w=48, h=48, color=COL_OK, value=1)
led_warn = ui.Led(x=144, y=276, w=48, h=48, color=COL_WARN, value=0)
led_bad = ui.Led(x=248, y=276, w=48, h=48, color=COL_BAD, value=0)
ui.Label("ปกติ", x=40, y=328, color=COL_DIM, value=16)
ui.Label("เฝ้าระวัง", x=144, y=328, color=COL_DIM, value=16)
ui.Label("ผิดปกติ", x=248, y=328, color=COL_DIM, value=16)
lbl_state = ui.Label("เริ่มทำงาน", x=40, y=356, color=COL_TEXT, value=20)
btn_ack = ui.Button("รับทราบ", x=368, y=276, w=104, h=88, color=0x3A4150, value=20)

ui.Panel(x=504, y=48, w=264, h=344, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ไฟเตือนหน้างาน", x=520, y=56, color=COL_DIM, value=16)
led_beacon = ui.Led(x=520, y=88, w=48, h=48, color=COL_BAD, value=0)
lbl_beacon_state = ui.Label("ไฟดับอยู่", x=576, y=100, color=COL_DIM, value=20)
lbl_beacon = ui.Label("สั่งปิดต้องยืนยันก่อน", x=520, y=144, color=COL_DIM, value=16)
# ปุ่มเปิด/ปิดแยกกัน: ปุ่มสลับบอกไม่ได้ว่าอยู่สถานะไหน
btn_on = ui.Button("เปิดไฟ", x=520, y=176, w=96, h=88, color=0x30A46C, value=20)
btn_off = ui.Button("ปิดไฟ", x=648, y=176, w=96, h=88, color=0x3A4150, value=20)
lbl_net = ui.Label("net: starting", x=520, y=296, color=COL_DIM, value=20)
lbl_sent = ui.Label("ส่งแล้ว 0 ใบ", x=520, y=336, color=COL_DIM, value=16)

# ข้อความ MsgBox ได้ราว 31 ตัวอักษรไทย เกินถูกตัดเงียบ ๆ
# คำยืนยันต้องบอก "สิ่งที่จะเกิด" ไม่ใช่ "ยืนยันไหม"
box = ui.MsgBox("ปิดไฟเตือน\nไฟหน้างานจะดับทันที", x=112, y=96, w=568, h=136,
                color=COL_CARD)
box.hide()
# ปุ่มใน MsgBox ยังไม่ส่งเหตุการณ์ จึงใช้ ui.Button สองปุ่มแทน
btn_yes = ui.Button("ยืนยัน", x=144, y=248, w=200, h=88, color=0x3A4150, value=20)
btn_no = ui.Button("ยกเลิก", x=376, y=248, w=200, h=88, color=0x3A4150, value=20)
btn_yes.hide()
btn_no.hide()
ui.poll()

last_sec = -1
last_shown = None


def show(now, value, state, latched, net_text, sent):
    global last_sec, last_shown
    bar_value.value(int(min(value, SCALE_MAX)))
    led_ok.value(1 if state == "OK" else 0)
    led_warn.value(1 if state == "WARN" else 0)
    led_bad.value(1 if state == "ALERT" else 0)

    if (state, latched) != last_shown:
        last_shown = (state, latched)
        lbl_state.text(STATE_TEXT[state])
        if latched:
            lbl_latch.show()
        else:
            lbl_latch.hide()

    sec = now // 1000
    if sec == last_sec:
        return
    last_sec = sec
    lbl_value.text("{:.1f} {}".format(value, UNIT))
    lbl_quality.text("ค่าค้าง อ่านไม่ได้" if stale else "ค่าปกติ")
    lbl_quality.color(COL_WARN if stale else COL_DIM)
    lbl_net.text(net_text)
    lbl_sent.text("ส่งแล้ว " + str(sent) + " ใบ")
    # ทีมเขียนเอง: เพิ่ม widget ของทีม (งบรวมทั้งจอไม่เกิน 64 ตัว ตอนนี้ใช้ไป 33)
    # เกณฑ์: ถอดปลั๊กเราเตอร์แล้วจอต้องยังทำงาน


def beacon(on):
    if on:
        beacon_lamp.on()
    else:
        beacon_lamp.off()
    led_beacon.value(1 if on else 0)
    lbl_beacon_state.text("ไฟติดอยู่" if on else "ไฟดับอยู่")


# --- ท่าที่ 4: Send ---
def payload(value, state, kind):
    d = {"id": DEVICE_ID, "v": round(value, 1), "unit": UNIT,
         "state": state, "kind": kind, "t": time.ticks_ms()}
    # ทีมเขียนเอง: เพิ่มหรือตัดฟิลด์ให้ตรงกับตาราง schema ที่ทีมออกแบบไว้ในใบงาน
    return json.dumps(d)


def send(value, state, kind):
    if not mqtt.is_connected():
        return False
    mqtt.publish(TOPIC, payload(value, state, kind))
    return True


# --- ท่าที่ 5: กันเน็ตหลุด ---
def go_online():
    if not wifi.is_connected():
        if not wifi.connect(WIFI_SSID, WIFI_PASS):   # บล็อกได้นานถ้ารหัสผิด
            return False
    return mqtt.connect(BROKER, port=1883, client_id=CLIENT_ID)


def show_link(ok):
    led_mqtt.value(1 if ok else 0)
    lbl_mqtt.text("MQTT: เชื่อมต่อแล้ว" if ok else "MQTT: ออฟไลน์")


state = "OK"
latched = False
asking = False
beacon(False)
# หยุดกลางทาง finally ยังบอกลา broker ชื่อจึงไม่ค้าง
try:
    online = go_online()
    show_link(online)
    missed = 0
    sent = 0
    last_alert = None
    now = time.ticks_ms()
    t_beat = now
    t_retry = now

    while True:
        now = time.ticks_ms()
        value = read_value()
        new_state = decide(value)

        if online and not mqtt.is_connected():
            online = False
            show_link(False)
            t_retry = now
        if (not online) and time.ticks_diff(now, t_retry) >= RETRY_MS:
            online = go_online()
            show_link(online)
            t_retry = now

        if new_state != state:
            on_state_change(state, new_state, value)
            state = new_state
            ready = last_alert is None or time.ticks_diff(now, last_alert) >= ALERT_GAP_MS
            if state == "ALERT":
                latched = True
                beacon(True)
                if ready:
                    if send(value, state, "event"):
                        last_alert = now
                        sent += 1
                    else:
                        missed += 1
                        pass  # ทีมเขียนเอง: ออฟไลน์แล้วจะ "ทิ้ง" หรือ "เก็บไว้ส่งทีหลัง"

        if time.ticks_diff(now, t_beat) >= HEARTBEAT_MS:
            t_beat = now
            if send(value, state, "beat"):
                sent += 1
            else:
                missed += 1

        # --- ปุ่มบนจอ ---
        for ev in ui.poll():   # เรียกทุกลูป ไม่งั้นจอซ่อน widget
            if ev["type"] != "clicked":
                continue
            if ev["handle"] == btn_on.id():
                beacon(True)
            elif ev["handle"] == btn_off.id() and not asking:
                asking = True
                box.show()
                btn_yes.show()
                btn_no.show()
            elif ev["handle"] == btn_yes.id() and asking:
                asking = False
                beacon(False)
                box.hide()
                btn_yes.hide()
                btn_no.hide()
            elif ev["handle"] == btn_no.id() and asking:
                asking = False
                box.hide()
                btn_yes.hide()
                btn_no.hide()
            elif ev["handle"] == btn_ack.id() and latched:
                latched = False
                if send(value, state, "ack"):
                    sent += 1

        show(now, value, state, latched,
             "online" if online else "offline - missed " + str(missed), sent)
        time.sleep_ms(LOOP_MS)
finally:
    try:
        mqtt.disconnect()
        show_link(False)
    except Exception:
        pass
