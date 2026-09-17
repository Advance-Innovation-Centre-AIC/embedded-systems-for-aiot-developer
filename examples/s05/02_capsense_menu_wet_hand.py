# 02_capsense_menu_wet_hand.py - เมนูสัมผัสสองปุ่ม และเรื่องมือเปียก
# ชุดตัวอย่างประจำคาบ 05
#
# Why : แผงคุมเครื่องล้างจาน ไมโครเวฟ และตู้กดน้ำในโรงพยาบาล ต้องเช็ดฆ่าเชื้อได้ทั้งแผง
#       ปุ่มกลไกมีร่องให้น้ำและคราบเข้า จึงถูกแทนด้วยแผ่นสัมผัสใต้กระจกเรียบ
#       แต่แผ่นสัมผัสไม่มีแรงต้านให้นิ้วรู้สึก ผู้ใช้จึงไม่รู้ว่ากดติดหรือยัง
# What: เมนูสี่รายการที่เลื่อนด้วยปุ่มสัมผัสสองปุ่ม และตอบกลับทุกครั้งด้วยจอ ไฟ เสียง
#       การตอบกลับไม่ใช่ของแถม มันคือสิ่งที่มาแทนแรงต้านของปุ่มกลไกที่หายไป
#
# sensors.capsense.* ใช้ได้เลยทั้งสองบอร์ดโดยไม่ต้องเรียก sensors.init() (บน Eva Kit
#   เรียกแล้วถูกปฏิเสธด้วย OSError เสียด้วยซ้ำ) แต่หลังรีเซ็ต การอ่านครั้งแรกอาจต้อง
#   รอคอร์จอตอบ และอาจโยน OSError ระหว่างนั้น
#
# ดูที่จอ: เมนูสี่แถว แถวที่เลือกมี > นำหน้าและใช้สีเน้น ที่เหลือเป็นสีข้อความรอง
#         เครื่องหมาย > คือสิ่งที่บอกแถวที่เลือกได้แม้พิมพ์จอออกมาเป็นขาวดำ
#         ขวามือมีแถบนับรอบที่แตะค้างเทียบกับเกณฑ์ และป้ายสถานะที่เปลี่ยนเมื่อค้างผิดปกติ
# กับดัก : น้ำบนแผ่นสัมผัสมีค่าคงตัวไดอิเล็กทริกสูง เครื่องจะอ่านว่ามีนิ้วแตะค้าง
#         ปุ่มสัมผัสจึงพังในครัวเปียกและกับมือใส่ถุงมือหนา นี่คือข้อจำกัดจริงของเทคโนโลยี

import gpio
import lcd
import sensors
import time
import ui

MENU = ("อุณหภูมิ", "ความเร็วพัดลม", "ตั้งเวลา", "ล้างถัง")
STUCK_ROUNDS = 40  # แตะค้างเกินนี้ถือว่าผิดปกติ เช่น มีน้ำหยดอยู่บนแผ่น

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
COL_TEXT = 0xE8EAED      # ข้อความหลัก
COL_DIM = 0x9AA3AF       # ข้อความรอง เชิงอรรถ และแถวที่ไม่ได้เลือก
COL_CARD = 0x171B22      # พื้นการ์ด
COL_ACCENT = 0x4A9EFF    # แถวที่เลือกอยู่ และแถบที่กำลังเปลี่ยน
COL_BAD = 0xE5484D       # ผิดปกติ - จอนี้ใช้กับอาการแตะค้างเท่านั้น

# ไฟสองดวง: ดวง "ตอบรับ" ติดตอนนิ้วแตะติด ดวง "เตือน" ติดตอนแตะค้างผิดปกติ
# บอร์ดที่รายงานครบ RGB_RED / RGB_GREEN / RGB_BLUE (Dev Kit) หาดวงจากชื่อ เพราะเลขดวง
# อาจเปลี่ยนในรุ่นถัดไป ส่วนบอร์ดที่ไม่ครบ (Eva Kit: ดวง 0 แดง ดวง 1 เขียว) ใช้เลขตรง ๆ
LED_NAMES = gpio.board_info()["led_names"]
RGB_FULL = all(n in LED_NAMES for n in ("RGB_RED", "RGB_GREEN", "RGB_BLUE"))
led_warn = gpio.led(LED_NAMES.index("RGB_RED") if RGB_FULL else 0)
led_ack = gpio.led(LED_NAMES.index("RGB_GREEN") if RGB_FULL else 1)

lcd.clear()
lcd.console("<h2>เมนูสัมผัสสองปุ่ม</h2>")
lcd.print("ซ้าย = ขึ้น | ขวา = ลง | ทั้งหมด", len(MENU), "รายการ")

