# part1/ex03_led_control.py - port ของ part1_ex3_led_control (part1_examples.c:249)
#
# หน้าจอ C : พื้น 0x0f0f23, LED เขียว 80x80 เหนือกลางจอ, บรรทัด "Brightness: n",
#            ปุ่ม ON (เขียว) / OFF (แดง) ใต้กลาง, slider 0-255 พร้อม % ใต้มัน,
#            คำอธิบาย, footer
# กลไก     : slider -> lv_led_set_brightness  ที่นี่คือ PROP_LED_BRIGHTNESS
# ต่างจาก C : ไม่มี - widget ครบทุกตัว

import time
import ui
import lcd

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
GREEN, RED = 0x4CAF50, 0xF44336        # lv_palette_main GREEN/RED


def cx(s, fs):
    return CX - (len(s) * fs) // 4


# ==== BOARD: Eva Kit (ล้อตามเมนู Controls จริง) ====
# LED แยก 3 ดวง: 0=แดง P16.7, 1=เขียว P16.6, 2=น้ำเงิน P16.5
# (ระวัง: ตารางชื่อใน firmware เรียก index 2 ว่า "RGB_RED" ทั้งที่ดวงจริงสีน้ำเงิน
#  — ที่นี่ใช้ index ตายตัวตามขาจริง จึงไม่โดนกับดักนั้น)
import gpio
hw_red = gpio.led(0)
hw_green = gpio.led(1)
hw_blue = gpio.led(2)
# ==== END BOARD ====

cur_pct = (150 * 100) // 255

ui.screen()
time.sleep_ms(200)

ui.Panel(x=0, y=0, w=W, h=H, color=0x0F0F23, min=0x0F0F23, max=0, value=0)

t = "Part 1 - Example 3: LED Widget"
ui.Label(t, x=cx(t, 14), y=17, color=0xFFFFFF, value=14)

# LED 80x80 ที่ CENTER(0,-70) ของ C -> y = 199 - 58 - 40
led = ui.Led(x=CX - 40, y=101, w=80, h=80, color=GREEN, value=1)
led.prop(ui.PROP_LED_BRIGHTNESS, 150)

bright = ui.Label("Brightness: 150", x=cx("Brightness: 150", 14), y=191,
                  color=0xFFFFFF, value=14)

# ปุ่ม ON/OFF ที่ CENTER(-60,+50) / (+60,+50) ของ C
b_on = ui.Button("ON", x=CX - 60 - 45, y=217, w=90, h=48, color=GREEN,
                 value=14)
b_off = ui.Button("OFF", x=CX + 60 - 45, y=217, w=90, h=48, color=RED,
                  value=14)

# slider w200 range 0-255 ค่าตั้งต้น 150 ที่ CENTER(0,+110) ของ C
sld = ui.Slider(x=CX - 100, y=282, w=200, h=22, min=0, max=255, value=150)
pct = ui.Label("59%", x=cx("59%", 14), y=306, color=0xFFFFFF, value=14)

t = "Learning: lv_led_create, lv_led_on/off, lv_led_set_brightness"
ui.Label(t, x=cx(t, 14), y=348, color=0xAAAAAA, value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=374, color=0x666666, value=14)

id_on, id_off, id_sld = b_on.id(), b_off.id(), sld.id()
lcd.print("ex03: ON/OFF buttons + slider drive the LED widget brightness")


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
        print("EV:", ev)
        if ev["type"] == "clicked" and ev["handle"] == id_on:
            led.prop(ui.PROP_LED_BRIGHTNESS, 255)
            bright.text("Brightness: 255 (ON)")
            cur_pct = 100
            hw_green.brightness(100)
        elif ev["type"] == "clicked" and ev["handle"] == id_off:
            led.prop(ui.PROP_LED_BRIGHTNESS, 0)
            bright.text("Brightness: 0 (OFF)")
            cur_pct = 0
            hw_green.brightness(0)
        elif ev["type"] == "value_changed" and ev["handle"] == id_sld:
            v = ev["value"]
            led.prop(ui.PROP_LED_BRIGHTNESS, v)
            bright.text("Brightness: " + str(v))
            pct.text(str((v * 100) // 255) + "%")
            cur_pct = (v * 100) // 255
    # ไม่มี PWM ฮาร์ดแวร์ - หรี่ค้างต้องยิง burst ซ้ำทุกรอบ (12ms/ครั้ง)
    if 0 < cur_pct < 100:
        hw_green.brightness(cur_pct)
    time.sleep_ms(50)

print("ex03_led_control: done")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
