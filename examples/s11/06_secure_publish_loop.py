# 06_secure_publish_loop.py - ส่งขึ้นแพลตฟอร์มผ่าน TLS แล้วโชว์หลักฐานบนจอ
#
# ไฟล์นี้สอน: ต้องเช็ก is_connected() ก่อนส่งทุกครั้ง เพราะสายหลุดได้ระหว่างลูป
#             และโปรแกรมที่ส่งต่อไปเฉย ๆ จะทำข้อความหายทั้งชั่วโมงโดยไม่มีใครรู้
# ดูที่จอ   : โหมดกับพอร์ตที่ใช้จริง - เลขใหญ่นับจำนวนครั้งที่ส่งสำเร็จ
#             - บรรทัดสถานะสายที่เปลี่ยนสีเขียว/แดง - กราฟจังหวะการส่ง
# กับดัก    : tesaiot.publish() ไม่ต้องใส่ topic เฟิร์มแวร์ประกอบให้จาก device_id
#             ใส่ topic เองจะไม่ตรงกับที่แพลตฟอร์มรออยู่

import wifi
import tesaiot
import lcd
import json
import time
import ui

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
DEVICE_ID = "eva-team03"
API_KEY = "<api key ของทีม>"
MQTT_PASS = "<รหัสผ่าน MQTT ของทีม>"
BROKER = "<ชื่อโฮสต์ของแพลตฟอร์ม>"

SEND_EVERY_MS = 2000        # ถี่พอให้เห็นจังหวะบนจอ งานจริงห่างกว่านี้ได้
TICK_MS = 100               # ซอยการหน่วงเป็นช่วงสั้น ๆ เพื่อให้ ui.poll() ได้ทำงาน
WAIT_LIMIT_MS = 30000

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF

ui.screen()
time.sleep_ms(200)

ui.Label("ส่งขึ้นแพลตฟอร์มผ่าน TLS", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=652, h=120, color=COL_CARD, min=COL_DIM, max=12, value=1)
mode_label = ui.Label("ยังไม่ได้ตั้งโหมด", x=40, y=64, color=COL_INFO, value=20)
seg_sent = ui.Seg7(text="0", x=40, y=96, w=152, h=48, color=COL_OK)
# คำอธิบายวางใต้ตัวเลข Seg7 สูง 48 จบที่ y=144 ป้ายจึงเริ่มที่ 146
ui.Label("ส่งสำเร็จ (ครั้ง)", x=40, y=148, color=COL_DIM, value=16)
link_label = ui.Label("สาย: ยังไม่ได้ต่อ", x=360, y=100, color=COL_WARN, value=24)
ui.Label("จังหวะการส่ง (ms ระหว่างสองครั้ง)", x=20, y=176, color=COL_DIM, value=16)
chart = ui.Chart(x=20, y=212, w=652, h=120, color=COL_CARD,
                 min=0, max=SEND_EVERY_MS * 2)
series = chart.add_series(COL_OK)
status = ui.Label("กำลังต่อ WiFi " + WIFI_SSID, x=20, y=332, color=COL_DIM, value=20)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 11 - MQTTs</h2>")
lcd.print("กำลังต่อ WiFi", WIFI_SSID)
if not wifi.connect(WIFI_SSID, WIFI_PASS):
    status.text("ต่อ WiFi ไม่ได้ จบตรงนี้")
    status.color(COL_BAD)
    ui.poll()
    lcd.print("<span class=error>ต่อ WiFi ไม่ได้</span>")
    raise SystemExit

status.text("ได้ IP " + wifi.ip() + " - กำลังตั้งค่าแพลตฟอร์ม")
ui.poll()
lcd.print("ได้ IP", wifi.ip())

tesaiot.config_set("device_id", DEVICE_ID)
tesaiot.config_set("api_key", API_KEY)
tesaiot.config_set("mqtt_pass", MQTT_PASS)
tesaiot.config_set("broker", BROKER)
tesaiot.config_set("sni_hostname", BROKER)
tesaiot.config_set("tls_mode", "server_tls")

