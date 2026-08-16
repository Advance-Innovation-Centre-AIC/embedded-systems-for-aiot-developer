# 15_one_number_many_faces.py - ตัวเลขตัวเดียว กับสิบวิธีที่จอเล่ามันออกมา
#
# ไฟล์นี้ไม่มีเซนเซอร์ ไม่มีเน็ต มีแต่ตัวเลขหนึ่งตัวที่เดินขึ้นลงเอง
# ของใหม่ทั้งหมดคือชนิดของ widget ที่ยังไม่เคยเจอในไฟล์ก่อนหน้า
# Slider Switch Checkbox Spinner Dropdown Image Compass และเสียงจาก ui.tone
#
# ไฟล์นี้สอน: ค่าเดียวกันเล่าได้หลายแบบ และแต่ละแบบตอบคำถามคนละข้อ
#             วงแหวนตอบว่า "เต็มแค่ไหน" ตัวเลขตอบว่า "เท่าไรพอดี"
#             กราฟตอบว่า "ที่ผ่านมาเป็นยังไง" ไฟติดดับตอบว่า "ถึงเกณฑ์หรือยัง"
# ดูที่จอ   : ทุกชิ้นขยับพร้อมกันจากตัวเลขตัวเดียว แตะปุ่มล่างซ้ายเพื่อฟังเสียง
#             และบรรทัด ui.list() บอกว่าตอนนี้จอมี widget อยู่กี่ตัว จากเพดาน 32
# กับดัก    : Seg7 รับเฉพาะข้อความ seg.value(50) เงียบสนิทและไม่มี error ให้จับ
#             ส่วน Slider Arc Bar Switch Checkbox รับเฉพาะ .value() ไม่รับ .text()
#             ชนิดไหนรับอะไร ไม่มีทางรู้จากการรันแล้วดูว่ามี error ไหม เพราะไม่มี

import lcd
import time
import ui

RUN_MS = 36000       # เดินนานเท่าไร
TICK_MS = 120        # คาบของลูป
STEP = 4             # ตัวเลขขยับทีละเท่าไร
HI, LO = 70, 30      # เกณฑ์บนกับล่าง ใช้จุดไฟติดดับกับเล่นเสียง

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_INFO = 0x00E676, 0xFFA726, 0x40C4FF

# ชื่อไอคอนที่เฟิร์มแวร์มีให้ใช้ ใส่ชื่อที่ไม่มีตอนสร้างจะได้ RuntimeError ทันที
# แต่ถ้าใส่ชื่อผิดทีหลังผ่าน .icon() จะเงียบสนิท ไม่มีอะไรเกิดขึ้นและไม่มี error
ICON_UP, ICON_DOWN = "arrow_up", "arrow_down"

ui.screen()
time.sleep_ms(200)

ui.Label("ตัวเลขตัวเดียว หลายวิธีเล่า", x=20, y=12, color=COL_TEXT, value=24)
status = ui.Label("กำลังจะเริ่มเดิน", x=20, y=48, color=COL_DIM, value=18)

# Panel เป็นพื้นหลังของการ์ด ของอื่นวางทับมันได้โดยตั้งใจ
# ลำดับสำคัญ ต้องสร้าง Panel ก่อนของที่จะวางบนมัน ไม่งั้นมันจะไปบังของที่มีอยู่
ui.Panel(x=14, y=72, w=764, h=136, color=COL_CARD, min=COL_DIM, max=12,
         value=1)

# Arc กับ Compass ถ้าไม่ใส่ w จะได้ 150x150 มาเลย ซึ่งใหญ่เกินกว่าจะวางสองชิ้น
# และ Compass ใช้ w เป็นเส้นผ่านศูนย์กลาง ส่วน h มันไม่สนใจ วงกลมเสมอ
arc = ui.Arc(x=20, y=78, w=120, h=120, min=0, max=100, value=0)
arc.color(COL_OK)
comp = ui.Compass(x=160, y=78, w=120, h=120)
comp.color(COL_INFO)

seg = ui.Seg7(text="0", x=300, y=100, w=150, h=64, color=COL_OK)
bar = ui.Bar(x=300, y=178, w=150, h=22, min=0, max=100, value=0)
bar.color(COL_OK)

