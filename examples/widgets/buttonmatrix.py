# buttonmatrix.py - ui.ButtonMatrix ตัวเดียว
#
# ทำอะไร  : แผงปุ่มหลายปุ่มในหนึ่ง widget เติมด้วย .add_button(ข้อความ, ขึ้นแถวใหม่)
#           กินแฮนเดิลใบเดียวไม่ว่าจะมีกี่ปุ่ม ต่างจากการวาง ui.Button ทีละใบ
# ดูที่จอ : เก้าปุ่มไทยจัดสามแถว แถวละสามปุ่ม เต็มความกว้างของ widget
#
# แบบไหนคือพัง:
#   - ปุ่มเป็นกล่องสี่เหลี่ยมแทนตัวอักษร = ฟอนต์ไทยไม่ได้ตั้งที่ LV_PART_ITEMS
#     (แผงปุ่มวาดตัวอักษรจากส่วน ITEMS เหมือน Table)
#   - ทุกปุ่มอยู่แถวเดียวกันหมด = ธง "ขึ้นแถวใหม่" ไม่ทำงาน
#   - แผงว่างเปล่า = .add_button() ไปไม่ถึง CM55
#
# ต่างจาก Keyboard: แผงนี้ "ส่ง event" กลับมาเป็น value_changed พร้อมลำดับปุ่ม
# จึงเป็นทางเดียวในไลบรารีนี้ที่รับตัวอักษรจากนิ้วแล้วเอาไปใช้ต่อในโปรแกรมได้
#
# รันจบเองใน 20 วินาที ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์

import time
import ui

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF

KEYS = ("เดินหน้า", "หยุด", "ถอยหลัง",
        "เร็วขึ้น", "คงที่", "ช้าลง",
        "บันทึก", "ล้างค่า", "ออก")

ui.screen()
time.sleep_ms(200)

ui.Label("ui.ButtonMatrix", x=24, y=16, color=COL_TEXT, value=28)

bm = ui.ButtonMatrix(x=24, y=72, w=744, h=264, color=COL_TEXT)
for i, key in enumerate(KEYS):
    # ปุ่มแรกของแถวสั่งขึ้นแถวใหม่ ยกเว้นปุ่มแรกสุดของทั้งแผง
    bm.add_button(key, i > 0 and i % 3 == 0)

ui.Label("เก้าปุ่ม แต่ใช้แฮนเดิลใบเดียว", x=24, y=344, color=COL_DIM, value=20)

seen = 0
for _ in range(100):
    for ev in ui.poll():
        if ev["handle"] == bm.id():
            seen += 1
            print("แตะปุ่มลำดับที่", ev["value"], KEYS[ev["value"]]
                  if 0 <= ev["value"] < len(KEYS) else "?")
    time.sleep_ms(200)

print("ui.ButtonMatrix: แตะไป", seen, "ครั้ง - event คือ value_changed พร้อมลำดับปุ่ม")
