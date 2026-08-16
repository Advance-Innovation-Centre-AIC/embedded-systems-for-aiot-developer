# 08_dropdown_textarea.py - อีกสามชนิดที่รับอินพุตได้ และค่าที่ถามกลับได้จริง
#
# Why : คาบนี้ลงมือกับ Button Switch Slider ครบแล้ว แต่โมดูลมีของรับอินพุต
#       หกชนิด อีกสามตัวคือ Checkbox Dropdown Textarea ซึ่งไม่ได้แปลกกว่ากัน
#       แต่ส่ง event คนละแบบ ทีมที่ไปเจอครั้งแรกตอนทำโปรเจกต์จบ จะเสียเวลา
#       ไปกับการรอ clicked จากของที่ไม่เคยส่ง clicked เลยสักครั้ง
# What: ไฟล์นี้วางครบทั้งหกชนิดบนหน้าเดียว แตะทีละตัวแล้วดูสามช่องของ event
#       พร้อมกับตารางที่บอกว่า .value() แบบถามกลับ ใครตอบได้ ใครตอบ 0 เฉย ๆ
#
# ดูที่จอ: แถวบนคือของให้แตะหกชิ้น แถวกลางคือ event ล่าสุดแบบดิบ
#          แถวล่างคือผลของการไล่ถาม .value() ทุกตัวพร้อมกัน
# กับดัก : Dropdown ส่ง value_changed พร้อม "ลำดับของตัวเลือก" เริ่มที่ 0
#          ไม่ได้ส่งข้อความของตัวเลือกกลับมา - Textarea ส่ง value_changed
#          ที่ value เป็น 0 เสมอ รู้ได้แค่ว่ามีคนพิมพ์ ไม่รู้ว่าพิมพ์อะไร
#          และ .value() ที่ตอบ 0 อาจแปลว่า "ศูนย์จริง" หรือ "ชนิดนี้ตอบไม่ได้"
#          ก็ได้ แยกออกจากกันไม่ได้เลย

import ui
import lcd
import time

RUN_MS = 40000

COL_TEXT = 0xFFFFFF
COL_DIM = 0xA0B4CC
COL_CARD = 0x142240
COL_OK = 0x00E676
COL_WARN = 0xFFA726
COL_INFO = 0x40C4FF

# ตัวเลือกของ Dropdown คั่นด้วยขึ้นบรรทัดใหม่ นี่คือรูปแบบที่ LVGL รับ
# เก็บเป็น list คู่ขนานไว้ด้วย เพราะ event ส่งกลับมาแค่ลำดับ ไม่ได้ส่งข้อความ
CHOICES = ["ช้า", "กลาง", "เร็ว"]
CHOICE_TEXT = "\n".join(CHOICES)

ui.screen()
time.sleep_ms(200)

ui.Label("หกชนิดที่รับอินพุตได้", x=20, y=10, color=COL_TEXT, value=24)

ui.Panel(x=20, y=48, w=750, h=112, color=COL_CARD, min=COL_DIM, max=12, value=1)

# ---- สามตัวที่คาบนี้ใช้แล้ว วางไว้ให้เทียบ ----
btn = ui.Button("Button", x=36, y=88, w=120, h=56, color=0x1565C0, value=16)

ui.Label("Switch", x=176, y=62, color=COL_DIM, value=14)
sw = ui.Switch(x=176, y=90)

ui.Label("Slider", x=280, y=62, color=COL_DIM, value=14)
sld = ui.Slider(x=280, y=96, w=180, min=0, max=100, value=40)

# ---- สามตัวที่ไฟล์นี้เพิ่มเข้ามา ----
cb = ui.Checkbox("Checkbox", x=480, y=64, color=COL_TEXT)

ui.Label("Dropdown", x=480, y=100, color=COL_DIM, value=14)
dd = ui.Dropdown(text=CHOICE_TEXT, x=480, y=122, w=140)

ui.Label("Textarea", x=636, y=62, color=COL_DIM, value=14)
ta = ui.Textarea(text="แตะแล้วพิมพ์", x=636, y=84, w=118, h=60)

# ป้ายหนึ่งใบที่ไม่รับอินพุตเลย เอาไว้พิสูจน์ว่า .value() ของมันตอบ 0
plain = ui.Label("Label ไม่รับอินพุต", x=20, y=170, color=COL_DIM, value=16)

# ตารางค้นกลับ handle -> ชื่อที่คนอ่านเข้าใจ dict ค้นได้ในก้าวเดียว
NAME_OF = {
    btn.id(): "Button",
    sw.id(): "Switch",
    sld.id(): "Slider",
    cb.id(): "Checkbox",
    dd.id(): "Dropdown",
    ta.id(): "Textarea",
}

