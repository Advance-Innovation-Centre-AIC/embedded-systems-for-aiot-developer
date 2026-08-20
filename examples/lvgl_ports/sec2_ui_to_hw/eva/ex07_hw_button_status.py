# part1/ex07_hw_button_status.py - port ของ part1_ex7_hw_button_status (part1_hw_examples.c:343)
#
# หน้าจอ C : คอลัมน์ปุ่มสมมาตร - LED 70x70 ต่อปุ่ม พร้อมชื่อปุ่ม และสถานะ
#            "PRESSED" (เขียว) / "Released" (แดงอ่อน)
# กลไก C   : timer 50ms อ่านปุ่ม
# ต่อบอร์ด : BOARD block นิยามรายการปุ่ม (ชื่อ, ตัวอ่าน, สี) - โค้ดกลางวาดตามจำนวน

import time
import ui
import lcd
import gpio

# ==== BOARD: Eva Kit — สองปุ่ม: USER Button 1 + CapSense BTN0 (SW4 ของ Eva
# มีบนเมนู Controls แต่ modgpio ยังเอื้อมไม่ถึง — ใช้ CapSense แทน) ====
import sensors as _s

_b1 = gpio.button(0)


def _cap0():
    return _s.capsense.buttons()[0]


BTNS = (
    ("USER Button 1", _b1.is_pressed, 0x00BCD4),
    ("CapSense BTN0", _cap0, 0xFF9800),
)
# ==== END BOARD ====

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
OK, BAD = 0x00FF00, 0xFF6666


def cx(s, fs):
    return CX - (len(s) * fs) // 4


ui.screen()
time.sleep_ms(200)

ui.Panel(x=0, y=0, w=W, h=H, color=0x16213E, min=0x16213E, max=0, value=0)

t = "Part 1 Ex7: Hardware Button Status"
ui.Label(t, x=cx(t, 14), y=17, color=0xFFFFFF, value=14)

# จัดคอลัมน์กลางจอตามจำนวนปุ่ม (ระยะห่างคอลัมน์ 180px)
n = len(BTNS)
leds, sts, readers, prev = [], [], [], []
for i, (name, rd, color) in enumerate(BTNS):
    x = CX + (i * 2 - (n - 1)) * 90
    leds.append(ui.Led(x=x - 35, y=139, w=70, h=70, color=color, value=0))
    ui.Label(name, x=x - (len(name) * 14) // 4, y=225, color=0xFFFFFF,
             value=14)
    sts.append(ui.Label("Released", x=x - 28, y=255, color=BAD, value=16))
    readers.append(rd)
    prev.append(None)

t = "[Part II] Press the buttons on the board"
ui.Label(t, x=cx(t, 14), y=336, color=0xAAAAAA, value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=374, color=0x666666, value=14)

lcd.print("ex07: watching " + str(n) + " hardware buttons")

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
    for i in range(n):
        try:
            b = readers[i]()
        except OSError:
            b = False               # ตัวอ่านสะดุดหนึ่งจังหวะ = ยังไม่กด
        if b != prev[i]:
            prev[i] = b
            leds[i].value(1 if b else 0)
            sts[i].text("PRESSED" if b else "Released")
            sts[i].color(OK if b else BAD)
    time.sleep_ms(50)

print("ex07_hw_button_status: done")
