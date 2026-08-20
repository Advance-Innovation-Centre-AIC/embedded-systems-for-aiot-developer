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

# sec3/ex07 - port ของ part2_ex7_real_imu_display (part2_hw_examples.c:187)
# layout เดียวกับ ex03 - ต่างที่ป้าย "REAL BMI270 DATA" ตาม C (ของเราจริงทั้งคู่)
ui.Panel(x=0, y=0, w=W, h=H, color=0x16213E, min=0x16213E, max=0, value=0)
t = "Part 2 Ex7: Real IMU (Based on Ex3)"
ui.Label(t, x=cx(t, 14), y=8, color=0xFFFFFF, value=14)

ch = ui.Chart(x=166, y=99, w=440, h=200, min=-200, max=1200, color=0xF44336)
s_y = ch.add_series(0x4CAF50)
s_z = ch.add_series(0x2196F3)
lab_x = ui.Label("X: +0.00", x=636, y=142, color=0xF44336, value=14)
lab_y = ui.Label("Y: +0.00", x=636, y=172, color=0x4CAF50, value=14)
lab_z = ui.Label("Z: +0.00", x=636, y=202, color=0x2196F3, value=14)
t = "Accelerometer (m/s^2) - REAL BMI270 DATA"
ui.Label(t, x=cx(t, 14), y=312, color=0xCCCCCC, value=14)
t = "[Part II] Using real BMI270 over sensors.bmi270"
ui.Label(t, x=cx(t, 14), y=348, color=0xAAAAAA, value=14)
ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

lcd.print("sec3 ex07: REAL BMI270 accel chart")
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
    ax, ay, az = sensors.bmi270.acceleration()
    ch.set_next(0, int(ax * 100))
    ch.set_next(s_y, int(ay * 100))
    ch.set_next(s_z, int(az * 100))
    lab_x.text("X: %+.2f" % ax)
    lab_y.text("Y: %+.2f" % ay)
    lab_z.text("Z: %+.2f" % az)
    time.sleep_ms(100)
print("sec3 ex07: done")
