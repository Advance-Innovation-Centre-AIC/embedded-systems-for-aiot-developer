# 05_madgwick_and_pedometer.py - สองคลาส IMU ที่เหลือใน dsp และหน่วยที่ดักไว้ทั้งคู่
# ชุดตัวอย่างประจำคาบ 06
#
# ไฟล์นี้สอน: dsp มีคลาสสาย IMU สองตัวที่ยังไม่ได้ใช้ในคาบนี้
#             Madgwick - รวม accel กับ gyro เป็นสามมุม roll pitch yaw
#             Pedometer - นับก้าวให้เสร็จในตัว ไม่ต้องเขียนตรรกะนับเอง
#             ทั้งคู่ไม่มี .value() ต่างจากตัวกรองหกตัวในคาบ 5
#             Madgwick มี .quaternion() กับ .reset() ส่วน Pedometer มีแค่ .reset()
# ดูที่จอ   : แถวบนเป็นสามคอลัมน์ ซ้ายคือสามมุมจาก Madgwick กลางคือ roll/pitch
#             จาก dsp.tilt() ไว้เทียบกัน ขวาคือตัวนับก้าวสองตัว ตัวหนึ่งตั้งเกณฑ์
#             ถูกหน่วย อีกตัวใช้ค่าตั้งต้น เดินอยู่กับที่แล้วดูว่าสองตัวนี้ต่างกันแค่ไหน
#             แถบล่างซ้ายคือการ์ดที่อธิบายกับดักหน่วย ล่างขวาคือปุ่ม
# กับดัก    : (1) Madgwick.update() รับ gyro หน่วย "เรเดียนต่อวินาที"
#                 แต่ sensors.bmi270 คืนมาเป็น "องศาต่อวินาที" ต้องแปลงเอง
#                 ลืมแปลง = ป้อนตัวเลขใหญ่กว่าที่ควรราว 57 เท่า มุมจะหมุนติ้ว
#             (2) Pedometer ตั้งต้น threshold=1.5 ซึ่งเป็นหน่วย g ไม่ใช่ m/s2
#                 ป้อนขนาดความเร่งเป็น m/s2 (วางนิ่งก็ 9.81 แล้ว) มันจะข้ามเกณฑ์
#                 ค้างตลอดเวลา เหลือแค่ min_interval คุมจังหวะ = นับก้าวทั้งที่ไม่ขยับ
#             (3) เกณฑ์ปล่อยของ Pedometer คือ 80% ของ threshold ตายตัว ตั้งเองไม่ได้
#
# บน Eva Kit: ห้ามเรียก sensors.init() ค่าหกแกนมาจาก snapshot ของคอร์จอ
#   การอ่านครั้งแรกหลังรีเซ็ตช้าได้ถึงราว 16 วินาที และอาจโยน OSError ระหว่างนั้น

import dsp
import lcd
import math
import sensors
import time
import ui

DT_MS = 100
FS = 10.0          # ลูป 100 ms = 10 Hz ต้องบอก Madgwick ให้ตรง ไม่งั้นมุมจะเดินผิดอัตรา
G = 9.80665        # ตัวหารที่เปลี่ยน m/s2 เป็น g

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
# สามมุมไม่ได้แยกกันด้วยสี แต่แยกด้วยชื่อที่เขียนนำหน้าค่าทุกบรรทัด
# แปลงเป็นขาวดำแล้วยังอ่านออกทั้งสามมุม ตาม §S7.7.1
COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_ACCENT = 0x4A9EFF
COL_WARN = 0xF5A623
COL_BAD = 0xE5484D
COL_OK = 0x30A46C

lcd.clear()
lcd.console("<h2>Madgwick กับ Pedometer</h2>")

# beta คุมว่าจะเชื่อ accel มากแค่ไหนเทียบกับ gyro ค่าสูง = ดึงกลับหาแรงโน้มถ่วงเร็ว
# แต่สั่นตาม accel มากขึ้น ค่าตั้งต้นคือ 0.1 ส่วน fs ต้องตรงกับคาบลูปจริง
ahrs = dsp.Madgwick(beta=0.15, fs=FS)

