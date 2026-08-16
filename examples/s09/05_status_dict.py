# 05_status_dict.py - อ่านสถานะครั้งเดียว แล้วใช้ค่าชุดนั้นทั้งรอบ
#
# ก่อนกด Run แก้ 2 บรรทัดนี้ก่อน:
#   WIFI_SSID = ชื่อวงที่ผู้สอนแจก
#   WIFI_PASS = รหัสผ่านของวงนั้น
#
# Why : ตอนลิงก์มีปัญหา คนมักไล่ถามทีละอย่าง - ต่ออยู่ไหม IP อะไร โหมดไหน
#        ถามสามคำถามคือได้คำตอบจากสามจังหวะเวลา ระหว่างนั้นลิงก์อาจหลุดไปแล้ว
#        แล้วเราจะได้ภาพผสมที่ไม่เคยเกิดขึ้นจริงสักวินาทีเดียว แล้วสรุปผิดจากมัน
# What: wifi.status() คืน dict ทั้งชุดในการอ่านครั้งเดียว ค่าทุกตัวในนั้น
#        มาจากจังหวะเวลาเดียวกัน เก็บ dict ไว้ในตัวแปรครั้งเดียวต่อรอบ
#        แล้วอ่านจากตัวแปรนั้น อย่าเรียก status() ซ้ำเพื่ออ่านคีย์ถัดไป
#
# สองคีย์ที่ไฟล์นี้ไม่แตะ: status() มีคีย์ ssid กับ rssi อยู่ด้วย แต่ wifi_status()
#   ใน modwifi.c เขียนค่าไว้ตรง ๆ ว่า mp_obj_new_str("", 0) และ mp_obj_new_int(0)
#   แบบไม่มีเงื่อนไข อ่านได้ "" กับ 0 เสมอ ไม่ว่าจะต่ออยู่หรือไม่ ใกล้หรือไกลเราเตอร์
#   เอาค่าที่ไม่เคยถูกวัดขึ้นจอ คือการสร้างหลักฐานปลอมให้คนหน้างานเชื่อ
#   ความแรงสัญญาณค่าจริงมีเฉพาะใน wifi.scan() (ดู s09/01 และ s09/02)
#
# ดูที่จอ: สามคีย์เรียงกันและเปลี่ยนค่าตรงหน้า พร้อมเลขรอบที่อ่าน
#          บรรทัดสีเทาข้างขวาคือคำเตือนเรื่องสองคีย์ที่ยังไม่มีใครวัด
# กับดัก : เขียนคีย์ที่ไม่มีอยู่ เช่น status()["channel"] จะได้ KeyError กลางลูป
#          คีย์ที่ไม่แน่ใจให้ใช้ .get() พร้อมค่าเริ่มต้นเสมอ

import wifi
import lcd
import ui
import time

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
WATCH_ROUNDS = 14
COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD = 0x00E676, 0xFFA726, 0xFF5252

# สามคีย์ที่เฟิร์มแวร์วัดจริงและเปลี่ยนค่าตามสถานการณ์
# อีกสองคีย์ (ssid, rssi) เป็นค่าคงที่ในซอร์ส จึงไม่อยู่ในรายการนี้
KEYS = ("connected", "ip", "mode")

lcd.clear()
lcd.console("<h2>คาบ 9 - อ่าน status() ครั้งเดียวต่อรอบ</h2>")

ui.screen()
time.sleep_ms(200)
ui.Label("คาบ 9 - อ่านสถานะครั้งเดียวต่อรอบ", x=20, y=12, color=COL_TEXT,
         value=24)
ui.Panel(x=20, y=48, w=650, h=160, color=COL_CARD, min=COL_DIM, max=12, value=1)

# หนึ่ง Label ต่อหนึ่งคีย์ สร้างครั้งเดียวแล้วเปลี่ยนแต่ข้อความ
# ถ้าสร้าง Label ใหม่ทุกรอบ โควตา 32 ตัวจะหมดภายในไม่กี่วินาที
rows = []
for i in range(len(KEYS)):
    rows.append(ui.Label(KEYS[i] + " = -", x=40, y=62 + i * 34,
                         color=COL_TEXT, value=20))

