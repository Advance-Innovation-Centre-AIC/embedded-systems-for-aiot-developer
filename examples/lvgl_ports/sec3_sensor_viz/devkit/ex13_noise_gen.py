import time
import ui
import lcd

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
N = 200


def cx(s, fs):
    return CX - (len(s) * fs) // 4


_lfsr = 0xACE1


def lfsr_noise(n=N, amp=20, mid=50):
    # LFSR 16 บิต taps 0xB400 - สุ่มเทียมแบบเดียวกับ aic_scope_generate_noise
    global _lfsr
    out = []
    for _ in range(n):
        b = _lfsr & 1
        _lfsr >>= 1
        if b:
            _lfsr ^= 0xB400
        out.append(mid + ((_lfsr % (2 * amp + 1)) - amp))
    return out


def draw_trace(ch, s, pts):
    i = 0
    for v in pts:
        ch.set_next(s, v)
        i += 1
        if i % 16 == 0:
            time.sleep_ms(6)


ui.screen()
time.sleep_ms(200)

# sec3/ex13 - port ของ part3_ex2_noise_generator (part3_examples.c:350)
# C วาดใหม่ทุก 50ms; ทาง IPC ของเราวาด 200 จุดได้ราว ~2Hz (แจ้งใน ledger)
ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)
t = "Part 3 - Example 2: Noise Generator - White Noise"
ui.Label(t, x=cx(t, 14), y=8, color=0xFF8800, value=14)

ch = ui.Chart(x=66, y=54, w=660, h=250, min=0, max=100, color=0xFF6600)
ch.prop(ui.PROP_CHART_POINTS, N)

t = "LFSR-based pseudo-random noise"
ui.Label(t, x=cx(t, 14), y=324, color=0x888888, value=14)
ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

lcd.print("sec3 ex13: LFSR noise redraws continuously")
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
    draw_trace(ch, 0, lfsr_noise())
    time.sleep_ms(150)
print("sec3 ex13: done")
