# 01_andon_severity_lamp.py - เสาไฟสถานะแบบโรงงาน (andon light)
#
# Why : ทุกเครื่องจักรในสายการผลิตมีเสาไฟสามสีอยู่บนหัวเครื่อง หัวหน้ากะเดินผ่าน
#       แล้วต้องรู้สถานะทั้งสายในสามวินาที โดยไม่ต้องหยุดอ่านจอทีละเครื่อง
#       สีจึงไม่ใช่การตกแต่ง มันคือช่องทางสื่อสารที่เร็วที่สุดที่เครื่องมี
# What: ระดับความรุนแรงหนึ่งระดับ = ไฟหนึ่งดวง และติดได้ทีละดวงเท่านั้น
#       IEC 60073 กำหนดไว้ว่า เขียว = ปกติ, แดง = ต้องเข้าแก้ทันที
#
# ดูที่จอ: การ์ดใหญ่บอกระดับเป็นคำ (ปกติ / เฝ้าดู / ผิดปกติ) และเปลี่ยนสีตามระดับ
#         กราฟซ้ายมีเส้นอ้างอิงสองเส้น เส้นฟ้าคือค่าจริงที่ไต่ขึ้นไปชนเส้น
# กับดัก : ถ้าปล่อยให้ไฟสองดวงติดพร้อมกัน คนอ่านจะตีความไม่ได้ว่าตกลงระดับไหน
#         โค้ดจึงต้องดับทุกดวงก่อนจุดดวงใหม่เสมอ ไม่ใช่แค่จุดดวงที่ต้องการ

import gpio
import lcd
import math
import sensors
import time
import ui

# ไฟล์นี้ต้องการ "สี" ไม่ใช่ "เลขดัชนี" จึงหาดวงจากชื่อที่บอร์ดรายงาน
# บอร์ดที่รายงานดวง RGB ครบสามสี (Dev Kit: RGB_RED / RGB_BLUE / RGB_GREEN) เชื่อชื่อได้ตรง ๆ
# และเลขดัชนีของมันอาจถูกจัดใหม่ในเฟิร์มแวร์รุ่นหน้า ชื่อจึงเป็นที่เดียวที่ควรยึด
# Eva Kit รายงานชื่อ RGB_RED เพียงดวงเดียว และดวงนั้นส่องสีน้ำเงินจริง ชื่อจึงใช้ไม่ได้
# ต้องใช้ดัชนีที่วัดจากบอร์ดแทน: 0 = แดง  1 = เขียว  2 = น้ำเงิน
LED_NAMES = gpio.board_info()["led_names"]


def lamp(rgb_name, eva_index):
    if "RGB_GREEN" in LED_NAMES and "RGB_BLUE" in LED_NAMES:
        return LED_NAMES.index(rgb_name)
    return eva_index


LAMP_OK = lamp("RGB_GREEN", 1)     # เขียว    - เดินเครื่องปกติ
LAMP_WARN = lamp("RGB_BLUE", 2)    # น้ำเงิน  - เฝ้าดู ยังไม่ต้องหยุด
LAMP_FAULT = lamp("RGB_RED", 0)    # แดง      - ต้องเข้าแก้

WARN_UT = 40.0     # เบี่ยงเบนจากเส้นฐานกี่ ไมโครเทสลา ถึงเรียกว่าเฝ้าดู
FAULT_UT = 120.0   # เบี่ยงเบนเท่าไหร่ถึงเรียกว่าผิดปกติจริง

C_OK = 0x30A46C
C_WARN = 0xF5A623
C_FAULT = 0xE5484D
NAME = {LAMP_OK: "ปกติ", LAMP_WARN: "เฝ้าดู", LAMP_FAULT: "ผิดปกติ"}
TINT = {LAMP_OK: C_OK, LAMP_WARN: C_WARN, LAMP_FAULT: C_FAULT}


def field_strength():
    mx, my, mz = sensors.bmm350.magnetic()
    return math.sqrt(mx * mx + my * my + mz * mz)


def set_lamp(active):
    # ดับให้หมดก่อนเสมอ นี่คือหัวใจของไฟล์นี้
    for i in range(gpio.num_leds()):
        gpio.led(i).off()
    gpio.led(active).on()


