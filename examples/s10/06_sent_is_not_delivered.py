# 06_sent_is_not_delivered.py - publish คืน True แปลว่าอะไร และไม่แปลว่าอะไร
#
# Why : รายงานที่เขียนว่า "ส่งสำเร็จ 100%" เพราะนับ True จาก publish() คือรายงาน
#        ที่นับความตั้งใจของตัวเอง ไม่ได้นับของที่ถึงปลายทาง วันที่ลูกค้าถามว่า
#        ทำไมกราฟหาย ตัวเลขฝั่งเราจะยืนยันว่าไม่มีอะไรผิด ทั้งที่ข้อมูลหายจริง
# What: True แปลว่า "ส่งต่อให้ชั้นเครือข่ายแล้ว" เท่านั้น ไม่ได้แปลว่า broker
#        ได้รับ และไม่ได้แปลว่ามีใครอ่าน หลักฐานเดียวที่เชื่อได้คือของเดินกลับมา
#
# ดูที่จอ: เลขใหญ่สองก้อนวางข้างกัน ซ้ายคือ "เราบอกว่าส่งแล้ว" ขวาคือ "กลับมาจริง"
#          ถ้าสองก้อนไม่เท่ากัน ป้ายผลต่างจะเป็นสีแดง นั่นคือคำตอบของทั้งไฟล์
# กับดัก : QoS 0 คือ "ส่งแล้วลืม" ไม่มีการยืนยันจาก broker เลย
#          โค้ดที่นับ True จาก publish() แล้วรายงานว่าส่งครบ กำลังรายงานผิด

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

# ส่งไปที่ topic ของตัวเอง แล้ว subscribe topic เดียวกัน ข้อความจึงวิ่งครบวง
# broker -> กลับมาหาเรา นี่คือวิธีพิสูจน์การส่งถึงที่ถูกที่สุดที่ทำได้ในห้องเรียน
TOPIC_ECHO = "bento/eva-team03/echo"
N = 10

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_TRACK = 0x27364F         # สีรางของ ui.Bar - ตัวแท่งที่วิ่งเป็นสีของธีมเสมอ
COL_OK, COL_BAD, COL_INFO = 0x00E676, 0xFF5252, 0x40C4FF

ui.screen()
time.sleep_ms(200)

ui.Label("ที่บอกว่าส่ง กับ ที่กลับมาจริง", x=20, y=12, color=COL_TEXT, value=24)

# ฝั่งซ้าย: ตัวเลขที่โปรแกรมของเราอ้าง
ui.Panel(x=20, y=52, w=325, h=150, color=COL_CARD, min=COL_INFO, max=12, value=1)
ui.Label("publish() บอกว่าสำเร็จ", x=36, y=64, color=COL_DIM, value=16)
seg_claim = ui.Seg7(text="0", x=36, y=94, w=180, h=76, color=COL_INFO)
bar_claim = ui.Bar(x=36, y=178, w=290, h=20, min=0, max=N, value=0, color=COL_TRACK)

# ฝั่งขวา: ตัวเลขที่วัดได้จริง
ui.Panel(x=355, y=52, w=315, h=150, color=COL_CARD, min=COL_OK, max=12, value=1)
ui.Label("เดินทางกลับมาถึงเรา", x=371, y=64, color=COL_DIM, value=16)
seg_seen = ui.Seg7(text="0", x=371, y=94, w=180, h=76, color=COL_OK)
bar_seen = ui.Bar(x=371, y=178, w=285, h=20, min=0, max=N, value=0, color=COL_TRACK)

l_diff = ui.Label("ต่างกัน 0 ใบ", x=20, y=220, color=COL_DIM, value=28)
l_note = ui.Label("กำลังต่อเน็ต...", x=20, y=266, color=COL_DIM, value=18)
# แยกสองป้ายให้อยู่ในเพดาน 126 ไบต์ของ ui.Label - ไทยหนึ่งตัวกิน 3 ไบต์
l_end = ui.Label("หลักฐานว่าถึงจริง", x=20, y=302, color=COL_DIM, value=18)
ui.Label("ต้องนับที่ปลายทาง ไม่ใช่ต้นทาง", x=200, y=302, color=COL_DIM,
         value=18)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 10 - ส่งแล้วถึงจริงไหม</h2>")

