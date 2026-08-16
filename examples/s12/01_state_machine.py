# 01_state_machine.py - สามสถานะ และเส้นแบ่งที่ต้องตัดสินใจไว้ล่วงหน้า
#
# ไฟล์นี้สอน: แยก "ค่าที่วัดได้" ออกจาก "สถานะที่ตัดสินแล้ว" ด้วยฟังก์ชันตัดสิน
#             ที่รับค่าเข้าไปแล้วคืนสถานะออกมา เกณฑ์จึงอยู่ที่เดียวและทดสอบได้
# ดูที่จอ   : กราฟค่าไต่ขึ้นแล้วไต่ลง ตัดผ่านเส้นส้ม (WARN) และเส้นแดง (ALERT)
#             ป้ายสถานะตัวใหญ่เปลี่ยนสีตามเส้นที่เพิ่งตัดผ่าน
#             Seg7 ซ้ายคือค่าปัจจุบัน Seg7 ขวาคือจำนวนครั้งที่สถานะเปลี่ยน
# กับดัก    : เขียน if value > WARN ก่อน if value > ALERT จะไม่มีทางเข้า ALERT เลย
#             ลำดับการตรวจต้องไล่จากเข้มที่สุดลงมาเสมอ

import lcd
import time
import ui

WARN_LIMIT = 8.0
ALERT_LIMIT = 15.0
LOOP_MS = 250
CHART_MAX = 220          # กราฟรับเฉพาะจำนวนเต็ม จึงคูณสิบก่อนใส่ (0.0-22.0 -> 0-220)

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x00E676, 0xFFA726, 0xFF5252, 0x40C4FF

# สถานะหนึ่งชื่อ ผูกกับสีหนึ่งสีและคลาสข้อความหนึ่งคลาส เขียนไว้ที่เดียวกัน
# ถ้าวันหลังเพิ่มสถานะที่สี่ ตารางนี้คือที่เดียวที่ต้องแก้
STATE_COLOR = {"OK": COL_OK, "WARN": COL_WARN, "ALERT": COL_BAD}
STATE_CLASS = {"OK": "ok", "WARN": "warn", "ALERT": "error"}


def level_of(value):
    # ไล่จากเข้มที่สุดลงมา ถ้าสลับลำดับ เงื่อนไขที่หลวมกว่าจะดักไว้ก่อนทุกครั้ง
    if value > ALERT_LIMIT:
        return "ALERT"
    if value > WARN_LIMIT:
        return "WARN"
    return "OK"


# แหล่งค่าวันนี้เป็นค่าจำลอง ทีมจะเปลี่ยนเป็นค่าของโจทย์ตัวเองทีหลัง
# ที่สำคัญคือส่วนล่างของไฟล์นี้ไม่ต้องแก้เลยตอนเปลี่ยนแหล่งค่า
SERIES = (0.0, 2.0, 5.0, 9.0, 11.0, 14.0, 16.0, 19.0, 17.0,
          12.0, 9.5, 7.0, 3.0, 1.0, 0.0)

ui.screen()
time.sleep_ms(200)

ui.Label("คาบ 12 - สามสถานะกับเส้นแบ่ง", x=20, y=10, color=COL_TEXT, value=24)
ui.Panel(x=20, y=46, w=650, h=126, color=COL_CARD, min=COL_DIM, max=12, value=1)

ui.Label("ค่าที่วัดได้", x=38, y=56, color=COL_DIM, value=16)
seg_val = ui.Seg7("0.0", x=38, y=80, w=170, h=56, color=COL_OK)

ui.Label("สถานะที่ตัดสินแล้ว", x=230, y=56, color=COL_DIM, value=16)
l_state = ui.Label("OK", x=230, y=78, color=COL_OK, value=28)

ui.Label("เปลี่ยนมาแล้ว (ครั้ง)", x=440, y=56, color=COL_DIM, value=16)
seg_chg = ui.Seg7("0", x=440, y=80, w=110, h=56, color=COL_INFO)

