import time
import math
import ui
import lcd
import sensors
import dsp

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 240000


ui.screen()
time.sleep_ms(200)

# sec3/ex11 - port ของ part2_ex11_real_chart_dashboard (part2_hw_examples.c:1411)
# 5 แท็บ Bar/Area/Scatter/Line/Mag - เข็มทิศใช้ ui.Compass (ใกล้กว่า scale หมุนของ C)
# C อัปเดตเฉพาะแท็บ active; MPY อ่านแท็บ active กลับไม่ได้ -> อัปเดตทุกแท็บ (ledger)
tv = ui.Tabview(x=0, y=0, w=W, h=H)
tabs = [tv.add_tab(n) for n in ("Bar", "Area", "Scat", "Line", "Mag")]
for tab in tabs:
    tab.color(0xF5F5F5)
tab_bar, tab_area, tab_sca, tab_lin, tab_mag = tabs

ui.Label("Bar: Real Accel X/Y/Z", x=210, y=6, color=0x111111, value=20,
         parent=tab_bar)
bars = []
for i in range(3):
    bars.append(ui.Bar(x=170 + i * 160, y=60, w=60, h=180, min=0, max=200,
                       value=100, color=(0xF44336, 0x4CAF50, 0x2196F3)[i],
                       parent=tab_bar))
    ui.Label("XYZ"[i], x=192 + i * 160, y=250, color=0x111111, value=20,
             parent=tab_bar)

ui.Label("Area: Tilt Magnitude (deg)", x=190, y=4, color=0x111111, value=20,
         parent=tab_area)
area_ch = ui.Chart(x=70, y=36, w=390, h=200, min=0, max=90, color=0xFF9800,
                   parent=tab_area)
tilt_l = ui.Label("Tilt: 0.0 deg", x=250, y=246, color=0x111111, value=20,
                  parent=tab_area)

ui.Label("Scatter: Roll vs Pitch (Tilt Ball)", x=160, y=4, color=0x111111,
         value=20, parent=tab_sca)
ui.Panel(x=140, y=36, w=290, h=210, color=0xFFFFFF, min=0x888888, max=4,
         value=2, parent=tab_sca)
dot = ui.Panel(x=278, y=134, w=14, h=14, color=0xE91E63, min=0xE91E63,
               max=7, value=0, parent=tab_sca)
sc_l = ui.Label("X: 0.00  Y: 0.00", x=200, y=252, color=0x111111, value=14,
                parent=tab_sca)

ui.Label("Line: Real Gyro R/P/Y", x=210, y=4, color=0x111111, value=20,
         parent=tab_lin)
lin_ch = ui.Chart(x=70, y=36, w=390, h=228, min=-1000, max=1000,
                  color=0xF44336, parent=tab_lin)
lin_sy = lin_ch.add_series(0x4CAF50)
lin_sz = lin_ch.add_series(0x2196F3)

cmp_w = ui.Compass(x=180, y=30, w=200, h=200, value=0, parent=tab_mag)
head_l = ui.Label("0 N", x=420, y=70, color=0x111111, value=24,
                  parent=tab_mag)
xyz_l = ui.Label("X:0 Y:0 Z:0", x=410, y=120, color=0x555555, value=14,
                 parent=tab_mag)
st_l = ui.Label("BMM350: Init...", x=410, y=150, color=0x555555, value=14,
                parent=tab_mag)

ui.Label(FOOTER, x=180, y=372, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

DIRS = ("N", "NE", "E", "SE", "S", "SW", "W", "NW")
lcd.print("sec3 ex11: five real-data chart styles incl BMM350 compass")
t0 = time.ticks_ms()
n = 0
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
    n += 1
    ax, ay, az = sensors.bmi270.acceleration()
    gx, gy, gz = sensors.bmi270.gyroscope()
    roll, pitch = dsp.tilt(ax, ay, az)

    for b, v in zip(bars, (ax, ay, az)):
        b.value(int(100 + v * 10))
    area_ch.set_next(0, int(math.sqrt(roll * roll + pitch * pitch)))
    tilt_l.text("Tilt: %.1f deg" % math.sqrt(roll * roll + pitch * pitch))
    px = int(278 + (roll / 90) * 130)
    py = int(134 - (pitch / 90) * 95)
    px = 148 if px < 148 else (408 if px > 408 else px)
    py = 44 if py < 44 else (228 if py > 228 else py)
    dot.pos(px, py)
    sc_l.text("X: %.2f  Y: %.2f" % (roll, pitch))
    lin_ch.set_next(0, int(gx * 10))
    lin_ch.set_next(lin_sy, int(gy * 10))
    lin_ch.set_next(lin_sz, int(gz * 10))

    if n % 5 == 0:                    # เข็มทิศ 2Hz พอ (BMM350 ช้ากว่า IMU)
        try:
            hd = sensors.bmm350.heading()
            mx, my, mz = sensors.bmm350.magnetic()
            cmp_w.value(int(hd))
            head_l.text("%d %s" % (int(hd), DIRS[int(((hd + 22.5) % 360) // 45)]))
            xyz_l.text("X:%d Y:%d Z:%d" % (mx, my, mz))
            st_l.text("BMM350: OK")
        except Exception:
            st_l.text("BMM350: no data")
    time.sleep_ms(100)
print("sec3 ex11: done")
