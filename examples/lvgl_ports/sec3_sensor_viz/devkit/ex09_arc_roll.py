import time
import ui
import lcd
import sensors

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000


def cx(s, fs):
    return CX - (len(s) * fs) // 4


ui.screen()
time.sleep_ms(200)
import dsp

# sec3/ex09 - port ของ part2_ex9_real_arc_gauge (part2_hw_examples.c:530)
# "ท่าบังคับคาบ 6" ตาม SHARED_BRIEF - roll จาก dsp.tilt (C ใช้ complementary filter)
ui.Panel(x=0, y=0, w=W, h=H, color=0x0F0F23, min=0x0F0F23, max=0, value=0)
t = "Part 2 Ex9: Arc Gauge (Roll Angle)"
ui.Label(t, x=cx(t, 14), y=8, color=0xFFFFFF, value=14)

arc = ui.Arc(x=CX - 100, y=95, w=200, h=200, min=0, max=100, value=50,
             color=0x00BCD4)
val_l = ui.Label("50%", x=cx("50%", 24), y=183, color=0xFFFFFF, value=24)
roll_l = ui.Label("Roll: 0.0 deg", x=cx("Roll: 0.0 deg", 14), y=310,
                  color=0x00FF00, value=14)
t = "[Part II] Roll from dsp.tilt (BMI270 accel)"
ui.Label(t, x=cx(t, 14), y=348, color=0xAAAAAA, value=14)
ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

lcd.print("sec3 ex09: tilt the board - roll angle on the arc")
t0 = time.ticks_ms()
prev = None
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
    ax, ay, az = sensors.bmi270.acceleration()
    roll, pitch = dsp.tilt(ax, ay, az)
    v = int(50 + roll * 50 / 90)
    v = 0 if v < 0 else (100 if v > 100 else v)
    if v != prev:
        prev = v
        arc.value(v)
        val_l.text(str(v) + "%")
        roll_l.text("Roll: %.1f deg" % roll)
    time.sleep_ms(100)
print("sec3 ex09: done")
