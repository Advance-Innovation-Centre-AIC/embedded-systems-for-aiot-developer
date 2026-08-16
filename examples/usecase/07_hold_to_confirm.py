# 07_hold_to_confirm.py - กดค้างเพื่อยืนยันคำสั่งที่ย้อนกลับไม่ได้
#
# Why : ปุ่ม "ล้างค่าโรงงาน" ของเราเตอร์ ปุ่มหยุดฉุกเฉินของสายพาน และปุ่มลบข้อมูล
#       ของเครื่องมือแพทย์ ล้วนต้องกดค้าง ไม่ใช่แตะ เพราะการแตะโดนโดยบังเอิญ
#       เกิดขึ้นได้ แต่การกดค้างสามวินาทีไม่เกิดเอง
# What: การยืนยันที่ดีต้องมี "ทางถอย" ปล่อยมือก่อนครบ = ยกเลิก ไม่มีผลใด ๆ
#       และต้องมีตัวบอกความคืบหน้า ไม่งั้นผู้ใช้ไม่รู้ว่าต้องค้างอีกนานแค่ไหน
#
# hold() ไม่ใช่ brightness(): brightness(pct) คือพัลส์เดียวยาวราว 12 ms แล้วจบ
#   ด้วยหลอดดับ (gpio_led_brightness() ปิดท้ายด้วย Cy_GPIO_Clr ใน modgpio.c)
#   hold(pct, ms) ทำพัลส์ซ้ำให้ตลอด ms ที่ขอ ตาจึงเห็นเป็นระดับที่ค้างอยู่
#   และเพราะมันกินเวลา ms พอดี จึงใช้แทน time.sleep_ms() ปิดท้ายลูปได้เลย
#
# ดูที่จอ: หน้าปัดวงกลมขวาคือความคืบหน้า ไต่จาก 0 ถึง 100% ตอนกดค้าง
#         Seg7 บอกเปอร์เซ็นต์เป็นตัวเลข การ์ดล่างบอก ยกเลิก หรือ ยืนยันแล้ว
#         กราฟซ้ายเก็บรูปการกดไว้ให้เห็นว่าปล่อยตรงไหน เส้นแดงคือเส้นครบ 100%
# ดูที่หลอด: LED2 สว่างขึ้นเรื่อย ๆ ตามเวลาที่ค้าง และดับทันทีที่ปล่อยมือ
#         ระดับที่เห็นคือระดับจริง ไม่ใช่การกะพริบสั้น ๆ ที่ตาแยกไม่ออก
# กับดัก : ห้ามใช้ sleep นับถอยหลัง เพราะจะตรวจไม่เจอตอนผู้ใช้ปล่อยมือกลางทาง
#         ต้องวนอ่านปุ่มตลอดเวลา แล้วเทียบเวลาเอาเอง

import gpio
import lcd
import time
import ui

HOLD_MS = 3000     # ต้องค้างครบเท่านี้ถึงจะนับว่ายืนยัน
DEBOUNCE_MS = 25
SAMPLE_MS = 60     # ส่งจุดขึ้นกราฟทุกกี่มิลลิวินาที
LOOP_MS = 10       # หนึ่งรอบลูปกินเวลาเท่านี้ และ led.hold() เป็นคนกินเวลานั้น

btn = gpio.button(0)
prog = gpio.led(2)      # ไฟบอกความคืบหน้า
done = gpio.led(0)      # ไฟยืนยันว่าคำสั่งทำงานแล้ว

lcd.clear()
lcd.console("<h2>กดค้าง 3 วินาทีเพื่อยืนยัน</h2>")
lcd.print("ปล่อยมือก่อนครบ = ยกเลิก ไม่มีผล")

ui.screen()
ui.Label("กดค้างเพื่อยืนยัน", x=12, y=6, value=24)
ch = ui.Chart(x=12, y=44, w=360, h=200, min=-5, max=105, color=0x00BFFF)
s_goal = ch.add_series(0xFF5555)
ui.Label("เส้นฟ้า = เปอร์เซ็นต์ที่ค้างอยู่", x=12, y=250, value=16,
         color=0x00BFFF)
ui.Label("เส้นแดง = เส้นครบ 100%", x=12, y=274, value=16, color=0xFF5555)

