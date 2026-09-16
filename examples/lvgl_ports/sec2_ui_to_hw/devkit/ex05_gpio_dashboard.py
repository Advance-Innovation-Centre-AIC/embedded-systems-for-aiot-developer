# part1/ex05_gpio_dashboard.py - port ของ part1_ex5_gpio_dashboard (part1_examples.c:471)
#
# หน้าจอ C : พื้น 0x1a1a2e, กล่อง 420x200 (พื้น 0x0f0f23 ขอบ 0x444444 หนา 2)
#            กลางจอ ภายในเป็นกริด 2x2 ของ {LED 40x40 + ชื่อ + switch 60x30}
#            สี แดง/เขียว/น้ำเงิน/เหลือง, ปุ่ม All ON ล่างซ้าย / All OFF ล่างขวา
# กลไก     : switch คุม LED รายตัว ปุ่มคุมทั้งชุด
# ต่างจาก C : switch ของ MPY สั่งติด/ดับจากโค้ดไม่ได้ (ไม่มี setter) - All ON/OFF
#            จึงคุมเฉพาะ LED ส่วน switch ค้างตำแหน่งของผู้ใช้ (บันทึกใน ledger)
# โบนัส Dev Kit: สถานะ 4 ช่องถูกสะท้อนขึ้น RGB Matrix แถวบน (ไม่กระทบหน้าจอ)

import time
import ui
import lcd

# ==== BOARD: TESAIoT Dev Kit — RGB Matrix 16x8 (DFR0522 @0x10) ====
import rgbmatrix
HAS_RGB = True
# ==== END BOARD ====

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
GREEN, RED = 0x4CAF50, 0xF44336
NAMES = ("LED1", "LED2", "LED3", "LED4")
COLORS = (0xF44336, 0x4CAF50, 0x2196F3, 0xFFEB3B)   # lv_palette RED/GREEN/BLUE/YELLOW
RGB_COLORS = (1, 2, 4, 3)                            # rgbmatrix RED/GREEN/BLUE/YELLOW


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



def apply_hw():
    # RGB ดวงเดียว: Yellow = R+G - รวมสถานะทุกช่องก่อนขับขาจริง
    r = state[0] or state[3]
    g = state[1] or state[3]
    if r:
        hw_red.on()
    else:
        hw_red.off()
    if g:
        hw_green.on()
    else:
        hw_green.off()
    if state[2]:
        hw_blue.on()
    else:
        hw_blue.off()


ui.screen()
time.sleep_ms(200)

ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)

t = "Part 1 - Example 5: GPIO Dashboard"
ui.Label(t, x=cx(t, 14), y=8, color=0xFFFFFF, value=14)

# กล่องรวม 420x200 ของ C หดแกนตั้งเป็น 170 ที่ CENTER(0,+10)
BX, BY = CX - 210, 199 + 8 - 85
ui.Panel(x=BX, y=BY, w=420, h=170, color=0x0F0F23, min=0x444444, max=0,
         value=2)

leds = []
sw_ids = []
state = [False, False, False, False]
for i in range(4):
    x = BX + 20 + (i % 2) * 200
    y = BY + 14 + (i // 2) * 66
    led = ui.Led(x=x, y=y, w=40, h=40, color=COLORS[i], value=0)
    ui.Label(NAMES[i], x=x + 50, y=y + 12, color=0xFFFFFF, value=14)
    sw = ui.Switch(x=x + 110, y=y + 2, w=70, h=36)
    leds.append(led)
    sw_ids.append(sw.id())

b_on = ui.Button("All ON", x=40, y=294, w=110, h=48, color=GREEN, value=14)
b_off = ui.Button("All OFF", x=W - 40 - 110, y=294, w=110, h=48, color=RED,
                  value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=374, color=0x666666, value=14)

id_on, id_off = b_on.id(), b_off.id()
lcd.print("ex05: four virtual GPIO channels - switches + All ON / All OFF")


def show(i, on):
    state[i] = on
    leds[i].value(1 if on else 0)
    if HAS_RGB:
        # ดวงละ 3 คอลัมน์บนแถวบนของ RGB Matrix ให้กวาดตาเห็นสถานะจากบอร์ด
        c = RGB_COLORS[i] if on else 0
        for px in range(3):
            rgbmatrix.pixel(i * 4 + px, 0, c)
    apply_hw()
    print(NAMES[i] + (": ON" if on else ": OFF"))



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
        h = ev["handle"]
        if ev["type"] == "toggled" and h in sw_ids:
            show(sw_ids.index(h), bool(ev["value"]))
        elif ev["type"] == "clicked" and h == id_on:
            for i in range(4):
                show(i, True)
            print("All LEDs: ON")
        elif ev["type"] == "clicked" and h == id_off:
            for i in range(4):
                show(i, False)
            print("All LEDs: OFF")
    time.sleep_ms(50)

print("ex05_gpio_dashboard: done")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
