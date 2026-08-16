# 03_aliasing_nyquist.py - สุ่มช้าเกินไป แล้วได้ความถี่ที่ไม่เคยมีอยู่จริง
# ชุดตัวอย่างประจำคาบ 07
#
# ไฟล์นี้สอน: aliasing ไม่ใช่ noise และกรองทิ้งทีหลังไม่ได้ เพราะมันคือความถี่จริง
#             ที่ถูกพับลงมาทับย่านที่เราสนใจตั้งแต่ตอนสุ่ม
# ดูที่จอ   : เส้นสว่างคือคลื่นจริง เส้นสีเน้นคือคลื่นที่เครื่องคิดว่าเห็น
#             เส้นจางเป็นบันไดคือค่าที่ ADC เก็บได้จริงที่ 40 Hz - บันไดแตะทั้ง
#             สองเส้นที่จุดสุ่มทุกจุด ขั้นแรก ๆ สองเส้นทับกัน พอเกิน 20 Hz มันแยกกัน
# กับดัก    : กฎคือ fs > 2*f ไม่ใช่ fs >= 2*f ตรงที่เท่ากันพอดีคือกรณีที่แย่ที่สุด
#             อาจสุ่มโดนจุดตัดศูนย์ทุกครั้งแล้วได้สัญญาณแบน ๆ ที่ดูเหมือนไม่มีอะไรเลย

import lcd
import math
import time
import ui

FS = 40.0                   # อัตราสุ่มที่ใช้จริง ตรึงไว้ทุกท่า
N_DISPLAY = 50              # เท่ากับหน้าต่างของ ui.Chart พอดี
SPAN_S = 0.225              # ช่วงเวลาที่วาด กว้างพอให้เห็นเก้าจุดสุ่ม
FREQS = (5.0, 18.0, 20.0, 30.0, 39.0, 41.0)     # ความถี่ของคลื่นจริงที่จะเดินดู
PLAY_MS = 1600


def alias_of(f_signal, fs):
    # พับความถี่ลงมาในย่าน 0 ถึง fs/2 ซึ่งคือย่านเดียวที่อัตราสุ่มนี้มองเห็น
    f = f_signal % fs
    if f > fs / 2.0:
        f = fs - f
    return f


ui.screen()
time.sleep_ms(200)

# ผังจอสี่แถบ ขอบนอก 24 - หัวเรื่อง / กราฟกับคำอธิบายสี / คำตัดสิน / แถวปุ่ม
# แถวปุ่มจบที่ x=680 จึงพ้นมุมของปุ่ม Console (x>=690) ได้ทั้งสี่ใบ
# ปุ่มกว้าง 140 สูง 88 เว้นกัน 32 ตาม §S7.13.11-12
ui.Label("aliasing: ความถี่ที่ไม่เคยมีอยู่จริง", x=24, y=8, value=20)
# ป้ายนี้บอกทั้งขั้นที่อยู่ และบอกว่ากำลังเล่นรวดอยู่หรือเปล่า ตาม §S7.7.5
lbl_pos = ui.Label("1 / 6", x=448, y=8, value=20, color=0x30A46C)

ch = ui.Chart(x=24, y=44, w=408, h=192, min=-120, max=120)   # หน่วยเป็นค่า x100
s_true = 0
s_hold = ch.add_series(0xE5484D)
s_alias = ch.add_series(0x30A46C)

ui.Label("ฟ้า = คลื่นจริง", x=448, y=44, value=20, color=0x4A9EFF)
ui.Label("แดง = ที่ ADC เก็บได้", x=448, y=72, value=20, color=0xE5484D)
ui.Label("เขียว = ที่เครื่องคิดว่าเห็น", x=448, y=100, value=20, color=0x30A46C)
lbl_f = ui.Label("f จริง 5 | Nyquist 20 Hz", x=448, y=132, value=20,
                 color=0xF5A623)
ui.Label("ความถี่ที่ 'เห็น' (Hz)", x=448, y=164, value=20)
seg_seen = ui.Seg7(x=448, y=196, w=152, h=40)
lbl_note = ui.Label("กดเดินหน้าเพื่อเพิ่มความถี่", x=24, y=244, value=20)

btn_prev = ui.Button("< ย้อน", x=24, y=284, w=140, h=88, color=0x9AA3AF, value=20)
btn_next = ui.Button("เดินหน้า >", x=196, y=284, w=140, h=88, color=0x4A9EFF, value=20)
btn_play = ui.Button(">> เล่นรวด", x=368, y=284, w=140, h=88, color=0x30A46C, value=20)
btn_home = ui.Button("เริ่มใหม่", x=540, y=284, w=140, h=88, color=0x4A9EFF, value=20)
ID_PREV, ID_NEXT = btn_prev.id(), btn_next.id()
ID_PLAY, ID_HOME = btn_play.id(), btn_home.id()

lcd.clear()
lcd.console("<h2>aliasing และ Nyquist</h2>")
lcd.print("อัตราสุ่มตรึงที่", int(FS), "Hz | Nyquist", int(FS / 2.0), "Hz")

