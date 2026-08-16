# 02_confirm_n.py - ต้องเห็นติดกันกี่รอบถึงจะเชื่อ
#
# ไฟล์นี้สอน: ระดับใหม่ต้องยืนพื้นครบ N รอบติดกันก่อนจึงเปลี่ยนสถานะจริง
#             ค่าที่กระโดดวูบเดียวไม่ใช่เหตุการณ์ มันคือสัญญาณรบกวน
# ดูที่จอ   : ป้ายสองใบ ซ้ายคือ "เชื่อทันที" ขวาคือ "ยืนยัน 3 รอบ" ตอนค่ากระโดด
#             วูบเดียว ป้ายซ้ายแดง ป้ายขวายังเขียว - นั่นคือบทเรียนทั้งไฟล์
#             แถบล่างคือ streak ที่กำลังสะสม
# กับดัก    : ราคาที่จ่ายคือเตือนช้าลง N คูณคาบลูป ตัวเลขนี้ต้องตอบให้ได้ว่า
#             "ช้าไปกี่วินาที" ไม่ใช่ตั้งให้ใหญ่ไว้ก่อน

import lcd
import time
import ui

ALERT_LIMIT = 15.0
CONFIRM_N = 3           # ต้องเห็นระดับใหม่ติดกันกี่รอบจึงจะเชื่อ
LOOP_MS = 250           # 3 รอบ x 250 ms = ช้าไป 0.75 วินาที ซึ่งรับได้
CHART_MAX = 320         # กราฟรับจำนวนเต็ม จึงคูณสิบก่อนใส่ (0.0-32.0 -> 0-320)

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_BAD = 0x30A46C, 0xE5484D

STATE_COLOR = {"OK": COL_OK, "ALERT": COL_BAD}

# ค่าจำลองที่มีทั้งของจริงและของปลอมปนกัน
# รอบที่ 3 กับ 8 คือค่ากระโดดวูบเดียว ส่วนช่วงท้ายคือของจริงที่ค้างอยู่
SERIES = (1.0, 2.0, 3.0, 22.0, 2.0, 3.0, 1.0, 2.0, 30.0,
          2.0, 18.0, 19.0, 20.0, 21.0, 18.0, 3.0, 2.0)


def level_of(value):
    return "ALERT" if value > ALERT_LIMIT else "OK"


ui.screen()
time.sleep_ms(200)

# ผังจอ: การ์ดใบเดียวสามช่อง - สองช่องซ้ายคือสองกฎที่เทียบกัน ช่องขวาคือค่าดิบ
# แล้วกราฟหนึ่งช่อง และสองบรรทัดล่างสุดที่จบก่อน x=690 (ที่ของปุ่ม Console)
ui.Label("คาบ 12 - ยืนยันกี่รอบถึงจะเชื่อ", x=24, y=8, color=COL_TEXT, value=24)
ui.Panel(x=24, y=56, w=744, h=168, color=COL_CARD, min=COL_CARD, max=12,
         value=1)

# สองฝั่งวางเทียบกัน ระยะห่างเท่ากัน สีเดียวกัน ต่างกันแค่กฎที่ใช้ตัดสิน
ui.Label("เชื่อทันที", x=40, y=72, color=COL_DIM, value=16)
l_naive = ui.Label("OK", x=40, y=104, color=COL_OK, value=28)
ui.Label("เปลี่ยนแล้ว", x=40, y=160, color=COL_DIM, value=16)
# ตัวนับสองตัวนี้เคยเป็นแดงกับเขียว ทั้งที่มันคือ "จำนวนครั้ง" ไม่ใช่สถานะ
# สีสถานะที่เอามาแต่งตัวเลขทำให้ตาอ่านว่าฝั่งหนึ่งผิดฝั่งหนึ่งถูกตลอดเวลา
# ทั้งที่บทเรียนคือให้เทียบตัวเลขกันเอง จึงใช้สีข้อความปกติทั้งคู่ (S7.7.2)
seg_naive = ui.Seg7("0", x=176, y=152, w=88, h=56, color=COL_TEXT)

ui.Label("ยืนยัน 3 รอบ", x=288, y=72, color=COL_DIM, value=16)
l_conf = ui.Label("OK", x=288, y=104, color=COL_OK, value=28)
ui.Label("เปลี่ยนแล้ว", x=288, y=160, color=COL_DIM, value=16)
seg_conf = ui.Seg7("0", x=424, y=152, w=88, h=56, color=COL_TEXT)

ui.Label("ค่าตอนนี้", x=536, y=72, color=COL_DIM, value=16)
seg_val = ui.Seg7("0.0", x=536, y=104, w=192, h=48, color=COL_ACCENT)
ui.Label("streak", x=536, y=160, color=COL_DIM, value=16)
bar_streak = ui.Bar(x=616, y=156, w=128, h=32, min=0, max=CONFIRM_N, value=0)

