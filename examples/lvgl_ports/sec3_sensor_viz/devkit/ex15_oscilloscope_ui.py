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


def gen_wave(wt, freq, n=N, sr=SR, amp=16000, mid=0):
    # หน่วย int16 แบบ C เพื่อให้สูตรวัด Vpp/RMS ตรงต้นฉบับ (/327)
    out = []
    per = sr / freq
    for i in range(n):
        ph = (i % per) / per
        if wt == 0:
            v = 1.0 if ph < 0.5 else -1.0
        elif wt == 1:
            v = math.sin(2 * math.pi * ph)
        else:
            v = 4 * ph - 1 if ph < 0.5 else 3 - 4 * ph
        out.append(mid + int(v * amp))
    return out


def push_chunk(ch, wt, freq, phase, k=8):
    # สโคปวิ่งต่อเนื่อง: ดัน k จุด/รอบ คงเฟสข้ามรอบ
    per = SR / freq
    for i in range(k):
        ph = ((phase + i) % per) / per
        if wt == 0:
            s = 1.0 if ph < 0.5 else -1.0
        elif wt == 1:
            s = math.sin(2 * math.pi * ph)
        else:
            s = 4 * ph - 1 if ph < 0.5 else 3 - 4 * ph
        ch.set_next(0, 50 + int(s * 20))
    return phase + k


ui.screen()
time.sleep_ms(200)

# sec3/ex15 - port ของ part3_ex5_oscilloscope_ui (part3_examples.c:664)
# จอสโคป + แผงควบคุมขวา + แถบวัด Vpp/Freq/RMS (คำนวณใน Python จากบัฟเฟอร์)
ui.Panel(x=0, y=0, w=W, h=H, color=0x0A0A0A, min=0x0A0A0A, max=0, value=0)
t = "Part 3 - Example 5: Digital Oscilloscope"
ui.Label(t, x=cx(t, 14), y=6, color=0x00FF00, value=14)

ch = ui.Chart(x=16, y=42, w=594, h=240, min=0, max=100, color=0x00FF00)
ch.prop(ui.PROP_CHART_POINTS, N)

ui.Panel(x=620, y=42, w=158, h=240, color=0x1A1A1A, min=0x333333, max=6,
         value=1)
dd = ui.Dropdown(x=630, y=52, w=138, h=44, parent=None)
for o in ("Square", "Sine", "Triangle"):
    dd.add_option(o)
dd.value(1)
sld = ui.Slider(x=634, y=112, w=130, h=22, min=0, max=100, value=30)
freq_l = ui.Label("1000 Hz", x=650, y=140, color=0xFFFFFF, value=14)
ui.Label("1 ms/div", x=650, y=176, color=0xFFFF00, value=14)
ui.Label("1 V/div", x=650, y=198, color=0x00FFFF, value=14)
run_b = ui.Button("Run", x=630, y=224, w=66, h=46, color=0x1B5E20, value=16)
stop_b = ui.Button("Stop", x=702, y=224, w=66, h=46, color=0x333333, value=16)

ui.Panel(x=16, y=288, w=762, h=52, color=0x1A1A1A, min=0x333333, max=6,
         value=1)
vpp_l = ui.Label("Vpp: --", x=80, y=304, color=0x00FF00, value=16)
fmeas_l = ui.Label("Freq: -- Hz", x=330, y=304, color=0xFFFF00, value=16)
rms_l = ui.Label("RMS: --", x=620, y=304, color=0x00FFFF, value=16)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1


def measure(pts):
    lo, hi, acc, cross = pts[0], pts[0], 0, 0
    prev = pts[0]
    for v in pts:
        if v < lo:
            lo = v
        if v > hi:
            hi = v
        acc += v * v
        if prev < 0 <= v:
            cross += 1
        prev = v
    rms = int(math.sqrt(acc / len(pts)))
    fr = (cross * SR) // len(pts)
    vpp_l.text("Vpp: " + str((hi - lo) // 327))
    fmeas_l.text("Freq: " + str(fr) + " Hz")
    rms_l.text("RMS: " + str(rms // 327))


wt, freq = 1, 1000
running = True
phase = 0
measure(gen_wave(wt, freq))

lcd.print("sec3 ex15: Run = เส้นวิ่ง + วัด Vpp/Freq/RMS สด")
t0 = time.ticks_ms()
last_meas = t0
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
        elif ev["type"] == "value_changed":
            if h == dd.id():
                wt = ev["value"]
            elif h == sld.id():
                freq = 100 + ev["value"] * ev["value"]
                freq_l.text(str(freq) + " Hz")
    if running:
        phase = push_chunk(ch, wt, freq, phase)
        if time.ticks_diff(time.ticks_ms(), last_meas) > 1000:
            last_meas = time.ticks_ms()
            measure(gen_wave(wt, freq))
    time.sleep_ms(100)
print("sec3 ex15: done")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
