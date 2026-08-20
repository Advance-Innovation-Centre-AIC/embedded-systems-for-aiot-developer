# part1/ex11_capsense_hardware.py - port ของ part1_ex11_capsense_hardware (part1_hw_examples.c:1258)
#
# หน้าจอ C : layout เดียวกับ ex10 (แผงเลื่อนสูงขึ้น, ปุ่มลึกลง) + บรรทัดสถานะ
#            การเชื่อมต่อ I2C, slider อ่านอย่างเดียว, อ่าน CAPSENSE จริง 40Hz
#            แล้วขับ LED จริง: BTN0->แดง BTN1->เขียว slider->น้ำเงิน (PWM)
# กลไก MPY : sensors.capsense.read() - Dev Kit อ่านผ่าน IPC snapshot ของ CM55,
#            Eva อ่าน I2C ตรง (API เดียวกัน) - คาบ 50ms (C ใช้ 25ms)

import time
import ui
import lcd
import sensors

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
PANEL, PANEL_HIT, BORDER = 0x333355, 0x00AA00, 0x666699
GREY, OK, LBLUE = 0x888888, 0x00FF00, 0x00AAFF


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


try:
    sensors.capsense.read()
    CONN = "I2C: Connected (addr 0x08)"
    CONN_C = 0x00FF00
    HAS_CS = True
except Exception:
    CONN = "I2C: CAPSENSE not responding"
    CONN_C = 0xFF4444
    HAS_CS = False

ui.screen()
time.sleep_ms(200)

ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)

t = "Part 1 Ex11: CAPSENSE Hardware"
ui.Label(t, x=cx(t, 14), y=7, color=0xFFFFFF, value=14)
ui.Label(CONN, x=cx(CONN, 14), y=25, color=CONN_C, value=14)

# แผง slider ที่ TOP_MID+55 -> +46
SX, SY = CX - 210, 46
ui.Panel(x=SX, y=SY, w=420, h=66, color=0x0F0F23, min=0x0F0F23, max=0,
         value=0)
ui.Label("SLIDER (CSS1)", x=SX + 10, y=SY + 6, color=0xFFFFFF, value=14)
sld_val = ui.Label("0%", x=SX + 330, y=SY + 4, color=0xFFFFFF, value=16)
sld = ui.Slider(x=SX + 10, y=SY + 34, w=340, h=21, min=0, max=100, value=0,
                color=LBLUE)
out_led = ui.Led(x=SX + 375, y=SY + 30, w=25, h=25, color=LBLUE, value=0)

# แผงปุ่มสองใบ ที่ BOTTOM_MID(+-110,-55) -> y=227
leds, sts = [], []
for i, bx in enumerate((CX - 110 - 70, CX + 110 - 70)):
    ui.Panel(x=bx, y=227, w=140, h=125, color=PANEL, min=BORDER, max=10,
             value=3)
    ui.Label("BTN" + str(i), x=bx + 70 - 14, y=235, color=0xFFFFFF, value=16)
    ui.Label("(CSB" + str(i + 1) + ")", x=bx + 70 - 18, y=255,
             color=0xAAAAAA, value=14)
    led = ui.Led(x=bx + 45, y=275, w=50, h=50, color=(0xF44336, 0x4CAF50)[i],
                 value=0)
    st = ui.Label("Ready", x=bx + 70 - 17, y=331, color=GREY, value=14)
    leds.append(led)
    sts.append(st)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=380, color=0x666666, value=14)

lcd.print("ex11: touch the CAPSENSE pads on the board - watch LEDs follow")

prev = (None, None)
prev_s = -1
hw_fns = (hw_red, hw_green)

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
    if HAS_CS:
        d = sensors.capsense.read()
        btns = (d["btn0"], d["btn1"])
        for i in range(2):
            if btns[i] != prev[i]:
                on = btns[i]
                leds[i].value(1 if on else 0)
                sts[i].text("TOUCHED" if on else "Ready")
                sts[i].color(OK if on else GREY)
                if on:
                    hw_fns[i].on()
                else:
                    hw_fns[i].off()
                print("[HW] BTN" + str(i) + (": TOUCHED" if on else ": Released"))
        prev = btns
        s = d["slider"]
        if s != prev_s:
            prev_s = s
            sld.value(s)
            sld_val.text(str(s) + "%")
            out_led.value(1 if s else 0)
            out_led.prop(ui.PROP_LED_BRIGHTNESS, (s * 255) // 100)
            hw_blue.brightness(s)
    time.sleep_ms(50)

print("ex11_capsense_hardware: done")
