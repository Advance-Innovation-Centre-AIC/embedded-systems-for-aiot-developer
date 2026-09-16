# s11_secure_telemetry.py - ส่ง telemetry ขึ้นแพลตฟอร์มผ่าน TLS (ฉบับฝึกเติมโค้ด)
# วิธีรัน: 1) รับ device_id - api_key - รหัส MQTT ที่อาจารย์ provision ให้ทีมมาก่อน
#          2) ทำช่อง 1-2 แล้วรันดู print(tesaiot.config()) ว่าค่าเข้าครบ แล้วค่อยทำช่อง 3
#          3) กด Program to Device แล้วเปิด dashboard ของแพลตฟอร์มดูค่าไหลขึ้น
#
# ต่างจากคาบ 10 ตรงที่เราไม่ได้เลือกพอร์ตเอง - พอร์ต 8884 มาจากค่า tls_mode
# ไม่ใช่จากคีย์ port และ tesaiot.connect() คืนค่าทันทีโดยยังไม่ได้ต่อเสร็จ
#
# คาบนี้มีตัวอย่างสามไฟล์ที่ examples/s11/ เรียงตามลำดับที่เราจะเจอปัญหาพอดี
# 01 คือคลังค่าตั้ง 05 คือการรอให้ต่อเสร็จจริง 06 คือลูปส่งที่เช็กสายก่อนส่งทุกครั้ง
#
# ดูที่จอ: ซ้ายคือตารางตัวตนที่บอร์ดใช้แนะนำตัว ขวาบนคือไฟสามดวงของการจับมือ TLS
#         ขวากลางคือปุ่มต่อ/ตัดสาย ขวาล่างคือเวลาที่ใช้จับมือ เทียบเพดาน 30 วินาที
#         หน้าจอเขียนมาให้ครบแล้ว ช่องว่างห้าจุดอยู่ที่ตรรกะ ไม่ได้อยู่ที่การวาด
# กับดัก : ถ้ายังไม่เติมช่องที่ 1-2 ตารางจะขึ้นค่าเปล่า และนั่นคือคำตอบว่าเติมครบหรือยัง

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
try:
    sensors.bmi270.motion()
except OSError:
    print("คอร์จอยังไม่ตอบรอบแรก จะลองใหม่ตอนส่ง")

lcd.clear()
lcd.console("<h2>MQTTs - ทีม " + TEAM_NAME + "</h2>")

# --- ท่าที่ 1: ตั้งค่าตัวตนของอุปกรณ์ ---
# config_set() รับทีละคู่ (key, value) และเก็บไว้ในเฟิร์มแวร์ ไม่ได้ต่ออะไรทั้งสิ้น
# เติม: tesaiot.config_set("device_id", DEVICE_ID) แล้วอีกสองบรรทัดสำหรับ api_key และ mqtt_pass
# ตั้งค่าแล้วอยากตรวจว่าเข้าจริงไหม: examples/s11/01_config_store.py เดินดูคลังค่าตั้งทั้ง 19 คีย์
# และเตือนข้อที่จะทำให้เสียเวลาเปล่า - mqtt_pass ตั้งได้ แต่อ่านกลับไม่ได้
# เขียน config()["mqtt_pass"] บนบอร์ดจริงจะได้ KeyError ไม่ใช่ค่าว่าง
pass

# sni_hostname คือชื่อที่เดินไปก่อนการเข้ารหัส ต้องเป็นชื่อโฮสต์เดียวกับ broker
# ถ้าตั้งไม่ตรง เซิร์ฟเวอร์จะยื่นใบรับรองผิดใบ แล้วการต่อจะล้มเงียบ ๆ โดยไม่มีข้อความ
# เติม: tesaiot.config_set("broker", BROKER) แล้วบรรทัดถัดไป tesaiot.config_set("sni_hostname", BROKER)
pass

print("config ปัจจุบัน:", tesaiot.config())

