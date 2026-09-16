import math
import time
import ui
import lcd
import dsp

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 180000
SR = 48000
N = 200
FFT_N = 256


def cx(s, fs):
    return CX - (len(s) * fs) // 4


def gen_wave(wt, freq, n, sr=SR, amp=16000, duty=50):
    out = []
    per = sr / freq
    for i in range(n):
        ph = (i % per) / per
        if wt == 0:
            v = 1.0 if ph < duty / 100 else -1.0
        elif wt == 1:
            v = math.sin(2 * math.pi * ph)
        else:
            v = 4 * ph - 1 if ph < 0.5 else 3 - 4 * ph
        out.append(int(v * amp))
    return out


def push_chunk(ch, wt, freq, phase, sr=SR, duty=50, k=8):
    per = sr / freq
    for i in range(k):
        ph = ((phase + i) % per) / per
        if wt == 0:
            s = 1.0 if ph < duty / 100 else -1.0
        elif wt == 1:
            s = math.sin(2 * math.pi * ph)
        else:
            s = 4 * ph - 1 if ph < 0.5 else 3 - 4 * ph
        ch.set_next(0, 50 + int(s * 20))
    return phase + k


ui.screen()
time.sleep_ms(200)

# sec3/ex17 - port ของ part3_scope_example.c (Custom Panel Scope)
# แผงกำหนดเอง 3 หน้า (Scope/Gen/FFT) สลับด้วย .hide()/.show() ตาม C เป๊ะ
ui.Panel(x=0, y=0, w=W, h=H, color=0x101018, min=0x101018, max=0, value=0)
t = "AIC-EEC Scope v1.0"
ui.Label(t, x=cx(t, 16), y=8, color=0x00FF88, value=16)
run_led = ui.Led(x=700, y=8, w=24, h=24, color=0x00FF00, value=255)
ui.Label("Run", x=734, y=12, color=0xCCCCCC, value=14)

nav_scp = ui.Button("Scope", x=8, y=48, w=112, h=56, color=0x2196F3, value=16)
nav_gen = ui.Button("Gen", x=8, y=112, w=112, h=56, color=0x333333, value=16)
nav_fft = ui.Button("FFT", x=8, y=176, w=112, h=56, color=0x333333, value=16)
run_sw = ui.Switch(x=24, y=248, w=80, h=40, value=1)

# --- Scope page ---
scp_ch = ui.Chart(x=136, y=48, w=644, h=250, min=0, max=100, color=0x00FF00)
scp_ch.prop(ui.PROP_CHART_POINTS, N)
scp_info = ui.Label("Wave: Sine    1000 Hz", x=340, y=312, color=0xCCCCCC,
                    value=14)

# --- Gen page (ซ่อนไว้ก่อน) ---
gen_pn = ui.Panel(x=136, y=48, w=644, h=290, color=0x181820, min=0x333333,
                  max=8, value=1)
gen_ch = ui.Chart(x=20, y=14, w=600, h=150, min=0, max=100, color=0xFFAA00,
                  parent=gen_pn)
gen_ch.prop(ui.PROP_CHART_POINTS, 100)
ui.Label("Freq (10-500 Hz)", x=20, y=180, color=0xCCCCCC, value=14,
         parent=gen_pn)
gen_fsld = ui.Slider(x=210, y=180, w=280, h=22, min=10, max=500, value=100,
                     parent=gen_pn)
gen_fl = ui.Label("100 Hz", x=520, y=180, color=0xFFFFFF, value=14,
                  parent=gen_pn)
ui.Label("Duty (%)", x=20, y=230, color=0xCCCCCC, value=14, parent=gen_pn)
gen_dsld = ui.Slider(x=210, y=230, w=280, h=22, min=0, max=100, value=50,
                     parent=gen_pn)
gen_dl = ui.Label("50 %", x=520, y=230, color=0xFFFFFF, value=14,
                  parent=gen_pn)

# --- FFT page (ซ่อนไว้ก่อน) ---
fft_ch = ui.Chart(x=136, y=48, w=644, h=250, min=0, max=100, color=0x00FFFF)
fft_ch.prop(ui.PROP_CHART_POINTS, 64)
fft_info = ui.Label("Dominant: -- Hz", x=340, y=312, color=0xFFFF00, value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=378, color=0x666666, value=14)

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

PAGES = ("scp", "gen", "fft")
page = "scp"
running = True
wt, freq = 1, 1000
gen_freq, gen_duty = 100, 50


def show_page(p):
    if p == "scp":
        scp_ch.show()
        scp_info.show()
    else:
        scp_ch.hide()
        scp_info.hide()
    if p == "gen":
        gen_pn.show()
    else:
        gen_pn.hide()
    if p == "fft":
        fft_ch.show()
        fft_info.show()
    else:
        fft_ch.hide()
        fft_info.hide()
    nav_scp.color(0x2196F3 if p == "scp" else 0x333333)
    nav_gen.color(0x2196F3 if p == "gen" else 0x333333)
    nav_fft.color(0x2196F3 if p == "fft" else 0x333333)


def fft_redraw():
    mags = dsp.fft_mag(gen_wave(wt, freq, FFT_N), n=FFT_N)
    top, dom = 1.0, 0
    for k in range(1, len(mags)):
        if mags[k] > top:
            top, dom = mags[k], k
    for b in range(64):
        v = int(mags[b * 2] * 100 / top)
        fft_ch.set_next(0, v if v <= 100 else 100)
    fft_info.text("Dominant: " + str((dom * SR) // FFT_N) + " Hz")


show_page("scp")
scp_phase = 0
gen_phase = 0

lcd.print("sec3 ex17: สวิตช์ Run = ทุกหน้าวิ่งต่อเนื่อง")
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        h = ev["handle"]
        if ev["type"] == "clicked":
            if h == _back_id:
                RUN_MS = 0
            elif h == nav_scp.id():
                page = "scp"
                show_page(page)
            elif h == nav_gen.id():
                page = "gen"
                show_page(page)
            elif h == nav_fft.id():
                page = "fft"
                show_page(page)
        elif ev["type"] == "toggled" and h == run_sw.id():
            running = ev["value"] == 1
            run_led.prop(ui.PROP_LED_BRIGHTNESS, 255 if running else 80)
        elif ev["type"] == "value_changed":
            if h == gen_fsld.id():
                gen_freq = ev["value"]
                gen_fl.text(str(gen_freq) + " Hz")
            elif h == gen_dsld.id():
                gen_duty = ev["value"]
                gen_dl.text(str(gen_duty) + " %")
    if running:
        if page == "scp":
            scp_phase = push_chunk(scp_ch, wt, freq, scp_phase)
        elif page == "gen":
            gen_phase = push_chunk(gen_ch, 0, gen_freq, gen_phase,
                                   sr=10000, duty=gen_duty, k=6)
        else:
            fft_redraw()    # FFT ใน C ~1ms - รีเฟรชได้ทุกรอบ
    time.sleep_ms(150 if page == "fft" else 100)
print("sec3 ex17: done")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
