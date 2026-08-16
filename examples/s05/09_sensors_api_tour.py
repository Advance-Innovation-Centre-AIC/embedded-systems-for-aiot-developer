# 09_sensors_api_tour.py - เรียกทุกชื่อในโมดูล sensors แล้วดูว่าใครตอบ ใครปฏิเสธ
# ชุดตัวอย่างประจำคาบ 05
#
# ไฟล์นี้สอน: โมดูล sensors บน Eva Kit มีสิบห้าชื่อระดับบนสุด - เก้าชื่อเป็น
#             ฟังก์ชันที่เรียกตรงได้ (ไฟล์นี้เรียกครบทั้งเก้า สี่ตอบ ห้าปฏิเสธ)
#             อีกหกชื่อเป็นตัวเซนเซอร์ย่อยกับตัวช่วยวินิจฉัย - bmi270 bmm350
#             capsense pot bmm350_diag bmm350_debug ซึ่งมีเมธอดของตัวเองอีกชั้น
#             การปฏิเสธของห้าชื่อนั้นคือระบบกำลังปกป้องเรา
#             ไม่ใช่ข้อจำกัด - บัส I2C ของเซนเซอร์เป็นของคอร์จอ การขับมันจาก
#             สคริปต์เคยทำให้บอร์ดค้างถาวรจนต้องต่อดีบักเกอร์
# ดูที่จอ   : ตารางเก้าแถว แถวเขียวคือเรียกได้ แถวส้มคือถูกปฏิเสธอย่างสุภาพ
#             ทั้งเก้าแถวคือผลจากการเรียกจริงในรอบนี้ ไม่ใช่ตารางที่พิมพ์ค้างไว้
# กับดัก    : read_all() ไม่ได้ล้มเหลวและไม่ได้ทำอะไรใหม่ - บน Eva Kit มันคืน
#             ผลของ snapshot() ตัวเดียวกันเป๊ะ ๆ ส่วน auto_rate() ก็ไม่ error
#             แต่มันตั้งจังหวะให้ background task ที่บนบอร์ดนี้ไม่มีวันได้เริ่ม
#             "เรียกได้" กับ "มีผล" เป็นคนละเรื่องกัน

# ต่อจากคาบ 1: examples/s01/12_every_sense_at_once.py ใช้ sensors.snapshot() ไปแล้ว
#   หนึ่งครั้ง และบอกไว้สั้น ๆ ว่า init() กับ scan() ถูกปฏิเสธ ไฟล์นี้เปิดทั้งโมดูล
#   ให้ดู แล้วพิสูจน์คำนั้นด้วยการเรียกจริงทุกชื่อ ไม่ใช่เชื่อตามคอมเมนต์

import lcd
import sensors
import time
import ui

# ชื่อ snapshot ถูกลงทะเบียนเฉพาะบน Eva Kit เท่านั้น (อยู่ใต้ #ifdef ในเฟิร์มแวร์)
# จึงใช้เป็นเครื่องพิสูจน์ว่ากำลังรันอยู่บนบอร์ดไหน โดยไม่ต้องถามใคร
IS_EVA = hasattr(sensors, "snapshot")

lcd.clear()
lcd.console("<h2>ทัวร์โมดูล sensors</h2>")
lcd.print("บอร์ดนี้เป็น Eva Kit:", IS_EVA)

ui.screen()
ui.Label("sensors: ใครตอบ ใครปฏิเสธ", x=24, y=24, value=24)

NAMES = ("snapshot()", "read_all()", "auto_status()", "auto_rate(200)",
         "init()", "scan()", "push()", "live_push()", "auto()")

# เก้าแถวเรียงชิดกันเป็นตารางเดียว ระยะบรรทัด 22 คือความสูงของตัวอักษร 16 พอดี
# คอลัมน์นี้กว้างถึง x=440 คำตัดสินที่ยาวกว่านั้นจะไปทับคอลัมน์ขวา จึงต้องสั้น
# ประโยคเต็มของแต่ละข้ออยู่ในคอนโซล ซึ่งไม่มีเพดานความกว้าง
rows = []
for i in range(9):
    rows.append(ui.Label(NAMES[i] + "  ...", x=24, y=64 + i * 22, value=16,
                         color=0x9AA3AF))

# คอลัมน์ขวา: หลักฐานสามบรรทัด แล้วปุ่มสองตัว
lbl_keys = ui.Label("คีย์ที่ snapshot คืนมา: ...", x=456, y=64, value=16,
                    color=0x4A9EFF)
