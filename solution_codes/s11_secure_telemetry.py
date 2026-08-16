# s11_secure_telemetry.py - ส่ง telemetry ขึ้นแพลตฟอร์มผ่าน TLS
# วิธีรัน: 1) รับ device_id - api_key - รหัส MQTT ที่อาจารย์ provision ให้ทีมมาก่อน
#          2) แก้ห้าบรรทัดบนหัวไฟล์ให้เป็นของทีม ที่เหลือเหมือนกันทุกทีม
#          3) กด Program to Device แล้วเปิด dashboard ของแพลตฟอร์มดูค่าไหลขึ้น
#
# ต่างจากคาบ 10 ตรงที่เราไม่ได้เลือกพอร์ตเอง - พอร์ต 8884 มาจากค่า tls_mode
# ไม่ใช่จากคีย์ port และ tesaiot.connect() คืนค่าทันทีโดยยังไม่ได้ต่อเสร็จ
#
# ดูที่จอ: ซ้ายคือตารางตัวตนที่บอร์ดใช้แนะนำตัวกับแพลตฟอร์ม ขวาบนคือไฟสามดวง
#         ของการจับมือ TLS ขวากลางคือเวลาที่ใช้จับมือ เทียบเพดาน 30 วินาที
#         ขวาล่างคือจำนวนใบที่ส่ง กับปุ่มต่อ/ตัดสายที่คนหน้าจอสั่งได้
# กับดัก : mqtt_pass ตั้งได้แต่อ่านกลับไม่ได้ ตารางจึงเขียนว่า "ตั้งแล้ว อ่านไม่ได้"
#         ไม่ใช่ปล่อยช่องว่าง - ช่องว่างทำให้คนอ่านสรุปว่ายังไม่ได้ตั้ง ซึ่งคนละเรื่องกัน

import tesaiot
import sensors
import lcd
import time
import json
import ui

TEAM_NAME = "BentoBuilders"
DEVICE_ID = "eva-team03"          # ต้องตรงกับที่ขึ้นทะเบียนไว้ และสั้นกว่า 31 ตัวอักษร
API_KEY = "<api key ของทีม>"
MQTT_PASS = "<รหัสผ่าน MQTT 16 ตัว>"
BROKER = "<โฮสต์แพลตฟอร์ม>"

# บอร์ดนี้ไม่มี sensors.init() ให้เรียก เซนเซอร์อยู่บนบัสที่คอร์จอ (CM55) ถือคนเดียว
# ฝั่ง Python ขอค่าที่คอร์จออ่านเก็บไว้ให้ จึงเรียกอ่านได้เลย เรียก init() จะได้ OSError
# อุ่นเครื่องหนึ่งครั้งตรงนี้ เพราะหลังรีเซ็ต คอร์จอเริ่มตอบเรื่องเซนเซอร์ราว 13 วินาที
# ให้การรอไปเกิดก่อนจับมือ TLS ไม่ใช่ไปแทรกกลางรอบส่งข้อมูล
try:
    sensors.bmi270.motion()
except OSError:
    print("คอร์จอยังไม่ตอบรอบแรก จะลองใหม่ตอนส่ง")

lcd.clear()
lcd.console("<h2>MQTTs - ทีม " + TEAM_NAME + "</h2>")

# --- ท่าที่ 1: ตั้งค่าตัวตนของอุปกรณ์ ---
# config_set() แค่เก็บค่าไว้ในเฟิร์มแวร์ ยังไม่ได้ต่ออะไรทั้งสิ้น เรียกผิดคีย์ก็ไม่มีใครเตือน
# ตัวตนของอุปกรณ์คือสามอย่างนี้รวมกัน: ชื่อที่แพลตฟอร์มรู้จัก กุญแจของ API และรหัส MQTT
tesaiot.config_set("device_id", DEVICE_ID)
tesaiot.config_set("api_key", API_KEY)
tesaiot.config_set("mqtt_pass", MQTT_PASS)

# sni_hostname คือชื่อที่เดินไปก่อนการเข้ารหัส เซิร์ฟเวอร์ใช้มันเลือกว่าจะยื่นใบรับรองใบไหน
# ต้องเป็นชื่อโฮสต์เดียวกับ broker ถ้าตั้งไม่ตรง การต่อจะล้มเงียบ ๆ โดยไม่มีข้อความอะไรเลย
tesaiot.config_set("broker", BROKER)
tesaiot.config_set("sni_hostname", BROKER)