if not wifi.connect(WIFI_SSID, WIFI_PASS):
    l_note.color(COL_BAD)
    l_note.text("ต่อ WiFi ไม่ได้")
    ui.poll()
    lcd.print("<span class=err>ต่อ WiFi ไม่ได้</span>")
    raise SystemExit
if not mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID):
    l_note.color(COL_BAD)
    l_note.text("ต่อ broker ไม่ได้")
    ui.poll()
    lcd.print("<span class=err>ต่อ broker ไม่ได้</span>")
    raise SystemExit

mqtt.subscribe(TOPIC_ECHO)
l_note.text("ส่ง " + str(N) + " ใบไปที่ topic ของตัวเอง แล้วนั่งนับที่กลับมา")
ui.poll()
lcd.print("subscribe", TOPIC_ECHO, "แล้ว")

claimed = 0     # จำนวนครั้งที่ publish() บอกว่าสำเร็จ
seen = 0        # จำนวนใบที่เดินทางกลับมาถึงเราจริง

for i in range(1, N + 1):
    # บนบอร์ด publish() โยน OSError เมื่อยังไม่ได้ต่อ ส่วน emulator คืน False
    # ต้องดักทั้งสองแบบ ตัวเลข claimed จึงจะเป็นตัวเลขที่เชื่อได้ว่านับถูก
    try:
        if mqtt.publish(TOPIC_ECHO, json.dumps({"seq": i})):
            claimed += 1
    except OSError:
        lcd.print("<span class=err>สายหลุดที่ใบที่", i, "</span>")
        break

    seg_claim.text(str(claimed))
    bar_claim.value(claimed)

    # หลังส่งแต่ละใบ เปิดหน้าต่างรับสั้น ๆ แล้วเก็บของที่วิ่งกลับมา
    # ต้องถามหลายครั้งในหน้าต่างนี้ เพราะบัฟเฟอร์มีช่องเดียว ถามครั้งเดียวจะพลาด
    t0 = time.ticks_ms()
    while time.ticks_diff(time.ticks_ms(), t0) < 400:
        msg = mqtt.get_message()
        if msg is not None:
            seen += 1
            seg_seen.text(str(seen))
            bar_seen.value(seen)
        ui.poll()
        time.sleep_ms(20)

diff = claimed - seen
l_diff.text("ต่างกัน " + str(diff) + " ใบ")

if seen < claimed:
    # ไม่ใช่บั๊กเสมอไป - ใบที่มาติดกันเร็วกว่าที่เราถามทันจะทับกันในบัฟเฟอร์ช่องเดียว
    # บทเรียนคือ ตัวเลข "ส่งแล้ว" ที่ฝั่งเราไม่ใช่ตัวเลขเดียวกับ "ถึงแล้ว" ที่ฝั่งโน้น
    l_diff.color(COL_BAD)
    l_note.color(COL_BAD)
    l_note.text("เลขสองข้างไม่ตรงกัน ฝั่งเราเชื่อไม่ได้")
    lcd.print("<span class=warn>ต่างกัน", diff, "ใบ</span>")
    # แยกสองบรรทัด เพราะ lcd.print ตัดทิ้งที่ 127 ไบต์ และไทยกินตัวละ 3 ไบต์
    lcd.print("ใบที่มาเร็วกว่าที่เราถามทัน")
    lcd.print("จึงทับกันในบัฟเฟอร์ช่องเดียว")
else:
    l_diff.color(COL_OK)
    l_note.color(COL_OK)
    l_note.text("รอบนี้ครบ แต่ QoS 0 ไม่ประกันว่ารอบหน้าจะครบ")
    lcd.print("<span class=ok>ครบทุกใบในรอบนี้</span>")

l_end.color(COL_TEXT)
ui.poll()
lcd.print("publish() บอกว่าสำเร็จ", claimed, "ครั้ง")
lcd.print("วิ่งกลับมาถึงเราจริง", seen, "ใบ")
lcd.print("ถ้าต้องการหลักฐานว่าถึงจริง")
lcd.print("ต้องนับที่ปลายทาง ไม่ใช่ที่ต้นทาง")
