# 02_heartbeat_liveness.py - ไฟหัวใจเต้น บอกว่าลูปยังไม่ตาย
#
# Why : เราเตอร์ เครื่องเซิร์ฟเวอร์ และ PLC ทุกตัวมีไฟดวงหนึ่งที่กะพริบเป็น
#       จังหวะตายตัว ช่างที่ยืนอยู่หน้าตู้ดูดวงนั้นดวงเดียวก็รู้ว่าซอฟต์แวร์
#       ข้างในยังทำงาน ไม่ใช่แค่มีไฟเลี้ยงเข้า
# What: ไฟติดค้างพิสูจน์ได้แค่ว่ามีไฟเลี้ยง แต่ไฟที่กะพริบเป็นจังหวะพิสูจน์ว่า
#       มีโค้ดกำลังวนอยู่จริง สองอย่างนี้ไม่เหมือนกันเลย
#
# ดูที่จอ: กราฟซ้ายเป็นคลื่นสี่เหลี่ยม ตุบ-ตุบ-เว้นยาว ตามตาราง PATTERN จริง
#         เส้นส้มคือจำนวนรอบลูปต่อช่วง ยังวิ่งอยู่ตลอดแม้ไฟกำลังดับ
#         Seg7 ขวานับรอบลูปสะสมขึ้นเรื่อย ๆ นั่นคือหลักฐานว่าลูปยังหมุน
# กับดัก : ห้ามใช้ sleep ยาว ๆ วาดจังหวะ เพราะลูปจะติดอยู่ที่ sleep ทำอย่างอื่นไม่ได้
#         ต้องเทียบเวลาจากนาฬิกาแทน ลูปจึงยังว่างพอไปทำงานจริง

import gpio
import lcd
import time
import ui

HB_LED = 2
# ตารางจังหวะ: (ติด/ดับ, กี่มิลลิวินาที) - แก้ตารางนี้ก็ได้จังหวะใหม่ทันที
PATTERN = ((1, 80), (0, 120), (1, 80), (0, 900))
SAMPLE_MS = 60     # ส่งจุดขึ้นกราฟทุกกี่มิลลิวินาที (50 จุดเต็มจอใน 3 วินาที)
LOOP_GAIN = 6      # คูณจำนวนรอบลูปก่อนวาด ให้เส้นสูงพอมองเห็น

led = gpio.led(HB_LED)
led.off()

lcd.clear()
lcd.console("<h2>ไฟหัวใจเต้น</h2>")
lcd.print("จังหวะ", len(PATTERN), "ช่วง | รอบละ",
          sum(p[1] for p in PATTERN), "ms")

ui.screen()
ui.Label("ไฟหัวใจเต้น พิสูจน์ว่าลูปยังหมุน", x=12, y=8, value=24)
ch = ui.Chart(x=12, y=44, w=472, h=212, min=0, max=110, color=0x4A9EFF)
s_load = ch.add_series(0xFFA040)
ui.Label("เส้นฟ้า = ระดับไฟหัวใจ", x=496, y=44, value=16, color=0x4A9EFF)
ui.Label("เส้นส้ม = รอบลูปต่อ 60 ms", x=496, y=68, value=16, color=0xF5A623)

ui.Label("รอบลูปสะสม", x=496, y=104, value=16)
seg = ui.Seg7(x=496, y=132, w=200, h=40)
seg.text("0")      # Seg7 รับ "ข้อความ" ถ้าไม่ตั้งค่า มันจะค้างที่ 0000

ui.Panel(x=496, y=180, w=284, h=100, color=0x171B22, min=0x171B22, value=2)
stp = ui.Label("ช่วงที่ 1/4", x=512, y=192, value=20, color=0x30A46C)
dur = ui.Label("ติด 80 ms", x=512, y=224, value=16)
cyc = ui.Label("รอบละ 1180 ms", x=512, y=248, value=16)

ui.Panel(x=12, y=292, w=472, h=44, color=0x171B22, min=0x171B22, value=2)
st = ui.Label("ลูปยังหมุน - ไฟไม่ได้ยึดลูปไว้", x=24, y=304, value=20)
ui.poll()

step = 0
step_start = time.ticks_ms()
work = 0
report_at = time.ticks_ms()
sample_at = time.ticks_ms()
sample_work = 0
cycle_ms = sum(p[1] for p in PATTERN)
cyc.text("รอบละ " + str(cycle_ms) + " ms")

for _ in range(4000):
    now = time.ticks_ms()

    # ส่วนที่ 1: ไฟหัวใจ ไม่บล็อกใคร ดูนาฬิกาแล้วเดินหน้าเมื่อถึงเวลา
    on_off, dur_ms = PATTERN[step]
    if time.ticks_diff(now, step_start) >= dur_ms:
        step = (step + 1) % len(PATTERN)
        step_start = now
        if PATTERN[step][0]:
            led.on()
        else:
            led.off()
        stp.text("ช่วงที่ " + str(step + 1) + "/" + str(len(PATTERN)))
        stp.color(0x33DD77 if PATTERN[step][0] else 0x8899AA)
        dur.text(("ติด " if PATTERN[step][0] else "ดับ ")
                 + str(PATTERN[step][1]) + " ms")

    # ส่วนที่ 2: งานจริงของเครื่อง ยังทำได้ตามปกติเพราะไฟไม่ได้ยึดลูปไว้
    work += 1
    sample_work += 1

    # ส่วนที่ 3: วาดกราฟ - คลื่นสี่เหลี่ยมของไฟ และภาระลูปในช่วงเดียวกัน
    if time.ticks_diff(now, sample_at) >= SAMPLE_MS:
        ch.set_next(0, 90 if PATTERN[step][0] else 5)
        ch.set_next(s_load, int(min(100, sample_work * LOOP_GAIN)))
        sample_work = 0
        sample_at = now
        seg.text(str(work))

    # ส่วนที่ 4: รายงานทุกสองวินาที เพื่อพิสูจน์ว่าลูปหมุนกี่รอบต่อวินาที
    if time.ticks_diff(now, report_at) >= 2000:
        lcd.print("ลูปหมุนไปแล้ว", work, "รอบ")
        st.text("ลูปหมุนแล้ว " + str(work) + " รอบ")
        report_at = now

    ui.poll()
    time.sleep_ms(5)

led.off()
st.text("หยุดไฟหัวใจแล้ว - ถ้าไฟค้างแปลว่าลูปตาย")
lcd.print("<span class=ok>หยุดไฟหัวใจแล้ว</span>")
print("หยุดไฟหัวใจแล้ว ถ้าเห็นไฟค้างแปลว่าลูปตายคาที่")