sld = ui.Slider(x=470, y=90, w=200, h=20, min=0, max=100, value=0)
sw = ui.Switch(x=470, y=130, w=60, h=30)
chk = ui.Checkbox("เกินเกณฑ์บน", x=560, y=130, w=140, h=30, color=COL_TEXT)

# Spinner ไม่มีค่าให้ตั้ง มันหมุนของมันเองตลอด งานเดียวของมันคือบอกว่า "ยังทำอยู่"
spin = ui.Spinner(x=720, y=90, w=60, h=60)

# Image รับชื่อไอคอนเป็นข้อความตอนสร้าง แล้วเปลี่ยนทีหลังด้วย .icon()
img = ui.Image(ICON_UP, x=720, y=170, w=48, h=48, color=COL_OK)

ui.Label("Arc", x=20, y=206, color=COL_DIM, value=16)
ui.Label("Compass", x=160, y=206, color=COL_DIM, value=16)
ui.Label("Seg7 กับ Bar", x=300, y=206, color=COL_DIM, value=16)
ui.Label("Slider Switch Checkbox", x=470, y=170, color=COL_DIM, value=16)
ui.Label("Spinner กับ Image", x=640, y=224, color=COL_DIM, value=16)

chart = ui.Chart(x=20, y=236, w=430, h=110, color=COL_INFO, min=0, max=100)

# Dropdown รับรายการตัวเลือกเป็นข้อความก้อนเดียว คั่นแต่ละตัวด้วย \n
# ส่วน value= ของมันคือขนาดตัวอักษร ไม่ใช่ตัวเลือกที่เลือกไว้ ตั้งไม่ได้จากตรงนี้
ui.Label("Dropdown มี 16 ชนิด", x=470, y=224, color=COL_DIM, value=16)
dd = ui.Dropdown("Label\nButton\nSlider\nSwitch\nCheckbox\nArc\nBar\nSpinner",
                 x=470, y=252, w=200, h=44, value=16)

count_lbl = ui.Label("ยังไม่ได้นับ widget", x=470, y=310, color=COL_DIM,
                     value=18)
ui.Label("Chart", x=20, y=352, color=COL_DIM, value=16)
btn = ui.Button("แตะฟังเสียง", x=200, y=352, w=150, h=36, color=COL_CARD,
                value=18)
