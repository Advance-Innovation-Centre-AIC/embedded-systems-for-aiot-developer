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

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
# สามสีล่างใช้กับสถานะเท่านั้น สีเน้นใช้กับเส้นค่าจริงและของที่กำลังเปลี่ยน
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

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

# ผังจอเดินบนกริด 8 ขอบนอก 24 - หัวเรื่องหนึ่งบรรทัด การ์ดสามช่องหนึ่งใบ
# กราฟหนึ่งช่อง แล้วสองบรรทัดล่างสุด ทุกอย่างที่ต่ำกว่า y=340 จบก่อน x=690
# ซึ่งเป็นที่ของปุ่ม Console ที่เฟิร์มแวร์ถือไว้
ui.Label("คาบ 12 - สามสถานะกับเส้นแบ่ง", x=24, y=8, color=COL_TEXT, value=28)
ui.Panel(x=24, y=56, w=744, h=160, color=COL_CARD, min=COL_CARD, max=12,
         value=1)

ui.Label("ค่าที่วัดได้", x=40, y=72, color=COL_DIM, value=20)
seg_val = ui.Seg7("0.0", x=40, y=104, w=192, h=56, color=COL_OK)

# ป้ายย่อจาก "สถานะที่ตัดสินแล้ว" เหลือสองคำ เพราะที่ฟอนต์ 20 ข้อความกว้างขึ้น
# ราวหนึ่งในสี่ ของเดิมจะยื่นไปทับช่องที่สาม
ui.Label("สถานะที่ตัดสิน", x=264, y=72, color=COL_DIM, value=20)
l_state = ui.Label("OK", x=264, y=104, color=COL_OK, value=28)

ui.Label("เปลี่ยนสถานะ (ครั้ง)", x=528, y=72, color=COL_DIM, value=20)
# ตัวนับไม่ใช่สถานะ จึงไม่ทาสีตามระดับ ถ้าทาเขียว ตาจะอ่านว่า "ตัวเลขนี้ปกติ"
seg_chg = ui.Seg7("0", x=528, y=104, w=120, h=56, color=COL_TEXT)

# ตัวเลขลอย ๆ ตอบไม่ได้ว่าสูงไหม แถบเทียบเกณฑ์จึงอยู่ในการ์ดใบเดียวกับค่า
# ตาม S7.7.8 - ค่าที่วัดได้ต้องมาพร้อมเกณฑ์ ไม่ใช่มาตัวเปล่า
ui.Label("เทียบเกณฑ์ ALERT 15.0", x=40, y=172, color=COL_DIM, value=20)
bar = ui.Bar(x=288, y=168, w=456, h=32, min=0, max=100, value=0)

# สามเส้นบนกราฟเดียว: ค่าจริง กับเส้นเกณฑ์สองเส้นที่วาดค้างไว้ให้เทียบด้วยตา
# เส้นเกณฑ์ไม่ใช่ข้อมูล มันคือการตัดสินใจของทีมที่เอามาวางทับข้อมูลไว้
# เส้นค่าจริงใช้สีเน้น ส่วนเส้นเกณฑ์ใช้สีของระดับที่มันหมายถึงจริง ๆ
ch = ui.Chart(x=24, y=232, w=744, h=96, color=COL_CARD, min=0, max=CHART_MAX)
s_val = ch.add_series(COL_ACCENT)
s_warn = ch.add_series(COL_WARN)
s_alert = ch.add_series(COL_BAD)

# ป้ายกำกับเส้น - เส้นเกณฑ์แบนอยู่ที่ค่าคงที่ ตัวเลขในป้ายจึงชี้เส้นได้เอง
# แม้ภาพจะถูกแปลงเป็นขาวดำ ซึ่งคือสิ่งที่ S7.7.1 ต้องการ
ui.Label("เส้นส้ม = WARN 8.0", x=24, y=336, color=COL_WARN, value=20)
ui.Label("เส้นแดง = ALERT 15.0", x=256, y=336, color=COL_BAD, value=20)
ui.Label("lcd เก็บเฉพาะขอบ", x=488, y=336, color=COL_DIM, value=20)
l_foot = ui.Label("", x=24, y=368, color=COL_DIM, value=20)
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