# --- ท่าที่ 2: กางหน้าจอ แล้วเอาตัวตนที่ตั้งไว้ขึ้นให้เห็น ---
# หน้าจอนี้ตอบคำถามเดียวที่คาบนี้ถาม: "ตกลงบอร์ดแนะนำตัวว่าเป็นใคร และต่อแบบไหน"
# ค่าที่ตั้งไว้ในท่าที่ 1 ไม่มีใครเห็นเลยถ้าไม่เอาขึ้นจอ - config_set() เงียบสนิท
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD, COL_OK, COL_RUN, COL_BAD = 0x171B22, 0x30A46C, 0x4A9EFF, 0xE5484D
WAIT_CEILING_S = 30            # เพดานของลูปรอข้างล่าง ตัวเลขเดียวกันทั้งจอและโค้ด

# จอวาดได้จริง 792 x 398 หน้านี้แบ่งเป็นแถบหัวเรื่องสูง 40 แล้วสองคอลัมน์
# ซ้ายคือตารางตัวตน ขวาคือการจับมือ TLS ปุ่มสั่งงาน และเวลาที่ใช้จับมือ
ui.screen()
time.sleep_ms(200)
ui.Label("MQTTs - ทีม " + TEAM_NAME, x=24, y=8, color=COL_TEXT, value=20)
lbl_sent = ui.Label("ส่งแล้ว 0 ใบ", x=560, y=8, color=COL_DIM, value=20)

