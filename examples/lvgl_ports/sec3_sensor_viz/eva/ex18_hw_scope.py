import math
import time
import ui
import lcd

# ==== BOARD: Eva Kit — pot เดี่ยว P15.1, LED น้ำเงิน P16.5 (PWM จริง) ====
import sensors
import gpio


def pot_pct():
    return ((sensors.pot.read() >> 4) * 100) // 4095  # 0-65535 -> %


hw_led = gpio.led(2)  # น้ำเงิน P16.5 จริง (ตารางชื่อ firmware หลอกว่า RGB_RED)
POT_NAME = "POTEN"
# ==== END BOARD ====

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 180000
N = 200
GEN_SR = 10000  # ตาม C: rate ต่ำให้เห็น duty ชัด


def cx(s, fs):
    return CX - (len(s) * fs) // 4


def push_square(ch, freq, duty, phase, k=8, sr=GEN_SR):
    # เส้นวิ่งต่อเนื่อง: duty จาก pot มีผลกับจุดใหม่ทันที
    per = sr / freq
    for i in range(k):
        ph = ((phase + i) % per) / per
        ch.set_next(0, 70 if ph < duty / 100 else 30)
    return phase + k


ui.screen()
time.sleep_ms(200)

# sec3/ex18 - port ของ part3_hw_scope_example.c (Hardware Integrated Scope)
# ของจริงตามกติกา sec3: pot จริงคุม duty, สวิตช์ OUTPUT ขับ LED จริงด้วย
# PWM (brightness) - C ใช้ POTEN + LED3 PWM แบบเดียวกัน
ui.Panel(x=0, y=0, w=W, h=H, color=0x0A0A0A, min=0x0A0A0A, max=0, value=0)
t = "Part 3 - HW Scope: Function Generator"
ui.Label(t, x=cx(t, 16), y=8, color=0x00FF88, value=16)

ch = ui.Chart(x=16, y=44, w=594, h=238, min=0, max=100, color=0x00FF00)
ch.prop(ui.PROP_CHART_POINTS, N)

ui.Panel(x=620, y=44, w=158, h=238, color=0x1A1A1A, min=0x333333, max=6,
         value=1)
ui.Label("OUTPUT", x=648, y=56, color=0xCCCCCC, value=14)
out_sw = ui.Switch(x=644, y=82, w=80, h=40)
out_led = ui.Led(x=740, y=88, w=26, h=26, color=0x2196F3, value=80)
duty_l = ui.Label("Duty: --%", x=636, y=150, color=0xFFFF00, value=14)
ui.Label(POT_NAME + " = duty", x=636, y=176, color=0x888888, value=14)
freq_l = ui.Label("100 Hz", x=648, y=222, color=0xFFFFFF, value=14)

ui.Label("Freq (10-500 Hz)", x=16, y=300, color=0xCCCCCC, value=14)
fsld = ui.Slider(x=210, y=298, w=330, h=22, min=10, max=500, value=100)
t = "Turn " + POT_NAME + " - the duty and the real LED follow"
ui.Label(t, x=cx(t, 14), y=336, color=0x888888, value=14)
ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

freq, duty, out_on = 100, 50, False
phase = 0

lcd.print("sec3 ex18: เส้นวิ่งตลอด - pot คุม duty, OUTPUT ขับ LED จริง")
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
        elif ev["type"] == "toggled" and ev["handle"] == out_sw.id():
            out_on = ev["value"] == 1
            if out_on:
                hw_led.brightness(duty)
            else:
                hw_led.brightness(0)
                hw_led.off()
            out_led.prop(ui.PROP_LED_BRIGHTNESS, 255 if out_on else 80)
        elif ev["type"] == "value_changed" and ev["handle"] == fsld.id():
            freq = ev["value"]
            freq_l.text(str(freq) + " Hz")
    d = pot_pct()
    if abs(d - duty) > 2:
        duty = d
        duty_l.text("Duty: " + str(duty) + "%")
        if out_on:
            hw_led.brightness(duty)  # PWM จริง - หรี่ตาม duty
    phase = push_square(ch, freq, duty, phase)
    time.sleep_ms(100)
hw_led.brightness(0)
hw_led.off()
print("sec3 ex18: done")
