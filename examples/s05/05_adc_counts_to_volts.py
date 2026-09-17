# 05_adc_counts_to_volts.py - เลขดิบจาก ADC ไม่ใช่แรงดัน มันคือจำนวนขั้น
# ชุดตัวอย่างประจำคาบ 05
#
# ไฟล์นี้สอน: การแปลงเลขดิบเป็นโวลต์ต้องรู้แรงดันเต็มสเกลกับจำนวนบิตเสมอ
# ดูที่จอ   : เส้นฟ้าเรียบ ๆ คือแรงดันจริง เส้นแดงเป็นบันไดคือค่าที่ ADC เห็น
#             กดเดินหน้าแล้วดูบันไดค่อย ๆ หยาบขึ้นตามจำนวนบิตที่ลดลง
# กับดัก    : ตัวส่วนมีสองตัว 2**n - 1 ใช้ตอน map เลขดิบเป็นแรงดัน ส่วน 2**n
#             ใช้ตอนหาขนาดขั้น สลับกันเมื่อไรจะเพี้ยนนิดเดียวจนจับไม่ได้

import lcd
import math
import time
import ui

# แรงดันเต็มสเกลที่ "สมมติ" ไว้สำหรับบทเรียนนี้ - ยังไม่มีใครวัดแรงดันอ้างอิงของลูกบิด
# บนบอร์ดจริงทั้งสองรุ่น (sensors.pot.voltage() ในเฟิร์มแวร์ก็คูณด้วย 3.3 โดยไม่ได้วัดเช่นกัน)
# สัญญาณในไฟล์นี้เราสร้างเองทั้งหมด ตัวเลขนี้จึงเป็นแค่สเกลของกราฟ ไม่ใช่ข้อเท็จจริงของบอร์ด
V_FULL_SCALE = 1.8          # โวลต์ (สมมติ)
N_BITS_API = 16             # sensors.pot.read() คืน 0-65535 ทั้งสองบอร์ด จึงเป็น 16 บิตของ API
N_POINTS = 50               # เท่ากับหน้าต่างของ ui.Chart พอดี
PLAY_MS = 1300

FULL_COUNT = (1 << N_BITS_API) - 1      # 65535 คือค่าสูงสุดที่อ่านได้ ไม่ใช่ 65536

# หนึ่งขั้นคือ (บิตที่ใช้จับขั้นจริง, บิตที่ตัวเลขอ้างว่าเป็น, หมายเหตุ)
# ขั้นสุดท้ายจงใจให้สองค่าไม่ตรงกัน เพื่อโชว์ว่าตัวเลขสวยขึ้นได้โดยไม่ละเอียดขึ้น
STEPS = [(16, 16, "ความละเอียดของ API บนบอร์ดนี้"),
         (12, 12, "SAR ADC ทั่วไปบน MCU"),
         (10, 10, "เริ่มเห็นบันไดด้วยตาเปล่า"),
         (8, 8, "บันไดชัดแล้ว ยังพอใช้งานได้"),
         (5, 5, "หยาบจนรูปทรงเริ่มเพี้ยน"),
         (3, 3, "หยาบมาก เห็นขั้นเต็ม ๆ"),
         (12, 16, "เลข 12 บิตถูกสเกลขึ้นเป็น 16 บิต")]


def counts_to_volts(raw, bits, v_fs):
    # ใช้ 2**bits - 1 เพราะ raw สูงสุดคือค่านั้น ไม่ใช่ 2**bits
    return raw / float((1 << bits) - 1) * v_fs


def lsb_size(bits, v_fs):
    # ขนาดของหนึ่งขั้น ใช้ 2**bits เพราะช่วงเต็มถูกหั่นเป็น 2**bits ช่วงเท่า ๆ กัน
    return v_fs / float(1 << bits)


def quantise(v, bits, v_fs):
    # นี่คือสิ่งที่ ADC ทำจริง ๆ หารด้วยขนาดขั้น ปัดเข้าขั้นที่ใกล้ที่สุด แล้วคูณกลับ
    step_v = lsb_size(bits, v_fs)
    return round(v / step_v) * step_v


def true_volts(n):
    # สัญญาณต่อเนื่องที่เราสร้างเอง จึงรู้ค่าที่ถูกต้องแน่นอน ไม่ต้องเดา
    return V_FULL_SCALE * (0.5 + 0.48 * math.sin(n * 2.0 * math.pi / 34.0))


ui.screen()
time.sleep_ms(200)

ui.Label("ADC: เลขดิบ ไม่ใช่โวลต์", x=12, y=8, value=20)
lbl_pos = ui.Label("1 / 7", x=472, y=8, value=20, color=0x30A46C)

# ซีรีส์ 0 มากับ Chart และถูกวาดก่อน จึงยกให้เป็นบันไดของ ADC
# ส่วนเส้นจริงเพิ่มทีหลังเพื่อให้วาดทับ จะได้ยังมองเห็นแม้ตอนบันไดละเอียดจนซ้อนกัน
ch = ui.Chart(x=12, y=36, w=420, h=156, min=0, max=1800, color=0xE5484D)
s_adc = 0
s_real = ch.add_series(0x00BFFF)

