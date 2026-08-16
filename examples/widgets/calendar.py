# calendar.py - ui.Calendar ตัวเดียว
#
# ทำอะไร  : ปฏิทินที่แตะเลือกวันได้ min= คือปี max= คือเดือน value= คือวัน
#           .month(ปี, เดือน) พลิกไปเดือนอื่นจากโปรแกรม
#           event เป็น value_changed ที่ค่าคือเลขแปดหลัก YYYYMMDD ก้อนเดียว
# ดูที่จอ : ปฏิทินหนึ่งเดือน มีลูกศรเปลี่ยนเดือนที่หัว วันที่ตั้งไว้มีกรอบล้อม
#           อีกแปดวินาทีต่อมาโปรแกรมสั่งพลิกเดือนเอง
#
# แบบไหนคือพัง:
#   - ปฏิทินขึ้นเดือน มกราคม 2026 ทั้งที่ขอเดือนอื่น = ค่าที่ให้อยู่นอกพิสัย
#     แล้วถูกถอยไปใช้ค่าปริยายเงียบ ๆ (ปีต้องอยู่ 1900-2200 เดือน 1-12 วัน 1-31)
#   - ไม่มีลูกศรที่หัวปฏิทิน = ส่วนหัวไม่ได้ถูกเพิ่ม
#   - แตะวันแล้วไม่มี event = callback ไม่ได้ลงทะเบียน
#
# กับดักที่ต้องรู้:
#   1) min กับ max ที่นี่ "ไม่ใช่ช่วงค่า" อย่าง Slider หรือ Bar แต่คือ ปี กับ เดือน
#   2) ลูกศรเปลี่ยนเดือนที่หัว "ไม่ส่ง event" - เฟิร์มแวร์กรองทิ้ง เพราะการพลิกดู
#      เดือนไม่ใช่การเลือกวัน รู้ได้เฉพาะตอนคนแตะ "วัน" เท่านั้น
#   3) ชื่อวันในสัปดาห์กับตัวเลขวันเป็นของ LVGL ไม่ได้ผ่านฟอนต์ไทย จึงเป็น
#      ภาษาอังกฤษเสมอ - ข้อความไทยของหน้าจอนี้ต้องอยู่ในป้ายรอบ ๆ
#
# รันจบเองใน 24 วินาที ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์

import time
import ui

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_OK = 0x30A46C

ui.screen()
time.sleep_ms(200)

ui.Label("ui.Calendar", x=24, y=16, color=COL_TEXT, value=28)

cal = ui.Calendar(x=24, y=72, w=320, h=264, color=COL_TEXT,
                  min=2026, max=8, value=15)

note = ui.Label("ยังไม่มีใครแตะวัน", x=392, y=112, color=COL_DIM, value=24)
ui.Label("แตะวันแล้วได้เลขแปดหลัก", x=392, y=160, color=COL_DIM, value=20)
ui.Label("กดลูกศรที่หัวไม่ได้ event", x=392, y=200, color=COL_DIM, value=20)
ui.Label("ชื่อวันเป็นอังกฤษเสมอ", x=24, y=344, color=COL_DIM,
         value=20)

MONTHS = ((2026, 8), (2026, 9), (2026, 10))

for i in range(60):
    if i % 20 == 0:
        y, m = MONTHS[(i // 20) % len(MONTHS)]
        cal.month(y, m)

    for ev in ui.poll():
        if ev["handle"] == cal.id() and ev["type"] == "value_changed":
            ymd = ev["value"]
            note.text("%04d-%02d-%02d" % (ymd // 10000, (ymd // 100) % 100,
                                          ymd % 100))
            note.color(COL_OK)
            print("แตะวันที่", ymd)
    time.sleep_ms(400)

print("ui.Calendar: min=ปี max=เดือน value=วัน, event คือ YYYYMMDD ก้อนเดียว")
