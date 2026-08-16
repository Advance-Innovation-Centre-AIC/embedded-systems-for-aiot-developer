# 06_layout_budget.py - พื้นที่ 792x398 และโควตา 32 ตัว
#
# Why : หน้าจอนี้ไม่ใช่ผืนผ้าใบไม่จำกัด มันมีเพดานแข็ง ๆ อยู่สองอัน คือจำนวน widget
#       32 ตัว กับพื้นที่ 792x398 พิกเซล คนที่วางไปเรื่อย ๆ จะไปเจอ RuntimeError
#       กลางทางตอนโค้ดยาวแล้ว หรือแย่กว่านั้น คือวางปุ่มไปทับปุ่ม Console แล้วแตะไม่ได้
#       ทั้งที่โค้ดสร้างสำเร็จและไม่มี error อะไรเลย
# What: งบประมาณสองก้อนที่ต้องคิดตั้งแต่ก่อนเขียน หนึ่งคือโควตา 32 ตัวต่อหนึ่งหน้า
#       สองคือพื้นที่วาง ซึ่งมีมุมขวาล่างราว 100x58 ถูกจองไว้ให้ปุ่ม Console
#       การเช็กก่อนสร้าง ถูกกว่าการไปแก้ตอนเจอปัญหาเสมอ
#
# ดูที่จอ: ตาราง 5x5 ปุ่ม แถบ Bar บอกว่าใช้โควตาไปแล้วกี่ใน 32 พร้อมตัวนับข้าง ๆ
#         และสามบรรทัดล่างสรุปผลเทียบกับ ui.list() ผลตรวจเขตห้ามวาง และผลชนเพดาน
# กับดัก : วาง widget เลย x=690 และ y=340 พร้อมกัน มันจะไปอยู่ใต้ปุ่ม Console
#         แตะไม่ได้ ทั้งที่โค้ดสร้างสำเร็จและไม่มี error อะไรเลย

import ui
import lcd
import time

# ตัวเลขของสนาม เขียนไว้บนสุดเป็นค่าคงที่ จะได้ไม่ต้องจำ
AREA_W = 792
AREA_H = 398
MAX_WIDGETS = 32
RESERVED_X = 690       # เลยจุดนี้ไปทางขวา รวมกับ y ข้างล่าง คือเขตห้ามวาง
RESERVED_Y = 340
COLS = 5
ROWS = 5
CELL_W = 140
CELL_H = 42
GAP_X = 10
GAP_Y = 4
LEFT = 20
TOP = 48
RUN_MS = 15000

COL_TEXT = 0xFFFFFF
COL_DIM = 0xA0B4CC
COL_OK = 0x00E676
COL_WARN = 0xFFA726

ui.screen()
time.sleep_ms(200)

# นับเองตั้งแต่ตัวแรก การรู้ว่าใช้ไปกี่ตัวก่อนสร้าง ดีกว่ามาเจอ RuntimeError ทีหลัง
ui.Label("งบประมาณของหน้าจอ", x=20, y=8, color=COL_TEXT, value=24)
ui.Label("โควตา widget ต่อหนึ่งหน้า", x=430, y=6, color=COL_DIM, value=14)
counter = ui.Label("ใช้ไป 4 / 32", x=430, y=26, color=COL_OK, value=16)

# Bar ทำให้ "งบประมาณ" กลายเป็นปริมาณที่ตามองเห็น ไม่ใช่แค่ตัวเลขในหัว
bar = ui.Bar(x=580, y=30, w=190, h=16, min=0, max=MAX_WIDGETS, value=4)
used = 4               # สี่ตัวข้างบนนี้คือของที่เราสร้างไปแล้ว


def spend(n):
    """บันทึกโควตาที่ใช้ไป แล้วอัปเดตทั้งตัวเลขและแถบให้ตรงกันในที่เดียว"""
    global used
    used = n
    counter.text("ใช้ไป " + str(used) + " / " + str(MAX_WIDGETS))
    bar.value(used)


# กันสาม slot สุดท้ายไว้ให้บรรทัดสรุปข้างล่าง ตารางจึงหยุดก่อนถึงเพดานสามตัว
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

        # เช็กโควตาก่อนสร้างทุกครั้ง ไม่ใช่สร้างแล้วรอให้มันโยน RuntimeError
        if used >= MAX_WIDGETS - RESERVE_FOR_TEXT:
            break
        ui.Button(str(r) + "," + str(c), x=x, y=y,
                  w=CELL_W, h=CELL_H, color=0x37474F, value=14)
        spend(used + 1)

# Label ที่สร้างด้วยข้อความว่าง LVGL จะเติมคำว่า "Label" ให้เอง
# แล้วคำนั้นค้างบนจอจนกว่าจะมีการเขียนทับครั้งแรก จึงต้องตั้งข้อความตั้งต้นเสมอ
summary = ui.Label("ยังไม่ได้เทียบกับ ui.list()", x=20, y=282,
                   color=COL_TEXT, value=18)
probe = ui.Label("ยังไม่ได้ตรวจเขตห้ามวาง", x=20, y=308, color=COL_WARN,
                 value=14)
ceiling = ui.Label("ยังไม่ได้ลองชนเพดาน", x=20, y=330, color=COL_DIM,
                   value=14)
spend(used + 3)

# ลองชนเพดานจริง ๆ หนึ่งครั้ง ให้เห็นว่ามันไม่ได้เตือนล่วงหน้า แต่โยน RuntimeError
if used >= MAX_WIDGETS:
    try:
        extra = ui.Button("33", x=20, y=356, w=60, h=28, color=0xFF5252)
        extra.delete()
        over = "ตัวที่ 33 สร้างได้ - แปลว่านับพลาด"
    except RuntimeError:
        over = "ตัวที่ 33 ถูกปฏิเสธ RuntimeError"
else:
    over = "ยังเหลือโควตา " + str(MAX_WIDGETS - used) + " ตัว"

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

lcd.print("โควตา 32 ตัว - พื้นที่ 792x398 - ข้ามไป " + str(skipped) + " ช่อง")
lcd.print("นับเอง " + str(used) + " - ui.list() รายงาน " + str(len(alive)))
lcd.print(over)

t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    ui.poll()
    time.sleep_ms(50)

summary.text("จบแล้ว - นับเอง " + str(used) + " - บนจอจริง " + str(len(alive)))
