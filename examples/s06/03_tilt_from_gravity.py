# 03_tilt_from_gravity.py - dsp.tilt() ทำอะไรกับสามตัวเลข และทำไมลำดับถึงสำคัญ
# ชุดตัวอย่างประจำคาบ 06
#
# ไฟล์นี้สอน: dsp.tilt(ax, ay, az) คืน (roll, pitch) - roll มาก่อนเสมอ
#             ข้างในมันคือ atan2 สองบรรทัด ไม่มีอะไรลึกลับ ไฟล์นี้คำนวณสูตร
#             เดียวกันด้วย math ของ Python วางไว้ข้าง ๆ ให้เห็นว่าตรงกันเป๊ะ
# ดูที่จอ   : ซ้ายคือ ax ay az ที่อ่านมาสด ๆ กลางคือคำตอบของ dsp.tilt()
#             ขวาคือคำตอบที่คำนวณเองด้วย atan2 - สองคอลัมน์ขวาต้องเท่ากันตลอด
#             แถวล่างสุดคือกับดัก: ค่าเดียวกันถ้าแกะสลับลำดับจะอ่านได้เป็นอะไร
# กับดัก    : เขียน pitch, roll = dsp.tilt(...) แล้วโปรแกรมไม่ error เลย
#             ตัวเลขขึ้นครบ สวยงาม และผิดทั้งหน้า - แถวล่างของจอนี้มีไว้ให้เห็น
#             ว่าความผิดนั้นหน้าตาอย่างไร ก่อนจะไปเจอมันเองในงานของตัวเอง
#
# ต่อจากคาบ 1: examples/s01/13_raw_and_filtered.py เรียก dsp.tilt() ไปแล้วหนึ่งครั้ง
#   แล้วเอาผลไปวาดกราฟเลย โดยไม่ได้เปิดดูว่าข้างในมันทำอะไร ไฟล์นี้คือข้างในนั้น
#   และเป็นที่แรกในคอร์สที่ลำดับ (roll, pitch) ถูกทดสอบจนเห็นว่าสลับแล้วเป็นอย่างไร
#
# บน Eva Kit: ห้ามเรียก sensors.init() มันโยน OSError ทันที ค่าหกแกนมาจาก
#   snapshot ที่คอร์จออ่านค้างไว้ หลังรีเซ็ต การอ่านครั้งแรกช้าได้ถึงราว 16 วินาที
#   และอาจโยน OSError ระหว่างนั้น - ลูปข้างล่างจึงดักไว้ทุกรอบ

import dsp
import lcd
import math
import sensors
import time
import ui

COL_ROLL = 0xFF5252
COL_PITCH = 0x4CAF50
COL_GRAY = 0xA0B4CC

lcd.clear()
lcd.console("<h2>dsp.tilt() คืออะไรกันแน่</h2>")
lcd.print("roll  = atan2(ay, az)")
lcd.print("pitch = atan2(-ax, sqrt(ay*ay + az*az))")
lcd.print("ทั้งสองบรรทัดคูณด้วย 180/pi เพื่อเปลี่ยนเรเดียนเป็นองศา")

# อุ่นเครื่องหนึ่งครั้ง ให้การรอยาว ๆ ไปเกิดก่อนสร้างหน้าจอ
try:
    sensors.bmi270.motion()
except OSError:
    lcd.print("คอร์จอยังไม่ตอบรอบแรก จะลองใหม่ในลูป")

ui.screen()
ui.Label("dsp.tilt() = atan2 สองบรรทัด", x=12, y=8, value=20)

# คอลัมน์ซ้าย: ค่าดิบสามแกน หน่วย m/s2 - วางราบแล้ว az ควรราว +9.8
ui.Label("ความเร่ง (m/s2)", x=12, y=44, value=18, color=COL_GRAY)
lbl_ax = ui.Label("ax  ----", x=12, y=70, value=20)
lbl_ay = ui.Label("ay  ----", x=12, y=98, value=20)
lbl_az = ui.Label("az  ----", x=12, y=126, value=20)
lbl_mag = ui.Label("|a| ----", x=12, y=154, value=20, color=COL_GRAY)

# คอลัมน์กลาง: คำตอบของเฟิร์มแวร์
ui.Label("dsp.tilt()", x=250, y=44, value=18, color=COL_GRAY)
lbl_roll = ui.Label("roll   ----", x=250, y=70, value=20, color=COL_ROLL)
lbl_pitch = ui.Label("pitch  ----", x=250, y=98, value=20, color=COL_PITCH)

# คอลัมน์ขวา: สูตรเดียวกันที่เราเขียนเอง ถ้าสองคอลัมน์นี้ไม่ตรงกัน แปลว่าเราเข้าใจผิด
ui.Label("คำนวณเองด้วย atan2", x=470, y=44, value=18, color=COL_GRAY)
lbl_roll2 = ui.Label("roll   ----", x=470, y=70, value=20, color=COL_ROLL)
lbl_pitch2 = ui.Label("pitch  ----", x=470, y=98, value=20, color=COL_PITCH)
lbl_diff = ui.Label("ต่างกัน ----", x=470, y=126, value=18, color=COL_GRAY)

