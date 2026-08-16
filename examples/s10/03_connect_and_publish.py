# 03_connect_and_publish.py - ต่อ broker แล้วส่งค่าขึ้นไปหนึ่งชุด
#
# ไฟล์นี้สอน: บันไดสามขั้นที่ห้ามสลับ - WiFi ให้ IP ก่อน แล้ว TCP จึงต่อได้
#             แล้ว MQTT จึงแนะนำตัวได้ ขั้นที่ผ่านแล้วต้องเห็นด้วยตาว่าผ่าน
# ดูที่จอ   : ป้ายสามขั้นไล่เปลี่ยนจากเทาเป็นเขียวตามลำดับ แล้วเลขใหญ่กับแท่ง
#             ความคืบหน้าเดินขึ้นทีละใบจนครบสิบ
# กับดัก    : ชื่ออาร์กิวเมนต์คือ username= ไม่ใช่ user= ใส่ผิดจะได้ TypeError ทันที
#             และ publish() บนบอร์ดจะโยน OSError ถ้ายังไม่ได้ต่อ ไม่ได้คืน False เฉย ๆ

import wifi
import mqtt
import lcd
import ui
import json
import time

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
BROKER = "192.168.1.50"          # IP ของเครื่องที่รัน broker ในแลน ไม่ใช่ localhost
DEVICE_ID = "eva-team03"
MQTT_USER = "eva-team03"         # ต้องเท่ากับ client_id ฝั่ง CE ตรวจข้อนี้
MQTT_PASS = "<รหัสผ่าน MQTT ของทีม>"
TOPIC = "bento/eva-team03/telemetry"
N = 10

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_TRACK = 0x171B22         # สีรางของ ui.Bar - ตัวแท่งที่วิ่งเป็นสีของธีมเสมอ
COL_OK, COL_BAD, COL_INFO = 0x30A46C, 0xE5484D, 0x4A9EFF

# วางจอให้ครบก่อนเริ่มต่อ ป้ายที่ยังไม่ถึงคิวเป็นสีเทา คนดูจึงรู้ว่าเหลืออีกกี่ขั้น
ui.screen()
time.sleep_ms(200)

ui.Label("บันไดสามขั้นก่อนส่งได้", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=652, h=120, color=COL_CARD, min=COL_DIM, max=12, value=1)
st_wifi = ui.Label("1) WiFi        รอ...", x=36, y=68, color=COL_DIM, value=20)
st_broker = ui.Label("2) broker      รอ...", x=36, y=100, color=COL_DIM, value=20)
st_pub = ui.Label("3) publish     รอ...", x=36, y=136, color=COL_DIM, value=20)

ui.Panel(x=20, y=184, w=652, h=140, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ส่งไปแล้วกี่ใบ", x=36, y=196, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=36, y=224, w=180, h=76, color=COL_INFO)
ui.Label("ความคืบหน้าของ 10 ใบ", x=240, y=212, color=COL_DIM, value=16)
bar = ui.Bar(x=240, y=244, w=412, h=24, min=0, max=N, value=0, color=COL_TRACK)
note = ui.Label("กำลังเริ่ม...", x=20, y=336, color=COL_DIM, value=20)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 10 - publish ครั้งแรก</h2>")

# ขั้นที่ 1 - WiFi ต้องได้ IP ก่อน
# ป้าย "กำลังต่อ" ต้องขึ้นก่อนบรรทัด connect() เพราะระหว่างต่อจอจะไม่อัปเดตเลย
st_wifi.text("1) WiFi        กำลังต่อ " + WIFI_SSID)
ui.poll()
lcd.print("1) กำลังต่อ WiFi", WIFI_SSID)
if not wifi.connect(WIFI_SSID, WIFI_PASS):
    st_wifi.color(COL_BAD)
    st_wifi.text("1) WiFi        ต่อไม่ได้")
    note.color(COL_BAD)
    note.text("จบตรงนี้ ตรวจชื่อวงกับรหัสผ่าน")
    ui.poll()
    lcd.print("<span class=err>ต่อ WiFi ไม่ได้ จบตรงนี้</span>")
    raise SystemExit
st_wifi.color(COL_OK)
st_wifi.text("1) WiFi        IP " + wifi.ip())
ui.poll()
lcd.print("<span class=ok>ได้ IP", wifi.ip(), "</span>")

# ขั้นที่ 2 - MQTT แนะนำตัวกับ broker
# client_id ต้องไม่ซ้ำกับใครบน broker เดียวกัน ถ้าซ้ำ broker จะเตะตัวเก่าออก
# แล้วสองบอร์ดจะผลัดกันเตะกันไปมาทั้งคาบ โดยไม่มีข้อความเตือนที่ฝั่งเรา
# keepalive=60 แปลว่าเงียบเกิน 60 วินาทีเมื่อไร broker มีสิทธิ์ตัดเราทิ้ง
st_broker.text("2) broker      กำลังต่อ " + BROKER)
ui.poll()
lcd.print("2) กำลังต่อ broker", BROKER)
ok = mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID,
                  username=MQTT_USER, password=MQTT_PASS, keepalive=60)