# คำเตือนสองบรรทัด แยกกันเพราะรวมแล้วเกินเพดาน 126 ไบต์ ไทยกินตัวละ 3 ไบต์
ui.Label("คีย์ ssid กับ rssi ยังไม่มีใครวัด", x=380, y=62, color=COL_DIM,
         value=16)
ui.Label("ซอร์สเขียนค่าไว้ตรง ๆ เป็น \"\" กับ 0", x=380, y=88, color=COL_DIM,
         value=16)
# ประโยคนี้ยาว 103 ไบต์ ยังอยู่ใต้เพดาน 126 ของตัวสร้าง จึงเป็นป้ายเดียวได้
# ครึ่งหลังคือคำตอบว่าชื่อวงมาจากไหน ห้ามตัดทิ้งเพื่อความสั้น
ui.Label("ชื่อวงที่ต่อ = สตริงที่เราส่งเข้า connect()", x=380, y=114,
         color=COL_DIM, value=16)
l_ssid = ui.Label("-", x=380, y=140, color=COL_OK, value=20)
l_phase = ui.Label("รอบที่ 1 - ก่อนต่อ", x=20, y=216, color=COL_DIM, value=18)
# ตัวสร้างกับ .text() รับได้เท่ากันคือ 126 ไบต์ ประโยคเต็มจึงวางที่ไหนก็ได้
l_same = ui.Label("-", x=20, y=248, color=COL_DIM, value=18)
l_same.text("ทุกค่าบนจอมาจากการอ่านครั้งเดียวกัน")
ui.poll()


def show(st, phase):
    # อัปเดตข้อความในที่เดิม จอจึงเป็นหน้าเดียวที่เปลี่ยนค่า ไม่ใช่รายการที่ไหลลง
    # st ที่รับเข้ามาคือ dict ก้อนเดียว ไม่ได้เรียก status() ซ้ำในฟังก์ชันนี้
    for i in range(len(KEYS)):
        rows[i].text("{} = {}".format(KEYS[i], st[KEYS[i]]))
    rows[0].color(COL_OK if st["connected"] else COL_BAD)
    l_phase.text(phase)
    ui.poll()


def dump(title):
    # status() อ่านครั้งเดียว แล้วใช้ค่าจากตัวแปรเดียวกันทั้งรอบ
    st = wifi.status()
    lcd.print("<b>--- " + title + " ---</b>")
    print("---", title, "---")
    for k in KEYS:
        lcd.print("  {} = {}".format(k, st[k]))
        print("  {:<10} = {}".format(k, st[k]))
    # คีย์ที่ไม่มีอยู่จริง อ่านด้วย .get() แล้วได้ค่าเริ่มต้น ไม่ระเบิด
    lcd.print("  channel = {} (ไม่มีคีย์นี้จริง)".format(
        st.get("channel", "ไม่มีคีย์นี้")))
    print("  {:<10} = {}  (คีย์นี้ไม่มีจริง จึงได้ค่าที่เราตั้งไว้)".format(
        "channel", st.get("channel", "ไม่มีคีย์นี้")))
    return st


# รอบแรก อ่านตอนยังไม่ได้ต่อ เพื่อให้เห็นว่า "ไม่ต่อ" หน้าตาเป็นอย่างไร
before = dump("ก่อนต่อ")
show(before, "รอบที่ 1 - ก่อนต่อ")
time.sleep_ms(1200)

l_phase.color(COL_WARN)
l_phase.text("กำลังต่อ " + WIFI_SSID + " ... จอจะนิ่งครู่หนึ่ง")
ui.poll()
lcd.print("กำลังต่อ", WIFI_SSID, "- อาจรอนาน")
ok = wifi.connect(WIFI_SSID, WIFI_PASS)
print("connect() คืนค่า", ok)

# ชื่อวงที่ต่ออยู่ เรารู้เพราะเราเป็นคนส่งมันเข้าไปเอง ไม่ได้ถามบอร์ด
# นี่คือทางเดียวที่ตอบคำถาม "ตอนนี้อยู่วงไหน" ได้ตรงกับความจริงบนเฟิร์มแวร์รุ่นนี้
l_ssid.text(WIFI_SSID if ok else "ยังไม่ได้ต่อวงไหน")
l_ssid.color(COL_OK if ok else COL_BAD)

