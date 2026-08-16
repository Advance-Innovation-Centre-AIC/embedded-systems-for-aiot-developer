# picture.py - ui.Picture ตัวเดียว
#
# ทำอะไร  : ภาพที่ "หมุนและย่อขยายได้" text= คือชื่อไอคอนในตัว
#           ชื่อที่มีจริงห้าชื่อ: heart star flag trophy skull
#           .prop(ui.PROP_ROTATION, มุม) หมุนเป็นหน่วยหนึ่งในสิบองศา
#           .prop(ui.PROP_SCALE, ขนาด) ย่อขยาย โดย 256 คือขนาดเดิม
# ดูที่จอ : ภาพสามใบเรียงกัน ใบซ้ายอยู่นิ่ง ใบกลางค่อย ๆ หมุน ใบขวาค่อย ๆ โตขึ้น
#
# แบบไหนคือพัง:
#   - ไม่มีภาพเลย = ชื่อไอคอนสะกดผิด CM55 ปฏิเสธทั้ง CREATE ถ้าหาชื่อไม่เจอ
#   - ภาพอยู่นิ่งทั้งสามใบ = prop หมุน/ย่อขยาย ไปไม่ถึง
#   - ภาพหมุนแล้วมุมโดนตัด = จุดหมุนไม่ได้อยู่กลางภาพ
#
# ต่างจาก ui.Image: ui.Image เป็น lv_canvas ซึ่งวาดได้แต่หมุนไม่ได้
#   ส่วน ui.Picture เป็น lv_image ตัวจริง สองตัวนี้คนละชนิดกัน
#
# รันจบเองใน 24 วินาที ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์

import time
import ui

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_ACCENT = 0x4A9EFF
COL_WARN = 0xF5A623

ui.screen()
time.sleep_ms(200)

ui.Label("ui.Picture", x=24, y=16, color=COL_TEXT, value=28)

# เว้นที่รอบภาพเผื่อไว้ - ภาพที่หมุนหรือขยายจะกินพื้นที่เกินกรอบเดิมออกไป
# รอบตัว โดยที่ x y w h ไม่เปลี่ยน ป้ายที่วางชิดเกินไปจะถูกทับ
# กรอบสูง 128 ที่ขยาย 1.5 เท่า จะสูงจริง 192 คือล้นบนล่างด้านละ 32
# ป้ายจึงอยู่ที่ y=296 ไม่ใช่ 288 - ครั้งแรกวางไว้ 288 แล้วขอบภาพใบขวา
# ไปแตะตัวอักษรพอดี เห็นตอนเปิดภาพดู ประตูตรวจพิกัดมองไม่เห็น
pic_still = ui.Picture(text="heart", x=48, y=96, w=128, h=128,
                       color=COL_ACCENT)
pic_spin = ui.Picture(text="star", x=304, y=96, w=128, h=128, color=COL_WARN)
pic_grow = ui.Picture(text="trophy", x=560, y=96, w=128, h=128,
                      color=COL_ACCENT)

ui.Label("อยู่นิ่ง", x=48, y=296, color=COL_DIM, value=20)
ui.Label("หมุน", x=304, y=296, color=COL_DIM, value=20)
ui.Label("ย่อขยาย", x=560, y=296, color=COL_DIM, value=20)

ui.Label("ชื่อไอคอนมีห้าชื่อเท่านั้น", x=24, y=344, color=COL_DIM,
         value=20)

for i in range(60):
    # มุมเป็นหน่วยหนึ่งในสิบองศา 3600 จึงเท่ากับหนึ่งรอบ
    pic_spin.prop(ui.PROP_ROTATION, (i * 60) % 3600)
    # 256 คือขนาดเดิม ไล่จาก 128 (ครึ่งหนึ่ง) ถึง 384 (หนึ่งเท่าครึ่ง)
    pic_grow.prop(ui.PROP_SCALE, 128 + i * 256 // 59)
    ui.poll()
    time.sleep_ms(400)

print("ui.Picture: text=ชื่อไอคอน, PROP_ROTATION หน่วย 0.1 องศา, PROP_SCALE 256=เท่าเดิม")
