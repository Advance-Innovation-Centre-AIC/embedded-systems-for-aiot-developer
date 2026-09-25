# 05_value_leaves_the_board.py - ค่าที่วัดได้บนโต๊ะนี้ ไปโผล่บนเครื่องคนอื่น
#
# ไฟล์นี้ใช้ของใหม่จากโมดูล mqtt สามชิ้น คือ connect แนะนำตัวกับ broker
# publish ส่งข้อความออกไปหนึ่งใบ และ is_connected ถามว่าสายยังอยู่ไหม
# ค่าที่ส่งมาจาก sensors.snapshot() ซึ่งเจอแล้วในไฟล์ 12 ของคาบ 1
#
# ไฟล์นี้สอน: บันไดสามขั้นที่ห้ามสลับ WiFi ต้องให้ IP ก่อน TCP จึงต่อได้
#             แล้ว MQTT จึงแนะนำตัวได้ ขั้นที่ยังไม่ผ่านต้องเห็นบนจอว่ายังไม่ผ่าน
# ดูที่จอ   : ป้ายสามขั้นไล่เปลี่ยนจากเทาเป็นเขียวตามลำดับ ถ้าขั้นไหนไม่ผ่าน
#             มันจะเป็นแดงพร้อมบอกเหตุผล แล้วโปรแกรมจบตรงนั้น ไม่ค้างรอ
#             ผ่านครบแล้วเลขใบที่ส่งจะเดินขึ้น พร้อมค่าลูกบิดและค่าเอียงที่ส่งออกไปจริง
# ดูอีกฝั่ง : เปิด examples/web/my_first_reader.html ในเบราว์เซอร์ แก้ TEAM ให้ตรงกับทีม
#             แล้วดูเลขของทีมเราขึ้นบนหน้าเว็บ ไม่ต้องลงโปรแกรมอะไรเพิ่มในเครื่อง
# กับดัก    : ชื่ออาร์กิวเมนต์คือ username= ไม่ใช่ user= ใส่ผิดได้ TypeError ทันที
#             และ publish() ตอนสายหลุดไม่ได้คืน False เฉย ๆ มันโยน OSError ออกมา
#             ต้องดักทั้งสองทาง ไม่ใช่เช็กแค่ค่าที่คืนกลับ
#
# วันนี้เราใช้ broker สาธารณะ broker.hivemq.com ที่พอร์ต 1883 ซึ่งไม่เข้ารหัส
# ใครในโลกก็ subscribe หัวข้อของเราได้ และใครก็ publish เข้ามาได้ ไม่ต้องมีรหัสผ่าน
# คาบ 11 เราจะย้ายไปทางที่เข้ารหัส ตอนนี้ยังไม่ใช่ ห้ามส่งของจริงที่เป็นความลับ
#
# สถานะการทดสอบ (2026-09-24): วัดเวลาไปกลับของ broker ทั้งสองตัวจาก Mac บนโต๊ะแล้ว
# แต่ยังไม่มีใครรันไฟล์นี้บนบอร์ดกับ broker.hivemq.com และยังไม่ได้ลองจากเน็ตของมหาวิทยาลัย

import json
import lcd
import sensors
import time
import ui
import wifi
import mqtt

# แก้สามบรรทัดนี้ให้ตรงกับที่ผู้สอนแจกหน้าห้อง WIFI_SSID WIFI_PASS และ TEAM
WIFI_SSID = "bento-teamXX"            # ชื่อ Hotspot มือถือของทีม (WiFi คณะต้อง login บอร์ดใช้ไม่ได้)
WIFI_PASS = "<รหัส Hotspot ของทีม>"     # อย่างน้อย 8 ตัว
TEAM = "teamXX"                   # ผู้สอนแจก team01 ถึง team19 ต้องแก้ ไม่งั้นโปรแกรมไม่ยอมรัน

