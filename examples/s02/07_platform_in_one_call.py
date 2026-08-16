# 07_platform_in_one_call.py - บอร์ดจำได้เองว่าจะต่อไปที่ไหน แม้ถอดไฟแล้วเสียบใหม่
#
# ไฟล์นี้ใช้ของใหม่จากโมดูล tesaiot ห้าชิ้น คือ config อ่านคลังค่าตั้งทั้งก้อน
# config_set เขียนทับทีละคีย์ connect สั่งเริ่มต่อ is_connected ถามว่าต่อได้หรือยัง
# และ publish ส่งข้อมูลออกโดยไม่ต้องบอกหัวข้อ เฟิร์มแวร์ประกอบหัวข้อให้เอง
#
# ไฟล์นี้สอน: ไฟล์ 05 ต้องพิมพ์ IP ของ broker ลงในโค้ดตรง ๆ ย้ายบอร์ดไปห้องอื่น
#             ก็ต้องแก้โค้ดทุกครั้ง ส่วนคลังค่าตั้งนี้อยู่บนแฟลชของบอร์ดเอง
#             ตั้งครั้งเดียวแล้วโปรแกรมทุกตัวหลังจากนั้นอ่านค่าเดียวกันได้เลย
# ดูที่จอ   : หกคีย์สำคัญจากคลังค่าตั้ง แล้วสถานะการต่อที่นับเวลารอขึ้นเรื่อย ๆ
#             ถ้าครบกำหนดแล้วยังไม่ติด จอจะบอกตรง ๆ ว่าไปไม่ถึง ไม่ใช่ค้างรอเงียบ ๆ
# กับดัก    : tesaiot.connect() ไม่ได้บล็อกจนต่อเสร็จ มันสั่งให้เริ่มแล้วคืนค่าทันที
#             ค่าที่คืนมาแปลว่า "รับคำสั่งแล้ว" ไม่ได้แปลว่า "ต่อได้แล้ว"
#             ต้องวนถาม is_connected() เอง และต้องมีกำหนดเวลาเลิกรอเสมอ
#
# ครึ่งหนึ่งของโมดูลนี้ใช้ไม่ได้บนบอร์ดชุดนี้ ตัวที่เกี่ยวกับชิปนิรภัย OPTIGA
# ทั้งหมด (sign encrypt hmac cred_read cred_write protected_update ฯลฯ) วิ่งไปหา
# คอร์ CM55 ซึ่งบิลด์มาโดยปิดสวิตช์นั้นไว้ (ENABLE_OPTIGA ?= 0 ใน proj_cm55/Makefile)
# เรียกไปก็ได้แต่รอจนหมดเวลาราวสิบวินาทีแล้วล้มเหลว ไฟล์นี้จึงไม่แตะเลยสักตัว
# เรื่องความปลอดภัยของจริงเป็นงานของคาบ 11 ไม่ใช่ของคาบนี้

import json
import lcd
import sensors
import time
import ui
import wifi
import tesaiot

# แก้ให้ตรงกับที่ผู้สอนแจกหน้าห้อง
WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
DEVICE_ID = "eva-team03"
BROKER = "192.168.1.50"

WAIT_MS = 15000      # รอให้ต่อติดนานสุดเท่าไร ครบแล้วเลิกรอ ไม่รอตลอดไป
POLL_MS = 250        # ถามซ้ำถี่แค่ไหนระหว่างรอ
N = 6                # ส่งกี่ใบถ้าต่อติด

# หกคีย์ที่อยากเห็นบนจอ คลังค่าตั้งจริงมีมากกว่านี้อีกหลายสิบ
SHOW = ("device_id", "broker", "port", "tls_mode", "qos", "keepalive")

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x00E676, 0xFFA726, 0xFF5252, 0x40C4FF

ui.screen()
time.sleep_ms(200)

ui.Label("คลังค่าตั้งที่บอร์ดจำเอง", x=20, y=12, color=COL_TEXT, value=24)
status = ui.Label("กำลังอ่านคลังค่าตั้ง", x=20, y=48, color=COL_DIM, value=18)

ui.Panel(x=20, y=78, w=650, h=120, color=COL_CARD, min=COL_DIM, max=12,
         value=1)
ui.Label("tesaiot.config()", x=36, y=88, color=COL_INFO, value=16)

# หกช่องสร้างไว้ครบตั้งแต่ตอนนี้ แล้วเดี๋ยวเขียนทับด้วยค่าจริง
# ห้ามสร้าง Label ด้วยข้อความว่าง LVGL จะเติมคำว่า "Label" ให้เอง แล้วมันจะค้าง
cells = []
for i in range(3):
    cells.append(ui.Label("รออ่าน", x=36, y=114 + i * 28, color=COL_TEXT,
                          value=18))
for i in range(3):
    cells.append(ui.Label("รออ่าน", x=360, y=114 + i * 28, color=COL_TEXT,
                          value=18))