# ตัวที่ตั้งเกณฑ์ให้ตรงหน่วยที่เราจะป้อน - เราจะป้อนขนาดเป็น g จึง 1.25 g
ped_ok = dsp.Pedometer(threshold=1.25, min_interval=300)
# ตัวที่ปล่อยค่าตั้งต้นไว้ แล้วป้อน m/s2 เข้าไปตรง ๆ - นี่คือกับดักข้อ 2 ที่จับต้องได้
ped_bad = dsp.Pedometer()

try:
    sensors.bmi270.motion()
except OSError:
    lcd.print("คอร์จอยังไม่ตอบรอบแรก จะลองใหม่ในลูป")

ui.screen()
# ผังจอเป็นสามแถบ ขอบนอก 24 ทุกด้าน - หัวเรื่อง แล้วสามคอลัมน์ค่าอ่าน
# แล้วแถบล่างที่มีการ์ดกับดักหน่วยอยู่ซ้าย และแถวปุ่มอยู่ขวา
# แถวปุ่มจบที่ y=332 จึงพ้นมุมของปุ่ม Console (x>=690 และ y>=340)
ui.Label("Madgwick + Pedometer: หน่วยคือกับดัก", x=24, y=8, value=24,
         color=COL_TEXT)

ui.Label("Madgwick (accel+gyro)", x=24, y=44, value=20, color=COL_DIM)
lbl_mr = ui.Label("roll   ----", x=24, y=72, value=20, color=COL_TEXT)
lbl_mp = ui.Label("pitch  ----", x=24, y=100, value=20, color=COL_TEXT)
lbl_my = ui.Label("yaw    ----", x=24, y=128, value=20, color=COL_TEXT)

ui.Label("dsp.tilt() เทียบกัน", x=280, y=44, value=20, color=COL_DIM)
lbl_tr = ui.Label("roll   ----", x=280, y=72, value=20, color=COL_TEXT)
lbl_tp = ui.Label("pitch  ----", x=280, y=100, value=20, color=COL_TEXT)
ui.Label("tilt ไม่มี yaw ให้", x=280, y=128, value=20, color=COL_DIM)

ui.Label("Pedometer สองตัว", x=520, y=44, value=20, color=COL_DIM)
lbl_ok = ui.Label("g    ----", x=520, y=72, value=20, color=COL_OK)
lbl_bad = ui.Label("m/s2 ----", x=520, y=100, value=20, color=COL_WARN)
lbl_active = ui.Label("active ----", x=520, y=128, value=20, color=COL_DIM)

# แถวเต็มความกว้าง: quaternion ยาวเกินคอลัมน์ จึงมีบรรทัดของตัวเอง
lbl_q = ui.Label("q ----", x=24, y=164, value=20, color=COL_DIM)
lbl_mag = ui.Label("|a| ----", x=520, y=164, value=20, color=COL_DIM)

# การ์ดกับดักหน่วย - สามบรรทัดชิดขอบซ้ายเดียวกันที่ x=40 (ขอบการ์ด 24 + ระยะใน 16)
ui.Panel(x=24, y=204, w=456, h=128)
ui.Label("ตัวส้มนับขึ้นทั้งที่บอร์ดวางนิ่ง", x=40, y=224, value=20,
         color=COL_WARN)
ui.Label("9.81 มากกว่าเกณฑ์ 1.5 ตั้งแต่ยังไม่ขยับ", x=40, y=260, value=20,
         color=COL_WARN)
ui.Label("ค่าตั้งต้นของไลบรารี ไม่ใช่ค่าที่ถูก", x=40, y=296,
         value=20, color=COL_BAD)

# ปุ่มสองใบเรียงข้างกัน กว้าง 116 สูง 88 เว้นกัน 32 ตาม §S7.13.11-12
ui.Label("reset ทั้งสามตัว", x=504, y=204, value=20, color=COL_DIM)
btn_reset = ui.Button("reset", x=504, y=244, w=116, h=88,
                      color=COL_ACCENT, value=20)
