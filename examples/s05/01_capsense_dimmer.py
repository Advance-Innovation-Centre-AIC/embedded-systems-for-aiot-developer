# 01_capsense_dimmer.py - สไลเดอร์สัมผัสเป็นสวิตช์หรี่ไฟ
# ชุดตัวอย่างประจำคาบ 05
#
# ไฟล์นี้สอน: ค่าจากสไลเดอร์สัมผัสใช้ตรง ๆ ไม่ได้ เพราะพอปล่อยนิ้ว ค่าสุดท้ายจะค้าง
#             ไม่ได้กลับเป็นศูนย์ ต้องดูว่าค่าหยุดเปลี่ยนกี่รอบแล้วจึงสรุปว่านิ้วออก
# ดูที่จอ   : เส้นฟ้าคือค่าดิบจาก slider() เส้นแดงคือระดับที่สั่งไฟจริง
#             ตอนไม่มีนิ้วแตะ ค่าดิบลงไปติดลบใต้เส้นศูนย์ แต่เส้นแดงค้างอยู่ที่เดิม
# ดูที่หลอด : LED0 ค้างที่ระดับเดิมตอนปล่อยนิ้ว ไม่ดับ เพราะ hold() ถูกเรียกซ้ำทุกรอบ
# กับดัก    : อย่าถือว่า slider() == 0 แปลว่าปล่อยนิ้ว มันแปลว่าแตะที่ปลายซ้ายก็ได้
#
# hold() ไม่ใช่ brightness(): brightness(pct) คือพัลส์เดียวยาวราว 12 ms แล้วจบด้วย
#   หลอดดับ (gpio_led_brightness() ปิดท้ายด้วย Cy_GPIO_Clr ใน modgpio.c)
#   ตาต้องได้พัลส์ซ้ำ ๆ ถึงจะเห็นเป็นระดับที่ค้างอยู่ hold(pct, ms) ทำการซ้ำนั้นให้
#   และกินเวลา ms พอดี จึงใช้แทน sleep ได้เลย
#
# บน Eva Kit: sensors.init() และ sensors.scan() ปฏิเสธด้วย OSError ไม่ต้องเรียก
#   ส่วน sensors.capsense.* ใช้ได้เลยโดยไม่ต้อง init แต่หลังรีเซ็ต การอ่านครั้งแรก
#   ช้าได้ถึงราว 16 วินาที และอาจโยน OSError ระหว่างนั้น - ลูปข้างล่างจึงดักไว้

import gpio
import lcd
import sensors
import time
import ui

IDLE_ROUNDS = 8    # ค่าไม่ขยับกี่รอบ ถือว่าปล่อยนิ้วแล้ว
CHART_MS = 250     # จังหวะเก็บจุดลงกราฟ ช้ากว่าลูป เพื่อให้ 50 จุดกินเวลาหลายวินาที
LOOP_MS = 80       # หนึ่งรอบลูปกินเวลาเท่านี้ และ hold() เป็นคนกินเวลานั้นเอง

# รูปสาธิต: ค่าดิบสมมติของการลากนิ้วขึ้นแล้วปล่อย ตัวเลขชุดนี้เราพิมพ์เอง
# ไม่ใช่ค่าจากเซนเซอร์ มีไว้ให้เห็นรูปที่ต้องมองหาก่อนจะได้ลากจริง
DEMO = (-1, -1, -1, -1, 5, 15, 28, 40, 52, 63, 74, 82, 88, 88, 88,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)

led = gpio.led(0)
led.off()

ui.screen()
ui.Label("สไลเดอร์สัมผัส = สวิตช์หรี่ไฟ", x=12, y=6, value=24)
# แกน Y เริ่มที่ -10 เพราะค่าดิบตอนไม่มีนิ้วแตะลงไปติดลบได้ ถ้าเริ่มที่ 0
# เส้นจะไปกองอยู่ที่ขอบล่างจนแยกไม่ออกว่าเป็นศูนย์หรือติดลบ
ch = ui.Chart(x=12, y=40, w=470, h=200, min=-10, max=100)
s_slider = 0
s_duty = ch.add_series(0xFF5555)