lbl_same = ui.Label("read_all = snapshot: ...", x=456, y=88, value=16,
                    color=0x4A9EFF)
lbl_auto = ui.Label("auto_status: ...", x=456, y=112, value=16, color=0x4A9EFF)

btn_again = ui.Button("ทดสอบอีกครั้ง", x=456, y=152, w=232, h=88,
                      color=0x4A9EFF, value=20)
btn_exit = ui.Button("ออก", x=456, y=272, w=232, h=88, color=0x9AA3AF,
                     value=20)

# การ์ดใจความสำคัญอยู่ใต้ตาราง Panel ต้องมาก่อนสองบรรทัดที่วางบนมันเสมอ
ui.Panel(x=24, y=276, w=408, h=88)
ui.Label("แถวส้มไม่ใช่ความผิดพลาดของเรา", x=40, y=296, value=16,
         color=0xF5A623)
ui.Label("ปฏิเสธเสียงดัง ดีกว่าบอร์ดค้างเงียบ", x=40, y=320, value=16,
         color=0xF5A623)
id_again = btn_again.id()
id_exit = btn_exit.id()

OK_COLOR = 0x30A46C
NO_COLOR = 0xF5A623


def verdict(i, ok, note):
    rows[i].text(NAMES[i] + "  " + note)
    rows[i].color(OK_COLOR if ok else NO_COLOR)


