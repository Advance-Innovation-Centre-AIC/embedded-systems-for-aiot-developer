# 09_link_gates_a_real_reading.py - ค่าจริงรออยู่ ลิงก์เป็นคนบอกว่าไปได้หรือยัง
# ชุดตัวอย่างประจำคาบ 09
#
# Why : แปดไฟล์ก่อนหน้าดูแต่ตัวลิงก์ - สแกน ต่อ หลุด ต่อใหม่ แต่ลิงก์ไม่ได้มีไว้
#       ดูเล่น มันมีไว้พาของออกไป ไฟล์นี้จึงเอาเซนเซอร์จริงมาต่อท้าย เพื่อให้เห็น
#       ว่าอะไรคือของที่รอส่ง และอะไรคือคนตัดสินว่าส่งได้หรือยัง
# What: sensors.snapshot() + wifi.is_connected() + คิวในหน่วยความจำ
# How : วัดทุกรอบไม่ว่าเน็ตจะเป็นยังไง เก็บใส่คิว แล้วปล่อยคิวเมื่อลิงก์กลับมา
#
# ดูที่จอ: แถบบนบอกสถานะลิงก์ เลขกลางคือค่าที่วัดได้ล่าสุด เลขขวาคือจำนวนค่าที่
#         ค้างอยู่ในคิว - ถอดสาย WiFi ที่เราเตอร์แล้วดูเลขขวาเดินขึ้น
# กับดัก : การวัดกับการส่งต้องแยกขาดจากกัน ถ้าเขียนให้ "วัดเมื่อเน็ตมา" พอเน็ตหลุด
#         ข้อมูลช่วงนั้นจะหายไปตลอดกาล ทั้งที่เซนเซอร์ยังทำงานได้ปกติ
#
# บน Eva Kit: sensors.snapshot() ใช้ได้ ส่วน init()/scan() ถูกปฏิเสธ - บัส I2C
#             เป็นของคอร์จอ (ดู examples/s05/09_sensors_api_tour.py)

import lcd
import sensors
import time
import ui
import wifi

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
QUEUE_MAX = 60          # เก็บได้กี่ค่า ก่อนต้องทิ้งของเก่าสุด
ROUNDS = 150

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_BAD, COL_INFO = 0x00E676, 0xFF5252, 0x40C4FF

ui.screen()
time.sleep_ms(200)

ui.Label("ค่ารออยู่ ลิงก์เป็นคนเปิดประตู", x=20, y=12, color=COL_TEXT, value=24)

ui.Panel(x=20, y=52, w=740, h=64, color=COL_CARD, min=COL_DIM, max=12, value=1)
lbl_link = ui.Label("กำลังต่อ...", x=40, y=72, color=COL_DIM, value=22)
led_link = ui.Led(x=690, y=68, w=40, h=40, color=COL_OK, value=0)

ui.Panel(x=20, y=130, w=360, h=100, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("อุณหภูมิที่วัดได้", x=36, y=142, color=COL_DIM, value=16)
seg_now = ui.Seg7(text="--", x=36, y=170, w=200, h=48, color=COL_INFO)

ui.Panel(x=400, y=130, w=360, h=100, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ค้างในคิว (ค่า)", x=416, y=142, color=COL_DIM, value=16)
seg_q = ui.Seg7(text="0", x=416, y=170, w=160, h=48, color=COL_OK)

bar_q = ui.Bar(x=20, y=250, w=740, h=24, color=COL_OK, min=0, max=QUEUE_MAX, value=0)
note = ui.Label("ถอด WiFi ที่เราเตอร์ แล้วดูเลขขวาเดินขึ้น", x=20, y=290,
                color=COL_DIM, value=18)
ui.poll()

lcd.clear()
lcd.console("<h2>ลิงก์เปิดประตูให้ค่าที่รออยู่</h2>")

wifi.connect(WIFI_SSID, WIFI_PASS)

queue = []
sent = 0
dropped = 0

for _ in range(ROUNDS):
    # --- วัดก่อนเสมอ ไม่ถามเน็ต ---------------------------------------------
    # นี่คือหัวใจของไฟล์นี้ เซนเซอร์ไม่ได้พังตอนเน็ตหลุด จึงไม่มีเหตุผลให้หยุดวัด
    snap = sensors.snapshot()
    if "bmi270" in snap:
        temp = snap["bmi270"]["temperature"]
        seg_now.text("{:.1f}".format(temp))
        queue.append(round(temp, 1))
        if len(queue) > QUEUE_MAX:
            queue.pop(0)          # คิวเต็ม ทิ้งของเก่าสุด ไม่ใช่ของใหม่สุด
            dropped = dropped + 1

    # --- แล้วค่อยถามว่าส่งได้ไหม ---------------------------------------------
    up = wifi.is_connected()
    led_link.value(1 if up else 0)
    if up:
        lbl_link.text("ออนไลน์ - " + wifi.ip())
        lbl_link.color(COL_OK)
        # ปล่อยคิวทีละค่า ไม่ปล่อยรวดเดียว - ปล่อยรวดเดียวคือการยิงรัวใส่ปลายทาง
        if queue:
            queue.pop(0)
            sent = sent + 1
            if sent % 10 == 0:
                lcd.print("ปล่อยไปแล้ว", sent, "ค่า | ค้าง", len(queue))
    else:
        lbl_link.text("ออฟไลน์ - ค่ายังถูกเก็บไว้")
        lbl_link.color(COL_BAD)

    seg_q.text(str(len(queue)))
    bar_q.value(len(queue))
    bar_q.color(COL_BAD if len(queue) > QUEUE_MAX * 3 // 4 else COL_OK)

    ui.poll()
    time.sleep_ms(200)

note.text("จบ - ปล่อยไป " + str(sent) + " ค่า ทิ้งเพราะคิวเต็ม " + str(dropped))
ui.poll()
lcd.print("<span class=ok>ปล่อย", sent, "| ค้าง", len(queue), "| ทิ้ง", dropped, "</span>")
print("เซนเซอร์ไม่ได้พังตอนเน็ตหลุด จึงไม่มีเหตุผลให้หยุดวัด")

# ตาคุณ
# 1) ลด QUEUE_MAX เหลือ 10 แล้วถอดเน็ตนานขึ้น ดูว่าเริ่มทิ้งค่าตอนไหน
# 2) เปลี่ยนให้ทิ้งของใหม่สุดแทนของเก่าสุด แล้วถามตัวเองว่าแบบไหนถูกกับงานของทีม