# อ่านกลับครั้งเดียว เก็บไว้ใช้ทั้งไฟล์ ไม่ต้องเรียก config() ซ้ำในลูป
cfg = tesaiot.config()
if cfg["tls_mode"] != "serverTLS":
    mode_label.text("โหมดไม่ตรง ได้ " + cfg["tls_mode"])
    mode_label.color(COL_BAD)
    ui.poll()
    lcd.print("<span class=error>โหมดไม่ตรง ได้", cfg["tls_mode"], "</span>")
    raise SystemExit

mode_label.text("โหมด {} -> พอร์ต 8884".format(cfg["tls_mode"]))
ui.poll()
lcd.print("โหมด", cfg["tls_mode"], "-> พอร์ต 8884")

tesaiot.connect()
t0 = time.ticks_ms()
status.text("connect() คืนค่าแล้ว กำลังรอ is_connected()")
status.color(COL_WARN)
ui.poll()

while not tesaiot.is_connected():
    if time.ticks_diff(time.ticks_ms(), t0) > WAIT_LIMIT_MS:
        status.text("ต่อไม่สำเร็จใน 30 วินาที")
        status.color(COL_BAD)
        ui.poll()
        lcd.print("<span class=error>ต่อไม่สำเร็จใน 30 วินาที</span>")
        raise SystemExit
    ui.poll()
    time.sleep_ms(TICK_MS)

up_ms = time.ticks_diff(time.ticks_ms(), t0)
link_label.text("สาย: ต่ออยู่")
link_label.color(COL_OK)
status.text("TLS สำเร็จใน {} ms - เช็ก is_connected() ก่อนส่งทุกครั้ง".format(up_ms))
status.color(COL_OK)
ui.poll()
lcd.print("<span class=ok>TLS สำเร็จใน", up_ms, "ms</span>")

sent = 0
last_send = time.ticks_ms()
while True:
    # เช็กสายก่อนส่งทุกครั้ง สายที่เคยดีเมื่อห้าวินาทีก่อนไม่ใช่หลักฐานของตอนนี้
    if not tesaiot.is_connected():
        link_label.text("สาย: หลุดแล้ว")
        link_label.color(COL_BAD)
        status.text("หลุดการเชื่อมต่อ ส่งไปแล้ว {} ครั้ง".format(sent))
        status.color(COL_BAD)
        ui.poll()
        lcd.print("<span class=warn>หลุดการเชื่อมต่อ ส่งไปแล้ว", sent, "ครั้ง</span>")
        break

    # ค่าที่ส่งวันนี้เป็นตัวนับกับเวลาเดินเครื่อง คาบ 12 ทีมจะเปลี่ยนเป็นค่าของโจทย์ตัวเอง
    # ส่งแบน ไม่ต้องห่อ bridge ของแพลตฟอร์มเป็นคนห่อให้เอง แล้วเติม device_id
    # จาก topic ให้ด้วย ห่อเองซ้ำจะได้ชื่อวัดขึ้นต้นด้วย data_ ทุกตัว
    payload = {"seq": sent + 1, "uptime_s": time.ticks_ms() // 1000}
    now = time.ticks_ms()
    gap = time.ticks_diff(now, last_send)
    last_send = now

    if tesaiot.publish(json.dumps(payload)):
        sent += 1
        seg_sent.text(str(sent))
        # ครั้งแรกไม่มีครั้งก่อนหน้าให้เทียบ จึงยังไม่มีจังหวะให้ลงกราฟ
        if sent > 1:
            chart.set_next(series, gap)
        lcd.print("ส่งครั้งที่", sent, "| โหมด", cfg["tls_mode"], "| พอร์ต 8884")
    else:
        link_label.text("สาย: publish ถูกปฏิเสธ")
        link_label.color(COL_WARN)
        lcd.print("<span class=warn>publish ถูกปฏิเสธ</span>")
    ui.poll()

    # หน่วงเป็นช่วงสั้น ๆ แทนการนอนยาวรวดเดียว จอจะได้ไม่ค้างระหว่างรอ
    waited = 0
    while waited < SEND_EVERY_MS:
        ui.poll()
        time.sleep_ms(TICK_MS)
        waited += TICK_MS
