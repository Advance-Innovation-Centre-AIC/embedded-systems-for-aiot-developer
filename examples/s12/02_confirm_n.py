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

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x00E676, 0xFFA726, 0xFF5252, 0x40C4FF

STATE_COLOR = {"OK": COL_OK, "ALERT": COL_BAD}

# ค่าจำลองที่มีทั้งของจริงและของปลอมปนกัน
# รอบที่ 3 กับ 8 คือค่ากระโดดวูบเดียว ส่วนช่วงท้ายคือของจริงที่ค้างอยู่
SERIES = (1.0, 2.0, 3.0, 22.0, 2.0, 3.0, 1.0, 2.0, 30.0,
          2.0, 18.0, 19.0, 20.0, 21.0, 18.0, 3.0, 2.0)


def level_of(value):
    return "ALERT" if value > ALERT_LIMIT else "OK"


ui.screen()
time.sleep_ms(200)

ui.Label("คาบ 12 - ยืนยันกี่รอบถึงจะเชื่อ", x=20, y=10, color=COL_TEXT, value=24)

# สองฝั่งวางเทียบกัน ระยะห่างเท่ากัน สีเดียวกัน ต่างกันแค่กฎที่ใช้ตัดสิน
ui.Label("เชื่อทันที", x=38, y=54, color=COL_DIM, value=16)
l_naive = ui.Label("OK", x=38, y=76, color=COL_OK, value=28)
ui.Label("เปลี่ยนแล้ว", x=38, y=118, color=COL_DIM, value=14)
seg_naive = ui.Seg7("0", x=130, y=112, w=70, h=44, color=COL_BAD)

ui.Label("ยืนยัน 3 รอบ", x=240, y=54, color=COL_DIM, value=16)
l_conf = ui.Label("OK", x=240, y=76, color=COL_OK, value=28)
ui.Label("เปลี่ยนแล้ว", x=240, y=118, color=COL_DIM, value=14)
seg_conf = ui.Seg7("0", x=336, y=112, w=70, h=44, color=COL_OK)

ui.Label("ค่าตอนนี้", x=460, y=54, color=COL_DIM, value=16)
seg_val = ui.Seg7("0.0", x=460, y=76, w=180, h=40, color=COL_INFO)
ui.Label("streak", x=460, y=118, color=COL_DIM, value=14)
bar_streak = ui.Bar(x=530, y=122, w=120, h=18, min=0, max=CONFIRM_N, value=0)

ch = ui.Chart(x=20, y=184, w=650, h=138, color=COL_CARD, min=0, max=CHART_MAX)
s_val = ch.add_series(COL_INFO)
s_lim = ch.add_series(COL_BAD)

# ข้อสังเกตยืนพื้นเป็นป้ายของตัวเอง เพราะ l_foot ถูกเขียนทับด้วยสรุปตอนจบ
# แยกสองใบเพราะประโยคนี้ยาว 159 ไบต์ ซึ่งเกินเพดาน 126 ไบต์ของ ui.Label
ui.Label("ดูจังหวะที่ป้ายสองใบไม่ตรงกัน", x=20, y=334, color=COL_DIM, value=16)
ui.Label("นั่นคือสายที่ไม่ต้องโทร", x=290, y=334, color=COL_DIM, value=16)
l_foot = ui.Label("กำลังเดินค่าจำลอง", x=20, y=358, color=COL_DIM, value=16)
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

l_foot.text("ทันที {} ครั้ง - ยืนยัน {} ครั้ง - ส่วนต่างคือสายที่ไม่ต้องโทร".format(
    naive_changes, changes))
l_foot.color(COL_TEXT)

lcd.print("<span class=error>เชื่อทันที เปลี่ยน {} ครั้ง</span>".format(naive_changes))
lcd.print("<span class=ok>ยืนยัน {} รอบ เปลี่ยน {} ครั้ง</span>".format(CONFIRM_N, changes))
lcd.print("ส่วนต่างคือสายที่ไม่ต้องโทรกลางดึก")
lcd.print("<span class=muted>ราคาที่จ่าย: ช้าลง {} ms</span>".format(CONFIRM_N * LOOP_MS))

# ค้างจอไว้ให้เห็นผลสุดท้าย ตัวเลขสองตัวบนการ์ดคือคำตอบของทั้งไฟล์
for _ in range(20):
    ui.poll()
    time.sleep_ms(100)
