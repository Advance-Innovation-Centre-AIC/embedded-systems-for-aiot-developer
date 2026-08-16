# 02_fft64_two_tones.py - FFT radix-2 เขียนเองทั้งตัว 64 จุด
# ชุดตัวอย่างประจำคาบ 07
#
# ไฟล์นี้สอน: FFT คือการมองสัญญาณเดียวกันในมุม "มีความถี่อะไรผสมอยู่บ้าง"
#             บอร์ดนี้ไม่มี FFT มาให้ ไม่มี ulab จึงเขียนเอง ได้จริงในสามสิบบรรทัด
# ดูที่จอ   : กราฟบนคือสัญญาณในโดเมนเวลา ตารางไฟข้างล่างคือสเปกตรัมของมัน
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

ui.Label("FFT 64 จุด เขียนเอง", x=12, y=8, value=20)
lbl_pos = ui.Label("1 / 5", x=470, y=8, value=20, color=0x50D890)
lbl_sig = ui.Label("กำลังเริ่ม", x=12, y=34, value=16, color=0xFFD24A)
ch_t = ui.Chart(x=12, y=54, w=670, h=58, min=-200, max=200)
# w กับ h คือกล่องพิกเซล ส่วน cols กับ rows คือจำนวนดอก กว้าง 240 กับ 16 คอลัมน์
# ได้ดอกละ 15 px ซึ่งเกือบจัตุรัสพอดีกับ 126/8 และสำคัญกว่านั้นคือมันจบที่ x=252
# เหลือคอลัมน์ขวาให้ Seg7 กับป้าย ไม่ถูกตารางไฟวาดทับ
dots = ui.DotMatrix(x=12, y=118, w=240, h=126, cols=COLS, rows=ROWS)

ui.Label("bin ยอดสูงสุด", x=270, y=120, value=14)
seg_bin = ui.Seg7(x=270, y=138, w=140, h=40)
ui.Label("คิดเป็นความถี่ (Hz)", x=430, y=120, value=14)
seg_hz = ui.Seg7(x=430, y=138, w=140, h=40)
lbl_res = ui.Label("ความละเอียดต่อ bin = fs/N", x=270, y=186, value=16)
lbl_note = ui.Label("กดเดินหน้าเพื่อเปลี่ยนสัญญาณ", x=270, y=212, value=16)

btn_prev = ui.Button("< ย้อน", x=20, y=250, w=140, h=64, color=0x546E7A, value=20)
btn_next = ui.Button("เดินหน้า >", x=176, y=250, w=160, h=64, color=0x1E88E5, value=20)
btn_play = ui.Button(">> เล่นรวด", x=352, y=250, w=160, h=64, color=0x2E7D32, value=20)
btn_home = ui.Button("เริ่มใหม่", x=528, y=250, w=140, h=64, color=0x6A1B9A, value=20)
ID_PREV, ID_NEXT = btn_prev.id(), btn_next.id()
ID_PLAY, ID_HOME = btn_play.id(), btn_home.id()

lbl_hint = ui.Label("กดเดินหน้าเพื่อเปลี่ยนสัญญาณ", x=20, y=322, value=16,
                    color=0x90A4AE)

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
        ch_t.set_next(0, int(sig[n] * 100.0))

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
    lbl_res.text("ความละเอียดต่อ bin = fs/N = %.3f Hz" % (FS / N))
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