raw = ui.Label("ยังไม่มีเหตุการณ์เข้ามา", x=20, y=204, color=COL_INFO, value=22)
mean = ui.Label("แตะของแถวบนทีละชิ้น", x=20, y=240, color=COL_DIM, value=18)
poll_line = ui.Label("ยังไม่ได้ไล่ถาม value()", x=20, y=276, color=COL_WARN,
                     value=16)
ui.Label("Dropdown ส่งลำดับ ไม่ได้ส่งข้อความ", x=20, y=306, color=COL_DIM,
         value=16)
ui.Label("value() ที่ตอบ 0 อาจแปลว่าตอบไม่ได้", x=20, y=332, color=COL_DIM,
         value=16)

lcd.clear()
lcd.console("<h2>หกชนิดที่รับอินพุตได้</h2>")
lcd.print("Button=clicked - Switch/Checkbox=toggled")
lcd.print("Slider/Dropdown/Textarea=value_changed")

# ชนิดที่ CM55 ตอบ .value() ได้จริงมีหกชนิด คือ Slider Arc Bar Switch
# Checkbox Dropdown - หน้านี้มีอยู่สี่ในหกตัวนั้น อีกสองตัวคือ Arc กับ Bar
# ซึ่งเป็นตัวแสดงผล จะได้เจอในคาบ 5 กับ 6 ส่วนชนิดอื่นคืน 0 เฉย ๆ
ASK_VALUE = [("Slider", sld), ("Switch", sw), ("Checkbox", cb),
             ("Dropdown", dd)]


def sweep_values():
    """ไล่ถาม .value() ทุกตัวในรอบเดียว แล้วรายงานเป็นบรรทัดเดียว

    ระวัง: .value() แบบถามกลับ ไม่ใช่การอ่านตัวแปรในเครื่องเรา มันเดินทาง
    ข้ามไปถาม CM55 แล้วรอคำตอบกลับมาทีละครั้ง ห้าครั้งต่อรอบแบบนี้พอไหว
    เพราะเราเรียกเฉพาะตอนมี event เข้ามาจริง ถ้าย้ายไปเรียกทุกรอบของลูป
    จะกลายเป็นการยิง IPC ยี่สิบครั้งต่อวินาทีโดยไม่มีใครได้อะไรเพิ่ม
    """
    parts = []
    for name, w in ASK_VALUE:
        parts.append(name[:2] + "=" + str(w.value()))
    poll_line.text(" ".join(parts) + "  Label=" + str(plain.value()))


sweep_values()

# หลักฐานชิ้นแรก - Label ตอบ 0 ทั้งที่ไม่ได้มีค่าอะไรให้ตอบ
lcd.print("Label.value() =", plain.value(), "- ตอบ 0 เพราะตอบไม่ได้")
lcd.print("Textarea.value() =", ta.value(), "- ตอบ 0 เหมือนกัน")
lcd.print("<span class=muted>0 สองตัวนี้คนละความหมายกับ 0 ของ Slider</span>")

t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        h = ev["handle"]
        t = ev["type"]
        v = ev["value"]
        who = NAME_OF.get(h, "ไม่รู้จัก")

        # สามช่องมีครบเสมอ พิมพ์ดิบก่อน แล้วค่อยแปลความหมาย
        raw.text(who + " " + t + " value=" + str(v))

        # กิ่งของ type เขียนให้ครบทุกชนิดที่รู้จัก แล้วเหลือกิ่งสุดท้ายไว้
        # ให้ 'unknown' ถ้าใช้ else รวบ ของแปลกจะหายเข้าไปโดยไม่มีใครเห็น
        if t == "clicked":
            mean.text("กดแล้วปล่อย - value ไม่ได้ใช้")
        elif t == "toggled":
            mean.text("สลับเป็น " + ("เปิด" if v == 1 else "ปิด"))
        elif t == "value_changed":
            if h == dd.id():
                # v คือลำดับ ต้องเอาไปเปิดตาราง CHOICES เองถึงจะได้ข้อความ
                pick = CHOICES[v] if 0 <= v < len(CHOICES) else "?"
                mean.text("Dropdown เลือกลำดับ " + str(v) + " = " + pick)
            elif h == ta.id():
                mean.text("Textarea มีคนพิมพ์ แต่ value เป็น " + str(v))
            else:
                mean.text("ค่าใหม่คือ " + str(v))
        else:
            # ชนิดที่สี่ที่เฟิร์มแวร์ใช้ตอนแปลรหัสไม่ออก อย่าเงียบใส่มัน
            mean.text("ชนิด " + t + " - จดไว้ อย่าเดาแทนมัน")
            mean.color(COL_WARN)

        lcd.print(who + " " + t + " value=" + str(v))
        sweep_values()

    time.sleep_ms(50)

raw.text("หมดเวลาแล้ว")
mean.text("จบ - หกชนิด สามแบบของ event")
sweep_values()
lcd.print("<span class=ok>สรุป: ต้องรู้ว่าแต่ละชนิดส่งอะไร ก่อนไปรอมัน</span>")
