# 08_status_screen.py - จอสถานะหนึ่งใบ ที่สามโมดูลแบ่งงานกันทำ
#
# ไฟล์นี้ไม่มีคำสั่งใหม่เลยสักตัว ทุกอย่างในนี้เคยผ่านตามาแล้วในไฟล์ 01 ถึง 07
# ของใหม่คือ "จะเอามันมาต่อกันยังไง" ซึ่งเป็นคำถามที่ไฟล์เดี่ยว ๆ ไม่เคยตอบ
#
# ไฟล์นี้สอน: จอกับลิ้นชักไม่ได้ทำงานเดียวกัน จอตอบว่า "ตอนนี้เป็นยังไง"
#             ลิ้นชักตอบว่า "ที่ผ่านมาเกิดอะไรขึ้นบ้าง" งานคนละอย่าง โมดูลคนละตัว
# ดูที่จอ   : ตัวเลขกับแถบขยับตลอด แต่บรรทัดในลิ้นชักเพิ่มเฉพาะตอนที่ระดับเปลี่ยน
#             เปิดลิ้นชักตอนจบแล้วจะได้ประวัติสั้น ๆ ที่อ่านรู้เรื่อง ไม่ใช่ร้อยบรรทัดซ้ำ
# กับดัก    : ถ้ายิง lcd.print() ทุกรอบของลูป ลิ้นชักจะมีแต่บรรทัดเดิมซ้ำกันเป็นร้อย
#             ประวัติที่ไม่มีใครอ่านไหว มีค่าเท่ากับไม่มีประวัติ

import lcd
import time
import ui

RUN_MS = 24000       # เดินนานเท่าไร
TICK_MS = 200        # คาบของลูป ตามท่าที่ 2 ของไฟล์ 07
WARN_AT = 60         # เกินเท่านี้ถือว่าเริ่มสูง
ALARM_AT = 85        # เกินเท่านี้ถือว่าต้องรีบดู

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD = 0x00E676, 0xFFA726, 0xFF5252

# ระดับสามชั้น เก็บชื่อ สี และคลาสของ span ไว้ด้วยกัน
# คลาสของ span คือระดับความสำคัญ ไม่ใช่สีที่ชอบ ตามที่ไฟล์ 02 อธิบายไว้
# จอกับลิ้นชักจึงเล่าเรื่องเดียวกันเสมอ เพราะทั้งคู่อ่านจากตารางเดียวกันนี้
LEVELS = (
    ("ปกติ", COL_OK, "ok"),
    ("เริ่มสูง", COL_WARN, "warn"),
    ("ต้องรีบดู", COL_BAD, "error"),
)


def level_of(v):
    """คืนหมายเลขระดับของค่า v - 0 ปกติ, 1 เริ่มสูง, 2 ต้องรีบดู"""
    if v >= ALARM_AT:
        return 2
    if v >= WARN_AT:
        return 1
    return 0


