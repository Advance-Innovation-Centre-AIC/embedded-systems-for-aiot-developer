# 15_press_and_hold.py - ปุ่มที่ต้องกดค้าง และเหตุการณ์ที่ clicked บอกไม่ได้
#
# ไฟล์นี้สอน: clicked มาถึงตอน "ปล่อยแล้ว" จึงบอกไม่ได้ว่าตอนนี้นิ้วยังกดอยู่ไหม
#             งาน HMI จริงหลายอย่างต้องรู้ให้ได้ เช่น เดินมอเตอร์ตอนกดค้าง
#             และหยุดทันทีที่ปล่อย ไฟล์นี้จึงใช้เหตุการณ์ห้าชนิดที่เพิ่มเข้ามา
# ดูที่จอ   : ปุ่มเพิ่ม/ลดสองใบ กดค้างแล้วตัวเลขไต่ขึ้นเอง ปล่อยแล้วหยุด
#             แถบล่างบอกว่าเหตุการณ์ล่าสุดที่เข้ามาคืออะไร
# กับดัก    : widget จะยัง "ไม่ส่ง" เหตุการณ์ห้าชนิดนี้จนกว่าเราจะขอด้วย
#             .listen(...) ค่าตั้งต้นคือไม่ส่งอะไรเลย ซึ่งตั้งใจให้เป็นแบบนั้น
#             เพราะคิวมีแค่ 16 ช่อง ถ้าทุก widget ส่งทุกอย่างคิวจะเต็มด้วย
#             เรื่องที่ไม่มีใครอ่าน

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

ui.Label("กดค้างไว้ - เหตุการณ์ที่ clicked บอกไม่ได้", x=24, y=8,
         color=COL_TEXT, value=28)
ui.Label("กดค้างที่ปุ่มเพิ่มหรือลด แล้วดูแถวล่าง", x=24, y=56,
         color=COL_DIM, value=20)

# สองปุ่มนี้คือ jog button - ของจริงในห้องคุมเครื่องจักร กดค้างเพื่อขยับทีละนิด
# สูง 88 ตามขั้นต่ำของเป้าสัมผัส เว้นห่างกัน 32
btn_up = ui.Button("เพิ่ม", x=24, y=96, w=232, h=88, color=COL_ACCENT,
                   value=24)
btn_down = ui.Button("ลด", x=288, y=96, w=232, h=88, color=COL_CARD,
                     value=24)

# การ์ดค่าที่ตั้งไว้ - ตัวเลขที่ต้องอ่านปราดเดียวจึงมีพื้นของตัวเอง
ui.Panel(x=552, y=96, w=216, h=88, color=COL_CARD)
ui.Label("ค่าที่ตั้งไว้", x=576, y=104, color=COL_DIM, value=20)
lbl_set = ui.Label("50", x=576, y=136, color=COL_TEXT, value=28)

# ---- ขอรับเหตุการณ์ -------------------------------------------------------
#
# .listen() คือการบอก CM55 ว่า "ปุ่มใบนี้ ขอทราบเรื่องพวกนี้ด้วย"
# ชื่อที่ใส่ตรงนี้คือชื่อเดียวกับที่จะกลับมาใน ev["type"] ไม่มีสองภาษาให้จำ
# พิมพ์ชื่อผิดจะได้ ValueError ทันที ไม่ใช่เงียบแล้วรอเหตุการณ์ที่ไม่มีวันมา
for b in (btn_up, btn_down):
    b.listen("pressed", "released", "press_lost",
             "long_pressed", "long_pressed_repeat")

lbl_last = ui.Label("ยังไม่มีเหตุการณ์เข้ามา", x=24, y=200, color=COL_DIM,
                    value=24)
lbl_why = ui.Label("เหตุการณ์แต่ละชนิดหมายถึงอะไร จะขึ้นตรงนี้", x=24, y=240,
                   color=COL_DIM, value=20)
lbl_state = ui.Label("นิ้ว: ไม่ได้แตะ", x=24, y=272, color=COL_DIM, value=20)

btn_reset = ui.Button("ตั้งค่าใหม่เป็น 50", x=24, y=304, w=304, h=88,
                      color=COL_CARD, value=24)

ID_UP, ID_DOWN, ID_RESET = btn_up.id(), btn_down.id(), btn_reset.id()

