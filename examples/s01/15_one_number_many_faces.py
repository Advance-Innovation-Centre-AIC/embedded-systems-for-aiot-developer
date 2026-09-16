# 15_one_number_many_faces.py - ตัวเลขตัวเดียว กับสิบวิธีที่จอเล่ามันออกมา
#
# ไฟล์นี้ไม่มีเซนเซอร์ ไม่มีเน็ต มีแต่ตัวเลขหนึ่งตัวที่เดินขึ้นลงเอง
# ของใหม่ทั้งหมดคือชนิดของ widget ที่ยังไม่เคยเจอในไฟล์ก่อนหน้า
# Slider Switch Checkbox Spinner Image Compass และเสียงจาก ui.tone
#
# ที่จอนี้ไม่มี: Dropdown เพราะมันไม่ได้เล่าค่า มันเป็นรายการให้เลือกเฉย ๆ
#   จอนี้เต็มแล้วด้วยของที่ขยับตามตัวเลข ตัวที่ไม่ขยับจึงถูกยกไปไว้ที่
#   examples/s04/08_dropdown_textarea.py ซึ่งเป็นคาบของมันจริง ๆ
#
# ไฟล์นี้สอน: ค่าเดียวกันเล่าได้หลายแบบ และแต่ละแบบตอบคำถามคนละข้อ
#             วงแหวนตอบว่า "เต็มแค่ไหน" ตัวเลขตอบว่า "เท่าไรพอดี"
#             กราฟตอบว่า "ที่ผ่านมาเป็นยังไง" ไฟติดดับตอบว่า "ถึงเกณฑ์หรือยัง"
# ดูที่จอ   : ทุกชิ้นขยับพร้อมกันจากตัวเลขตัวเดียว แตะปุ่มแถวล่างเพื่อฟังเสียง
#             และบรรทัด ui.list() บอกว่าตอนนี้จอมี widget อยู่กี่ตัว จากเพดาน 64
# กับดัก    : seg.value(50) ได้ 50 ไม่ใช่ 50.0 - ทศนิยมต้องส่งเป็นข้อความ
#             ส่วน Slider Arc Bar Switch รับเฉพาะ .value() ไม่รับ .text()
#             Checkbox รับทั้งสองอย่าง และต้องใช้ .text() ถ้าข้อความเป็นภาษาไทย
#             ชนิดไหนรับอะไร ไม่มีทางรู้จากการรันแล้วดูว่ามี error ไหม เพราะไม่มี

import lcd
import time
import ui

RUN_MS = 36000       # เดินนานเท่าไร
TICK_MS = 120        # คาบของลูป
STEP = 4             # ตัวเลขขยับทีละเท่าไร
HI, LO = 70, 30      # เกณฑ์บนกับล่าง ใช้จุดไฟติดดับกับเล่นเสียง

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_INFO = 0x30A46C, 0xF5A623, 0x4A9EFF

# ชื่อไอคอนที่เฟิร์มแวร์มีให้ใช้ ใส่ชื่อที่ไม่มีตอนสร้างจะได้ RuntimeError ทันที
# แต่ถ้าใส่ชื่อผิดทีหลังผ่าน .icon() จะเงียบสนิท ไม่มีอะไรเกิดขึ้นและไม่มี error
ICON_UP, ICON_DOWN = "arrow_up", "arrow_down"

ui.screen()
time.sleep_ms(200)

ui.Label("ตัวเลขตัวเดียว หลายวิธีเล่า", x=24, y=24, color=COL_TEXT, value=24)
status = ui.Label("กำลังจะเริ่มเดิน", x=456, y=32, color=COL_DIM, value=20)

# Panel เป็นพื้นหลังของการ์ด ของอื่นวางทับมันได้โดยตั้งใจ
# ลำดับสำคัญ ต้องสร้าง Panel ก่อนของที่จะวางบนมัน ไม่งั้นมันจะไปบังของที่มีอยู่
# การ์ดใบนี้กว้าง 744 เว้นขอบใน 16 ทุกด้าน ของข้างในจึงอยู่ในช่วง 40 ถึง 752
ui.Panel(x=24, y=72, w=744, h=160, color=COL_CARD, min=COL_DIM, max=12,
         value=1)

# Arc กับ Compass ถ้าไม่ใส่ w จะได้ 150x150 มาเลย ซึ่งใหญ่เกินกว่าจะวางสองชิ้น
# และ Compass ใช้ w เป็นเส้นผ่านศูนย์กลาง ส่วน h มันไม่สนใจ วงกลมเสมอ
arc = ui.Arc(x=40, y=88, w=88, h=88, min=0, max=100, value=0)
arc.color(COL_OK)
comp = ui.Compass(x=144, y=88, w=88, h=88)
comp.color(COL_INFO)

seg = ui.Seg7(text="0", x=248, y=88, w=152, h=64, color=COL_OK)
bar = ui.Bar(x=248, y=160, w=152, h=16, min=0, max=100, value=0)
bar.color(COL_OK)

# รางเลื่อนหนา 24 แล้วจัดกลางแถบ ไม่ใช่ 88 - นิ้วจับที่หัวเลื่อน ไม่ได้จับที่
# ความหนาของราง รางที่หนา 88 กินพื้นที่เท่าการ์ดใบหนึ่งโดยไม่ได้กดง่ายขึ้นเลย
sld = ui.Slider(x=416, y=120, w=192, h=24, min=0, max=100, value=0)

# Spinner ไม่มีค่าให้ตั้ง มันหมุนของมันเองตลอด งานเดียวของมันคือบอกว่า "ยังทำอยู่"
spin = ui.Spinner(x=624, y=100, w=64, h=64)

