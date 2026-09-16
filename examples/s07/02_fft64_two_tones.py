# 02_fft64_two_tones.py - FFT radix-2 เขียนเองทั้งตัว 64 จุด
# ชุดตัวอย่างประจำคาบ 07
#
# ไฟล์นี้สอน: FFT คือการมองสัญญาณเดียวกันในมุม "มีความถี่อะไรผสมอยู่บ้าง"
#             บอร์ดนี้ไม่มี FFT มาให้ ไม่มี ulab จึงเขียนเอง ได้จริงในสามสิบบรรทัด
# ดูที่จอ   : กราฟซ้ายคือสัญญาณในโดเมนเวลา ตารางไฟตรงกลางคือสเปกตรัมของมัน
#             ขั้นแรกใส่โทนเดียวได้แท่งเดียว ขั้นที่สามใส่สองโทนได้สองแท่ง
#             และแท่งที่สองเตี้ยกว่าครึ่งหนึ่งพอดี เพราะเราใส่ขนาดครึ่งเดียวจริง ๆ
# กับดัก    : bin ไม่ใช่ Hz ต้องคูณด้วย fs/N เอง และครึ่งบนของสเปกตรัมเป็นเงา
#             ของครึ่งล่าง จึงพล็อตแค่ถึง N/2 เท่านั้น

import lcd
import math
import time
import ui

N = 64                          # ต้องเป็นกำลังของสอง เพราะเป็น radix-2
FS = 64.0                       # Hz สมมติว่าเก็บได้ 64 ตัวอย่างต่อวินาที
COLS = 16                       # ตารางไฟกว้างสุด 16 ดอก จึงวาดได้ 16 bin แรก
ROWS = 8                        # ความสูงของแท่ง หยาบเป็น 8 ขั้น
PLAY_MS = 1600

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
# จอนี้ไม่มีสถานะปกติ/ผิดปกติให้รายงาน จึงไม่มีเขียว ส้ม แดง อยู่เลยสักจุด
COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF

# หนึ่งขั้นคือ (ชื่อ, ((รอบต่อหน้าต่าง, ขนาด), ...), ใส่ noise หรือไม่)
SIGNALS = (("โทนเดียว 3 รอบ", ((3, 1.0),), False),
           ("โทนเดียว 10 รอบ", ((10, 1.0),), False),
           ("ผสมสองโทน 3 + 10", ((3, 1.0), (10, 0.5)), False),
           ("ผสมสองโทน + noise", ((3, 1.0), (10, 0.5)), True),
           ("โทนเดียว 15 รอบ", ((15, 1.0),), False))


def bit_reverse(i, bits):
    # จัดลำดับใหม่ก่อนเริ่ม เพราะการหั่นคู่-คี่ซ้ำ ๆ ทำให้ลำดับสลับแบบกลับบิตพอดี
    r = 0
    for _ in range(bits):
        r = (r << 1) | (i & 1)
        i >>= 1
    return r


def fft(re, im):
    n = len(re)
    bits = 0
    while (1 << bits) < n:
        bits += 1
    for i in range(n):
        j = bit_reverse(i, bits)
        if j > i:
            re[i], re[j] = re[j], re[i]
            im[i], im[j] = im[j], im[i]
    size = 2
    while size <= n:                     # รวมกลับทีละชั้น 2, 4, 8, ... , N
        step = -2.0 * math.pi / size
        half = size >> 1
        for start in range(0, n, size):
            for k in range(half):
                wr, wi = math.cos(step * k), math.sin(step * k)
                a, b = start + k, start + k + half
                tr = wr * re[b] - wi * im[b]     # butterfly หนึ่งตัว
                ti = wr * im[b] + wi * re[b]
                re[b], im[b] = re[a] - tr, im[a] - ti
                re[a], im[a] = re[a] + tr, im[a] + ti
        size <<= 1


def make_signal(tones, noisy):
    # สร้างสัญญาณเอง เพราะเรารู้คำตอบที่ถูกไว้ล่วงหน้า จะได้ตัดสินได้ว่า FFT ถูก
    out = []
    for i in range(N):
        v = 0.0
        for cycles, amp in tones:
            v += amp * math.cos(2 * math.pi * cycles * i / N)
        if noisy:
            # noise แบบคำนวณได้ ไม่ใช่สุ่มจริง ตัวเลขจึงซ้ำได้ทุกครั้งที่รัน
            v += 0.25 * math.sin(i * 12.9898) * math.cos(i * 4.1414)
        out.append(v)
    return out


