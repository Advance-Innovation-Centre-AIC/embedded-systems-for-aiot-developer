# 06_button_picks_led.py - ปุ่มเดียวคุมไฟสามดวง ด้วยการจำสถานะเอง
#
# Why : รีโมทแอร์มีปุ่มไม่กี่ปุ่ม แต่คุมได้หลายสิบสถานะ เพราะปุ่มไม่ได้แปลว่า "ทำสิ่งนี้"
#         แต่แปลว่า "เลื่อนไปสถานะถัดไป" ปุ่มเดียวจึงพอ ถ้าโปรแกรมจำได้ว่าตอนนี้อยู่ไหน
# What: บอร์ดนี้เปิดให้ Python ใช้ปุ่มเดียว แต่มีไฟสามดวง ทางออกคือตัวแปร current
#         ที่บอกว่าดวงไหนกำลังติด และปุ่มมีหน้าที่เดียวคือบวกหนึ่งแล้ววนกลับ
#
# ดูที่จอ: การ์ดบนบอกดัชนีกับชื่อดวงที่ติดอยู่ ขวามือคือสามแถวของหลอดทั้งสามดวง
#          ดวงที่ติดขึ้นเขียว ที่เหลือจาง ตรงกับหลอดจริงบนบอร์ดทุกครั้งที่กด
# กับดัก : gpio.led(i).value() อ่านกลับได้จริง แต่ตอบระดับของขา ณ วินาทีที่ถาม
#          brightness() กับ hold() จบด้วยขาต่ำเสมอ อ่านตามหลังไปจึงได้ 0
#          ทั้งที่เพิ่งเห็นหลอดสว่าง โปรแกรมที่พึ่งค่านั้นจะเถียงกับสายตาผู้ใช้

import gpio
import lcd
import time
import ui

DEBOUNCE_MS = 40
POLL_MS = 5
RUN_MS = 20000

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_INFO = 0x30A46C, 0xF5A623, 0x4A9EFF

lcd.clear()
lcd.console("<h2>ปุ่มเดียว ไฟสามดวง</h2>")

N = gpio.num_leds()
NAMES = gpio.board_info()["led_names"]
btn = gpio.button(0)

# ดับทุกดวงก่อน เพื่อให้สถานะจริงตรงกับตัวแปรที่เรากำลังจะตั้ง
for i in range(N):
    gpio.led(i).off()

ui.screen()
time.sleep_ms(200)

ui.Label("ปุ่มเดียว ไฟสามดวง", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=652, h=124, color=COL_CARD, min=COL_DIM, max=12, value=1)

ui.Label("ดัชนีที่ติดอยู่", x=40, y=64, color=COL_DIM, value=16)
seg = ui.Seg7(text="-", x=40, y=92, w=64, h=44, color=COL_DIM)
l_name = ui.Label("ยังไม่มีดวงไหนติด", x=112, y=96, color=COL_DIM, value=24)

rows = []
for i in range(N):
    rows.append(ui.Label(NAMES[i] + "   ดับ", x=400, y=64 + i * 32,
                         color=COL_DIM, value=20))

ui.Label("กดปุ่ม SW2 บนบอร์ด (โค้ดเรียก " + btn.name() + ") เพื่อเลื่อนดวงถัดไป",
         x=20, y=192, color=COL_DIM, value=16)
# แยกเป็นสองใบทั้งสองก้อน - ui.Label ตัดที่ 126 ไบต์ ไทยหนึ่งตัวกิน 3 ไบต์
ui.Label("current = (current + 1) % N", x=20, y=220, color=COL_DIM, value=16)
ui.Label("ทำให้ดวงสุดท้ายวนกลับดวงแรกเอง", x=328, y=220, color=COL_DIM,
         value=16)
ui.Label("ทุกอย่างบนจอนี้อ่านจาก current", x=20, y=252, color=COL_WARN,
         value=16)
ui.Label("ไม่มีบรรทัดไหนถามหลอดเลย", x=360, y=252, color=COL_WARN, value=16)

st = ui.Label("กดปุ่มเพื่อเลื่อนไปดวงถัดไป", x=20, y=336,
              color=COL_INFO, value=20)

# ตัวแปรนี้คือความจริงของโปรแกรม ไม่ใช่ฮาร์ดแวร์
# -1 แปลว่า "ยังไม่มีดวงไหนติด" ซึ่งเป็นสถานะที่เราเพิ่งสั่งไปเมื่อกี้จริง ๆ
current = -1
presses = 0

stable = btn.is_pressed()
last_raw = stable
t0 = time.ticks_ms()
last_change = t0

lcd.print("<span class=muted>กดปุ่มเพื่อเลื่อนไปดวงถัดไป</span>")

while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    now = time.ticks_ms()
    raw = btn.is_pressed()

    if raw != last_raw:
        last_raw = raw
        last_change = now

    elif raw != stable and time.ticks_diff(now, last_change) >= DEBOUNCE_MS:
        stable = raw

        if stable:
            # ดับดวงเดิมก่อน แล้วค่อยเลื่อน ลำดับนี้ห้ามสลับ
            # ถ้าเลื่อนก่อนดับ เราจะไปดับดวงใหม่ที่เพิ่งจะจุด
            if current >= 0:
                gpio.led(current).off()
                rows[current].text(NAMES[current] + "   ดับ")
                rows[current].color(COL_DIM)

            # +1 แล้ววนกลับด้วย % N ทำให้ดวงสุดท้ายวนกลับไปดวงแรกเอง
            current = (current + 1) % N
            presses = presses + 1
            gpio.led(current).on()

            # รายงานจากตัวแปร current ไม่ใช่จากการถามหลอด
            # ตัวแปรนี้เป็นสิ่งเดียวที่รู้ความจริง
            rows[current].text(NAMES[current] + "   ติด")
            rows[current].color(COL_OK)
            seg.text(str(current))
            seg.color(COL_OK)
            l_name.text(NAMES[current])
            l_name.color(COL_OK)
            st.text("กดไปแล้ว " + str(presses) + " ครั้ง | ตอนนี้ดวง " +
                    str(current) + " = " + NAMES[current])
            lcd.print("ติดอยู่ที่ดวง", current, "=", NAMES[current])

    # ลูปถามปุ่มทุก 5 ms ต้องแบ่งจังหวะให้จอด้วยทุกรอบ ไม่งั้นภาพจะค้าง
    ui.poll()
    time.sleep_ms(POLL_MS)

for i in range(N):
    gpio.led(i).off()
    rows[i].text(NAMES[i] + "   ดับ")
    rows[i].color(COL_DIM)

seg.text("-")
seg.color(COL_DIM)
l_name.text("ดับครบทุกดวงแล้ว")
l_name.color(COL_DIM)
st.text("จบแล้ว สถานะจริงอยู่ในตัวแปร ไม่ใช่ในหลอด")
st.color(COL_OK)
ui.poll()

lcd.print("<span class=ok>จบแล้ว ดับไฟครบทุกดวง</span>")
lcd.print("<span class=muted>กดไปทั้งหมด", presses, "ครั้ง</span>")