# ตารางตัวตน: สี่บรรทัดนี้เคยเป็น print() ที่ไม่มีใครอ่าน ตอนนี้มันอยู่บนจอ
# ui.Table จัดสองคอลัมน์ให้เอง ค่าที่ยาวไม่เท่ากันจึงไม่ทำให้คอลัมน์เยื้อง
# การ์ดต้องถูกสร้างก่อนของที่วางบนมัน LVGL วาดตามลำดับการสร้าง การ์ดที่มาทีหลัง
# จะทาทับของที่สร้างไว้ก่อนจนหายไปทั้งใบ โดยไม่มี error สักบรรทัด
ui.Panel(x=24, y=48, w=440, h=344, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ตัวตนที่บอร์ดใช้แนะนำตัว", x=40, y=60, color=COL_DIM, value=16)
cfg0 = tesaiot.config()
# สี่แถวคูณ 72 พิกเซล = 288 พอดีกับความสูงที่ขอไว้ ขอมากกว่านี้แถวล่างสุดจะถูกตัด
# คอลัมน์ซ้ายต้องกว้างพอสำหรับ "mqtt_pass" ทั้งคำ ถ้าถูกตัดบรรทัด แถวนั้นสูงสองเท่าทันที
tbl_id = ui.Table(x=40, y=96, w=408, h=288, cols=2)
tbl_id.col_width(0, 160)
tbl_id.col_width(1, 224)
tbl_id.add_row("device_id", DEVICE_ID)
tbl_id.add_row("broker", BROKER)
tbl_id.add_row("tls_mode", str(cfg0["tls_mode"]))
# ค่าที่อ่านกลับไม่ได้ ต้องเขียนว่า "ตั้งแล้วแต่ดูไม่ได้" ไม่ใช่ปล่อยว่าง
# ช่องว่างบนหน้าจอแปลว่า "ยังไม่ได้ตั้ง" ซึ่งเป็นคนละเรื่องกับ "ตั้งแล้วแต่ดูไม่ได้"
tbl_id.add_row("mqtt_pass", "ตั้งแล้ว ดูไม่ได้")

# การ์ดขวา: ไฟสามดวงของการจับมือ ติดทีละดวงเสมอ แล้วปุ่มสั่งงาน แล้วเวลาที่ใช้
ui.Panel(x=480, y=48, w=288, h=344, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("การจับมือ TLS", x=496, y=60, color=COL_DIM, value=16)
led_wait = ui.Led(x=496, y=96, w=48, h=48, color=COL_RUN, value=1)
led_ok = ui.Led(x=592, y=96, w=48, h=48, color=COL_OK, value=0)
led_fail = ui.Led(x=688, y=96, w=48, h=48, color=COL_BAD, value=0)
# ป้ายอยู่ใต้ไฟของตัวเอง ไม่ใช่ข้าง ๆ เพราะการ์ดกว้าง 288 ไม่พอให้ไฟกับคำอยู่บรรทัดเดียว
ui.Label("กำลังต่อ", x=496, y=148, color=COL_DIM, value=16)
ui.Label("สำเร็จ", x=592, y=148, color=COL_DIM, value=16)
ui.Label("ล้มเหลว", x=672, y=148, color=COL_DIM, value=16)

# ปุ่มสั่งงานสองปุ่มแยกกัน วางไว้เหนือมาตรวัด เพราะปุ่มที่ยาวลงไปถึง y=340
# จะไปทับมุมขวาล่างที่เฟิร์มแวร์ถือไว้ให้ปุ่ม Console แล้วกดไม่โดน
btn_conn = ui.Button("ต่อใหม่", x=496, y=184, w=88, h=88, color=0x30A46C, value=20)
btn_disc = ui.Button("ตัดสาย", x=616, y=184, w=88, h=88, color=0x3A4150, value=20)

# เวลาจับมือ เทียบกับเพดานที่โค้ดใช้จริง ไม่ใช่เพดานที่เดาเอา
# ui.Scale คือไม้บรรทัด ไม่มีเข็มและไม่รับ .value() ตัวที่ขยับคือ ui.Bar ที่วางเหนือมัน
lbl_hs = ui.Label("0 วิ", x=496, y=284, color=COL_TEXT, value=20)
ui.Label("เพดาน 30 วิ", x=592, y=284, color=COL_DIM, value=16)
bar_hs = ui.Bar(x=496, y=320, w=192, h=12, color=COL_RUN,
                min=0, max=WAIT_CEILING_S, value=0)
sc_hs = ui.Scale(x=496, y=340, w=192, h=44, color=COL_TEXT,
                 min=0, max=WAIT_CEILING_S)
# ไม้บรรทัดยาว 192 มีที่พอสำหรับตัวเลขสามตัว คือ 0 15 30 มากกว่านั้นเลขจะทับกัน
sc_hs.ticks(7, 3)

# กล่องยืนยัน: ตัดสายคือคำสั่งที่ถอยกลับไม่ได้ทันที ต้องจับมือ TLS ใหม่ทั้งชุด
# คำยืนยันจึงบอกสิ่งที่จะเกิด ไม่ใช่ถามลอย ๆ ว่า "แน่ใจไหม"
# ปุ่มในตัว MsgBox เองยังไม่ส่งเหตุการณ์กลับมาให้ Python เห็น จึงใช้ ui.Button จริง
box = ui.MsgBox("ตัดสาย\nต้องจับมือ TLS ใหม่ทั้งชุด", x=112, y=96, w=568, h=136,
                color=COL_CARD)
btn_yes = ui.Button("ตัดสาย", x=144, y=248, w=200, h=88, color=0x3A4150, value=20)
btn_no = ui.Button("ไม่ตัด", x=376, y=248, w=200, h=88, color=0x3A4150, value=20)
box.hide()
btn_yes.hide()
btn_no.hide()
ui.poll()

# --- ท่าที่ 3: สั่งต่อ แล้ววนรอจนกว่าจะต่อเสร็จจริง ---
tesaiot.connect()                 # คืนค่าทันที ยังไม่ได้แปลว่าต่อแล้ว
t0 = time.ticks_ms()

# ลูปรอทุกลูปต้องมี timeout ไม่งั้นวันที่เน็ตล่ม โปรแกรมจะค้างตรงนี้ตลอดกาล
# เติม: while not tesaiot.is_connected(): ถ้าเกิน 30000 ms ให้พิมพ์เตือนแล้ว break ไม่งั้น time.sleep_ms(500)
# ทำไมต้องมีลูปรอ ทั้งที่บรรทัดบนเรียก connect() ไปแล้ว: examples/s11/05_wait_for_connected.py
# จับเวลาสองก้อนวางข้างกัน เวลาที่ connect() ใช้คืนค่า (แทบเป็นศูนย์) กับเวลาจนต่อสำเร็จจริง
# เห็นสองเลขนั้นแล้วจะเลิกเชื่อค่าที่ connect() คืนมาไปตลอด
# และมันย้ำกฎที่ใช้ได้ทุกที่ - ลูปที่รออะไรสักอย่าง ต้องมีทางออกด้วยเวลาเสมอ
pass

# ไฟดวงที่ดับจะหรี่ ไม่ใช่หายไป คนดูจึงยังเห็นว่ามีสถานะนั้นอยู่ในระบบ (เขียนมาให้แล้ว)
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
payload = {}

while tesaiot.is_connected():
    # --- ท่าที่ 3: ส่งค่าเซนเซอร์ขึ้นแพลตฟอร์มทุก 5 วินาที ---
    # สามค่าที่จะเติมข้างล่างมาจากการถามฮาร์ดแวร์สามรอบ พลาดรอบเดียว
    # ไม่ควรทำให้หลุดการเชื่อมต่อไปด้วย จึงห่อไว้ใน try ทั้งก้อน
    try:
        m = sensors.bmi270.motion()             # (ax, ay, az, gx, gy, gz)

        # ส่งเป็นตัวเลขจริง ไม่ใช่สตริง ไม่งั้น dashboard จะขึ้นค่าแต่วาดกราฟไม่ได้
        # เติม: payload = {"accel_x": round(m[0], 2), "heading": round(sensors.bmm350.heading(), 1), "pot": sensors.pot.percent()}
        # ส่งแบน ห้ามห่อใต้ {"data": ...} เพราะ bridge ห่อให้เองอยู่แล้ว ห่อซ้ำ
        # จะได้ชื่อวัด data_accel_x ซึ่งตารางหน่วยฝั่งเซิร์ฟเวอร์หาไม่เจอ
        # ส่งขึ้นแพลตฟอร์มไม่ต้องใส่ topic เอง เฟิร์มแวร์ประกอบให้จาก device_id
        #   examples/s11/06_secure_publish_loop.py คือลูปส่งเต็มรูปแบบของท่านี้ ใส่ topic เองเมื่อไร
        #   จะไม่ตรงกับที่แพลตฟอร์มรออยู่ แล้วค่าจะหายไปทั้งที่ฝั่งเราไม่มี error อะไรเลย
        pass
    except OSError:
        time.sleep_ms(1000)
        continue

    tesaiot.publish(json.dumps(payload))        # ไม่ต้องใส่ topic เฟิร์มแวร์ประกอบให้
    sent += 1

    # --- ท่าที่ 4: แสดงหลักฐานบนจอ ให้ตอบคำถาม MVP ได้ ---
    cfg = tesaiot.config()

    # เติม: lcd.print("ส่งครั้งที่", sent, "| โหมด", cfg["tls_mode"], "-> 8884")
    # ลูปนี้ยังขาดของอย่างหนึ่งที่ examples/s11/06_secure_publish_loop.py มี คือการเช็ก
    #   is_connected() ก่อน "ทุกครั้ง" ที่ส่ง ไม่ใช่เช็กแค่ตอนเข้าลูป เพราะสายหลุดกลางทางได้
    #   และโปรแกรมที่ส่งต่อไปเฉย ๆ จะทำข้อมูลหายทั้งชั่วโมงโดยไม่มีใครรู้ตัวสักคน
    pass

    lbl_sent.text("ส่งแล้ว " + str(sent) + " ใบ")

    # --- ท่าที่ 5: ปุ่มบนจอ กับคำสั่งที่ถอยกลับไม่ได้ (เขียนมาให้แล้ว) ---
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
