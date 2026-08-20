import time
import ui
import lcd
import sensors

# ==== BOARD: Eva Kit — ADC = pot เดี่ยว P15.1 ====
import sensors as _sp

def read_raw():
    return _sp.pot.read() >> 4      # 0-65535 -> 0-4095
# ==== END BOARD ====

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 180000


ui.screen()
time.sleep_ms(200)

# sec3/ex08 - port ของ part2_ex8_real_sensor_dashboard (part2_hw_examples.c:437)
tv = ui.Tabview(x=0, y=0, w=W, h=H)
tab_adc = tv.add_tab("ADC")
tab_acc = tv.add_tab("Accel")
tab_gyr = tv.add_tab("Gyro")
for tab in (tab_adc, tab_acc, tab_gyr):
    tab.color(0xFFFFFF)

ui.Label("ADC Monitor (REAL)", x=210, y=33, color=0x111111, value=24,
         parent=tab_adc)
adc_bar = ui.Bar(x=90, y=120, w=450, h=50, min=0, max=100, value=0,
                 color=0x1565C0, parent=tab_adc)
adc_l = ui.Label("ADC: 0 (0.00V)", x=190, y=200, color=0x111111, value=24,
                 parent=tab_adc)

ui.Label("Accelerometer (REAL BMI270)", x=150, y=4, color=0x111111, value=24,
         parent=tab_acc)
acc_ch = ui.Chart(x=60, y=48, w=400, h=175, min=-200, max=1200,
                  color=0xF44336, parent=tab_acc)
acc_sy = acc_ch.add_series(0x4CAF50)
acc_sz = acc_ch.add_series(0x2196F3)
acc_l = [ui.Label("0.0", x=560, y=40 + i * 55,
                  color=(0xF44336, 0x4CAF50, 0x2196F3)[i], value=24,
                  parent=tab_acc) for i in range(3)]

ui.Label("Gyroscope (REAL BMI270)", x=180, y=4, color=0x111111, value=24,
         parent=tab_gyr)
gy_arc = []
gy_l = []
for i in range(3):
    gy_arc.append(ui.Arc(x=230 + (i - 1) * 155 + 90, y=52, w=140, h=140,
                         min=0, max=100, value=50,
                         color=(0xF44336, 0x4CAF50, 0x2196F3)[i],
                         parent=tab_gyr))
    ui.Label("XYZ"[i], x=230 + (i - 1) * 155 + 152, y=198, color=0x111111,
             value=20, parent=tab_gyr)
    gy_l.append(ui.Label("0.00", x=230 + (i - 1) * 155 + 138, y=222,
                         color=0x111111, value=14, parent=tab_gyr))

ui.Label(FOOTER, x=180, y=372, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

lcd.print("sec3 ex08: REAL dashboard - pot + BMI270")
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
    raw = read_raw()
    adc_bar.value((raw * 100) // 4095)
    adc_l.text("ADC: %d (%.2fV)" % (raw, raw / 4095 * 3.3))
    ax, ay, az = sensors.bmi270.acceleration()
    acc_ch.set_next(0, int(ax * 100))
    acc_ch.set_next(acc_sy, int(ay * 100))
    acc_ch.set_next(acc_sz, int(az * 100))
    for l, v in zip(acc_l, (ax, ay, az)):
        l.text("%.1f" % v)
    gx, gy, gz = sensors.bmi270.gyroscope()
    for a, l, g in zip(gy_arc, gy_l, (gx, gy, gz)):
        v = int(50 + g / 5)
        a.value(0 if v < 0 else (100 if v > 100 else v))
        l.text("%.2f" % g)
    time.sleep_ms(100)
print("sec3 ex08: done")
