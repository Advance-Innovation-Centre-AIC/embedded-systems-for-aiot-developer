# 06_sense_decide_act_report.py - วงจรเต็มสี่ขั้นในไฟล์เดียว
# ชุดตัวอย่างประจำคาบ 12
#
# ไฟล์นี้สอน: โครงของผลิตภัณฑ์ AIoT ทุกตัวคือสี่ขั้นนี้วนไปเรื่อย ๆ
#             วัด -> ตัดสิน -> สั่งของจริง -> รายงานออกไป
#             ห้าไฟล์ก่อนหน้าฝึกทีละขั้น ไฟล์นี้ต่อให้ครบวง
# ดูที่จอ   : แถวสถานะสี่ช่อง สว่างทีละช่องตามขั้นที่กำลังทำ วนซ้ำทุกรอบ
#             ไฟ LED บนบอร์ดติดจริงเมื่อค่าข้ามเกณฑ์ ไม่ใช่แค่ป้ายบนจอเปลี่ยนสี
# กับดัก    : ขั้น "ตัดสิน" ต้องมีช่วงหน่วง (hysteresis) ไม่งั้นค่าที่แกว่งรอบ
#             เกณฑ์พอดีจะสั่งเปิดปิดสลับกันหลายครั้งต่อวินาที - รีเลย์จริงพัง
#             ด้วยวิธีนี้ และไม่มี error ให้จับสักตัว
#
# บน Eva Kit: sensors.snapshot() ใช้ได้ ส่วน gpio.led() คุมไฟบนบอร์ดได้ตรง ๆ
#             ทั้งคู่ไม่ต้องขออนุญาตใคร

import gpio
import json
import lcd
import sensors
import time
import ui

DEVICE_ID = "eva-team03"
ON_ABOVE = 27.5        # ข้ามขึ้นเกินนี้จึงสั่งเปิด
OFF_BELOW = 26.5       # ต้องตกต่ำกว่านี้จึงยอมสั่งปิด - ช่องว่าง 1 องศาคือ hysteresis
REPORT_MS = 3000
ROUNDS = 160

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_ON, COL_OFF = 0x00E676, 0x9AA0A6
COL_WARN = 0xFFC83D

ui.screen()
time.sleep_ms(200)

ui.Label("วัด -> ตัดสิน -> สั่ง -> รายงาน", x=20, y=12, color=COL_TEXT, value=24)

# แถวสี่ขั้น - ป้ายที่สว่างคือขั้นที่กำลังทำอยู่จริงในรอบนี้
ui.Panel(x=20, y=52, w=650, h=64, color=COL_CARD, min=COL_DIM, max=12, value=1)
st = []
NAMES = ("1 วัด", "2 ตัดสิน", "3 สั่ง", "4 รายงาน")
for i in range(4):
    st.append(ui.Label(NAMES[i], x=40 + i * 160, y=72, color=COL_DIM, value=20))