note = ui.Label("อีกสองชนิดอยู่ไฟล์ 14", x=380, y=356, color=COL_DIM,
                value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>ตัวเลขตัวเดียว หลายวิธีเล่า</h2>")

# ui.list() คืนรายการ widget ที่มีอยู่บนจอตอนนี้ แต่ละตัวเป็น dict สองช่อง
# คือ id กับ type ใช้ตรวจว่าเราใช้โควตา 32 ตัวไปเท่าไรแล้ว โดยไม่ต้องนั่งนับเอง
live = ui.list()
count_lbl.color(COL_OK)
count_lbl.text("ui.list() นับได้ " + str(len(live)) + " ตัว จาก 32")
lcd.print("บนจอตอนนี้มี widget", len(live), "ตัว จากเพดาน 32 ตัว")
for w in live:
    lcd.print("  id", w["id"], "=", w["type"])

ui.poll()

t0 = time.ticks_ms()
value = 0
step = STEP
beeps = 0
taps = 0
was_high = False

while True:
    t_work = time.ticks_ms()
    if time.ticks_diff(t_work, t0) >= RUN_MS:
        break

    # ตัวเลขเดินขึ้นจนชนเพดานแล้วกลับลง นี่คือแหล่งข้อมูลเดียวของทั้งหน้าจอ
    value = value + step
    if value >= 100:
        value = 100
        step = -STEP
    elif value <= 0:
        value = 0
        step = STEP

    # --- ค่าเดียวกัน ส่งเข้าทุกชิ้น ---
    arc.value(value)                       # เต็มแค่ไหน
    bar.value(value)                       # เต็มแค่ไหน แบบเส้นตรง
    sld.value(value)                       # เต็มแค่ไหน และลากได้ด้วยนิ้ว
    seg.text(str(value))                   # เท่าไรพอดี - Seg7 รับข้อความเท่านั้น
    chart.set_next(0, value)               # ที่ผ่านมาเป็นยังไง

    # Compass คิดเป็นองศา 0 ถึง 359 ไม่ใช่เปอร์เซ็นต์ ต้องแปลงสเกลก่อนส่ง
    # และมันรับเฉพาะจำนวนเต็ม ส่งทศนิยมเข้าไปจะได้ TypeError
    comp.value(value * 359 // 100)

    # ไฟติดดับสองตัวตอบคำถามเดียวกันคนละหน้าตา คือ "ถึงเกณฑ์หรือยัง"
    high = value >= HI
    sw.value(1 if high else 0)
    chk.value(1 if high else 0)

    if high:
        arc.color(COL_WARN)
        bar.color(COL_WARN)
        seg.color(COL_WARN)
        img.icon(ICON_UP)
        img.color(COL_WARN)
    else:
        arc.color(COL_OK)
        bar.color(COL_OK)
        seg.color(COL_OK)
        img.icon(ICON_DOWN)
        img.color(COL_OK)

    # เล่นเสียงเฉพาะตอน "ข้ามเกณฑ์" ไม่ใช่ทุกรอบที่ค่าเกิน
    # ยิงทุกรอบเมื่อไร เสียงจะกลายเป็นเสียงหึ่งที่ไม่มีใครแยกออกว่าหมายถึงอะไร
    if high != was_high:
        was_high = high
        beeps = beeps + 1
        # ui.tone รับ "โน้ต MIDI" 0-127 ไม่ใช่ความถี่เป็นเฮิรตซ์ และรับแบบตำแหน่ง
        # เท่านั้น เขียน ui.tone(note=72) จะได้ TypeError ทันที
        # ลำดับคือ โน้ต, รูปคลื่น, ความแรง 0-127, ความยาวเป็น ms
        ui.tone(72 if high else 60, ui.WAVE_SINE, 90, 120)
        lcd.print("ข้ามเกณฑ์ที่ค่า", value, "-> ", "สูง" if high else "ต่ำ")

    # ui.poll() คืนรายการเหตุการณ์ที่เกิดขึ้นตั้งแต่ครั้งก่อน แต่ละตัวเป็น dict
    # สามช่อง คือ handle ของ widget ที่ถูกแตะ type ของเหตุการณ์ และ value
    # ถ้าไม่เรียกทุกรอบ เหตุการณ์จะกองอยู่จนเต็มคิวแล้วตัวใหม่จะถูกทิ้ง
    for ev in ui.poll():
        if ev["handle"] == btn.id() and ev["type"] == "clicked":
            taps = taps + 1
            # sfx คือเสียงสำเร็จรูป รับเป็นเลขค่าคงที่ ไม่ใช่ชื่อเป็นข้อความ
            ui.sfx(ui.SFX_UI_SELECT)
            lcd.print("<span class=ok>แตะปุ่มครั้งที่", taps, "</span>")

    status.color(COL_WARN if high else COL_DIM)
    status.text("ค่า " + str(value) + " | ข้ามเกณฑ์ " + str(beeps) +
                " ครั้ง | แตะปุ่ม " + str(taps))

    work = time.ticks_diff(time.ticks_ms(), t_work)
    left = TICK_MS - work
    if left > 0:
        time.sleep_ms(left)

# จบแล้วปล่อยค่าสุดท้ายค้างไว้ ไม่ล้างจอ คนดูจะได้อ่านทัน
status.color(COL_DIM)
status.text("จบแล้ว - ข้ามเกณฑ์ " + str(beeps) + " ครั้ง แตะปุ่ม " + str(taps))
note.text("Spinner ยังหมุนอยู่ เพราะมันไม่เคยรู้ว่างานจบ")
ui.poll()

lcd.console("<span class=muted>------------------------</span>")
lcd.print("<span class=ok>ข้ามเกณฑ์", beeps, "ครั้ง | แตะปุ่ม", taps,
          "ครั้ง</span>")
print("widget บนจอ", len(ui.list()), "ตัว | เพดาน 32 ตัว")

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# เพิ่มบรรทัด seg.value(50) เข้าไปในลูป แล้วรันใหม่ ตัวเลขบน Seg7 จะไม่เปลี่ยน
# ตามที่สั่ง และจะไม่มี error ขึ้นให้เห็นสักตัว จากนั้นลองสลับเป็น bar.text("50")
# แล้วตอบว่าเกิดอะไรขึ้น และเราจะรู้ล่วงหน้าได้อย่างไรว่าชนิดไหนรับอะไร
# ใบ้: ความเงียบไม่ได้แปลว่าสำเร็จ ที่พึ่งเดียวคือเอกสารกับการทดลองทีละชิ้น
