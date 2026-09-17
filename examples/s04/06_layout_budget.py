# 06_layout_budget.py - พื้นที่ 792x398 กับงบ widget: งบของคอร์ส 32 ตัว (เพดานเฟิร์มแวร์ 64)
#
# Why : หน้าจอนี้ไม่ใช่ผืนผ้าใบไม่จำกัด มันมีเส้นอยู่สามเส้น หนึ่งคือพื้นที่ 792x398 พิกเซล
#       สองคือเพดานของเฟิร์มแวร์ 64 widget ต่อหน้า (UI_MAX_WIDGETS) เกินแล้วได้ RuntimeError
#       สามคืองบที่คอร์สนี้ขีดให้ตัวเอง 32 ตัวต่อหน้า เพื่อให้จอยังอ่านออกและเหลือที่ให้ดีบัก
#       คนที่วางไปเรื่อย ๆ จะไปเจอ RuntimeError กลางทางตอนโค้ดยาวแล้ว หรือแย่กว่านั้น
#       คือวางปุ่มไปทับปุ่ม Console แล้วแตะไม่ได้ ทั้งที่โค้ดสร้างสำเร็จและไม่มี error อะไรเลย
# What: งบประมาณสองก้อนที่ต้องคิดตั้งแต่ก่อนเขียน หนึ่งคือจำนวน widget (งบคอร์ส 32 / เพดาน 64)
#       สองคือพื้นที่วาง ซึ่งมีมุมขวาล่างราว 100x58 ถูกจองไว้ให้ปุ่ม Console
#       การเช็กก่อนสร้าง ถูกกว่าการไปแก้ตอนเจอปัญหาเสมอ
#
# ดูที่จอ: ตาราง 5x5 ปุ่ม แถบ Bar บอกว่าใช้งบไปแล้วกี่ใน 32 พร้อมตัวนับข้าง ๆ
#         และสามบรรทัดล่างสรุปผลเทียบกับ ui.list() ผลตรวจเขตห้ามวาง และผลลองสร้างตัวที่ 33
#         ไฟล์นี้ใช้พอดี 32 ตัว แล้วลองสร้างตัวที่ 33 ให้ดู: ตัวที่ 33 สร้างได้ เพราะ 32 คืองบของคอร์ส
#         ไม่ใช่เพดานของเฟิร์มแวร์ - เพดาน 64 ไฟล์นี้ไปไม่ถึง ถ้าถึงจะได้ RuntimeError: ui: max 64 widgets
# กับดัก : วาง widget เลย x=690 และ y=340 พร้อมกัน มันจะไปอยู่ใต้ปุ่ม Console
#         แตะไม่ได้ ทั้งที่โค้ดสร้างสำเร็จและไม่มี error อะไรเลย

import ui
import lcd
import time

# ตัวเลขของสนาม เขียนไว้บนสุดเป็นค่าคงที่ จะได้ไม่ต้องจำ
AREA_W = 792
AREA_H = 398
MAX_WIDGETS = 64       # เพดานของเฟิร์มแวร์ (UI_MAX_WIDGETS) เกินแล้ว RuntimeError
COURSE_BUDGET = 32     # งบที่คอร์สขีดให้ตัวเอง ต่ำกว่าเพดานครึ่งหนึ่ง
RESERVED_X = 690       # เลยจุดนี้ไปทางขวา รวมกับ y ข้างล่าง คือเขตห้ามวาง
RESERVED_Y = 340
COLS = 5
ROWS = 5
CELL_W = 140
CELL_H = 36
GAP_X = 10
GAP_Y = 4
LEFT = 20
TOP = 72                # ใต้ป้ายนับ (y=44) ไม่ให้แถวแรกทับ
RUN_MS = 15000

COL_TEXT = 0x4A9EFF
COL_DIM = 0x9AA3AF
COL_OK = 0x30A46C
COL_WARN = 0xF5A623

ui.screen()
time.sleep_ms(200)

# นับเองตั้งแต่ตัวแรก การรู้ว่าใช้ไปกี่ตัวก่อนสร้าง ดีกว่ามาเจอ RuntimeError ทีหลัง
ui.Label("งบประมาณของหน้าจอ", x=20, y=8, color=COL_TEXT, value=24)
ui.Label("งบ widget ต่อหนึ่งหน้า", x=432, y=8, color=COL_DIM, value=16)
counter = ui.Label("ใช้ไป 4 / งบ 32 (เพดาน 64)", x=432, y=44, color=COL_OK, value=16)

# Bar ทำให้ "งบประมาณ" กลายเป็นปริมาณที่ตามองเห็น ไม่ใช่แค่ตัวเลขในหัว
# แถบเต็ม = ชนงบของคอร์ส ไม่ใช่ชนเพดานของบอร์ด
bar = ui.Bar(x=580, y=32, w=192, h=16, min=0, max=COURSE_BUDGET, value=4)
used = 4               # สี่ตัวข้างบนนี้คือของที่เราสร้างไปแล้ว