after = dump("หลังต่อ")
l_phase.color(COL_OK if ok else COL_BAD)
show(after, "รอบที่ 2 - หลังต่อ")

# เทียบทีละคีย์แล้วบอกว่าอะไรเปลี่ยน วิธีนี้ใช้ debug ได้จริงหน้างาน
# เพราะมันตอบคำถามว่า "การต่อครั้งนี้เปลี่ยนอะไรในเครื่องบ้าง"
lcd.print("<b>คีย์ที่เปลี่ยนไปหลังต่อ</b>")
print("คีย์ที่เปลี่ยนไปหลังต่อ:")
for k in KEYS:
    if before[k] != after[k]:
        lcd.print("  {}: {} -> {}".format(k, before[k], after[k]))
        print("  {:<10} {} -> {}".format(k, before[k], after[k]))

# is_connected() กับ status()["connected"] ตอบเรื่องเดียวกัน
# ตัวแรกสั้นกว่าเมื่อต้องการคำตอบอย่างเดียว ตัวหลังคุ้มกว่าเมื่อต้องการหลายค่าพร้อมกัน
# ที่ห้ามทำคือเรียก status() สามครั้งเพื่ออ่านสามคีย์ - เก็บผลไว้ในตัวแปรครั้งเดียวพอ
lcd.print("is_connected() = {} - [\"connected\"] = {}".format(
    wifi.is_connected(), after["connected"]))
print("is_connected() =", wifi.is_connected())
print('status()["connected"] =', after["connected"])

if not after["connected"]:
    l_same.color(COL_BAD)
    l_phase.color(COL_BAD)
    l_phase.text("ยังไม่ได้ต่อ ข้ามรอบเฝ้าดู")
    l_same.text("ตรวจ WIFI_SSID กับ WIFI_PASS ที่หัวไฟล์")
    ui.poll()
    lcd.print("<span class=err>ยังไม่ได้ต่อ ข้ามรอบเฝ้าดู</span>")
    raise SystemExit

# เฝ้าดูหลายรอบ รอบละ "หนึ่งการอ่าน" เพื่อพิสูจน์ข้อที่ไฟล์นี้สอน
# ถ้าลิงก์หลุดกลางทาง ทั้งสามค่าจะเปลี่ยนพร้อมกันในรอบเดียวกัน ไม่ใช่ทยอยเปลี่ยน
l_same.color(COL_OK)
lcd.print("<b>เฝ้าดู</b> ลองปิดเราเตอร์แล้วดูว่าสามค่าเปลี่ยนพร้อมกันไหม")
last_ip = after["ip"]

for i in range(WATCH_ROUNDS):
    st = wifi.status()          # อ่านครั้งเดียว - บรรทัดนี้คือหัวใจของไฟล์
    show(st, "เฝ้าดูรอบที่ {}/{}".format(i + 1, WATCH_ROUNDS))
    l_same.text("รอบนี้อ่าน status() ไป 1 ครั้ง ใช้ค่า {} ตัว".format(len(KEYS)))

    # จอเขียนทับที่เดิมได้ทุกรอบ แต่ log ต้องพิมพ์ "เมื่อค่าเปลี่ยน" ไม่ใช่ทุกรอบ
    # ไม่งั้นบรรทัดที่สำคัญกว่าจะถูกดันหายไปจากหน้าจอคอนโซล
    if st["ip"] != last_ip:
        lcd.print("  รอบ {} IP เปลี่ยน {} -> {}".format(i + 1, last_ip, st["ip"]))
        last_ip = st["ip"]
    print("  รอบ", i + 1, "connected =", st["connected"], "ip =", st["ip"])

    for _ in range(3):
        ui.poll()
        time.sleep_ms(160)

l_phase.text("จบการเฝ้าดู")
l_same.text("ทุกแถวบนจอมาจากการอ่าน status() ครั้งเดียวกัน")
ui.poll()
print("กติกาข้อเดียวของไฟล์นี้: หนึ่งรอบ อ่าน status() หนึ่งครั้ง")
