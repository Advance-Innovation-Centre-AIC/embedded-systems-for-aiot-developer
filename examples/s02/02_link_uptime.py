# 02_link_uptime.py - ต่อติดแล้ว กับยังต่ออยู่ ไม่ใช่คำถามเดียวกัน
#
# ไฟล์นี้ไม่มีคำสั่งใหม่เลย ทุกตัวเคยผ่านตามาแล้วในไฟล์ 01 ของคาบนี้ และในคาบ 1
# ของใหม่คือการเอามันมาต่อกันบนจอใบเดียว ซึ่งเป็นจอที่ใบงานขอ
#
# ไฟล์นี้สอน: connect() ตอบว่า "ตอนนั้นต่อสำเร็จ" ส่วน is_connected() ตอบว่า
#             "ตอนนี้ยังต่ออยู่ไหม" สองคำถามคนละเวลา และคำตอบต่างกันได้ทุกวินาที
# ดูที่จอ   : ป้าย IP มุมขวาบนค้างไว้ตลอด ตัวเลขวินาทีที่ออนไลน์เดินขึ้นเรื่อย ๆ
#             เส้นกราฟอยู่สูงตอนลิงก์ปกติ และทรุดลงพื้นทันทีที่ลิงก์หลุด
#             ลองปิด WiFi ที่เราเตอร์สักครู่ระหว่างที่โปรแกรมกำลังเดิน แล้วดูเส้น
# กับดัก    : ถามแล้วเชื่อครั้งเดียวตอนต้นโปรแกรม คือการรายงานสถานะของอดีต
#             จอที่แขวนอยู่หน้างานต้องตอบเรื่องปัจจุบัน จึงต้องถามซ้ำทุกรอบ

import lcd
import time
import ui
import wifi

# แก้สองบรรทัดนี้ให้ตรงกับเครือข่ายที่ผู้สอนแจก
WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"

WATCH_MS = 30000     # เฝ้าดูลิงก์นานเท่าไร
TICK_MS = 1000       # ถามซ้ำทุกกี่ ms

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

ui.screen()
time.sleep_ms(200)

ui.Label("ต่อติดแล้ว แต่ยังต่ออยู่ไหม", x=24, y=8, color=COL_TEXT, value=24)

# ป้าย IP อยู่มุมขวาบน ที่เดิมตลอดทั้งโปรแกรม คนที่เดินผ่านมาจึงตอบได้ในสายตาเดียว
# ว่าบอร์ดออนไลน์อยู่หรือไม่ โดยไม่ต้องรอให้มีอะไรเกิดขึ้นก่อน
# วางที่ x=480 เพราะหัวเรื่องขนาด 28 กินถึงราว x=424 แล้ว
ip_lbl = ui.Label("ยังไม่ได้ต่อ", x=480, y=16, color=COL_WARN, value=20)

