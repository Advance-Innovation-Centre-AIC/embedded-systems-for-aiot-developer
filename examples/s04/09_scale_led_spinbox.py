# 09_scale_led_spinbox.py - สาม widget ที่แยกหน้าจอ HMI ออกจากหน้าจอเล่น ๆ
# ชุดตัวอย่างประจำคาบ 04
#
# Why : แปดไฟล์ก่อนหน้าใช้ widget ที่บอก "ค่าเท่าไร" แต่หน้าจอควบคุมจริงต้องตอบ
#       สามคำถามที่มากกว่านั้น - ค่านี้สูงไหมเมื่อเทียบกับพิสัย · ตอนนี้สถานะอะไร ·
#       และผู้ใช้จะป้อนค่าที่ต้องการเข้าไปยังไง
# What: ui.Scale (ไม้บรรทัดมีขีดและตัวเลข) · ui.Led (ไฟสถานะ) · ui.Spinbox (ป้อนเลขทีละหลัก)
# How : ประกอบสามตัวเป็นแผงคุมอุณหภูมิหนึ่งแผง
#
# ดูที่จอ: แถบบนคือมาตรวัด 0-100 พร้อมตัวเลขกำกับทุก 20 · กลางซ้ายคือช่องตั้ง
#         เป้าหมาย · กลางขวาคือไฟสถานะสามดวงแบบแผงควบคุมจริง
# กับดัก : ui.Scale ไม่มีเข็ม และไม่รับ .value() - มันคือไม้บรรทัด ไม่ใช่หน้าปัด
#         ตัวที่ขยับคือสิ่งที่เราวางทับลงไปเอง เช่น ui.Bar หรือป้ายตัวเลข
#         ส่วน ui.Led สั่ง .value(0) แล้ว "หรี่" ไม่ใช่ "หาย" - ไฟแผงควบคุมที่
#         หายไปตอนดับ แย่กว่าไฟที่หรี่ลง เพราะคนดูแยกไม่ออกว่าดับหรือจอเสีย
#
# บน Eva Kit: ทั้งสามตัวเป็นของใหม่ในไลบรารี เพิ่มเมื่อ 15 ส.ค. 2026

import lcd
import time
import ui

T_MIN, T_MAX = 0, 100
WARN_AT, ALARM_AT = 60, 80
ROUNDS = 60

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD = 0x00E676, 0xFFC83D, 0xFF5252

ui.screen()
time.sleep_ms(200)

ui.Label("แผงคุมอุณหภูมิ", x=20, y=10, color=COL_TEXT, value=24)

# --- มาตรวัด: ค่ากับพิสัยของมันอยู่ด้วยกัน --------------------------------
ui.Panel(x=20, y=48, w=740, h=110, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("อุณหภูมิเทียบพิสัยใช้งาน", x=40, y=58, color=COL_DIM, value=16)
# แถบค่าวางไว้ "เหนือ" ไม้บรรทัด เพราะ Scale ไม่มีเข็มให้ - ตัวที่ขยับคือ Bar
bar = ui.Bar(x=40, y=84, w=660, h=14, color=COL_OK, min=T_MIN, max=T_MAX, value=0)
ui.Scale(x=40, y=102, w=660, h=48, color=COL_TEXT, min=T_MIN, max=T_MAX)

# --- ช่องตั้งค่า: ผู้ใช้พิมพ์ตัวเลขเข้าไปได้ -------------------------------
ui.Panel(x=20, y=176, w=360, h=120, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("เป้าหมาย (องศา)", x=40, y=188, color=COL_DIM, value=16)
sp = ui.Spinbox(x=40, y=220, w=200, h=56, color=COL_OK, min=T_MIN, max=T_MAX,
                value=45)

# --- ไฟสถานะ: สามดวงแบบแผงจริง ------------------------------------------
ui.Panel(x=400, y=176, w=360, h=120, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("สถานะ", x=420, y=188, color=COL_DIM, value=16)
led_ok = ui.Led(x=428, y=222, w=40, h=40, color=COL_OK, value=1)
ui.Label("ปกติ", x=478, y=232, color=COL_DIM, value=18)
led_warn = ui.Led(x=560, y=222, w=40, h=40, color=COL_WARN, value=0)
ui.Label("เฝ้าระวัง", x=610, y=232, color=COL_DIM, value=18)
led_bad = ui.Led(x=700, y=222, w=40, h=40, color=COL_BAD, value=0)

msg = ui.Label("ค่าลอย ๆ ไม่บอกว่าสูงไหม มาตรวัดบอก", x=20, y=312,
               color=COL_DIM, value=18)
ui.poll()

lcd.clear()
lcd.console("<h2>Scale + Led + Spinbox</h2>")

# ไล่ค่าขึ้นลงเพื่อให้เห็นไฟสามดวงสลับกันทำงาน
for i in range(ROUNDS):
    t = T_MIN + (T_MAX - T_MIN) * i // (ROUNDS - 1)
    bar.value(t)

    # หนึ่งดวงติดเท่านั้น - แผงที่ติดพร้อมกันหลายดวงคือแผงที่อ่านไม่ออก
    led_ok.value(1 if t < WARN_AT else 0)
    led_warn.value(1 if WARN_AT <= t < ALARM_AT else 0)
    led_bad.value(1 if t >= ALARM_AT else 0)

    if t >= ALARM_AT:
        bar.color(COL_BAD)
        msg.text("เกินเกณฑ์อันตราย " + str(ALARM_AT) + " องศา")
        msg.color(COL_BAD)
    elif t >= WARN_AT:
        bar.color(COL_WARN)
        msg.text("เข้าเขตเฝ้าระวัง " + str(WARN_AT) + " องศา")
        msg.color(COL_WARN)
    else:
        bar.color(COL_OK)
        msg.text("อยู่ในพิสัยปกติ")
        msg.color(COL_DIM)

    if i % 12 == 0:
        lcd.print("อุณหภูมิ", t, "องศา")
    ui.poll()
    time.sleep_ms(120)

lcd.print("<span class=ok>จบรอบ - ไล่ครบทั้งสามสถานะ</span>")
print("Scale บอกพิสัย · Led บอกสถานะ · Spinbox รับค่าจากคน")

# ตาคุณ
# 1) ลองเรียก scale.value(50) ดู - มันจะไม่ขยับ เพราะไม้บรรทัดไม่มีค่าเดียว
#    ตัวที่ต้องขยับคือ bar ที่วางทับอยู่
# 2) ทำให้ไฟติดพร้อมกันสองดวงตอนคาบเกี่ยว แล้วถามคนข้าง ๆ ว่าอ่านออกไหม
#    นั่นคือเหตุผลที่แผงจริงให้ติดทีละดวง