# สี่บรรทัดนี้ไม่ต้องแก้ ทั้งห้องใช้ชื่อชุดเดียวกัน
BROKER = "broker.hivemq.com"      # สำรอง: "test.mosquitto.org" ถ้าผู้สอนประกาศให้เปลี่ยน
ROOT = "bento-aiot"               # ชื่อนำหน้าของทั้งห้อง
DEVICE_ID = "bento-aiot-" + TEAM  # client_id ต้องไม่ซ้ำกับใครบน broker ทั้งโลก
TOPIC = ROOT + "/" + TEAM + "/telemetry"

# เฟิร์มแวร์ส่งแบบ retain ไม่ได้ (modmqtt.c ตั้ง retain เป็น false ตายตัว)
# broker จึงไม่เก็บใบล่าสุดไว้ให้ใคร หน้าเว็บที่เปิดช้ากว่าบอร์ดจะเห็นแค่ใบถัดไป
# ไฟล์นี้จึงส่งซ้ำทุก 2 วินาทีนานสองนาที ให้ทั้งห้องมีเวลาเปิดหน้าเว็บทัน
N = 60               # ส่งกี่ใบแล้วหยุด
GAP_MS = 2000        # เว้นระหว่างใบกี่ ms

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

ui.screen()
time.sleep_ms(200)

# ผังจอ: หัวเรื่องกับคำเตือนเรื่องพอร์ตอยู่แถวบนสุด แล้วสองการ์ดเรียงลงมา
# คำเตือนย้ายขึ้นมาบนสุดเพราะมันต้องอ่านก่อนกดรัน ไม่ใช่หลังจากส่งของออกไปแล้ว
ui.Label("ส่งค่าออกจากบอร์ด", x=24, y=8, color=COL_TEXT, value=24)
ui.Label("พอร์ต 1883 ไม่เข้ารหัส ห้ามส่งของลับ", x=384, y=16,
         color=COL_WARN, value=20)

