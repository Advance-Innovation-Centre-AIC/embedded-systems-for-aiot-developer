# list.py - ui.List ตัวเดียว
#
# ทำอะไร  : รายการที่เลื่อนได้ text= ตอนสร้างคือหัวข้อของกลุ่ม ส่วนแต่ละบรรทัด
#           เติมด้วย .add_item(ข้อความ, รหัสไอคอน) - ไอคอนเป็นเลข ไม่ใช่สตริง
# ดูที่จอ : หัวข้อไทยหนึ่งบรรทัด แล้วห้าบรรทัดที่แต่ละบรรทัดมีไอคอนนำหน้า
#           บรรทัดสุดท้ายโผล่มาครึ่งเดียว เพราะรายการยาวกว่ากรอบและเลื่อนได้
#
# แบบไหนคือพัง:
#   - ไอคอนกลายเป็นกล่องสี่เหลี่ยม = ฟอนต์ไทยไปทับฟอนต์สัญลักษณ์
#     (ฟอนต์ไทยไม่มีบล็อก LV_SYMBOL เฟิร์มแวร์ต้องปักฟอนต์ไอคอนไว้แยก)
#   - ข้อความไทยเป็นกล่อง = ฟอนต์ไทยไปไม่ถึงบรรทัดของรายการ
#   - รายการว่าง = .add_item() ไปไม่ถึง CM55
#
# รันจบเองใน 20 วินาที ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์

import time
import ui

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF

ui.screen()
time.sleep_ms(200)

ui.Label("ui.List", x=24, y=16, color=COL_TEXT, value=28)

ls = ui.List(text="อุปกรณ์ที่ต่ออยู่", x=24, y=72, w=440, h=264,
             color=COL_TEXT)
ls.add_item("เครือข่ายไร้สาย", ui.ICON_WIFI)
ls.add_item("แบตเตอรี", ui.ICON_BATTERY)
ls.add_item("การแจ้งเตือน", ui.ICON_BELL)
ls.add_item("ตั้งค่า", ui.ICON_SETTINGS)
ls.add_item("เสียง", ui.ICON_AUDIO)

ui.Label("ไอคอนคือ ui.ICON_*", x=496, y=72, color=COL_DIM, value=20)
ui.Label("ไม่ใช่ตัวอักษร", x=496, y=112, color=COL_DIM, value=20)
ui.Label("รายการเลื่อนได้", x=496, y=152, color=COL_DIM, value=20)

ui.Label("หัวข้อมาจาก text= ตอนสร้าง", x=24, y=344, color=COL_DIM, value=20)

for _ in range(100):
    ui.poll()
    time.sleep_ms(200)

print("ui.List: text=หัวข้อกลุ่ม, .add_item(ข้อความ, ui.ICON_*)")