def reading_at(ms):
    """ค่าอ่านจำลอง เดินขึ้นลงเป็นรอบ ๆ ให้ครบทั้งสามระดับ

    คาบ 5 เป็นต้นไปจะถอดฟังก์ชันนี้ทิ้งแล้วเสียบค่าจากเซนเซอร์จริงเข้ามาแทน
    ที่ยังจำลองไว้ก่อน เพราะคาบนี้เรากำลังเรียนเรื่องการรายงานผล ไม่ใช่การวัด
    """
    step = (ms // 600) % 20
    if step > 10:
        step = 20 - step
    return step * 10


ui.screen()
time.sleep_ms(200)

ui.Label("จอสถานะ - จอบอกตอนนี้ ลิ้นชักบอกที่ผ่านมา", x=20, y=12,
         color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=650, h=124, color=COL_CARD, min=COL_DIM, max=12, value=1)

ui.Label("ค่าล่าสุด", x=40, y=62, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=40, y=86, w=170, h=64, color=COL_OK)

ui.Label("ระดับตอนนี้", x=250, y=62, color=COL_DIM, value=16)
level_lbl = ui.Label("ปกติ", x=250, y=88, color=COL_OK, value=28)
bar = ui.Bar(x=250, y=132, w=390, h=24, min=0, max=100, value=0)

ui.Label("ประวัติอยู่ในลิ้นชัก Console", x=20, y=192, color=COL_DIM, value=20)
chart = ui.Chart(x=20, y=224, w=650, h=110, color=COL_CARD, min=0, max=100)
s_value = chart.add_series(COL_OK)

clock_lbl = ui.Label("เวลาเดินไป 0 ms", x=20, y=342, color=COL_DIM, value=16)
change_lbl = ui.Label("ยังไม่เปลี่ยนระดับ", x=200, y=342, color=COL_DIM,
                      value=16)
rounds_lbl = ui.Label("รอบที่ 0", x=440, y=342, color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>ประวัติการเปลี่ยนระดับ</h2>")
lcd.console("<span class=muted>บรรทัดจะเพิ่มเฉพาะตอนระดับเปลี่ยน</span>")

t0 = time.ticks_ms()
rounds = 0
changes = 0

# -1 แปลว่า "ยังไม่เคยรู้ระดับมาก่อน" รอบแรกจึงนับเป็นการเปลี่ยนเสมอ
# ถ้าตั้งต้นเป็น 0 ประวัติจะไม่มีบรรทัดแรกบอกว่าเริ่มต้นที่ระดับไหน
last_level = -1

while True:
    t_work = time.ticks_ms()
    elapsed = time.ticks_diff(t_work, t0)
    if elapsed >= RUN_MS:
        break

    rounds = rounds + 1
    value = reading_at(elapsed)
    lv = level_of(value)
    name, color, cls = LEVELS[lv]

    # --- งานของจอ: ตอบว่าตอนนี้เป็นยังไง ทำทุกรอบ เพราะจอไม่สะสมอะไรไว้ ---
    seg.text(str(value))
    seg.color(color)
    level_lbl.text(name)
    level_lbl.color(color)
    bar.value(value)
    chart.set_next(s_value, value)
    clock_lbl.text("เวลาเดินไป " + str(elapsed) + " ms")
    rounds_lbl.text("รอบที่ " + str(rounds))

    # --- งานของลิ้นชัก: ตอบว่าที่ผ่านมาเกิดอะไร ทำเฉพาะตอนมีเรื่องให้เล่า ---
    if lv != last_level:
        changes = changes + 1
        last_level = lv
        lcd.print("<span class=" + cls + ">" + str(elapsed) + " ms  " +
                  name + "  ค่า " + str(value) + "</span>")
        change_lbl.text("เปลี่ยนระดับไปแล้ว " + str(changes) + " ครั้ง")
        change_lbl.color(color)

    ui.poll()

    # --- งานของ time: ให้ลูปเดินตรงจังหวะ ตามท่าที่ 2 ของไฟล์ 07 ---
    work = time.ticks_diff(time.ticks_ms(), t_work)
    left = TICK_MS - work
    if left > 0:
        time.sleep_ms(left)

# จบแล้วปล่อยค่าสุดท้ายค้างไว้ ไม่ล้างจอ คนดูจะได้อ่านทัน
# เขียนทับป้ายเดิมด้วยข้อความที่สั้นกว่าหรือพอ ๆ กัน ป้ายที่ยาวขึ้นตอนจบ
# จะยื่นไปทับป้ายข้าง ๆ ทั้งที่ตอนสร้างวางไว้ห่างกันดีแล้ว
clock_lbl.text("จบแล้ว - จอวาด " + str(rounds) + " รอบ")
change_lbl.text("เปลี่ยน " + str(changes) + " ครั้ง")
change_lbl.color(COL_DIM)
ui.poll()

lcd.console("<span class=muted>------------------------</span>")
lcd.print("<span class=ok>จอวาด", rounds, "รอบ | ลิ้นชักได้", changes,
          "บรรทัด</span>")

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# ย้ายบล็อก lcd.print() ออกจาก if ให้มันยิงทุกรอบ แล้วรันใหม่ เปิดลิ้นชักดู
# แล้วตอบว่า ประวัติแบบไหนที่คนเดินมาดูหน้างานใช้งานได้จริงกว่ากัน
# ใบ้: ลองหาคำตอบจากลิ้นชักว่า "ค่าขึ้นถึงระดับต้องรีบดูตอนวินาทีที่เท่าไร"
