# 01_board_info.py - ถามบอร์ดก่อนว่ามีอะไรให้เล่นบ้าง
#
# ไฟล์นี้สอน: เขียนโปรแกรมจากค่าที่ถามบอร์ดมา ไม่ใช่จากตัวเลขที่เราจำไว้
# ดูที่จอ   : ชื่อบอร์ดกับจำนวนหลอด แล้วต่อด้วยรายชื่อหลอดเรียงตามเลขดัชนี
#             ลิ้นชัก Console เก็บรายการเดียวกันแบบเรียงบรรทัดไว้ให้อ่านย้อนหลัง
# กับดัก    : ปุ่มที่พิมพ์บนบอร์ดว่า SW2 คือ gpio.button(0) และ .name() คืน "SW1"

import gpio
import lcd
import time
import ui

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_OK, COL_WARN, COL_INFO = 0x00E676, 0xFFA726, 0x40C4FF

# ถามครั้งเดียว เก็บ dict ไว้ในตัวแปร แล้วอ่านจากตัวแปรตลอดทั้งไฟล์
info = gpio.board_info()

lcd.clear()
lcd.console("<h2>บอร์ดตัวนี้มีอะไรบ้าง</h2>")

ui.screen()
time.sleep_ms(200)              # ให้ CM55 ล้างจอให้จบก่อนค่อยวางของ

ui.Label("บอร์ดตัวนี้มีอะไรบ้าง", x=20, y=12, color=COL_TEXT, value=24)
ui.Label(info["name"], x=20, y=58, color=COL_INFO, value=24)

# Seg7 รับ "ข้อความ" ไม่ใช่ตัวเลข สั่ง .value(3) แล้วจอจะนิ่งสนิทโดยไม่ฟ้องอะไรเลย
seg = ui.Seg7(text=str(info["leds"]), x=20, y=100, w=70, h=48, color=COL_OK)
rows = ui.Label("กำลังอ่านรายชื่อ", x=110, y=118, color=COL_TEXT, value=18)

lcd.print("ชื่อ:", info["name"])
lcd.print("ไฟ", info["leds"], "ดวง | ปุ่ม", info["buttons"], "ปุ่ม")

# led_names เป็น list ของสตริง ยาวเท่ากับ info["leds"] เสมอ
# ใช้ enumerate เพื่อได้ทั้งเลขดัชนีและชื่อในรอบเดียว ไม่ต้องนับเอง
# ลิ้นชักรับรายละเอียดเต็ม ส่วนบนจอรวบเป็นบรรทัดเดียว เพราะจอ 4.3 นิ้วมีที่จำกัด
lcd.console("<span class=muted>--- หลอดไฟ ---</span>")
line = ""
for i, name in enumerate(info["led_names"]):
    lcd.print("  gpio.led(" + str(i) + ") =", name)
    line = line + str(i) + "=" + name + "  "
rows.text("gpio.led(i) -> " + line)

lcd.console("<span class=muted>--- ปุ่ม ---</span>")
for i, name in enumerate(info["btn_names"]):
    lcd.print("  gpio.button(" + str(i) + ") =", name)

# กับดักของไฟล์นี้ ประกาศไว้บนจอเลย เพราะคนที่ก้มดูบอร์ดกับคนที่มองโค้ด
# จะเรียกปุ่มเดียวกันคนละชื่อ แล้วคุยกันไม่รู้เรื่องอยู่ครึ่งชั่วโมง
trap = ui.Label("บนแผ่นวงจรพิมพ์ว่า SW2", x=20, y=176, color=COL_WARN,
                value=18)
# ข้อความเต็มส่งด้วย .text() เพราะช่องตอนสร้าง widget แคบกว่าช่องของ .text()
trap.text("บนแผ่นวงจรพิมพ์ว่า SW2 แต่โค้ดเรียก " +
          info["btn_names"][0] + " ดัชนี 0")

# gpio.num_leds() ให้ตัวเลขเดียวกับ info["leds"] แต่สั้นกว่า
# ใช้ตัวนี้ในลูปทั่วไป จะได้ไม่ต้องเรียก board_info() ซ้ำ
n = gpio.num_leds()
lcd.print("<span class=muted>num_leds() =", n, "</span>")

# บทเรียนสำคัญของไฟล์นี้: เขียน range(n) ไม่ใช่ range(3)
# ถ้าวันหนึ่งย้ายไปบอร์ดที่มีห้าดวง โค้ดชุดนี้วิ่งครบเองโดยไม่ต้องแก้
for i in range(n):
    gpio.led(i).off()

ui.Label("range(n) สั่งดับครบ " + str(n) + " ดวง โดยไม่ต้องรู้เลข 3 มาก่อน",
         x=20, y=226, color=COL_OK, value=18)
lcd.print("<span class=ok>ดับไฟครบ", n, "ดวงแล้ว</span>")

# บรรทัดนี้ถูกปิดไว้โดยตั้งใจ ปลดคอมเมนต์แล้วรันดูสักครั้ง
# มันจะโยน ValueError เพราะบอร์ดนี้เปิดให้ Python ใช้ปุ่มเดียว
# ปุ่มอื่นบนบอร์ดมีอยู่จริงและเมนู Controls ใช้ได้ แต่ฝั่ง Python ยังเข้าไม่ถึง
# gpio.button(1)

# dict ดิบทั้งก้อนยาวเกินกว่าจะอ่านบนจอ 4.3 นิ้ว จึงส่งไปคอนโซลฝั่งคอมแทน
# ทุกอย่างที่ขึ้นจอข้างบน มาจากก้อนนี้ก้อนเดียว
print("board_info() =", info)
