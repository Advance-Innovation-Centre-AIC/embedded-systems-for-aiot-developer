# led.py - ui.Led ตัวเดียว
#
# ทำอะไร  : ไฟสถานะแบบแผงควบคุม color= คือสีตอนติด value= คือสถานะตั้งต้น
#           สั่ง .value(1) ให้ติด .value(0) ให้ดับ
# ดูที่จอ : สี่ดวงเรียงกัน สามดวงแรกติด ดวงที่สี่สั่งดับ
#           แล้วอีกสิบวินาทีต่อมาไล่สลับให้เห็นการเปลี่ยนสถานะ
#
# แบบไหนคือพัง:
#   - ดวงที่สั่งดับ "หายไปเลย" = ผิด ของถูกคือมันต้อง "หรี่ลง" แต่ยังเห็นวงกลม
#     เพราะไฟแผงควบคุมที่หายไปตอนดับ แยกไม่ออกจากจอเสีย
#   - ทุกดวงสีเดียวกัน = color= ไม่ทำงาน
#   - ไม่มีวงกลมเลย = สร้างไม่สำเร็จ
#
# ขนาด 88 พิกเซล ตามกติกาเป้าสัมผัส แม้ไฟจะไม่ใช่ของที่ต้องแตะ เพราะต้องเห็นชัด
# จากระยะยืน - ขั้นต่ำของไฟสถานะคือ 48 พิกเซล
#
# รันจบเองใน 24 วินาที ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์

import time
import ui

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_OK = 0x30A46C
COL_WARN = 0xF5A623
COL_BAD = 0xE5484D

ui.screen()
time.sleep_ms(200)

ui.Label("ui.Led", x=24, y=16, color=COL_TEXT, value=28)

led_ok = ui.Led(x=24, y=96, w=88, h=88, color=COL_OK, value=1)
led_warn = ui.Led(x=208, y=96, w=88, h=88, color=COL_WARN, value=1)
led_bad = ui.Led(x=392, y=96, w=88, h=88, color=COL_BAD, value=1)
led_off = ui.Led(x=576, y=96, w=88, h=88, color=COL_OK, value=0)

ui.Label("ปกติ", x=24, y=200, color=COL_DIM, value=20)
ui.Label("เฝ้าระวัง", x=208, y=200, color=COL_DIM, value=20)
ui.Label("ผิดปกติ", x=392, y=200, color=COL_DIM, value=20)
ui.Label("สั่งดับ", x=576, y=200, color=COL_DIM, value=20)

note = ui.Label("ดวงที่สี่ต้องหรี่ ไม่ใช่หาย", x=24, y=256, color=COL_TEXT,
                value=24)
ui.Label("แผงจริงให้ติดทีละดวง", x=24, y=344, color=COL_DIM, value=20)

# ไล่ให้ติดทีละดวง แบบที่แผงควบคุมจริงทำ - ติดพร้อมกันหลายดวงคือแผงที่อ่านไม่ออก
LAMPS = (led_ok, led_warn, led_bad)
for i in range(60):
    if i % 5 == 0:
        on = (i // 5) % 3
        for j, lamp in enumerate(LAMPS):
            lamp.value(1 if j == on else 0)
        note.text("ตอนนี้ติดดวงที่ " + str(on + 1) + " จากสามดวง")
    ui.poll()
    time.sleep_ms(400)

print("ui.Led: value(0) คือหรี่ ไม่ใช่หาย - lv_led_off ตั้งความสว่างขั้นต่ำ 80 จาก 255")
