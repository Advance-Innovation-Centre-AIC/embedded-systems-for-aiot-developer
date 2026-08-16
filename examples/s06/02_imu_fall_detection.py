# 02_imu_fall_detection.py - ตรวจการล้มด้วยลำดับสองเหตุการณ์
# ชุดตัวอย่างประจำคาบ 06
#
# ไฟล์นี้สอน: การล้มแยกจากการวางของแรง ๆ ได้ด้วยลำดับสามขั้น คือตกอิสระ
#             แล้วกระแทกภายในเวลาสั้น ๆ แล้วนิ่งต่ออีกพักหนึ่ง
# ดูที่จอ   : กราฟขนาดความเร่ง พร้อมเส้นเกณฑ์ตกอิสระ (เหลืองล่าง) และกระแทก
#             (แดงบน) ขวามือเป็นสามขั้นที่สว่างทีละขั้นตามที่เครื่องเชื่อ
# กับดัก    : ถ้าตรวจแค่แรงกระแทกอย่างเดียว จะเตือนผิดทุกครั้งที่วางของลงโต๊ะ
#             ระบบที่เตือนผิดบ่อย ผู้ใช้จะถอดทิ้ง แล้วมันก็ช่วยอะไรไม่ได้เลย
#
# บน Eva Kit: sensors.init() และ sensors.scan() ปฏิเสธด้วย OSError ไม่ต้องเรียก
#   ส่วน sensors.bmi270.* ใช้ได้เลยโดยไม่ต้อง init - มันอ่านจาก snapshot ที่ CM55
#   ส่งมาทาง IPC (modsensors.c) หลังรีเซ็ต การอ่านครั้งแรกช้าได้ถึงราว 16 วินาที
#   และอาจโยน OSError ระหว่างนั้น - magnitude() จึงดักไว้

import lcd
import math
import sensors
import time
import ui

FREE_FALL = 3.0    # ขนาดต่ำกว่านี้ (m/s^2) ถือว่ากำลังตกอิสระ
IMPACT = 25.0      # ขนาดสูงกว่านี้ ถือว่าเป็นการกระแทก
WINDOW_MS = 800    # กระแทกต้องเกิดภายในเวลานี้หลังตกอิสระ
STILL_MS = 1500    # หลังกระแทกต้องนิ่งนานเท่านี้ ถึงจะเชื่อว่าล้มจริง
IDLE, FELL, HIT = 0, 1, 2
Y_MAX = 400        # แกน Y ของกราฟ = ขนาดความเร่ง x10 จึงกิน 0 ถึง 40 m/s^2

ON = 0x00E676      # สีของขั้นที่เครื่องเชื่อแล้ว
OFF = 0x666666     # สีของขั้นที่ยังไม่ถึง


def magnitude():
    try:
        ax, ay, az, _, _, _ = sensors.bmi270.motion()
    except OSError:
        # หลังรีเซ็ต CM55 ยังไม่พร้อมตอบ คืนค่านิ่ง 1 g แทน ไม่ใช่ปล่อยให้ตาย
        return 9.8
    return math.sqrt(ax * ax + ay * ay + az * az)


lcd.clear()
lcd.console("<h2>ตรวจการล้ม</h2>")
lcd.print("ตกอิสระ <", FREE_FALL, "| กระแทก >", IMPACT, "m/s2")

# ---- หน้าจอ ------------------------------------------------------------------
ui.screen()
ui.Label("ตรวจการล้ม - ลำดับสามขั้น", x=12, y=6, value=24)
ch = ui.Chart(x=12, y=40, w=470, h=232, min=0, max=Y_MAX)
s_mag = 0                          # ซีรีส์ 0 เกิดพร้อมกราฟ สีฟ้าเริ่มต้น
s_ff = ch.add_series(0xFFC107)
s_hit = ch.add_series(0xFF5555)
ui.Label("ฟ้า = ขนาดความเร่ง", x=496, y=42, value=16, color=0x00BFFF)
ui.Label("เหลือง = เกณฑ์ตกอิสระ", x=496, y=64, value=16, color=0xFFC107)
ui.Label("แดง = เกณฑ์กระแทก", x=496, y=86, value=16, color=0xFF5555)

