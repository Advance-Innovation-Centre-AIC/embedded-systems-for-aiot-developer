# 09_your_level_rule.py - ไฟล์นี้รันได้ แต่ยังตอบผิดทุกข้อ งานของคุณคือทำให้มันถูก
#
# ไฟล์นี้เป็นคู่ฝึกของ 08_status_screen.py ในนั้นกฎตัดระดับเขียนไว้ให้แล้ว
# ส่วนในนี้ยังว่าง และลอกจาก 08 มาตรง ๆ ไม่ได้ เพราะโจทย์คนละเจ้าใช้เส้นคนละที่
# ต้องอ่านโจทย์ข้างล่างแล้วตัดสินใจเอง
#
# โจทย์จากทีมซ่อมบำรุง (นี่คือที่มาของตัวเลข ไม่ใช่เลขที่เราคิดขึ้นเอง)
#   ต่ำกว่า 50        ไม่ต้องสนใจ           -> "ปกติ"
#   ตั้งแต่ 50 ถึง 79  ให้จับตาไว้            -> "เริ่มสูง"
#   ตั้งแต่ 80 ขึ้นไป   ต้องเข้าไปดูทันที      -> "ต้องรีบดู"
#
# ดูที่จอ: หกแถวคือหกค่าที่เอาไปลองกฎของคุณ เขียวคือกฎตอบตรงกับที่ควรเป็น
#          แดงคือยังไม่ตรง รันครั้งแรกจะได้เขียวสองแถวแดงสี่แถว นั่นถูกแล้ว
#          เพราะกฎที่ให้มาตอบ "ปกติ" ทุกค่า จึงบังเอิญถูกเฉพาะสองแถวแรก
# กับดัก : ลำดับของ if สำคัญกว่าที่คิด ถ้าเช็กเงื่อนไข 50 ก่อน 80 ค่า 92 จะตกลง
#          ช่องกลางแล้วไม่มีวันไปถึงช่องบนเลย และไม่มี error ให้จับสักตัว

import lcd
import time
import ui

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_BAD = 0x30A46C, 0xE5484D

# ชุดค่าที่ใช้ตรวจกฎ เลือกไว้ให้ครบทั้งสามระดับ และมีค่าที่อยู่ตรงเส้นพอดีด้วย
# ค่าตรงเส้นคือจุดที่กฎผิดกันบ่อยที่สุด เพราะ >= กับ > ต่างกันแค่ตัวเดียว
CASES = (
    (12, "ปกติ"),
    (49, "ปกติ"),
    (50, "เริ่มสูง"),
    (79, "เริ่มสูง"),
    (80, "ต้องรีบดู"),
    (92, "ต้องรีบดู"),
)


# ----- เติมส่วนนี้เอง (งานของคุณ) -----
def level_name(v):
    """คืนชื่อระดับของค่า v เป็นสตริง ตามโจทย์ที่หัวไฟล์

    ตอนนี้มันตอบ "ปกติ" ทุกค่า ซึ่งผ่านแค่สองข้อแรกและตกที่เหลือ
    แก้ข้างในให้ตอบครบทั้งสามระดับ แล้วรันใหม่จนหกแถวเขียวหมด

    ใบ้: เขียน if เรียงจากเงื่อนไขที่เข้มที่สุดลงมาหาเงื่อนไขที่หลวมที่สุด
         แล้วปิดท้ายด้วย return ของกรณีที่เหลือ โดยไม่ต้องมี if
    """
    return "ปกติ"
# ----- จบส่วนที่ต้องเติม -----


ui.screen()
time.sleep_ms(200)

ui.Label("เขียนกฎตัดระดับเอง แล้วให้บอร์ดตรวจให้", x=20, y=12,
         color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=652, h=96, color=COL_CARD, min=COL_DIM, max=12, value=1)

ui.Label("ผ่านแล้ว", x=40, y=64, color=COL_DIM, value=16)
seg_ok = ui.Seg7(text="0", x=40, y=88, w=152, h=52, color=COL_OK)

ui.Label("ยังไม่ผ่าน", x=232, y=64, color=COL_DIM, value=16)
seg_bad = ui.Seg7(text="0", x=232, y=88, w=152, h=52, color=COL_BAD)

bar = ui.Bar(x=412, y=100, w=232, h=28, min=0, max=len(CASES), value=0)

# หกแถวสร้างไว้ครบตั้งแต่ตอนนี้ แล้วเดี๋ยวเขียนทับด้วยผลตรวจ
# ห้ามสร้าง Label ด้วยข้อความว่าง LVGL จะเติมคำว่า "Label" ให้เอง แล้วมันจะค้าง
row1 = ui.Label("รอตรวจ", x=20, y=172, color=COL_DIM, value=20)
row2 = ui.Label("รอตรวจ", x=20, y=200, color=COL_DIM, value=20)
row3 = ui.Label("รอตรวจ", x=20, y=228, color=COL_DIM, value=20)
row4 = ui.Label("รอตรวจ", x=20, y=256, color=COL_DIM, value=20)
row5 = ui.Label("รอตรวจ", x=20, y=284, color=COL_DIM, value=20)
row6 = ui.Label("รอตรวจ", x=20, y=312, color=COL_DIM, value=20)
rows = (row1, row2, row3, row4, row5, row6)

result_lbl = ui.Label("ยังไม่ได้ตรวจ", x=20, y=348, color=COL_DIM, value=20)
ui.Label("แก้แล้วรันใหม่ได้เรื่อย ๆ", x=300, y=352, color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>ตรวจกฎตัดระดับ</h2>")

passed = 0
for i in range(len(CASES)):
    value, want = CASES[i]

    # เรียกกฎของคุณ แล้วเทียบกับคำตอบที่โจทย์บอกไว้ ไม่ใช่เทียบกับที่เราเดา
    got = level_name(value)
    ok = (got == want)

    if ok:
        passed = passed + 1
        rows[i].color(COL_OK)
        lcd.print("<span class=ok>" + str(value) + " -> " + got + "</span>")
    else:
        rows[i].color(COL_BAD)
        lcd.print("<span class=error>" + str(value) + " ควรเป็น " + want +
                  " แต่ได้ " + got + "</span>")

    rows[i].text(str(value) + "   ควรเป็น " + want + "   ได้ " + got)
    seg_ok.text(str(passed))
    seg_bad.text(str(i + 1 - passed))
    bar.value(passed)

    ui.poll()
    time.sleep_ms(300)

if passed == len(CASES):
    result_lbl.text("ผ่านครบทั้ง " + str(len(CASES)) + " ข้อ - กฎใช้ได้แล้ว")
    result_lbl.color(COL_OK)
    lcd.print("<span class=ok>ผ่านครบ - กฎนี้ตรงตามโจทย์แล้ว</span>")
else:
    result_lbl.text("ยังตกอยู่ " + str(len(CASES) - passed) + " ข้อ - ดูแถวแดง")
    result_lbl.color(COL_BAD)
    lcd.print("<span class=warn>ยังตก", len(CASES) - passed, "ข้อ</span>")

ui.poll()

# ----- ตาคุณ ต่ออีกขั้น -----
# ผ่านครบหกข้อแล้ว ลองเพิ่มลงใน CASES อีกหนึ่งแถวที่คุณคิดว่ากฎของตัวเองน่าจะตก
# แล้วรันดูว่าตกจริงไหม ถ้าไม่ตก แปลว่ากฎแข็งกว่าที่คิด
# ใบ้: ค่าที่ทีมซ่อมบำรุงไม่ได้พูดถึงเลยในโจทย์ ก็เป็นค่าที่ต้องตอบให้ได้เหมือนกัน