def spectrum_bytes(heights):
    # ก่อภาพแท่งสำหรับ ui.DotMatrix ซึ่งรับ "ไบต์ที่แพ็กแล้ว" หนึ่งบิตต่อหนึ่งดอก
    # เรียงตามแถวก่อน และภายในแถวเรียงบิตแบบ MSB ก่อน 16 คอลัมน์กินแถวละ 2 ไบต์
    out = bytearray()
    for r in range(ROWS):
        word = 0
        for c in range(COLS):
            # แท่งก่อจากล่างขึ้นบน แถว 0 อยู่บนสุด จึงต้องกลับด้านตอนเทียบ
            word = (word << 1) | (1 if heights[c] >= (ROWS - r) else 0)
        out.append((word >> 8) & 0xFF)
        out.append(word & 0xFF)
    return bytes(out)


ui.screen()
time.sleep_ms(200)

# ผังจอเดินบนกริด 8 ขอบนอก 24 - หัวเรื่องหนึ่งบรรทัด แถวคำอธิบายสัญญาณ
# แถวภาพสองมุมมอง (เวลา กับ ความถี่) แถวปุ่มสี่ปุ่ม และแถบสรุปล่างสุด
ui.Label("FFT 64 จุด เขียนเอง", x=24, y=8, color=COL_TEXT, value=28)
lbl_pos = ui.Label("1 / 5", x=696, y=8, color=COL_DIM, value=24)
lbl_sig = ui.Label("กำลังเริ่ม", x=24, y=56, color=COL_TEXT, value=24)
# ป้ายโหมดเล่นรวดอยู่แถวเดียวกับชื่อสัญญาณ เพราะแถบล่างสุดถูกจองให้ผลของ FFT
lbl_hint = ui.Label("กดเดินหน้าเพื่อเปลี่ยนสัญญาณ", x=424, y=56, color=COL_DIM,
                    value=20)

ui.Label("สัญญาณตามเวลา", x=24, y=96, color=COL_DIM, value=20)
ui.Label("สเปกตรัม 16 bin", x=312, y=96, color=COL_DIM, value=20)
ui.Label("bin สูงสุด", x=520, y=96, color=COL_DIM, value=20)
ui.Label("คิดเป็น Hz", x=648, y=96, color=COL_DIM, value=20)

# เส้นเดียวบนกราฟนี้คือสัญญาณที่เราใส่เข้าไป จึงเป็นสีเน้นของจอ
ch_t = ui.Chart(x=24, y=128, w=272, h=104, color=COL_CARD, min=-200, max=200)
s_time = ch_t.add_series(COL_ACCENT)
# w กับ h คือกล่องพิกเซล ส่วน cols กับ rows คือจำนวนดอก 192/16 ได้ดอกละ 12 px
# และ 104/8 ได้ 13 เฟิร์มแวร์เลือกค่าน้อยกว่าเป็นระยะพิทช์ ดอกจึงเป็นจัตุรัส
dots = ui.DotMatrix(x=312, y=128, w=192, h=104, cols=COLS, rows=ROWS)

# Seg7 ใช้ฟอนต์ตายตัว 28 ของมันเอง ขนาดตัวเลขคุมด้วย h ไม่ใช่ value
seg_bin = ui.Seg7(x=520, y=128, w=112, h=48, color=COL_ACCENT)
seg_hz = ui.Seg7(x=648, y=128, w=112, h=48, color=COL_ACCENT)
lbl_res = ui.Label("1 bin = fs/N Hz", x=520, y=184, color=COL_DIM, value=20)

# แถวปุ่ม h=88 ตามเกณฑ์เป้าสัมผัส เว้นระหว่างปุ่ม 32 และจบที่ y=336
# ปุ่มขวาสุดล้ำเข้าคอลัมน์ x>690 ได้ เพราะมันจบก่อน y=340 ที่ปุ่ม Console เริ่ม
btn_prev = ui.Button("< ย้อน", x=24, y=248, w=168, h=88, color=0x3A4150,
                     value=24)
btn_next = ui.Button("เดินหน้า >", x=224, y=248, w=168, h=88, color=COL_ACCENT,
                     value=24)
btn_play = ui.Button(">> เล่นรวด", x=424, y=248, w=168, h=88, color=COL_ACCENT,
                     value=24)
btn_home = ui.Button("เริ่มใหม่", x=624, y=248, w=144, h=88, color=0x3A4150,
                     value=24)
ID_PREV, ID_NEXT = btn_prev.id(), btn_next.id()
ID_PLAY, ID_HOME = btn_play.id(), btn_home.id()

