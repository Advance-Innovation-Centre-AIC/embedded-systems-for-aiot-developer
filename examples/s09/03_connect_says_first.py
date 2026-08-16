# 03_connect_says_first.py - บอกก่อนแล้วค่อยรอ เพราะ connect() บล็อก
#
# ไฟล์นี้สอน: ป้ายสถานะต้องขึ้นจอ "ก่อน" บรรทัดที่บล็อก ไม่ใช่หลัง
#             เพราะ wifi.connect() ไม่คืนค่าจนกว่าจะรู้ผล ระหว่างนั้นจอไม่อัปเดตเลย
# ดูที่จอ   : ป้าย "กำลังต่อ" สีส้มขึ้นก่อนจอนิ่ง แล้วจึงเป็นเวลาที่รอไปเป็น ms
#             กับหมายเลข IP ที่ได้มา
# กับดัก    : ip() คืนสตริงเสมอ ตอนยังไม่ต่อคืน "0.0.0.0" ซึ่ง if ถือว่าเป็นจริง
#             การเช็ก if wifi.ip(): จึงผ่านทั้งที่ยังไม่มีที่อยู่

import wifi
import lcd
import ui
import time

# แก้สองบรรทัดนี้ให้ตรงกับเครือข่ายที่ผู้สอนแจก
WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

lcd.clear()
lcd.console("<h2>คาบ 9 - ต่อ WiFi</h2>")

ui.screen()
time.sleep_ms(200)
ui.Label("คาบ 9 - ต่อ WiFi แล้วบอกก่อนรอ", x=20, y=12, color=COL_TEXT, value=24)

# ป้ายนี้กับ ui.poll() ข้างล่างต้องมาก่อน connect() เสมอ ไม่ใช่หลัง
# มันคือสัญญากับผู้ใช้ว่า "ระบบยังมีชีวิตอยู่ กำลังทำงานที่ใช้เวลา"
l_state = ui.Label("กำลังต่อ " + WIFI_SSID + " ... จอจะนิ่งไปครู่หนึ่ง",
                   x=40, y=68, color=COL_WARN, value=20)
ui.Label("เวลาที่ใช้ (ms)", x=40, y=104, color=COL_DIM, value=16)
seg = ui.Seg7(text="----", x=40, y=128, w=180, h=60, color=COL_WARN)
ui.Label("IP ที่ได้", x=260, y=104, color=COL_DIM, value=16)
l_ip = ui.Label("-", x=260, y=132, color=COL_TEXT, value=24)

# ลำดับสามขั้นนี้คือทั้งบทเรียน วางไว้บนจอให้อ่านได้ตลอดเวลาที่รัน
ui.Label("1) ป้าย \"กำลังต่อ\" ขึ้นจอ แล้ว ui.poll()", x=40, y=228,
         color=COL_TEXT, value=20)
ui.Label("2) wifi.connect() บล็อก จอนิ่งตลอดช่วงนี้", x=40, y=260,
         color=COL_TEXT, value=20)
ui.Label("3) กลับมาแล้วจึงเขียนผลลงจอ", x=40, y=288, color=COL_TEXT, value=20)
l_note = ui.Label("ป้ายต้องขึ้นก่อนบรรทัดที่บล็อก", x=20, y=332, color=COL_DIM,
                  value=16)
ui.poll()

lcd.print("กำลังต่อ", WIFI_SSID, "- อาจรอนาน")
print("กำลังต่อ", WIFI_SSID)

t0 = time.ticks_ms()
ok = wifi.connect(WIFI_SSID, WIFI_PASS)
elapsed = time.ticks_diff(time.ticks_ms(), t0)

# ค่าที่คืนมาเป็น bool ตรงไปตรงมา ต่างจาก tesaiot.connect() ในคาบ 11
# ที่คืนค่าก่อนจะต่อเสร็จ ความต่างนี้สำคัญ อย่าจำสลับกัน
# Seg7 รับ "ข้อความ" เท่านั้น seg.value(1234) เงียบสนิทไม่ขึ้นอะไรเลย
seg.text(str(elapsed))
print("connect() ใช้เวลา", elapsed, "ms คืนค่า", ok)

if not ok:
    seg.color(COL_BAD)
    l_state.color(COL_BAD)
    l_state.text("ต่อไม่สำเร็จ ตรวจชื่อวงและรหัสผ่านอีกครั้ง")
    ui.poll()
    lcd.print("<span class=err>ต่อไม่สำเร็จใน", elapsed, "ms</span>")
    lcd.print("ตรวจชื่อวงและรหัสผ่านอีกครั้ง")
    raise SystemExit

# ip() คืนสตริงเสมอ ไม่เคยคืน None
# ตอนยังไม่ได้ต่อมันคืน "0.0.0.0" ซึ่งเป็นสตริงที่ if ถือว่าเป็นจริง
# การเช็ก if wifi.ip(): จึงผ่านทั้งที่ยังไม่มีที่อยู่ ต้องเทียบกับ "0.0.0.0" ตรง ๆ
ip = wifi.ip()
seg.color(COL_OK)
l_state.color(COL_OK)
l_state.text("ต่อสำเร็จ - " + WIFI_SSID)
l_ip.text(ip)
ui.poll()
lcd.print("<span class=ok>ต่อสำเร็จใน", elapsed, "ms</span>")
lcd.print("IP:", ip)

if ip == "0.0.0.0":
    # เกิดได้เมื่อลิงก์ขึ้นแล้วแต่ DHCP ยังไม่แจกเลขให้ รอสักครู่แล้วอ่านซ้ำ
    l_ip.color(COL_WARN)
    l_note.color(COL_WARN)
    l_note.text("ลิงก์ขึ้นแล้วแต่ยังไม่ได้เลข IP กำลังรอ DHCP")
    ui.poll()
    lcd.print("ยังไม่ได้เลข IP รอ DHCP อีกครู่")
    for _ in range(10):
        ui.poll()
        time.sleep_ms(200)
    ip = wifi.ip()
    l_ip.text(ip)
    lcd.print("อ่านซ้ำได้", ip)

l_note.text("ip()==\"0.0.0.0\" ต้องเทียบตรง ๆ อย่าเช็กแค่ if wifi.ip():")
ui.poll()
print("สรุป: ip =", wifi.ip(), "is_connected =", wifi.is_connected())
lcd.print("สรุป ip =", wifi.ip(), "connected =", wifi.is_connected())
