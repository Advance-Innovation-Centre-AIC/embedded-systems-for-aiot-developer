# s08_dashboard.py - Mini-HMI แดชบอร์ด 4 การ์ด บน Eva Kit
# วิธีรัน: 1) บนจอบอร์ด แตะการ์ด Playground ค้างหน้านี้ไว้ตลอดคาบ
#          2) แก้ชื่อทีมในบรรทัด TEAM ให้เป็นของทีมเรา
#          3) กด Program to Device แล้วปล่อยให้รันยาว 10 นาที พร้อมจับเวลา
#
# งบ widget ของไฟล์นี้ = 32   (เพดานของเฟิร์มแวร์คือ 64 ห้ามเกิน)
#   หัวเรื่อง 1 + ไฟค่าค้าง 1 + ป้ายค่าค้าง 1 + แถบสถานะ 1              =  4
#   การ์ด IMU      Panel + หัวข้อ + Chart + Label ค่า                  =  4
#   การ์ดเข็มทิศ   Panel + หัวข้อ + Compass + Label องศา + Label ทิศ   =  5
#   การ์ดสัมผัส    Panel + หัวข้อ + ไฟ 2 ดวง + ป้าย 2 + Bar + Scale + % =  9
#   การ์ด pot      Panel + หัวข้อ + Arc + Seg7 + Label โวลต์           =  5
#   แถบคำสั่ง      ปุ่ม 2 + ป้ายเกณฑ์ + Spinbox + ไฟเตือน + ป้าย       =  5
# จะเพิ่มอะไร ให้แก้ตัวเลขบล็อกนี้ก่อนพิมพ์โค้ดเสมอ
#
# ดูที่จอ: สี่การ์ดคือค่าที่วัดได้ แถบล่างสุดคือสิ่งที่ผู้ใช้ "สั่ง" ได้
#         แดชบอร์ดที่มีแต่ของให้ดู ไม่ใช่แผงควบคุม มันคือโปสเตอร์ที่ขยับได้
# กับดัก : ค่าที่อ่านไม่ได้ ห้ามแทนด้วยศูนย์เงียบ ๆ เพราะศูนย์คือค่าที่ดูเหมือนวัดมาจริง
#         ไฟล์นี้จึงคงค่าล่าสุดไว้ แล้วจุดไฟ "ค่าค้าง" บอกว่าเลขบนจอไม่ใช่ของตอนนี้

import ui
import sensors
import time

TEAM = "BentoBuilders"
UI_TEXT_MS = 1000    # ตัวเลขที่คนต้องอ่าน เขียนใหม่ไม่เกินวินาทีละครั้ง
STALE_MS = 3000      # อ่านไม่ได้ติดกันเกินเท่านี้ ถือว่าเลขบนจอเป็นของเก่า