steps_ui = [
    ui.Label("1 ตกอิสระ", x=504, y=122, value=20, color=OFF),
    ui.Label("2 กระแทก", x=504, y=152, value=20, color=OFF),
    ui.Label("3 นิ่ง", x=504, y=182, value=20, color=OFF),
]

ui.Label("แจ้งเตือน", x=496, y=238, value=16)
seg = ui.Seg7(x=600, y=232, w=90, h=36)

# สองป้ายแทนหนึ่งก้อน - ui.Label ตัดที่ 126 ไบต์ และไทยตัวละ 3 ไบต์
ui.Label("ตรวจแค่แรงกระแทกอย่างเดียว", x=24, y=294, value=20, color=0xFFC107)
ui.Label("เตือนผิดทุกครั้งที่วางของลงโต๊ะ", x=24, y=324, value=20,
         color=0xFFC107)


def light(upto):
    # ขั้นที่ 0 คือยังไม่เกิดอะไร ขั้นที่ 1-3 คือเชื่อไปแล้วกี่ขั้น
    for i, w in enumerate(steps_ui):
        w.color(ON if i < upto else OFF)


HOLD_MS = 2000     # แจ้งเตือนแล้วค้างสามขั้นไว้เท่านี้ ให้คนหน้าจอทันเห็น

state = IDLE
mark = 0
alerts = 0
seg.text("0")
lit = -1
alert_at = 0
alerted = False

for _ in range(2500):
    m = magnitude()
    now = time.ticks_ms()

    # กราฟรับได้เฉพาะจำนวนเต็ม จึงคูณสิบก่อนแล้วอ่านแกน Y เป็นสิบเท่าของ m/s^2
    ch.set_next(s_mag, int(m * 10))
    ch.set_next(s_ff, int(FREE_FALL * 10))
    ch.set_next(s_hit, int(IMPACT * 10))

    if state == IDLE:
        if m < FREE_FALL:
            state = FELL
            mark = now
            lcd.print("1) ตกอิสระ - ขนาด", round(m, 1))

    elif state == FELL:
        if m > IMPACT:
            state = HIT
            mark = now
            lcd.print("2) กระแทก - ขนาด", round(m, 1))
        elif time.ticks_diff(now, mark) > WINDOW_MS:
            state = IDLE          # ตกแล้วไม่กระแทก อาจแค่ยกขึ้นเร็ว ๆ

    # ต้องนิ่งจริง คนที่ลุกขึ้นเองทันทีไม่ถือว่าต้องเรียกช่วยเหลือ
    elif state == HIT:
        if abs(m - 9.8) > 4.0:
            state = IDLE
            lcd.print("<span class=muted>ขยับต่อ - ยกเลิก</span>")
        elif time.ticks_diff(now, mark) >= STILL_MS:
            state = IDLE
            alerts += 1
            alert_at = now
            alerted = True
            seg.text(str(alerts))
            lcd.print("<span class=ok>3) นิ่งหลังกระแทก - แจ้งเตือน</span>")

    # สามขั้นบนจอคือสถานะของเครื่อง ไม่ใช่คำบรรยาย จึงต้องสว่างตามจริง
    if alerted and time.ticks_diff(now, alert_at) >= HOLD_MS:
        alerted = False
    upto = 3 if alerted else state
    if upto != lit:
        lit = upto
        light(upto)

    ui.poll()
    time.sleep_ms(20)

print("แจ้งเตือนไป", alerts, "ครั้ง | ลองวางบอร์ดลงโต๊ะแรง ๆ ดูว่าเตือนหรือไม่")