# คำอธิบายสั้น ๆ ของแต่ละชนิด เขียนไว้ก่อน ผู้เรียนจึงได้ทำนายแล้วตรวจ
MEANING = {
    "pressed": "นิ้วลงบนปุ่ม - เริ่มทำงานได้ตรงนี้",
    "released": "นิ้วยกขึ้น - จุดที่ต้องสั่งหยุด",
    "press_lost": "นิ้วยังกดอยู่ แต่เลื่อนออกนอกปุ่มแล้ว",
    "long_pressed": "กดค้างครบเวลา - เริ่มโหมดกดค้าง",
    "long_pressed_repeat": "ยังกดค้างอยู่ - value คือจำนวนครั้งที่ย้ำ",
    "clicked": "กดแล้วปล่อยบนปุ่มเดิม - มาถึงตอนปล่อยแล้วเสมอ",
}

setpoint = 50
holding = 0             # handle ที่นิ้วกดค้างอยู่ตอนนี้ 0 = ไม่มี


def clamp(v):
    """ค่าตั้งอยู่ในพิสัย 0-100 เสมอ - ไม่ปล่อยให้เลขวิ่งหลุดพิสัยจริง"""
    if v < 0:
        return 0
    if v > 100:
        return 100
    return v


lcd.clear()
lcd.console("<h2>ปุ่มที่ต้องกดค้าง</h2>")
lcd.print("pressed / released / press_lost / long_pressed / long_pressed_repeat")
lcd.print("ทั้งห้าชนิดนี้ต้องขอด้วย .listen() ก่อน ไม่งั้นไม่มีอะไรมาถึงเลย")

while True:
    for ev in ui.poll():
        h = ev["handle"]
        t = ev["type"]

        if h == ID_RESET and t == "clicked":
            setpoint = 50
            lbl_set.text(str(setpoint))
            continue

        if h not in (ID_UP, ID_DOWN):
            continue

        step = 1 if h == ID_UP else -1

        if t == "pressed":
            # จุดที่ของจริงจะเริ่มเดิน - ไม่ใช่ตอน clicked ซึ่งสายไปแล้ว
            holding = h
            setpoint = clamp(setpoint + step)
            lbl_state.text("นิ้ว: กดอยู่")
            lbl_state.color(COL_ACCENT)
        elif t == "long_pressed_repeat":
            # value คือจำนวนครั้งที่ย้ำมาตั้งแต่ครั้งที่แล้วที่เราถาม
            # เฟิร์มแวร์ยุบให้เหลือช่องเดียวในคิว แล้วนับให้ - กดค้างสิบวินาที
            # จึงไม่ทำให้ released ของตัวเองถูกทิ้ง
            setpoint = clamp(setpoint + step * ev["value"])
        elif t in ("released", "press_lost"):
            # ทั้งสองชนิดแปลว่า "หยุดได้แล้ว" ต้องรับทั้งคู่
            # ถ้ารับแค่ released แล้วผู้ใช้ลากนิ้วออกนอกปุ่ม เครื่องจะเดินต่อ
            holding = 0
            lbl_state.text("นิ้ว: ไม่ได้แตะ")
            lbl_state.color(COL_DIM)

        lbl_set.text(str(setpoint))
        lbl_last.text(t + "  value=" + str(ev["value"]))
        lbl_last.color(COL_OK if t == "released" else COL_TEXT)
        lbl_why.text(MEANING.get(t, "ชนิดที่ยังไม่รู้จัก"))
        lcd.print(t + " value=" + str(ev["value"]) + " -> " + str(setpoint))

    # หน่วงสั้น เพราะ long_pressed_repeat มาถี่ ถ้าหลับนานคิวจะสะสม
    time.sleep_ms(30)

# ตาคุณ
# 1) ลบชื่อ "press_lost" ออกจาก .listen() แล้วกดค้างที่ปุ่มเพิ่ม ลากนิ้วออก
#    นอกปุ่มก่อนปล่อย - ค่าจะไต่ต่อไปหรือหยุด ลองแล้วอธิบายว่าทำไม
# 2) เปลี่ยน time.sleep_ms(30) เป็น 500 แล้วกดค้างสามวินาที ค่าที่ได้ยัง
#    ถูกไหม (ยัง เพราะ value นับให้) แต่ปุ่มรู้สึกอย่างไร
# 3) ให้ปุ่ม "ตั้งค่าใหม่" ฟัง long_pressed แล้วสั่งรีเซ็ตเมื่อกดค้างเท่านั้น
#    นี่คือท่ากันกดพลาดที่ของจริงใช้กับคำสั่งอันตราย