ui.screen()          # เปิดหน้าจอ Playground แบบว่าง (ล้าง widget เดิมทุกตัวให้เอง)
time.sleep_ms(200)   # ให้ CM55 ตามทันก่อนเราเริ่มสร้างของใหม่
# ---------- ชุดสีเดียวกับแดชบอร์ดของเฟิร์มแวร์ ----------
# ต้นทางคือ ui_theme.py ในชุดตัวอย่างของเฟิร์มแวร์ Eva Kit ไม่ใช่ไฟล์ในหลักสูตรนี้
# ทำไมยืมสีจากเฟิร์มแวร์: ผู้ใช้เห็นหน้าจอเราต่อจากหน้าจอเครื่อง ถ้าสีคนละชุด
# สมองจะอ่านว่าเป็นคนละระบบ ความสม่ำเสมอของสีคือส่วนหนึ่งของการออกแบบ HMI
BG_CARD   = 0x142240   # พื้นการ์ด น้ำเงินเข้ม
COL_WHITE = 0xFFFFFF   # ตัวเลขพระเอก
COL_GRAY  = 0xA0B4CC   # ข้อความประกอบ / สถานะเงียบ
COL_IMU   = 0x4CAF50   # BMI270 เขียว
COL_COMP  = 0xE040FB   # BMM350 ม่วง
COL_TOUCH = 0x00BCD4   # CapSense ฟ้าน้ำทะเล
COL_POT   = 0x8BC34A   # Potentiometer เขียวมะนาว
COL_STAT  = 0x00E676   # เขียว "ปกติ"
# สองสีนี้สงวนไว้ให้เรื่องเดียวคือการเตือน ห้ามเอาไปใช้กับกราฟหรือเข็มทิศ
# เพราะสีที่ใช้ทั้งกับของปกติและของผิดปกติ จะไม่มีความหมายอะไรเหลืออยู่เลย
COL_WARN  = 0xFFC83D   # เหลือง "ค่าเชื่อไม่ได้"
COL_ALERT = 0xFF5252   # แดง "ต้องลงมือ"
# ---------- เซนเซอร์บนบอร์ดนี้ไม่ต้องเปิด แต่ต้องอุ่นเครื่อง ----------
# IMU แถบสัมผัส และลูกบิด อยู่บนบัสที่คอร์จอ (CM55) ถือไว้คนเดียว ฝั่ง Python
# ขอค่าที่คอร์จออ่านเก็บไว้ให้ทุก 200 ms จึงเรียกอ่านได้เลยโดยไม่ต้อง init
# sensors.init() บนบอร์ดนี้จะโยน OSError ทันที ไม่ใช่ค้าง อย่าใส่กลับเข้ามา
# ส่วนเข็มทิศ BMM350 อยู่คนละบัส (I3C) ไม่เกี่ยวกับเรื่องนี้เลย และอ่านตรงได้
#
# หลังรีเซ็ต คอร์จอเริ่มตอบเรื่องเซนเซอร์ราว 13 วินาที การอ่านครั้งแรกจึงนิ่ง
# ได้ถึง 16 วินาที ยิงหนึ่งครั้งตรงนี้ ให้การรอไปเกิดก่อนสร้างการ์ดทั้งสี่ใบ
try:
    sensors.bmi270.motion()
except OSError:
    print("คอร์จอยังไม่ตอบรอบแรก จะลองใหม่ในลูป")
# ---------- แถบหัว: ชื่อทีม ไฟค่าค้าง และหลักฐานว่าลูปยังเดิน ----------
ui.Label("แดชบอร์ดทีม " + TEAM, x=8, y=8, color=COL_WHITE, value=20)
# ไฟดวงนี้คือคำตอบของคำถาม "เลขที่เห็นอยู่ตอนนี้ ใช่ค่าปัจจุบันไหม"
led_stale = ui.Led(x=452, y=10, w=22, h=22, color=COL_WARN, value=0)
ui.Label("ค่าค้าง", x=480, y=10, color=COL_GRAY, value=16)
# เลขรอบที่หยุดนิ่ง = ลูปตาย ส่วนเลข loop ms ที่ค่อย ๆ โตขึ้น = เริ่มมีอะไรสะสม
# แถบนี้ทำหน้าที่เดียวกับไฟหัวใจเต้นของอุปกรณ์จริง - แดชบอร์ดที่ค้างภาพสวย ๆ ไว้
# แยกไม่ออกจากแดชบอร์ดที่ยังทำงาน ถ้าไม่มีอะไรบนจอที่ขยับตามรอบลูป
head = ui.Label("รอบที่ 0 | loop 0 ms", x=560, y=10, color=COL_GRAY, value=16)
# ---------- ท่าที่ 1: การ์ด IMU (ซ้ายบน) ----------
# Panel คือ "การ์ด": color=สีพื้น  min=สีขอบ  max=รัศมีมุม  value=ความหนาขอบ
# สร้าง Panel ก่อน Label เสมอ ของที่สร้างทีหลังจะอยู่ทับด้านบน
imu_panel = ui.Panel(x=8, y=32, w=386, h=160,
                     color=BG_CARD, min=COL_IMU, max=12, value=2)
imu_title = ui.Label("ความเร่ง BMI270 (m/s2)", x=20, y=36, color=COL_IMU, value=18)
# Chart เก็บ 50 จุด รับเฉพาะจำนวนเต็ม เราจึงคูณ 10 ก่อนป้อน (-150..150 = -15.0..+15.0)
# ถ้าป้อน int(ax) ตรง ๆ ความละเอียดจะเหลือ 1 m/s2 ต่อขั้น กราฟจะดูเป็นขั้นบันได
imu_chart = ui.Chart(x=18, y=60, w=366, h=94, min=-150, max=150, color=COL_IMU)
sy = imu_chart.add_series(COL_STAT)     # แกน Y เขียว
sz = imu_chart.add_series(0x448AFF)     # แกน Z ฟ้า
imu_val = ui.Label("X+0.0 Y+0.0 Z+9.8", x=20, y=160, color=COL_WHITE, value=16)
# ---------- ท่าที่ 2: การ์ดเข็มทิศ (ขวาบน) ----------
comp_panel = ui.Panel(x=402, y=32, w=382, h=160,
                      color=BG_CARD, min=COL_COMP, max=12, value=2)
