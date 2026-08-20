import time
import ui
import lcd

# ==== BOARD: TESAIoT Dev Kit — ADC = VR1 (pots ตรงสกรีน) ====
import pots

def read_raw():
    return pots.read(0)             # VR1: 0-4095
# ==== END BOARD ====

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000


def cx(s, fs):
    return CX - (len(s) * fs) // 4


ui.screen()
time.sleep_ms(200)

# sec3/ex01 - port ของ part2_ex1_slider_bar (part2_examples.c:194)
# C ใช้ simulate_adc_read(); เราใช้ pot จริงตามกติกา "ไม่มี virtual" ของ sec3
ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)
t = "Part 2 - Example 1: ADC Visualization"
ui.Label(t, x=cx(t, 14), y=8, color=0xFFFFFF, value=14)

ui.Label("ADC Raw Value (0-4095):", x=20, y=42, color=0xCCCCCC, value=14)
sld = ui.Slider(x=CX - 150, y=66, w=300, h=22, min=0, max=4095, value=2048)
ui.Label("Percentage:", x=20, y=108, color=0xCCCCCC, value=14)
bar = ui.Bar(x=CX - 150, y=133, w=300, h=25, min=0, max=100, value=50)

raw_l = ui.Label("Raw: 2048", x=CX - 100 - 34, y=224, color=0x00FF00, value=14)
pct_l = ui.Label("Percent: 50%", x=CX + 100 - 42, y=224, color=0x00FFFF,
                 value=14)
volt_l = ui.Label("Voltage: 1.650 V", x=cx("Voltage: 1.650 V", 24), y=262,
                  color=0xFFFF00, value=24)

t = "Learning: lv_slider, lv_bar, ADC value mapping"
ui.Label(t, x=cx(t, 14), y=336, color=0xAAAAAA, value=14)
t = "Formula: V = (raw / 4095) x 3.3V"
ui.Label(t, x=cx(t, 14), y=354, color=0xAAAAAA, value=14)
ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

lcd.print("sec3 ex01: turn the pot - slider/bar/volts follow")
prev = -1
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
    raw = read_raw()
    if abs(raw - prev) > 8:
        prev = raw
        p = (raw * 100) // 4095
        sld.value(raw)
        bar.value(p)
        raw_l.text("Raw: " + str(raw))
        pct_l.text("Percent: " + str(p) + "%")
        volt_l.text("Voltage: %.3f V" % (raw / 4095 * 3.3))
    time.sleep_ms(100)
print("sec3 ex01: done")