# บันไดสามขั้น ห่างกันขั้นละ 40 เพราะตัวอักษร 24 สูงราว 32 px รวมสระบนล่าง
# ข้อความยาวสุดคือ "2) broker    ต่อแล้ว broker.hivemq.com" ราว 390 px ยังไม่ถึงขอบการ์ด
ui.Panel(x=24, y=56, w=744, h=144, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
st_wifi = ui.Label("1) WiFi      ยังไม่ถึงคิว", x=40, y=72, color=COL_DIM,
                   value=20)
st_broker = ui.Label("2) broker    ยังไม่ถึงคิว", x=40, y=112, color=COL_DIM,
                     value=20)
st_pub = ui.Label("3) publish   ยังไม่ถึงคิว", x=40, y=152, color=COL_DIM,
                  value=16)

ui.Panel(x=24, y=216, w=744, h=120, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
ui.Label("ส่งไปแล้ว (ใบ)", x=40, y=232, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=40, y=264, w=160, h=56, color=COL_ACCENT)

ui.Label("ความคืบหน้า", x=248, y=232, color=COL_DIM, value=20)
bar = ui.Bar(x=248, y=264, w=328, h=24, min=0, max=N, value=0)
bar.color(COL_ACCENT)
payload_lbl = ui.Label("ยังไม่ได้ประกอบ payload", x=248, y=296, color=COL_DIM,
                       value=20)

note = ui.Label("กำลังเริ่ม", x=24, y=352, color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>ส่งค่าออกจากบอร์ด</h2>")


def stop_here(label, screen_msg, log_msg):
    """ปิดงานอย่างสุภาพ บอกบนจอว่าไปไม่ถึงไหน แล้วจบ ไม่ค้างรอ

    โปรแกรมที่ล้มเหลวแล้วเงียบ คือโปรแกรมที่คนหน้างานต้องเดาเอง
    ทุกทางออกของไฟล์นี้จึงเขียนบนจอไว้เสมอว่าติดที่ขั้นไหน
    """
    label.color(COL_BAD)
    label.text(screen_msg)
    note.color(COL_BAD)
    note.text(log_msg)
    ui.poll()
    lcd.print("<span class=error>" + log_msg + "</span>")
    print("หยุดที่:", log_msg)
    raise SystemExit


# client_id สร้างจาก TEAM ถ้าใช้ชื่อทีมคนอื่น broker จะเตะบอร์ดของทีมนั้นหลุด
# และเฟิร์มแวร์ไม่ต่อใหม่เอง จึงไม่ยอมรันจนกว่าจะแก้ teamXX เป็นเลขทีมจริง
if len(TEAM) != 6 or TEAM[:4] != "team" or not TEAM[4:].isdigit() or TEAM == "team00":
    stop_here(st_wifi, "1) WiFi      ยังไม่ได้ตั้งชื่อทีม",
              "แก้ TEAM เป็นเลขทีมของคุณก่อน เช่น team03")

# --- ขั้นที่ 1: WiFi ต้องได้ IP ก่อน ---
# ป้าย "กำลังต่อ" ต้องขึ้นก่อนบรรทัด connect() เพราะระหว่างต่อจอไม่ขยับเลย
# และ connect() บล็อกได้นานถึงราว 85 วินาที ถ้าวงนั้นไม่มีอยู่จริงในห้อง
st_wifi.color(COL_WARN)
st_wifi.text("1) WiFi      กำลังต่อ " + WIFI_SSID)
note.color(COL_WARN)
note.text("ครั้งแรกอาจรอนาน จอจะนิ่ง อย่ากดรีเซ็ต")
ui.poll()
lcd.print("1) กำลังต่อ WiFi", WIFI_SSID)

t0 = time.ticks_ms()
ok = wifi.connect(WIFI_SSID, WIFI_PASS)
took = time.ticks_diff(time.ticks_ms(), t0)
lcd.print("connect() ใช้เวลา", took, "ms คืนค่า", ok)

if not ok:
    # ไปต่อไม่ได้จริง ๆ แต่ยังบอกได้ว่าทำไม ใช้ scan() ตอบว่าบอร์ดได้ยินวงนี้ไหม
    # "ไม่ได้ยินเลย" กับ "ได้ยินแต่รหัสผิด" เป็นคนละปัญหาและแก้คนละทาง
    heard = False
    try:
        for net in wifi.scan():
            if net[0] == WIFI_SSID:
                heard = True
    except OSError:
        # scan() เองก็ล้มได้ ไม่ใช่เหตุให้โปรแกรมตายก่อนบอกเหตุผลบนจอ
        pass
    if heard:
        why = "ได้ยินวง " + WIFI_SSID + " แต่ต่อไม่ผ่าน ตรวจรหัสผ่าน"
    else:
        why = "ไม่ได้ยินวง " + WIFI_SSID + " เลย ตรวจชื่อวงหรือย้ายที่"
    stop_here(st_wifi, "1) WiFi      ต่อไม่ติด", why)

ip = wifi.ip()
if ip == "0.0.0.0":
    # ลิงก์ขึ้นแล้วแต่ยังไม่ได้เลข IP คือยังส่งอะไรออกไม่ได้ รอ DHCP อีกหน่อย
    # "0.0.0.0" เป็นสตริงที่ไม่ว่าง เขียน if wifi.ip(): จึงผ่านทั้งที่ยังไม่มีที่อยู่
    for _ in range(15):
        ui.poll()
        time.sleep_ms(200)
        ip = wifi.ip()
        if ip != "0.0.0.0":
            break

if ip == "0.0.0.0":
    stop_here(st_wifi, "1) WiFi      ลิงก์ขึ้นแต่ไม่มี IP",
              "ลิงก์ขึ้นแต่ DHCP ไม่ให้เลข ส่งอะไรออกไม่ได้")

st_wifi.color(COL_OK)
st_wifi.text("1) WiFi      IP " + ip)
ui.poll()
lcd.print("<span class=ok>ได้ IP", ip, "</span>")

# --- ขั้นที่ 2: แนะนำตัวกับ broker ---
# client_id ต้องไม่ซ้ำกับใครบน broker เดียวกัน ถ้าซ้ำ broker จะเตะตัวเก่าออก
# แล้วสองบอร์ดจะผลัดกันเตะกันไปมาทั้งคาบ โดยฝั่งเราไม่มีข้อความเตือนอะไรเลย
# บน broker สาธารณะ "ใคร" แปลว่าทุกคนบนอินเทอร์เน็ต ไม่ใช่แค่เพื่อนในห้อง
# ชื่อ team03 เปล่า ๆ ชนกับคนแปลกหน้าได้ง่าย จึงเติม bento-aiot- ไว้ข้างหน้า
# ชื่อนี้ยาว 17 ตัวอักษร เฟิร์มแวร์ตัด client_id ที่ 31 ตัวเงียบ ๆ ยังเหลือที่
st_broker.color(COL_WARN)
st_broker.text("2) broker    กำลังต่อ " + BROKER)
note.text("ถ้าเน็ตของห้องกันพอร์ต 1883 ขั้นนี้จะไม่ผ่าน")
ui.poll()
lcd.print("2) กำลังต่อ broker", BROKER, "พอร์ต 1883")

# broker เป็นอาร์กิวเมนต์เดียวที่บังคับ ที่เหลือมีค่าตั้งต้นให้แล้ว
# ชื่อคือ username= ไม่ใช่ user= และ keepalive=60 แปลว่าเงียบเกิน 60 วินาที
# เมื่อไร broker มีสิทธิ์ตัดเราทิ้งได้เลย
try:
    linked = mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID, keepalive=60)
except OSError:
    # connect() โยน OSError เฉพาะตอนชั้น WiFi ของบอร์ดเองยังไม่พร้อม
    stop_here(st_broker, "2) broker    ต่อไม่ได้",
              "ชั้น WiFi ของบอร์ดไม่พร้อม ลองรันใหม่")

if not linked:
    # ทางที่เจอบ่อยที่สุดมาจบตรงนี้ ไม่ใช่ที่ except ข้างบน: เน็ตของห้องกันพอร์ต 1883
    # แปลงชื่อ broker เป็น IP ไม่ได้ หรือ broker ล่ม connect() คืน False ทั้งหมด
    # ไม่ใช่ความผิดของโค้ดทีม
    stop_here(st_broker, "2) broker    ต่อไม่ได้",
              BROKER + " ไม่ตอบ: เน็ตกันพอร์ต 1883 หรือชื่อผิด")

st_broker.color(COL_OK)
st_broker.text("2) broker    ต่อแล้ว " + BROKER)
st_pub.color(COL_WARN)
st_pub.text("3) publish   กำลังส่ง")
note.color(COL_DIM)
note.text("หัวข้อ " + TOPIC)
ui.poll()
lcd.print("<span class=ok>ต่อ broker แล้ว</span>")
lcd.print("หัวข้อที่ส่ง:", TOPIC)

# --- ขั้นที่ 3: ส่งของจริง ---
# ค่าที่ส่งคือค่าจากบอร์ดจริง ไม่ใช่ตัวเลขสุ่ม คนที่นั่งดูอีกฝั่งจะได้
# พิสูจน์ได้ด้วยมือตัวเองว่าหมุนลูกบิดหรือเอียงบอร์ดที่นี่ แล้วเลขที่โน่นขยับตาม
# ส่งสองค่า ลูกบิดกับค่าเอียง ทั้ง Eva Kit และ Dev Kit มีทั้งคู่ ส่วน in ที่ถามก่อนหยิบ
# กันรอบที่ snapshot ไม่มีคีย์นั้นมาด้วย ไม่ให้ทั้งลูปตายเพราะ KeyError
# แบบเดียวกับ examples/s01/12_every_sense_at_once.py
sent = 0
for i in range(1, N + 1):
    knob = -1        # -1 แปลว่ารอบนี้อ่านลูกบิดไม่ได้
    az = -99.0       # -99 แปลว่ารอบนี้อ่านค่าเอียงไม่ได้ ค่าจริงอยู่ราว -10 ถึง 10
    try:
        s = sensors.snapshot()
        if "pot" in s:
            knob = int(s["pot"]["percent"])
        if "bmi270" in s:
            # ค่าเร่งแกน z หน่วย m/s^2 วางราบราว 9.8 ตะแคงแล้วลดลง
            az = round(s["bmi270"]["az"], 2)
    except OSError:
        # อ่านเซนเซอร์ไม่ได้รอบนี้ ไม่ใช่เหตุให้หยุดส่ง ส่งค่าบอกว่าอ่านไม่ได้ไปแทน
        pass

    # คีย์สั้นตัวเล็กทั้งหมด และมี id กับ n เสมอ หน้าเว็บของทั้งห้องอ่านชุดนี้
    payload = {"id": TEAM,
               "n": i,
               "knob": knob,
               "az": az,
               "uptime_s": time.ticks_ms() // 1000}
    body = json.dumps(payload)

    # publish() คืน True เมื่อส่งต่อให้ชั้นเครือข่ายสำเร็จ แต่ถ้าสายหลุดไปแล้ว
    # มันไม่คืน False มันโยน OSError ออกมา จึงต้องดักทั้งสองทาง
    try:
        ok = mqtt.publish(TOPIC, body)
    except OSError:
        st_pub.color(COL_BAD)
        st_pub.text("3) publish   สายหลุดที่ใบที่ " + str(i))
        note.color(COL_BAD)
        note.text("ส่งไปได้ " + str(sent) + " ใบก่อนสายหลุด")
        ui.poll()
        lcd.print("<span class=error>สายหลุดที่ใบที่", i, "</span>")
        break

    if not ok:
        st_pub.color(COL_BAD)
        st_pub.text("3) publish   ถูกปฏิเสธที่ใบที่ " + str(i))
        ui.poll()
        lcd.print("<span class=error>ใบที่", i, "ถูกปฏิเสธ</span>")
        break

    sent = i
    seg.text(str(sent))            # Seg7 รับข้อความ ไม่ใช่ตัวเลข
    bar.value(sent)
    payload_lbl.color(COL_TEXT)
    payload_lbl.text("ใบที่ " + str(i) + " knob=" + str(knob) + " az=" + str(az))
    ui.poll()
    lcd.print("ใบที่", i, "->", body)
    print("ส่ง:", body)

    time.sleep_ms(GAP_MS)

# --- สรุป ---
# is_connected() ตอบว่า "ตอนนี้ยังต่ออยู่ไหม" ซึ่งเป็นคนละคำถามกับค่าที่
# connect() คืนมาตอนต้น อันนั้นตอบว่า "ตอนนั้นต่อสำเร็จ" คนละเวลากัน
still = mqtt.is_connected()
if sent == N:
    st_pub.color(COL_OK)
    st_pub.text("3) publish   ส่งครบ " + str(N) + " ใบ")
note.color(COL_OK if still else COL_WARN)
note.text("ส่งได้ " + str(sent) + " ใบ | is_connected() = " + str(still))
ui.poll()

lcd.console("<span class=muted>------------------------</span>")
lcd.print("<span class=ok>ส่งได้", sent, "ใบ จาก", N, "</span>")
print("ส่งได้", sent, "ใบ | ยังต่ออยู่:", still)

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# เปิดหน้าเว็บอ่านค่าจากเว็บหลักสูตร ต่อท้ายลิงก์ด้วยเลขทีมเดียวกับบรรทัดบน เช่น
# https://advance-innovation-centre-aic.github.io/embedded-systems-for-aiot-developer/examples/web/my_first_reader.html?team=team05
# ระหว่างที่ไฟล์นี้กำลังส่ง หมุนลูกบิดหรือเอียงบอร์ด
# ดูว่าเลข knob หรือ az บนหน้าเว็บขยับตามมือคุณจริงไหม แล้วจดว่าเห็นใบแรกที่ n เท่าไร
# จากนั้นตกลงกับทีมข้าง ๆ ให้ตั้ง TEAM ชนกันชั่วคราว แล้วรันพร้อมกันสองบอร์ด
# ใบ้: broker ยอมให้ client_id ซ้ำกันไม่ได้ ดูว่าใครถูกเตะออก และฝั่งเราเห็นอะไรบ้าง
