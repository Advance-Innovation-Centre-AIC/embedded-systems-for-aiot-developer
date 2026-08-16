# 10_board_knows_itself.py - ถามบอร์ดว่าตัวเองมีอะไร แทนที่จะเปิดคู่มือหา
#
# ไฟล์นี้ใช้ของใหม่สองอย่าง คือ gpio.board_info() ที่คืนรายการฮาร์ดแวร์ของบอร์ด
# กับ hasattr() ที่ถามว่าโมดูลหนึ่งมีคำสั่งชื่อนั้นจริงไหม แค่สองอย่างนี้
#
# ไฟล์นี้สอน: บอร์ดตอบเองได้ว่ามีหลอดกี่ดวง ปุ่มกี่ปุ่ม ชื่ออะไรบ้าง
#             และ machine ของพอร์ตนี้มีคำสั่งไหนจริง ไม่มีคำสั่งไหน
# ดูที่จอ   : ชื่อบอร์ดกับตัวเลขสองตัว แล้วตารางสองคอลัมน์ ซ้ายคือที่มีจริง
#             ขวาคือที่ไม่มี ทั้งตารางไม่ได้พิมพ์ไว้ล่วงหน้า มันถามบอร์ดสด ๆ
# กับดัก    : โค้ดที่ลอกจากอินเทอร์เน็ตมักขึ้นต้นด้วย machine.PWM หรือ machine.ADC
#             พอร์ตนี้ไม่มีทั้งคู่ จะได้ AttributeError ทันทีตั้งแต่บรรทัดแรก
#             ไม่ใช่บอร์ดพัง แต่เป็นบอร์ดคนละใบกับที่คนเขียนโค้ดนั้นใช้

import gpio
import lcd
import machine
import time
import ui

# ชื่อคำสั่งที่จะไปถาม machine ว่ามีไหม เรียงสี่ตัวแรกคือที่คาดว่ามี
# สี่ตัวหลังคือที่โค้ดจากอินเทอร์เน็ตชอบเรียกหา
ASK = ("Pin", "I2C", "PDM_PCM", "RTC", "PWM", "ADC", "SPI", "Timer")

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_BAD, COL_INFO = 0x00E676, 0xFF5252, 0x40C4FF

# ถามครั้งเดียว เก็บ dict ไว้ในตัวแปร แล้วอ่านจากตัวแปรตลอดทั้งไฟล์
info = gpio.board_info()

ui.screen()
time.sleep_ms(200)

ui.Label("บอร์ดตอบเองว่ามีอะไร", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=650, h=104, color=COL_CARD, min=COL_DIM, max=12, value=1)

ui.Label("ชื่อบอร์ด", x=40, y=64, color=COL_DIM, value=16)
ui.Label(info["name"], x=40, y=90, color=COL_INFO, value=24)

ui.Label("หลอด", x=440, y=64, color=COL_DIM, value=16)
seg_led = ui.Seg7(text=str(info["leds"]), x=440, y=88, w=70, h=56,
                  color=COL_OK)

ui.Label("ปุ่ม", x=540, y=64, color=COL_DIM, value=16)
seg_btn = ui.Seg7(text=str(info["buttons"]), x=540, y=88, w=70, h=56,
                  color=COL_OK)

led_row = ui.Label("กำลังอ่านรายชื่อหลอด", x=20, y=166, color=COL_TEXT,
                   value=18)
btn_row = ui.Label("กำลังอ่านรายชื่อปุ่ม", x=20, y=194, color=COL_TEXT,
                   value=18)

ui.Label("ถาม machine ว่ามีคำสั่งไหนจริง", x=20, y=228, color=COL_TEXT,
         value=20)

# สร้างช่องว่างไว้แปดช่อง สี่ช่องซ้าย สี่ช่องขวา แล้วเดี๋ยวเขียนทับด้วยคำตอบ
# ห้ามสร้าง Label ด้วยข้อความว่าง LVGL จะเติมคำว่า "Label" ให้เอง แล้วมันจะค้าง
cells = []
for i in range(4):
    cells.append(ui.Label("รอถาม", x=40, y=256 + i * 26, color=COL_DIM,
                          value=18))
for i in range(4):
    cells.append(ui.Label("รอถาม", x=380, y=256 + i * 26, color=COL_DIM,
                          value=18))