# Image รับชื่อไอคอนเป็นข้อความตอนสร้าง แล้วเปลี่ยนทีหลังด้วย .icon()
img = ui.Image(ICON_UP, x=704, y=104, w=48, h=48, color=COL_OK)

ui.Label("Arc", x=40, y=184, color=COL_DIM, value=16)
ui.Label("Compass", x=144, y=184, color=COL_DIM, value=16)
ui.Label("Seg7 กับ Bar", x=248, y=184, color=COL_DIM, value=16)
ui.Label("Slider", x=416, y=184, color=COL_DIM, value=16)
ui.Label("Spinner", x=608, y=184, color=COL_DIM, value=16)
ui.Label("Image", x=696, y=184, color=COL_DIM, value=16)

# แถวล่างอยู่นอกการ์ด เป็นของที่ต้องแตะได้ จึงสูง 88 และห่างกันอย่างน้อย 32
chart = ui.Chart(x=24, y=248, w=264, h=88, color=COL_INFO, min=0, max=100)
btn = ui.Button("แตะฟังเสียง", x=320, y=248, w=152, h=88, color=0x3A4150,
                value=20)
sw = ui.Switch(x=504, y=248, w=88, h=88)

# กับดักที่เห็นได้จากภาพอย่างเดียว: ข้อความไทยที่ใส่ตอนสร้าง Checkbox จะออกมา
# เป็นกล่องเปล่าเรียงกัน เพราะทางสร้างของมันไม่ได้เลือกฟอนต์ไทยให้ ต่างจาก
# Button ที่เลือกให้ ทางแก้คือสร้างด้วย ASCII ไว้ก่อน แล้วส่งข้อความไทยตาม
# ด้วย .text() ซึ่งเป็นคนละเส้นทางและเลือกฟอนต์ไทยให้ถูกต้อง
chk = ui.Checkbox("over HI", x=624, y=248, w=144, h=88, color=COL_TEXT)
chk.text("เกินเกณฑ์บน")

ui.Label("Chart", x=24, y=344, color=COL_DIM, value=16)
count_lbl = ui.Label("ยังไม่ได้นับ widget", x=112, y=344, color=COL_DIM,
                     value=16)
ui.Label("Switch Checkbox", x=504, y=344, color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>ตัวเลขตัวเดียว หลายวิธีเล่า</h2>")

# ui.list() คืนรายการ widget ที่มีอยู่บนจอตอนนี้ แต่ละตัวเป็น dict สองช่อง
# คือ id กับ type ใช้ตรวจว่าเราใช้โควตา 32 ตัวไปเท่าไรแล้ว โดยไม่ต้องนั่งนับเอง
live = ui.list()
count_lbl.color(COL_OK)
count_lbl.text("ui.list() นับได้ " + str(len(live)) + " ตัว จาก 64")
lcd.print("บนจอตอนนี้มี widget", len(live), "ตัว จากเพดาน 64 ตัว")
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
    seg.text(str(value))                   # เท่าไรพอดี - ส่งข้อความเพื่อคุมรูปแบบ
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

    # ข้อความบรรทัดนี้อยู่มุมขวาบน มีที่ให้ราว 30 ตัวอักษร ยาวกว่านั้นจะล้นขอบจอ
    # ไปโดยไม่มีใครเตือน ป้ายที่ล้นไม่ได้ error มันแค่หายไปครึ่งหนึ่ง
    status.color(COL_WARN if high else COL_DIM)
    status.text("ค่า " + str(value) + " | ข้าม " + str(beeps) +
                " | แตะ " + str(taps))

    work = time.ticks_diff(time.ticks_ms(), t_work)
    left = TICK_MS - work
    if left > 0:
        time.sleep_ms(left)

# จบแล้วปล่อยค่าสุดท้ายค้างไว้ ไม่ล้างจอ คนดูจะได้อ่านทัน
status.color(COL_DIM)
status.text("จบแล้ว | ข้าม " + str(beeps) + " | แตะ " + str(taps))
ui.poll()

# ประโยคยาว ๆ ไม่ควรอยู่บนแผงจอที่มีที่ว่างจำกัด มันไปอยู่ในคอนโซลซึ่งมีฟอนต์
# ครบและกว้างไม่จำกัด ส่วนแผงจอเก็บไว้ให้ค่าที่ต้องอ่านเร็ว
lcd.print("Spinner ยังหมุนอยู่ เพราะมันไม่เคยรู้ว่างานจบ")
lcd.print("อีกสองชนิดอยู่ไฟล์ 14 ส่วน Dropdown อยู่ examples/s04/08")

lcd.console("<span class=muted>------------------------</span>")
lcd.print("<span class=ok>ข้ามเกณฑ์", beeps, "ครั้ง | แตะปุ่ม", taps,
          "ครั้ง</span>")
print("widget บนจอ", len(ui.list()), "ตัว | เพดาน 64 ตัว")

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# เพิ่มบรรทัด seg.value(50) เข้าไปในลูป แล้วรันใหม่ ตัวเลขบน Seg7 จะไม่เปลี่ยน
# ตามที่สั่ง และจะไม่มี error ขึ้นให้เห็นสักตัว จากนั้นลองสลับเป็น bar.text("50")
# แล้วตอบว่าเกิดอะไรขึ้น และเราจะรู้ล่วงหน้าได้อย่างไรว่าชนิดไหนรับอะไร
# ใบ้: ความเงียบไม่ได้แปลว่าสำเร็จ ที่พึ่งเดียวคือเอกสารกับการทดลองทีละชิ้น
