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

ui.Label("median กับ ค่าเฉลี่ย ตอนเจอค่าหลุด", x=24, y=24, value=24)
lbl_pos = ui.Label("ท่า - / -", x=600, y=32, value=20, color=0x30A46C)

# สีของสามเส้นต้องเป็นสีเดียวกับป้ายบอกสีที่อยู่ข้าง ๆ ไม่ใช่แค่ใกล้เคียง
# เดิมป้ายเขียนว่าแดงด้วย 0xE5484D แต่เส้นเป็น 0xFF5555 คนละเฉด และเส้นค่าดิบ
# ไม่ได้ตั้งสีไว้เลยทั้งที่ป้ายบอกว่าฟ้า - ป้ายที่บอกสีผิดคือป้ายที่พาไปหาเส้นผิดตัว
ch = ui.Chart(x=24, y=76, w=428, h=164, min=0, max=110,
              color=0x4A9EFF)                # หน่วยเป็นค่า x10
s_raw = 0
s_mean = ch.add_series(0xE5484D)
s_med = ch.add_series(0x30A46C)

# คอลัมน์ขวา: ป้ายบอกสี แล้ว N แล้วตัวเลขสองตัว
# ซ้าย-ขวาของ Seg7 สองตัวตรงกับป้าย SMA / Median ที่อยู่เหนือมัน คนที่แยกสี
# ไม่ออกจึงยังบอกได้ว่าตัวไหนของใคร ตาม S7.7.1 ที่ห้ามสื่อสถานะด้วยสีอย่างเดียว
ui.Label("ฟ้า = ค่าดิบ (มี spike)", x=472, y=76, value=16, color=0x4A9EFF)
ui.Label("แดง = SMA", x=472, y=108, value=16, color=0xE5484D)
ui.Label("เขียว = Median", x=608, y=108, value=16, color=0x30A46C)
lbl_win = ui.Label("N = -", x=472, y=140, value=24, color=0xF5A623)
ui.Label("เบี่ยงสูงสุด (x100)", x=560, y=144, value=16)
seg_mean = ui.Seg7(x=472, y=180, w=136, h=48, color=0xE5484D)
seg_med = ui.Seg7(x=624, y=180, w=136, h=48, color=0x30A46C)

lbl_note = ui.Label("กำลังคำนวณ", x=24, y=248, value=16)
bar_pos = ui.Bar(x=520, y=252, w=168, h=16, min=0, max=len(WINDOWS) - 1,
                 value=0)

# แถวปุ่มสูง 88 ตามเกณฑ์เป้าสัมผัส ห่างกัน 32 และตัวขวาสุดจบที่ 680 ซึ่งอยู่
# ซ้ายของมุมที่ปุ่มคอนโซลจองไว้ ปุ่มที่ยื่นเข้าไปในมุมนั้นจะกดไม่ได้
btn_prev = ui.Button("< ย้อน", x=24, y=284, w=128, h=88, color=0x9AA3AF,
                     value=20)
btn_next = ui.Button("เดินหน้า >", x=184, y=284, w=152, h=88, color=0x4A9EFF,
                     value=20)
btn_play = ui.Button(">> เล่นรวด", x=368, y=284, w=152, h=88, color=0x30A46C,
                     value=20)
btn_home = ui.Button("เริ่มใหม่", x=552, y=284, w=128, h=88, color=0x4A9EFF,
                     value=20)
ID_PREV, ID_NEXT = btn_prev.id(), btn_next.id()
ID_PLAY, ID_HOME = btn_play.id(), btn_home.id()

lcd.clear()
lcd.console("<h2>median เทียบ ค่าเฉลี่ย</h2>")
lcd.print("ฐาน", BASE, "| spike", SPIKE, "| spike ติดกัน", BURST, "ตัว")
lcd.print("<span class=muted>กดเดินหน้าเพื่อขยายหน้าต่าง N "
          "- ระหว่างเล่นรวด กดปุ่มไหนก็หยุด</span>")

i = 0                   # ท่าปัจจุบัน - ความจริงของโปรแกรมอยู่ที่ตัวนี้
playing = False
t_next = 0


def show():
    window = WINDOWS[i]
    mean_buf = []
    med = dsp.Median(window=window)
    worst_mean = 0.0
    worst_med = 0.0

    # ---- 1) คิดให้จบก่อน ยังไม่แตะจอเลยสักคำสั่ง ----
    # คิดครบทั้ง 50 จุดแล้วเก็บไว้ในลิสต์ ตรงนี้ไม่มีคำสั่งไปหาคอร์จอแม้แต่คำสั่งเดียว
    pts_raw = []
    pts_mean = []
    pts_med = []
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

        pts_raw.append(int(x * 10.0))
        pts_mean.append(int(m * 10.0))
        pts_med.append(int(mv * 10.0))

    tolerated = (window - 1) // 2

    # ---- 2) รายงานตัวเลขทั้งหมดก่อน แล้วค่อยวาดกราฟทีหลัง ----
    # ป้ายมุมขวาบนบอกทั้งตำแหน่งและสถานะ "กำลังเล่นรวด" ในบรรทัดเดียว
    # จอนี้ไม่มีที่ให้ป้ายใบ้แยกอีกใบ และสองเรื่องนี้เป็นเรื่องเดียวกันอยู่แล้ว
    lbl_pos.text(("เล่นรวด %d / %d" if playing else "ท่า %d / %d")
                 % (i + 1, len(WINDOWS)))
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

    # ---- 3) ค่อยเทจุดลงกราฟเป็นอย่างสุดท้าย ----
    # ลำดับสองขั้นบนสลับกันไม่ได้ และนี่คือกับดักที่มองไม่เห็นจากโค้ด
    # คำสั่ง set_next() 150 ครั้งรวดเดียวถมคิว IPC จนเต็ม คำสั่งที่ต่อคิวอยู่ข้างหลัง
    # ถูกทิ้งเงียบ ๆ ไม่มี error ไม่มีค่าคืนมาให้เช็ก เพราะมันเป็นแบบยิงแล้วลืม
    # เดิมไฟล์นี้วาดกราฟก่อนแล้วค่อยสั่งเปลี่ยนข้อความ ผลคือป้ายสามใบค้างอยู่ที่
    # ค่าตั้งต้นและ Seg7 ค้างที่ 0000 ตลอดกาล - เห็นได้จากภาพเรนเดอร์เท่านั้น
    for k in range(len(pts_raw)):
        ch.set_next(s_raw, pts_raw[k])
        ch.set_next(s_mean, pts_mean[k])
        ch.set_next(s_med, pts_med[k])

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
