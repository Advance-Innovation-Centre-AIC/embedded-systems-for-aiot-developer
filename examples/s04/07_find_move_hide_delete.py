# 07_find_move_hide_delete.py - จัดการ widget ที่สร้างไปแล้ว
#
# Why : สามคาบที่ผ่านมาเราสร้าง widget แล้วปล่อยไว้ที่เดิมตลอดโปรแกรม พองาน
#       โตขึ้นจะเจอโจทย์ที่ตรงกันข้าม คือของอยู่บนจอแล้ว และเราต้องย้ายมัน
#       ย่อมัน ซ่อนมัน หรือลบมันทิ้งเพื่อคืนโควตา ทั้งสี่ท่านี้มีอยู่ในโมดูล
#       แต่ไม่เคยมีไฟล์ไหนในคลังนี้แตะเลย จนกระทั่งไฟล์นี้
# What: ui.list() บอกว่าบนจอมีอะไรอยู่จริง - ui.get(id) เอา widget กลับคืนมา
#       จากเลขอย่างเดียว - แล้ว .pos() .size() .show() .hide() .delete()
#       ทำงานกับ widget ที่ได้คืนมานั้นได้เหมือนตัวจริงทุกอย่าง
#
# ดูที่จอ: กล่องสีส้มหนึ่งใบเป็นเป้าหมาย ปุ่มสามใบสั่งย้าย สั่งย่อ-ขยาย
#          และสั่งซ่อน-แสดง บรรทัดล่างรายงานว่า ui.list() นับได้กี่ตัว
# กับดัก : ui.list() ใช้คีย์ 'id' ส่วน ui.poll() ใช้คีย์ 'handle' คนละคำ
#          ค่าเดียวกัน เขียนสลับกันแล้วจะได้ KeyError ที่อ่านแล้วงง
#          และของที่ .hide() ไว้ ยังกินโควตาอยู่ (เพดานเฟิร์มแวร์ 64 - คอร์สใช้ไม่เกิน 32) มีแต่ .delete() ที่คืนช่อง

import ui
import lcd
import time

RUN_MS = 30000

COL_TEXT = 0x4A9EFF
COL_DIM = 0x9AA3AF
COL_CARD = 0x171B22
COL_OK = 0x30A46C
COL_WARN = 0xF5A623
COL_INFO = 0x4A9EFF

# ตำแหน่งสองจุดที่เป้าหมายจะสลับไปมา และสองขนาดที่จะสลับกัน
POS_A = (430, 90)
POS_B = (430, 190)
SIZE_A = (220, 70)
SIZE_B = (140, 44)

ui.screen()
time.sleep_ms(200)

ui.Label("ตามหา ย้าย ซ่อน แล้วลบ", x=20, y=12, color=COL_TEXT, value=24)

ui.Panel(x=20, y=52, w=380, h=252, color=COL_CARD, min=COL_DIM, max=12, value=1)

b_move = ui.Button("ย้ายที่", x=40, y=72, w=172, h=88, color=0x4A9EFF, value=20)
b_size = ui.Button("ย่อ ขยาย", x=220, y=148, w=172, h=88, color=0x30A46C,
                   value=20)
b_hide = ui.Button("ซ่อน แสดง", x=40, y=224, w=172, h=88, color=0x4A9EFF,
                   value=20)

# เป้าหมายที่ปุ่มทั้งสามใบจะไปสั่ง สร้างครั้งเดียวแล้วไม่แตะตัวแปรนี้อีกเลย
# ทุกคำสั่งข้างล่างจะไปหามันผ่าน ui.get() เพื่อพิสูจน์ว่าเลขอย่างเดียวก็พอ
target = ui.Button("เป้าหมาย", x=POS_A[0], y=POS_A[1],
                   w=SIZE_A[0], h=SIZE_A[1], color=0xF5A623, value=20)
TARGET_ID = target.id()

status = ui.Label("ยังไม่ได้สั่งอะไร", x=432, y=264, color=COL_INFO, value=20)
census = ui.Label("ยังไม่ได้นับ", x=432, y=296, color=COL_DIM, value=16)
ui.Label("ui.list() ใช้คีย์ id ส่วน poll ใช้ handle", x=168, y=332,
         color=COL_WARN, value=16)

ID_MOVE = b_move.id()
ID_SIZE = b_size.id()
ID_HIDE = b_hide.id()

lcd.clear()
lcd.console("<h2>จัดการ widget ที่สร้างไปแล้ว</h2>")

# ------------------------------------------------------------------
# หลักฐานชิ้นที่ 1 - ui.list() บอกว่ามีอะไรอยู่จริง และคีย์ของมันชื่อ id
# ------------------------------------------------------------------
alive = ui.list()
lcd.print("ui.list() นับได้", len(alive), "ตัว")
for row in alive:
    # row["id"] ไม่ใช่ row["handle"] - เขียนผิดคำจะได้ KeyError
    lcd.print("  id=" + str(row["id"]) + " type=" + row["type"])


def census_text():
    """นับใหม่ทุกครั้งที่มีการเปลี่ยนแปลง แล้วเขียนลงบรรทัดรายงาน"""
    rows = ui.list()
    census.text("ui.list() = " + str(len(rows)) + " ตัว")
    return len(rows)


census_text()