# ---- หน้าจอ: เมนูสี่แถวคือตัวเมนูเอง ไม่ใช่คำบรรยายของเมนู ------------------
# ผังเดินบนกริด 8 ขอบนอก 24 - การ์ดเมนูกับการ์ดกับดักเรียงลงมาทางซ้าย
# คอลัมน์ขวาเป็นเครื่องวัดการแตะค้าง ซึ่งเป็นคนละเรื่องกับการเลือกเมนู
ui.screen()
ui.Label("เมนูสัมผัสสองปุ่ม", x=24, y=8, color=COL_TEXT, value=28)

ui.Panel(x=24, y=56, w=432, h=192, color=COL_CARD, min=COL_CARD, max=12,
         value=1)                    # value= ของ Panel คือความหนาขอบ ไม่ใช่ฟอนต์
# แถวห่างกัน 44 เพราะตัวอักษรขนาด 24 สูงราว 32 - เว้น 12 ให้ตาแยกบรรทัดออก
rows = [ui.Label("", x=40, y=72 + i * 44, color=COL_DIM, value=24)
        for i in range(len(MENU))]

ui.Panel(x=24, y=264, w=432, h=96, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
# กล่องกับดักคือคำอธิบายที่อยู่นิ่ง ไม่ใช่สถานะ จึงใช้สีข้อความรอง ไม่ใช่สีเฝ้าระวัง
# ถ้าทาสีเตือนให้ข้อความที่ไม่เคยเปลี่ยน สีเตือนจริงบนจอเดียวกันจะหมดความหมาย
ui.Label("กับดัก: น้ำบนแผ่นอ่านเหมือนนิ้ว", x=40, y=280, color=COL_DIM,
         value=20)
ui.Label("จึงพังในครัวเปียก และถุงมือหนา", x=40, y=312, color=COL_DIM,
         value=20)

ui.Label("CAP1 = เลื่อนขึ้น", x=480, y=56, color=COL_DIM, value=20)
ui.Label("CAP2 = เลื่อนลง", x=480, y=88, color=COL_DIM, value=20)
ui.Label("แตะค้างมาแล้วกี่รอบ", x=480, y=136, color=COL_DIM, value=20)
hold_bar = ui.Bar(x=480, y=168, w=208, h=32, min=0, max=STUCK_ROUNDS, value=0,
                  color=COL_ACCENT)
hold_txt = ui.Label("0 / " + str(STUCK_ROUNDS), x=480, y=208, color=COL_DIM,
                    value=20)
# สถานะปกติต้องเงียบ (§S7.7.3) จึงเป็นสีข้อความรอง ไม่ใช่สีเขียว
# สีแดงบนจอนี้ปรากฏได้ที่เดียวคือตอนแตะค้างผิดปกติ จึงยังแปลว่าผิดปกติจริง
warn = ui.Label("สถานะ: ปกติ", x=480, y=256, color=COL_DIM, value=24)


def draw_menu(sel):
    # วาดทั้งสี่แถวใหม่ทุกครั้ง ผู้ใช้จะได้เห็นทั้งเมนู ไม่ใช่เห็นแค่บรรทัดที่เลือก
    for i, name in enumerate(MENU):
        rows[i].text(("> " if i == sel else "   ") + name)
        rows[i].color(COL_ACCENT if i == sel else COL_DIM)


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
        led_ack.on()
        ui.tone(76, ui.WAVE_SINE, 90, 60)
        draw_menu(idx)
        lcd.print("> " + MENU[idx])
    elif b1 and not prev[1]:
        idx = (idx + 1) % len(MENU)
        led_ack.on()
        ui.tone(72, ui.WAVE_SINE, 90, 60)
        draw_menu(idx)
        lcd.print("> " + MENU[idx])
    elif not b0 and not b1:
        led_ack.off()

    # แถบนี้คือคำตอบว่า "แตะค้างมานานแค่ไหนแล้ว" ซึ่งนิ้วบอกเองไม่ได้
    held = held0 if held0 > held1 else held1
    if held != shown_hold:
        shown_hold = held
        hold_bar.value(held if held < STUCK_ROUNDS else STUCK_ROUNDS)
        hold_txt.text(str(held) + " / " + str(STUCK_ROUNDS))

    # ตรวจอาการ "แตะค้างไม่เลิก" ซึ่งในครัวจริงมักแปลว่ามีน้ำอยู่บนแผ่น
    if held0 == STUCK_ROUNDS or held1 == STUCK_ROUNDS:
        stuck = True
        led_warn.on()
        warn.text("แตะค้างผิดปกติ\nเช็ดแผ่นสัมผัส")
        warn.color(COL_BAD)
        lcd.print("<span class=muted>แตะค้างผิดปกติ - เช็ดแผ่นสัมผัส</span>")
    elif not b0 and not b1:
        led_warn.off()
        if stuck:
            stuck = False
            warn.text("สถานะ: ปกติ")
            warn.color(COL_DIM)

    prev = (b0, b1)
    ui.poll()
    time.sleep_ms(60)

led_warn.off()
led_ack.off()
print("เลือกไว้ที่:", MENU[idx])
