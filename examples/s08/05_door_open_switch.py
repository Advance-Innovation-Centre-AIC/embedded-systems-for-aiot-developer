# 05_door_open_switch.py - สวิตช์แม่เหล็กบอกว่าประตูเปิดหรือปิด
# ชุดตัวอย่างประจำคาบ 08
#
# ไฟล์นี้สอน: ตัวตรวจจับสถานะสองค่าต้องมีเกณฑ์สองระดับ (hysteresis) เกณฑ์ที่ใช้
#             ขึ้นกับสถานะตอนนี้ ค่าที่แกว่งอยู่ระหว่างสองเส้นจึงไม่ทำให้สถานะเปลี่ยน
# ดูที่จอ   : เส้นแดงคือเกณฑ์ปิด เส้นเหลืองคือเกณฑ์เปิด ระยะห่างสองเส้นคือ hysteresis
#             เส้นฟ้าคือค่าจริง ป้ายล่างเปลี่ยนต่อเมื่อเส้นฟ้าข้ามเส้นใดเส้นหนึ่งจริง ๆ
# กับดัก    : ถ้าตั้งเกณฑ์เปิดกับปิดใกล้กันเกินไป สองเส้นบนกราฟจะทับกันจนแยกไม่ออก
#             แล้วขยับประตูครั้งเดียวจะได้รายงานเปิด-ปิด-เปิด-ปิด รัวเป็นสิบครั้ง
#
# [หน่วยยังไม่ยืนยัน] เกณฑ์ 45 กับ 25 ในไฟล์นี้คือ "เบี่ยงจากเส้นฐาน" ในหน่วยของ
#   บอร์ด ไม่ใช่ไมโครเทสลา เพราะ magnetic() คืนตัวเลขที่ขนาดรวมวัดได้ราว 1532
#   ขณะที่สนามแม่เหล็กโลกอยู่ที่ 25-65 uT สองอย่างนี้จริงพร้อมกันไม่ได้ และยังไม่มี
#   ใครตัดสิน ตัวเลขเกณฑ์จึงต้องปรับตามที่วัดได้จริงในห้องนั้น อย่ายกไปใช้ที่อื่น
#
# ทั้งสองบอร์ด: sensors.bmm350.* อ่านตรงจากชิปบนบัส I3C ของมันเอง ไม่ผ่านคอร์จอ
#   จึงใช้ได้เลยโดยไม่ต้อง init และไม่ต้องรอ snapshot ของ Eva แต่รอบแรก ๆ ยังโยน
#   OSError ได้ - magnitude() จึงดักไว้
#   ส่วนไฟสองดวงหาตามชื่อ ไม่ใช่ตามเลข - ดู led_named() ข้างล่างว่าทำไม

import gpio
import lcd
import math
import sensors
import time
import ui

CLOSE_DEV = 45.0   # เบี่ยงเกินนี้ = ประตูปิด (แม่เหล็กชิด)
OPEN_DEV = 25.0    # เบี่ยงต่ำกว่านี้ = ประตูเปิด - ต้องต่ำกว่า CLOSE_DEV ชัดเจน
CONFIRM = 3        # ต้องอ่านได้ตรงกันกี่ครั้งติดถึงจะเปลี่ยนสถานะ
Y_MAX = 60         # แกน Y ต้องสูงกว่า CLOSE_DEV พอควร ไม่งั้นเส้นชนขอบบน

# ไฟสองดวงหาตามชื่อ ไม่ใช่ตามเลข - เลขดัชนีของ gpio.led() ต่างกันตามบอร์ดและอาจเปลี่ยนอีก
# ตารางที่เฟิร์มแวร์รายงาน (modgpio.c วัดจริง 2026-09-16):
#   Eva Kit : LED1=แดง  LED2=เขียว  RGB_RED=ฟ้า   <- ชื่อ RGB_RED บน Eva คือดวงสีฟ้า
#   Dev Kit : LED1 LED2 อยู่บน SoM มองไม่เห็นบนบอร์ดประกอบ  RGB_RED RGB_BLUE RGB_GREEN
# สีแดงเป็นข้อยกเว้น: ทั้งสองบอร์ดมีทั้ง "RGB_RED" และ "LED1" ต้องดูก่อนว่ามี RGB ครบสามสีไหม
LED_NAMES = gpio.board_info()["led_names"]
HAS_RGB = "RGB_GREEN" in LED_NAMES          # Dev Kit จริง / Eva ไม่มีชื่อนี้