# ห้าบรรทัดข้างบนเป็นแค่ห้าคีย์จากทั้งหมด 19 คีย์ในคลังค่าตั้ง
# examples/s11/01_config_store.py เดินดูทั้งชุด และเป็นที่ที่ควรไปดูตอนสงสัยว่า
# "ค่าที่ตั้งไปเข้าจริงไหม" - พร้อมข้อยกเว้นข้อเดียวที่ต้องจำ คือ mqtt_pass ตั้งได้แต่อ่านกลับไม่ได้

print("config ปัจจุบัน:", tesaiot.config())

# --- ท่าที่ 2: กางหน้าจอ แล้วเอาตัวตนที่ตั้งไว้ขึ้นให้เห็น ---
# หน้าจอนี้ตอบคำถามเดียวที่คาบนี้ถาม: "ตกลงบอร์ดแนะนำตัวว่าเป็นใคร และต่อแบบไหน"
# ค่าที่ตั้งไว้ในท่าที่ 1 ไม่มีใครเห็นเลยถ้าไม่เอาขึ้นจอ - config_set() เงียบสนิท
COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD, COL_OK, COL_RUN, COL_BAD = 0x142240, 0x00E676, 0x4FC3F7, 0xFF5252
WAIT_CEILING_S = 30            # เพดานของลูปรอข้างล่าง ตัวเลขเดียวกันทั้งจอและโค้ด

ui.screen()
time.sleep_ms(200)
ui.Label("MQTTs - ทีม " + TEAM_NAME, x=16, y=6, color=COL_TEXT, value=20)

