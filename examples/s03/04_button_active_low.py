# 04_button_active_low.py - ปุ่มนี้ 0 คือกด ไม่ใช่ 1
#
# ไฟล์นี้สอน: .value() คืนแรงดันดิบบนขา 1 = ปล่อย 0 = กด ส่วน .is_pressed()
#             คืนความหมายที่คนเข้าใจ สองตัวนี้กลับด้านกัน
# ดูที่จอ   : เลข 1 ตัวใหญ่ทางซ้ายคู่กับคำว่า ปล่อย ทางขวา กดปุ่ม SW2 ค้างไว้
#             เลขจะเป็น 0 คำจะเป็น กดอยู่ และเส้นกราฟจะตกลงพร้อมกัน
# กับดัก    : ปุ่มที่พิมพ์บนบอร์ดว่า SW2 คือ gpio.button(0) และ .name() คืน "SW1"
#             เรียก gpio.button(1) เมื่อไรได้ ValueError ทันที เพราะมีปุ่มเดียว

import gpio
import lcd
import time
import ui

RUN_MS = 15000
POLL_MS = 100

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_INFO = 0x00E676, 0xFFA726, 0x40C4FF

btn = gpio.button(0)

ui.screen()
time.sleep_ms(200)

ui.Label("ปุ่ม active-low", x=20, y=12, color=COL_TEXT, value=24)

# ซ้ายคือค่าดิบจาก value() ขวาคือความหมายจาก is_pressed() วางคู่กันให้เทียบด้วยตา
raw_seg = ui.Seg7(text="1", x=20, y=52, w=90, h=64, color=COL_INFO)
mean_lbl = ui.Label("value() = 1  is_pressed() = ปล่อย", x=130, y=76,
                    color=COL_DIM, value=22)

# กราฟค่าดิบ ตั้งช่วงเป็น 0 ถึง 1 พอดี เส้นจึงมีแค่สองระดับ อ่านออกทันที
# ว่าช่วงไหนนิ้วแตะอยู่ โดยไม่ต้องอ่านตัวเลขสักตัว
chart = ui.Chart(x=20, y=136, w=650, h=110, min=0, max=1, color=COL_CARD)
line = chart.add_series(COL_WARN)
legend = ui.Label("เส้นบน 1 = ปล่อย", x=20, y=254, color=COL_DIM, value=18)
# ข้อความเต็มส่งด้วย .text() เพราะช่องตอนสร้าง widget แคบกว่าช่องของ .text()
legend.text("เส้นบน 1 = ปล่อย (pull-up) เส้นล่าง 0 = กด (ลัดลงกราวด์)")

# ประกาศกับดักชื่อไว้บนจอตั้งแต่ต้น เพื่อให้คนที่มองบอร์ดกับคนที่มองโค้ด
# คุยกันรู้เรื่อง ป้ายใบนี้จะถูกใช้เขียนสรุปตอนหมดเวลาด้วย
name_lbl = ui.Label("บนบอร์ดพิมพ์ว่า SW2", x=20, y=292, color=COL_WARN,
                    value=18)
name_lbl.text("บนบอร์ดพิมพ์ว่า SW2 - โค้ดเรียกว่า " + btn.name() + " ดัชนี 0")

lcd.clear()
lcd.console("<h2>ปุ่ม active-low</h2>")
lcd.print("<span class=warn>บนบอร์ดพิมพ์ว่า SW2</span>")
lcd.print("<span class=warn>โค้ดเรียกว่า " + btn.name() + " ดัชนี 0</span>")
lcd.print("<span class=muted>กดปุ่มค้างไว้แล้วดูสองฝั่ง</span>")

# จำค่าที่รายงานไปครั้งล่าสุด เพื่อเขียนลิ้นชักเฉพาะตอนค่าเปลี่ยน
# ถ้าเขียนทุกรอบ ลิ้นชักจะเต็มไปด้วยบรรทัดซ้ำจนหาจังหวะที่เปลี่ยนไม่เจอ
last_shown = -1
edges = 0
t0 = time.ticks_ms()

while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    # อ่านทั้งสองเมธอดในรอบเดียวกัน จะได้เทียบกันได้จริง
    # ถ้าอ่านคนละรอบ นิ้วอาจขยับระหว่างนั้น แล้วสองค่าจะไม่ตรงกัน
    raw = btn.value()
    pressed = btn.is_pressed()

    # กราฟยิงทุกรอบ ไม่ใช่ยิงเฉพาะตอนเปลี่ยน เส้นจึงเดินสม่ำเสมอตามเวลาจริง
    # และคนดูรู้ได้ว่าโปรแกรมยังเดินอยู่ ต่างจากเส้นที่นิ่งเพราะโปรแกรมค้าง
    chart.set_next(line, raw)

    if raw != last_shown:
        last_shown = raw
        edges = edges + 1
        raw_seg.text(str(raw))
        legend.text("ค่าเปลี่ยนไปแล้ว " + str(edges) + " ครั้ง")

        if pressed:
            raw_seg.color(COL_WARN)
            mean_lbl.text("value() = 0  is_pressed() = กดอยู่")
            mean_lbl.color(COL_OK)
            lcd.print("value() =", raw, "-> <span class=ok>กดอยู่</span>")
        else:
            raw_seg.color(COL_INFO)
            mean_lbl.text("value() = 1  is_pressed() = ปล่อย")
            mean_lbl.color(COL_DIM)
            lcd.print("value() =", raw, "-> <span class=muted>ปล่อย</span>")

    ui.poll()
    time.sleep_ms(POLL_MS)

# ทำไมวงจรถึงออกแบบให้กลับด้าน: ตอนไม่กด มีตัวต้านทาน pull-up ดึงขาไว้ที่ไฟบวก
# ค่าจึงเป็น 1 พอกดปุ่ม สวิตช์ต่อขานั้นลงกราวด์ ค่าจึงตกเป็น 0
# แบบนี้ทำให้ขาไม่เคยลอย ซึ่งเป็นสาเหตุของค่าที่อ่านได้มั่ว ๆ
legend.text("หมดเวลา - ค่าเปลี่ยนไปแล้ว " + str(edges) + " ครั้ง")
name_lbl.text("เข้าใจวงจรด้วย value() เขียนตรรกะด้วย is_pressed()")
name_lbl.color(COL_DIM)
lcd.print("<span class=muted>1 = pull-up ดึงไว้</span>")
lcd.print("<span class=muted>0 = ปุ่มลัดลงกราวด์</span>")
ui.poll()
