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

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC S7.13
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_WARN = 0x30A46C, 0xF5A623

ui.screen()
time.sleep_ms(200)

ui.Label("ใครส่งเหตุการณ์ชนิดไหน", x=24, y=8, color=COL_TEXT, value=28)
# ตัวนับขั้นไม่ใช่สถานะ จึงเป็นสีข้อความรอง ไม่ใช่เขียว
lbl_pos = ui.Label("1 / 4  Button", x=600, y=16, color=COL_DIM, value=20)

# ของจริงสี่ตัวที่ส่งเหตุการณ์ต่างชนิดกัน วางเรียงเป็นแถวเดียวให้เทียบง่าย
# ป้ายชื่อชนิดอยู่เหนือทุกตัว ไม่ใช่เฉพาะ Switch เพราะคำสั่งบนจออ้างชื่อชนิด
# ผู้เรียนจึงต้องหาเจอว่าตัวไหนคือตัวไหนโดยไม่ต้องเดา
ui.Label("Button", x=24, y=56, color=COL_DIM, value=20)
ui.Label("Switch", x=216, y=56, color=COL_DIM, value=20)
ui.Label("Checkbox", x=368, y=56, color=COL_DIM, value=20)
ui.Label("Slider", x=576, y=56, color=COL_DIM, value=20)

# ทั้งสี่สูง 88 ตามขั้นต่ำของเป้าสัมผัส และเว้นห่างกัน 32 ทุกช่อง
# 24 + 160 + 32 + 120 + 32 + 176 + 32 + 192 = 768 พอดีขอบขวา
btn = ui.Button("แตะฉัน", x=24, y=96, w=160, h=88, color=COL_ACCENT, value=24)
sw = ui.Switch(x=216, y=96, w=120, h=88, color=COL_ACCENT)
cb = ui.Checkbox("ติ๊กฉัน", x=368, y=96, w=176, h=88, color=COL_TEXT)
sld = ui.Slider(x=576, y=96, w=192, h=24, color=COL_ACCENT, min=0, max=100,
                value=30)

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

ask = ui.Label("แตะ Button แล้วต้องได้ clicked", x=24, y=192, color=COL_ACCENT,
               value=24)
why = ui.Label("ปุ่มส่ง clicked ค่า value เป็น 1 เสมอ", x=24, y=232,
               color=COL_DIM, value=20)
last = ui.Label("ยังไม่มีเหตุการณ์เข้ามา", x=24, y=264, color=COL_DIM, value=20)

# สี่ขั้นเดินวนกลับมาที่ขั้นแรกเอง จึงไม่ต้องมีปุ่มย้อน
# แถวปุ่มอยู่ล่างสุด y+h = 392 จึงต้องจบก่อน x=690 ที่เป็นที่ของปุ่ม Console
btn_next = ui.Button("เดินหน้า", x=24, y=304, w=200, h=88, color=COL_ACCENT,
                     value=24)
btn_home = ui.Button("เริ่มใหม่", x=256, y=304, w=200, h=88, color=0x3A4150,
                     value=24)

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
    ask.color(COL_ACCENT)
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