# กราฟสองเส้น ให้เห็นว่าสองแกนแยกกันจริงตอนเอียงทีละทาง
ch = ui.Chart(x=12, y=190, w=460, h=140, min=-90, max=90, color=COL_ROLL)
s_roll = 0                       # ซีรีส์ 0 มาพร้อมกราฟ สีมาจาก color= ข้างบน
s_pitch = ch.add_series(COL_PITCH)
ui.Label("กราฟ: แดง roll / เขียว pitch", x=12, y=336, value=16, color=COL_GRAY)

# แถวกับดัก: ค่าชุดเดียวกัน อ่านผิดลำดับ แล้วดูว่ามันหน้าตาน่าเชื่อแค่ไหน
ui.Panel(x=484, y=190, w=198, h=140)
ui.Label("ถ้าแกะสลับลำดับ", x=496, y=198, value=18, color=0xFFC107)
lbl_bad1 = ui.Label("pitch อ่านเป็น ----", x=496, y=228, value=18,
                    color=0xFFC107)
lbl_bad2 = ui.Label("roll อ่านเป็น ----", x=496, y=258, value=18,
                    color=0xFFC107)
ui.Label("ไม่มี error ให้เห็นเลย", x=496, y=292, value=18, color=0xFF5252)

btn_exit = ui.Button("ออก", x=496, y=340, w=140, h=50, color=0x546E7A,
                     value=20)
id_exit = btn_exit.id()

R2D = 180.0 / math.pi
running = True

while running:
    try:
        # dsp.tilt() ใช้แค่สามค่าแรก มันไม่ต้องการ gyro เลย เพราะคิดจากแรงโน้มถ่วงล้วน ๆ
        ax, ay, az, gx, gy, gz = sensors.bmi270.motion()
    except OSError:
        # อ่านพลาดหนึ่งรอบ ไม่ควรทำให้หน้าจอดับ ใช้ค่าเดิมไปก่อน
        ui.poll()
        time.sleep_ms(50)
        continue

    # คำตอบจากเฟิร์มแวร์
    roll, pitch = dsp.tilt(ax, ay, az)

    # สูตรเดียวกันที่เขียนเอง - นี่คือทั้งหมดที่อยู่ข้างใน dsp.tilt()
    # roll ถามว่า "แรงโน้มถ่วงเอียงไปทาง y เทียบกับ z แค่ไหน"
    my_roll = math.atan2(ay, az) * R2D
    # pitch ถามว่า "แกน x จมลงเทียบกับระนาบ yz แค่ไหน" - เครื่องหมายลบหน้า ax
    # คือสิ่งที่ทำให้ "หัวเชิด" ได้ค่าบวกแทนที่จะเป็นลบ
    my_pitch = math.atan2(-ax, math.sqrt(ay * ay + az * az)) * R2D

    mag = math.sqrt(ax * ax + ay * ay + az * az)

    lbl_ax.text("ax  {:+7.3f}".format(ax))
    lbl_ay.text("ay  {:+7.3f}".format(ay))
    lbl_az.text("az  {:+7.3f}".format(az))
    # วางนิ่ง |a| ควรราว 9.81 ถ้าไม่ใช่ แปลว่ากำลังขยับอยู่ และ tilt จะเชื่อไม่ได้
    lbl_mag.text("|a| {:6.3f}".format(mag))

    lbl_roll.text("roll  {:+7.2f}".format(roll))
    lbl_pitch.text("pitch {:+7.2f}".format(pitch))
    lbl_roll2.text("roll  {:+7.2f}".format(my_roll))
    lbl_pitch2.text("pitch {:+7.2f}".format(my_pitch))

    d = max(abs(roll - my_roll), abs(pitch - my_pitch))
    lbl_diff.text("ต่างกัน {:.4f} องศา".format(d))

    # แถวกับดัก - เอาค่าคู่เดิมมาอ่านสลับที่ ตัวเลขยังดูสมเหตุสมผลทุกประการ
    lbl_bad1.text("pitch อ่านเป็น {:+.1f}".format(roll))
    lbl_bad2.text("roll อ่านเป็น {:+.1f}".format(pitch))

    ch.set_next(s_roll, int(max(-90, min(90, roll))))
    ch.set_next(s_pitch, int(max(-90, min(90, pitch))))

    for ev in ui.poll():
        if ev['handle'] == id_exit:
            running = False

    time.sleep_ms(200)

ui.clear()
print("ช่อง 'ต่างกัน' อยู่ระดับ 0.0001 องศา เพราะเฟิร์มแวร์คิดด้วย float 32 บิต")
print("ส่วน Python คิดด้วย 64 บิต - ต่างกันแค่นั้น ไม่ใช่สูตรคนละตัว")