ui.Panel(x=20, y=128, w=330, h=96, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("อุณหภูมิ", x=36, y=140, color=COL_DIM, value=16)
seg = ui.Seg7(text="--", x=36, y=166, w=200, h=46, color=0x40C4FF)

ui.Panel(x=366, y=128, w=304, h=96, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("สถานะพัดลม", x=382, y=140, color=COL_DIM, value=16)
lbl_act = ui.Label("ปิด", x=382, y=168, color=COL_OFF, value=28)

ch = ui.Chart(x=20, y=238, w=548, h=118, min=20, max=35)
s_temp = 0
ui.Label("เส้นบน = เกณฑ์เปิด", x=580, y=246, color=COL_WARN, value=16)
s_on = ch.add_series(COL_WARN)
ui.Label("เส้นล่าง = เกณฑ์ปิด", x=580, y=272, color=COL_DIM, value=16)
s_off = ch.add_series(COL_DIM)

sent_lbl = ui.Label("ยังไม่ได้รายงาน", x=20, y=364, color=COL_DIM, value=18)
ui.poll()

lcd.clear()
lcd.console("<h2>วงจรเต็มสี่ขั้น</h2>")

fan = gpio.led(0)
fan.off()
on = False
reports = 0
last_report = time.ticks_ms()


def light(step):
    """ให้เห็นด้วยตาว่าตอนนี้อยู่ขั้นไหน - ขั้นเดียวเท่านั้นที่สว่าง"""
    for i in range(4):
        st[i].color(COL_ON if i == step else COL_DIM)


for _ in range(ROUNDS):
    # --- 1 วัด -------------------------------------------------------------
    light(0)
    snap = sensors.snapshot()
    if "bmi270" not in snap:
        lcd.print("<span class=warn>ไม่มีค่าจาก IMU รอบนี้</span>")
        ui.poll()
        time.sleep_ms(200)
        continue
    temp = snap["bmi270"]["temperature"]
    seg.text("{:.1f}".format(temp))
    ch.set_next(s_temp, int(temp))
    ch.set_next(s_on, int(ON_ABOVE))
    ch.set_next(s_off, int(OFF_BELOW))

    # --- 2 ตัดสิน ----------------------------------------------------------
    # เกณฑ์สองค่า ไม่ใช่ค่าเดียว: เปิดเมื่อเกินขอบบน ปิดเมื่อต่ำกว่าขอบล่าง
    # ระหว่างสองขอบไม่ทำอะไรเลย นั่นคือสิ่งที่กันการสั่งสลับถี่ ๆ
    light(1)
    want = on
    if not on and temp > ON_ABOVE:
        want = True
    elif on and temp < OFF_BELOW:
        want = False

    # --- 3 สั่งของจริง -----------------------------------------------------
    light(2)
    if want != on:
        on = want
        if on:
            fan.on()
            lbl_act.text("เปิด")
            lbl_act.color(COL_ON)
            lcd.print("<span class=ok>เปิดพัดลมที่", round(temp, 1), "C</span>")
        else:
            fan.off()
            lbl_act.text("ปิด")
            lbl_act.color(COL_OFF)
            lcd.print("ปิดพัดลมที่", round(temp, 1), "C")

    # --- 4 รายงาน ----------------------------------------------------------
    # รายงานห่างกว่าที่วัด ด้วยเหตุผลเดียวกับคาบ 10 - ปลายทางไม่ต้องเห็นทุกรอบ
    light(3)
    now = time.ticks_ms()
    if time.ticks_diff(now, last_report) >= REPORT_MS:
        last_report = now
        reports = reports + 1
        line = json.dumps({"device": DEVICE_ID,
                           "temp_c": round(temp, 1),
                           "fan": 1 if on else 0})
        # ไฟล์นี้ยังไม่ต่อ broker จริง เพื่อให้รันได้แม้ไม่มีเน็ต - คาบ 10 กับ 11
        # สอนวิธีต่อไปแล้ว เอาบรรทัด mqtt.publish(TOPIC, line) มาวางตรงนี้ได้เลย
        print(line)
        sent_lbl.text("รายงานแล้ว " + str(reports) + " ครั้ง")

    ui.poll()
    time.sleep_ms(200)

fan.off()
for i in range(4):
    st[i].color(COL_DIM)
sent_lbl.text("จบ - รายงานทั้งหมด " + str(reports) + " ครั้ง")
ui.poll()
lcd.print("<span class=ok>จบรอบ - พัดลมถูกสั่งจากค่าที่วัดได้จริง</span>")
print("สี่ขั้นนี้คือโครงของงานจบ เปลี่ยนแค่ว่าวัดอะไร ตัดสินด้วยกฎอะไร สั่งอะไร")

# ตาคุณ
# 1) ตั้ง ON_ABOVE กับ OFF_BELOW ให้เท่ากัน แล้วดูว่าพัดลมสั่งสลับกี่ครั้งต่อนาที
#    เมื่ออุณหภูมิแกว่งรอบเกณฑ์พอดี - นี่คือเหตุผลที่ต้องมีช่องว่าง
# 2) เปลี่ยนขั้นที่ 1 ไปอ่านเสียงแทนอุณหภูมิ (mic.level()) โดยไม่แตะขั้น 2-4
#    ถ้าแก้ได้ที่เดียวจบ แปลว่าโครงนี้แยกส่วนถูกต้องแล้ว
