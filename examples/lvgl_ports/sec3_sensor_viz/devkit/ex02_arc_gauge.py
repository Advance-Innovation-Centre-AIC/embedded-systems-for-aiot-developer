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

# sec3/ex02 - port ของ part2_ex2_arc_gauge (part2_examples.c:294)
ui.Panel(x=0, y=0, w=W, h=H, color=0x0F0F23, min=0x0F0F23, max=0, value=0)
t = "Part 2 - Example 2: Arc Gauge"
ui.Label(t, x=cx(t, 14), y=8, color=0xFFFFFF, value=14)

arc = ui.Arc(x=CX - 100, y=99, w=200, h=200, min=0, max=100, value=50,
             color=0x00BCD4)
val_l = ui.Label("50%", x=cx("50%", 24), y=187, color=0xFFFFFF, value=24)

t = "Learning: lv_arc_create, lv_arc_set_rotation"
ui.Label(t, x=cx(t, 14), y=336, color=0xAAAAAA, value=14)
t = "Use case: Gauge display, gyroscope angle"
ui.Label(t, x=cx(t, 14), y=354, color=0xAAAAAA, value=14)
ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

lcd.print("sec3 ex02: pot -> arc gauge percent")
prev = -1
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
    p = (read_raw() * 100) // 4095
    if p != prev:
        prev = p
        arc.value(p)
        val_l.text(str(p) + "%")
    time.sleep_ms(100)
print("sec3 ex02: done")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
