# 02_capsense_menu_wet_hand.py - เมนูสัมผัสสองปุ่ม และเรื่องมือเปียก
# ชุดตัวอย่างประจำคาบ 05
#
# Why : แผงคุมเครื่องล้างจาน ไมโครเวฟ และตู้กดน้ำในโรงพยาบาล ต้องเช็ดฆ่าเชื้อได้ทั้งแผง
#       ปุ่มกลไกมีร่องให้น้ำและคราบเข้า จึงถูกแทนด้วยแผ่นสัมผัสใต้กระจกเรียบ
#       แต่แผ่นสัมผัสไม่มีแรงต้านให้นิ้วรู้สึก ผู้ใช้จึงไม่รู้ว่ากดติดหรือยัง
# What: เมนูสี่รายการที่เลื่อนด้วยปุ่มสัมผัสสองปุ่ม และตอบกลับทุกครั้งด้วยจอ ไฟ เสียง
#       การตอบกลับไม่ใช่ของแถม มันคือสิ่งที่มาแทนแรงต้านของปุ่มกลไกที่หายไป
#
# บน Eva Kit: sensors.init() และ sensors.scan() ปฏิเสธด้วย OSError ไม่ต้องเรียก
#   ส่วนตัวอ่านของแต่ละเซนเซอร์ใช้ได้เลย - มันอ่านจาก snapshot ที่ CM55 ส่งมาทาง IPC
#   (modsensors.c) หลังรีเซ็ต การอ่านครั้งแรกช้าได้ถึงราว 16 วินาที
#   และอาจโยน OSError ระหว่างนั้น
#
# ดูที่จอ: เมนูสี่แถว แถวที่เลือกเป็นสีเขียวมี > นำหน้า ที่เหลือเป็นสีเทา
#         ขวามือมีแถบนับรอบที่แตะค้างเทียบกับเกณฑ์ และป้ายสถานะที่เปลี่ยนเป็นแดงเมื่อค้างผิดปกติ
# กับดัก : น้ำบนแผ่นสัมผัสมีค่าคงตัวไดอิเล็กทริกสูง เครื่องจะอ่านว่ามีนิ้วแตะค้าง
#         ปุ่มสัมผัสจึงพังในครัวเปียกและกับมือใส่ถุงมือหนา นี่คือข้อจำกัดจริงของเทคโนโลยี

import gpio
import lcd
import sensors
import time
import ui

MENU = ("อุณหภูมิ", "ความเร็วพัดลม", "ตั้งเวลา", "ล้างถัง")
STUCK_ROUNDS = 40  # แตะค้างเกินนี้ถือว่าผิดปกติ เช่น มีน้ำหยดอยู่บนแผ่น

PICK = 0x00E676    # สีของแถวที่เลือกอยู่
DIM = 0x777777     # สีของแถวที่ไม่ได้เลือก

lcd.clear()
lcd.console("<h2>เมนูสัมผัสสองปุ่ม</h2>")
lcd.print("ซ้าย = ขึ้น | ขวา = ลง | ทั้งหมด", len(MENU), "รายการ")

# ---- หน้าจอ: เมนูสี่แถวคือตัวเมนูเอง ไม่ใช่คำบรรยายของเมนู ------------------
ui.screen()
ui.Label("เมนูสัมผัสสองปุ่ม", x=12, y=6, value=24)
ui.Panel(x=12, y=44, w=396, h=196)   # พาเนลต้องสร้างก่อนป้ายที่วางทับบนมัน
rows = [ui.Label("", x=28, y=58 + i * 46, value=24) for i in range(len(MENU))]

ui.Label("CAP1 = เลื่อนขึ้น", x=424, y=50, value=16, color=0x00BFFF)
ui.Label("CAP2 = เลื่อนลง", x=424, y=74, value=16, color=0x00BFFF)
ui.Label("แตะค้างมาแล้วกี่รอบ", x=424, y=112, value=16)
hold_bar = ui.Bar(x=424, y=138, w=250, h=18, min=0, max=STUCK_ROUNDS, value=0)
hold_txt = ui.Label("0 / " + str(STUCK_ROUNDS), x=424, y=162, value=16,
                    color=DIM)
