import time
import ui
import lcd
import mic
import dsp

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
N = 128  # 256 ตัวอย่างไมค์ decimate x2


def cx(s, fs):
    return CX - (len(s) * fs) // 4


ui.screen()
time.sleep_ms(200)

# sec3/ex14 - ยุบ part3_ex3_audio_waveform + part3_ex4_mic_visualizer
# (part3_examples.c:441,532) เป็นหนึ่ง: C ex3 เล่น "เสียงจำลอง" เพราะไม่มี
# ไมค์ในเดโมนั้น ส่วน ex4 คือไมค์ + level meter - ของเรามีไมค์ PDM จริง
# จึงใช้เสียงจริงทั้งจอ: mic.raw() -> dsp.s16() จบใน C ไม่ติดลูป 280ms
ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)
t = "Part 3 - Example 3+4: Microphone Waveform + Level"
ui.Label(t, x=cx(t, 14), y=8, color=0xFF00FF, value=14)

ch = ui.Chart(x=46, y=54, w=580, h=240, min=0, max=100, color=0xFF00FF)
ch.prop(ui.PROP_CHART_POINTS, N)

lvl_bar = ui.Bar(x=666, y=54, w=40, h=240, min=0, max=100, value=0)
lvl_l = ui.Label("Level: 0%", x=640, y=306, color=0xFFFFFF, value=14)

btn = ui.Button("Pause", x=CX - 60, y=310, w=120, h=40, color=0x2196F3,
                value=16)
t = "Real PDM mic - speak or clap near the board"
info_l = ui.Label(t, x=cx(t, 14), y=354, color=0x888888, value=14)
ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

mic.start(sens=4)
running = True
peak = 300  # autogain: C สเกล int16 เต็ม แต่เสียงพูดจริงเล็ก - ไต่ตามยอด

lcd.print("sec3 ex14: live mic waveform - Pause freezes the trace")
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked":
            if ev["handle"] == _back_id:
                RUN_MS = 0
            elif ev["handle"] == btn.id():
                running = not running
                btn.text("Pause" if running else "Play")
    if running:
        samples = dsp.s16(mic.raw(), 2)  # 128 จุด แกะใน C
        m = sum(samples) // len(samples)
        top = 1
        for v in samples:
            d = v - m if v > m else m - v
            if d > top:
                top = d
        peak = max(300, (peak * 3 + top) // 4)  # smooth ลดกราฟเด้ง
        i = 0
        for v in samples:
            ch.set_next(0, 50 + ((v - m) * 40) // peak)
            i += 1
            if i % 16 == 0:
                time.sleep_ms(6)
        lv = mic.level()
        lvl_bar.value(lv)
        lvl_l.text("Level: " + str(lv) + "%")
    time.sleep_ms(80)
mic.stop()
print("sec3 ex14: done")
