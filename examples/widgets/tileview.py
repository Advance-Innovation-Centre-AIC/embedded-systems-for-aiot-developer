# tileview.py - ui.Tileview ตัวเดียว
#
# ทำอะไร  : ผืนใหญ่ที่แบ่งเป็นช่อง แล้วปัดนิ้วเลื่อนไปทีละช่อง
#           .add_tile(คอลัมน์, แถว, ทิศที่ปัดออกได้) คืน "ช่อง" มาหนึ่งช่อง
#           เอาช่องนั้นไปใส่ parent= ของ widget ที่จะอยู่ในช่องนั้น
# ดูที่จอ : เห็นช่องแรกเต็มพื้นที่ ปัดไปทางซ้ายเจอช่องที่สอง ปัดขึ้นเจอช่องที่สาม
#
# แบบไหนคือพัง:
#   - เห็นของทุกช่องซ้อนกันหมด = ช่องไม่ได้ถูกสร้าง ของเลยไปกองที่ผืนเดียวกัน
#   - ปัดแล้วไม่ไปไหน = ทิศที่อนุญาตถูกตั้งผิด
#   - ช่องว่างเปล่า = parent= ไม่ได้ถูกส่งไปกับ widget ลูก
#
# ต่างจาก Tabview: Tabview มีแถบให้แตะ คนจึงเห็นว่ามีอะไรอยู่บ้าง
#   ส่วน Tileview ไม่มีป้ายบอก ต้องรู้เองว่าปัดได้ - เหมาะกับของที่คนใช้ทุกวัน
#   ไม่เหมาะกับหน้าจอที่คนแปลกหน้าต้องหาของให้เจอในสามวินาที
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

ui.Label("ui.Tileview", x=24, y=16, color=COL_TEXT, value=28)

tv = ui.Tileview(x=24, y=72, w=744, h=248, color=COL_CARD)

# (คอลัมน์, แถว) และทิศที่ปัดออกจากช่องนั้นได้
t_home = tv.add_tile(0, 0, ui.DIR_RIGHT | ui.DIR_BOTTOM)
t_right = tv.add_tile(1, 0, ui.DIR_LEFT)
t_down = tv.add_tile(0, 1, ui.DIR_TOP)

# พิกัดของลูกนับจากมุมซ้ายบนของช่อง ไม่ใช่ของจอ
ui.Label("ช่องแรก - ปัดซ้ายหรือขึ้น", x=24, y=24, color=COL_TEXT, value=24,
         parent=t_home)
ui.Label("กราฟรวมอยู่ที่นี่ได้", x=24, y=72, color=COL_DIM, value=20,
         parent=t_home)

ui.Label("ช่องขวา - รายละเอียด", x=24, y=24, color=COL_ACCENT, value=24,
         parent=t_right)
ui.Label("ช่องล่าง - ประวัติ", x=24, y=24, color=COL_ACCENT, value=24,
         parent=t_down)

ui.Label("สามช่อง กินสี่แฮนเดิล", x=24, y=344, color=COL_DIM,
         value=20)

for _ in range(100):
    ui.poll()
    time.sleep_ms(200)

print("ui.Tileview: .add_tile(col, row, dir) คืนช่อง แล้วส่งช่องนั้นเป็น parent=")
