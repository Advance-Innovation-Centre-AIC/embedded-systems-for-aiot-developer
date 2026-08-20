import time
import ui
import lcd
import sensors

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 180000


ui.screen()
time.sleep_ms(200)

# sec3/ex06 - port ของ part2_ex6_chart_dashboard (part2_examples.c:1124)
# C: TabView ซ้าย 4 แท็บ Bar/Area/Scatter/Line - MPY: แถบแท็บอยู่บน (delta ใน
# ledger), Bar = ui.Bar แนวตั้ง 3 แท่ง (Chart ของ firmware เป็น LINE เท่านั้น),
# Scatter = จุดเคลื่อนที่ roll/pitch จาก accel จริง
tv = ui.Tabview(x=0, y=0, w=W, h=H)
tab_bar = tv.add_tab("Bar")
tab_area = tv.add_tab("Area")
tab_sca = tv.add_tab("Scatter")
tab_lin = tv.add_tab("Line")
for tab in (tab_bar, tab_area, tab_sca, tab_lin):
    tab.color(0xF5F5F5)

# --- Bar tab: ax/ay/az เป็นแท่งแนวตั้ง (แทน chart TYPE_BAR) ---
ui.Label("Accel snapshot (m/s^2)", x=200, y=6, color=0x111111, value=20,
         parent=tab_bar)
bars = []
for i in range(3):
    bars.append(ui.Bar(x=170 + i * 160, y=60, w=60, h=180, min=0, max=200,
                       value=100, color=(0xF44336, 0x4CAF50, 0x2196F3)[i],
                       parent=tab_bar))
    ui.Label("XYZ"[i], x=192 + i * 160, y=250, color=0x111111, value=20,
             parent=tab_bar)

# --- Area tab: line chart accel (C วาด area ด้วย custom draw - เราเป็นเส้น) ---
ui.Label("Accel trend", x=250, y=4, color=0x111111, value=20, parent=tab_area)
area_ch = ui.Chart(x=70, y=36, w=390, h=228, min=-200, max=1200,
                   color=0xF44336, parent=tab_area)
area_sy = area_ch.add_series(0x4CAF50)
area_sz = area_ch.add_series(0x2196F3)

# --- Scatter tab: tilt ball จาก accel จริง (จุด 14px ใน panel มีขอบ) ---
ui.Label("Tilt ball (roll vs pitch)", x=210, y=4, color=0x111111, value=20,
         parent=tab_sca)
ui.Panel(x=140, y=36, w=290, h=230, color=0xFFFFFF, min=0x888888, max=4,
         value=2, parent=tab_sca)
dot = ui.Panel(x=278, y=144, w=14, h=14, color=0xE91E63, min=0xE91E63,
               max=7, value=0, parent=tab_sca)

# --- Line tab: gyro 3 series ---
ui.Label("Gyro (dps x10)", x=240, y=4, color=0x111111, value=20,
         parent=tab_lin)
lin_ch = ui.Chart(x=70, y=36, w=390, h=228, min=-1000, max=1000,
                  color=0xF44336, parent=tab_lin)
lin_sy = lin_ch.add_series(0x4CAF50)
lin_sz = lin_ch.add_series(0x2196F3)

ui.Label(FOOTER, x=180, y=372, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

lcd.print("sec3 ex06: four chart styles from real IMU - switch the tabs")
import math
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
    ax, ay, az = sensors.bmi270.acceleration()
    gx, gy, gz = sensors.bmi270.gyroscope()

    for b, v in zip(bars, (ax, ay, az)):
        b.value(int(100 + v * 10))
    area_ch.set_next(0, int(ax * 100))
    area_ch.set_next(area_sy, int(ay * 100))
    area_ch.set_next(area_sz, int(az * 100))
    # tilt ball: roll/pitch จาก accel -> พิกัดใน panel 290x230 (ขอบ 8px)
    roll = math.atan2(ay, az)
    pitch = math.atan2(-ax, math.sqrt(ay * ay + az * az))
    px = int(278 + (roll / 1.2) * 130)
    py = int(144 - (pitch / 1.2) * 100)
    px = 148 if px < 148 else (408 if px > 408 else px)
    py = 44 if py < 44 else (244 if py > 244 else py)
    dot.pos(px, py)
    lin_ch.set_next(0, int(gx * 10))
    lin_ch.set_next(lin_sy, int(gy * 10))
    lin_ch.set_next(lin_sz, int(gz * 10))
    time.sleep_ms(100)
print("sec3 ex06: done")
