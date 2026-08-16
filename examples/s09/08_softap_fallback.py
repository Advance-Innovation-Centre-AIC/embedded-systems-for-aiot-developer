# 08_softap_fallback.py - ถ้าในห้องไม่มีวงให้ต่อ บอร์ดปล่อยวงของตัวเองได้
#
# ก่อนกด Run แก้ 2 บรรทัดนี้ก่อน:
#   WIFI_SSID = ชื่อวงที่ผู้สอนแจก
#   WIFI_PASS = รหัสผ่านของวงนั้น
#
# Why : วันที่ 14 ส.ค. 2026 เราสแกนจากบอร์ดในห้องจริงแล้วเจอ 18 วง
#        แต่ไม่มีวงชื่อ AIoT-Class อยู่ในนั้นสักวง ไฟล์ที่เรียก connect() ตรง ๆ
#        จึงค้างอยู่ที่ "กำลังต่อ" ไปเรื่อย ๆ โดยไม่มีอะไรบอกว่าเพราะอะไร
#        ไฟล์นี้เลยไม่เดา มันสแกนก่อน แล้วค่อยตัดสินใจจากสิ่งที่เห็นจริง
# What: scan() -> หาชื่อวงที่ต้องการในผลสแกน
#        เจอ    -> connect() ตามปกติ
#        ไม่เจอ -> softap() บอร์ดกลายเป็นตัวปล่อยสัญญาณเองที่ 192.168.4.1
#
# ข้อแลกเปลี่ยนที่ต้องรู้: บอร์ดมีวิทยุชุดเดียว เป็นตัวปล่อยสัญญาณแล้ว
#   จะเป็นลูกข่ายของวงอื่นพร้อมกันไม่ได้ โหมด softap จึง "ไม่มีทางออกอินเทอร์เน็ต"
#   ping() ออกนอกจึงใช้ไม่ได้ คุยได้เฉพาะกับเครื่องที่มาต่อเข้าวงของเราเท่านั้น
#   นี่ไม่ใช่ข้อจำกัดของคอร์สนี้ เครื่องมือช่างและกล้องติดรถส่วนใหญ่ก็เป็นแบบนี้
#
# ดูที่จอ: แถวล่างคือวงที่สแกนเจอจริง ๆ ในห้อง แถวบนบอกว่าบอร์ดตัดสินใจทำอะไร
# กับดัก : softap() ไม่ใส่อาร์กิวเมนต์จะได้ชื่อ PSoC-Edge-MPY รหัส micropython
#          ทุกบอร์ดในห้องจะชื่อซ้ำกันหมด ตั้งชื่อให้ต่างกันทุกทีมเสมอ

import wifi
import lcd
import ui
import time

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
# ตั้งชื่อวงสำรองให้ไม่ซ้ำกับทีมอื่น เปลี่ยนเลขท้ายเป็นเลขทีมตัวเอง
AP_SSID = "bento-team01"
AP_PASS = "12345678"
SHOW_ROWS = 3
COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD = 0x00E676, 0xFFA726, 0xFF5252

lcd.clear()
lcd.console("<h2>คาบ 9 - ไม่มีวงให้ต่อ ก็ปล่อยวงเอง</h2>")

ui.screen()
time.sleep_ms(200)
ui.Label("แผนสำรองเมื่อหาวงไม่เจอ", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=650, h=120, color=COL_CARD, min=COL_DIM, max=12, value=1)
l_found = ui.Label("กำลังสแกน รอสักครู่", x=40, y=66, color=COL_WARN, value=20)
l_action = ui.Label("-", x=40, y=100, color=COL_TEXT, value=20)
l_ip = ui.Label("-", x=40, y=134, color=COL_DIM, value=20)

ui.Panel(x=20, y=186, w=650, h=140, color=COL_CARD, min=COL_DIM, max=12, value=1)
rows = []
for i in range(SHOW_ROWS):
    rows.append(ui.Label("-", x=40, y=200 + i * 32, color=COL_DIM, value=18))
l_hint = ui.Label("-", x=40, y=296, color=COL_DIM, value=16)
ui.Label("วิทยุมีชุดเดียว เป็น AP แล้วออกเน็ตไม่ได้", x=20, y=340,
         color=COL_DIM, value=16)
ui.poll()

# --- สแกนก่อน แล้วค่อยตัดสินใจ ---
# บรรทัดนี้บล็อกได้ถึง 10 วินาที ข้อความ "กำลังสแกน" จึงต้องขึ้นก่อนหน้านี้
lcd.print("กำลังสแกน...")
nets = wifi.scan()
print("สแกนเจอ", len(nets), "วง")
lcd.print("สแกนเจอ <b>{}</b> วง".format(len(nets)))