ch = ui.Chart(x=24, y=240, w=744, h=88, color=COL_CARD, min=0, max=CHART_MAX)
s_val = ch.add_series(COL_ACCENT)
s_lim = ch.add_series(COL_BAD)

# ป้ายกำกับเส้น - เส้นเกณฑ์แบนอยู่ที่ค่าคงที่ ป้ายจึงชี้เส้นได้แม้ภาพเป็นขาวดำ
ui.Label("เส้นฟ้า = ค่าที่วัดได้", x=24, y=336, color=COL_ACCENT, value=16)
ui.Label("เส้นแดง = เกณฑ์ ALERT", x=288, y=336, color=COL_BAD, value=16)
# ข้อสังเกตยืนพื้นอยู่ในตัว l_foot เอง แล้วถูกเขียนทับด้วยสรุปตอนจบ
l_foot = ui.Label("ดูจังหวะที่ป้ายสองใบไม่ตรงกัน", x=24, y=368, color=COL_DIM,
                  value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 12 - เชื่อทันที เทียบกับ ยืนยัน 3 รอบ</h2>")

# --- ฝั่งซ้าย: เชื่อทันทีที่เห็น ---
naive_state = "OK"
naive_changes = 0

# --- ฝั่งขวา: ต้องยืนยันให้ครบก่อน ---
state = "OK"
pending = "OK"      # ระดับที่กำลังจะเชื่อ ถ้ามันยืนพื้นได้ครบ CONFIRM_N รอบ
streak = 0
changes = 0

for i in range(len(SERIES)):
    value = SERIES[i]
    level = level_of(value)

    if level != naive_state:
        naive_state = level
        naive_changes += 1
        l_naive.text(naive_state)
        l_naive.color(STATE_COLOR[naive_state])
        seg_naive.text(str(naive_changes))

    # นับว่าระดับเดิมยืนพื้นมากี่รอบติดกัน เจอระดับใหม่เมื่อไรให้เริ่มนับหนึ่งใหม่
    if level == pending:
        streak += 1
    else:
        pending = level
        streak = 1

    # เปลี่ยนสถานะก็ต่อเมื่อยืนยันครบ และระดับที่ยืนยันได้ต่างจากสถานะปัจจุบันจริง
    if streak >= CONFIRM_N and pending != state:
        state = pending
        changes += 1
        l_conf.text(state)
        l_conf.color(STATE_COLOR[state])
        seg_conf.text(str(changes))
        lcd.print("<b>ยืนยันแล้ว</b> รอบ {} - เปลี่ยนเป็น {}".format(i, state))

    seg_val.text("{:.1f}".format(value))
    bar_streak.value(streak if streak < CONFIRM_N else CONFIRM_N)
    ch.set_next(s_val, int(value * 10))
    ch.set_next(s_lim, int(ALERT_LIMIT * 10))

    # บันทึกเฉพาะรอบที่สองฝั่งไม่ตรงกัน เพราะนั่นคือรอบที่การยืนยันทำงานอยู่
    if naive_state != state:
        lcd.print("<span class=warn>รอบ {} - ทันที {} - ยืนยัน {}</span>".format(
            i, naive_state, state))

    ui.poll()
    time.sleep_ms(LOOP_MS)

# เขียนทับด้วยข้อความที่กว้างพอ ๆ กับของเดิม ป้ายที่ยาวขึ้นตอนจบจะยื่นไปทับ
# ของข้าง ๆ ทั้งที่ตอนสร้างวางไว้ห่างดีแล้ว - ส่วนต่างคือคำตอบของทั้งไฟล์
l_foot.text("ทันที {} ครั้ง - ยืนยัน {} ครั้ง - ต่างกัน {} สาย".format(
    naive_changes, changes, naive_changes - changes))
l_foot.color(COL_TEXT)

lcd.print("<span class=error>เชื่อทันที เปลี่ยน {} ครั้ง</span>".format(naive_changes))
lcd.print("<span class=ok>ยืนยัน {} รอบ เปลี่ยน {} ครั้ง</span>".format(CONFIRM_N, changes))
lcd.print("ส่วนต่างคือสายที่ไม่ต้องโทรกลางดึก")
lcd.print("<span class=muted>ราคาที่จ่าย: ช้าลง {} ms</span>".format(CONFIRM_N * LOOP_MS))

# ค้างจอไว้ให้เห็นผลสุดท้าย ตัวเลขสองตัวบนการ์ดคือคำตอบของทั้งไฟล์
for _ in range(20):
    ui.poll()
    time.sleep_ms(100)