ui.Label("ฟ้า = ค่าดิบจาก slider()", x=496, y=44, value=16, color=0x00BFFF)
ui.Label("แดง = ความสว่างที่สั่งจริง", x=496, y=68, value=16, color=0xFF5555)
ui.Label("ความสว่าง (%)", x=496, y=100, value=16)
seg = ui.Seg7(x=496, y=122, w=180, h=44)
bar = ui.Bar(x=496, y=186, w=180, h=18, min=0, max=100)
ui.Label("ลากนิ้วบนสไลเดอร์ CapSense", x=496, y=214, value=14)

st = ui.Label("ปล่อยนิ้ว", x=24, y=262, value=28, color=0x9AA0A6)
sub = ui.Label("ค้างไว้ที่ 0%", x=24, y=300, value=18)
ui.poll()

lcd.clear()
lcd.console("<h2>สไลเดอร์สัมผัส = สวิตช์หรี่ไฟ</h2>")
lcd.print("ลากนิ้วบนสไลเดอร์ CapSense")

level = 0
prev = -1
idle = 0
seg.text("0")

# สาธิตก่อน ให้เห็นว่าตอนปล่อยนิ้วเส้นฟ้ากับเส้นแดงแยกจากกันอย่างไร
st.text("รูปสาธิต ยังไม่ใช่ค่าจริง")
sub.text("ลากขึ้น แล้วปล่อย - ดูเส้นแดงค้าง")
demo_level = 0
for d in DEMO:
    if d >= 0:
        demo_level = d
    ch.set_next(s_slider, d)
    ch.set_next(s_duty, demo_level)
    seg.text(str(demo_level))
    bar.value(demo_level)
    ui.poll()
    # hold() เป็นตัวกินเวลาของรอบนี้ด้วย จึงไม่ต้องมี sleep_ms อีกบรรทัด
    led.hold(demo_level, 40)

st.text("ปล่อยนิ้ว")
st.color(0x9AA0A6)
sub.text("ค่าจริงจากเซนเซอร์ - ลากได้เลย")
seg.text("0")
bar.value(0)
ui.poll()

chart_at = time.ticks_ms()

for _ in range(800):
    try:
        s = sensors.capsense.slider()
    except OSError:
        # หลังรีเซ็ต CM55 ยังไม่พร้อมตอบ รอรอบหน้า ไม่ใช่ปล่อยให้โปรแกรมตาย
        sub.text("รอ CM55 พร้อม - นานได้ถึง 16 วินาที")
        ui.poll()
        time.sleep_ms(LOOP_MS)
        continue

    if s != prev:
        idle = 0
        level = s
        prev = s
        st.text("กำลังลาก")
        st.color(0x55DD55)
        sub.text("ความสว่าง %d%%" % level)
        seg.text(str(level))
        bar.value(level)
        lcd.print("ความสว่าง", level, "%")
    else:
        idle += 1
        # ค่าค้างหลายรอบ = นิ้วออกไปแล้ว ไม่ใช่ผู้ใช้นิ่งอยู่ที่ค่านั้น
        if idle == IDLE_ROUNDS:
            st.text("ปล่อยนิ้ว")
            st.color(0x9AA0A6)
            sub.text("ค้างไว้ที่ %d%%" % level)
            lcd.print("ปล่อยนิ้ว - ค้างไว้ที่", level, "%")

    # เส้นฟ้าคือค่าดิบที่เซนเซอร์บอก ไม่ตัดทิ้งค่าติดลบ เพราะค่าติดลบคือข้อมูล
    # เส้นแดงคือระดับที่เราสั่งไฟ ซึ่งเราเลือกให้ค้างที่ค่าสุดท้ายเมื่อปล่อยนิ้ว
    if time.ticks_diff(time.ticks_ms(), chart_at) >= CHART_MS:
        chart_at = time.ticks_ms()
        ch.set_next(s_slider, int(s))
        ch.set_next(s_duty, int(max(0, level)))

    ui.poll()
    # สั่งไฟทุกรอบ ไม่ใช่เฉพาะตอนค่าเปลี่ยน ตาถึงจะเห็นระดับที่ค้างอยู่
    # ค่าติดลบตอนไม่มีนิ้วแตะ ยึดเป็น 0 ก่อน เพราะเปอร์เซ็นต์ติดลบไม่มีความหมาย
    led.hold(max(0, level), LOOP_MS)

led.off()
sub.text("จบ - ค่าสุดท้าย %d%% ไม่ได้กลับเป็นศูนย์" % level)
ui.poll()
lcd.print("ค่าสุดท้าย", level, "% - ไม่กลับเป็นศูนย์")
