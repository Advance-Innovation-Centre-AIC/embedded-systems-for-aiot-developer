# 02_led_blink.py - ทำให้ไฟกะพริบเป็นจังหวะ แล้วนับรอบที่กะพริบไปแล้ว
#
# Why : งานปลายคาบคือไฟวิ่งครบทุกดวงบนบอร์ดที่ปรับจังหวะได้ ทุกอย่างเริ่มจากไฟดวงเดียว
#       ที่ติดแล้วดับตามเวลาที่เรากำหนดเอง ถ้าคุมจังหวะของดวงเดียวไม่ได้
#       หลายดวงก็คุมไม่ได้
# What: กะพริบหนึ่งรอบคือ สั่งติด รอ สั่งดับ รอ เวลาที่รออยู่นั่นแหละคือจังหวะ
#       led.on() กับ led.off() สั่งค่าตรง ๆ ไม่สนใจว่าเดิมเป็นอะไร
#       led.toggle() สั่งกลับด้านจากค่าเดิม เขียนสั้นกว่าแต่ผลขึ้นกับค่าเดิมเสมอ
#
# ดูที่จอ: หลอดที่เลือกไว้ (ชื่อขึ้นบนจอ) กะพริบทุก 250 ms ป้ายบนจอเขียนว่า ติด หรือ ดับ
#          ตรงกับหลอดจริงทุกครั้ง และตัวเลขนับรอบเดินขึ้นทีละหนึ่ง
# กับดัก : ถ้าจบลูปด้วย toggle() จำนวนคี่ ไฟจะค้างติดโดยไม่ได้ตั้งใจ
#          โปรแกรมที่จบแล้วต้องบอกได้ว่าไฟอยู่สถานะไหน จึงต้องปิดท้ายด้วย off()

import gpio
import lcd
import time
import ui

ROUNDS = 6              # กะพริบกี่รอบต่อหนึ่งชุด
ON_MS = 250             # ติดค้างนานเท่าไร
OFF_MS = 250            # ดับค้างนานเท่าไร

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_OK, COL_WARN, COL_INFO = 0x30A46C, 0xF5A623, 0x4A9EFF

# เลือกดวงจากชื่อที่ board_info() รายงาน ไม่ใช่จากเลขที่จำมา
# บอร์ดที่ประกอบบนฐานแล้ว หลอด LED1/LED2 บนโมดูลอาจถูกบังจนมองไม่เห็น
# ถ้าบอร์ดมีดวงชื่อ RGB_ ใช้ดวงนั้น ไม่มีก็ใช้ดวงแรก - เลขดัชนีอาจถูกจัดใหม่ในรุ่นหน้า
# หมายเหตุ Eva Kit: ดวงที่ชื่อ RGB_RED บนบอร์ดนั้นคือหลอดสีน้ำเงินจริง ๆ (ชื่อในตาราง
#   เฟิร์มแวร์ไม่ตรงกับสี - กับดักที่คาบ 3 พูดถึง) ไฟล์นี้ต้องการแค่ "ดวงที่มองเห็น" ไม่ได้ต้องการสี
LED = 0
for i, name in enumerate(gpio.board_info()["led_names"]):
    if name.startswith("RGB_"):
        LED = i
        break

# ขอตัวจัดการมาเก็บไว้ในตัวแปรครั้งเดียว ไม่ต้องเรียก gpio.led(LED) ซ้ำทุกบรรทัด
led = gpio.led(LED)
led.off()               # เริ่มจากสถานะที่เรารู้แน่ว่าคืออะไร

ui.screen()
time.sleep_ms(200)

ui.Label("ไฟกะพริบและตัวนับรอบ", x=20, y=12, color=COL_TEXT, value=24)
state = ui.Label("ดับ", x=20, y=64, color=COL_DIM, value=28)
seg = ui.Seg7(text="0", x=200, y=56, w=140, h=56, color=COL_OK)
how = ui.Label("led.on() / led.off()", x=372, y=72, color=COL_INFO, value=20)
note = ui.Label("มองหลอด " + led.name() + " บนบอร์ดคู่กับป้ายบนจอ", x=20,
                y=152, color=COL_DIM, value=20)

lcd.clear()
lcd.console("<h2>ไฟกะพริบและตัวนับรอบ</h2>")
lcd.print("หลอด", led.name(), "| รอบละ", ON_MS + OFF_MS, "ms")

rounds = 0

# ชุดที่ 1 - on/off สั่งค่าตรง ๆ อ่านโค้ดบรรทัดเดียวก็รู้ว่าไฟจะเป็นอะไร
for k in range(ROUNDS):
    led.on()
    state.text("ติด")
    state.color(COL_WARN)
    ui.poll()
    time.sleep_ms(ON_MS)

    led.off()
    state.text("ดับ")
    state.color(COL_DIM)
    rounds = rounds + 1
    seg.text(str(rounds))
    ui.poll()
    time.sleep_ms(OFF_MS)

lcd.print("<span class=ok>on/off ครบ", rounds, "รอบ</span>")

# ชุดที่ 2 - toggle() สั่งกลับด้านจากค่าเดิม ได้จังหวะเดียวกันด้วยคำสั่งเดียว
# ราคาที่จ่ายคือโปรแกรมต้องจำเองว่าตอนนี้ไฟอยู่สถานะไหน ป้ายบนจอจึงจะตรงกับหลอด
how.text("led.toggle()")
on = False

for k in range(ROUNDS * 2):
    led.toggle()
    on = not on

    if on:
        state.text("ติด")
        state.color(COL_WARN)
    else:
        state.text("ดับ")
        state.color(COL_DIM)
        rounds = rounds + 1
        seg.text(str(rounds))

    ui.poll()
    time.sleep_ms(ON_MS)

# ปิดท้ายด้วยค่าที่แน่นอน ไม่พึ่งความบังเอิญว่าจำนวน toggle เป็นเลขคู่พอดี
led.off()
state.text("ดับ")
state.color(COL_DIM)
note.text("จบแล้ว กะพริบไป " + str(rounds) + " รอบ ปิดท้ายด้วย off()")
lcd.print("<span class=ok>รวม", rounds, "รอบ - จบด้วย off()</span>")
ui.poll()
