# 05_short_long_press.py - ปุ่มเดียว สองความหมาย
#
# Why : หูฟังไร้สาย นาฬิกาวิ่ง และไฟฉายทุกยี่ห้อมีปุ่มเดียว แตะ = เปลี่ยนโหมด
#       กดค้าง = เปิด/ปิดเครื่อง ทุกปุ่มที่ต้องกันน้ำมีต้นทุนสูง วิศวกรจึงบีบ
#       หลายคำสั่งลงในปุ่มเดียวเสมอ
# What: ความหมายไม่ได้อยู่ที่ "กด" แต่อยู่ที่ "กดนานเท่าไร" เราจึงต้องจับเวลา
#       ตอนกดลง แล้วตัดสินตอนปล่อย ไม่ใช่ตอนกด
#
# ดูที่จอ: กราฟเส้นฟ้าคือปุ่มดิบ เส้นแดงคือคำตัดสิน สังเกตว่าเส้นแดงขยับ
#         ตอนปล่อยมือ ไม่ใช่ตอนกดลง - นั่นคือบทเรียนทั้งหมดของไฟล์นี้
#         ถ้ายังไม่มีใครกด จอจะบอกว่า "ยังไม่มีการกด" และสองเส้นจะนิ่ง
# กับดัก : ปุ่มนี้ active-low ค่า 1 คือปล่อย 0 คือกด และบอร์ดไม่มี debounce
#         ในฮาร์ดแวร์เลย ต้องกันเด้งด้วยเวลาในโค้ด ไม่งั้นหนึ่งกดจะนับได้หลายครั้ง

import gpio
import lcd
import time
import ui

LONG_MS = 1000     # กดนานกว่านี้ถือว่า "กดค้าง"
DEBOUNCE_MS = 25   # หน้าสัมผัสเด้งอยู่ไม่กี่มิลลิวินาที กันไว้เท่านี้พอ
SAMPLE_MS = 60     # ส่งจุดขึ้นกราฟทุกกี่มิลลิวินาที
HOLD_SHOW_MS = 800  # ให้คำตัดสินค้างบนกราฟนานเท่านี้ ตาจะได้ทัน

RAW_UP = 10        # ระดับที่วาดเมื่อปล่อย
RAW_DOWN = 70      # ระดับที่วาดเมื่อกด
DEC_SHORT = 35     # ระดับคำตัดสิน "แตะสั้น"
DEC_LONG = 85      # ระดับคำตัดสิน "กดค้าง"

btn = gpio.button(0)
n = gpio.num_leds()

lcd.clear()
lcd.console("<h2>ปุ่มเดียว สองความหมาย</h2>")
lcd.print("ปุ่ม", btn.name(), "| กดค้าง >", LONG_MS, "ms = ล้าง")

ui.screen()
# ผังจอสองคอลัมน์ ขอบนอก 24 - ซ้ายคือกราฟกับคำอธิบายเส้นและแถบสถานะ
# ขวาคือตัวเลขที่กำลังเดิน แถบความคืบหน้า และการ์ดคำตัดสิน
#
# ก่อนแก้ ป้ายคำอธิบายสองใบถูกวางไว้ที่ x=448 และ x=428 ซึ่งคาบเกี่ยวทั้งสอง
# คอลัมน์ แล้วการ์ดที่สร้างทีหลังก็ทาทับมันหายไปทั้งใบ ส่วนป้ายคำตัดสิน vd
# ถูกวางที่ y=336 ซึ่งอยู่ "ใต้" การ์ดของตัวเองที่จบไปแล้วตั้งแต่ y=320
ui.Label("ปุ่มเดียว สองความหมาย", x=24, y=8, value=24)
ch = ui.Chart(x=24, y=48, w=440, h=192, min=0, max=100, color=0x4A9EFF)
s_dec = ch.add_series(0xE5484D)
ui.Label("เส้นฟ้า = ระดับปุ่มดิบ (กด=สูง)", x=24, y=252, value=20,
         color=0x4A9EFF)
ui.Label("เส้นแดง = คำตัดสินตอนปล่อย", x=24, y=280, value=20, color=0xE5484D)

