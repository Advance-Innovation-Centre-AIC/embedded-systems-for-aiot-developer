# 04_pot_taper_volume.py - ทำไมลูกบิดเสียงต้องเป็นเส้นโค้ง
# ชุดตัวอย่างประจำคาบ 05
#
# Why : หูคนรับรู้ความดังเป็นลอการิทึม ถ้าเอาตำแหน่งลูกบิดไปเป็นระดับเสียงตรง ๆ
#       ผู้ใช้จะพบว่าเสียงดังสุดตั้งแต่หมุนไปหนึ่งในสี่ แล้วอีกสามในสี่ที่เหลือแทบไม่ต่างกัน
#       ลูกบิดเสียงของเครื่องเสียงทุกเครื่องจึงเป็นแบบ log taper ไม่ใช่เส้นตรง
# What: ลูกบิดบนบอร์ดนี้เป็นแบบ linear เราจึงต้องดัดเส้นในซอฟต์แวร์เอง
#       log_taper() คือการดัดเส้นนั้น ยกกำลังสามคือค่าประมาณที่วงการเครื่องเสียงใช้กัน
#       ไฟล์นี้เดินดูทีละจุด 0 / 25 / 50 / 75 / 100 % เพื่อให้เทียบสองเส้นได้ทันที
#       ไม่ใช่ไหลผ่านไปจนดูไม่ทัน และฟังเสียงของแต่ละจุดเทียบกันได้ด้วย
#
# บน Eva Kit: sensors.init() และ sensors.scan() ปฏิเสธด้วย OSError ไม่ต้องเรียก
#   ส่วนตัวอ่านของแต่ละเซนเซอร์ใช้ได้เลย - มันอ่านจาก snapshot ที่ CM55 ส่งมาทาง IPC
#   (modsensors.c) หลังรีเซ็ต การอ่านครั้งแรกช้าได้ถึงราว 16 วินาที
#   และอาจโยน OSError ระหว่างนั้น
#
# ดูที่จอ: กราฟสองเส้นเต็มย่าน - ฟ้าคือเส้นตรงพุ่งขึ้นสม่ำเสมอ แดงคือเส้นโค้งที่แบนราบ
#         ครึ่งแรกแล้วค่อยพุ่งช่วงท้าย หมุดเหลืองคือจุดที่กำลังดูอยู่
#         ล่างจอเป็นแถบปุ่มสี่ปุ่มไว้เดินทีละท่า ขวามือมีตัวเลขตำแหน่ง สองค่าเทียบกัน
#         และแถบความดังจริงที่ส่งออกลำโพง
# กับดัก : ห้ามเอาเปอร์เซ็นต์ตำแหน่งไปเป็นระดับเสียงตรง ๆ ครึ่งทางของลูกบิด
#         ไม่ใช่ครึ่งหนึ่งของความดังที่หูได้ยิน
#         และอย่าใช้ time.sleep นาน ๆ ในลูป เพราะระหว่างหลับ ui.poll() ไม่ทำงาน
#         ปุ่มจะกดไม่ติด - หน่วงด้วยการเทียบ ticks_ms แทน

import lcd
import sensors
import time
import ui

NOTE = 69          # A4 - ใช้โน้ตเดิมตลอด ให้ความต่างมาจากความดังอย่างเดียว
POINTS = 50        # กราฟเก็บได้ 50 จุดพอดี กวาดสาธิตจึงกวาดเท่านี้
PLAY_MS = 1100     # จังหวะตอนเล่นรวดเดียว
STEPS = (0, 25, 50, 75, 100)   # ท่าที่เดินดู หน่วยเป็นเปอร์เซ็นต์ของลูกบิด
KNOB_MOVE = 5.0    # ลูกบิดจริงขยับเกินเท่านี้ ถือว่าคนหมุนเอง ให้กระโดดตามไป


def log_taper(pct):
    # ประมาณเส้น log taper ด้วยการยกกำลังสาม ใช้กันทั่วไปในเครื่องเสียง
    x = pct / 100.0
    return (x ** 3) * 100.0


