# 07_ticks_and_beat.py - ลูปที่สั่ง sleep เท่าเดิมทุกรอบ ไม่ได้เดินตรงเวลา
#
# ไฟล์นี้สอน: sleep_ms() แปลว่า "หลับอย่างน้อยเท่านี้" ไม่ได้แปลว่า "รอบละเท่านี้"
#             งานที่ทำก่อนหลับกินเวลาของมันเอง คาบจริงจึงยาวกว่าที่ขอเสมอ
#             เครื่องมือที่วัดเรื่องนี้มีสองตัวคือ ticks_ms() กับ ticks_diff()
# ดูที่จอ   : กราฟสองเส้น ครึ่งแรกเส้นเขียวลอยเหนือเส้นส้มตลอด นั่นคือคาบที่ยาวเกิน
#             พอถึงรอบที่ 20 ท่าเปลี่ยน เส้นเขียวทรุดลงมาทาบเส้นส้ม และเลข "ช้าสะสม"
#             หยุดโต ทั้งที่งานต่อรอบเท่าเดิมทุกอย่าง
# กับดัก    : ห้ามลบเวลาสองค่าด้วยเครื่องหมายลบธรรมดา นาฬิกานี้นับขึ้นแล้ววนกลับ
#             ticks_diff() รู้เรื่องการวน ส่วน t2 - t1 ไม่รู้ แล้วจะได้เลขติดลบมหาศาล

import lcd
import time
import ui

TARGET_MS = 200      # คาบที่เราขอ
ROUNDS = 40          # เดินทั้งหมดกี่รอบ
SWITCH_AT = 20       # เปลี่ยนจากท่าที่ 1 เป็นท่าที่ 2 ตอนรอบที่เท่าไร
CHART_MAX = 400      # เพดานแกนตั้งของกราฟ เป็น ms

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN = 0x30A46C, 0xF5A623

ui.screen()
time.sleep_ms(200)

ui.Label("จังหวะของลูป กับนาฬิกาที่ไม่รอใคร", x=20, y=12, color=COL_TEXT,
         value=24)
ui.Panel(x=20, y=52, w=652, h=100, color=COL_CARD, min=COL_DIM, max=12, value=1)

ui.Label("รอบที่", x=40, y=64, color=COL_DIM, value=16)
seg_round = ui.Seg7(text="0", x=40, y=88, w=112, h=56, color=COL_OK)

ui.Label("คาบจริงรอบนี้ (ms)", x=192, y=64, color=COL_DIM, value=16)
seg_period = ui.Seg7(text="0", x=192, y=88, w=152, h=56, color=COL_OK)

ui.Label("ช้าสะสม (ms)", x=400, y=64, color=COL_DIM, value=16)
seg_drift = ui.Seg7(text="0", x=400, y=88, w=152, h=56, color=COL_WARN)

# กราฟมีไว้เป็นเครื่องมือวัดของบทเรียนนี้ ไม่ได้มีไว้อวดว่าวาดกราฟได้
# ตัวเลขคาบเดียว ๆ บอกไม่ได้ว่ามันคงที่หรือไม่ ต้องเห็นทั้งแถวถึงจะตอบได้
# Chart รับเฉพาะจำนวนเต็ม และช่วงแกนตั้งกำหนดตอนสร้าง เปลี่ยนทีหลังไม่ได้
chart = ui.Chart(x=20, y=164, w=652, h=140, color=COL_CARD, min=0, max=CHART_MAX)

# Chart เกิดมาพร้อมเส้นที่ 0 อยู่แล้ว add_series() จึงคืนเลข 1 เป็นเส้นแรกที่เราเพิ่ม
# เก็บเลขที่มันคืนมาไว้ในตัวแปร อย่าเดาเอง
s_real = chart.add_series(COL_OK)
s_target = chart.add_series(COL_WARN)

ui.Label("เส้นเขียว = คาบจริงที่วัดได้", x=20, y=308, color=COL_DIM, value=16)
ui.Label("เส้นส้ม = คาบที่ขอไว้", x=336, y=308, color=COL_DIM, value=16)

phase_lbl = ui.Label("ท่าที่ 1 - สั่ง sleep เท่าเดิมทุกรอบ", x=20, y=336,
                     color=COL_WARN, value=20)