# ------------------------------------------------------------------
# หลักฐานชิ้นที่ 2 - ui.get() ปฏิเสธเลขนอกช่วง 0-63 ด้วย ValueError (เพดานเฟิร์มแวร์ 64)
# ลองจริงหนึ่งครั้ง ดีกว่าเชื่อเพราะสไลด์บอก
# ------------------------------------------------------------------
try:
    ui.get(99)
    lcd.print("ui.get(99) ผ่านได้ - ผิดจากที่เอกสารบอก")
except ValueError:
    lcd.print("<span class=ok>ui.get(99) โยน ValueError ตามคาด</span>")

# เลขที่อยู่ในช่วงแต่ไม่มี widget ตัวนั้น ก็ ValueError เหมือนกัน
try:
    ui.get(31)
    lcd.print("id 31 มีตัวตนอยู่จริง")
except ValueError:
    lcd.print("<span class=ok>id 31 ยังว่าง - ValueError เช่นกัน</span>")

# ------------------------------------------------------------------
# หลักฐานชิ้นที่ 3 - .text() แบบไม่ใส่อาร์กิวเมนต์ "อ่านกลับได้แล้ว"
#
# บรรทัดเหล่านี้เคยเขียนว่าอ่านกลับไม่ได้ และต้องจำไว้ฝั่งเราเอง ซึ่งจริงจนถึง
# 15 ส.ค. 2026 เฟิร์มแวร์ได้ opcode GET_TEXT (0x6B) เมื่อ 16 ส.ค. modui.c จึง
# คืนสตริงจริงออกมา ไม่ใช่ None อีกแล้ว
# ------------------------------------------------------------------
lcd.print("target.text() แบบอ่าน คืน " + str(target.text()))
lcd.print("จอบอกได้เองแล้วว่าเขียนอะไรอยู่ ไม่ต้องจำฝั่งเรา")

# ------------------------------------------------------------------
# หลักฐานชิ้นที่ 4 - .delete() คืนโควตาจริง ส่วน .hide() ไม่คืน
# สร้างของใช้แล้วทิ้งหนึ่งตัว นับก่อน-หลัง แล้วลบทิ้ง
# ------------------------------------------------------------------
before = census_text()
throwaway = ui.Button("ชั่วคราว", x=20, y=308, w=140, h=88, color=0x4A9EFF,
                      value=20)
mid = len(ui.list())
throwaway.delete()
time.sleep_ms(120)          # ให้ CM55 ลบเสร็จก่อนไปนับใหม่
after = census_text()
lcd.print("นับได้ " + str(before) + " -> " + str(mid) + " -> " + str(after))
lcd.print("delete() คืนช่องในโควตา 64 ให้จริง")

hidden = False              # ความจริงเรื่องซ่อน-แสดงอยู่ที่ตัวแปรนี้ที่เดียว
at_a = True                 # ตอนนี้เป้าหมายอยู่ตำแหน่ง A หรือเปล่า
big = True                  # ตอนนี้เป้าหมายเป็นขนาดใหญ่หรือเปล่า

lcd.print("แตะปุ่มซ้ายสามใบ แล้วมองกล่องสีส้ม")

t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] != "clicked":
            continue
        h = ev["handle"]

        # ทุกกิ่งเริ่มด้วยการไปเอา widget กลับมาจากเลข ไม่ได้ใช้ตัวแปร target
        # นี่คือท่าเดียวกับที่โปรแกรมใหญ่ ๆ ใช้ตอนที่ผังจอมาจากไฟล์ข้อมูล
        if h == ID_MOVE:
            w = ui.get(TARGET_ID)
            at_a = not at_a
            x, y = POS_A if at_a else POS_B
            w.pos(x, y)
            status.text("pos(" + str(x) + ", " + str(y) + ")")
            lcd.print("pos ->", x, y)

        elif h == ID_SIZE:
            w = ui.get(TARGET_ID)
            big = not big
            cw, ch = SIZE_A if big else SIZE_B
            w.size(cw, ch)
            status.text("size(" + str(cw) + ", " + str(ch) + ")")
            lcd.print("size ->", cw, ch)

        elif h == ID_HIDE:
            w = ui.get(TARGET_ID)
            hidden = not hidden
            if hidden:
                w.hide()
                # ของที่ซ่อนอยู่ยังอยู่ในบัญชี ตัวเลขจาก ui.list() จึงไม่ลด
                status.text("hide() - ยังอยู่ในโควตา")
            else:
                w.show()
                status.text("show() - กลับมาแล้ว")
            lcd.print("hidden =", hidden, "- ui.list() =", len(ui.list()))
            census_text()

    time.sleep_ms(50)

# ปิดท้ายด้วยการล้างทั้งหน้า ui.clear() ลบทุกตัวและคืนโควตาให้ครบ 64
# ต่างจาก ui.screen() ตรงที่ไม่ได้ตั้งขนาดพื้นที่วาดใหม่ให้ด้วย
status.text("กำลังจะเรียก ui.clear()")
ui.poll()
time.sleep_ms(900)

ui.clear()
time.sleep_ms(200)
lcd.print("ui.clear() แล้ว - ui.list() =", len(ui.list()))
lcd.print("<span class=ok>จอว่าง โควตากลับมาครบ 64 ช่อง</span>")