def nearest_step(pct):
    k = int((pct + 12.5) // 25)
    return 0 if k < 0 else (len(STEPS) - 1 if k >= len(STEPS) else k)


lcd.clear()
lcd.console("<h2>เส้นตรง เทียบ เส้นโค้งเสียง</h2>")
lcd.print("ตำแหน่ง | เส้นตรง | เส้นโค้ง (ที่ใช้จริง)")

# ---- หน้าจอ: เนื้อหาทั้งหมดต้องอยู่เหนือ y=244 เพราะแถบปุ่มกิน 250 ถึง 314 ----
ui.screen()
ui.Label("เส้นตรง เทียบ เส้นโค้งเสียง", x=12, y=8, value=24)
ch = ui.Chart(x=12, y=36, w=432, h=200, min=0, max=100)
s_lin = 0                          # ซีรีส์ 0 เกิดพร้อมกราฟ สีฟ้าเริ่มต้น
s_log = ch.add_series(0xFF5555)
s_mark = ch.add_series(0xFFC107)   # สร้างทีหลังสุด จึงถูกวาดทับเส้นอื่น
ui.Label("ฟ้า = เส้นตรง", x=456, y=40, value=16, color=0x4A9EFF)
ui.Label("แดง = เส้นโค้ง ที่ใช้จริง", x=456, y=76, value=16, color=0xE5484D)
ui.Label("เหลือง = จุดที่กำลังดู", x=456, y=112, value=16, color=0xF5A623)

seg = ui.Seg7(x=456, y=148, w=120, h=36)
lbl_pos = ui.Label("", x=600, y=112, value=20, color=0x30A46C)
lbl_body = ui.Label("", x=456, y=148, value=16)
ui.Label("ความดังจริงที่ส่งออก", x=456, y=192, value=16)
vol_bar = ui.Bar(x=456, y=228, w=252, h=16, min=0, max=100, value=0)
lbl_knob = ui.Label("", x=456, y=220, value=16, color=0x9AA3AF)

# แถบปุ่ม - เว้นมุมขวาล่างไว้ให้ปุ่ม Console ของหน้า Playground
btn_prev = ui.Button("< ย้อน", x=20, y=252, w=140, h=88, color=0x9AA3AF,
                     value=20)
btn_next = ui.Button("เดินหน้า >", x=176, y=252, w=160, h=88, color=0x4A9EFF,
                     value=20)
btn_play = ui.Button(">> เล่นรวด", x=352, y=252, w=160, h=88, color=0x30A46C,
                     value=20)
btn_home = ui.Button("เริ่มใหม่", x=528, y=252, w=140, h=88, color=0x4A9EFF,
                     value=20)
ID_PREV, ID_NEXT = btn_prev.id(), btn_next.id()
ID_PLAY, ID_HOME = btn_play.id(), btn_home.id()

lbl_hint = ui.Label("", x=20, y=332, value=16, color=0x9AA3AF)

# ---- กวาดทั้งย่านก่อน ให้เห็นรูปทรงของเส้นก่อนเริ่มเดินทีละท่า ----------------
for k in range(POINTS):
    pct = k * 100.0 / (POINTS - 1)
    ch.set_next(s_lin, int(pct))
    ch.set_next(s_log, int(log_taper(pct)))


def mark(pct):
    # หมุดคือซีรีส์เต็ม 50 จุด ที่เป็นศูนย์ทุกจุดยกเว้นตำแหน่งปัจจุบัน
    # ต้องส่งครบ 50 จุดทุกครั้ง ไม่งั้นซีรีส์นี้จะเลื่อนไม่ตรงกับอีกสองเส้น
    hit = int(pct * (POINTS - 1) / 100.0)
    for k in range(POINTS):
        ch.set_next(s_mark, 100 if k == hit else 0)


i = 0                   # ท่าปัจจุบัน - ความจริงของโปรแกรมอยู่ที่ตัวนี้
playing = False
t_next = 0


def show():
    pct = STEPS[i]
    lin = float(pct)
    log = log_taper(pct)
    mark(pct)
    seg.text(str(pct))
    lbl_pos.text("ท่า %d / %d" % (i + 1, len(STEPS)))
    lbl_body.text("เส้นตรง " + str(int(lin)) + " -> เส้นโค้ง " + str(int(log)))
    vol_bar.value(int(log))
    lbl_hint.text("กำลังเล่นรวด - กดปุ่มไหนก็หยุด" if playing
                  else "กดเดินหน้าเพื่อไปจุดถัดไป หรือหมุนลูกบิดจริงก็ได้")
    lcd.print(str(pct) + "% | " + str(int(lin)) + " | " + str(int(log)))

    # velocity ของ ui.tone อยู่ในช่วง 0-127 แปลงจากเส้นโค้ง
    vel = int(log * 127 / 100)
    if vel > 0:
        ui.tone(NOTE, ui.WAVE_SINE, vel, 300)


knob = sensors.pot.percent()
lbl_knob.text("ลูกบิดจริงตอนนี้ " + str(int(knob)) + "%")
show()

while True:
    for ev in ui.poll():
        if ev["type"] != "clicked":
            continue
        h = ev["handle"]
        if h == ID_PREV:
            playing = False
            i = (i - 1) % len(STEPS)
        elif h == ID_NEXT:
            playing = False
            i = (i + 1) % len(STEPS)
        elif h == ID_HOME:
            playing = False
            i = 0
        elif h == ID_PLAY:
            playing = not playing
            t_next = time.ticks_add(time.ticks_ms(), PLAY_MS)
        show()

    # ลูกบิดจริงเป็นทางเข้าอีกทางหนึ่ง หมุนเมื่อไรก็กระโดดไปท่าที่ใกล้ที่สุด
    pct = sensors.pot.percent()
    if abs(pct - knob) >= KNOB_MOVE:
        knob = pct
        lbl_knob.text("ลูกบิดจริงตอนนี้ " + str(int(pct)) + "%")
        playing = False
        i = nearest_step(pct)
        show()

    # เล่นรวดเดียว: เดินหน้าเองตามเวลา แต่ไม่หลับ เพื่อให้ปุ่มยังกดติด
    if playing and time.ticks_diff(time.ticks_ms(), t_next) >= 0:
        i += 1
        if i >= len(STEPS):
            i = len(STEPS) - 1
            playing = False
        t_next = time.ticks_add(time.ticks_ms(), PLAY_MS)
        show()

    time.sleep_ms(30)
