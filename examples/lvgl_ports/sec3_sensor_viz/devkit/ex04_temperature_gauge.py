import time
import ui
import lcd

# ==== BOARD: TESAIoT Dev Kit — อุณหภูมิจริงจาก SHT40 (I2C 0x44) ====
import sensors

def read_temp():
    return sensors.sht40.temperature()
TEMP_SRC = "SHT40"
# ==== END BOARD ====

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000


def cx(s, fs):
    return CX - (len(s) * fs) // 4


ui.screen()
time.sleep_ms(200)

# sec3/ex04 - port ของ part2_ex4_scale_temperature (part2_examples.c:487)
# C: scale วงกลม + section แดง 75-100 + เข็ม lv_scale_set_line_needle_value
# MPY: ui.Scale (ROUND_OUT) + เข็มจริงผ่าน PROP_SCALE_NEEDLE (ไม่มี section)
# เมื่อค่าที่แสดงเปลี่ยน และโซนร้อนแทนด้วยสี label กลาง (แดงเมื่อ >= 75)
ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)
t = "Part 2 - Example 4: Temperature Gauge"
ui.Label(t, x=cx(t, 14), y=8, color=0xFFFFFF, value=14)

SC_X, SC_Y, SC_W = CX - 100, 99, 200
sc = ui.Scale(x=SC_X, y=SC_Y, w=SC_W, h=SC_W, color=0x1565C0, min=0, max=100)
sc.prop(ui.PROP_SCALE_MODE, ui.SCALE_ROUND_OUT)
sc.ticks(21, 5)

temp_l = ui.Label("-- C", x=cx("-- C", 24), y=230, color=0xFFFFFF, value=24)
ui.Label("(" + TEMP_SRC + ")", x=cx("(" + TEMP_SRC + ")", 14), y=310,
         color=0xAAAAAA, value=14)

t = "Learning: lv_scale_4, needle via PROP_SCALE_NEEDLE, hot zone >= 75 C"
ui.Label(t, x=cx(t, 14), y=350, color=0xAAAAAA, value=14)
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
    # เข็มจริงของ lv_scale (แบบเดียวกับที่ C ใช้) หมุนอยู่กับที่ - ไม่กระพริบ
    sc.prop(ui.PROP_SCALE_NEEDLE, (NEEDLE << 16) | (int(v) & 0xFFFF))


lcd.print("sec3 ex04: real temperature on a round gauge (" + TEMP_SRC + ")")
prev_shown = None
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
    tc = read_temp()
    shown = int(tc)
    if shown != prev_shown:
        prev_shown = shown
        v = 0 if tc < 0 else (100 if tc > 100 else tc)
        draw_needle(v)
        temp_l.text("%d C" % shown)
        temp_l.color(0xF44336 if shown >= 75 else 0xFFFFFF)
    time.sleep_ms(500)
print("sec3 ex04: done")