note_lbl = ui.Label("ยังไม่เริ่มจับเวลา", x=20, y=368, color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>จังหวะของลูป</h2>")
lcd.print("<span class=muted>ขอคาบละ", TARGET_MS, "ms</span>")

# ticks_ms() คือจำนวนมิลลิวินาทีตั้งแต่บอร์ดบูต ไม่ใช่เวลาบนปฏิทิน
t_start = time.ticks_ms()
t_prev = t_start

for n in range(1, ROUNDS + 1):
    # ---- งานประจำรอบ เท่ากันทั้งสองท่า นี่คือตัวแปรควบคุมของการทดลองนี้ ----
    t_work = time.ticks_ms()
    period = time.ticks_diff(t_work, t_prev)
    t_prev = t_work

    # ช้าสะสม = เวลาที่เดินไปจริง ลบเวลาที่ควรจะเป็นถ้าทุกรอบตรงเป๊ะ
    drift = time.ticks_diff(t_work, t_start) - n * TARGET_MS

    seg_round.text(str(n))

    # รอบแรกไม่มีคาบให้วัด เพราะยังไม่มีรอบก่อนหน้าให้ลบกัน
    # ค่าผลต่างค่าแรกของทุกการวัดเป็นแบบนี้เสมอ ทิ้งไปหนึ่งค่าแล้วเริ่มนับจริงที่รอบสอง
    if n > 1:
        seg_period.text(str(period))
        seg_drift.text(str(drift))
        seg_period.color(COL_WARN if period > TARGET_MS + 10 else COL_OK)

        chart.set_next(s_real, period)
        chart.set_next(s_target, TARGET_MS)

    ui.poll()

    # ---- ตรงนี้เท่านั้นที่สองท่าต่างกัน ----
    if n < SWITCH_AT:
        # ท่าที่ 1 หลับเท่าเดิมทุกรอบ เวลาที่เพิ่งใช้วาดจอไปไม่ได้ถูกหักออก
        # คาบจริงจึงเป็น TARGET_MS บวกเวลางาน และส่วนเกินนั้นสะสมทุกรอบ
        time.sleep_ms(TARGET_MS)
    else:
        if n == SWITCH_AT:
            phase_lbl.text("ท่าที่ 2 - หักเวลางานออกก่อนหลับ")
            phase_lbl.color(COL_OK)
            lcd.print("<span class=info>เปลี่ยนเป็นท่าที่ 2 ที่รอบ", n, "</span>")

        # ท่าที่ 2 วัดว่างานรอบนี้กินไปกี่ ms แล้วหลับแค่ส่วนที่เหลือของคาบ
        # เครื่องมือยังเป็นคู่เดิม ticks_ms กับ ticks_diff ไม่มีของใหม่
        work = time.ticks_diff(time.ticks_ms(), t_work)
        left = TARGET_MS - work
        if left > 0:
            time.sleep_ms(left)
        else:
            # งานยาวกว่าคาบที่ขอ ไม่มีเวลาให้หลับเลย บอกออกไปตรง ๆ
            note_lbl.text("รอบนี้งานยาวกว่าคาบที่ขอ ไม่ได้หลับเลย")
            note_lbl.color(COL_WARN)

total = time.ticks_diff(time.ticks_ms(), t_start)
want = ROUNDS * TARGET_MS

note_lbl.text("เดิน " + str(ROUNDS) + " รอบใช้จริง " + str(total) +
              " ms - ตั้งใจไว้ " + str(want) + " ms")
note_lbl.color(COL_DIM)
ui.poll()

lcd.print("ใช้จริง", total, "ms | ตั้งใจไว้", want, "ms")
lcd.print("<span class=ok>ส่วนต่าง", total - want, "ms</span>")

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# ตั้ง SWITCH_AT = 41 เพื่อปิดท่าที่ 2 ทิ้งไปเลย แล้วทำนายก่อนรันว่าเลขช้าสะสม
# ตอนจบจะออกมาราวเท่าไร จากนั้นรันจริงแล้วเทียบกับที่ทำนายไว้
# ใบ้: มองที่คาบจริงรอบเดียว แล้วถามว่าส่วนเกินของรอบเดียวนั้นเกิดขึ้นกี่ครั้ง
