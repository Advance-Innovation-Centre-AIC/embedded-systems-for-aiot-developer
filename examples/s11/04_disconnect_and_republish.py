# 04_disconnect_and_republish.py - ปิดงานให้เรียบร้อย แล้วเปิดใหม่
#
# ไฟล์นี้สอน: tesaiot.disconnect() คืน True/False ไม่เหมือน mqtt.disconnect()
#             ที่คืน None - สองโมดูลนี้เขียนคนละครั้งกัน อย่าจำรวมกัน
#             และ publish() ของโมดูลนี้รับ payload มาก่อน topic ซึ่งสลับกับ
#             mqtt.publish(topic, payload) ที่เพิ่งใช้มาทั้งคาบที่แล้ว
# ดูที่จอ   : ห้าขั้นไล่เปลี่ยนเป็นเขียว ขั้นที่ 4 คือการต่อกลับหลังปิดไปแล้ว
# กับดัก    : สลับลำดับเป็น tesaiot.publish(topic, payload) จะไม่ error
#             เพราะทั้งสองช่องรับสตริงเหมือนกัน แต่ข้อมูลไปโผล่ผิดที่เงียบ ๆ

import wifi
import tesaiot
import lcd
import ui
import json
import time

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
DEVICE_ID = "eva-team03"
API_KEY = "<api key ของทีม>"
MQTT_PASS = "<รหัสผ่าน MQTT ของทีม>"
BROKER = "mqtt.tesaiot.dev"
WAIT_MS = 30000

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

ui.screen()
time.sleep_ms(200)
ui.Label("ปิดงานให้เรียบร้อย แล้วเปิดใหม่", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=652, h=192, color=COL_CARD, min=COL_DIM, max=12, value=1)
s1 = ui.Label("1) WiFi + ตั้งตัวตน    รอ...", x=40, y=68, color=COL_DIM, value=20)
s2 = ui.Label("2) connect + รอจริง    รอ...", x=40, y=100, color=COL_DIM, value=20)
s3 = ui.Label("3) publish รอบแรก      รอ...", x=40, y=136, color=COL_DIM, value=20)
s4 = ui.Label("4) disconnect()        รอ...", x=40, y=168, color=COL_DIM, value=20)
s5 = ui.Label("5) ต่อกลับ + ส่งอีกใบ  รอ...", x=40, y=204, color=COL_DIM, value=20)