def spend(n):
    """บันทึกโควตาที่ใช้ไป แล้วอัปเดตทั้งตัวเลขและแถบให้ตรงกันในที่เดียว"""
    global used
    used = n
    counter.text("ใช้ไป " + str(used) + " / งบ " + str(COURSE_BUDGET) +
                 " (เพดาน " + str(MAX_WIDGETS) + ")")
    bar.value(used)


# กันสาม slot สุดท้ายไว้ให้บรรทัดสรุปข้างล่าง ตารางจึงหยุดก่อนถึงงบสามตัว
RESERVE_FOR_TEXT = 3
skipped = 0

for r in range(ROWS):
    for c in range(COLS):
        x = LEFT + c * (CELL_W + GAP_X)
        y = TOP + r * (CELL_H + GAP_Y)

        # ตรวจสองอย่างก่อนสร้าง: ล้นขอบไหม และทับเขตของปุ่ม Console ไหม
        # ตรวจก่อนสร้าง ไม่ใช่สร้างแล้วค่อยมาดูว่าผลออกมาเป็นอย่างไร
        if x + CELL_W > AREA_W or y + CELL_H > AREA_H:
            skipped = skipped + 1
            continue
        if x + CELL_W > RESERVED_X and y + CELL_H > RESERVED_Y:
            skipped = skipped + 1
            continue

        # เช็กงบก่อนสร้างทุกครั้ง ไม่ใช่สร้างแล้วรอให้บอร์ดโยน RuntimeError ที่ 64
        if used >= COURSE_BUDGET - RESERVE_FOR_TEXT:
            break
        ui.Button(str(r) + "," + str(c), x=x, y=y,
                  w=CELL_W, h=CELL_H, color=0x3A4150, value=14)
        spend(used + 1)

# Label ที่สร้างด้วยข้อความว่าง LVGL จะเติมคำว่า "Label" ให้เอง
# แล้วคำนั้นค้างบนจอจนกว่าจะมีการเขียนทับครั้งแรก จึงต้องตั้งข้อความตั้งต้นเสมอ
summary = ui.Label("ยังไม่ได้เทียบกับ ui.list()", x=20, y=284,
                   color=COL_TEXT, value=20)
probe = ui.Label("ยังไม่ได้ตรวจเขตห้ามวาง", x=20, y=308, color=COL_WARN,
                 value=16)
ceiling = ui.Label("ยังไม่ได้ลองสร้างตัวที่ 33", x=20, y=332, color=COL_DIM,
                   value=16)
spend(used + 3)

# ลองสร้างตัวที่ 33 จริง ๆ หนึ่งครั้ง: งบ 32 เป็นเส้นที่คอร์สขีดเอง บอร์ดไม่รู้จัก
# ตัวที่ 33 จึงสร้างได้ - เส้นที่บอร์ดบังคับคือ 64 และไฟล์นี้ไปไม่ถึง
# ถ้าไปถึง (เช่น ขยายตารางเป็น 8x8) บอร์ดจะโยน RuntimeError: ui: max 64 widgets
if used >= COURSE_BUDGET:
    try:
        extra = ui.Button("33", x=580, y=308, w=88, h=88, color=0xE5484D)   # ช่องว่างขวาล่าง ไม่ทับป้ายสรุป
        extra.delete()
        over = "ตัวที่ 33 สร้างได้ - งบ 32 ไม่ใช่เพดาน 64"
    except RuntimeError:
        over = "ตัวที่ 33 ถูกปฏิเสธ - เพดานต่ำกว่า 64"
else:
    over = "ยังเหลืองบ " + str(COURSE_BUDGET - used) + " ตัว"

# ui.list() คืนรายการ widget ที่มีอยู่จริงบนจอ ใช้ตรวจว่าที่เรานับตรงกับของจริง
# ระวังชื่อช่อง: ที่นี่คือ 'id' แต่ใน ui.poll() คือ 'handle' คนละคำ ค่าเดียวกัน
alive = ui.list()
match = "ตรงกัน" if len(alive) == used else "ไม่ตรง"
summary.text("นับเอง " + str(used) + " - ui.list() " + str(len(alive)) +
             " - " + match)

# ตรวจเขตห้ามวางด้วยจุดตัวอย่างหนึ่งจุด ให้เห็นว่ากฎนี้ตัดสินอย่างไร
PROBE_X = 700
PROBE_Y = 350
if PROBE_X > RESERVED_X and PROBE_Y > RESERVED_Y:
    probe.text("จุด x=700 y=350 อยู่ใต้ปุ่ม Console จึงข้าม")
else:
    probe.text("จุด x=700 y=350 วางได้")
ceiling.text(over)

lcd.print("งบคอร์ส 32 (เพดาน 64) - พื้นที่ 792x398 - ข้ามไป " + str(skipped) + " ช่อง")
lcd.print("นับเอง " + str(used) + " - ui.list() รายงาน " + str(len(alive)))
lcd.print(over)

t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    ui.poll()
    time.sleep_ms(50)

summary.text("จบแล้ว - นับเอง " + str(used) + " - บนจอจริง " + str(len(alive)))