ui.Panel(x=24, y=316, w=440, h=60, color=0x171B22, min=0x171B22, value=2)
st = ui.Label("แตะสั้น = เลื่อนโหมด | กดค้าง = ล้าง", x=40, y=332, value=20)

ui.Label("กดค้างมาแล้ว (ms)", x=496, y=48, value=20)
seg = ui.Seg7(x=496, y=80, w=160, h=48)
seg.text("0")      # ตั้งค่าเริ่มต้นเอง ไม่งั้นจอขึ้น 0000 ตามค่าตั้งต้นของวิดเจ็ต

ui.Label("ความคืบหน้าสู่ 1000 ms", x=496, y=144, value=20)
bar = ui.Bar(x=496, y=176, w=256, h=24, min=0, max=LONG_MS, value=0)

ui.Label("แตะสั้น = ระดับกลาง", x=496, y=216, value=20)
ui.Label("กดค้าง = ระดับสูง", x=496, y=244, value=20)

# การ์ดคำตัดสิน จบที่ y=340 พอดี จึงพ้นมุมของปุ่ม Console (x>=690 และ y>=340)
ui.Panel(x=496, y=284, w=272, h=56, color=0x171B22, min=0x171B22, value=2)
vd = ui.Label("ยังไม่มีการกด", x=512, y=296, value=20, color=0x9AA3AF)
ui.poll()

mode = 0
gpio.led(mode).on()

prev = btn.value()          # 1 = ปล่อย
down_at = 0
last_edge = time.ticks_ms()
sample_at = time.ticks_ms()
dec_level = 0
dec_at = time.ticks_ms()

for _ in range(6000):
    now = time.ticks_ms()
    v = btn.value()

    # รับขอบเฉพาะเมื่อพ้นหน้าต่างกันเด้งแล้วเท่านั้น
    if v != prev and time.ticks_diff(now, last_edge) > DEBOUNCE_MS:
        last_edge = now
        if v == 0:
            down_at = now                      # ขอบขาลง = เริ่มจับเวลา
        else:
            held = time.ticks_diff(now, down_at)   # ขอบขาขึ้น = ตัดสิน
            dec_at = now
            if held >= LONG_MS:
                for i in range(n):
                    gpio.led(i).off()
                dec_level = DEC_LONG
                vd.text("กดค้าง -> ล้างทั้งหมด")
                vd.color(0xE5484D)
                st.text("กดค้าง " + str(held) + " ms -> ล้างทั้งหมด")
                lcd.print("กดค้าง", held, "ms -> ล้างทั้งหมด")
            else:
                gpio.led(mode).off()
                mode = (mode + 1) % n
                gpio.led(mode).on()
                dec_level = DEC_SHORT
                vd.text("แตะสั้น -> โหมด " + str(mode))
                vd.color(0x30A46C)
                st.text("แตะ " + str(held) + " ms -> โหมด " + str(mode))
                lcd.print("แตะ", held, "ms -> โหมด", mode)
        prev = v

    # แถบและ Seg7 ไต่ตามเวลาที่กดค้างอยู่ ผู้ใช้จะรู้ว่าอีกไกลแค่ไหน
    if v == 0:
        held_now = time.ticks_diff(now, down_at)
        seg.text(str(held_now))
        bar.value(int(min(held_now, LONG_MS)))
    elif time.ticks_diff(now, dec_at) >= HOLD_SHOW_MS:
        bar.value(0)

    # ส่งจุดขึ้นกราฟตามจังหวะคงที่ สองเส้นจึงมีจำนวนจุดเท่ากันเสมอ
    if time.ticks_diff(now, sample_at) >= SAMPLE_MS:
        sample_at = now
        ch.set_next(0, RAW_DOWN if v == 0 else RAW_UP)
        if time.ticks_diff(now, dec_at) >= HOLD_SHOW_MS:
            dec_level = 0
        ch.set_next(s_dec, dec_level)

    ui.poll()
    time.sleep_ms(5)

st.text("จบ - คำตัดสินเกิดตอนปล่อย ไม่ใช่ตอนกด")
print("จบ ลองสังเกตว่าคำตัดสินเกิดตอนปล่อย ไม่ใช่ตอนกด")
