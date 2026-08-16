# menu.py - ui.Menu ตัวเดียว
#
# ทำอะไร  : กองหน้าซ้อนกัน แตะแถวแล้วลงไปหน้าลูก มีปุ่มย้อนกลับให้เอง
#           .add_page("ชื่อ") คืนหน้า - หน้าแรกที่สร้างคือหน้าที่เมนูเปิดให้
#           page.section() คืนกล่องจัดกลุ่ม แล้ว section.row("ข้อความ") คืนแถว
#           row.opens(page) คือเส้นที่เชื่อมแถวกับหน้า
#           value=1 เปิดปุ่มย้อนกลับตั้งแต่หน้าราก
# ดูที่จอ : เมนูสามแถวไทย มีลูกศรย้อนกลับที่หัว แตะแถวแล้วเข้าไปหน้าลูก
#
# แบบไหนคือพัง:
#   - หัวเรื่องหรือแถวเป็นกล่องสี่เหลี่ยม = ฟอนต์ไทยไปไม่ถึงหัวเมนูหรือป้ายในแถว
#   - ลูกศรย้อนกลับเป็นกล่องสี่เหลี่ยม = ฟอนต์ไทยไปทับฟอนต์สัญลักษณ์ของลูกศร
#   - แตะแถวแล้วไม่ไปไหน = ลืม .opens() หรือส่ง section ไปแทน page
#     (CM55 รับเฉพาะแฮนเดิลชนิดหน้าเท่านั้น อย่างอื่นเงียบสนิท)
#   - เมนูว่างเปล่าตั้งแต่แรก = ไม่ได้สร้างหน้าไหนเลย
#
# กับดักที่ต้องรู้: เมนู "ไม่ส่ง event" เลยสักตัว ทั้งตัวเมนูและแถว
#   การแตะแถวถูกจัดการจบภายใน LVGL โปรแกรมจึงไม่มีทางรู้ว่าคนเปิดหน้าไหนอยู่
#
# รันจบเองใน 20 วินาที ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์

import time
import ui

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_CARD = 0x171B22

ui.screen()
time.sleep_ms(200)

ui.Label("ui.Menu", x=24, y=16, color=COL_TEXT, value=28)

menu = ui.Menu(x=24, y=72, w=440, h=248, color=COL_CARD, value=1)

page_root = menu.add_page("ตั้งค่าเครื่อง")     # หน้าแรก = หน้าที่เมนูเปิดให้
page_net = menu.add_page("เครือข่าย")
page_about = menu.add_page("เกี่ยวกับเครื่อง")

sec = page_root.section()
row_net = sec.row("เครือข่าย")
sec.separator()
row_about = sec.row("เกี่ยวกับเครื่อง")

row_net.opens(page_net)
row_about.opens(page_about)

sec_net = page_net.section()
sec_net.row("วง AIoT-Class")
sec_net.row("ต่ออัตโนมัติ")

sec_about = page_about.section()
sec_about.row("รุ่น Eva Kit EPC2")

ui.Label("แตะแถวเข้าหน้าลูก", x=496, y=112, color=COL_TEXT, value=24)
ui.Label("กดลูกศรที่หัวเพื่อกลับ", x=496, y=160, color=COL_DIM, value=20)
ui.Label("เมนูไม่ส่ง event กลับมาเลย", x=24, y=344, color=COL_DIM, value=20)

events = 0
for _ in range(100):
    for _ev in ui.poll():
        events += 1
    time.sleep_ms(200)

# ต้องเป็นศูนย์ ต่อให้เดินเข้าออกทุกหน้าจนครบ
print("ui.Menu: event ที่ได้รับตลอดการรัน =", events, "(ควรเป็น 0)")
print(".add_page() -> .section() -> .row() -> .opens()")
