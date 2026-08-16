# 01_imu_step_counter.py - นับก้าวจากความเร่ง
# ชุดตัวอย่างประจำคาบ 06
#
# ไฟล์นี้สอน: หนึ่งก้าวคือยอดคลื่นหนึ่งลูกของขนาดความเร่ง ต้องกรองก่อนนับเสมอ
# ดูที่จอ   : ฟ้าคือขนาดความเร่งดิบ แดงคือค่าหลังกรอง EMA เหลืองคือเส้นเกณฑ์
#             ขวามือคือจำนวนก้าวตัวใหญ่ กับป้ายบอกว่าพร้อมนับหรือยังห้ามนับซ้ำ
# กับดัก    : ถ้าไม่มีเวลาห้ามนับซ้ำ (refractory) การสั่นหนึ่งครั้งจะถูกนับหลายก้าว
#             เพราะสัญญาณจริงข้ามเกณฑ์ขึ้นลงหลายรอบในหนึ่งก้าว
#
# บน Eva Kit: sensors.init() และ sensors.scan() ปฏิเสธด้วย OSError ไม่ต้องเรียก
#   ส่วน sensors.bmi270.* ใช้ได้เลยโดยไม่ต้อง init - มันอ่านจาก snapshot ที่ CM55
#   ส่งมาทาง IPC (modsensors.c) หลังรีเซ็ต การอ่านครั้งแรกช้าได้ถึงราว 16 วินาที
#   และอาจโยน OSError ระหว่างนั้น - ลูปข้างล่างจึงดักไว้

import dsp
import lcd
import math
import sensors
import time
import ui

THRESH = 12.0      # ขนาดความเร่งเกินนี้ (m/s^2) ถือว่าเป็นยอดคลื่น
REFRACT_MS = 300   # ห้ามนับก้าวถัดไปเร็วกว่านี้ - คนวิ่งเร็วสุดราว 3 ก้าว/วินาที
ALPHA = 0.3        # ค่าปรับความนุ่มของตัวกรอง EMA
Y_MAX = 250        # แกน Y ของกราฟ = ขนาดความเร่ง x10 จึงกิน 0 ถึง 25 m/s^2

smooth = dsp.EMA(alpha=ALPHA)

lcd.clear()
lcd.console("<h2>ตัวนับก้าว</h2>")
lcd.print("เกณฑ์", THRESH, "m/s2 | ห้ามนับซ้ำใน", REFRACT_MS, "ms")

# ---- หน้าจอ ------------------------------------------------------------------
ui.screen()
ui.Label("ตัวนับก้าว - ทำไมต้องกรองก่อนนับ", x=12, y=6, value=24)
ch = ui.Chart(x=12, y=40, w=470, h=232, min=0, max=Y_MAX)
s_raw = 0                          # ซีรีส์ 0 เกิดพร้อมกราฟ สีฟ้าเริ่มต้น
s_ema = ch.add_series(0xFF5555)
s_thr = ch.add_series(0xFFC107)
ui.Label("ฟ้า = ขนาดความเร่งดิบ", x=496, y=44, value=16, color=0x00BFFF)
ui.Label("แดง = หลังกรอง EMA", x=496, y=68, value=16, color=0xFF5555)
ui.Label("เหลือง = เกณฑ์ 12 m/s2", x=496, y=92, value=16, color=0xFFC107)

ui.Label("นับได้ (ก้าว)", x=496, y=128, value=16)
seg = ui.Seg7(x=496, y=152, w=180, h=40)
state = ui.Label("พร้อมนับก้าว", x=496, y=210, value=20, color=0x00E676)

# สองป้ายแทนหนึ่งก้อน - ui.Label ตัดที่ 126 ไบต์ และไทยตัวละ 3 ไบต์
ui.Panel(x=12, y=286, w=662, h=74)
ui.Label("วางนิ่ง ๆ สองเส้นทับกันสนิท", x=24, y=294, value=20, color=0xFFC107)
ui.Label("พอเขย่า เส้นแดงเรียบกว่าเส้นฟ้า", x=24, y=324, value=20,
         color=0xFFC107)

steps = 0
above = False
last_step = time.ticks_ms()
seg.text("0")
waiting = False

for _ in range(2000):
    try:
        ax, ay, az, _, _, _ = sensors.bmi270.motion()
    except OSError:
        # หลังรีเซ็ต CM55 ยังไม่พร้อมตอบ รอรอบหน้า ไม่ใช่ปล่อยให้โปรแกรมตาย
        state.text("รอ CM55 พร้อม")
        waiting = -1            # บังคับให้รอบหน้าเขียนป้ายสถานะใหม่
        ui.poll()
        time.sleep_ms(25)
        continue

    mag = math.sqrt(ax * ax + ay * ay + az * az)

    # กรองก่อนเสมอ ค่าดิบมีหนามแหลมที่ทำให้นับเกินได้ง่ายมาก
    v = smooth.update(mag)
    now = time.ticks_ms()

    # กราฟรับได้เฉพาะจำนวนเต็ม จึงคูณสิบก่อนแล้วอ่านแกน Y เป็นสิบเท่าของ m/s^2
    ch.set_next(s_raw, int(mag * 10))
    ch.set_next(s_ema, int(v * 10))
    ch.set_next(s_thr, int(THRESH * 10))

    if v > THRESH and not above:
        above = True
        if time.ticks_diff(now, last_step) >= REFRACT_MS:
            steps += 1
            last_step = now
            seg.text(str(steps))
            if steps % 5 == 0:
                lcd.print("ก้าวที่", steps, "| ค่ายอด", round(v, 1))
    elif v < THRESH - 1.0:
        # ต้องตกต่ำกว่าเกณฑ์พอสมควรถึงจะพร้อมนับใหม่ นี่คือ hysteresis
        above = False

    # ป้ายนี้คือคำตอบว่าทำไมยอดคลื่นบางลูกไม่ถูกนับ
    busy = time.ticks_diff(now, last_step) < REFRACT_MS
    if busy != waiting:
        waiting = busy
        state.text("รอพ้นเวลาห้ามนับซ้ำ" if busy else "พร้อมนับก้าว")
        state.color(0xFF9800 if busy else 0x00E676)

    ui.poll()
    time.sleep_ms(25)

lcd.print("<span class=ok>รวม " + str(steps) + " ก้าว</span>")
print("ลองตั้ง REFRACT_MS = 0 แล้วเดินเท่าเดิม จะเห็นตัวเลขพุ่งเกินจริง")
