import math
import time
import ui
import lcd

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
SR = 48000
N = 200


def cx(s, fs):
    return CX - (len(s) * fs) // 4


_lfsr = 0xACE1


def gen_wave(wt, freq, n=N, sr=SR, amp=20, mid=50):
    # สเกลจอเดียวกับ C: 50 + v*40/32767 (amp 16000 -> แกว่ง ~±20)
    global _lfsr
    out = []
    if wt == 4:
        for _ in range(n):
            b = _lfsr & 1
            _lfsr >>= 1
            if b:
                _lfsr ^= 0xB400
            out.append(mid + ((_lfsr % (2 * amp + 1)) - amp))
        return out
    per = sr / freq
    for i in range(n):
        ph = (i % per) / per
        if wt == 0:
            v = 1.0 if ph < 0.5 else -1.0
        elif wt == 1:
            v = math.sin(2 * math.pi * ph)
        elif wt == 2:
            v = 4 * ph - 1 if ph < 0.5 else 3 - 4 * ph
        else:
            v = 2 * ph - 1
        out.append(mid + int(v * amp))
    return out


def draw_trace(ch, s, pts):
    # 200 จุดคือ 200 ข้อความ IPC - เว้นจังหวะให้ drain (fast-mode 16/5ms)
    i = 0
    for v in pts:
        ch.set_next(s, v)
        i += 1
        if i % 16 == 0:
            time.sleep_ms(6)


ui.screen()
time.sleep_ms(200)

# sec3/ex12 - port ของ part3_ex1_waveform_generator (part3_examples.c:255)
# คลื่นคำนวณใน Python; จุดกราฟ 200 ตามต้นฉบับผ่าน PROP_CHART_POINTS (R4)
ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)
t = "Part 3 - Example 1: Waveform Generator"
ui.Label(t, x=cx(t, 14), y=8, color=0x00FF88, value=14)

ch = ui.Chart(x=66, y=44, w=660, h=240, min=0, max=100, color=0x00FF00)
ch.prop(ui.PROP_CHART_POINTS, N)

dd = ui.Dropdown(x=36, y=294, w=170, h=44)
for o in ("Square", "Sine", "Triangle", "Sawtooth", "Noise"):
    dd.add_option(o)
dd.value(1)

sld = ui.Slider(x=280, y=316, w=240, h=22, min=0, max=100, value=30)
freq_l = ui.Label("Freq: 1000 Hz", x=310, y=290, color=0xFFFFFF, value=14)
info_l = ui.Label("Waveform: Sine", x=580, y=312, color=0x888888, value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

NAMES = ("Square", "Sine", "Triangle", "Sawtooth", "Noise")
wt, freq = 1, 1000
draw_trace(ch, 0, gen_wave(wt, freq))

lcd.print("sec3 ex12: pick a wave + slide the frequency")
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    dirty = False
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
        elif ev["type"] == "value_changed":
            if ev["handle"] == dd.id():
                wt = ev["value"]
                info_l.text("Waveform: " + NAMES[wt])
                dirty = True
            elif ev["handle"] == sld.id():
                freq = 100 + ev["value"] * ev["value"]  # แผนที่กำลังสองแบบ C
                freq_l.text("Freq: " + str(freq) + " Hz")
                dirty = True
    if dirty or wt == 4:  # Noise วาดใหม่เรื่อย ๆ เหมือน timer ของ C
        draw_trace(ch, 0, gen_wave(wt, freq))
    time.sleep_ms(100)
print("sec3 ex12: done")
