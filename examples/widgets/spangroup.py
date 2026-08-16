# spangroup.py - ui.SpanGroup ตัวเดียว
#
# ทำอะไร  : ย่อหน้าเดียวที่ประกอบจาก "ท่อน" หลายท่อน แต่ละท่อนมีขนาด สี และ
#           เส้นใต้ของตัวเองได้ .pen(สี) ตั้งสีของท่อนที่จะเติมต่อจากนี้
#           .add_span(ข้อความ, ขนาด, ธง) เติมหนึ่งท่อน
#           ธงที่มี: ui.SPAN_UNDERLINE และ ui.SPAN_STRIKETHROUGH
# ดูที่จอ : ย่อหน้าไทยสี่บรรทัด บรรทัดหนึ่งมีคำที่ตัวใหญ่กว่าเพื่อน
#           บรรทัดหนึ่งมีเส้นใต้ บรรทัดหนึ่งมีเส้นขีดกลาง
#
# แบบไหนคือพัง:
#   - ท่อนที่ขอขนาดพิเศษเป็นกล่องสี่เหลี่ยม แต่ท่อนอื่นเป็นตัวอักษร
#     = บันไดขนาดฟอนต์มีแต่ตัวละติน ไม่มีตัวไทย นี่คือของเดิมที่แก้ไปแล้ว
#     ถ้ากลับมาอีกแปลว่าการเลือกฟอนต์ของท่อนถูกแก้ผิด
#   - ทุกท่อนสีเดียวกันหมด = .pen() ไปไม่ถึง
#   - แก้แล้วจอไม่เปลี่ยน = ลืมสั่งให้ย่อหน้าจัดเรียงใหม่หลังแก้
#
# กับดักที่ต้องรู้: .pen() เป็นปากกา มีผลกับท่อนที่เติม "หลังจากนั้น" เท่านั้น
#   และท่อนไม่มีแฮนเดิล จึงแก้ทีละท่อนไม่ได้ ต้อง .clear_items() แล้วเขียนใหม่
#   ส่วน .text() บน SpanGroup คือคำสั่งล้างทุกท่อนทิ้งแล้วเหลือท่อนเดียว
#
# รันจบเองใน 20 วินาที ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์

import time
import ui

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_WARN = 0xF5A623
COL_BAD = 0xE5484D

ui.screen()
time.sleep_ms(200)

ui.Label("ui.SpanGroup", x=24, y=16, color=COL_TEXT, value=28)

ui.Panel(x=24, y=72, w=744, h=248, color=COL_CARD, max=8)
sg = ui.SpanGroup(x=48, y=96, w=696, h=200, color=COL_TEXT)

sg.pen(COL_DIM)
sg.add_span("ขนาดต่างกัน ", 20)
sg.pen(COL_TEXT)
sg.add_span("ท่อนนี้ใหญ่กว่า", 28)
sg.add_span("\n", 20)

sg.pen(COL_WARN)
sg.add_span("เฝ้าระวัง", 24, ui.SPAN_UNDERLINE)
sg.pen(COL_TEXT)
sg.add_span("  ท่อนนี้มีเส้นใต้\n", 24)

sg.pen(COL_BAD)
sg.add_span("ยกเลิกแล้ว", 24, ui.SPAN_STRIKETHROUGH)
sg.pen(COL_TEXT)
sg.add_span("  ท่อนนี้มีเส้นขีดกลาง\n", 24)

sg.pen(COL_ACCENT)
sg.add_span("สีมาจากปากกาที่ตั้งไว้ก่อนเติม", 24)

ui.Label("เส้นใต้รอดจากภาพขาวดำ สีไม่รอด", x=24, y=344, color=COL_DIM,
         value=20)

for _ in range(100):
    ui.poll()
    time.sleep_ms(200)

print("ui.SpanGroup: .pen() ทาท่อนถัดไป, .add_span(ข้อความ, ขนาด, ธง), .clear_items() ล้างทั้งย่อหน้า")