ui.Label("สถานะการต่อ", x=20, y=210, color=COL_DIM, value=16)
conn_lbl = ui.Label("ยังไม่ได้สั่งต่อ", x=20, y=236, color=COL_DIM, value=24)

ui.Label("รอมาแล้ว (ms)", x=380, y=210, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=380, y=232, w=150, h=52, color=COL_INFO)

note = ui.Label("กำลังเริ่ม", x=20, y=298, color=COL_DIM, value=18)
warn = ui.Label("ครึ่ง OPTIGA ของโมดูลนี้ปิดอยู่บนบอร์ดชุดนี้", x=20, y=332,
                color=COL_WARN, value=16)
ui.Label("ตั้งครั้งเดียว อยู่บนแฟลช ถอดไฟแล้วยังอยู่", x=20, y=364,
         color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>คลังค่าตั้งที่บอร์ดจำเอง</h2>")

# --- อ่านคลังค่าตั้งที่มีอยู่เดิม ---
# ไม่ต้องต่อเน็ตก่อน คลังนี้อยู่บนแฟลชของบอร์ด อ่านได้ทันทีตั้งแต่บรรทัดแรก
cfg = tesaiot.config()
lcd.print("คลังค่าตั้งมีทั้งหมด", len(cfg), "คีย์")
for k in cfg:
    lcd.print("  " + k + " =", cfg[k])

# --- เขียนทับเฉพาะคีย์ที่คาบนี้ต้องใช้ ---
# config_set() รับสองอาร์กิวเมนต์ที่ต้องเป็นข้อความทั้งคู่ ส่งเลข 1883 ดิบ ๆ
# เข้าไปจะไม่ผ่าน ต้องเขียนเป็น "1883" แม้ตอนอ่านกลับมามันจะเป็นเลขก็ตาม
tesaiot.config_set("device_id", DEVICE_ID)
tesaiot.config_set("broker", BROKER)
tesaiot.config_set("port", "1883")

# อ่านซ้ำหลังเขียน เพื่อพิสูจน์ว่าค่าที่เขียนไปลงจริง ไม่ใช่เชื่อว่าลง
cfg = tesaiot.config()

for i in range(len(SHOW)):
    key = SHOW[i]
    if key in cfg:
        cells[i].color(COL_TEXT)
        cells[i].text(key + " = " + str(cfg[key]))
    else:
        # คีย์ที่ไม่มีในเฟิร์มแวร์รุ่นนี้ ต้องบอกให้รู้ ไม่ใช่แสดงช่องว่างเฉย ๆ
        cells[i].color(COL_DIM)
        cells[i].text(key + " = ไม่มีคีย์นี้")

status.color(COL_OK)
status.text("อ่านได้ " + str(len(cfg)) + " คีย์ | เขียนทับไป 3 คีย์")
ui.poll()
lcd.print("<span class=ok>เขียนทับ device_id, broker, port แล้ว</span>")

# ตรงนี้คือจุดสำคัญของทั้งไฟล์ ถอดไฟบอร์ดแล้วเสียบใหม่ ค่าสามตัวนี้ยังอยู่
# โปรแกรมตัวถัดไปจึงไม่ต้องรู้ IP ของ broker เลย มันถาม config() เอาได้เลย
lcd.console("<span class=muted>ค่าพวกนี้อยู่บนแฟลช ถอดไฟแล้วยังอยู่</span>")

# --- ต่อ WiFi ก่อน ---
# tesaiot.connect() ไม่ได้ต่อ WiFi ให้ มันคุยระดับบนกว่านั้น ต้องมี IP ก่อนเสมอ
status.color(COL_WARN)
status.text("กำลังต่อ WiFi จอจะนิ่งสักครู่")
ui.poll()
lcd.print("กำลังต่อ WiFi", WIFI_SSID)

if not wifi.connect(WIFI_SSID, WIFI_PASS):
    heard = False
    for net in wifi.scan():
        if net[0] == WIFI_SSID:
            heard = True
    if heard:
        why = "ได้ยินวง " + WIFI_SSID + " แต่ต่อไม่ผ่าน ตรวจรหัสผ่าน"
    else:
        why = "ไม่ได้ยินวง " + WIFI_SSID + " เลย ตรวจชื่อวงหรือย้ายที่"
    conn_lbl.color(COL_BAD)
    conn_lbl.text("ไม่มีเน็ต")
    status.color(COL_BAD)
    status.text("อ่านเขียนคลังค่าตั้งได้ แต่ต่อเน็ตไม่ได้")
    note.color(COL_BAD)
    note.text(why)
    ui.poll()
    lcd.print("<span class=error>" + why + "</span>")
    lcd.print("<span class=ok>ส่วนคลังค่าตั้งทำงานครบแล้ว ไม่ต้องใช้เน็ต</span>")
    print("หยุดที่: " + why)
    raise SystemExit

lcd.print("<span class=ok>ได้ IP", wifi.ip(), "</span>")

# --- สั่งต่อ แล้ววนถามเอง ---
# connect() คืนค่าทันทีโดยไม่รอให้ต่อเสร็จ ค่าที่ได้แปลว่า "รับคำสั่งไปแล้ว"
# ใครเขียน if tesaiot.connect(): แล้วส่งต่อทันที จะได้ OSError เพราะสายยังไม่ขึ้น
conn_lbl.color(COL_WARN)
conn_lbl.text("สั่งต่อแล้ว กำลังรอ")
note.color(COL_DIM)
note.text("รอสูงสุด " + str(WAIT_MS // 1000) + " วินาที แล้วเลิกรอ")
ui.poll()
lcd.print("สั่ง tesaiot.connect() แล้ว กำลังรอสาย")

tesaiot.connect()

t0 = time.ticks_ms()
linked = False

while True:
    waited = time.ticks_diff(time.ticks_ms(), t0)
    seg.text(str(waited))

    if tesaiot.is_connected():
        linked = True
        break

    # กำหนดเวลาเลิกรอ คือสิ่งที่แยกโปรแกรมที่ล้มเหลวอย่างสุภาพ
    # ออกจากโปรแกรมที่ค้างจนคนดูต้องถอดไฟ
    if waited >= WAIT_MS:
        break

    ui.poll()
    time.sleep_ms(POLL_MS)

if not linked:
    seg.color(COL_BAD)
    conn_lbl.color(COL_BAD)
    conn_lbl.text("ต่อไม่ติดใน " + str(WAIT_MS // 1000) + " วินาที")
    status.color(COL_BAD)
    status.text("มีเน็ตแล้ว แต่ไปไม่ถึงแพลตฟอร์มที่ " + BROKER)
    note.color(COL_BAD)
    note.text("ตรวจว่า broker เปิดอยู่และอยู่วงเดียวกัน")
    ui.poll()
    lcd.print("<span class=error>รอครบ", WAIT_MS, "ms แล้วยังไม่ติด</span>")
    lcd.print("<span class=muted>ค่าที่ตั้งไว้ยังอยู่ ไม่ต้องตั้งใหม่รอบหน้า</span>")
    print("ต่อไม่ติด | broker =", cfg.get("broker", "?"))
    raise SystemExit

seg.color(COL_OK)
conn_lbl.color(COL_OK)
conn_lbl.text("ต่อติดใน " + str(time.ticks_diff(time.ticks_ms(), t0)) + " ms")
status.color(COL_OK)
status.text("พร้อมส่งแล้ว")
ui.poll()
lcd.print("<span class=ok>ต่อติดแล้ว</span>")

# --- ส่งของจริง ---
# publish() ของโมดูลนี้ไม่ต้องใส่หัวข้อ เฟิร์มแวร์ประกอบให้จาก device_id
# ที่อยู่ในคลังค่าตั้ง เปลี่ยนชื่อบอร์ดที่เดียว หัวข้อเปลี่ยนตามทั้งระบบ
sent = 0
for i in range(1, N + 1):
    knob = -1
    try:
        s = sensors.snapshot()
        if "pot" in s:
            knob = int(s["pot"]["percent"])
    except OSError:
        knob = -1

    body = json.dumps({"n": i, "knob": knob,
                       "uptime_s": time.ticks_ms() // 1000})

    if not tesaiot.is_connected():
        conn_lbl.color(COL_BAD)
        conn_lbl.text("สายหลุดที่ใบที่ " + str(i))
        ui.poll()
        lcd.print("<span class=error>สายหลุดที่ใบที่", i, "</span>")
        break

    if tesaiot.publish(body):
        sent = i
        note.color(COL_OK)
        note.text("ส่งแล้ว " + str(sent) + " ใบ | knob = " + str(knob))
        lcd.print("ใบที่", i, "->", body)
    else:
        note.color(COL_WARN)
        note.text("ใบที่ " + str(i) + " ถูกปฏิเสธ")
        lcd.print("<span class=warn>ใบที่", i, "ถูกปฏิเสธ</span>")

    ui.poll()
    time.sleep_ms(1500)

warn.color(COL_DIM)
status.color(COL_OK)
status.text("จบแล้ว - ส่งได้ " + str(sent) + " ใบ จาก " + str(N))
ui.poll()

lcd.console("<span class=muted>------------------------</span>")
lcd.print("<span class=ok>ส่งได้", sent, "ใบ จาก", N, "</span>")
print("ส่งได้", sent, "ใบ | คลังค่าตั้งมี", len(cfg), "คีย์")

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# รันไฟล์นี้หนึ่งรอบ แล้วถอดสาย USB ออกจริง ๆ เสียบกลับ แล้วรันแค่สองบรรทัดนี้
# ในหน้า Playground: import tesaiot แล้ว print(tesaiot.config()["broker"])
# ตอบว่าเลขที่ได้คือเลขที่คุณตั้งไว้เมื่อกี้ หรือกลับไปเป็นค่าโรงงาน
# ใบ้: ถ้าอยากลบทิ้งให้กลับไปเป็นค่าโรงงานจริง ๆ มี tesaiot.config_reset() ให้ใช้
