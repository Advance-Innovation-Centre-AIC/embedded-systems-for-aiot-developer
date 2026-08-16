# 14_hard_iron_calibration.py - การคาลิเบรตเข็มทิศ เป็นสิ่งที่วัดได้
#
# ไฟล์นี้ไม่ต้องแก้อะไรก่อนกด Run
#
# Why : โดรนทุกลำบังคับให้ผู้ใช้หมุนเลขแปดก่อนบินครั้งแรก เพราะโลหะและแม่เหล็ก
#       บนตัวเครื่องเองสร้างสนามคงที่ที่เดินทางไปกับเซนเซอร์ ทำให้ทิศเพี้ยนเป็นสิบองศา
#       คนส่วนใหญ่ทำพิธีกรรมนี้แล้วหวังว่าจะดีขึ้น โดยไม่เคยเห็นตัวเลขอะไรเลย
#       จึงไม่มีทางรู้ว่าหมุนพอหรือยัง และไม่มีทางรู้ว่าครั้งนี้ได้ผลหรือเปล่า
# What: ค่าชดเชย hard-iron เป็นตัวเลขที่อ่านออกมาดูได้ ไม่ใช่ของที่เชื่อเอาเอง
#       cal_reset() ล้างค่าชดเชยเดิม แล้ว cal_status() คืน offset_x offset_y
#       และ valid ให้เฝ้าดูว่าตัวชดเชยกำลังลู่เข้าหาค่าใหม่หรือยัง
#       คำว่า "ลู่เข้า" จึงมีรูปร่างที่ตาดูออกบนกราฟ และมีเงื่อนไขหยุดที่เขียนเป็นโค้ดได้
#
# [Eva Kit: heading() วัดจริงบนบอร์ดแล้วเมื่อ 2026-08-14 ได้ 215.8]
# [ยังไม่ยืนยัน: cal_reset() และ cal_status() ยังไม่มีใครรันบนบอร์ด Eva Kit]
#       สองฟังก์ชันนี้อยู่ในโมดูลเดียวกับ heading() ซึ่งพิสูจน์แล้วว่าติดต่อชิปได้
#       (modsensors_bmm350.c) แต่ "โมดูลเดียวกัน" ไม่ใช่หลักฐานว่าฟังก์ชันนี้ทำงาน
#       ก่อนใช้ไฟล์นี้ในห้องเรียน ต้องมีใครรันบนบอร์ดหนึ่งครั้งแล้วบันทึกผลไว้
# ห้ามใช้: sensors.init() และ sensors.scan() บน Eva Kit ทั้งคู่ปฏิเสธด้วย OSError
# จังหวะ : หลังรีเซ็ต การอ่านเซนเซอร์ครั้งแรกช้าได้ถึงราว 16 วินาที
#          บรรทัดอ่านค่าแรก ๆ จึงอาจได้ OSError ให้ครอบด้วย try/except ไว้
#
# ไฟล์คู่กัน: examples/s08/06_compass_readout.py คือฝั่งที่เอาค่าไปใช้จริง
#       ถ้าเข็มในไฟล์นั้นสั่นหรือชี้ผิดทิศ ให้กลับมารันไฟล์นี้ก่อน
#
# ดูที่จอ: สองเส้นคือ offset_x กับ offset_y ตอนเริ่มจะแกว่ง แล้วค่อย ๆ แบนลงจน
#          เป็นเส้นตรง นั่นคือหน้าตาของคำว่าลู่เข้า แถบขวานับรอบที่นิ่งติดกัน
#          ถ้าเห็นเส้นเดียว แปลว่าสองค่าเท่ากันพอดีจนทับกันสนิท
# กับดัก : อย่าหมุนอยู่แกนเดียว ค่าชดเชยจะลู่เข้าหาคำตอบที่ผิด และอย่าคาลิเบรต
#          ขณะวางบนโต๊ะเหล็ก จะได้ค่าที่ใช้ที่อื่นไม่ได้

import lcd
import sensors
import time
import ui

ROUNDS = 60        # จำนวนรอบที่เฝ้าดู รอบละราวครึ่งวินาที
SETTLED_UT = 1.5   # ค่าชดเชยขยับน้อยกว่านี้ติดกัน ถือว่านิ่งแล้ว
SETTLED_N = 6      # ต้องนิ่งติดกันกี่รอบ
SUB_N = 5          # อ่านย่อยกี่ครั้งต่อรอบ ใช้เพื่อให้กราฟมีจุดพอให้เห็นรูปร่าง

