# 05_hmi_survives_offline.py - เน็ตหลุดแล้วจอต้องยังทำงาน
#
# Why : เครื่องจักรอยู่ในโรงงาน คนคุมยืนอยู่หน้าเครื่อง เราเตอร์อยู่อีกตึกหนึ่ง
#       วันที่เราเตอร์ดับ ถ้าจอหน้าเครื่องดับตามไปด้วย คนที่ยืนอยู่ตรงนั้นจะทำงานต่อไม่ได้
#       ทั้งที่เครื่องยังดีอยู่ทุกอย่าง - ความล้มเหลวของสายไม่ควรลามมาเป็นความล้มเหลวของจอ
# What: HMI ที่แยกลูปของจอออกจากลูปของเครือข่ายอย่างเด็ดขาด จอวาดของมันไปเรื่อย ๆ
#       ส่วนการส่งเป็นงานที่ล้มเหลวได้ และเมื่อล้มเหลว หน้าที่ของจอคือรายงานให้รู้
#       ไม่ใช่หยุดทำงานตาม คนหน้างานต้องรู้ทั้ง "ค่าเท่าไร" และ "ค่านี้ออกไปแล้วหรือยัง"
#
# ดูที่จอ: ลากสไลเดอร์แล้ว Seg7 เปลี่ยนตามทันที ไม่ต้องรอเน็ต
#          กราฟล่างมีสองเส้น ฟ้าคือค่าบนจอ เขียวคือค่าที่ส่งออกไปได้จริง
#          ถอดปลั๊กเราเตอร์ เส้นเขียวจะตกลงพื้นเป็นช่องว่าง ส่วนเส้นฟ้ายังลากได้ตามปกติ
#          ป้าย net เปลี่ยนเป็นแดง และตัวนับ "ส่งไม่ออก" ไต่ขึ้นทุกคาบส่ง
# กับดัก : ui.Seg7 รับข้อความ ไม่ใช่ตัวเลข seg.value(50) ไม่เกิดอะไรขึ้นเลย
#          และไม่มี error ด้วย ต้องใช้ seg.text("50") เท่านั้น

import json
import mqtt
import time
import ui
import wifi

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
BROKER = "192.168.1.50"
DEVICE_ID = "team03"
TOPIC = "bento/team03/telemetry"
SEND_EVERY_MS = 3000
CHART_MS = 300           # กราฟเดินตามลูปของจอ ไม่ได้เดินตามคาบส่ง

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_BAD, COL_INFO = 0x30A46C, 0xE5484D, 0x4A9EFF

ui.screen()
time.sleep_ms(200)

ui.Label("คาบ 12 - จอต้องอยู่ได้เมื่อเน็ตหลุด", x=20, y=8, color=COL_TEXT,
         value=24)
ui.Panel(x=20, y=44, w=652, h=128, color=COL_CARD, min=COL_DIM, max=12, value=1)

ui.Label("ค่าที่กำลังเฝ้าดู", x=40, y=52, color=COL_DIM, value=16)
# ส่งเป็นข้อความเพื่อคุมรูปแบบเอง .value() ได้แต่จำนวนเต็ม
seg = ui.Seg7("25", x=40, y=76, w=172, h=52, color=COL_TEXT)

ui.Label("ลากเพื่อเปลี่ยนค่า", x=232, y=52, color=COL_DIM, value=16)
slider = ui.Slider(x=232, y=80, w=420, h=28, min=0, max=100, value=25)

l_net = ui.Label("net: -", x=660, y=128, color=COL_DIM, value=20)
l_miss = ui.Label("ส่งไม่ออก: 0", x=660, y=164, color=COL_DIM, value=20)

# สองเส้นบนกราฟเดียวคือหัวใจของไฟล์นี้ เส้นบนจอกับเส้นที่ออกไปถึงปลายทาง
# เป็นคนละเรื่องกัน และต้องมองเห็นได้ว่าคนละเรื่องกันจริง ๆ
ch = ui.Chart(x=20, y=200, w=652, h=140, color=COL_CARD, min=0, max=100)
s_local = ch.add_series(COL_INFO)
s_sent = ch.add_series(COL_OK)

# สองป้ายในบรรทัดเดียว ให้อยู่ในเพดาน 126 ไบต์ของ ui.Label - ไทยตัวละ 3 ไบต์
l_foot = ui.Label("ฟ้า = ค่าบนจอ", x=20, y=348, color=COL_DIM, value=16)
ui.Label("เขียว = ค่าที่ส่งออกไปได้จริง", x=180, y=348, color=COL_DIM,
         value=16)
ui.poll()

# ต่อเน็ตหลังจากสร้างจอเสร็จแล้ว ลำดับนี้ตั้งใจ - จอต้องพร้อมก่อนสิ่งที่อาจล้มเหลว
online = False
if wifi.connect(WIFI_SSID, WIFI_PASS):
    online = mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID)
value, missed = 25, 0
# ค่าที่ปลายทางรู้จักล่าสุด ตกเป็นศูนย์เมื่อส่งไม่ออก เพราะปลายทางไม่รู้อะไรเลยจริง ๆ
value_at_broker = 0
t_send = time.ticks_ms()
t_chart = time.ticks_ms()

while True:
    # ลูปของจอเดินของมันไปเรื่อย ๆ ไม่ว่าสายจะเป็นอย่างไร และกรองด้วย handle ด้วย
    # ไม่ใช่ด้วยชนิดเหตุการณ์อย่างเดียว พอเพิ่ม widget ตัวที่สองแล้วจะแยกไม่ออกทันที
    for ev in ui.poll():
        if ev["handle"] == slider.id() and ev["type"] == "value_changed":
            value = ev["value"]
            seg.text(str(value))

    now = time.ticks_ms()
    if time.ticks_diff(now, t_send) >= SEND_EVERY_MS:
        t_send = now
        online = mqtt.is_connected()
        # ค่าที่ส่งไม่ออกถูกทิ้ง ไม่เก็บย้อนหลัง เพราะค่าที่ค้างมานานไม่มีประโยชน์
        # ถ้าโจทย์ของทีมต้องการเก็บย้อนหลัง นี่คือจุดที่ต้องเปลี่ยน
        body = json.dumps({"id": DEVICE_ID, "v": value})
        sent = False
        try:
            sent = bool(online and mqtt.publish(TOPIC, body))
        except OSError:
            # หลุดระหว่างส่ง เดินต่อ ห้ามให้ทั้งโปรแกรมตายเพราะสายเส้นเดียว
            online = False
        if not sent:
            missed += 1
        value_at_broker = value if sent else 0
        l_net.color(COL_OK if online else COL_BAD)
        l_net.text("net: online" if online else "net: offline")
        l_miss.color(COL_DIM if missed == 0 else COL_BAD)
        l_miss.text("ส่งไม่ออก: " + str(missed))

    # กราฟเดินตามลูปของจอ ไม่ได้เดินตามคาบส่ง เพราะจอเป็นของคนที่ยืนอยู่ตรงนี้
    # เส้นฟ้าขยับทันทีที่ลากสไลเดอร์ ส่วนเส้นเขียวขยับได้เร็วสุดทุก SEND_EVERY_MS
    # ช่องว่างระหว่างสองเส้นคือช่วงเวลาที่ปลายทางไม่รู้ว่าเกิดอะไรขึ้น
    if time.ticks_diff(now, t_chart) >= CHART_MS:
        t_chart = now
        ch.set_next(s_local, value)
        ch.set_next(s_sent, value_at_broker)

    time.sleep_ms(100)
