# win.py - ui.Win ตัวเดียว
#
# ทำอะไร  : หน้าต่างที่มีแถบหัวเรื่องในตัว text= คือข้อความบนแถบหัว
#           .content() คืน "ตัวหน้าต่าง" ซึ่งคือที่ที่ของข้างในไปอยู่
#           เรียก .content() ซ้ำได้ ได้แฮนเดิลเดิมทุกครั้ง
# ดูที่จอ : กรอบหน้าต่างมีแถบหัวไทยด้านบน ข้างในมีสามบรรทัดและหนึ่งปุ่ม
#
# แบบไหนคือพัง:
#   - แถบหัวว่างเปล่า = text= ไปไม่ถึง
#   - หัวเรื่องไทยเป็นกล่องสี่เหลี่ยม = ฟอนต์ไทยไปไม่ถึงแถบหัว
#   - ของข้างในไปโผล่นอกกรอบ = ลืมส่ง parent=body
#
# ต่างจาก ui.Panel: Panel เป็นแค่พื้นสี่เหลี่ยม ไม่มีหัวเรื่อง
#   ส่วน Win มีแถบหัวที่เฟิร์มแวร์จัดให้ เหมาะกับกล่องที่ต้องมีชื่อกำกับ
#
# รันจบเองใน 20 วินาที ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์

import time
import ui

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF

ui.screen()
time.sleep_ms(200)

ui.Label("ui.Win", x=24, y=16, color=COL_TEXT, value=28)

wn = ui.Win(text="รายงานประจำกะ", x=24, y=72, w=744, h=248, color=COL_CARD)
body = wn.content()

# พิกัดของลูกนับจากมุมซ้ายบนของตัวหน้าต่าง ไม่ใช่ของจอ และไม่รวมแถบหัว
ui.Label("เดินเครื่อง 7 ชั่วโมง 20 นาที", x=24, y=16, color=COL_TEXT,
         value=24, parent=body)
ui.Label("หยุดฉุกเฉิน 0 ครั้ง", x=24, y=56, color=COL_TEXT, value=24,
         parent=body)
ui.Label("แจ้งเตือน 3 ครั้ง", x=24, y=96, color=COL_TEXT, value=24,
         parent=body)
ui.Button("รับทราบ", x=432, y=40, w=232, h=88, color=COL_ACCENT, value=24,
          parent=body)

ui.Label("content() คือที่ที่ของข้างในไปอยู่", x=24, y=344, color=COL_DIM,
         value=20)

for _ in range(100):
    for ev in ui.poll():
        print("event", ev["type"], "handle", ev["handle"])
    time.sleep_ms(200)

print("ui.Win: text=แถบหัว, .content() คืนตัวหน้าต่าง เรียกซ้ำได้แฮนเดิลเดิม")