ui.screen()
# ผังจอสองคอลัมน์ ขอบนอก 24 - ซ้ายคือกราฟกับการ์ดคำแนะนำ ขวาคือคำอธิบายเส้น
# ตัวเลขที่ขยับรอบนี้ และตัวนับรอบที่นิ่ง
#
# ก่อนแก้ ป้าย "เห็นเส้นเดียว" ถูกวางไว้ที่ x=428 ซึ่งคาบสองคอลัมน์ แล้วการ์ด
# ที่สร้างทีหลังก็ทาทับมันหายไปทั้งใบ ตอนนี้มันอยู่ในคอลัมน์ขวาที่เป็นบ้านของมัน
ui.Label("คาลิเบรตเข็มทิศ - ดูตัวเลขลู่เข้า", x=24, y=8, value=24)
ch = ui.Chart(x=24, y=48, w=440, h=192, min=-50, max=50)
s_ox = 0
s_oy = ch.add_series(0xE5484D)

ui.Label("ฟ้า = offset_x (uT)", x=496, y=48, value=20, color=0x4A9EFF)
ui.Label("แดง = offset_y (uT)", x=496, y=76, value=20, color=0xE5484D)
ui.Label("เส้นเดียว = ค่าเท่ากัน", x=496, y=104, value=20,
         color=0x9AA3AF)

ui.Label("ขยับรอบนี้ (uT)", x=496, y=148, value=20)
seg = ui.Seg7(x=496, y=180, w=180, h=48)

# ตัวนับกับแถบอยู่ติดกัน ตัวเลขบอกค่า แถบบอกว่าอีกไกลแค่ไหน ตาม §S7.7.8
# สองแถวนี้จบที่ y=299 จึงพ้นมุมของปุ่ม Console (x>=690 และ y>=340)
lbl_stab = ui.Label("นิ่งติดกัน 0/%d" % SETTLED_N, x=496, y=244, value=20)
bar = ui.Bar(x=496, y=280, w=248, h=20, min=0, max=SETTLED_N)

ui.Panel(x=24, y=268, w=440, h=108)
st = ui.Label("หมุนบอร์ดเป็นเลขแปดช้า ๆ", x=40, y=288, value=24)
sub = ui.Label("valid = ?", x=40, y=328, value=20)
ui.poll()

lcd.clear()
lcd.console("<h2>คาลิเบรต hard-iron</h2>")

before = sensors.bmm350.cal_status()
lcd.print("ก่อนล้าง: ox", round(before["offset_x"], 1),
          "oy", round(before["offset_y"], 1))

sensors.bmm350.cal_reset()
lcd.print("ล้างแล้ว - เริ่มหมุนเลขแปดได้เลย")

prev_x = None
prev_y = None
stable = 0
done = False

for r in range(ROUNDS):
    # อ่านย่อยระหว่างรอบ ค่าเดียวกันแต่ถี่กว่า เพื่อให้เส้นบนกราฟมีรูปร่างให้ดู
    for _ in range(SUB_N):
        s = sensors.bmm350.cal_status()
        ch.set_next(s_ox, int(s["offset_x"]))
        ch.set_next(s_oy, int(s["offset_y"]))
        ui.poll()
        time.sleep_ms(100)

    st_now = sensors.bmm350.cal_status()
    ox = st_now["offset_x"]
    oy = st_now["offset_y"]

    if prev_x is None:
        move = 999.0
    else:
        move = abs(ox - prev_x) + abs(oy - prev_y)

    # นับรอบที่ค่านิ่ง ถ้าขยับใหม่ให้เริ่มนับหนึ่ง
    if move < SETTLED_UT:
        stable += 1
    else:
        stable = 0

    seg.text("%.1f" % move if move < 100 else "--")
    bar.value(stable)
    lbl_stab.text("นิ่งติดกัน %d/%d" % (stable, SETTLED_N))
    sub.text("รอบ %d | valid = %s" % (r, st_now["valid"]))
    ui.poll()

    if r % 4 == 0:
        lcd.print(str(r) + ": ox " + str(int(ox)) + " oy " + str(int(oy)) +
                  " ขยับ " + str(round(move, 1)))

    if stable >= SETTLED_N and st_now["valid"]:
        done = True
        st.text("นิ่งแล้วที่รอบ %d" % r)
        st.color(0x30A46C)
        ui.poll()
        lcd.print("<span class=ok>ค่าชดเชยนิ่งแล้วที่รอบ " + str(r) + "</span>")
        break

    prev_x = ox
    prev_y = oy

after = sensors.bmm350.cal_status()
if not done:
    st.text("ยังไม่นิ่งภายใน %d รอบ" % ROUNDS)
    st.color(0xF5A623)
sub.text("ox %.1f oy %.1f | valid = %s"
         % (after["offset_x"], after["offset_y"], after["valid"]))
ui.poll()

lcd.print("หลังคาลิเบรต: ox", round(after["offset_x"], 1),
          "oy", round(after["offset_y"], 1))
lcd.print("valid =", after["valid"], "| heading =", int(sensors.bmm350.heading()))
print("จดค่า offset ไว้ ถ้าย้ายบอร์ดไปติดบนโครงเหล็ก ต้องคาลิเบรตใหม่ทั้งหมด")
print("ขั้นถัดไป เอาค่าไปใช้ที่ examples/s08/06_compass_readout.py")