# ตารางตัวตน: สามบรรทัดนี้เคยเป็น print() ที่ไม่มีใครอ่าน ตอนนี้มันอยู่บนจอ
# ui.Table จัดสองคอลัมน์ให้เอง ค่าที่ยาวไม่เท่ากันจึงไม่ทำให้คอลัมน์เยื้อง
ui.Panel(x=16, y=34, w=440, h=354, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ตัวตนที่บอร์ดใช้แนะนำตัว", x=30, y=40, color=COL_DIM, value=14)
cfg0 = tesaiot.config()
tbl_id = ui.Table(x=30, y=62, w=412, h=286, cols=2)
tbl_id.col_width(0, 140)
tbl_id.col_width(1, 260)
tbl_id.add_row("คีย์", "ค่าที่ตั้งไว้")
tbl_id.add_row("device_id", DEVICE_ID)
tbl_id.add_row("broker", BROKER)
tbl_id.add_row("tls_mode", str(cfg0["tls_mode"]))
# ค่าที่อ่านกลับไม่ได้ ต้องเขียนว่า "ตั้งแล้วแต่อ่านไม่ได้" ไม่ใช่ปล่อยว่าง
# ช่องว่างบนหน้าจอแปลว่า "ยังไม่ได้ตั้ง" ซึ่งเป็นคนละเรื่องกับ "ตั้งแล้วแต่ดูไม่ได้"
ui.Label("mqtt_pass ตั้งแล้ว แต่อ่านกลับไม่ได้", x=30, y=356, color=COL_DIM, value=14)

# การ์ดขวาบน: ไฟสามดวงของการจับมือ ติดทีละดวงเสมอ
ui.Panel(x=470, y=34, w=306, h=132, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("การจับมือ TLS", x=484, y=40, color=COL_DIM, value=14)
led_wait = ui.Led(x=488, y=64, w=28, h=28, color=COL_RUN, value=1)
ui.Label("กำลังต่อ", x=484, y=98, color=COL_DIM, value=14)
led_ok = ui.Led(x=588, y=64, w=28, h=28, color=COL_OK, value=0)
ui.Label("สำเร็จ", x=588, y=98, color=COL_DIM, value=14)
led_fail = ui.Led(x=688, y=64, w=28, h=28, color=COL_BAD, value=0)
ui.Label("ไม่สำเร็จ", x=678, y=98, color=COL_DIM, value=14)

# การ์ดขวากลาง: เวลาจับมือ เทียบกับเพดานที่โค้ดใช้จริง ไม่ใช่เพดานที่เดาเอา
ui.Panel(x=470, y=174, w=306, h=126, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("เวลาจับมือ เทียบเพดาน 30 วิ", x=484, y=180, color=COL_DIM, value=14)
lbl_hs = ui.Label("0 วิ", x=484, y=202, color=COL_TEXT, value=20)
bar_hs = ui.Bar(x=484, y=236, w=200, h=12, color=COL_RUN,
                min=0, max=WAIT_CEILING_S, value=0)
sc_hs = ui.Scale(x=484, y=250, w=200, h=44, color=COL_TEXT,
                 min=0, max=WAIT_CEILING_S)
sc_hs.ticks(16, 5)

# การ์ดขวาล่าง: ตัวนับใบที่ส่ง กับปุ่มสั่งงานสองปุ่มแยกกัน
ui.Panel(x=470, y=308, w=306, h=80, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ส่งแล้ว (ใบ)", x=484, y=312, color=COL_DIM, value=14)
seg_sent = ui.Seg7("0", x=484, y=332, w=110, h=40, color=COL_TEXT)
btn_conn = ui.Button("ต่อใหม่", x=606, y=312, w=80, h=32, color=0x1B5E20, value=14)
btn_disc = ui.Button("ตัดสาย", x=606, y=350, w=80, h=32, color=0x37474F, value=14)

# กล่องยืนยัน: ตัดสายคือคำสั่งที่ถอยกลับไม่ได้ทันที ต้องจับมือ TLS ใหม่ทั้งชุด
# คำยืนยันจึงบอกสิ่งที่จะเกิด ไม่ใช่ถามลอย ๆ ว่า "แน่ใจไหม"
# ปุ่มในตัว MsgBox เองยังไม่ส่งเหตุการณ์กลับมาให้ Python เห็น จึงใช้ ui.Button จริง
box = ui.MsgBox("ตัดสาย\nต้องจับมือ TLS ใหม่ทั้งชุด", x=30, y=120, w=412, h=104,
                color=COL_CARD)
btn_yes = ui.Button("ตัดสาย", x=60, y=246, w=150, h=46, color=0x37474F, value=16)
btn_no = ui.Button("ไม่ตัด", x=240, y=246, w=150, h=46, color=0x37474F, value=16)
box.hide()
btn_yes.hide()
btn_no.hide()
ui.poll()

# --- ท่าที่ 3: สั่งต่อ แล้ววนรอจนกว่าจะต่อเสร็จจริง ---
# connect() เป็น API แบบ asynchronous - มันคืนค่าทันทีเพื่อไม่ให้บล็อกโปรแกรมทั้งตัว
# ค่าที่คืนมาไม่ใช่สถานะสุดท้าย ตัวจริงที่ตอบได้คือ is_connected() เท่านั้น
tesaiot.connect()
t0 = time.ticks_ms()

# ลูปรอทุกลูปต้องมี timeout ไม่งั้นวันที่เน็ตล่ม โปรแกรมจะค้างตรงนี้ตลอดกาล
# TLS ใช้เวลาหลายรอบไป-กลับ (TCP + handshake + ตรวจใบรับรอง) นับเป็นวินาที ไม่ใช่มิลลิวินาที
# ตัวเลขจริงของสองจังหวะนี้ examples/s11/05_wait_for_connected.py วัดไว้แล้วและวางไว้ข้างกัน
# ซ้ายคือ ms ที่ connect() ใช้คืนค่า ขวาคือ ms จนกว่าจะต่อสำเร็จจริง
# ความต่างของสองเลขนั้นคือเหตุผลทั้งหมดที่ลูปห้าบรรทัดนี้ต้องมีอยู่
last_s = -1
while not tesaiot.is_connected():
    waited = time.ticks_diff(time.ticks_ms(), t0)
    if waited > WAIT_CEILING_S * 1000:
        print("ต่อแพลตฟอร์มไม่สำเร็จใน 30 วินาที")
        break
    # แถบเดินขึ้นทุกครึ่งวินาที แต่ตัวเลขเขียนใหม่แค่ตอนวินาทีเปลี่ยน
    # คนที่ยืนรออยู่หน้าจอ ต้องเห็นว่าโปรแกรมยังทำงาน ไม่ใช่เห็นจอนิ่งแล้วเดาว่าแฮงก์
    if waited // 1000 != last_s:
        last_s = waited // 1000
        bar_hs.value(last_s)
        lbl_hs.text(str(last_s) + " วิ")
    ui.poll()
    time.sleep_ms(500)

# ไฟดวงที่ดับจะหรี่ ไม่ใช่หายไป คนดูจึงยังเห็นว่ามีสถานะนั้นอยู่ในระบบ
led_wait.value(0)
if tesaiot.is_connected():
    lcd.console("<span class=ok>เชื่อมต่อ TLS สำเร็จ</span>")
    led_ok.value(1)
    led_fail.value(0)
else:
    lcd.console("<span class=muted>ต่อไม่สำเร็จใน 30 วินาที</span>")
    led_ok.value(0)
    led_fail.value(1)
ui.poll()

sent = 0
asking = False
payload = {"data": {}}

# เงื่อนไขของ while คือการเช็กสายก่อนทุกรอบส่ง ไม่ใช่เช็กครั้งเดียวตอนเริ่ม
# examples/s11/06_secure_publish_loop.py วางกฎข้อนี้ไว้เป็นหัวเรื่องของไฟล์เลย เพราะสายหลุด
# ระหว่างลูปได้เสมอ และโปรแกรมที่ส่งต่อไปโดยไม่เช็ก จะทำข้อมูลหายทั้งชั่วโมงอย่างเงียบสนิท
while tesaiot.is_connected():
    # --- ท่าที่ 3: ส่งค่าเซนเซอร์ขึ้นแพลตฟอร์มทุก 5 วินาที ---
    # ห่อทุกอย่างไว้ใน "data" เพราะแพลตฟอร์มแยกส่วนข้อมูลออกจากส่วนหัวด้วยคีย์นี้
    # และส่งเป็นตัวเลขจริง ไม่ใช่สตริง "25.5" ไม่งั้น dashboard จะขึ้นค่าแต่วาดกราฟไม่ได้
    #
    # สามค่านี้มาจากการถามฮาร์ดแวร์สามรอบ (IMU กับลูกบิดผ่านคอร์จอ ส่วนเข็มทิศ
    # อ่านตรงบนบัส I3C ของมันเอง) พลาดรอบเดียวไม่ควรทำให้หลุดการเชื่อมต่อไปด้วย
    try:
        m = sensors.bmi270.motion()             # (ax, ay, az, gx, gy, gz)
        payload = {"data": {"ax": round(m[0], 2),
                            "heading": round(sensors.bmm350.heading(), 1),
                            "pot": sensors.pot.percent()}}
    except OSError:
        time.sleep_ms(1000)
        continue

    # ไม่ต้องใส่ topic เฟิร์มแวร์ประกอบให้จาก device_id ที่เราตั้งไว้ในท่าที่ 1
    tesaiot.publish(json.dumps(payload))
    sent += 1

    # --- ท่าที่ 4: แสดงหลักฐานบนจอ ให้ตอบคำถาม MVP ได้ ---
    # จอต้องตอบได้ว่า "ต่างจากคาบ 10 ตรงไหน" โดยไม่ต้องเปิดโค้ดดู
    # tls_mode คือคำตอบ: มันเป็นตัวกำหนดพอร์ต 8884 ไม่ใช่คีย์ port ที่ไม่มีอยู่จริง
    cfg = tesaiot.config()
    lcd.print("ส่งครั้งที่", sent, "| โหมด", cfg["tls_mode"], "-> 8884")
    seg_sent.text(str(sent))

    # --- ท่าที่ 5: ปุ่มบนจอ กับคำสั่งที่ถอยกลับไม่ได้ ---
    # ปุ่มถูกถามระหว่างรอบส่ง ไม่ใช่ถามถี่ ๆ ทุกมิลลิวินาที นิ้วคนไม่ได้เร็วขนาดนั้น
    for ev in ui.poll():
        if ev["type"] != "clicked":
            continue
        if ev["handle"] == btn_disc.id() and not asking:
            asking = True
            box.show()
            btn_yes.show()
            btn_no.show()
        elif ev["handle"] == btn_yes.id() and asking:
            asking = False
            box.hide()
            btn_yes.hide()
            btn_no.hide()
            tesaiot.disconnect()
        elif ev["handle"] == btn_no.id() and asking:
            asking = False
            box.hide()
            btn_yes.hide()
            btn_no.hide()
        elif ev["handle"] == btn_conn.id():
            tesaiot.connect()

    time.sleep_ms(5000)

# ออกจากลูปแปลว่าสายหลุด จอต้องบอกด้วย ไม่ใช่ค้างไฟเขียวไว้ให้คนเข้าใจผิด
led_ok.value(0)
led_fail.value(1)
ui.poll()
lcd.print("<span class=muted>หลุดการเชื่อมต่อ ส่งไป " + str(sent) + " ครั้ง</span>")