comp_title = ui.Label("เข็มทิศ BMM350", x=414, y=36, color=COL_COMP, value=18)
# Compass ใช้ w เป็นเส้นผ่านศูนย์กลาง - ระบุ h เท่ากันไว้ด้วย เพื่อให้ด่านตรวจ
# พิกัดอ่านขนาดของมันได้ ตัวไหนที่ด่านอ่านไม่ได้ คือตัวที่ยังไม่มีใครตรวจ
compass = ui.Compass(x=418, y=58, w=110, h=110, color=COL_COMP)
# ตัวเลของศาตัวใหญ่คือของที่ต้องอ่านออกจากอีกฝั่งห้อง จึงให้พื้นที่มากสุดในการ์ด
comp_deg = ui.Label("000 deg", x=548, y=74, color=COL_WHITE, value=28)
comp_dir = ui.Label("N", x=548, y=124, color=COL_COMP, value=24)
# ---------- ท่าที่ 3: การ์ดสัมผัส (ซ้ายล่าง) ----------
cap_panel = ui.Panel(x=8, y=200, w=386, h=130,
                     color=BG_CARD, min=COL_TOUCH, max=12, value=2)
cap_title = ui.Label("แถบสัมผัส CapSense", x=20, y=204, color=COL_TOUCH, value=18)
# ไฟสองดวงแทนข้อความที่เปลี่ยนสี - ถ่ายจอเป็นขาวดำแล้วยังแยกออกว่าดวงไหนติด
# ส่วนคำว่า ON สีเขียวกับ --- สีเทา พอเป็นขาวดำแล้วอ่านไม่ออกว่าอันไหนคืออันไหน
cap_led0 = ui.Led(x=22, y=232, w=26, h=26, color=COL_TOUCH, value=0)
ui.Label("ปุ่ม 0", x=56, y=234, color=COL_GRAY, value=16)
cap_led1 = ui.Led(x=150, y=232, w=26, h=26, color=COL_TOUCH, value=0)
ui.Label("ปุ่ม 1", x=184, y=234, color=COL_GRAY, value=16)
cap_pct = ui.Label("แถบเลื่อน 0 %", x=270, y=234, color=COL_WHITE, value=16)
# แถบค่าอยู่เหนือไม้บรรทัด - Scale ไม่มีเข็มและไม่รับ .value() ตัวที่ขยับคือ Bar
cap_bar = ui.Bar(x=22, y=270, w=350, h=12, min=0, max=100, value=0, color=COL_TOUCH)
cap_sc = ui.Scale(x=22, y=284, w=350, h=40, color=COL_GRAY, min=0, max=100)
# ---------- ท่าที่ 4: การ์ดลูกบิด (ขวาล่าง) ----------
pot_panel = ui.Panel(x=402, y=200, w=382, h=130,
                     color=BG_CARD, min=COL_POT, max=12, value=2)
pot_title = ui.Label("ลูกบิด Potentiometer", x=414, y=204, color=COL_POT, value=18)
# Arc ให้ความรู้สึก "อยู่ตรงไหนของช่วง" ส่วน Seg7 ให้ตัวเลขที่จดลงใบงานได้ ใช้คู่กัน
pot_arc = ui.Arc(x=414, y=228, w=94, h=94, min=0, max=100, value=0)
pot_seg7 = ui.Seg7(x=520, y=232, w=140, h=44, color=COL_POT, min=0, max=100, value=0)
pot_volt = ui.Label("0.000 V", x=520, y=286, color=COL_WHITE, value=16)
# ---------- ท่าที่ 5: แถบคำสั่ง - สิ่งที่ผู้ใช้สั่งได้ ไม่ใช่แค่ของให้ดู ----------
# แดชบอร์ดที่ผู้ใช้แตะอะไรไม่ได้เลย คือโปสเตอร์ที่ตัวเลขขยับได้ ไม่ใช่แผงควบคุม
# ปุ่มเดินหน้ากับหยุดภาพแยกกันคนละปุ่ม เพราะปุ่มสลับปุ่มเดียวบอกไม่ได้ว่าตอนนี้อยู่โหมดไหน
btn_run = ui.Button("เดินหน้า", x=8, y=340, w=120, h=48, color=0x1B5E20, value=16)
btn_hold = ui.Button("หยุดภาพ", x=136, y=340, w=120, h=48, color=0x37474F, value=16)
ui.Label("เกณฑ์เตือนลูกบิด (%)", x=272, y=352, color=COL_GRAY, value=14)
# Spinbox แทนการฝังเลขเกณฑ์ไว้ในโค้ด - คนหน้างานเป็นคนรู้ว่าเกณฑ์ควรเป็นเท่าไร
# ไม่ใช่คนเขียนโปรแกรมเมื่อสามเดือนก่อน และมันกันพิมพ์เกินพิสัยให้ด้วยในตัว
spin_thr = ui.Spinbox(x=436, y=340, w=110, h=48, color=COL_WHITE,
                      min=0, max=100, value=80)
