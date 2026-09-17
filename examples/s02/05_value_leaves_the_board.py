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
#             ผ่านครบแล้วเลขใบที่ส่งจะเดินขึ้น พร้อมค่าลูกบิดที่ส่งออกไปจริง
# กับดัก    : ชื่ออาร์กิวเมนต์คือ username= ไม่ใช่ user= ใส่ผิดได้ TypeError ทันที
#             และ publish() ตอนสายหลุดไม่ได้คืน False เฉย ๆ มันโยน OSError ออกมา
#             ต้องดักทั้งสองทาง ไม่ใช่เช็กแค่ค่าที่คืนกลับ
#
# บอร์ดนี้ต่อ broker ได้เฉพาะพอร์ต 1883 ซึ่งเป็นแบบไม่เข้ารหัส ใครดักกลางทางอ่านได้หมด
# พอเข้าคาบ 11 เราจะย้ายไปทางที่เข้ารหัส ตอนนี้ยังไม่ใช่ ห้ามส่งของจริงที่เป็นความลับ

import json
import lcd
import sensors
import time
import ui
import wifi
import mqtt

# แก้ห้าบรรทัดนี้ให้ตรงกับที่ผู้สอนแจกหน้าห้อง
WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
BROKER = "192.168.1.50"          # IP ของเครื่องที่รัน broker ในแลน ไม่ใช่ localhost
DEVICE_ID = "team03"
TOPIC = "bento/team03/telemetry"

N = 12               # ส่งกี่ใบแล้วหยุด
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
# ข้อความยาวสุดคือ "2) broker    ต่อแล้ว <IP>" ซึ่งกินราว 490 px ยังไม่ถึงขอบการ์ด
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
    for net in wifi.scan():
        if net[0] == WIFI_SSID:
            heard = True
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
              "ลิงก์ขึ้นแล้วแต่ DHCP ไม่ให้เลข ส่งอะไรออกไม่ได้")

st_wifi.color(COL_OK)
st_wifi.text("1) WiFi      IP " + ip)
ui.poll()
lcd.print("<span class=ok>ได้ IP", ip, "</span>")

# --- ขั้นที่ 2: แนะนำตัวกับ broker ---
# client_id ต้องไม่ซ้ำกับใครบน broker เดียวกัน ถ้าซ้ำ broker จะเตะตัวเก่าออก
# แล้วสองบอร์ดจะผลัดกันเตะกันไปมาทั้งคาบ โดยฝั่งเราไม่มีข้อความเตือนอะไรเลย
st_broker.color(COL_WARN)
st_broker.text("2) broker    กำลังต่อ " + BROKER)
note.text("ถ้าไม่มี broker ที่เลขนี้ ขั้นนี้จะไม่ผ่าน")
ui.poll()
lcd.print("2) กำลังต่อ broker", BROKER, "พอร์ต 1883")

# broker เป็นอาร์กิวเมนต์เดียวที่บังคับ ที่เหลือมีค่าตั้งต้นให้แล้ว
# ชื่อคือ username= ไม่ใช่ user= และ keepalive=60 แปลว่าเงียบเกิน 60 วินาที
# เมื่อไร broker มีสิทธิ์ตัดเราทิ้งได้เลย
try:
    linked = mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID, keepalive=60)
except OSError:
    stop_here(st_broker, "2) broker    ต่อไม่ได้",
              "broker ที่ " + BROKER + " ไม่ตอบ")

if not linked:
    stop_here(st_broker, "2) broker    ปฏิเสธ",
              "ตรวจ IP ของ broker และพอร์ต 1883 ว่าเปิดอยู่จริง")

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
# ค่าที่ส่งคือค่าจากลูกบิดจริงบนบอร์ด ไม่ใช่ตัวเลขสุ่ม คนที่นั่งดูอีกฝั่งจะได้
# พิสูจน์ได้ด้วยมือตัวเองว่าหมุนลูกบิดที่นี่แล้วเลขที่โน่นขยับตาม
sent = 0
for i in range(1, N + 1):
    knob = -1
    try:
        s = sensors.snapshot()
        if "pot" in s:
            knob = int(s["pot"]["percent"])
    except OSError:
        # อ่านเซนเซอร์ไม่ได้รอบนี้ ไม่ใช่เหตุให้หยุดส่ง ส่ง -1 ไปแล้วบอกให้รู้ว่าอ่านไม่ได้
        knob = -1

    payload = {"id": DEVICE_ID,
               "n": i,
               "knob": knob,
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
    payload_lbl.text("ใบที่ " + str(i) + " knob=" + str(knob))
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
# เปิด MQTT Explorer บนคอมแล้วเข้าไปดูหัวข้อ bento/# ระหว่างที่ไฟล์นี้กำลังส่ง
# แล้วหมุนลูกบิดบนบอร์ดไปมา ดูว่าเลข knob บนหน้าจอคอมขยับตามมือคุณจริงไหม
# จากนั้นเปลี่ยน DEVICE_ID ให้ชนกับเพื่อนสักคน แล้วรันพร้อมกันสองบอร์ด
# ใบ้: broker ยอมให้ client_id ซ้ำกันไม่ได้ ดูว่าใครถูกเตะออก และฝั่งเราเห็นอะไรบ้าง
