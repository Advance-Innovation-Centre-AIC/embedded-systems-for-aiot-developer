# 07_real_reading_over_tls.py - ค่าที่วัดได้จริง ออกไปแบบที่คนกลางอ่านไม่ได้
# ชุดตัวอย่างประจำคาบ 11
#
# Why : หกไฟล์ก่อนหน้าสอนกลไก TLS ด้วยค่าที่แต่งขึ้น เพื่อให้เห็นการจับมือและ
#       การตั้งค่าชัด ๆ ไฟล์นี้ปิดวง - ค่าที่ออกไปคือค่าที่ชิปบนบอร์ดวัดได้จริง
# What: sensors.snapshot() + tesaiot.publish() บนพอร์ตที่เข้ารหัส
# How : วัดถี่ ส่งห่าง และแสดงให้เห็นทั้งสองจังหวะพร้อมกันบนจอ
#
# ดูที่จอ: ไฟเขียวคือลิงก์เข้ารหัสพร้อม เลขซ้ายคือค่าที่วัดได้ล่าสุด เลขขวาคือ
#         จำนวนครั้งที่ส่งสำเร็จ และแถบล่างคือเวลานับถอยหลังถึงการส่งครั้งต่อไป
# กับดัก : การเข้ารหัสไม่ได้ทำให้ข้อมูลถูกต้องขึ้น มันแค่ทำให้คนกลางอ่านไม่ได้
#         ถ้าค่าที่วัดผิดตั้งแต่ต้น มันก็จะผิดอย่างปลอดภัยไปถึงปลายทาง
#
# บน Eva Kit: sensors.snapshot() ใช้ได้ · tesaiot.publish() ใช้การตั้งค่าที่เก็บไว้
#             ในบอร์ดจากไฟล์ 01 และ 02 ของคาบนี้ ต้องรันสองไฟล์นั้นก่อน

import json
import lcd
import sensors
import tesaiot
import time
import ui

SEND_MS = 5000
ROUNDS = 160

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_BAD, COL_INFO = 0x30A46C, 0xE5484D, 0x4A9EFF

ui.screen()
time.sleep_ms(200)

ui.Label("ค่าจริง ส่งแบบเข้ารหัส", x=20, y=12, color=COL_TEXT, value=24)

ui.Panel(x=20, y=52, w=740, h=64, color=COL_CARD, min=COL_DIM, max=12, value=1)
led_tls = ui.Led(x=44, y=68, w=48, h=48, color=COL_OK, value=0)
lbl_tls = ui.Label("ยังไม่ได้ต่อ", x=100, y=72, color=COL_DIM, value=24)

ui.Panel(x=20, y=132, w=360, h=100, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("อุณหภูมิที่วัดได้", x=36, y=144, color=COL_DIM, value=20)
seg_now = ui.Seg7(text="--", x=36, y=172, w=200, h=48, color=COL_INFO)

ui.Panel(x=400, y=132, w=360, h=100, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ส่งสำเร็จ (ครั้ง)", x=416, y=144, color=COL_DIM, value=20)
seg_sent = ui.Seg7(text="0", x=416, y=172, w=160, h=48, color=COL_OK)

ui.Label("อีกกี่วินาทีถึงส่งครั้งต่อไป", x=20, y=248, color=COL_DIM, value=20)
bar_next = ui.Bar(x=20, y=272, w=740, h=24, color=COL_INFO, min=0, max=SEND_MS, value=0)
note = ui.Label("เข้ารหัสกันคนกลางอ่าน ไม่ได้กันค่าผิด",
                x=20, y=308, color=COL_DIM, value=20)
ui.poll()

lcd.clear()
lcd.console("<h2>ค่าจริงผ่านช่องเข้ารหัส</h2>")

if not tesaiot.connect():
    lbl_tls.text("ต่อไม่ได้ - รัน 01_config_store.py ก่อน")
    lbl_tls.color(COL_BAD)
    ui.poll()
    raise SystemExit

led_tls.value(1)
lbl_tls.text("ช่องเข้ารหัสพร้อม - " + tesaiot.device_id())
lbl_tls.color(COL_OK)
ui.poll()

sent = 0
fails = 0
last_send = time.ticks_ms()
last_value = None

for _ in range(ROUNDS):
    snap = sensors.snapshot()
    if "bmi270" in snap:
        last_value = snap["bmi270"]["temperature"]
        seg_now.text("{:.1f}".format(last_value))

    now = time.ticks_ms()
    waited = time.ticks_diff(now, last_send)
    bar_next.value(waited if waited < SEND_MS else SEND_MS)

    if last_value is not None and waited >= SEND_MS:
        last_send = now
        try:
            # publish(payload, topic) -- payload มาก่อน และต้องเป็น JSON สตริง
            # เขียน publish("temp_c", 25.3) จะกลายเป็นการส่ง "temp_c" ขึ้นไป
            # โดยใช้ 25.3 เป็น topic ซึ่งไม่ใช่สิ่งที่ตั้งใจเลย
            tesaiot.publish(json.dumps({"temperature": round(last_value, 1)}))
            sent = sent + 1
            seg_sent.text(str(sent))
            lcd.print("ส่งแล้ว", sent, "ครั้ง | ล่าสุด", round(last_value, 1), "C")
        except OSError as e:
            # ลิงก์เข้ารหัสหลุดกลางทาง จอต้องบอก ไม่ใช่เงียบ
            fails = fails + 1
            led_tls.value(0)
            lbl_tls.text("ส่งไม่ออก: " + str(e))
            lbl_tls.color(COL_BAD)

    ui.poll()
    time.sleep_ms(200)

tesaiot.disconnect()
led_tls.value(0)
note.text("จบ - ส่งสำเร็จ " + str(sent) + " ครั้ง พลาด " + str(fails))
ui.poll()
lcd.print("<span class=ok>จบ - สำเร็จ", sent, "| พลาด", fails, "</span>")
print("การเข้ารหัสปกป้องเส้นทาง ไม่ได้ปกป้องความถูกต้องของค่า")

# ตาคุณ
# 1) เปิด MQTT Explorer ที่ปลายทาง แล้วเทียบว่าเลขที่เห็นตรงกับเลขบนจอไหม
# 2) ลองส่งค่าที่ยังไม่ได้วัด (last_value = None) ดูว่าโค้ดกันไว้ตรงไหน
#    และถ้าไม่กัน ปลายทางจะได้อะไร
