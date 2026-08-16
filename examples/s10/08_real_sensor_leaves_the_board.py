# 08_real_sensor_leaves_the_board.py - ค่าที่วัดได้จริงบนโต๊ะนี้ ออกไปหาคนอื่น
# ชุดตัวอย่างประจำคาบ 10
#
# ไฟล์นี้สอน: เจ็ดไฟล์ก่อนหน้าส่งเลขที่เราแต่งขึ้นเอง เพื่อให้เห็นกลไก MQTT ชัด ๆ
#             ไฟล์นี้เอาเลขแต่งออก แล้วใส่เซนเซอร์จริงเข้าไปแทน - ตั้งแต่ชิป
#             บนบอร์ด ผ่าน WiFi ไปถึง broker ครบวง
# ดูที่จอ   : กราฟสองเส้น ฟ้าคือค่าที่วัดได้ทุกรอบ เขียวคือค่าที่ส่งออกไปจริง
#             เส้นเขียวเป็นขั้นบันได เพราะเราส่งทุกสองวินาที ไม่ได้ส่งทุกรอบ
# กับดัก    : วัดเร็วกับส่งเร็วเป็นคนละเรื่อง - วัดทุก 200 ms ได้สบาย แต่ส่งทุก
#             200 ms คือ 5 ข้อความต่อวินาทีต่อบอร์ด สิบห้าโต๊ะก็ 75 ข้อความ/วินาที
#             เข้า broker ตัวเดียว นั่นคือวิธีทำให้ห้องเรียนล่มโดยไม่มีใครผิด
#
# บน Eva Kit: sensors.snapshot() อ่านได้ตามปกติ - มันขอผ่าน IPC ไปให้คอร์จอ
#             เป็นคนคุยกับชิป ส่วน sensors.init() หรือ scan() จะโดนปฏิเสธ
#             เพราะบัส I2C ไม่ใช่ของเรา (ดู examples/s05/09_sensors_api_tour.py)

import json
import lcd
import mqtt
import sensors
import time
import ui
import wifi

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
BROKER = "192.168.1.50"
DEVICE_ID = "eva-team03"
MQTT_USER = "eva-team03"
MQTT_PASS = "<รหัสผ่าน MQTT ของทีม>"
TOPIC = "bento/eva-team03/telemetry"

READ_MS = 200          # วัดถี่ เพราะการวัดไม่ได้กวนใคร
SEND_MS = 2000         # ส่งห่าง เพราะการส่งกวน broker และกวนเพื่อนร่วมห้อง
ROUNDS = 150

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_READ, COL_SENT = 0x4A9EFF, 0x30A46C
COL_BAD = 0xE5484D

ui.screen()
time.sleep_ms(200)

ui.Label("ค่าจริงจากโต๊ะนี้ ออกไปหาคนอื่น", x=20, y=12, color=COL_TEXT, value=24)