ui.Panel(x=20, y=256, w=652, h=72, color=COL_CARD, min=COL_DIM, max=12, value=1)
note = ui.Label("กำลังเริ่ม", x=40, y=272, color=COL_DIM, value=20)
note2 = ui.Label("-", x=40, y=300, color=COL_DIM, value=16)
ui.Label("tesaiot.publish รับ payload ก่อน topic", x=20, y=340, color=COL_DIM,
         value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 11 - ปิดแล้วเปิดใหม่</h2>")


def step(label, text, color):
    label.text(text)
    label.color(color)
    ui.poll()


def wait_connected(limit_ms):
    # connect() คืนค่าทันที ตัวที่ตอบได้จริงคือ is_connected() เท่านั้น
    # ลูปรอทุกลูปต้องมี timeout ไม่งั้นวันที่เน็ตล่มจะค้างตรงนี้ตลอดกาล
    t0 = time.ticks_ms()
    while not tesaiot.is_connected():
        if time.ticks_diff(time.ticks_ms(), t0) > limit_ms:
            return -1
        ui.poll()
        time.sleep_ms(500)
    return time.ticks_diff(time.ticks_ms(), t0)


# --- ขั้นที่ 1: WiFi แล้วตั้งตัวตน ---
note.text("ต่อ WiFi ก่อน จอจะนิ่งครู่หนึ่ง")
ui.poll()
if not wifi.connect(WIFI_SSID, WIFI_PASS):
    step(s1, "1) WiFi + ตั้งตัวตน    WiFi ไม่ติด", COL_BAD)
    note.color(COL_BAD)
    note.text("ตรวจ WIFI_SSID กับ WIFI_PASS ที่หัวไฟล์")
    raise SystemExit

tesaiot.config_set("device_id", DEVICE_ID)
tesaiot.config_set("api_key", API_KEY)
tesaiot.config_set("mqtt_pass", MQTT_PASS)
tesaiot.config_set("broker", BROKER)
tesaiot.config_set("sni_hostname", BROKER)
cfg = tesaiot.config()
step(s1, "1) WiFi + ตั้งตัวตน    IP {}".format(wifi.ip()), COL_OK)
note.text("โหมด {} broker {}".format(cfg["tls_mode"], cfg["broker"]))
lcd.print("ตั้งตัวตนแล้ว โหมด", cfg["tls_mode"])

# --- ขั้นที่ 2: ต่อ แล้วรอจนต่อเสร็จจริง ---
step(s2, "2) connect + รอจริง    กำลังรอ...", COL_WARN)
tesaiot.connect()
took = wait_connected(WAIT_MS)

if took < 0:
    step(s2, "2) connect + รอจริง    ไม่ติดใน 30 วินาที", COL_BAD)
    note.color(COL_BAD)
    note.text("ตรวจ broker sni_hostname และตัวตนของทีม")
    lcd.print("<span class=err>ต่อไม่ติดใน 30 วินาที</span>")
    raise SystemExit

step(s2, "2) connect + รอจริง    ต่อเสร็จใน {} ms".format(took), COL_OK)
lcd.print("<span class=ok>ต่อเสร็จใน", took, "ms</span>")

# --- ขั้นที่ 3: ส่งใบแรก ---
# payload มาก่อน topic ไม่ใส่ topic เฟิร์มแวร์ประกอบให้จาก device_id
sent1 = tesaiot.publish(json.dumps({"round": 1}))
step(s3, "3) publish รอบแรก      คืน {}".format(sent1),
     COL_OK if sent1 else COL_WARN)
lcd.print("publish รอบแรกคืนค่า", sent1)
time.sleep_ms(1000)

# --- ขั้นที่ 4: ปิดให้เรียบร้อย ---
# ตัวนี้คืน bool ไม่เหมือน mqtt.disconnect() ที่คืน None
closed = tesaiot.disconnect()
still = tesaiot.is_connected()
step(s4, "4) disconnect()  คืน {} ต่ออยู่ {}".format(closed, still),
     COL_OK if closed and not still else COL_BAD)
note.color(COL_DIM)
note.text("tesaiot.disconnect() คืน bool ส่วน mqtt คืน None")
lcd.print("disconnect() คืน", closed, "แล้ว is_connected() =", still)
print("tesaiot.disconnect() คืนค่า", closed, "(เป็น bool ไม่ใช่ None)")
time.sleep_ms(1200)

# --- ขั้นที่ 5: ต่อกลับแล้วส่งอีกใบ ---
step(s5, "5) ต่อกลับ + ส่งอีกใบ  กำลังรอ...", COL_WARN)
tesaiot.connect()
took2 = wait_connected(WAIT_MS)

if took2 < 0:
    step(s5, "5) ต่อกลับ + ส่งอีกใบ  ต่อกลับไม่ติด", COL_BAD)
    note2.color(COL_BAD)
    note2.text("ปิดแล้วเปิดใหม่ต้องเว้นจังหวะให้ broker ตามทัน")
else:
    sent2 = tesaiot.publish(json.dumps({"round": 2}))
    step(s5, "5) ต่อกลับ + ส่งอีกใบ  {} ms publish {}".format(took2, sent2),
         COL_OK if sent2 else COL_WARN)
    note2.color(COL_OK)
    note2.text("ปิดแล้วเปิดใหม่ได้ ไม่ต้องรีเซ็ตบอร์ด")
    lcd.print("<span class=ok>ต่อกลับใน", took2, "ms และส่งได้อีกใบ</span>")

ui.poll()
print("")
print("สรุปสามข้อของไฟล์นี้:")
print("  1. tesaiot.disconnect() คืน bool ส่วน mqtt.disconnect() คืน None")
print("  2. tesaiot.publish(payload) - payload มาก่อน ต่างจาก mqtt.publish(topic, payload)")
print("  3. connect() คืนค่าทันที ต้องวนรอ is_connected() เองทุกครั้ง")
