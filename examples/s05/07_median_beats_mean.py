# 07_median_beats_mean.py - ค่าหลุดหนึ่งค่า ทำลายค่าเฉลี่ย แต่ทำอะไร median ไม่ได้
# ชุดตัวอย่างประจำคาบ 05
#
# Why : ค่าหลุดค่าเดียวจากสัญญาณรบกวน ถ้าตัวกรองคือค่าเฉลี่ย มันจะไปเปื้อน
#       ผลลัพธ์อีกห้ารอบถัดไป และในห้ารอบนั้นระบบอาจสั่งวาล์วเปิดไปแล้ว
# What: SMA เอาทุกค่ามาบวกกัน ค่าที่หลุดจึงลากค่าเฉลี่ยไปด้วยเต็ม ๆ
#       ส่วน median เรียงลำดับแล้วหยิบตัวกลาง ค่าที่หลุดไปนอนริมแถว
#       ไม่มีสิทธิ์ถูกหยิบ อิทธิพลของมันเป็นศูนย์ ไม่ใช่แค่น้อยลง
#
# ดูที่จอ: หนามแหลมสีฟ้าคือ spike ที่ป้อนเข้าไป เส้นแดง (SMA) กระเพื่อมตามทุกครั้ง
#          เส้นเขียว (median) แบนสนิทที่ spike เดี่ยว
#          กดเดินหน้าจนหน้าต่างถึง 7 แล้วดูว่าเส้นเขียวเลิกยกตัวที่ spike สามตัวติด
#          ตัวเลขข้างขวาคือค่าเบี่ยงสูงสุดของแต่ละวิธี ยิ่งน้อยยิ่งทน
# กับดัก : dsp.Median บังคับหน้าต่างให้เป็นเลขคี่ ถ้าขอ 4 จะได้ 5 และเพดานคือ 15
#          หน้าต่าง N กันได้ spike ที่ติดกันไม่เกิน (N-1)//2 ตัวเท่านั้น

import dsp
import lcd
import time
import ui

N_SAMPLES = 50                  # เท่ากับหน้าต่างของ ui.Chart พอดี
BASE = 2.50
SPIKE = 9.90
SPIKE_AT = (6, 13, 14, 15, 30)  # เดี่ยวหนึ่งครั้ง สามตัวติด แล้วเดี่ยวอีกครั้ง
BURST = 3                       # จำนวน spike ที่ติดกันในกลุ่มกลาง
WINDOWS = (3, 5, 7, 9, 15)      # ท่าที่เดิน หนึ่งท่าคือหนึ่งขนาดหน้าต่าง
PLAY_MS = 1500

samples = []
for i in range(N_SAMPLES):
    # ค่าหลุด แบบที่เกิดจริงเวลาสาย I2C โดนรบกวน
    samples.append(SPIKE if i in SPIKE_AT else BASE)

ui.screen()
time.sleep_ms(200)

ui.Label("median กับ ค่าเฉลี่ย ตอนเจอค่าหลุด", x=12, y=8, value=20)
lbl_pos = ui.Label("", x=470, y=8, value=20, color=0x50D890)

ch = ui.Chart(x=12, y=36, w=420, h=200, min=0, max=110)   # หน่วยเป็นค่า x10
s_raw = 0
s_mean = ch.add_series(0xFF5555)
s_med = ch.add_series(0x55FF88)

ui.Label("ฟ้า = ค่าดิบ (มี spike)", x=446, y=38, value=16, color=0x00BFFF)
ui.Label("แดง = SMA", x=446, y=60, value=16, color=0xFF5555)
ui.Label("เขียว = Median", x=446, y=82, value=16, color=0x55FF88)
lbl_win = ui.Label("", x=446, y=108, value=24, color=0xFFD24A)
ui.Label("SMA เบี่ยงสูงสุด (x100)", x=446, y=140, value=14)
seg_mean = ui.Seg7(x=446, y=158, w=150, h=40, color=0xFF5555)
ui.Label("median เบี่ยงสูงสุด (x100)", x=446, y=196, value=14)
seg_med = ui.Seg7(x=446, y=214, w=150, h=40, color=0x55FF88)
lbl_note = ui.Label("", x=12, y=238, value=16)

btn_prev = ui.Button("< ย้อน", x=20, y=250, w=140, h=64, color=0x546E7A, value=20)
btn_next = ui.Button("เดินหน้า >", x=176, y=250, w=160, h=64, color=0x1E88E5, value=20)
btn_play = ui.Button(">> เล่นรวด", x=352, y=250, w=160, h=64, color=0x2E7D32, value=20)
btn_home = ui.Button("เริ่มใหม่", x=528, y=250, w=140, h=64, color=0x6A1B9A, value=20)
ID_PREV, ID_NEXT = btn_prev.id(), btn_next.id()
ID_PLAY, ID_HOME = btn_play.id(), btn_home.id()

