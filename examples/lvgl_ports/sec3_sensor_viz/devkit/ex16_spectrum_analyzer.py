import math
import time
import ui
import lcd
import dsp

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
SR = 48000
FFT_N = 256
BINS = 64  # แสดง 64 จาก 128 bins (decimate x2) ตาม C


def cx(s, fs):
    return CX - (len(s) * fs) // 4


_lfsr = 0xACE1


def gen_wave(wt, freq, n=FFT_N, sr=SR, amp=16000):
    global _lfsr
    out = []
    if wt == 4:
        for _ in range(n):
            b = _lfsr & 1
            _lfsr >>= 1
            if b:
                _lfsr ^= 0xB400
            out.append((_lfsr % (2 * amp + 1)) - amp)
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
        out.append(int(v * amp))
    return out


ui.screen()
time.sleep_ms(200)

# sec3/ex16 - port ของ part3_ex6_spectrum_analyzer (part3_examples.c:824)
# FFT จริงใน C ผ่าน dsp.fft_mag() (R4); C วาดแท่ง LV_CHART_TYPE_BAR แต่
# ui.Chart เป็น LINE เท่านั้น - วาดเป็นเส้น envelope แบบ spectrum analyzer
ui.Panel(x=0, y=0, w=W, h=H, color=0x0A0A1E, min=0x0A0A1E, max=0, value=0)
t = "Part 3 - Example 6: FFT Spectrum Analyzer"
ui.Label(t, x=cx(t, 14), y=8, color=0xFF6600, value=14)

dd = ui.Dropdown(x=16, y=40, w=170, h=44)
for o in ("Square", "Sine", "Triangle", "Sawtooth", "Noise"):
    dd.add_option(o)
dd.value(1)
dom_l = ui.Label("Dominant: -- Hz", x=580, y=52, color=0xFFFF00, value=14)
run_b = ui.Button("Run", x=220, y=40, w=90, h=44, color=0x1B5E20, value=16)
stop_b = ui.Button("Stop", x=320, y=40, w=90, h=44, color=0x333333, value=16)

ch = ui.Chart(x=66, y=96, w=660, h=230, min=0, max=100, color=0x00FFFF)
ch.prop(ui.PROP_CHART_POINTS, BINS)

ui.Label("0 Hz", x=66, y=334, color=0x888888, value=14)
ui.Label("24000 Hz", x=650, y=334, color=0x888888, value=14)
ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1


def redraw(wt, freq):
    sig = gen_wave(wt, freq)
    mags = dsp.fft_mag(sig, n=FFT_N)  # 128 bins สเกล 2/N ใน C
    top, dom = 1.0, 0
    for k in range(1, len(mags)):
        if mags[k] > top:
            top, dom = mags[k], k
    i = 0
    for b in range(BINS):
        v = int(mags[b * 2] * 100 / top)
        ch.set_next(0, v if v <= 100 else 100)
        i += 1
        if i % 16 == 0:
            time.sleep_ms(6)
    dom_l.text("Dominant: " + str((dom * SR) // FFT_N) + " Hz")


wt, freq = 1, 1000
running = True
redraw(wt, freq)

lcd.print("sec3 ex16: Run = analyzer วัดซ้ำต่อเนื่องด้วย dsp.fft_mag")
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        h = ev["handle"]
        if ev["type"] == "clicked":
            if h == _back_id:
                RUN_MS = 0
            elif h == run_b.id():
                running = True
            elif h == stop_b.id():
                running = False
        elif ev["type"] == "value_changed" and h == dd.id():
            wt = ev["value"]
    if running:
        redraw(wt, freq)    # FFT ใน C จบ ~1ms - รีเฟรชทั้งสเปกตรัมได้ทุกรอบ
    time.sleep_ms(250)
print("sec3 ex16: done")