note = ui.Label("ยังไม่ได้ถามอะไรเลย", x=20, y=368, color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>บอร์ดตอบเองว่ามีอะไร</h2>")
lcd.print("ชื่อ:", info["name"])
lcd.print("หลอด", info["leds"], "ดวง | ปุ่ม", info["buttons"], "ปุ่ม")

# led_names กับ btn_names เป็น list ของสตริง ยาวเท่ากับตัวเลขสองตัวข้างบนเสมอ
# enumerate ให้ทั้งเลขดัชนีและชื่อในรอบเดียว ไม่ต้องนับเอง
lcd.console("<span class=muted>--- หลอด ---</span>")
line = ""
for i, name in enumerate(info["led_names"]):
    lcd.print("  gpio.led(" + str(i) + ") =", name)
    line = line + str(i) + "=" + name + "  "
led_row.text("gpio.led(i) -> " + line)

lcd.console("<span class=muted>--- ปุ่ม ---</span>")
line = ""
for i, name in enumerate(info["btn_names"]):
    lcd.print("  gpio.button(" + str(i) + ") =", name)
    line = line + str(i) + "=" + name + "  "
btn_row.text("gpio.button(i) -> " + line)

ui.poll()
time.sleep_ms(600)

# --- ถาม machine ทีละชื่อ ---
# hasattr(machine, "PWM") ถามว่า "โมดูลนี้มีของชื่อนี้ไหม" แล้วตอบ True หรือ False
# มันไม่ได้เรียกใช้ของนั้น จึงถามได้อย่างปลอดภัยแม้กับชื่อที่ไม่มีอยู่จริง
#
# ตารางนี้ไม่ได้พิมพ์คำตอบไว้ล่วงหน้า ถ้าวันหนึ่งเฟิร์มแวร์เพิ่ม PWM เข้ามา
# ไฟล์นี้จะรายงานว่ามี โดยไม่มีใครต้องกลับมาแก้บรรทัดไหนเลย
have = 0
lcd.console("<span class=muted>--- machine ---</span>")

for i, name in enumerate(ASK):
    ok = hasattr(machine, name)

    if ok:
        have = have + 1
        cells[i].color(COL_OK)
        cells[i].text(name + "   มี")
        lcd.print("<span class=ok>machine." + name + " มี</span>")
    else:
        cells[i].color(COL_BAD)
        cells[i].text(name + "   ไม่มี")
        lcd.print("<span class=error>machine." + name + " ไม่มี</span>")

    ui.poll()
    time.sleep_ms(220)

note.color(COL_TEXT)
note.text("ถาม " + str(len(ASK)) + " ชื่อ มีจริง " + str(have) + " ชื่อ")
ui.poll()

lcd.console("<span class=muted>------------------------</span>")
lcd.print("<span class=ok>มีจริง", have, "จาก", len(ASK), "ชื่อ</span>")

# ไม่มี PWM แล้วจะหรี่ไฟยังไง คำตอบอยู่ที่ gpio ไม่ใช่ที่ machine
# gpio.led(n).brightness(pct) สร้างพัลส์ให้เองด้วยซอฟต์แวร์ ไม่ต้องพึ่งขา PWM
# ส่วน .hold(pct, ms) ย้ำพัลส์นั้นซ้ำจนครบเวลา ตาจึงเห็นเป็นระดับที่ค้างอยู่
# เรื่องนี้เป็นงานเต็ม ๆ ของคาบ 3 ตรงนี้แค่ให้เห็นว่าทางออกมีอยู่
lcd.print("<span class=info>ไม่มี PWM ก็หรี่ไฟได้ ด้วย gpio.led().brightness()</span>")

# dict ดิบทั้งก้อนยาวเกินกว่าจะอ่านบนจอ 4.3 นิ้ว จึงส่งไปคอนโซลฝั่งคอมแทน
print("board_info() =", info)
print("machine ที่มีจริง:", [n for n in ASK if hasattr(machine, n)])

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# เติมชื่อที่คุณเคยเห็นในโค้ดจากอินเทอร์เน็ตลงใน ASK อีกสองชื่อ เช่น "UART"
# หรือ "DAC" แล้วทำนายก่อนรันว่าจะได้เขียวหรือแดง จากนั้นรันจริงแล้วเทียบ
# ใบ้: ตารางวางไว้แปดช่อง ถ้าเติมเกินนั้น ช่องที่เกินจะไม่มีที่ให้เขียน
#      ต้องขยับตัวเลข 4 ในลูปที่สร้าง cells ให้พอกับความยาวของ ASK ด้วย
