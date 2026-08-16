# 06_ema_time_constant.py - alpha ของ EMA แปลว่าอะไรในหน่วยเวลาจริง
# ชุดตัวอย่างประจำคาบ 05
#
# ไฟล์นี้สอน: alpha แปลงเป็นวินาทีได้ตรง ๆ ถ้ารู้คาบของลูป ด้วย
#             tau = dt * (1-alpha)/alpha ซึ่งคือเวลาที่ค่าไล่ขึ้นถึง 63.2% ของขั้น
# ดูที่จอ   : เส้นฟ้าคือขั้นบันไดดิบที่กระโดดขึ้นทันที เส้นแดงคือ EMA ของขั้นนี้
#             เส้นเทาจาง ๆ คือ alpha ของขั้นที่แล้ว ไว้เทียบว่าชันขึ้นหรือช้าลง
# กับดัก    : dsp.EMA ตั้งต้น alpha = 0.1 ถ้าไม่ใส่ ไม่ใช่ 0.5 อย่างที่หลายคนเดา
#             และตัวอย่างแรกถูกใช้เป็นค่าตั้งต้นตรง ๆ ไม่ได้คูณ alpha

import dsp
import lcd
import time
import ui

DT_MS = 200                     # คาบของลูป สมมติ 5 Hz แบบที่ใช้จริงในคาบเรียน
N_SAMPLES = 50                  # เท่ากับหน้าต่างของ ui.Chart พอดี
STEP_AT = 8
ALPHAS = (1.0, 0.5, 0.2, 0.1, 0.05, 0.02)
PLAY_MS = 1400


def steps_to_63(a):
    # เช็กด้วยการเดินสมการจริง ไม่เชื่อสูตรอย่างเดียว
    y, n = 0.0, 0
    while y < 0.632:
        y = a * 1.0 + (1.0 - a) * y
        n += 1
    return n


def raw(n):
    return 0.0 if n < STEP_AT else 1.0   # ขั้นบันไดล้วน ไม่มี noise


ui.screen()
time.sleep_ms(200)

ui.Label("EMA: alpha คือเวลา ไม่ใช่เลขลอย ๆ", x=12, y=8, value=20)
lbl_pos = ui.Label("1 / 6", x=470, y=8, value=20, color=0x50D890)

ch = ui.Chart(x=12, y=36, w=420, h=160, min=-10, max=120)   # หน่วยเป็นค่า x100
s_raw = 0
s_prev = ch.add_series(0x6B7280)        # เงาของ alpha ขั้นที่แล้ว
s_now = ch.add_series(0xFF5555)

ui.Label("ฟ้า = ขั้นบันไดดิบ", x=446, y=38, value=16, color=0x00BFFF)
ui.Label("แดง = EMA ขั้นนี้", x=446, y=60, value=16, color=0xFF5555)
ui.Label("เทา = alpha ขั้นที่แล้ว", x=446, y=82, value=16, color=0x9AA3AF)
lbl_a = ui.Label("alpha = 1.00", x=446, y=108, value=24, color=0xFFD24A)
ui.Label("tau (ms)", x=446, y=140, value=14)
seg_tau = ui.Seg7(x=446, y=158, w=150, h=40)
lbl_note = ui.Label("กดเดินหน้าเพื่อลด alpha", x=12, y=210, value=16)

btn_prev = ui.Button("< ย้อน", x=20, y=250, w=140, h=64, color=0x546E7A, value=20)
btn_next = ui.Button("เดินหน้า >", x=176, y=250, w=160, h=64, color=0x1E88E5,
                     value=20)
btn_play = ui.Button(">> เล่นรวด", x=352, y=250, w=160, h=64, color=0x2E7D32,
                     value=20)
btn_home = ui.Button("เริ่มใหม่", x=528, y=250, w=140, h=64, color=0x6A1B9A,
                     value=20)
ID_PREV, ID_NEXT = btn_prev.id(), btn_next.id()
ID_PLAY, ID_HOME = btn_play.id(), btn_home.id()

lbl_hint = ui.Label("กดเดินหน้าเพื่อลด alpha", x=20, y=322, value=16,
                    color=0x90A4AE)

lcd.clear()
lcd.console("<h2>EMA และค่าคงที่เวลา</h2>")
lcd.print("คาบลูปที่สมมติ", DT_MS, "ms")
# dsp.LPF คือ EMA ตัวเดียวกันนี้ แค่คำนวณ alpha ให้จากความถี่ตัดกับอัตราสุ่ม
# จึงเลือกได้สองทาง จะบอกเป็น alpha หรือบอกเป็นความถี่ตัดก็ได้ผลเดียวกัน
lcd.print("<span class=muted>dsp.LPF บอกเป็นความถี่ตัด ข้างในคือ EMA ตัวนี้</span>")

step = 0                # ขั้นปัจจุบัน - ความจริงของโปรแกรมอยู่ที่ตัวนี้
playing = False
t_next = 0


def show():
    a = ALPHAS[step]
    a_prev = ALPHAS[step - 1] if step else a

    # ป้อนครบ 50 จุดในขั้นเดียว หน้าต่างของ Chart กว้าง 50 พอดี ภาพเดิมถูกแทนที่หมด
    f_now = dsp.EMA(alpha=a)
    f_prev = dsp.EMA(alpha=a_prev)
    for n in range(N_SAMPLES):
        x = raw(n)
        ch.set_next(s_raw, int(x * 100.0))
        ch.set_next(s_prev, int(f_prev.update(x) * 100.0))
        ch.set_next(s_now, int(f_now.update(x) * 100.0))

    # tau คือค่าคงที่เวลาของวงจร RC ที่ตัวกรองนี้เลียนแบบอยู่ สูตรตรงเมื่อ alpha
    # เล็ก ที่ alpha ใหญ่ ๆ ตัวกรองไม่เหมือน RC แล้ว ตัวเลขจึงเป็นแนวโน้ม
    tau_ms = DT_MS * (1.0 - a) / a

    lbl_pos.text("%d / %d" % (step + 1, len(ALPHAS)))
    lbl_a.text("alpha = %.2f" % a)
    seg_tau.text(str(int(tau_ms)))
    lbl_note.text("ถึง 63.2%% ที่ตัวอย่างที่ %d | เทียบเท่า SMA(%.0f)"
                  % (steps_to_63(a), (2.0 - a) / a))
    lbl_hint.text("กำลังเล่นรวด - กดปุ่มไหนก็หยุด" if playing
                  else "กดเดินหน้าเพื่อลด alpha")
    lcd.print("alpha " + str(a) + " -> tau " + str(int(tau_ms)) + " ms")


show()

while True:
    for ev in ui.poll():
        if ev["type"] != "clicked":
            continue
        handle = ev["handle"]
        if handle == ID_PREV:
            playing = False
            step = (step - 1) % len(ALPHAS)
        elif handle == ID_NEXT:
            playing = False
            step = (step + 1) % len(ALPHAS)
        elif handle == ID_HOME:
            playing = False
            step = 0
        elif handle == ID_PLAY:
            playing = not playing
            t_next = time.ticks_add(time.ticks_ms(), PLAY_MS)
        show()

    # เล่นรวดเดียว: เดินหน้าเองตามเวลา แต่ไม่หลับ เพื่อให้ปุ่มยังกดติด
    if playing and time.ticks_diff(time.ticks_ms(), t_next) >= 0:
        step += 1
        if step >= len(ALPHAS):
            step = len(ALPHAS) - 1
            playing = False
        t_next = time.ticks_add(time.ticks_ms(), PLAY_MS)
        show()

    time.sleep_ms(30)