ui.Label("ค่าเทียบกับ ALERT_LIMIT", x=38, y=140, color=COL_DIM, value=14)
bar = ui.Bar(x=230, y=142, w=420, h=18, min=0, max=100, value=0)

# สามเส้นบนกราฟเดียว: ค่าจริง กับเส้นเกณฑ์สองเส้นที่วาดค้างไว้ให้เทียบด้วยตา
# เส้นเกณฑ์ไม่ใช่ข้อมูล มันคือการตัดสินใจของทีมที่เอามาวางทับข้อมูลไว้
ch = ui.Chart(x=20, y=182, w=650, h=140, color=COL_CARD, min=0, max=CHART_MAX)
s_val = ch.add_series(COL_INFO)
s_warn = ch.add_series(COL_WARN)
s_alert = ch.add_series(COL_BAD)

# ข้อสังเกตยืนพื้นเป็นป้ายของตัวเอง เพราะ l_foot ถูกเขียนทับด้วยสรุปตอนจบ
# และแยกเป็นสองใบ ให้อยู่ในเพดาน 126 ไบต์ของ ui.Label - ไทยตัวละ 3 ไบต์
ui.Label("lcd เก็บเฉพาะจังหวะที่เปลี่ยน", x=20, y=334, color=COL_DIM, value=16)
ui.Label("ไม่ได้เก็บทุกรอบ", x=290, y=334, color=COL_DIM, value=16)
l_foot = ui.Label("", x=20, y=358, color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 12 - บันทึกเฉพาะตอนที่สถานะเปลี่ยน</h2>")
lcd.print("<span class=muted>WARN > 8.0 - ALERT > 15.0</span>")

state = "OK"
changes = 0

for i in range(len(SERIES)):
    value = SERIES[i]
    level = level_of(value)

    # จอต้องบอกค่าล่าสุดทุกรอบ เพราะคนหน้างานมองจอเพื่อดู "ตอนนี้"
    seg_val.text("{:.1f}".format(value))
    seg_val.color(STATE_COLOR[level])
    pct = int(value * 100 / ALERT_LIMIT)
    bar.value(100 if pct > 100 else pct)
    ch.set_next(s_val, int(value * 10))
    ch.set_next(s_warn, int(WARN_LIMIT * 10))
    ch.set_next(s_alert, int(ALERT_LIMIT * 10))

    if level != state:
        # เก็บทั้งสถานะเดิมและใหม่ไว้ เพราะ "OK -> ALERT" กับ "WARN -> ALERT"
        # เป็นคนละเรื่องกันสำหรับคนที่ต้องไปดูหน้างาน
        lcd.print("<span class={}>รอบ {} - {:.1f} - {} -> {}</span>".format(
            STATE_CLASS[level], i, value, state, level))
        state = level
        changes += 1
        l_state.text(state)
        l_state.color(STATE_COLOR[state])
        seg_chg.text(str(changes))

    ui.poll()
    time.sleep_ms(LOOP_MS)

l_foot.text("จบชุดข้อมูล - เปลี่ยนสถานะ {} ครั้ง จาก {} รอบ".format(changes, len(SERIES)))
l_foot.color(COL_TEXT)
lcd.print("<b>สรุป</b> เปลี่ยน {} ครั้ง - จบที่ {}".format(changes, state))

# ตรวจเส้นแบ่งด้วยมือ ค่าที่ตกลงบนเส้นพอดีต้องอยู่ฝั่งไหน ตอบให้ได้ก่อนเขียนต่อ
# ที่นี่ใช้ > ไม่ใช่ >= ดังนั้นค่าเท่ากับเกณฑ์พอดี ยังไม่ถือว่าเกิน
for v in (8.0, 8.1, 15.0, 15.1):
    lcd.print("<span class=muted>ค่า {} -> {}</span>".format(v, level_of(v)))

# ค้างจอไว้ให้อ่านผลสุดท้ายทัน ลูปจบแล้วแต่ค่าบนจอยังต้องอยู่ครบ
for _ in range(20):
    ui.poll()
    time.sleep_ms(100)
