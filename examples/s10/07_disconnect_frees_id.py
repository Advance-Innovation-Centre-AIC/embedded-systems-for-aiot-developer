# 07_disconnect_frees_id.py - บอกลา broker ให้ถูกวิธี แล้วต่อใหม่ด้วยชื่อเดิมได้ทันที
#
# ไฟล์นี้สอน: mqtt.disconnect() ไม่ได้มีไว้ตอนโปรแกรมพัง มันมีไว้ตอนเราเลิกใช้
#             ตามตั้งใจ ถ้าไม่เรียก broker จะยังนับว่าเรายังอยู่จนครบ keepalive
#             ระหว่างนั้น client_id เดิมยังถูกจอง กดรันใหม่ก็จะชนกับตัวเองเมื่อกี้
#             อาการที่เห็นคือ "ต่อติดแล้วหลุดสลับกัน" ซึ่งไม่ได้มีอะไรผิดในโค้ดเลย
# ดูที่จอ   : สี่ขั้นไล่เปลี่ยนเป็นเขียว ขั้นที่ 4 คือการต่อกลับด้วย client_id ตัวเดิม
#             ถ้าขั้นนี้ผ่าน แปลว่าการบอกลาของเราไปถึง broker จริง
# กับดัก    : disconnect() คืน None ไม่ใช่ True เขียน if mqtt.disconnect(): จะไม่จริงเลย
#             และหลังตัดแล้ว publish() จะโยน OSError ไม่ได้คืน False - ไฟล์นี้จับให้ดู

import wifi
import mqtt
import lcd
import ui
import json
import time

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
BROKER = "192.168.1.50"          # IP ของเครื่องที่รัน broker ในแลน ไม่ใช่ localhost
DEVICE_ID = "team03"         # ใช้ตัวเดียวกันทั้งสองรอบ นั่นคือประเด็นของไฟล์นี้
TOPIC = "bento/team03/telemetry"

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_BAD, COL_WARN = 0x30A46C, 0xE5484D, 0xF5A623

ui.screen()
time.sleep_ms(200)
ui.Label("บอกลาให้ถูกวิธี แล้วกลับมาใหม่", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=652, h=192, color=COL_CARD, min=COL_DIM, max=12, value=1)
st1 = ui.Label("1) ต่อรอบแรก           รอ...", x=36, y=68, color=COL_DIM, value=20)
st2 = ui.Label("2) ส่งหนึ่งใบ          รอ...", x=36, y=100, color=COL_DIM, value=20)
st3 = ui.Label("3) disconnect()        รอ...", x=36, y=136, color=COL_DIM, value=20)
st4 = ui.Label("4) ต่อกลับชื่อเดิม     รอ...", x=36, y=168, color=COL_DIM, value=20)
st5 = ui.Label("5) publish หลังตัด     รอ...", x=36, y=204, color=COL_DIM, value=20)

ui.Panel(x=20, y=256, w=652, h=72, color=COL_CARD, min=COL_DIM, max=12, value=1)
note = ui.Label("กำลังเริ่ม", x=36, y=272, color=COL_DIM, value=20)
note2 = ui.Label("-", x=36, y=300, color=COL_DIM, value=16)
ui.Label("client_id เดิมถูกใช้ซ้ำทั้งสองรอบโดยตั้งใจ", x=20, y=340,
         color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 10 - disconnect() คืนชื่อให้ว่าง</h2>")


def step(label, text, color):
    label.text(text)
    label.color(color)
    ui.poll()


# --- ขั้นที่ 1: WiFi แล้วต่อ broker รอบแรก ---
note.text("ต่อ WiFi ก่อน จอจะนิ่งครู่หนึ่ง")
ui.poll()
if not wifi.connect(WIFI_SSID, WIFI_PASS):
    step(st1, "1) ต่อรอบแรก           WiFi ไม่ติด", COL_BAD)
    note.color(COL_BAD)
    note.text("ตรวจ WIFI_SSID กับ WIFI_PASS ที่หัวไฟล์")
    lcd.print("<span class=err>WiFi ไม่ติด</span>")
    raise SystemExit

lcd.print("WiFi ได้ IP {}".format(wifi.ip()))
if not mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID):
    step(st1, "1) ต่อรอบแรก           ต่อ broker ไม่ได้", COL_BAD)
    note.color(COL_BAD)
    note.text("ตรวจ BROKER ว่าเป็น IP ของเครื่องในแลนจริง")
    lcd.print("<span class=err>ต่อ broker ไม่ได้</span>")
    raise SystemExit

