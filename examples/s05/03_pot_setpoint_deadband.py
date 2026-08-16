# 03_pot_setpoint_deadband.py - ลูกบิดตั้งค่า พร้อมแถบตาย
# ชุดตัวอย่างประจำคาบ 05
#
# Why : ลูกบิดตั้งอุณหภูมิเตาอบ ลูกบิดเสียงในรถ วาล์วปรับแรงดัน ยังชนะจอสัมผัส
#       ในงานที่ต้องปรับโดยไม่ละสายตา เพราะมือหาตำแหน่งเจอเอง แต่ค่าดิบจากลูกบิด
#       สั่นตลอดเวลาแม้ไม่มีใครแตะ ระบบที่รายงานทุกการเปลี่ยนแปลงจะส่งข้อความท่วมเครือข่าย
# What: dead-band คือกติกาข้อเดียวว่า "ขยับไม่ถึงเท่านี้ ถือว่าไม่ได้ขยับ"
#       ค่าที่รายงานจึงกลายเป็นขั้นบันได ไม่ใช่เส้นต่อเนื่องตามค่าดิบ
#       และจำนวนข้อความที่ส่งออกไปลดลงหลายเท่าโดยผู้ใช้ไม่รู้สึกว่าเสียอะไร
#
# บน Eva Kit: sensors.init() และ sensors.scan() ปฏิเสธด้วย OSError ไม่ต้องเรียก
#   ส่วนตัวอ่านของแต่ละเซนเซอร์ใช้ได้เลย - มันอ่านจาก snapshot ที่ CM55 ส่งมาทาง IPC
#   (modsensors.c) หลังรีเซ็ต การอ่านครั้งแรกช้าได้ถึงราว 16 วินาที
#   และอาจโยน OSError ระหว่างนั้น
#
# ดูที่จอ: กราฟสองเส้น - ฟ้าคือค่าดิบที่ไต่ขึ้นแล้วสั่นอยู่กับที่ แดงคือค่าที่รายงาน
#         ซึ่งเป็นขั้นบันไดและนิ่งสนิทตอนมือไม่ได้หมุน ขวามือมีตัวเลขอุณหภูมิที่ตั้งไว้
#         แถบตำแหน่งลูกบิด และบรรทัดนับว่าอ่านไปกี่ครั้ง รายงานจริงกี่ครั้ง
# กับดัก : ปลายสเกลของลูกบิดจริงมักถึงไม่สุด ต้องยึด (clamp) ที่ 0 และ 100
#         ไม่งั้นผู้ใช้จะหมุนสุดแล้วเครื่องยังบอก 98%

import gpio
import lcd
import sensors
import time
import ui

DEAD_PCT = 2.0     # ขยับน้อยกว่านี้ ถือว่าไม่ได้ขยับ
EDGE_PCT = 3.0     # ใกล้ปลายสเกลเท่านี้ ให้ยึดเป็น 0 หรือ 100 ไปเลย
SET_MIN = 20       # ช่วงอุณหภูมิที่ตั้งได้ องศาเซลเซียส
SET_MAX = 80

# ค่าสั่นสาธิต 4 จังหวะ กว้างสุด 1.8% ซึ่งยังไม่ถึง DEAD_PCT จึงต้องถูกกลืนทั้งหมด
JITTER = (0.9, 0.3, -0.9, -0.3)


def clamp_ends(pct):
    if pct < EDGE_PCT:
        return 0.0
    if pct > 100.0 - EDGE_PCT:
        return 100.0
    return pct


def to_setpoint(pct):
    return SET_MIN + (SET_MAX - SET_MIN) * pct / 100.0


lcd.clear()
lcd.console("<h2>ลูกบิดตั้งอุณหภูมิ</h2>")
lcd.print("แถบตาย", DEAD_PCT, "% | ช่วงตั้งได้", SET_MIN, "-", SET_MAX, "C")

# ---- หน้าจอ ------------------------------------------------------------------
ui.screen()
ui.Label("ลูกบิดตั้งอุณหภูมิ กับแถบตาย", x=12, y=6, value=24)
ch = ui.Chart(x=12, y=40, w=470, h=232, min=0, max=100)
s_raw = 0                          # ซีรีส์ 0 เกิดพร้อมกราฟ สีฟ้าเริ่มต้น
s_rep = ch.add_series(0xFF5555)
ui.Label("ฟ้า = ค่าดิบ ต่อเนื่อง", x=496, y=44, value=16, color=0x00BFFF)
ui.Label("แดง = ที่รายงาน ขั้นบันได", x=496, y=68, value=16, color=0xFF5555)

ui.Label("ตั้งไว้ (องศา C)", x=496, y=104, value=16)
seg = ui.Seg7(x=496, y=128, w=180, h=40)
ui.Label("ตำแหน่งลูกบิด", x=496, y=180, value=16)
pot_bar = ui.Bar(x=496, y=204, w=250, h=18, min=0, max=100, value=0)
pot_txt = ui.Label("0 %", x=496, y=228, value=16, color=0x00BFFF)

ui.Panel(x=12, y=286, w=662, h=48)
count_txt = ui.Label("อ่าน 0 ครั้ง / รายงาน 0 ครั้ง", x=24, y=298, value=20,
                     color=0xFFC107)

# ---- กวาดค่าสาธิตลงกราฟก่อน ให้เห็นรูปทรงตั้งแต่วินาทีแรก ---------------------
# 38 จุดแรกคือมือค่อย ๆ หมุน ทีละ 1.5% ซึ่งน้อยกว่าแถบตาย ค่ารายงานจึงขยับเป็นขั้น
# 12 จุดหลังคือมือปล่อยแล้ว เหลือแต่ค่าสั่น ซึ่งถูกกลืนหมด เส้นแดงจึงนิ่งสนิท
demo_shown = -999.0
for i in range(50):
    step = i if i < 38 else 37
    raw = 6.0 + step * 1.5 + JITTER[i % 4]
    raw = clamp_ends(raw)
    if abs(raw - demo_shown) >= DEAD_PCT:
        demo_shown = raw
    ch.set_next(s_raw, int(raw))
    ch.set_next(s_rep, int(demo_shown))
ui.poll()

shown = -999.0
changes = 0
raw_reads = 0

for _ in range(700):
    pct = clamp_ends(sensors.pot.percent())
    raw_reads += 1

    pot_bar.value(int(pct))
    pot_txt.text(str(int(pct)) + " %")

    if abs(pct - shown) >= DEAD_PCT:
        shown = pct
        changes += 1
        setpoint = to_setpoint(pct)
        seg.text(str(round(setpoint, 1)))
        lcd.print("ตั้งไว้", round(setpoint, 1), "C  (", int(pct), "% )")

        # ไฟบอกว่าตั้งไว้สูงหรือต่ำ โดยไม่ต้องอ่านตัวเลข
        gpio.led(0).off()
        gpio.led(1).off()
        if setpoint > (SET_MIN + SET_MAX) / 2:
            gpio.led(0).on()
        else:
            gpio.led(1).on()

    count_txt.text("อ่าน " + str(raw_reads) + " ครั้ง / รายงาน " +
                   str(changes) + " ครั้ง")
    ui.poll()
    time.sleep_ms(80)

gpio.led(0).off()
gpio.led(1).off()
lcd.print("อ่าน", raw_reads, "ครั้ง | รายงานจริง", changes, "ครั้ง")
print("ลองตั้ง DEAD_PCT = 0 แล้วดูว่าจำนวนรายงานพุ่งขึ้นกี่เท่า")
