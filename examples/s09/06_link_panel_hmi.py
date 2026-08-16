# 06_link_panel_hmi.py - หน้าจอสถานะลิงก์ ที่ทุกตัวเลขบนจอวัดมาจริง
#
# ก่อนกด Run แก้ 2 บรรทัดนี้ก่อน:
#   WIFI_SSID = ชื่อวงที่ผู้สอนแจก
#   WIFI_PASS = รหัสผ่านของวงนั้น
#
# ไฟล์นี้สอน: ตัวเลขทุกตัวบนจอสถานะ ต้องบอกได้ว่าวัดมาจากไหน
# ดูที่จอ   : การ์ดเดียวกลางจอ มี SSID - IP - ping ms ที่อัปเดตทุกวินาที
#             ลองถอดสาย LAN ของเราเตอร์ แล้วดูว่า ping เปลี่ยนเป็น "ไม่ตอบ"
#             แต่ IP ยังอยู่ - สองบรรทัดนั้นตอบคนละคำถามกัน
# กับดัก    : wifi.ping() รับเฉพาะหมายเลข IP ใส่ชื่อโฮสต์จะได้ ValueError
#
# ทำไมจอนี้ไม่มีแท่งความแรงสัญญาณ: wifi_status() ใน modwifi.c เขียน rssi เป็น
#   mp_obj_new_int(0) แบบไม่มีเงื่อนไข แท่งที่วาดจากมันจึงเต็ม 100% สีเขียว
#   ตลอดเวลา ทุกที่ แม้เดินออกนอกตึก - เป็นคำโกหกที่ไม่ error และไม่ค้าง
#   เลขที่วัดได้จริงและตอบคำถามเดียวกันคือ ping เป็นมิลลิวินาที จึงใช้ตัวนั้นแทน
#   ความแรงเป็น dBm จริง ๆ มีเฉพาะใน wifi.scan() (ดู s09/02) แต่นั่นคือความแรง
#   ของวงที่สแกนเจอ ไม่ใช่ของวงที่กำลังต่ออยู่ - คนละคำถามกัน

import wifi
import ui
import time

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"

# ping ไปที่ Google Public DNS - อยู่นอกบ้านเราแน่นอน และตอบ ping เสมอ
PING_IP = "8.8.8.8"
PING_TIMEOUT_MS = 1500     # สั้นพอที่จอจะไม่ค้างนานตอนเน็ตล่ม
GOOD_MS = 60               # เร็วกว่านี้ถือว่าดี
SLOW_MS = 200              # ช้ากว่านี้ถือว่าต้องดู

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_OK, COL_WARN, COL_BAD = 0x00E676, 0xFFA726, 0xFF5252

# สร้าง widget ให้ครบก่อนเข้าลูป การสร้างกลางลูปทั้งกินเวลาและกินโควตา 32 ตัว
# พื้นที่วาดได้จริงกว้าง 792 สูง 398 - มุมขวาล่างราว x>690 y>340 มีปุ่ม Console ทับอยู่
ui.screen()
time.sleep_ms(200)
ui.Panel(x=40, y=30, w=700, h=300, color=0x142240, min=COL_DIM, max=12, value=1)
ui.Label("LINK STATUS", x=60, y=44, color=COL_DIM, value=20)
l_ssid = ui.Label("SSID: -", x=60, y=88, color=COL_TEXT, value=20)
l_ip = ui.Label("IP: -", x=60, y=132, color=COL_TEXT, value=20)
l_ping = ui.Label("ping 8.8.8.8: -", x=60, y=176, color=COL_TEXT, value=20)
bar = ui.Bar(x=60, y=214, w=620, h=22, min=0, max=PING_TIMEOUT_MS, value=0,
             color=COL_DIM)
ui.Label("แถบยาว = ตอบช้า - เต็มราง = ไม่ตอบเลย", x=60, y=242, color=COL_DIM,
         value=16)
l_note = ui.Label("กำลังต่อ...", x=60, y=276, color=COL_DIM, value=18)
ui.poll()

# ป้าย "กำลังต่อ..." ขึ้นก่อนบรรทัดนี้แล้ว เพราะระหว่าง connect() จอจะไม่อัปเดตเลย
if not wifi.connect(WIFI_SSID, WIFI_PASS):
    l_note.color(COL_BAD)
    l_note.text("ต่อไม่สำเร็จ ตรวจชื่อวงและรหัสผ่าน")
    ui.poll()
    raise SystemExit

# ชื่อวงมาจากตัวแปรของเราเอง ไม่ได้ถามบอร์ด เพราะบอร์ดตอบเรื่องนี้ไม่ได้
# เขียนไว้ครั้งเดียวหลังต่อสำเร็จ เพราะมันไม่เปลี่ยนอีกตลอดการรันครั้งนี้
l_ssid.text("SSID: " + WIFI_SSID)
l_ssid.color(COL_OK)
l_note.text("อัปเดตทุก 1 วินาที - ทุกตัวเลขบนจอนี้วัดมาจริง")

while True:
    # อ่านทีละอย่างตามลำดับ แล้วเขียนลงจอทันทีที่ได้ค่า
    # ถ้าลิงก์หลุด ping() จะโยน OSError จึงต้องดักไว้ ไม่ใช่ปล่อยให้โปรแกรมตาย
    up = wifi.is_connected()
    ip = wifi.ip()
    l_ip.text("IP: " + ip)

    if not up or ip == "0.0.0.0":
        # ลิงก์หลุดแล้วจอต้องบอกว่าหลุด ไม่ใช่ค้างค่าล่าสุดไว้เฉย ๆ
        # จอที่ค้างค่าเก่าอันตรายกว่าจอว่าง เพราะคนอ่านจะเชื่อว่ามันยังจริง
        l_ip.color(COL_BAD)
        l_ping.color(COL_BAD)
        l_ping.text("ping 8.8.8.8: ลิงก์หลุด")
        bar.color(COL_BAD)
        bar.value(PING_TIMEOUT_MS)
        l_note.color(COL_BAD)
        l_note.text("ลิงก์หลุด - ตัวเลขที่เห็นเป็นของรอบก่อนหน้า")
    else:
        l_ip.color(COL_TEXT)
        try:
            ms = wifi.ping(PING_IP, PING_TIMEOUT_MS)
        except OSError:
            ms = -1

        if ms < 0:
            # ต่ออยู่แต่ปลายทางไม่ตอบ คือคนละอาการกับลิงก์หลุด และซ่อมคนละแบบ
            l_ping.color(COL_WARN)
            l_ping.text("ping 8.8.8.8: ไม่ตอบใน " + str(PING_TIMEOUT_MS) + " ms")
            bar.color(COL_WARN)
            bar.value(PING_TIMEOUT_MS)
            l_note.color(COL_WARN)
            l_note.text("ต่อวงได้ แต่ออกอินเทอร์เน็ตไม่ได้")
        else:
            col = COL_OK if ms <= GOOD_MS else (
                COL_WARN if ms <= SLOW_MS else COL_BAD)
            l_ping.color(col)
            l_ping.text("ping 8.8.8.8: " + str(ms) + " ms")
            bar.color(col)
            bar.value(ms)
            l_note.color(COL_OK)
            l_note.text("ปกติ - อัปเดตทุก 1 วินาที")

    # รอหนึ่งวินาทีแบบยัง poll อยู่ ถ้า sleep ก้อนเดียวยาว ๆ เฟิร์มแวร์จะซ่อน widget ทิ้ง
    for _ in range(5):
        ui.poll()
        time.sleep_ms(200)
