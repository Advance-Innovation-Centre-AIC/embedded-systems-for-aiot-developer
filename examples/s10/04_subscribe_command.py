# 04_subscribe_command.py - รับคำสั่งจากข้างนอก แล้วทำตาม
#
# ไฟล์นี้สอน: subscribe บอก broker ว่าเราสนใจ topic ไหน แล้วถาม get_message()
#             ถี่ ๆ ในลูป ใครส่งอะไรมาที่ topic นั้นเราจะได้เห็น
# ดูที่จอ   : ป้ายล่างสุดเต้นเป็นจุดตลอดเวลา นั่นคือหลักฐานว่าลูปยังเดินอยู่
#             พอส่งคำสั่งเข้ามา ป้าย "คำสั่งล่าสุด" เปลี่ยนและเลขใหญ่เดินขึ้น
# กับดัก    : payload ที่ได้กลับมาเป็น bytes ไม่ใช่ str ต้อง .decode() ก่อนเสมอ
#             และบัฟเฟอร์มีช่องเดียว ข้อความใหม่ทับของเก่าเงียบ ๆ ถ้าถามไม่ทัน

import wifi
import mqtt
import lcd
import ui
import json
import time

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
BROKER = "192.168.1.50"
DEVICE_ID = "eva-team03"
TOPIC_CMD = "bento/eva-team03/command"

NOTE_A4 = 69                # ui.tone รับ "โน้ต MIDI" 0-127 ไม่ใช่ความถี่เป็นเฮิรตซ์
LOOP_MS = 100

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_BAD, COL_INFO = 0x00E676, 0xFF5252, 0x40C4FF

ui.screen()
time.sleep_ms(200)

ui.Label("รอคำสั่งจากข้างนอก", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=650, h=130, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("topic ที่เฝ้าอยู่", x=36, y=62, color=COL_DIM, value=16)
l_topic = ui.Label(TOPIC_CMD, x=36, y=86, color=COL_INFO, value=20)
ui.Label("คำสั่งล่าสุดที่ได้รับ", x=36, y=122, color=COL_DIM, value=16)
l_last = ui.Label("- ยังไม่มี -", x=36, y=146, color=COL_TEXT, value=20)

ui.Panel(x=20, y=194, w=650, h=120, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ทำตามคำสั่งไปแล้วกี่ครั้ง", x=36, y=204, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=36, y=228, w=180, h=76, color=COL_OK)
ui.Label('ส่ง {"cmd":"beep"} หรือ {"cmd":"count"}', x=240, y=234,
         color=COL_DIM, value=18)
ui.Label("มาที่ topic ข้างบน จาก MQTT Explorer", x=240, y=262,
         color=COL_DIM, value=18)
alive = ui.Label("กำลังต่อเน็ต...", x=20, y=336, color=COL_DIM, value=18)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 10 - รับคำสั่ง</h2>")

if not wifi.connect(WIFI_SSID, WIFI_PASS):
    alive.color(COL_BAD)
    alive.text("ต่อ WiFi ไม่ได้")
    ui.poll()
    lcd.print("<span class=err>ต่อ WiFi ไม่ได้</span>")
    raise SystemExit
if not mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID):
    alive.color(COL_BAD)
    alive.text("ต่อ broker ไม่ได้")
    ui.poll()
    lcd.print("<span class=err>ต่อ broker ไม่ได้</span>")
    raise SystemExit

# subscribe ครั้งเดียวก่อนเข้าลูป ไม่ต้องเรียกซ้ำทุกรอบ
# มันคืน False ถ้า broker ปฏิเสธ ซึ่งเกิดได้เมื่อบัญชีเราไม่มีสิทธิ์อ่าน topic นี้
if not mqtt.subscribe(TOPIC_CMD):
    l_topic.color(COL_BAD)
    alive.color(COL_BAD)
    alive.text("broker ปฏิเสธการ subscribe")
    ui.poll()
    lcd.print("<span class=err>broker ปฏิเสธการ subscribe</span>")
    raise SystemExit

l_topic.color(COL_OK)
alive.text("กำลังรอคำสั่ง")
ui.poll()
lcd.print("<span class=ok>รอคำสั่งที่</span>", TOPIC_CMD)
lcd.print('ลองส่ง {"cmd":"beep"} หรือ {"cmd":"count"} มาดู')

counter = 0
ticks = 0

while True:
    # ถามทุกรอบ ห้ามหลับยาว ยิ่งรอบห่างยิ่งมีโอกาสพลาดข้อความที่มาติด ๆ กัน
    msg = mqtt.get_message()

    if msg is not None:
        # msg เป็น tuple สองช่อง: topic เป็น str ส่วน payload เป็น bytes
        topic, raw = msg

        try:
            cmd = json.loads(raw.decode())
        except ValueError:
            # คนส่งมั่วได้เสมอ ข้อความที่ไม่ใช่ JSON ต้องไม่ทำให้ทั้งโปรแกรมตาย
            l_last.color(COL_BAD)
            l_last.text("ไม่ใช่ JSON")
            lcd.print("ได้ข้อความที่ไม่ใช่ JSON:", raw)
            cmd = {}

        # .get() แทนการเข้าถึงคีย์ตรง ๆ เพราะ JSON ที่ถูกต้องแต่ไม่มีคีย์ cmd ก็เป็นไปได้
        action = cmd.get("cmd", "")

        if action == "beep":
            # 880 คือความถี่ของ A4 ไม่ใช่หมายเลขโน้ต ใส่ 880 ลงไปจะถูกตัดทิ้ง
            # เพราะช่วงที่รับได้คือ 0-127 กับดักเดียวกับที่คาบ 4 สอนไว้
            ui.tone(NOTE_A4, ui.WAVE_SQUARE, 90, 150)
            counter += 1
            l_last.color(COL_OK)
            l_last.text("beep")
            lcd.print("ทำตามคำสั่ง beep")
        elif action == "count":
            counter += 1
            l_last.color(COL_OK)
            l_last.text("count")
            lcd.print("ทำตามคำสั่ง count ->", counter)
        else:
            l_last.color(COL_BAD)
            l_last.text("ไม่รู้จัก: " + str(action))
            lcd.print("ไม่รู้จักคำสั่งนี้:", action)

        seg.text(str(counter))   # Seg7 รับข้อความ seg.value(n) เงียบสนิท

    # จุดที่เต้นคือสัญญาณว่าลูปยังเดินอยู่ ไม่ใช่เครื่องแฮงก์
    # ถ้าไม่มีสัญญาณแบบนี้ จอที่นิ่งกับจอที่ตายหน้าตาเหมือนกันเป๊ะ
    ticks += 1
    alive.text("กำลังรอคำสั่ง" + "." * (ticks % 4) + "   รอบที่ " + str(ticks))

    # ui.poll() ต้องเรียกทุกรอบ ไม่งั้นจอค้างทั้งที่โปรแกรมยังเดิน
    ui.poll()

    # 100 ms ต่อรอบคือถี่พอสำหรับคำสั่งที่คนกด และไม่กิน CPU จนเกินไป
    time.sleep_ms(LOOP_MS)