bar_pos = ui.Bar(x=20, y=322, w=300, h=14, min=0, max=len(WINDOWS) - 1, value=0)
lbl_hint = ui.Label("", x=336, y=318, value=16, color=0x90A4AE)

lcd.clear()
lcd.console("<h2>median เทียบ ค่าเฉลี่ย</h2>")
lcd.print("ฐาน", BASE, "| spike", SPIKE, "| spike ติดกัน", BURST, "ตัว")

i = 0                   # ท่าปัจจุบัน - ความจริงของโปรแกรมอยู่ที่ตัวนี้
playing = False
t_next = 0


def show():
    window = WINDOWS[i]
    mean_buf = []
    med = dsp.Median(window=window)
    worst_mean = 0.0
    worst_med = 0.0

    # ป้อนครบ 50 จุดในท่าเดียว หน้าต่างของ Chart กว้าง 50 พอดี ภาพเดิมถูกแทนที่หมด
    for x in samples:
        mean_buf.append(x)
        if len(mean_buf) > window:
            mean_buf.pop(0)
        m = sum(mean_buf) / len(mean_buf)
        mv = med.update(x)

        if abs(m - BASE) > worst_mean:
            worst_mean = abs(m - BASE)
        if abs(mv - BASE) > worst_med:
            worst_med = abs(mv - BASE)

        ch.set_next(s_raw, int(x * 10.0))
        ch.set_next(s_mean, int(m * 10.0))
        ch.set_next(s_med, int(mv * 10.0))

    tolerated = (window - 1) // 2
    lbl_pos.text("ท่า %d / %d" % (i + 1, len(WINDOWS)))
    lbl_win.text("N = %d" % window)
    seg_mean.text(str(int(worst_mean * 100)))
    seg_med.text(str(int(worst_med * 100)))
    if tolerated >= BURST:
        lbl_note.text("กัน spike ติดกันได้ %d ตัว -> ทนกลุ่ม %d ตัวไหว"
                      % (tolerated, BURST))
    else:
        lbl_note.text("กัน spike ติดกันได้ %d ตัว -> กลุ่ม %d ตัว median แพ้"
                      % (tolerated, BURST))
    bar_pos.value(i)
    lbl_hint.text("กำลังเล่นรวด - กดปุ่มไหนก็หยุด" if playing
                  else "กดเดินหน้าเพื่อขยายหน้าต่าง N")
    lcd.print("N=" + str(window) + " SMA เบี่ยง " + str(round(worst_mean, 2)) +
              " median " + str(round(worst_med, 2)))


show()

# ตารางตัวเลขเต็ม ๆ อ่านบนจอ 4.3 นิ้วไม่ไหว ส่งออก serial ไว้อ่านทีหลัง
print("N    กัน spike ติดกันได้   ผลกับกลุ่ม %d ตัว" % BURST)
for w in WINDOWS:
    ok = "median รอด" if (w - 1) // 2 >= BURST else "median แพ้"
    print("%-4d %-21d %s" % (w, (w - 1) // 2, ok))
print("")
print("spike เดี่ยวสูงกว่าฐาน %.2f -> ดัน SMA(5) ขึ้น %.2f"
      % (SPIKE - BASE, (SPIKE - BASE) / 5))
print("ส่วน median ขยับ 0.00 เพราะค่าหลุดถูกเรียงไปอยู่ริมแถว ไม่ใช่ตัวกลาง")
print("ท่ามาตรฐานในงานจริงคือ median ก่อนเพื่อฆ่า spike แล้วค่อย EMA เพื่อความเรียบ")
lcd.print("<span class=ok>ท่ามาตรฐาน: median ก่อน แล้วค่อย EMA</span>")

while True:
    for ev in ui.poll():
        if ev["type"] != "clicked":
            continue
        h = ev["handle"]
        if h == ID_PREV:
            playing = False
            i = (i - 1) % len(WINDOWS)
        elif h == ID_NEXT:
            playing = False
            i = (i + 1) % len(WINDOWS)
        elif h == ID_HOME:
            playing = False
            i = 0
        elif h == ID_PLAY:
            playing = not playing
            t_next = time.ticks_add(time.ticks_ms(), PLAY_MS)
        show()

    # เล่นรวดเดียว: เดินหน้าเองตามเวลา แต่ไม่หลับ เพื่อให้ปุ่มยังกดติด
    if playing and time.ticks_diff(time.ticks_ms(), t_next) >= 0:
        i += 1
        if i >= len(WINDOWS):
            i = len(WINDOWS) - 1
            playing = False
        t_next = time.ticks_add(time.ticks_ms(), PLAY_MS)
        show()

    time.sleep_ms(30)
