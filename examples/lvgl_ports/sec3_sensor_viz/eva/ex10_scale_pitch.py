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

# sec3/ex10 - port ของ part2_ex10_real_scale_gauge (part2_hw_examples.c:639)
# pitch จริงบน scale วงกลม + เข็มจริงของ lv_scale (PROP_SCALE_NEEDLE)
ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)
t = "Part 2 Ex10: Scale Gauge (Pitch Angle)"
ui.Label(t, x=cx(t, 14), y=8, color=0xFFFFFF, value=14)

SC_X, SC_Y, SC_W = CX - 100, 95, 200
sc = ui.Scale(x=SC_X, y=SC_Y, w=SC_W, h=SC_W, color=0x1565C0, min=0, max=100)
sc.prop(ui.PROP_SCALE_MODE, ui.SCALE_ROUND_OUT)
sc.ticks(21, 5)
val_l = ui.Label("50", x=cx("50", 24), y=183, color=0xFFFFFF, value=24)
pit_l = ui.Label("Pitch: 0.0 deg", x=cx("Pitch: 0.0 deg", 14), y=310,
                 color=0x00FF00, value=14)
t = "[Part II] Pitch from dsp.tilt (BMI270 accel)"
ui.Label(t, x=cx(t, 14), y=348, color=0xAAAAAA, value=14)
ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

NEEDLE = 85
sc.prop(ui.PROP_SCALE_NEEDLE_COLOR, 0xFF9800)


def draw_needle(v):
    # เข็มจริงของ lv_scale หมุนอยู่กับที่ - ไม่ลบ/สร้าง widget จึงไม่กระพริบ
    sc.prop(ui.PROP_SCALE_NEEDLE, (NEEDLE << 16) | (int(v) & 0xFFFF))


lcd.print("sec3 ex10: tilt the board - pitch needle follows")
t0 = time.ticks_ms()
prev = None
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
    ax, ay, az = sensors.bmi270.acceleration()
    roll, pitch = dsp.tilt(ax, ay, az)
    v = int(50 + pitch * 50 / 90)
    v = 0 if v < 0 else (100 if v > 100 else v)
    if v != prev:
        prev = v
        draw_needle(v)
        val_l.text(str(v))
        pit_l.text("Pitch: %.1f deg" % pitch)
    time.sleep_ms(200)
print("sec3 ex10: done")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