ui.Panel(x=20, y=52, w=332, h=96, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("อุณหภูมิที่วัดได้ตอนนี้", x=36, y=64, color=COL_DIM, value=20)
seg_now = ui.Seg7(text="--", x=36, y=92, w=200, h=48, color=COL_READ)

ui.Panel(x=368, y=52, w=304, h=96, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ส่งออกไปแล้ว (ครั้ง)", x=384, y=64, color=COL_DIM, value=20)
seg_sent = ui.Seg7(text="0", x=384, y=92, w=140, h=48, color=COL_SENT)

# กราฟตั้งช่วง 20-35 องศา ไม่ใช่ 0-100 เพราะช่วงกว้างเกินจริงทำให้เส้นแบน
# จนมองไม่เห็นว่าค่าขยับ - สเกลที่แคบพอดีคือสิ่งที่ทำให้กราฟมีประโยชน์
ch = ui.Chart(x=20, y=164, w=548, h=192, min=20, max=35)
s_read = 0                      # เส้นที่ 0 มีมาพร้อม Chart อยู่แล้ว
s_sent = ch.add_series(COL_SENT)

ui.Label("ฟ้า = วัดทุก 0.2 วิ", x=580, y=172, color=COL_READ, value=20)
ui.Label("เขียว = ส่งทุก 2 วิ", x=580, y=196, color=COL_SENT, value=20)
status = ui.Label("กำลังต่อ...", x=20, y=364, color=COL_DIM, value=20)
ui.poll()

lcd.clear()
lcd.console("<h2>ค่าจริงออกจากบอร์ด</h2>")

if not wifi.connect(WIFI_SSID, WIFI_PASS):
    status.text("ต่อ WiFi ไม่ได้ - ตรวจชื่อวงกับรหัสผ่าน")
    status.color(COL_BAD)
    ui.poll()
    raise SystemExit

mqtt.connect(BROKER, client_id=DEVICE_ID, username=MQTT_USER, password=MQTT_PASS)
status.text("ต่อแล้ว ส่งทุก 2 วินาที")
status.color(COL_SENT)
ui.poll()

sent = 0
last_send = time.ticks_ms()
last_value = None

for _ in range(ROUNDS):
    # snapshot() คืนทุกเซนเซอร์ในครั้งเดียว เราหยิบมาใช้ตัวเดียว - ที่เหลือ
    # ไม่ได้เสียเปล่า เพราะการอ่านครั้งเดียวถูกกว่าการถามทีละตัวหลายครั้ง
    snap = sensors.snapshot()
    if "bmi270" in snap:
        temp = snap["bmi270"]["temperature"]
        last_value = temp
        seg_now.text("{:.1f}".format(temp))
        ch.set_next(s_read, int(temp))

    now = time.ticks_ms()
    if last_value is not None and time.ticks_diff(now, last_send) >= SEND_MS:
        last_send = now
        payload = json.dumps({"device": DEVICE_ID,
                              "temp_c": round(last_value, 1),
                              "t_ms": now})
        try:
            mqtt.publish(TOPIC, payload)
            sent = sent + 1
            seg_sent.text(str(sent))
            lcd.print("ส่งแล้ว", sent, "ครั้ง | ล่าสุด", round(last_value, 1), "C")
        except OSError as e:
            # ลิงก์หลุดกลางทาง จอต้องบอก ไม่ใช่เงียบแล้วให้เดา
            status.text("ส่งไม่ออก: " + str(e))
            status.color(COL_BAD)

    # เส้นเขียวถูกป้อนทุกรอบด้วยค่าที่ส่งไปล่าสุด จึงกลายเป็นขั้นบันได
    # นี่คือภาพของคำว่า "จอเห็นบ่อยกว่าที่คลาวด์เห็น" ซึ่งจริงเสมอในงาน IoT
    if last_value is not None:
        ch.set_next(s_sent, int(last_value))

    ui.poll()
    time.sleep_ms(READ_MS)

mqtt.disconnect()
status.text("จบรอบ - ส่งทั้งหมด " + str(sent) + " ครั้ง")
ui.poll()
lcd.print("<span class=ok>จบ - วัด", ROUNDS, "รอบ ส่ง", sent, "ครั้ง</span>")
print("วัดกี่ครั้งกับส่งกี่ครั้งไม่เท่ากัน และไม่ควรเท่ากัน")

# ตาคุณ
# 1) เปลี่ยน SEND_MS เป็น 200 ให้เท่ากับ READ_MS แล้วนับว่า broker รับไหว
#    กี่โต๊ะ ก่อนที่ข้อความจะเริ่มหาย
# 2) เพิ่มเงื่อนไข "ส่งเฉพาะเมื่อค่าเปลี่ยนเกิน 0.3 องศา" แล้วดูว่าจำนวนครั้ง
#    ที่ส่งลดลงเท่าไร โดยที่คนดูปลายทางยังเห็นภาพเดิม