ui.Label("ฟ้า = แรงดันจริง", x=448, y=40, value=16, color=0x4A9EFF)
ui.Label("แดง = ที่ ADC เห็น", x=448, y=64, value=16, color=0xE5484D)
lbl_bits = ui.Label("16 บิต", x=448, y=92, value=24, color=0xF5A623)
ui.Label("ขนาดขั้น (uV)", x=448, y=124, value=16)
seg_step = ui.Seg7(x=600, y=144, w=152, h=40)

lbl_note = ui.Label("กดเดินหน้าเพื่อลดจำนวนบิต", x=12, y=200, value=16)
# ความคลาดเคลื่อนที่วัดได้ กับครึ่งขั้นตามทฤษฎี วางบรรทัดเดียวกันเพื่อให้เทียบได้เลย
lbl_err = ui.Label("ยังไม่ได้วัด", x=12, y=224, value=16, color=0xF5A623)

btn_prev = ui.Button("< ย้อน", x=20, y=252, w=140, h=88, color=0x9AA3AF, value=20)
btn_next = ui.Button("เดินหน้า >", x=176, y=252, w=160, h=88, color=0x4A9EFF,
                     value=20)
btn_play = ui.Button(">> เล่นรวด", x=352, y=252, w=160, h=88, color=0x30A46C,
                     value=20)
btn_home = ui.Button("เริ่มใหม่", x=528, y=252, w=140, h=88, color=0x4A9EFF,
                     value=20)
ID_PREV, ID_NEXT = btn_prev.id(), btn_next.id()
ID_PLAY, ID_HOME = btn_play.id(), btn_home.id()

lbl_hint = ui.Label("กดเดินหน้าเพื่อลดจำนวนบิต", x=344, y=348, value=16,
                    color=0x9AA3AF)

lcd.clear()
lcd.console("<h2>ADC: นับขั้น ไม่ใช่โวลต์</h2>")
lcd.print("เต็มสเกล (สมมติ)", V_FULL_SCALE, "V | API", N_BITS_API, "บิต")

step = 0                # ขั้นปัจจุบัน - ความจริงของโปรแกรมอยู่ที่ตัวนี้
playing = False
t_next = 0


def show():
    real_bits, claimed_bits, note = STEPS[step]

    # ป้อนครบ 50 จุดในขั้นเดียว หน้าต่างของ Chart กว้าง 50 พอดี ภาพเดิมจึงถูกแทนที่หมด
    worst_uv = 0.0
    for n in range(N_POINTS):
        v = true_volts(n)
        stepped = quantise(v, real_bits, V_FULL_SCALE)
        err_uv = abs(v - stepped) * 1e6
        if err_uv > worst_uv:
            worst_uv = err_uv
        ch.set_next(s_real, int(v * 1000.0))
        ch.set_next(s_adc, int(stepped * 1000.0))

    lsb_uv = int(lsb_size(real_bits, V_FULL_SCALE) * 1e6)

    lbl_pos.text("%d / %d" % (step + 1, len(STEPS)))
    if real_bits == claimed_bits:
        lbl_bits.text("%d บิต" % real_bits)
    else:
        lbl_bits.text("%d -> %d บิต" % (real_bits, claimed_bits))
    seg_step.text(str(lsb_uv))
    lbl_note.text(note)

    # ความคลาดเคลื่อนจากการปัดเข้าขั้น ไม่มีวันเกินครึ่งขั้น นั่นเป็นขอบเขต
    # ทางคณิตศาสตร์ ไม่ใช่คุณภาพของชิป บรรทัดนี้คือหลักฐานว่ามันจริง
    lbl_err.text("ผิดสุด %d uV | ครึ่งขั้น %d uV" % (int(worst_uv), lsb_uv // 2))
    lbl_hint.text("กำลังเล่นรวด - กดปุ่มไหนก็หยุด" if playing
                  else "กดเดินหน้าเพื่อลดจำนวนบิต")
    lcd.print(str(real_bits) + " บิต | ขั้น " + str(lsb_uv) + " uV | ผิดสุด " +
              str(int(worst_uv)) + " uV")


show()

# ตอบคำถามที่นักศึกษาถามบ่อยที่สุด: 32768 คือกี่โวลต์ ส่งออก serial ไว้อ่านทีหลัง
# คำตอบขึ้นกับ V_FS ที่สมมติ - เปลี่ยนตัวเลขบนสุดของไฟล์ ตารางนี้ก็เปลี่ยนตาม
print("V_FS = %.1f V (assumed, not measured), %d bit, full count = %d"
      % (V_FULL_SCALE, N_BITS_API, FULL_COUNT))
for raw in (0, 16384, 32768, 49152, 65535):
    print("raw %-6d = %.4f V" % (raw, counts_to_volts(raw, N_BITS_API,
                                                      V_FULL_SCALE)))

while True:
    for ev in ui.poll():
        if ev["type"] != "clicked":
            continue
        handle = ev["handle"]
        if handle == ID_PREV:
            playing = False
            step = (step - 1) % len(STEPS)
        elif handle == ID_NEXT:
            playing = False
            step = (step + 1) % len(STEPS)
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
        if step >= len(STEPS):
            step = len(STEPS) - 1
            playing = False
        t_next = time.ticks_add(time.ticks_ms(), PLAY_MS)
        show()

    time.sleep_ms(30)