lcd.clear()
lcd.console("<h2>เสาไฟสถานะ - andon light</h2>")

ui.screen()
ui.Label("เสาไฟสถานะ - andon light", x=12, y=8, value=24)
ch = ui.Chart(x=12, y=44, w=472, h=212, min=-10, max=160, color=0x4A9EFF)
s_warn = ch.add_series(C_WARN)
s_fault = ch.add_series(C_FAULT)
ui.Label("เส้นฟ้า = เบี่ยงเบนจริง", x=496, y=44, value=16, color=0x4A9EFF)
ui.Label("เส้นเหลือง = เฝ้าดู 40 uT", x=496, y=68, value=16, color=C_WARN)
ui.Label("เส้นแดง = ผิดปกติ 120 uT", x=496, y=92, value=16, color=C_FAULT)

ui.Panel(x=496, y=124, w=284, h=88, color=0x171B22, min=0x171B22, value=2)
ui.Label("ระดับตอนนี้", x=512, y=132, value=16)
lv = ui.Label("ปกติ", x=512, y=160, value=28, color=C_OK)

ui.Label("เบี่ยงเบนเทียบเส้นผิดปกติ", x=496, y=220, value=16)
bar = ui.Bar(x=496, y=244, w=280, h=20, min=0, max=120, value=0)
seg = ui.Seg7(x=496, y=276, w=160, h=40)
seg.text("0")      # ตั้งค่าเริ่มต้นเอง ไม่งั้นจอขึ้น 0000 ตามค่าตั้งต้นของวิดเจ็ต
ui.Label("uT", x=664, y=284, value=20)

ui.Panel(x=12, y=292, w=472, h=44, color=0x171B22, min=0x171B22, value=2)
st = ui.Label("กำลังวัดเส้นฐานของห้อง...", x=24, y=304, value=20)
ui.poll()

# เส้นอ้างอิงสองเส้นเติมให้เต็มหน้าต่างก่อน กราฟจะได้อ่านออกตั้งแต่วินาทีแรก
for _ in range(50):
    ch.set_next(s_warn, int(WARN_UT))
    ch.set_next(s_fault, int(FAULT_UT))

# วัดเส้นฐานตอนเริ่ม สนามแม่เหล็กของแต่ละห้องไม่เท่ากัน จะ hardcode ไม่ได้
base = field_strength()
lcd.print("เส้นฐานของห้องนี้:", int(base), "uT")
lcd.print("<span class=muted>เขียว=ปกติ ฟ้า=เฝ้าดู แดง=ผิดปกติ</span>")
st.text("เส้นฐานห้องนี้ " + str(int(base)) + " uT")

last = -1
for _ in range(300):
    dev = abs(field_strength() - base)
    if dev >= FAULT_UT:
        level = LAMP_FAULT
    elif dev >= WARN_UT:
        level = LAMP_WARN
    else:
        level = LAMP_OK

    # จอกราฟรับได้ทุกรอบ ส่วนไฟกับ lcd เขียนเฉพาะตอนระดับเปลี่ยน
    ch.set_next(0, int(dev))
    ch.set_next(s_warn, int(WARN_UT))
    ch.set_next(s_fault, int(FAULT_UT))
    bar.value(int(min(dev, 120.0)))
    seg.text(str(int(dev)))

    # เขียนจอเฉพาะตอนระดับเปลี่ยน ไม่ใช่ทุกรอบ จอจะได้ไม่ถูกถล่ม
    if level != last:
        set_lamp(level)
        lv.text(NAME[level])
        lv.color(TINT[level])
        st.text("เส้นฐาน " + str(int(base)) + " uT | เบี่ยง " + str(int(dev)))
        lcd.print("ระดับเปลี่ยน | เบี่ยงเบน", int(dev), "uT")
        last = level

    ui.poll()
    time.sleep_ms(100)

set_lamp(LAMP_OK)
lv.text("ปกติ")
lv.color(C_OK)
st.text("จบรอบวัด - เสาไฟกลับสู่สถานะปกติ")
lcd.print("<span class=ok>จบรอบวัด - กลับสู่ปกติ</span>")
print("เสาไฟกลับสู่สถานะปกติ")