# scan() คืนไม่เกิน 20 วงต่อครั้ง ในห้องที่มีวงเยอะกว่านั้น วงที่ 21 หายไปเงียบ ๆ
# ถ้าห้องไหนแน่นจริงและหาวงที่ต้องการไม่เจอ ให้ลองสแกนซ้ำอีกครั้ง
if len(nets) >= 20:
    print("เตือน: ได้ผลเต็มเพดาน 20 วง อาจมีวงที่ถูกตัดทิ้ง")
    lcd.print("<span class=warn>ผลเต็มเพดาน 20 วง อาจมีวงตกหล่น</span>")

nets.sort(key=lambda net: net[1], reverse=True)
for i in range(SHOW_ROWS):
    if i < len(nets):
        # net คือ tuple (ssid, rssi, security, channel) - อ่านด้วยเลขดัชนี
        rows[i].text("{:>4} dBm  ch {:<3} {}".format(nets[i][1], nets[i][3],
                                                     nets[i][0]))
        rows[i].color(COL_TEXT)

# หาชื่อวงที่ต้องการในผลสแกน - เทียบกับ net[0] ซึ่งเป็นช่อง ssid
target = None
for net in nets:
    if net[0] == WIFI_SSID:
        target = net
        break

if target is not None:
    # --- ทางปกติ: เจอวงที่ต้องการ ---
    l_found.color(COL_OK)
    l_found.text("เจอ {} ที่ {} dBm".format(WIFI_SSID, target[1]))
    l_action.text("กำลังต่อแบบลูกข่าย (โหมด sta)")
    ui.poll()
    lcd.print("เจอวง <b>{}</b> ความแรง {} dBm".format(WIFI_SSID, target[1]))

    ok = wifi.connect(WIFI_SSID, WIFI_PASS)
    if ok:
        l_action.color(COL_OK)
        l_action.text("ต่อสำเร็จ โหมด sta")
        l_ip.text("IP ที่ได้จาก DHCP: " + wifi.ip())
        l_hint.text("โหมดนี้ออกเน็ตได้ ลอง wifi.ping() ต่อได้เลย")
        lcd.print("<span class=ok>ต่อสำเร็จ IP {}</span>".format(wifi.ip()))
    else:
        l_action.color(COL_BAD)
        l_action.text("เจอวงแต่ต่อไม่ติด - รหัสผ่านผิดหรือเปล่า")
        l_ip.text("ip() = " + wifi.ip() + "  (ยังไม่ได้ต่อ)")
        l_hint.text("เจอวงแต่ต่อไม่ติด = ปัญหาอยู่ที่รหัสผ่าน")
        lcd.print("<span class=err>เจอวงแต่ต่อไม่ติด</span>")
else:
    # --- แผนสำรอง: ไม่เจอวงที่ต้องการ ---
    l_found.color(COL_BAD)
    l_found.text("ไม่เจอ {} ในการสแกนครั้งนี้".format(WIFI_SSID))
    l_action.color(COL_WARN)
    l_action.text("เปลี่ยนไปปล่อยวงเอง (โหมด softap)")
    ui.poll()
    lcd.print("<span class=warn>ไม่เจอ {} - ปล่อยวงเอง</span>".format(WIFI_SSID))

    # ตัดลิงก์เก่าทิ้งก่อน กันกรณีบอร์ดยังค้างต่ออยู่กับวงจากการรันครั้งก่อน
    wifi.disconnect()

    # softap() รับสองอาร์กิวเมนต์ ไม่ใส่ก็ได้แต่จะได้ชื่อซ้ำกันทั้งห้อง
    started = wifi.softap(AP_SSID, AP_PASS)
    print("softap() คืนค่า", started)

    if started:
        l_action.color(COL_OK)
        l_action.text("ปล่อยวง {} แล้ว".format(AP_SSID))
        # ที่อยู่ของบอร์ดในโหมด AP เป็นเลขตายตัวเสมอ ไม่ต้องเดาเหมือนตอนเป็นลูกข่าย
        l_ip.color(COL_OK)
        l_ip.text("บอร์ดอยู่ที่ 192.168.4.1  รหัส {}".format(AP_PASS))
        l_hint.text("เอามือถือต่อวงนี้ แล้วจะคุยกับบอร์ดได้")
        lcd.print("<span class=ok>ปล่อยวง {} ที่ 192.168.4.1</span>".format(
            AP_SSID))
        lcd.print("โหมดนี้ไม่มีทางออกเน็ต ping() ออกนอกใช้ไม่ได้")
    else:
        l_action.color(COL_BAD)
        l_action.text("เปิด softap ไม่สำเร็จ")
        l_hint.text("ลองรีเซ็ตบอร์ดแล้วรันใหม่")
        lcd.print("<span class=err>เปิด softap ไม่สำเร็จ</span>")

ui.poll()
print("สรุป: อย่าเรียก connect() โดยไม่รู้ว่าวงนั้นมีอยู่จริงหรือเปล่า")
print("      scan() ตอบคำถามนั้นได้ใน 10 วินาที ส่วน connect() ที่เดาผิดกินไป 85")