warn = ui.Label("สถานะ: ปกติ", x=424, y=196, value=20, color=PICK)

ui.Panel(x=12, y=254, w=662, h=76)
# สามป้ายแทนหนึ่งก้อน เพราะ ui.Label ตัดข้อความที่เกิน 126 ไบต์ทิ้งเงียบ ๆ
# และภาษาไทยหนึ่งตัวอักษรกิน 3 ไบต์ - 32 ตัวก็เต็มโควตาแล้ว
ui.Label("กับดัก: น้ำบนแผ่นอ่านเหมือนนิ้ว", x=24, y=260, value=18,
         color=0xFFC107)
ui.Label("แตะค้าง ปุ่มสัมผัสจึงพัง", x=24, y=284, value=18, color=0xFFC107)
ui.Label("ในครัวเปียก และกับถุงมือหนา", x=24, y=308, value=18, color=0xFFC107)


def draw_menu(sel):
    # วาดทั้งสี่แถวใหม่ทุกครั้ง ผู้ใช้จะได้เห็นทั้งเมนู ไม่ใช่เห็นแค่บรรทัดที่เลือก
    for i, name in enumerate(MENU):
        rows[i].text(("> " if i == sel else "   ") + name)
        rows[i].color(PICK if i == sel else DIM)


idx = 0
prev = (False, False)
held0 = 0
held1 = 0
shown_hold = -1
stuck = False

draw_menu(idx)
ui.poll()
lcd.print("> " + MENU[idx])

for _ in range(900):
    b0, b1 = sensors.capsense.buttons()

    # นับว่าแต่ละปุ่มถูกแตะค้างมากี่รอบแล้ว
    held0 = held0 + 1 if b0 else 0
    held1 = held1 + 1 if b1 else 0

    # ทำงานตอนขอบขาขึ้นเท่านั้น ไม่ใช่ตลอดเวลาที่นิ้วอยู่
    if b0 and not prev[0]:
        idx = (idx - 1) % len(MENU)
        gpio.led(1).on()
        ui.tone(76, ui.WAVE_SINE, 90, 60)
        draw_menu(idx)
        lcd.print("> " + MENU[idx])
    elif b1 and not prev[1]:
        idx = (idx + 1) % len(MENU)
        gpio.led(1).on()
        ui.tone(72, ui.WAVE_SINE, 90, 60)
        draw_menu(idx)
        lcd.print("> " + MENU[idx])
    elif not b0 and not b1:
        gpio.led(1).off()

    # แถบนี้คือคำตอบว่า "แตะค้างมานานแค่ไหนแล้ว" ซึ่งนิ้วบอกเองไม่ได้
    held = held0 if held0 > held1 else held1
    if held != shown_hold:
        shown_hold = held
        hold_bar.value(held if held < STUCK_ROUNDS else STUCK_ROUNDS)
        hold_txt.text(str(held) + " / " + str(STUCK_ROUNDS))

    # ตรวจอาการ "แตะค้างไม่เลิก" ซึ่งในครัวจริงมักแปลว่ามีน้ำอยู่บนแผ่น
    if held0 == STUCK_ROUNDS or held1 == STUCK_ROUNDS:
        stuck = True
        gpio.led(0).on()
        warn.text("แตะค้างผิดปกติ\nเช็ดแผ่นสัมผัส")
        warn.color(0xFF5555)
        lcd.print("<span class=muted>แตะค้างผิดปกติ - เช็ดแผ่นสัมผัส</span>")
    elif not b0 and not b1:
        gpio.led(0).off()
        if stuck:
            stuck = False
            warn.text("สถานะ: ปกติ")
            warn.color(PICK)

    prev = (b0, b1)
    ui.poll()
    time.sleep_ms(60)

gpio.led(0).off()
gpio.led(1).off()
print("เลือกไว้ที่:", MENU[idx])
