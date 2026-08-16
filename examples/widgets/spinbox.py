# spinbox.py - ui.Spinbox ตัวเดียว
#
# ทำอะไร  : ช่องป้อนตัวเลขที่แก้ทีละหลัก min= max= คือพิสัย value= คือค่าตั้งต้น
#           .digits(จำนวนหลัก, หลักหน้าจุดทศนิยม) จัดรูปแบบการแสดงผล
# ดูที่จอ : ช่องซ้ายแสดง 2375 เป็นจำนวนเต็ม ช่องขวาแสดงเลขเดียวกันเป็น 23.75
#           ทั้งสองช่องเก็บ 2375 เท่ากัน ต่างกันแค่ตอนวาด
#
# แบบไหนคือพัง:
#   - สองช่องแสดงเหมือนกัน = .digits() ไปไม่ถึง
#   - ค่าเกินพิสัยได้ = lv_spinbox_set_range ไม่ทำงาน
#   - แตะลูกศรแล้วไม่มี event = callback ไม่ได้ลงทะเบียน
#
# ทำไมต้องมี: แถบเลื่อนป้อน 23.75 ไม่ได้ ช่องนี้ได้ และกันพิมพ์นอกพิสัยให้ด้วย
#   จุดทศนิยมเป็นเรื่องของการแสดงผลล้วน ๆ ค่าที่โปรแกรมได้ยังเป็นจำนวนเต็ม
#
# รันจบเองใน 20 วินาที ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์

import time
import ui

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF

ui.screen()
time.sleep_ms(200)

ui.Label("ui.Spinbox", x=24, y=16, color=COL_TEXT, value=28)

ui.Label("จำนวนเต็ม", x=24, y=80, color=COL_DIM, value=20)
sp_int = ui.Spinbox(x=24, y=112, w=336, h=96, color=COL_TEXT, min=0, max=9999,
                    value=2375)

ui.Label("เลขเดียวกัน สองหลักหลังจุด", x=392, y=80, color=COL_DIM, value=20)
sp_dec = ui.Spinbox(x=392, y=112, w=336, h=96, color=COL_TEXT, min=0, max=9999,
                    value=2375)
sp_dec.digits(4, 2)

note = ui.Label("ทั้งสองช่องเก็บ 2375 เท่ากัน", x=24, y=240, color=COL_TEXT,
                value=24)
ui.Label("จุดทศนิยมเป็นเรื่องของการวาด", x=24, y=344, color=COL_DIM, value=20)

for _ in range(100):
    for ev in ui.poll():
        if ev["type"] == "value_changed":
            note.text("ค่าใหม่ " + str(ev["value"]))
            print("spinbox handle", ev["handle"], "value", ev["value"])
    time.sleep_ms(200)

print("ui.Spinbox: .digits(4, 2) ทำให้ 2375 วาดเป็น 23.75 แต่ค่ายังเป็น 2375")