def led_named(*names, fallback=0):
    """หา LED จากชื่อในตารางเฟิร์มแวร์ - เลขดัชนีต่างกันตามบอร์ด ชื่อไม่ต่าง"""
    for n in names:
        if n in LED_NAMES:
            return gpio.led(LED_NAMES.index(n))
    return gpio.led(fallback)


led_open = led_named("RGB_RED" if HAS_RGB else "LED1")   # แดง: Dev Kit RGB_RED / Eva LED1
led_shut = led_named("RGB_GREEN", "LED2")                # เขียว: Dev Kit RGB_GREEN / Eva LED2


last_mag = 0.0


def magnitude():
    global last_mag
    try:
        mx, my, mz = sensors.bmm350.magnetic()
    except OSError:
        return last_mag     # ยังไม่พร้อมตอบ ใช้ค่าเดิมไปก่อน ไม่ใช่ปล่อยให้ตาย
    last_mag = math.sqrt(mx * mx + my * my + mz * mz)
    return last_mag


ui.screen()
ui.Label("สวิตช์ประตูแบบแม่เหล็ก", x=12, y=8, value=24)
ch = ui.Chart(x=12, y=40, w=472, h=200, min=0, max=Y_MAX)
s_dev = 0
s_close = ch.add_series(0xFF5555)
s_open = ch.add_series(0xFFC83D)

ui.Label("ฟ้า = เบี่ยงจากเส้นฐาน", x=496, y=44, value=16, color=0x4A9EFF)
ui.Label("แดง = เกณฑ์ปิด 45", x=496, y=68, value=16, color=0xE5484D)
ui.Label("เหลือง = เกณฑ์เปิด 25", x=496, y=92, value=16, color=0xF5A623)
ui.Label("เบี่ยงตอนนี้ (หน่วยบอร์ด)", x=496, y=124, value=16)
seg = ui.Seg7(x=496, y=148, w=180, h=44)
bar = ui.Bar(x=496, y=200, w=180, h=16, min=0, max=Y_MAX)
ui.Label("แถบ = เบี่ยง 0-60", x=496, y=224, value=16)

st = ui.Label("ประตูเปิด", x=24, y=264, value=28, color=0x30A46C)
sub = ui.Label("กำลังเก็บเส้นฐาน...", x=24, y=300, value=20)
ui.poll()

lcd.clear()
lcd.console("<h2>สวิตช์ประตูแบบแม่เหล็ก</h2>")
lcd.print("เก็บเส้นฐานตอนประตูเปิด")
base = sum(magnitude() for _ in range(10)) / 10.0
lcd.print("เส้นฐาน", int(base), "| ปิดที่", int(CLOSE_DEV),
          "เปิดที่", int(OPEN_DEV))

shut = False
led_open.on()
votes = 0
flips = 0
sub.text("เส้นฐาน %d | โหวต 0/%d" % (int(base), CONFIRM))

for _ in range(900):
    dev = abs(magnitude() - base)

    # แกนของ hysteresis: ใช้เกณฑ์คนละตัวตามสถานะที่อยู่ตอนนี้
    want = (dev < OPEN_DEV) if shut else (dev > CLOSE_DEV)
    votes = votes + 1 if want else 0

    ch.set_next(s_dev, int(min(dev, Y_MAX)))
    ch.set_next(s_close, int(CLOSE_DEV))    # เส้นแนวนอนสองเส้นคือรูปของ hysteresis
    ch.set_next(s_open, int(OPEN_DEV))
    seg.text(str(int(dev)))
    bar.value(int(min(dev, Y_MAX)))
    sub.text("เส้นฐาน %d | โหวต %d/%d" % (int(base), votes, CONFIRM))

    if votes >= CONFIRM:
        shut = not shut
        votes = 0
        flips += 1
        led_open.off()
        led_shut.off()
        if shut:
            led_shut.on()
            st.text("ประตูปิด")
            st.color(0xFF5555)
            lcd.print("ประตูปิด (เบี่ยง " + str(int(dev)) + ")")
            ui.tone(72, ui.WAVE_SINE, 90, 90)
        else:
            led_open.on()
            st.text("ประตูเปิด")
            st.color(0x55DD55)
            lcd.print("<span class=ok>ประตูเปิด (เบี่ยง " + str(int(dev)) + ")</span>")
            ui.tone(60, ui.WAVE_SQUARE, 100, 250)

    ui.poll()
    time.sleep_ms(100)

led_open.off()
led_shut.off()
lcd.print("เปลี่ยนสถานะทั้งหมด", flips, "ครั้ง")
lcd.print("<span class=muted>ลองตั้ง OPEN_DEV = CLOSE_DEV แล้วเขย่าแม่เหล็กดู</span>")
