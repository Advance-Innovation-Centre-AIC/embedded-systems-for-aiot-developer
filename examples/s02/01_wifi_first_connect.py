# 01_wifi_first_connect.py - พาบอร์ดออกเน็ตครั้งแรก แล้วอ่านเลขที่อยู่ของมัน
#
# ไฟล์นี้สอน: wifi.connect() รับสองค่าคือชื่อวงกับรหัสผ่าน และคืน True หรือ False
#             ส่วน wifi.ip() คืนเลขที่อยู่ของบอร์ดบนวงนั้น และ wifi.is_connected()
#             ตอบว่าตอนนี้ยังต่ออยู่ไหม สามตัวนี้คือทั้งหมดที่คาบนี้ใช้
# ดูที่จอ   : ป้าย "กำลังต่อ" สีส้มขึ้นก่อน แล้วจอจะนิ่งไปนาน นั่นคือช่วงที่บอร์ดกำลังต่อ
#             พอกลับมาจะได้เวลาที่ใช้เป็น ms กับเลข IP ขึ้นตัวใหญ่
# กับดัก    : connect() บล็อกได้นานถึงราว 85 วินาที ระหว่างนั้นจอไม่ขยับเลยแม้แต่พิกเซล
#             ป้ายบอกสถานะกับ ui.poll() จึงต้องมา "ก่อน" บรรทัดนั้น ไม่ใช่หลัง

import lcd
import time
import ui
import wifi

# แก้สองบรรทัดนี้ให้ตรงกับเครือข่ายที่ผู้สอนแจกหน้าห้อง
WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD = 0x00E676, 0xFFA726, 0xFF5252

ui.screen()
time.sleep_ms(200)

ui.Label("ต่อ WiFi ครั้งแรกของคาบนี้", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=650, h=136, color=COL_CARD, min=COL_DIM, max=12, value=1)

ui.Label("สถานะ", x=40, y=62, color=COL_DIM, value=16)
state = ui.Label("กำลังจะเริ่มต่อ", x=40, y=86, color=COL_WARN, value=20)

ui.Label("เวลาที่ใช้ (ms)", x=40, y=124, color=COL_DIM, value=16)
seg = ui.Seg7(text="----", x=180, y=118, w=150, h=48, color=COL_WARN)

ui.Label("IP ที่ได้", x=360, y=124, color=COL_DIM, value=16)
ip_lbl = ui.Label("-", x=360, y=148, color=COL_TEXT, value=24)

step_lbl = ui.Label("ป้ายนี้ขึ้นก่อน แล้วจอจะนิ่ง", x=20, y=210, color=COL_TEXT,
                    value=20)
conn_lbl = ui.Label("ยังไม่ได้ถาม is_connected()", x=20, y=248, color=COL_DIM,
                    value=20)
note = ui.Label("ครั้งแรกอาจรอนาน อย่าเพิ่งกดรีเซ็ต", x=20, y=286,
                color=COL_DIM, value=20)

# เคาะให้ป้ายทั้งหมดขึ้นจอจริง ๆ ก่อนเข้าบรรทัดที่บล็อก
# ถ้าลืมบรรทัดนี้ ป้ายจะยังไม่ทันโผล่ แล้วคนดูจะเห็นจอว่างตลอดช่วงที่รอ
ui.poll()

lcd.clear()
lcd.console("<h2>ต่อ WiFi</h2>")
lcd.print("กำลังต่อวง", WIFI_SSID)
print("กำลังต่อ", WIFI_SSID, "- บรรทัดถัดไปจะบล็อก")

# จับเวลาคร่อมบรรทัดที่บล็อก จะได้รู้ด้วยตัวเลขว่ารอไปเท่าไร ไม่ใช่รู้สึกว่านาน
t0 = time.ticks_ms()
ok = wifi.connect(WIFI_SSID, WIFI_PASS)
elapsed = time.ticks_diff(time.ticks_ms(), t0)

# Seg7 รับ "ข้อความ" เท่านั้น seg.value(12345) เงียบสนิทและไม่มี error ให้จับ
seg.text(str(elapsed))
step_lbl.text("กลับมาแล้ว - connect() คืนค่า " + str(ok))
print("connect() ใช้เวลา", elapsed, "ms คืนค่า", ok)

if not ok:
    # ล้มเหลวก็ต้องพูด โปรแกรมที่พูดเฉพาะตอนสำเร็จจะเงียบสนิทในจังหวะที่คนอยากรู้ที่สุด
    seg.color(COL_BAD)
    state.text("ต่อไม่สำเร็จ")
    state.color(COL_BAD)
    note.text("ตรวจชื่อวงกับรหัสผ่านอีกครั้ง แล้วรันใหม่")
    note.color(COL_BAD)
    ui.poll()
    lcd.print("<span class=error>ต่อไม่สำเร็จใน", elapsed, "ms</span>")
    raise SystemExit

seg.color(COL_OK)
state.text("ต่อสำเร็จ - " + WIFI_SSID)
state.color(COL_OK)

# ip() คืนสตริงเสมอ ไม่เคยคืน None และตอนยังไม่มีที่อยู่มันคืน "0.0.0.0"
# ซึ่งเป็นสตริงที่ if ถือว่าจริง การเขียน if wifi.ip(): จึงผ่านทั้งที่ยังไม่มีเลข
# ต้องเทียบกับ "0.0.0.0" ตรง ๆ เท่านั้น
ip = wifi.ip()
ip_lbl.text(ip)

if ip == "0.0.0.0":
    ip_lbl.color(COL_WARN)
    note.text("ลิงก์ขึ้นแล้วแต่ยังไม่ได้เลข IP กำลังรอ DHCP")
    note.color(COL_WARN)
    ui.poll()
    lcd.print("<span class=warn>ยังไม่ได้เลข IP รอ DHCP</span>")

    # รอเป็นรอบสั้น ๆ แล้วอ่านซ้ำ ดีกว่าหลับยาวรวดเดียวแล้วหวังว่าจะทัน
    for _ in range(10):
        ui.poll()
        time.sleep_ms(200)
    ip = wifi.ip()
    ip_lbl.text(ip)
    lcd.print("อ่านซ้ำได้", ip)

# ถามยืนยันอีกทางหนึ่ง ค่าที่ connect() คืนมาบอกว่า "ตอนนั้นสำเร็จ"
# ส่วน is_connected() บอกว่า "ตอนนี้ยังต่ออยู่ไหม" สองคำถามคนละเวลา
conn_lbl.text("is_connected() ตอบว่า " + str(wifi.is_connected()))
conn_lbl.color(COL_OK if wifi.is_connected() else COL_WARN)
note.text("เอาเลข IP นี้ไปกรอกในใบงานได้เลย")
note.color(COL_DIM)
ui.poll()

lcd.print("<span class=ok>ต่อสำเร็จใน", elapsed, "ms</span>")
lcd.print("<span class=ok>IP:", ip, "</span>")
print("สรุป ip =", ip, "| is_connected =", wifi.is_connected())

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# พิมพ์รหัสผ่านให้ผิดไปหนึ่งตัว แล้วรันใหม่ จับเวลาว่ากว่าจะรู้ว่าผิดใช้เวลากี่ ms
# เทียบกับตอนที่รหัสถูก แล้วตอบว่าทำไมกรณีผิดถึงใช้เวลามากกว่า ไม่ใช่น้อยกว่า
# ใบ้: บอร์ดไม่ได้ยอมแพ้ตั้งแต่ครั้งแรก มันลองใหม่ให้เองหลายรอบก่อนจะคืน False