step(st1, "1) ต่อรอบแรก           สำเร็จ", COL_OK)
note.text("ต่อด้วย client_id = " + DEVICE_ID)
lcd.print("<span class=ok>ต่อรอบแรกสำเร็จ</span>")
time.sleep_ms(800)

# --- ขั้นที่ 2: ส่งหนึ่งใบ เพื่อให้แน่ใจว่าเซสชันใช้งานได้จริง ---
sent = mqtt.publish(TOPIC, json.dumps({"round": 1}))
step(st2, "2) ส่งหนึ่งใบ          publish คืน {}".format(sent),
     COL_OK if sent else COL_WARN)
lcd.print("publish รอบแรกคืนค่า {}".format(sent))
time.sleep_ms(800)

# --- ขั้นที่ 3: บอกลา ---
# ค่าที่คืนกลับมาคือ None เสมอ ไม่ใช่ True จึงห้ามเอาไปตัดสินใจใน if
result = mqtt.disconnect()
still = mqtt.is_connected()
step(st3, "3) disconnect()  คืน {} is_connected {}".format(
    result, still), COL_OK if not still else COL_BAD)
note.text("ตัดแล้ว broker รู้ทันที ไม่ต้องรอ keepalive หมด")
lcd.print("disconnect() คืน {} แล้ว is_connected() = {}".format(result, still))
print("disconnect() คืนค่า", result, "(None เสมอ)")
time.sleep_ms(1000)

# --- ขั้นที่ 4: ต่อกลับด้วยชื่อเดิม ---
# ถ้าขั้นนี้ผ่านทันที แปลว่าชื่อถูกคืนให้ว่างจริง
# ถ้าไม่เคยเรียก disconnect() ขั้นนี้มักจะติด ๆ หลุด ๆ อยู่ราวหนึ่งนาที
back = mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID)
step(st4, "4) ต่อกลับชื่อเดิม     {}".format("สำเร็จ" if back else "ไม่สำเร็จ"),
     COL_OK if back else COL_BAD)
lcd.print("ต่อกลับด้วยชื่อเดิมคืนค่า {}".format(back))

if back:
    note.color(COL_OK)
    note.text("ชื่อ {} ว่างทันทีหลังบอกลา".format(DEVICE_ID))
    mqtt.publish(TOPIC, json.dumps({"round": 2}))
    note2.text("ส่งรอบสองสำเร็จบนเซสชันใหม่")

# --- ขั้นที่ 5: พิสูจน์ว่า publish หลังตัดโยน OSError ไม่ได้คืน False ---
mqtt.disconnect()
try:
    mqtt.publish(TOPIC, json.dumps({"round": 3}))
    step(st5, "5) publish หลังตัด     ไม่โยน error (ผิดคาด)", COL_WARN)
except OSError as e:
    # นี่คือพฤติกรรมที่ถูกต้อง ดักไว้เพื่อให้เห็นว่ามันเป็น error ไม่ใช่ค่า False
    step(st5, "5) publish หลังตัด     OSError ตามคาด", COL_OK)
    note2.text("publish ตอนไม่ได้ต่อ = OSError ไม่ใช่ False")
    lcd.print("publish หลังตัดได้ OSError ตามคาด: {}".format(e))
    print("publish หลังตัด โยน OSError:", e)

ui.poll()
print("สรุปสามข้อ:")
print("  1. disconnect() คืน None ห้ามใส่ใน if")
print("  2. บอกลาแล้ว client_id ว่างทันที ต่อใหม่ชื่อเดิมได้เลย")
print("  3. publish ตอนไม่ได้ต่อ โยน OSError ไม่ได้คืน False")