lbl_note = ui.Label("กดเดินหน้าเพื่อเปลี่ยนสัญญาณ", x=24, y=352, color=COL_DIM,
                    value=20)

stages = 0                      # นับชั้นด้วยการเลื่อนบิต ไม่พึ่ง log ของ float
while (1 << stages) < N:
    stages += 1
SPEEDUP = float(N * N) / (N * stages // 2)

lcd.clear()
lcd.console("<h2>FFT 64 จุด เขียนเอง</h2>")
lcd.print("N =", N, "| fs =", int(FS), "Hz |", len(SIGNALS), "สัญญาณ")

step = 0                # ขั้นปัจจุบัน - ความจริงของโปรแกรมอยู่ที่ตัวนี้
playing = False
t_next = 0


def show():
    name, tones, noisy = SIGNALS[step]
    sig = make_signal(tones, noisy)

    # ป้อนครบ 50 จุด หน้าต่างของ Chart กว้าง 50 พอดี ภาพเดิมจึงถูกแทนที่หมด
    # FFT ข้างล่างยังใช้ครบทั้ง 64 ตัวอย่าง ไม่ได้ตัดข้อมูลทิ้ง
    for n in range(50):
        ch_t.set_next(s_time, int(sig[n] * 100.0))

    re = list(sig)
    im = [0.0] * N
    fft(re, im)

    # ขนาดของแต่ละ bin คูณ 2/N เพื่อให้อ่านเป็นแอมพลิจูดของคลื่นเดิมได้ตรง ๆ
    mags = [math.sqrt(re[k] * re[k] + im[k] * im[k]) * 2.0 / N
            for k in range(N // 2)]

    peak_bin = 0
    for k in range(N // 2):
        if mags[k] > mags[peak_bin]:
            peak_bin = k

    heights = []
    for c in range(COLS):
        # ปัดขึ้นหนึ่งขั้นเสมอถ้ามีค่า จะได้ไม่หายไปทั้งแท่งเพราะปัดลง
        h = int(mags[c] * ROWS + 0.5)
        heights.append(h if h else (1 if mags[c] > 0.02 else 0))
    dots.set_pixels(spectrum_bytes(heights))

    lbl_pos.text("%d / %d" % (step + 1, len(SIGNALS)))
    lbl_sig.text("ใส่เข้าไป: " + name)
    seg_bin.text(str(peak_bin))
    seg_hz.text(str(int(peak_bin * FS / N)))
    # ป้ายสั้นลงเพราะคอลัมน์ขวากว้าง 248 ประโยคเต็มยังอยู่ครบใน print() ข้างล่าง
    lbl_res.text("1 bin = %.3f Hz" % (FS / N))
    if peak_bin >= COLS:
        lbl_note.text("ยอดอยู่ที่ bin %d ซึ่งเกิน %d คอลัมน์ที่ตารางวาดได้"
                      % (peak_bin, COLS))
    else:
        lbl_note.text("ยอดสูงสุด %.2f | FFT เร็วกว่า DFT %d เท่า"
                      % (mags[peak_bin], int(SPEEDUP)))
    lbl_hint.text("กำลังเล่นรวด - กดปุ่มไหนก็หยุด" if playing
                  else "กดเดินหน้าเพื่อเปลี่ยนสัญญาณ")
    lcd.print(name + " -> ยอดที่ bin " + str(peak_bin) + " = " +
              str(int(peak_bin * FS / N)) + " Hz")


show()

# ตัวเลขสองตัวนี้คือเหตุผลทั้งหมดที่ FFT มีอยู่ ส่งออก serial ไว้อ่านทีหลัง
print("ความละเอียดต่อ bin = fs/N = %.3f Hz" % (FS / N))
print("คูณเชิงซ้อน FFT ~ %d  เทียบ DFT ตรง ๆ %d ครั้ง  (เร็วขึ้น %.0f เท่า)"
      % (N * stages // 2, N * N, SPEEDUP))

while True:
    for ev in ui.poll():
        if ev["type"] != "clicked":
            continue
        handle = ev["handle"]
        if handle == ID_PREV:
            playing = False
            step = (step - 1) % len(SIGNALS)
        elif handle == ID_NEXT:
            playing = False
            step = (step + 1) % len(SIGNALS)
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
        if step >= len(SIGNALS):
            step = len(SIGNALS) - 1
            playing = False
        t_next = time.ticks_add(time.ticks_ms(), PLAY_MS)
        show()

    time.sleep_ms(30)
