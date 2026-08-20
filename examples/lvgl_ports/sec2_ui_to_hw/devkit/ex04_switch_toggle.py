# part1/ex04_switch_toggle.py - port ของ part1_ex4_switch_toggle (part1_examples.c:352)
#
# หน้าจอ C : พื้น 0x16213e, LED เหลือง 60x60 เหนือกลาง + "Virtual LED",
#            switch 80x40 ใต้กลาง, บรรทัดสถานะสีเขียว, คำอธิบายสองบรรทัด, footer
# กลไก     : lv_obj_has_state(CHECKED) -> ที่นี่คือ event 'toggled' value 0/1

import time
import ui
import lcd

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
YELLOW = 0xFFEB3B                       # lv_palette_main YELLOW


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

ui.Panel(x=0, y=0, w=W, h=H, color=0x16213E, min=0x16213E, max=0, value=0)

t = "Part 1 - Example 4: Switch Control"
ui.Label(t, x=cx(t, 14), y=17, color=0xFFFFFF, value=14)

# LED 60x60 ที่ CENTER(0,-60) ของ C -> y = 199 - 50 - 30
led = ui.Led(x=CX - 30, y=119, w=60, h=60, color=YELLOW, value=0)
t = "Virtual LED"
ui.Label(t, x=cx(t, 14), y=189, color=0xFFFFFF, value=14)

# switch 80x40 ที่ CENTER(0,+40) ของ C
sw = ui.Switch(x=CX - 40, y=212, w=80, h=40)

status = ui.Label("GPIO State: LOW (OFF)", x=cx("GPIO State: LOW (OFF)", 14),
                  y=274, color=0x00FF00, value=14)

t = "Learning: lv_switch_create, LV_STATE_CHECKED"
ui.Label(t, x=cx(t, 14), y=322, color=0xAAAAAA, value=14)
t = "This switch would control a real GPIO in actual hardware"
ui.Label(t, x=cx(t, 14), y=340, color=0xAAAAAA, value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=374, color=0x666666, value=14)

sw_id = sw.id()
lcd.print("ex04: toggle the switch - virtual LED follows")


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
        if ev["type"] == "toggled" and ev["handle"] == sw_id:
            if ev["value"]:
                led.value(1)
                hw_red.on()      # แดง+เขียว = เหลืองบน RGB ดวงเดียว
                hw_green.on()
                status.text("GPIO State: HIGH (ON)")
                print("Switch ON - GPIO would be HIGH")
            else:
                led.value(0)
                hw_red.off()
                hw_green.off()
                status.text("GPIO State: LOW (OFF)")
                print("Switch OFF - GPIO would be LOW")
    time.sleep_ms(50)

print("ex04_switch_toggle: done")
