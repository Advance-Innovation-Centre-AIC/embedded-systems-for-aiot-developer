# part1/ex06_hw_led_control.py - port ของ part1_ex6_hw_led_control (part1_hw_examples.c:202)
#
# หน้าจอ C : UI เดียวกับ ex03 ทุกจุด แต่คุมของจริง - ปุ่ม ON/OFF สั่ง LED เขียวจริง
#            และ slider หรี่ LED น้ำเงินจริงด้วย PWM
# กลไก MPY : gpio.led(n).on()/.off() และ .brightness(pct)
# ต่างจาก C : เลือกดวงตามชื่อจาก board_info() - Dev Kit ใช้ RGB_GREEN/RGB_BLUE,
#            Eva ใช้ led(1)=เขียว led(2)=น้ำเงิน (ชื่อบนบอร์ดคือ RGB_RED แต่ดวงจริงสีน้ำเงิน)

import time
import ui
import lcd

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
GREEN, RED = 0x4CAF50, 0xF44336


def cx(s, fs):
    return CX - (len(s) * fs) // 4


# ==== BOARD: TESAIoT Dev Kit (ล้อตามเมนู GPIO & RGB จริง) ====
# RGB LED หนึ่งดวงบน SoM: index ตายตัวจาก modgpio (2=แดง P20.6, 3=น้ำเงิน P20.5,
# 4=เขียว P20.4) — LED1/LED2 ในตาราง firmware สั่งได้แต่มองไม่เห็นบนบอร์ดประกอบ
import gpio
hw_red = gpio.led(2)
hw_blue = gpio.led(3)
hw_green = gpio.led(4)
# ==== END BOARD ====


ui.screen()
time.sleep_ms(200)

ui.Panel(x=0, y=0, w=W, h=H, color=0x0F0F23, min=0x0F0F23, max=0, value=0)

t = "Part 1 Ex6: HW LED Control (Based on Ex3)"
ui.Label(t, x=cx(t, 14), y=17, color=0xFFFFFF, value=14)

led = ui.Led(x=CX - 40, y=101, w=80, h=80, color=GREEN, value=1)
led.prop(ui.PROP_LED_BRIGHTNESS, 150)

bright = ui.Label("Brightness: 150", x=cx("Brightness: 150", 14), y=191,
                  color=0xFFFFFF, value=14)

b_on = ui.Button("ON", x=CX - 60 - 45, y=217, w=90, h=48, color=GREEN,
                 value=14)
b_off = ui.Button("OFF", x=CX + 60 - 45, y=217, w=90, h=48, color=RED,
                  value=14)

sld = ui.Slider(x=CX - 100, y=282, w=200, h=22, min=0, max=255, value=150)
pct = ui.Label("59%", x=cx("59%", 14), y=306, color=0xFFFFFF, value=14)

t = "[Part II] gpio.led().on/off + gpio.led().brightness()"
ui.Label(t, x=cx(t, 14), y=352, color=0xAAAAAA, value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=374, color=0x666666, value=14)

id_on, id_off, id_sld = b_on.id(), b_off.id(), sld.id()
lcd.print("ex06: ON/OFF -> real green LED, slider -> real blue LED PWM")


# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
            break
        if ev["type"] == "clicked" and ev["handle"] == id_on:
            led.prop(ui.PROP_LED_BRIGHTNESS, 255)
            bright.text("Brightness: 255 (ON)")
            hw_green.on()
        elif ev["type"] == "clicked" and ev["handle"] == id_off:
            led.prop(ui.PROP_LED_BRIGHTNESS, 0)
            bright.text("Brightness: 0 (OFF)")
            hw_green.off()
        elif ev["type"] == "value_changed" and ev["handle"] == id_sld:
            v = ev["value"]
            p = (v * 100) // 255
            led.prop(ui.PROP_LED_BRIGHTNESS, v)
            bright.text("Brightness: " + str(v))
            pct.text(str(p) + "%")
            hw_blue.brightness(p)
    time.sleep_ms(50)

print("ex06_hw_led_control: done")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
