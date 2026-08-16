# 05_wait_for_connected.py - connect() คืนค่าก่อนต่อเสร็จ ต้องรอด้วย is_connected()
#
# ไฟล์นี้สอน: tesaiot.connect() สั่งให้เริ่มต่อแล้วคืนค่าทันที ค่าที่คืนมาจึงไม่ใช่
#             สถานะสุดท้าย ตัวเดียวที่ตอบได้ว่าต่อเสร็จหรือยังคือ is_connected()
# ดูที่จอ   : เลขซ้ายคือ ms ที่ connect() ใช้คืนค่า (แทบเป็นศูนย์) เลขขวาคือ ms
#             จนกว่าจะต่อสำเร็จจริง และแถบที่ค่อย ๆ เดินเข้าหาเพดานเวลา
# กับดัก    : ลูปรอที่ไม่มี timeout จะค้างตลอดกาลในวันที่แพลตฟอร์มล่ม
#             ทุกลูปที่รออะไรสักอย่างต้องมีทางออกด้วยเวลาเสมอ

import wifi
import tesaiot
import lcd
import time
import ui

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
DEVICE_ID = "eva-team03"
API_KEY = "<api key ของทีม>"
MQTT_PASS = "<รหัสผ่าน MQTT ของทีม>"
BROKER = "<ชื่อโฮสต์ของแพลตฟอร์ม>"

WAIT_LIMIT_MS = 30000       # รอนานสุดเท่านี้ แล้วยอมแพ้อย่างมีสติ
POLL_MS = 100               # ถามสถานะถี่พอให้แถบบนจอเดินลื่น

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF

ui.screen()
time.sleep_ms(200)