btn_exit = ui.Button("ออก", x=652, y=244, w=116, h=88, color=COL_DIM,
                     value=20)
id_reset = btn_reset.id()
id_exit = btn_exit.id()

running = True
while running:
    try:
        ax, ay, az, gx, gy, gz = sensors.bmi270.motion()
    except OSError:
        ui.poll()
        time.sleep_ms(50)
        continue

    # แปลงหน่วยก่อนป้อน - บรรทัดสามบรรทัดนี้คือทั้งหมดของกับดักข้อ 1
    # ถ้าลบสามบรรทัดนี้ทิ้งแล้วป้อน gx gy gz ตรง ๆ มุมจะหมุนเร็วกว่าความจริงราว 57 เท่า
    rx = math.radians(gx)
    ry = math.radians(gy)
    rz = math.radians(gz)

    # update() รับหกค่าเรียงตามนี้เท่านั้น และคืนสามมุมเป็นองศา ไม่ใช่เรเดียน
    m_roll, m_pitch, m_yaw = ahrs.update(ax, ay, az, rx, ry, rz)
    # tilt() ใช้ accel อย่างเดียว จึงไม่มี yaw ให้ - เทียบสองคอลัมน์แล้วจะเห็นว่า
    # roll/pitch ใกล้กันมากตอนวางนิ่ง แต่ตอนสะบัด Madgwick นิ่งกว่า เพราะมี gyro ช่วย
    t_roll, t_pitch = dsp.tilt(ax, ay, az)

    lbl_mr.text("roll  {:+7.1f}".format(m_roll))
    lbl_mp.text("pitch {:+7.1f}".format(m_pitch))
    lbl_my.text("yaw   {:+7.1f}".format(m_yaw))

    # quaternion() คืนสี่ค่าเรียง (w, x, y, z) - นี่คือสถานะจริงที่คลาสนี้เก็บไว้
    # ส่วนสามมุมข้างบนเป็นแค่การแปลงให้คนอ่านออก มุมออยเลอร์มีจุดตายที่ pitch 90 องศา
    qw, qx, qy, qz = ahrs.quaternion()
    lbl_q.text("q {:+.2f} {:+.2f} {:+.2f} {:+.2f}".format(qw, qx, qy, qz))

    lbl_tr.text("roll  {:+7.1f}".format(t_roll))
    lbl_tp.text("pitch {:+7.1f}".format(t_pitch))

    # ตัวเดียวกันทั้งสองตัว ต่างกันแค่หน่วยที่ป้อนเข้าไป
    n_ok, act_ok = ped_ok.update(ax / G, ay / G, az / G)
    n_bad, act_bad = ped_bad.update(ax, ay, az)

    mag = math.sqrt(ax * ax + ay * ay + az * az)
    lbl_ok.text("g    {:5d} ก้าว".format(n_ok))
    lbl_bad.text("m/s2 {:5d} ก้าว".format(n_bad))
    # active เป็น True เฉพาะรอบที่เพิ่งนับก้าวได้จริง ไม่ใช่ค่าค้าง
    lbl_active.text("active {} / {}".format(act_ok, act_bad))
    lbl_mag.text("|a| {:5.2f} m/s2".format(mag))

    for ev in ui.poll():
        h = ev['handle']
        if h == id_reset:
            # Madgwick.reset() ดึง quaternion กลับไปที่ (1,0,0,0) คือท่าอ้างอิง
            # Pedometer.reset() ล้างจำนวนก้าวและสถานะ latch ทิ้ง
            ahrs.reset()
            ped_ok.reset()
            ped_bad.reset()
            lcd.print("reset แล้ว - yaw กลับไป 0 และตัวนับทั้งสองเริ่มใหม่")
        elif h == id_exit:
            running = False

    time.sleep_ms(DT_MS)

ui.clear()
print("yaw ของ Madgwick จะค่อย ๆ หนีเมื่อวางนิ่งนาน ๆ เพราะไม่มี accel ตัวไหน")
print("บอกทิศเหนือได้ ต้องมีเข็มทิศเข้ามาช่วย - ดู 04_compass_and_magnetometer.py")
