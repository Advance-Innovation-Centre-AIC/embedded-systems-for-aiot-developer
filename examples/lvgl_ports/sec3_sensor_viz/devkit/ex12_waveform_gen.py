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


def push_chunk(ch, wt, freq, phase, k=8):
    # สโคปวิ่งจริง: ดันทีละ k จุดต่อรอบลูป (~80 จุด/วินาที) แทนวาดทั้งเส้น
    global _lfsr
    per = SR / freq
    for i in range(k):
        if wt == 4:
            b = _lfsr & 1
            _lfsr >>= 1
            if b:
                _lfsr ^= 0xB400
            v = 50 + ((_lfsr % 41) - 20)
        else:
            ph = ((phase + i) % per) / per
            if wt == 0:
                s = 1.0 if ph < 0.5 else -1.0
            elif wt == 1:
                s = math.sin(2 * math.pi * ph)
            elif wt == 2:
                s = 4 * ph - 1 if ph < 0.5 else 3 - 4 * ph
            else:
                s = 2 * ph - 1
            v = 50 + int(s * 20)
        ch.set_next(0, v)
    return phase + k


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
info_l = ui.Label("Waveform: Sine", x=560, y=352, color=0x888888, value=14)
run_b = ui.Button("Run", x=560, y=294, w=104, h=46, color=0x1B5E20, value=16)
stop_b = ui.Button("Stop", x=676, y=294, w=104, h=46, color=0x333333, value=16)

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
running = True
phase = 0

lcd.print("sec3 ex12: Run = คลื่นวิ่งต่อเนื่อง, Stop = หยุดนิ่ง")
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
        elif ev["type"] == "value_changed":
            if h == dd.id():
                wt = ev["value"]
                info_l.text("Waveform: " + NAMES[wt])
            elif h == sld.id():
                freq = 100 + ev["value"] * ev["value"]  # แผนที่กำลังสองแบบ C
                freq_l.text("Freq: " + str(freq) + " Hz")
    if running:
        phase = push_chunk(ch, wt, freq, phase)
    time.sleep_ms(100)
print("sec3 ex12: done")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
