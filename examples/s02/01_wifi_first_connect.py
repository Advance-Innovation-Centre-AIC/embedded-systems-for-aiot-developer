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
WIFI_SSID = "bento-teamXX"            # ชื่อ Hotspot มือถือของทีม (WiFi คณะต้อง login บอร์ดใช้ไม่ได้)
WIFI_PASS = "<รหัส Hotspot ของทีม>"     # อย่างน้อย 8 ตัว

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

ui.screen()
time.sleep_ms(200)

# ผังจอเดินบนกริด 8 ขอบนอก 24 - หัวเรื่องหนึ่งบรรทัด การ์ดผลการต่อหนึ่งใบ
# แล้วสามบรรทัดเล่าลำดับเหตุการณ์ ทั้งหมดจบก่อน y=340 ซึ่งเป็นที่ของปุ่ม Console
ui.Label("ต่อ WiFi ครั้งแรกของคาบนี้", x=24, y=8, color=COL_TEXT, value=24)
ui.Panel(x=24, y=56, w=744, h=128, color=COL_CARD, min=COL_CARD, max=12,
         value=1)

# สามช่องในการ์ดวางห่างกันพอให้ป้ายที่ยาวขึ้นตอนรันไม่ไปทับช่องข้าง ๆ
# ช่องสถานะยาวสุดคือ "ต่อสำเร็จ - <ชื่อวง>" ซึ่งกินราว 300 px ที่ตัวอักษร 24
ui.Label("สถานะ", x=40, y=72, color=COL_DIM, value=16)
state = ui.Label("กำลังจะเริ่มต่อ", x=40, y=104, color=COL_WARN, value=20)

ui.Label("เวลาที่ใช้ (ms)", x=376, y=72, color=COL_DIM, value=16)
seg = ui.Seg7(text="----", x=376, y=104, w=144, h=64, color=COL_WARN)

ui.Label("IP ที่ได้", x=560, y=72, color=COL_DIM, value=16)
ip_lbl = ui.Label("-", x=560, y=104, color=COL_TEXT, value=24)

step_lbl = ui.Label("ป้ายนี้ขึ้นก่อน แล้วจอจะนิ่ง", x=24, y=208, color=COL_TEXT,
                    value=20)
conn_lbl = ui.Label("ยังไม่ได้ถาม is_connected()", x=24, y=256, color=COL_DIM,
                    value=20)
note = ui.Label("ครั้งแรกอาจรอนาน อย่ากดรีเซ็ต", x=24, y=304,
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

# ส่งเป็นข้อความเพื่อคุมรูปแบบเอง seg.value(12345) ก็ได้ แต่ได้จำนวนเต็มล้วน
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