ui.Panel(x=24, y=56, w=744, h=160, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
ui.Label("สถานะลิงก์", x=40, y=72, color=COL_DIM, value=16)
link_lbl = ui.Label("ยังไม่ได้ตรวจ", x=40, y=104, color=COL_DIM, value=28)

ui.Label("หลุดไปแล้ว (ครั้ง)", x=456, y=72, color=COL_DIM, value=16)
# ตัวนับไม่ใช่สถานะ จึงเริ่มด้วยสีข้อความ ไม่ใช่สีเขียว - ถ้าทาเขียวไว้ตั้งแต่ต้น
# ตาจะอ่านว่า "ยืนยันแล้วว่าปกติ" ทั้งที่ยังไม่ได้ตรวจอะไรเลยสักรอบ
drop_lbl = ui.Label("0", x=456, y=104, color=COL_TEXT, value=28)

ui.Label("ออนไลน์มาแล้ว (วินาที)", x=40, y=152, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=304, y=144, w=144, h=56, color=COL_DIM)

# คำอธิบายเส้นวางไว้เหนือกราฟ ไม่ใช่ใต้กราฟ เพราะแถบล่างสุดเป็นที่ของบรรทัดสถานะ
# และข้อความบอกด้วยคำว่า "สูง/ต่ำ" ไม่ใช่ด้วยชื่อสี จอขาวดำก็ยังอ่านออก (§S7.7.1)
ui.Label("เส้นสูง = ต่ออยู่  เส้นต่ำ = หลุด", x=24, y=224, color=COL_DIM,
         value=16)
hist_lbl = ui.Label("ประวัติอยู่ในลิ้นชัก Console", x=440, y=224, color=COL_DIM,
                    value=20)

# เส้นกราฟค่าเดียวใช้สีเน้นเสมอ ไม่ใช่สีเขียว - เขียวสงวนไว้บอกว่า "ปกติ" เท่านั้น
chart = ui.Chart(x=24, y=256, w=744, h=80, color=COL_CARD, min=0, max=100)
s_link = chart.add_series(COL_ACCENT)

state = ui.Label("กำลังเริ่ม", x=24, y=352, color=COL_DIM, value=16)

# เคาะให้ป้ายทั้งชุดขึ้นจอก่อนเข้าบรรทัดที่บล็อก ถ้าลืม คนดูจะเห็นจอว่างตลอดช่วงที่รอ
ui.poll()

lcd.clear()
lcd.console("<h2>ประวัติของลิงก์</h2>")
lcd.console("<span class=muted>บรรทัดจะเพิ่มเฉพาะตอนสถานะเปลี่ยน</span>")

# --- ขั้นที่หนึ่ง: ต่อ ตามท่าเดียวกับไฟล์ 01 ของคาบนี้ ---
state.text("กำลังต่อเน็ต จอจะนิ่งสักครู่")
state.color(COL_WARN)
ui.poll()

if not wifi.connect(WIFI_SSID, WIFI_PASS):
    ip_lbl.text("ต่อไม่ติด")
    ip_lbl.color(COL_BAD)
    link_lbl.text("ต่อไม่ติด")
    link_lbl.color(COL_BAD)
    state.text("ตรวจชื่อวงกับรหัสผ่านอีกครั้ง แล้วรันใหม่")
    state.color(COL_BAD)
    ui.poll()
    lcd.print("<span class=error>ต่อไม่ติด - ยังไม่ได้เริ่มเฝ้าดู</span>")
    raise SystemExit

ip = wifi.ip()
ip_lbl.text("IP " + ip)
ip_lbl.color(COL_OK)
lcd.print("<span class=ok>ต่อสำเร็จ IP", ip, "</span>")

# --- ขั้นที่สอง: เฝ้าดู ถามซ้ำทุกรอบ ไม่เชื่อคำตอบเดิม ---
state.text("กำลังเฝ้าดู - ลองปิด WiFi ที่เราเตอร์ดูได้")
state.color(COL_DIM)

t0 = time.ticks_ms()
online_ms = 0
drops = 0

# -1 แปลว่า "ยังไม่เคยรู้สถานะมาก่อน" รอบแรกจึงนับเป็นการเปลี่ยนเสมอ
# ถ้าตั้งต้นเป็น 1 ประวัติจะไม่มีบรรทัดแรกบอกว่าเริ่มต้นที่สถานะไหน
last = -1

while True:
    t_work = time.ticks_ms()
    elapsed = time.ticks_diff(t_work, t0)
    if elapsed >= WATCH_MS:
        break

    # ถามใหม่ทุกรอบ นี่คือทั้งบทเรียนของไฟล์นี้
    up = wifi.is_connected()
    now = 1 if up else 0

    # --- งานของจอ: ตอบว่าตอนนี้เป็นยังไง ทำทุกรอบ ---
    if up:
        online_ms = online_ms + TICK_MS
        link_lbl.text("ต่ออยู่")
        link_lbl.color(COL_OK)
        seg.color(COL_OK)
        chart.set_next(s_link, 100)
    else:
        link_lbl.text("หลุด")
        link_lbl.color(COL_BAD)
        seg.color(COL_BAD)
        chart.set_next(s_link, 0)

    seg.text(str(online_ms // 1000))

    # --- งานของลิ้นชัก: ตอบว่าที่ผ่านมาเกิดอะไร ทำเฉพาะตอนมีเรื่องให้เล่า ---
    # ยิงทุกรอบเมื่อไร ลิ้นชักจะมีแต่บรรทัดเดิมซ้ำกันสามสิบบรรทัด
    # แล้วคำถามว่า "หลุดตอนวินาทีที่เท่าไร" จะหาคำตอบไม่เจอในกองนั้น
    if now != last:
        last = now
        if up:
            lcd.print("<span class=ok>" + str(elapsed // 1000) +
                      " s  ลิงก์กลับมา  IP " + wifi.ip() + "</span>")
        else:
            drops = drops + 1
            drop_lbl.text(str(drops))
            drop_lbl.color(COL_BAD)
            lcd.print("<span class=error>" + str(elapsed // 1000) +
                      " s  ลิงก์หลุด</span>")

    ui.poll()

    # ลูปเดินตรงจังหวะตามท่าเดียวกับ examples/s01/07_ticks_and_beat.py
    work = time.ticks_diff(time.ticks_ms(), t_work)
    left = TICK_MS - work
    if left > 0:
        time.sleep_ms(left)

# จบแล้วปล่อยค่าสุดท้ายค้างไว้ ไม่ล้างจอ คนดูจะได้อ่านทัน
pct_up = online_ms * 100 // WATCH_MS
state.text("จบแล้ว - ต่ออยู่ " + str(pct_up) + "% ของเวลาที่เฝ้าดู")
state.color(COL_OK if drops == 0 else COL_WARN)
# ป้ายใบนี้อยู่ครึ่งขวาของจอ ข้อความตอนจบจึงต้องสั้นกว่าตอนสร้าง ไม่ใช่ยาวกว่า
# ไม่งั้นมันจะยื่นพ้นขอบขวาไปโดยไม่มี error ให้จับ
hist_lbl.text("หลุด " + str(drops) + " ครั้ง - ดูในลิ้นชัก")
ui.poll()

lcd.console("<span class=muted>------------------------</span>")
lcd.print("<span class=ok>ต่ออยู่", pct_up, "% | หลุด", drops, "ครั้ง</span>")
print("uptime", pct_up, "% | drops", drops, "| ip", wifi.ip())

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# ระหว่างที่โปรแกรมกำลังเฝ้าดู ให้เดินถือบอร์ดออกไปไกลจากเราเตอร์จนสุดห้อง
# แล้วเดินกลับมา เปิดลิ้นชักดูว่าได้กี่บรรทัด และหลุดตอนวินาทีที่เท่าไร
# ใบ้: ถ้าลิ้นชักว่างเปล่า แปลว่าลิงก์ไม่เคยหลุดเลย ไม่ใช่แปลว่าโปรแกรมไม่ทำงาน
