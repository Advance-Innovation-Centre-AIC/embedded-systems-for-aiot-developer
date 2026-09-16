# part1/ex09_hw_gpio_dashboard.py - port ของ part1_ex9_hw_gpio_dashboard (part1_hw_examples.c:675)
#
# หน้าจอ C : คอลัมน์ LED แนวตั้งสามแถว (Red/Green สลับด้วย switch, Blue คุมด้วย
#            pot) + แผงล่างซ้าย "USER BTN2" (LED ส้ม 60 + สถานะ) + แผงล่างขวา
#            "POT -> Blue LED" (bar น้ำเงิน + % ตัวโต) + ปุ่ม All ON/OFF กลาง
# กลไก MPY : gpio.led จริง, pot จริง (pots/sensors.pot), ปุ่มที่สองตามบอร์ด
# โบนัส Dev Kit: สถานะ LED สะท้อนขึ้น RGB Matrix แถวบน

import time
import ui
import lcd

# ==== BOARD: Eva Kit — pot เดี่ยว P15.1 (เมนู Controls ใช้ตัวเดียวกัน) ====
import sensors

def pot_pct():
    return int(sensors.pot.percent())
# ==== END BOARD ====


# ==== BOARD: Eva Kit — สองปุ่ม: USER Button 1 + CapSense BTN0 ====
import gpio as _g
import sensors as _s

_b1 = _g.button(0)


def _cap0():
    return _s.capsense.buttons()[0]


BTNS = (
    ("USR BTN1", _b1.is_pressed),
    ("CAP BTN0", _cap0),
)
# ==== END BOARD ====

# ==== BOARD: Eva Kit — ไม่มี RGB Matrix ====
HAS_RGB = False
# ==== END BOARD ====

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
RED, GREEN, BLUE = 0xF44336, 0x4CAF50, 0x2196F3
ORANGE, CYAN_B = 0xFF9800, 0x00AAFF
OK, BAD = 0x00FF00, 0xFF6666


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

hw = (hw_red, hw_green)

ui.screen()
time.sleep_ms(200)

ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)

t = "Part 1 Ex9: HW GPIO Dashboard"
ui.Label(t, x=cx(t, 14), y=7, color=0xFFFFFF, value=14)

# คอลัมน์ LED สามแถว (y ของ C: 65+50i -> 54+41i)
NAMES = ("Red", "Green")
COLS = (RED, GREEN)
leds, sw_ids, state = [], [], [False, False]
for i in range(2):
    y = 54 + i * 41
    led = ui.Led(x=CX - 122, y=y, w=45, h=45, color=COLS[i], value=0)
    ui.Label(NAMES[i], x=CX - 35 - len(NAMES[i]) * 3, y=y + 12,
             color=0xFFFFFF, value=14)
    sw = ui.Switch(x=CX + 45 - 35, y=y + 2, w=70, h=38)
    leds.append(led)
    sw_ids.append(sw.id())

BY = 54 + 2 * 41
led_blue = ui.Led(x=CX - 122, y=BY, w=45, h=45, color=BLUE, value=0)
ui.Label("Blue", x=CX - 35 - 12, y=BY + 4, color=0xFFFFFF, value=14)
ui.Label("(POT ctrl)", x=CX + 45 - 35, y=BY + 12, color=CYAN_B, value=14)

# แผงล่างซ้าย: สถานะปุ่มจริงทุกตัวของบอร์ด (C: 225x128 @BOTTOM_LEFT(5,-25))
ui.Panel(x=137, y=271, w=225, h=106, color=0x0F0F23, min=0x444444, max=0,
         value=2)
ui.Label("Buttons", x=249 - 21, y=277, color=0xFFFFFF, value=14)
_bn = len(BTNS)
btn_leds, btn_lbls, btn_prev = [], [], []
for _i, (_nm, _rd) in enumerate(BTNS):
    _cx = 137 + (225 // (_bn * 2)) * (_i * 2 + 1)
    btn_leds.append(ui.Led(x=_cx - 16, y=299, w=32, h=32, color=ORANGE,
                           value=0))
    btn_lbls.append(ui.Label(_nm, x=_cx - (len(_nm) * 14) // 4, y=338,
                             color=BAD, value=14))
    btn_prev.append(None)

# แผงล่างขวา: pot -> blue (C: 225x128 @BOTTOM_RIGHT(-5,-25))
ui.Panel(x=562, y=271, w=225, h=106, color=0x0F0F23, min=0x444444, max=0,
         value=2)
ui.Label("POT -> Blue LED", x=674 - 45, y=279, color=CYAN_B, value=14)
adc_bar = ui.Bar(x=584, y=305, w=180, h=21, min=0, max=100, value=0,
                 color=0x0088FF)
adc_l = ui.Label("0%", x=664, y=336, color=CYAN_B, value=24)

b_on = ui.Button("All ON", x=CX - 60 - 50, y=225, w=100, h=40, color=GREEN,
                 value=14)
b_off = ui.Button("All OFF", x=CX + 60 - 50, y=225, w=100, h=40, color=RED,
                  value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=380, color=0x666666, value=14)

id_on, id_off = b_on.id(), b_off.id()
lcd.print("ex09: switches -> real Red/Green, pot -> real Blue PWM, watch BTN2")


def set_ch(i, on):
    state[i] = on
    leds[i].value(1 if on else 0)
    if on:
        hw[i].on()
    else:
        hw[i].off()
    if HAS_RGB:
        for px in range(3):
            rgbmatrix.pixel(i * 4 + px, 0, (1 if i == 0 else 2) if on else 0)
    print("[HW] " + NAMES[i] + (": ON" if on else ": OFF"))


prev_p = -1

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
            set_ch(sw_ids.index(h), bool(ev["value"]))
        elif ev["type"] == "clicked" and h == id_on:
            set_ch(0, True)
            set_ch(1, True)
            led_blue.value(1)
            hw_blue.brightness(100)
        elif ev["type"] == "clicked" and h == id_off:
            set_ch(0, False)
            set_ch(1, False)
            led_blue.value(0)
            hw_blue.brightness(0)

    for _i, (_nm, _rd) in enumerate(BTNS):
        try:
            b = _rd()
        except OSError:
            b = False               # ตัวอ่านสะดุดหนึ่งจังหวะ = ยังไม่กด
        if b != btn_prev[_i]:
            btn_prev[_i] = b
            btn_leds[_i].value(1 if b else 0)
            btn_lbls[_i].color(OK if b else BAD)

    p = pot_pct()
    if abs(p - prev_p) > 1:
        prev_p = p
        adc_bar.value(p)
        adc_l.text(str(p) + "%")
        led_blue.value(1)
        led_blue.prop(ui.PROP_LED_BRIGHTNESS, (p * 255) // 100)
        hw_blue.brightness(p)
    time.sleep_ms(100)

print("ex09_hw_gpio_dashboard: done")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
