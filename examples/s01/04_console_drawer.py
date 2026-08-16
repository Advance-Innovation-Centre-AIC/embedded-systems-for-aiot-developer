# 04_console_drawer.py - ลิ้นชัก Console กับการ์ดสรุปที่ไม่ต้องเปิดลิ้นชัก
#
# Why : รายงานผลตรวจที่ยาวสิบบรรทัด คนดูหน้างานไม่ได้อยากอ่านทั้งสิบบรรทัด
#       เขาอยากรู้บรรทัดเดียวว่า "ผ่านกี่ข้อจากกี่ข้อ" แล้วค่อยเจาะเฉพาะข้อที่ตก
# What: lcd.console() กับ lcd.print() เขียนลงลิ้นชักที่ปิดอยู่เหมือนกัน ต่างกันที่
#       console() เหมาะกับหัวเรื่องและเส้นคั่นที่ไม่ใช่ข้อมูล ส่วนการ์ด ui ด้านบน
#       คือบทสรุปที่เห็นได้ทันทีโดยไม่ต้องกดปุ่มอะไรเลย
#
# ดูที่จอ: การ์ดบนสุดนับขึ้นจาก 0/4 พร้อมแถบความคืบหน้า และรายการสี่ข้อ
#          ไล่โผล่ทีละข้อ เขียวคือผ่าน แดงคือไม่ผ่าน
# กับดัก : มุมขวาล่างราว 100x58 พิกเซลเป็นของปุ่ม Console ที่บอร์ดจองไว้แล้ว
#          ห้ามวาง widget ทับตรงนั้น ไม่งั้นกดเปิดลิ้นชักไม่ได้

import lcd
import time
import ui

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

# รายการที่จะตรวจ เก็บเป็น list ของ tuple สองช่อง (ชื่อรายการ, ผลที่ได้)
# เขียนข้อมูลไว้ก้อนเดียวข้างบน แล้วให้ลูปข้างล่างเป็นคนแสดงผล
# เพิ่มรายการทีหลังจึงแก้แค่ list ไม่ต้องแตะตรรกะการรายงานเลย
CHECKS = [
    ("จอแสดงผล", True),
    ("ปุ่มบนบอร์ด", True),
    ("หลอด LED", True),
    ("การ์ด SD", False),
]

TOTAL = len(CHECKS)

ui.screen()
time.sleep_ms(200)

ui.Label("รายงานตรวจสอบบอร์ด", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=652, h=112, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ผ่านแล้ว", x=40, y=64, color=COL_DIM, value=16)

# Seg7 รับข้อความ ไม่ใช่ตัวเลข จึงเขียน "0/4" ตรง ๆ ได้เลย
# ถ้าเผลอเรียก seg.value(0) จะไม่มีอะไรเกิดขึ้น และไม่มี error ให้จับด้วย
seg = ui.Seg7(text="0/" + str(TOTAL), x=40, y=96, w=160, h=48, color=COL_OK)
bar = ui.Bar(x=220, y=104, w=432, h=24, min=0, max=TOTAL, value=0)

# สองป้ายสรุปนี้สร้างไว้ตั้งแต่ตอนนี้ แล้วเดี๋ยวค่อยเขียนทับตอนรู้ผล
# ถ้าไปสร้างข้างใน if/else ทีหลัง จะได้ป้ายคนละใบวางที่พิกัดเดียวกันสองใบ
# ซึ่งบนจอจริงคือตัวหนังสือซ้อนกันอ่านไม่ออก แม้ตอนรันจะเข้าแค่ทางเดียวก็ตาม
sum_lbl = ui.Label("ยังตรวจไม่จบ", x=20, y=324, color=COL_DIM, value=20)
sum_more = ui.Label("รายละเอียดอยู่ในลิ้นชัก Console", x=20, y=352,
                    color=COL_DIM, value=20)

lcd.clear()

# console() ใช้กับสิ่งที่ "จัดหน้า" ไม่ใช่ "ข้อมูล" เช่นหัวเรื่องกับเส้นคั่น
lcd.console("<h2>รายงานตรวจสอบบอร์ด</h2>")
lcd.console("<span class=muted>เริ่มตรวจ</span>")

passed = 0
row_y = 180
for name, ok in CHECKS:
    if ok:
        passed = passed + 1
        ui.Label(name + "  ผ่าน", x=40, y=row_y, color=COL_OK, value=20)
        lcd.print(name, "<span class=ok>ผ่าน</span>")
    else:
        # รายงานทั้งตอนผ่านและตอนไม่ผ่าน โปรแกรมที่พูดเฉพาะตอนสำเร็จ
        # จะเงียบสนิทตอนล้มเหลว ซึ่งคือจังหวะที่คนใช้อยากรู้ที่สุด
        ui.Label(name + "  ไม่ผ่าน", x=40, y=row_y, color=COL_BAD, value=20)
        lcd.print(name, "<span class=error>ไม่ผ่าน</span>")

    seg.text(str(passed) + "/" + str(TOTAL))
    bar.value(passed)
    row_y = row_y + 34

    ui.poll()
    time.sleep_ms(500)

# เส้นคั่นแยกส่วนข้อมูลออกจากส่วนสรุป ใช้ console() เพราะมันไม่ใช่ข้อมูล
lcd.console("<span class=muted>------------------------</span>")

# เลือกสีของบรรทัดสรุปจากผลจริง ไม่ใช่ทาเขียวไว้ก่อนแล้วค่อยดู
# การ์ดด้านบนกับบรรทัดในลิ้นชักต้องเล่าเรื่องเดียวกันเสมอ
if passed == TOTAL:
    seg.color(COL_OK)
    sum_lbl.text("ผ่านครบทุกรายการ")
    sum_lbl.color(COL_OK)
    sum_more.color(COL_DIM)
    lcd.print("<span class=ok>ผ่านครบ", passed, "รายการ</span>")
else:
    seg.color(COL_WARN)
    # แยกเป็นสองป้าย เพราะป้ายหนึ่งใบพาข้อความไปได้ 126 ไบต์ และไทยตัวละ 3 ไบต์
    sum_lbl.text("ยังมีรายการที่ตก")
    sum_lbl.color(COL_WARN)
    sum_more.color(COL_WARN)
    lcd.print("<span class=warn>ผ่าน", passed, "จาก", TOTAL, "</span>")

ui.poll()

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# แก้ CHECKS ให้ผ่านครบทุกข้อ แล้วรันดูว่าการ์ดข้างบนเปลี่ยนไปยังไง
# จากนั้นเพิ่มรายการที่ห้าเข้าไป และสังเกตว่าต้องแก้อะไรบ้างนอกจาก CHECKS
# ใบ้: ลองนับ y ของแถวสุดท้ายดูว่ามันยังอยู่ในจอ 398 พิกเซลหรือเปล่า