def run_tour():
    # 1) snapshot() - ทางหลักของบอร์ดนี้ ขอค่าที่คอร์จออ่านค้างไว้ผ่าน IPC
    #    ครั้งแรกหลังรีเซ็ตรอได้ถึงราว 16 วินาที เพราะคอร์จอเพิ่งตอบสายเซนเซอร์
    #    ที่ราว 13 วินาที - ระหว่างนั้นมันโยน OSError จึงต้องดักไว้เสมอ
    snap = None
    try:
        snap = sensors.snapshot()
        verdict(0, True, "OK " + str(len(snap)) + " กลุ่ม")
        # จำนวนคีย์ขึ้นกับบอร์ด รายการเต็มจึงยาวเกินคอลัมน์ได้เสมอ
        # ป้ายที่ยาวเกินไม่ error มันวิ่งเลยขอบจอไปเฉย ๆ จึงต้องตัดเองพร้อมจุดสามจุด
        # ให้เห็นว่าถูกตัด แล้วส่งรายการเต็มออกคอนโซลซึ่งไม่มีเพดานความกว้าง
        keys = ",".join(sorted(snap))
        lbl_keys.text("คีย์: " + (keys[:24] + "..." if len(keys) > 24
                                  else keys))
        lcd.print("คีย์ที่ snapshot คืนมาครบชุด:", keys)
    except OSError:
        verdict(0, False, "OSError - คอร์จอยังไม่ตอบ")
    except AttributeError:
        verdict(0, False, "ไม่มีชื่อนี้ (ไม่ใช่ Eva Kit)")

    # 2) read_all() - บน Eva Kit บรรทัดนี้เรียก snapshot() ต่อให้ตรง ๆ
    #    จึงคืน dict หน้าตาเดียวกัน ไม่ได้อ่านบัสเองและไม่ได้เพิ่มอะไรเลย
    try:
        all_d = sensors.read_all()
        verdict(1, True, "OK " + str(len(all_d)) + " กลุ่ม")
        if snap is not None:
            same = sorted(all_d) == sorted(snap)
            lbl_same.text("read_all = snapshot: " + str(same))
    except OSError:
        verdict(1, False, "OSError")

    # 3) auto_status() - ไม่ถูกปฏิเสธ คืน dict สี่ช่อง running/rate_ms/push_count/mask
    #    อย่าเดาค่าที่มันจะตอบ ให้อ่านจากบอร์ดตรงนี้ - บน Eva Kit เฟิร์มแวร์ตั้ง
    #    mask ไว้ที่ 8 (BMM350 ตัวเดียว) ตั้งแต่ตอนสร้าง task เพราะเข็มทิศอยู่คนละ
    #    บัส (I3C) กับที่คอร์จอถือไว้ ตัวเลข mask ที่เห็นจึงบอกได้ว่ามีใครถูกเปิดไว้บ้าง
    try:
        st = sensors.auto_status()
        verdict(2, True, "OK running=" + str(st["running"]))
        lbl_auto.text("auto_status: " + str(st["rate_ms"]) + " ms, push "
                      + str(st["push_count"]))
    except OSError:
        verdict(2, False, "OSError")

    # 4) auto_rate(ms) - ไม่ error เช่นกัน หนีบค่าไว้ 20..5000 อย่างเงียบ ๆ
    #    แต่บนบอร์ดนี้มันตั้งจังหวะให้งานที่ไม่มีวันได้เริ่ม เรียกได้ ไม่ได้แปลว่ามีผล
    try:
        sensors.auto_rate(200)
        verdict(3, True, "OK (แต่ไม่มีผลบนบอร์ดนี้)")
    except OSError:
        verdict(3, False, "OSError")

    # 5-9) ห้าชื่อที่เฟิร์มแวร์ปิดประตูไว้บน Eva Kit
    #      ทั้งห้าตัวจบด้วยการขับบัส SCB0 ซึ่งคอร์จอถือไว้ - ตัวที่แย่ที่สุดคือ auto()
    #      เพราะมันไม่ได้ขับบัสเอง แต่ไปเปิด background task ที่ขับบัสแทน
    #      แล้วอาการค้างจะอยู่ต่อไปหลังบรรทัดนั้นจบไปแล้ว
    try:
        sensors.init()
        verdict(4, True, "ผ่าน (ไม่ใช่ Eva Kit)")
    except OSError:
        verdict(4, False, "OSError - CM55 เป็นเจ้าของบัส")

    try:
        sensors.scan()
        verdict(5, True, "ผ่าน (ไม่ใช่ Eva Kit)")
    except OSError:
        # ประโยคเต็มยาวเกินคอลัมน์ จึงเหลือใจความไว้บนจอ แล้วส่งฉบับเต็มออกคอนโซล
        verdict(5, False, "OSError - ไล่สแกนทั้งบัสคือทางลัดพัง")
        lcd.print("scan() ไล่ที่อยู่ 112 ตัวบนบัสที่คอร์จอถืออยู่ "
                  "ซึ่งเป็นทางลัดที่สุดไปสู่บัสที่พัง")

    if IS_EVA:
        # เรียกเฉพาะบน Eva Kit ที่รู้แน่ว่ามันจะปฏิเสธทันที
        # บนบอร์ดอื่น push() ส่งข้อมูลจริง และ live_push() วนไม่รู้จบจนกด Ctrl+C
        # ส่วน auto() เปิด background task ค้างไว้ - ไม่ใช่ของที่ควรลองสุ่ม ๆ
        try:
            sensors.push()
            verdict(6, True, "ผ่าน")
        except OSError:
            verdict(6, False, "OSError - คอร์จอมีค่าพวกนี้อยู่แล้ว")

        try:
            sensors.live_push()
            verdict(7, True, "ผ่าน")
        except OSError:
            verdict(7, False, "OSError - ประตูเดียวกับ push()")

        try:
            sensors.auto()
            verdict(8, True, "ผ่าน")
        except OSError:
            verdict(8, False, "OSError - อันตรายที่สุดในห้าตัว")
    else:
        verdict(6, False, "ข้าม - ไม่ใช่ Eva Kit")
        verdict(7, False, "ข้าม - บอร์ดอื่นจะวนไม่รู้จบ")
        verdict(8, False, "ข้าม - บอร์ดอื่นจะเปิด task ค้างไว้")

    # ของที่ยังใช้ได้ตามปกติทุกอย่าง - ปิดท้ายให้เห็นว่าไม่ได้เสียอะไรไปเลย
    try:
        lcd.print("pot:", sensors.pot.read(), "|",
                  round(sensors.pot.percent(), 1), "% |",
                  round(sensors.pot.voltage(), 3), "V")
        b0, b1 = sensors.capsense.buttons()
        lcd.print("capsense: btn0", b0, "btn1", b1, "slider",
                  sensors.capsense.slider())
        lcd.print("capsense.read():", sensors.capsense.read())
    except OSError:
        lcd.print("เซนเซอร์ยังไม่พร้อม ลองกดทดสอบอีกครั้ง")


run_tour()

running = True
while running:
    for ev in ui.poll():
        h = ev['handle']
        if h == id_again:
            run_tour()
        elif h == id_exit:
            running = False
    time.sleep_ms(200)

ui.clear()
print("ห้าชื่อที่ถูกปฏิเสธ ไม่ได้หายไปจากโมดูล มันยังอยู่และยังเรียกได้")
print("สิ่งที่เปลี่ยนคือมันตอบว่า 'ไม่' แทนที่จะพาบอร์ดไปค้าง")