step = 0                # ขั้นปัจจุบัน - ความจริงของโปรแกรมอยู่ที่ตัวนี้
playing = False
t_next = 0


def show():
    f_true = FREQS[step]
    f_seen = alias_of(f_true, FS)
    # การพับผ่าน fs/2 ทำให้เฟสกลับด้าน จึงต้องกลับเครื่องหมายตอนวาดคลื่นที่เห็น
    flip = -1.0 if f_true > FS / 2.0 else 1.0

    t_sample = -1.0         # เวลาของจุดสุ่มล่าสุด เริ่มที่ค่าที่ยังไม่ถึงจุดแรก
    held = 0.0

    # ป้อนครบ 50 จุดในขั้นเดียว หน้าต่างของ Chart กว้าง 50 พอดี ภาพเดิมถูกแทนที่หมด
    for n in range(N_DISPLAY):
        t = SPAN_S * n / (N_DISPLAY - 1)

        # ถึงเวลาสุ่มหรือยัง ถ้าถึงก็หยิบค่าจากคลื่นจริง แล้วคงไว้จนจุดถัดไป
        # นี่คือสิ่งที่ ADC ทำจริง เรียกว่า zero-order hold
        if t >= t_sample + 1.0 / FS:
            t_sample = math.floor(t * FS) / FS
            held = math.sin(2 * math.pi * f_true * t_sample)

        ch.set_next(s_true, int(100.0 * math.sin(2 * math.pi * f_true * t)))
        ch.set_next(s_hold, int(100.0 * held))
        ch.set_next(s_alias, int(flip * 100.0 * math.sin(2 * math.pi * f_seen * t)))

    # ป้ายขั้นบอกสถานะการเล่นรวดไปในตัว จึงไม่ต้องมีป้ายใบที่สองมาแย่งที่แถวปุ่ม
    lbl_pos.text("%d / %d  เล่นรวด" % (step + 1, len(FREQS)) if playing
                 else "%d / %d" % (step + 1, len(FREQS)))
    lbl_f.text("f จริง %.0f | Nyquist %.0f Hz" % (f_true, FS / 2.0))
    seg_seen.text(str(int(f_seen)))
    if f_true < FS / 2.0:
        lbl_note.text("อยู่ใต้ Nyquist - สองเส้นทับกัน ตัวเลขเชื่อได้")
    elif f_true == FS / 2.0:
        lbl_note.text("อยู่ที่ Nyquist พอดี - ไม่ปลอดภัย อาจได้เส้นแบน")
    else:
        lbl_note.text("เกิน Nyquist - เครื่องรายงาน %.0f Hz ซึ่งไม่มีอยู่จริง"
                      % f_seen)
    lcd.print(str(int(f_true)) + " Hz สุ่มที่ " + str(int(FS)) +
              " Hz -> เห็น " + str(int(f_seen)) + " Hz")


show()

# หลักฐานว่า 30 Hz กับ 10 Hz แยกกันไม่ได้จริง ๆ ที่ 40 Hz: ที่จุดสุ่มทั้งเก้าจุด
# สองคลื่นให้ค่าเท่ากันหมด ต่างกันแค่เครื่องหมาย ตัวเลขชุดเดียวกันจึงตอบไม่ได้
# ว่าคลื่นจริงคืออันไหน ไม่ใช่เพราะอัลกอริทึมอ่อน แต่เพราะข้อมูลที่ต้องใช้แยกแยะ
# ไม่ได้ถูกเก็บมาตั้งแต่แรก - ตารางยาวไปสำหรับจอ 4.3 นิ้ว จึงส่งออก serial
print("n   t (ms)   sin 30 Hz   sin 10 Hz   |ผลบวก|")
for n in range(9):
    t = n / FS
    a = math.sin(2 * math.pi * 30.0 * t)
    b = math.sin(2 * math.pi * 10.0 * t)
    print("%-3d %-8.1f %-11.4f %-11.4f %.6f" % (n, t * 1000, a, b, abs(a + b)))

# ทางแก้มีทางเดียว คือ anti-aliasing low-pass filter ที่ตัวฮาร์ดแวร์ก่อนถึง ADC
# บนบอร์ดนี้คือ C204 ที่ wiper ของ pot ซึ่ง fc ประมาณ 637 Hz
lcd.print("แก้ได้ทางเดียว: anti-aliasing filter ก่อนถึง ADC")
lcd.print("<span class=muted>บนบอร์ดนี้คือ C204 ที่ wiper ของ pot fc ~637 Hz</span>")

while True:
    for ev in ui.poll():
        if ev["type"] != "clicked":
            continue
        handle = ev["handle"]
        if handle == ID_PREV:
            playing = False
            step = (step - 1) % len(FREQS)
        elif handle == ID_NEXT:
            playing = False
            step = (step + 1) % len(FREQS)
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
        if step >= len(FREQS):
            step = len(FREQS) - 1
            playing = False
        t_next = time.ticks_add(time.ticks_ms(), PLAY_MS)
        show()

    time.sleep_ms(30)