ui.Label("ต้องกดค้างครบ 3.0 วินาที", x=390, y=44, value=16)
ui.Label("ปล่อยก่อนครบ = ยกเลิก", x=390, y=68, value=16)
ui.Label("หน้าปัดความคืบหน้า", x=598, y=12, value=16)
arc = ui.Arc(x=598, y=40, w=170, h=170, min=0, max=100, value=0)
seg = ui.Seg7(x=390, y=104, w=120, h=40)
seg.text("0")      # Seg7 รับ "ข้อความ" ถ้าไม่ตั้งค่า มันจะค้างที่ 0000
ui.Label("%", x=518, y=112, value=20)

ui.Label("ความคืบหน้า", x=390, y=196, value=16)
bar = ui.Bar(x=390, y=222, w=380, h=18, min=0, max=100, value=0)

ui.Panel(x=390, y=252, w=390, h=72, color=0x1A1A2E, min=0x3F4247, value=2)
vd = ui.Label("รอการกด", x=406, y=262, value=28, color=0x8899AA)
sub = ui.Label("กดค้างที่ปุ่ม SW1 บนบอร์ด", x=406, y=298, value=16)
ui.poll()

# เส้นเป้าหมายเติมให้เต็มก่อน เส้นฟ้าจะได้มีอะไรให้เทียบตั้งแต่จุดแรก
for _ in range(50):
    ch.set_next(s_goal, 100)

prev = btn.value()
last_edge = time.ticks_ms()
sample_at = time.ticks_ms()
down_at = 0
fired = False
shown = -1
pct = 0

for _ in range(8000):
    now = time.ticks_ms()
    v = btn.value()

    if v != prev and time.ticks_diff(now, last_edge) > DEBOUNCE_MS:
        last_edge = now
        if v == 0:
            down_at = now
            fired = False
            vd.text("กำลังค้าง...")
            vd.color(0xFFC107)
        else:
            if not fired:
                lcd.print("ยกเลิก - ปล่อยที่", shown, "%")
                vd.text("ยกเลิก")
                vd.color(0x8899AA)
                sub.text("ปล่อยที่ " + str(shown) + "% - ไม่มีผลใด ๆ")
            prog.off()
            shown = -1
            pct = 0
            arc.value(0)
            bar.value(0)
            seg.text("0")
        prev = v

    if v == 0 and not fired:
        held = time.ticks_diff(now, down_at)
        pct = min(100, held * 100 // HOLD_MS)
        arc.value(int(pct))
        bar.value(int(pct))
        seg.text(str(pct))
        if pct // 10 != shown // 10:
            lcd.print("ค้างอยู่", pct, "%")
        shown = pct
        if held >= HOLD_MS:
            fired = True
            prog.off()
            vd.text("ยืนยันแล้ว")
            vd.color(0x33DD77)
            sub.text("คำสั่งถูกดำเนินการเรียบร้อย")
            for _ in range(6):
                done.toggle()
                ui.poll()
                time.sleep_ms(80)
            done.off()
            lcd.print("<span class=ok>ยืนยันแล้ว - คำสั่งถูกดำเนินการ</span>")

    # กราฟเก็บรูปการกดไว้ ปล่อยตรงไหนก็เห็นตรงนั้น
    if time.ticks_diff(now, sample_at) >= SAMPLE_MS:
        sample_at = now
        ch.set_next(0, int(pct))
        ch.set_next(s_goal, 100)

    ui.poll()
    # ไฟบอกความคืบหน้าถูกสั่งซ้ำทุกรอบ ตาจึงเห็นเป็นระดับที่ค้างอยู่ ไม่ใช่พัลส์เดี่ยว
    # ตอนไม่ได้กด pct เป็น 0 ซึ่ง hold() แปลว่าดับ - บรรทัดเดียวจบทุกกรณี
    # ยิงคำสั่งไปแล้ว (fired) ก็ดับ เพราะไฟดวงนี้บอก "ยังเหลืออีกเท่าไร" ไม่ใช่ "เสร็จแล้ว"
    # และ hold() กินเวลา LOOP_MS พอดี จึงทำหน้าที่แทน time.sleep_ms() ไปในตัว
    prog.hold(0 if fired else int(pct), LOOP_MS)

prog.off()
done.off()
sub.text("จบรอบทดสอบ")
print("จบ")