if not ok:
    st_broker.color(COL_BAD)
    st_broker.text("2) broker      ปฏิเสธ")
    note.color(COL_BAD)
    note.text("ตรวจ IP ของ broker และพอร์ต 1883")
    ui.poll()
    lcd.print("<span class=err>ต่อ broker ไม่ได้</span>")
    lcd.print("ตรวจ IP ของ broker และพอร์ต 1883")
    raise SystemExit

st_broker.color(COL_OK)
st_broker.text("2) broker      ต่อแล้ว " + BROKER)
st_pub.color(COL_OK)
st_pub.text("3) publish     กำลังส่ง")
note.text("ส่งทุก 2 วินาที ดูเลขเดินขึ้น")
ui.poll()
lcd.print("<span class=ok>ต่อ broker แล้ว</span>")

# ขั้นที่ 3 - ส่งจริงสิบครั้ง ค่าที่ส่งคือตัวนับกับเวลาเดินเครื่อง
# ตัวนับดีตรงที่ฝั่งรับเห็นทันทีว่าข้อความหายไประหว่างทางหรือไม่ เลขจะข้าม
done = 0
for i in range(1, N + 1):
    payload = {"id": DEVICE_ID,
               "count": i,
               "uptime_s": time.ticks_ms() // 1000}

    # publish() คืน True เมื่อส่งต่อให้ชั้นเครือข่ายสำเร็จ
    # บนบอร์ด ถ้ายังไม่ได้ต่อ มันไม่คืน False - มันโยน OSError ออกมาเลย
    # จึงต้องมีทั้ง try และการเช็กค่าที่คืนกลับ โค้ดจึงถูกทั้งบนบอร์ดและบน emulator
    try:
        sent = mqtt.publish(TOPIC, json.dumps(payload))
    except OSError:
        st_pub.color(COL_BAD)
        st_pub.text("3) publish     สายหลุดที่ใบที่ " + str(i))
        ui.poll()
        lcd.print("<span class=err>สายหลุดระหว่างส่ง หยุดที่ครั้งที่", i, "</span>")
        break

    if not sent:
        st_pub.color(COL_BAD)
        st_pub.text("3) publish     ถูกปฏิเสธที่ใบที่ " + str(i))
        ui.poll()
        lcd.print("<span class=err>ใบที่", i, "ถูกปฏิเสธ</span>")
        break

    done = i
    seg.text(str(done))          # Seg7 รับข้อความ ไม่ใช่ตัวเลข
    bar.value(done)
    ui.poll()
    lcd.print("ส่งครั้งที่", i, "-> ok")
    time.sleep_ms(2000)

st_pub.text("3) publish     ส่งแล้ว " + str(done) + " ใบ")
note.color(COL_OK if done == N else COL_BAD)
note.text("จบรอบ ส่งได้ " + str(done) + " จาก " + str(N) + " ใบ")
ui.poll()
lcd.print("จบรอบทดสอบ ส่งได้", done, "จาก", N, "ใบ")
lcd.print("ดูฝั่ง MQTT Explorer ว่าครบสิบข้อความไหม")