led_alarm = ui.Led(x=560, y=350, w=26, h=26, color=COL_ALERT, value=0)
ui.Label("เกินเกณฑ์", x=594, y=352, color=COL_GRAY, value=14)

DIRS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
rounds = 0
running = True          # ปุ่มบนจอเป็นคนเปลี่ยนค่านี้ ไม่ใช่โค้ดที่ไหนอีก
threshold = 80          # ค่าเริ่มต้น ตรงกับ value= ของ Spinbox ข้างบน
stale_shown = False
alarm_shown = False
last_text = time.ticks_ms()
t_good = time.ticks_ms()
ax, ay, az = 0.0, 0.0, 9.81      # ค่าล่าสุดที่อ่านได้จริง ไม่ใช่ศูนย์ที่แกล้งทำเป็นค่า
heading = 0.0
pct, volts = 0.0, 0.0
print("s08 dashboard started - 32 widgets - cadence 200 ms")
# ---------- ท่าที่ 6: ลูปหลัก 200 ms ----------
try:
    while True:
        t0 = time.ticks_ms()
        ok = True

        # อ่านทีละตัวในลูปเดียว ทุกบรรทัดที่อ่านคือการถามคอร์จอหนึ่งรอบ
        # motion() ได้ 6 แกนจากการถามรอบเดียว จึงถูกกว่าเรียก acceleration()+gyroscope()
        # ห่อ try ไว้ทุกตัว เพราะแดชบอร์ดที่ดับทั้งหน้าเพราะอ่านพลาดรอบเดียว ใช้งานจริงไม่ได้
        #
        # ที่สำคัญกว่านั้น: except ไม่ได้เขียนศูนย์ทับค่าเดิม มันคงค่าล่าสุดไว้แล้วยกธง
        # ศูนย์คือตัวเลขที่หน้าตาเหมือนค่าที่วัดมาจริง คนอ่านจึงแยกไม่ออกว่าอ่านไม่ได้
        if running:
            try:
                ax, ay, az, gx, gy, gz = sensors.bmi270.motion()
            except OSError:
                ok = False
            imu_chart.set_next(0, int(ax * 10))
            imu_chart.set_next(sy, int(ay * 10))
            imu_chart.set_next(sz, int(az * 10))

            # BMM350 อยู่บนบัส I3C ของตัวเอง ไม่ได้แชร์กับใคร จึงอ่านตรงจากชิปได้จริง
            # % 360 กันค่าที่หลุดขอบ ถ้า Compass ได้ค่า 361 เข็มจะกระตุกกลับ
            # การ์ดใบนี้เป็นรุ่นย่อของ examples/s08/06_compass_readout.py ซึ่งวาดกราฟทิศตามเวลา
            # ไว้ด้วย ทีมที่จะเอาเข็มทิศไปใช้จริงควรอ่านไฟล์นั้น เพราะมันบอกวิธีดูว่า
            # ค่าที่ได้เชื่อได้หรือยัง - วางนิ่งแล้วกราฟต้องนิ่งตาม ถ้ายังสั่นคือยังคาลิเบรตไม่พอ
            try:
                heading = sensors.bmm350.heading() % 360.0
            except OSError:
                ok = False
            compass.value(int(heading))

            # read() คืนทั้งสามค่าในการเรียกครั้งเดียว ประหยัดกว่า buttons()+slider()
            try:
                cap = sensors.capsense.read()
            except OSError:
                cap = {'btn0': False, 'btn1': False, 'slider': 0}
                ok = False
            # ไฟหรี่ ไม่ใช่หาย ตอนไม่ได้แตะ - ดวงที่หายไปทำให้คนดูแยกไม่ออกว่าจอเสียหรือเปล่า
            cap_led0.value(1 if cap['btn0'] else 0)
            cap_led1.value(1 if cap['btn1'] else 0)
            cap_bar.value(int(cap['slider']))

            try:
                pct = sensors.pot.percent()
                volts = sensors.pot.voltage()
            except OSError:
                ok = False
            pot_arc.value(int(pct))

            if ok:
                t_good = time.ticks_ms()

        # ไฟค่าค้างเขียนเฉพาะตอนเปลี่ยน คิวคำสั่งของจอมีก้นถัง และคำสั่งเปลี่ยนข้อความ
        # คือกลุ่มแรกที่เฟิร์มแวร์ทิ้งเมื่อคิวเต็ม การยิงทุกรอบจึงแลกมาด้วยเลขที่ค้างเอง
        stale = time.ticks_diff(time.ticks_ms(), t_good) >= STALE_MS
        if stale != stale_shown:
            stale_shown = stale
            led_stale.value(1 if stale else 0)

        # ไฟเตือนเทียบกับเกณฑ์ที่คนหน้างานตั้งเอง ไม่ใช่เกณฑ์ที่ฝังไว้ในโค้ด
        alarm = pct >= threshold
        if alarm != alarm_shown:
            alarm_shown = alarm
            led_alarm.value(1 if alarm else 0)

        # ---------- ตัวเลขที่คนต้องอ่าน ขยับไม่เกินวินาทีละครั้ง ----------
        # กราฟ แถบ เข็ม และไฟ ขยับที่ 200 ms ได้ เพราะตาอ่านรูปทรงไม่ได้อ่านหลัก
        # แต่ตัวเลขที่กระพริบห้าครั้งต่อวินาที ไม่มีใครอ่านทัน และไม่มีใครได้อะไรจากมัน
        if time.ticks_diff(time.ticks_ms(), last_text) >= UI_TEXT_MS:
            last_text = time.ticks_ms()
            imu_val.text("X{:+.1f} Y{:+.1f} Z{:+.1f}".format(ax, ay, az))
            comp_deg.text("{:03.0f} deg".format(heading))
            comp_dir.text(DIRS[int((heading + 22.5) / 45.0) % 8])
            cap_pct.text("แถบเลื่อน {} %".format(cap['slider'] if running else 0))
            # Seg7 รับ "ข้อความ" ไม่ใช่ "ค่า" - ui_widget_mgr_set_value() ในเฟิร์มแวร์
            # ไม่มี case ของ UI_WIDGET_SEG7 เลย ถ้าเรียก .value() ตัวเลขจะค้างที่
            # 0000 ตลอดกาลทั้งบนบอร์ดและใน emulator โดยไม่มี error ให้เห็น
            pot_seg7.text("{:.1f}".format(pct))
            pot_volt.text("{:.3f} V".format(volts))
            head.text("รอบที่ {} | loop {} ms".format(
                rounds, time.ticks_diff(time.ticks_ms(), t0)))

        rounds += 1

        # กฎเหล็กข้อ 1: ต้องเรียก poll ทุกลูป ไม่งั้นจอจะซ่อน widget ราวสองวินาที
        # และคาบนี้มีปุ่มให้กดจริงแล้ว poll จึงไม่ใช่แค่จังหวะหายใจของ CM55 อีกต่อไป
        for ev in ui.poll():
            if ev["type"] == "clicked":
                if ev["handle"] == btn_run.id():
                    running = True
                elif ev["handle"] == btn_hold.id():
                    # หยุดภาพไม่ได้หยุดโปรแกรม เลขรอบยังเดิน คนดูจึงรู้ว่าเครื่องไม่ได้ค้าง
                    running = False
            elif ev["type"] == "value_changed" and ev["handle"] == spin_thr.id():
                threshold = ev["value"]

        # 200 ms คือจังหวะที่พิสูจน์แล้วว่าแดชบอร์ดหนักรันยาวได้ ถ้าเร่งให้เร็วกว่านี้
        # เฟรมจะหายเงียบ ๆ โดยไม่มี error ให้เห็น
        time.sleep_ms(200)

except KeyboardInterrupt:
    ui.clear()
    print("dashboard stopped after {} rounds".format(rounds))
