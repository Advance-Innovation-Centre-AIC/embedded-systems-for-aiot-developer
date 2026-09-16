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
WAVE_PTS = 64  # จุดของกราฟโดเมนเวลา - วาดใหม่เฉพาะตอนเปลี่ยนชนิดคลื่น
WAVES = ("Square", "Sine", "Triangle", "Sawtooth", "Noise")


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
#
# สัญญาณมาจาก gen_wave() ในไฟล์นี้เอง ไม่ใช่ไมโครโฟน - ตัวอย่างนี้สอน FFT
# (ตัวอย่างที่ใช้ไมค์จริงคือ ex14_audio_waveform.py)
ui.Panel(x=0, y=0, w=W, h=H, color=0x0A0A1E, min=0x0A0A1E, max=0, value=0)
t = "Part 3 - Example 6: FFT Spectrum Analyzer"
ui.Label(t, x=cx(t, 14), y=8, color=0xFF6600, value=14)

dd = ui.Dropdown(x=16, y=40, w=170, h=44)
for o in WAVES:
    dd.add_option(o)
dd.value(1)
run_b = ui.Button("Run", x=196, y=40, w=84, h=44, color=0x1B5E20, value=16)
stop_b = ui.Button("Stop", x=288, y=40, w=84, h=44, color=0x333333, value=16)
wave_l = ui.Label("gen: Sine 1000 Hz", x=360, y=52, color=0x00FF88, value=14)
dom_l = ui.Label("FFT: -- Hz", x=560, y=52, color=0xFFFF00, value=14)

# กราฟบน = รูปคลื่นตามเวลา (เห็นว่าคลื่นที่เลือกหน้าตาอย่างไร)
# กราฟล่าง = สเปกตรัมของคลื่นเดียวกัน (เห็นว่าฮาร์มอนิกต่างกันอย่างไร)
wave_ch = ui.Chart(x=66, y=92, w=660, h=86, min=-100, max=100, color=0x00FF88)
wave_ch.prop(ui.PROP_CHART_POINTS, WAVE_PTS)
ui.Label("time", x=16, y=124, color=0x888888, value=14)

ch = ui.Chart(x=66, y=196, w=660, h=130, min=0, max=100, color=0x00FFFF)
ch.prop(ui.PROP_CHART_POINTS, BINS)
ui.Label("freq", x=16, y=250, color=0x888888, value=14)

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



def draw_wave_time(sig):
    """วาดรูปคลื่นตามเวลา - เรียกเฉพาะตอนเปลี่ยนชนิด เพราะรูปคลื่นคงที่"""
    step = len(sig) // WAVE_PTS
    for i in range(WAVE_PTS):
        wave_ch.set_next(0, int(sig[i * step] * 100 / 16000))
        if i % 16 == 0:
            time.sleep_ms(6)


def redraw(wt, freq, with_time=False):
    sig = gen_wave(wt, freq)
    if with_time:
        draw_wave_time(sig)
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
    # bin กว้าง SR/FFT_N = 187.5 Hz ยอดจึงตกที่ bin ใกล้สุด ไม่ตรง 1000 พอดี
    dom_l.text("FFT: " + str((dom * SR) // FFT_N) + " Hz (bin " +
               str(SR // FFT_N) + " Hz)")


wt, freq = 1, 1000
running = True
wave_l.text("gen: " + WAVES[wt] + " " + str(freq) + " Hz")
redraw(wt, freq, True)

lcd.print("ex16: gen = คลื่นที่สร้าง, FFT = ที่วัดได้")
lcd.print("bin กว้าง 187.5 Hz ยอดจึงตกที่ 937 ไม่ใช่ 1000 พอดี")
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    changed = False
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
            changed = True
            wave_l.text("gen: " + WAVES[wt] + " " + str(freq) + " Hz")
    if running or changed:
        # เปลี่ยนชนิดคลื่นแล้ววาดรูปคลื่นใหม่ด้วย แม้กด Stop ค้างไว้
        redraw(wt, freq, changed)
    time.sleep_ms(250)

print("sec3 ex16: done")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