ui.Label("connect() คืนค่าก่อนต่อเสร็จ", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=652, h=120, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("connect() คืนค่าใน (ms)", x=40, y=64, color=COL_DIM, value=16)
seg_call = ui.Seg7(text="----", x=40, y=88, w=180, h=68, color=COL_INFO)
ui.Label("ต่อสำเร็จจริงที่ (ms)", x=360, y=64, color=COL_DIM, value=16)
seg_conn = ui.Seg7(text="----", x=360, y=88, w=180, h=68, color=COL_OK)

wait_label = ui.Label("เพดานเวลา {} ms".format(WAIT_LIMIT_MS), x=20, y=188,
                      color=COL_DIM, value=16)
bar = ui.Bar(x=20, y=212, w=652, h=28, min=0, max=WAIT_LIMIT_MS, value=0)
# สองป้ายในบรรทัดเดียว ให้อยู่ในเพดาน 126 ไบต์ของ ui.Label - ไทยตัวละ 3 ไบต์
gap_label = ui.Label("ช่องว่างระหว่างสองเลข", x=20, y=252, color=COL_DIM,
                     value=20)
ui.Label("คือเวลาที่ TLS ใช้จับมือ", x=252, y=252, color=COL_DIM, value=20)
step = ui.Label("กำลังต่อ WiFi " + WIFI_SSID, x=20, y=288, color=COL_WARN, value=20)
status = ui.Label("รอสัญญาณ...", x=20, y=336, color=COL_DIM, value=20)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 11 - รอให้ต่อเสร็จจริง</h2>")
lcd.print("กำลังต่อ WiFi", WIFI_SSID)

# ขึ้นข้อความก่อนเรียก ไม่ใช่หลังเรียก เพราะ wifi.connect บล็อกยาว
if not wifi.connect(WIFI_SSID, WIFI_PASS):
    step.text("ต่อ WiFi ไม่ได้")
    step.color(COL_BAD)
    status.text("TLS วิ่งบน TCP ซึ่งวิ่งบน IP จบตรงนี้")
    status.color(COL_BAD)
    ui.poll()
    lcd.print("<span class=error>ต่อ WiFi ไม่ได้ จบตรงนี้</span>")
    raise SystemExit

step.text("ได้ IP " + wifi.ip())
step.color(COL_OK)
ui.poll()
lcd.print("ได้ IP", wifi.ip())

# ตั้งตัวตนและปลายทางก่อนสั่งต่อ config_set แค่เก็บค่า ยังไม่ได้ต่ออะไรทั้งสิ้น
tesaiot.config_set("device_id", DEVICE_ID)
tesaiot.config_set("api_key", API_KEY)
tesaiot.config_set("mqtt_pass", MQTT_PASS)
tesaiot.config_set("broker", BROKER)

# sni_hostname คือชื่อที่เดินไปก่อนการเข้ารหัส เซิร์ฟเวอร์ใช้มันเลือกใบรับรองที่จะยื่น
# ต้องเป็นชื่อโฮสต์เดียวกับ broker ถ้าตั้งไม่ตรง การต่อจะล้มโดยไม่มีข้อความอะไรเลย
tesaiot.config_set("sni_hostname", BROKER)

# ตั้งโหมดแล้วอ่านกลับตรวจทันที ตามท่าที่ฝึกมาในไฟล์ 03
tesaiot.config_set("tls_mode", "server_tls")
mode = tesaiot.config()["tls_mode"]
if mode != "serverTLS":
    status.text("โหมดไม่ใช่ที่ขอไว้ ได้ " + mode + " แทน หยุดก่อน")
    status.color(COL_BAD)
    ui.poll()
    lcd.print("<span class=error>โหมดไม่ตรง ได้", mode, "</span>")
    raise SystemExit

status.text("โหมดตรวจแล้ว: " + mode + " -> พอร์ต 8884")
status.color(COL_INFO)
ui.poll()
lcd.print("โหมดตรวจแล้ว:", mode, "-> พอร์ต 8884")

# วัดเวลาสองช่วงแยกกัน ช่วงแรกคือคำสั่ง ช่วงที่สองคือการเชื่อมต่อจริง
t0 = time.ticks_ms()
tesaiot.connect()
t_call = time.ticks_diff(time.ticks_ms(), t0)
seg_call.text(str(t_call))
step.text("connect() คืนค่าแล้ว แต่ยังไม่ได้แปลว่าต่อแล้ว")
step.color(COL_WARN)
ui.poll()
lcd.print("connect() คืนค่าใน", t_call, "ms")
print("connect() คืนค่าภายใน", t_call, "ms  <- ยังไม่ได้แปลว่าต่อแล้ว")

while not tesaiot.is_connected():
    waited = time.ticks_diff(time.ticks_ms(), t0)

    # ทางออกด้วยเวลา ต้องมาก่อนการรออีกรอบเสมอ
    if waited > WAIT_LIMIT_MS:
        step.text("ยังต่อไม่สำเร็จภายในเพดานเวลา ยอมแพ้")
        step.color(COL_BAD)
        status.text("ไล่ตรวจ: broker - sni_hostname - api_key - เวลาของเครื่อง")
        status.color(COL_BAD)
        ui.poll()
        lcd.print("<span class=error>ยอมแพ้ที่", WAIT_LIMIT_MS, "ms</span>")
        lcd.print("ไล่ตรวจ broker - sni - api_key - นาฬิกา")
        raise SystemExit

    bar.value(waited)
    wait_label.text("รออยู่ {} ms จากเพดาน {} ms".format(waited, WAIT_LIMIT_MS))
    ui.poll()
    time.sleep_ms(POLL_MS)

total = time.ticks_diff(time.ticks_ms(), t0)
bar.value(total)        # แถบค้างไว้ตรงที่รอจริง ห่างจากเพดานอีกไกล
seg_conn.text(str(total))
wait_label.text("รอจริง {} ms จากเพดาน {} ms".format(total, WAIT_LIMIT_MS))
gap_label.text("ต่างกัน {} ms".format(total - t_call))
gap_label.color(COL_WARN)
step.text("ต่อสำเร็จจริงแล้ว ตอนนี้ publish ได้")
step.color(COL_OK)
status.text("เลขสองตัวไม่เท่ากัน = คืนค่าแล้ว ไม่ใช่เสร็จ")
status.color(COL_OK)
ui.poll()

lcd.print("<span class=ok>ต่อสำเร็จจริงที่", total, "ms</span>")
lcd.print("ต่างกัน", total - t_call, "ms คือเวลาที่ TLS จับมือ")
print("ต่อสำเร็จจริงที่", total, "ms")
print("ต่างกัน", total - t_call, "ms คือเวลาที่ TLS ใช้จับมือ")
