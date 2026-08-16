# 02_event_types.py - เหตุการณ์หน้าตาเป็นอย่างไร และใครส่งอะไร
#
# ไฟล์นี้สอน: ตัวควบคุมแต่ละชนิดส่ง type ไม่เหมือนกัน ต้องรู้ก่อนว่าจะรออะไร
# ดูที่จอ   : ของจริงสี่ตัวให้แตะอยู่แถวบน ใต้ลงมาคือคำสั่งของขั้นนี้
#             และบรรทัดของเหตุการณ์ล่าสุดที่เพิ่งเข้ามา
# กับดัก    : Checkbox ส่ง toggled ไม่ใช่ clicked ทั้งที่หน้าตาเหมือนของกด
#             ถ้าไปรอ clicked จากมัน จะรอทั้งวันโดยไม่มีอะไรเกิดขึ้น

import lcd
import time
import ui

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_OK, COL_WARN, COL_INFO = 0x00E676, 0xFFA726, 0x40C4FF

ui.screen()
time.sleep_ms(200)

ui.Label("ใครส่งเหตุการณ์ชนิดไหน", x=20, y=12, color=COL_TEXT, value=24)
lbl_pos = ui.Label("1 / 4  Button", x=520, y=16, color=COL_OK, value=20)

# ของจริงสี่ตัวที่ส่งเหตุการณ์ต่างชนิดกัน วางเรียงกันให้เทียบง่าย
btn = ui.Button("Button", x=36, y=58, w=130, h=64, color=0x1E88E5, value=16)
# ui.Switch ไม่มีข้อความในตัวเอง ต่างจาก Button และ Checkbox จึงต้องมีป้ายกำกับ
ui.Label("Switch", x=190, y=56, color=COL_DIM, value=14)
sw = ui.Switch(x=190, y=78)
cb = ui.Checkbox("Checkbox", x=290, y=82, color=COL_TEXT)
sld = ui.Slider(x=440, y=84, w=210, min=0, max=100, value=30)

# เก็บ handle คู่กับชื่อที่คนอ่านเข้าใจ เขียนเป็น dict เพราะเรากำลังจะค้นจาก
# handle ซึ่ง dict ทำได้ในก้าวเดียว
NAME_OF = {
    btn.id(): "Button",
    sw.id(): "Switch",
    cb.id(): "Checkbox",
    sld.id(): "Slider",
}

# หนึ่งขั้นคือ (handle ที่ต้องแตะ, ชื่อ, type ที่ต้องได้, เหตุผล)
# เขียนคำตอบไว้ก่อนแตะ ผู้เรียนจึงได้ "ทำนายแล้วตรวจ" ไม่ใช่แค่ "ลองดู"
STEPS = [
    (btn.id(), "Button", "clicked", "ปุ่มส่ง clicked ค่า value เป็น 1 เสมอ"),
    (sw.id(), "Switch", "toggled", "สวิตช์ส่ง toggled ค่า 1 ตอนเปิด 0 ตอนปิด"),
    (cb.id(), "Checkbox", "toggled",
     "กล่องติ๊กส่ง toggled ไม่ใช่ clicked ทั้งที่หน้าตาเหมือนของกด"),
    (sld.id(), "Slider", "value_changed",
     "แถบเลื่อนส่ง value_changed และส่งถี่มากระหว่างที่นิ้วยังลากอยู่"),
]

ask = ui.Label("แตะ Button แล้วต้องได้ clicked", x=20, y=154, color=COL_INFO,
               value=22)
why = ui.Label("ปุ่มส่ง clicked ค่า value เป็น 1 เสมอ", x=20, y=190,
               color=COL_DIM, value=16)
last = ui.Label("ยังไม่มีเหตุการณ์เข้ามา", x=20, y=214, color=COL_DIM, value=16)

# สี่ขั้นเดินวนกลับมาที่ขั้นแรกเอง จึงไม่ต้องมีปุ่มย้อน
# เว้นมุมขวาล่างไว้ให้ปุ่ม Console ของหน้า Playground
btn_next = ui.Button("เดินหน้า", x=20, y=250, w=180, h=64, color=0x1E88E5,
                     value=20)
btn_home = ui.Button("เริ่มใหม่", x=216, y=250, w=160, h=64, color=0x6A1B9A,
                     value=20)

ID_NEXT, ID_HOME = btn_next.id(), btn_home.id()

# ปุ่มเดินเรื่องก็ส่ง clicked เหมือนกัน ต้องกันมันออกจากการตรวจ
# ไม่งั้นกดเดินหน้าแล้วโปรแกรมจะนับว่าผู้เรียนตอบขั้น Button ถูก
NAV_IDS = (ID_NEXT, ID_HOME)

lcd.clear()
lcd.console("<h2>ใครส่งเหตุการณ์ชนิดไหน</h2>")
lcd.print("Button=clicked - Switch/Checkbox=toggled - Slider=value_changed")

step = 0                # ขั้นปัจจุบัน ความจริงของโปรแกรมอยู่ที่ตัวนี้


def show():
    """วาดคำสั่งของขั้นปัจจุบัน โดยยังไม่ตัดสินว่าถูกหรือผิด"""
    want_id, who, want_type, reason = STEPS[step]
    lbl_pos.text(str(step + 1) + " / " + str(len(STEPS)) + "  " + who)
    ask.text("แตะ " + who + " แล้วต้องได้ " + want_type)
    ask.color(COL_INFO)
    why.text(reason)


def judge(ev):
    """เทียบเหตุการณ์ที่เข้ามากับสิ่งที่ขั้นนี้บอกไว้ แล้วรายงานผลตรวจ"""
    want_id, who, want_type, reason = STEPS[step]
    handle = ev["handle"]
    got = NAME_OF.get(handle, "?")

    # ทั้งสามช่องมีเสมอ ไม่ต้องเช็กว่ามีไหม
    last.text("handle=" + str(handle) + "  type=" + ev["type"] +
              "  value=" + str(ev["value"]))
    lcd.print(got + " " + ev["type"] + " value=" + str(ev["value"]))

    if handle == want_id and ev["type"] == want_type:
        ask.text(who + " ส่ง " + want_type + " ถูกต้องตามที่บอกไว้")
        ask.color(COL_OK)
    elif handle == want_id:
        ask.text(who + " ส่ง " + ev["type"] + " ไม่ใช่ " + want_type)
        ask.color(COL_WARN)
    else:
        ask.text("นั่นคือ " + got + " - ขั้นนี้ให้แตะ " + who)
        ask.color(COL_WARN)


show()

while True:
    for ev in ui.poll():
        handle = ev["handle"]

        # ปุ่มเดินเรื่องจัดการก่อน แล้ว continue เพื่อไม่ให้ตกไปถึงการตรวจ
        if handle in NAV_IDS and ev["type"] == "clicked":
            step = (step + 1) % len(STEPS) if handle == ID_NEXT else 0
            show()
            continue

        judge(ev)

    # หน่วงสั้น ๆ เท่านั้น ถ้าหลับนานกว่านี้ ui.poll() จะไม่ทำงานระหว่างหลับ
    # แล้วปุ่มจะกดไม่ติด ซึ่งดูเหมือนจอค้าง ทั้งที่โปรแกรมยังเดินอยู่
    time.sleep_ms(30)
