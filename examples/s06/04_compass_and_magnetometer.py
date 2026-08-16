# 04_compass_and_magnetometer.py - เข็มทิศบนบอร์ด และตัวเลขหนึ่งตัวที่ยังไม่มีใครตอบได้
# ชุดตัวอย่างประจำคาบ 06
#
# ไฟล์นี้สอน: sensors.bmm350 มีห้าคำสั่ง magnetic() heading() chip_id()
#             cal_reset() cal_status() - ทั้งห้าใช้ได้บน Eva Kit
#             และ dsp.compass() คือฟังก์ชันคนละตัวกับ bmm350.heading()
# ดูที่จอ   : แถวบนเป็นสามคอลัมน์ ซ้ายคือสามแกนสนามแม่เหล็กและขนาดรวม
#             กลางคือ heading() ของไดรเวอร์เทียบกับ dsp.compass() ซึ่ง
#             "ไม่เท่ากัน" และนั่นถูกต้องแล้ว ขวาคือสถานะการสอบเทียบ
#             แถวล่างซ้ายคือการ์ดที่บอกข้อบกพร่องที่ยังค้างอยู่ ล่างขวาคือปุ่ม
# กับดัก    : (1) dsp.compass(mx, my, mz) รับสามค่า แต่ทิ้ง mz ทั้งดุ้น
#                 ในซอร์สเขียนไว้ว่า reserved for tilt compensation - แปลว่ามันยัง
#                 ไม่ได้ชดเชยการเอียง เอียงบอร์ดเมื่อไร ทิศที่ได้เพี้ยนทันที
#             (2) heading() ของไดรเวอร์ใช้ atan2(x, y) ส่วน dsp.compass() ใช้
#                 atan2(y, x) - คนละสูตร จึงคนละคำตอบ ไม่ใช่ตัวใดตัวหนึ่งพัง
#             (3) การสอบเทียบจะ valid เองได้โดยเราไม่ได้สั่ง เพราะ background task
#                 ของบอร์ดป้อนค่าให้ตัวสะสมอยู่เบื้องหลังตลอดเวลา
#
# ข้อบกพร่องที่ยังไม่มีข้อสรุป - พูดให้ตรงตั้งแต่ต้น
#   ภาพจากบอร์ดจริงแสดงขนาดสนามราว 1532 ในขณะที่สนามแม่เหล็กโลกอยู่ที่ 25-65 uT
#   สิ่งที่ยืนยันแล้ว: ทิศที่ได้ถูกต้อง (วัดได้ 215.8 องศาตรงกับของจริง) แปลว่า
#   อัตราส่วนระหว่างแกนถูก ระบบแกนถูก สิ่งที่ยังไม่ยืนยัน: ตัวเลขขนาดกับหน่วย uT
#   ที่เขียนกำกับไว้ จะถูกทั้งคู่ไม่ได้ ในซอร์สมีการหารด้วยค่าคงที่สองตัวคือ 14.55
#   (แกน X,Y) และ 9.0 (แกน Z) โดยคอมเมนต์เขียนเองว่า approximate, without OTP
#   calibration และ "สำหรับ atan2 แค่นี้พอ" - ยังไม่มีใครตัดสินว่าตัวคงที่ผิด
#   หรือป้ายหน่วยผิด อย่าสอนตัวเลขขนาดเป็นข้อเท็จจริงจนกว่าจะมีคนวัดเทียบ

import dsp
import lcd
import math
import sensors
import time
import ui

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
# สองทิศไม่ได้แยกกันด้วยสี แต่แยกด้วยชื่อฟังก์ชันที่เขียนอยู่ในบรรทัดเดียวกัน
# แปลงเป็นขาวดำแล้วยังอ่านออกทั้งคู่ ตาม §S7.7.1
COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_ACCENT = 0x4A9EFF
COL_WARN = 0xF5A623
COL_BAD = 0xE5484D
COL_OK = 0x30A46C

# สนามแม่เหล็กโลกในตำราอยู่ในช่วงนี้ ใช้เป็นไม้บรรทัดเทียบบนจอ
EARTH_MIN = 25.0
EARTH_MAX = 65.0

lcd.clear()
lcd.console("<h2>เข็มทิศ BMM350</h2>")

# BMM350 ไม่ได้อยู่บนบัสเดียวกับเซนเซอร์ตัวอื่น มันอยู่บน I3C ขา P3[0]/P3[1]
# ซึ่งคอร์จอไม่ได้ถือไว้ นี่คือเหตุผลที่ bmm350.* เรียกได้ตรง ๆ ไม่ต้องผ่าน snapshot
# และเป็นเหตุผลที่มันไม่ถูกปฏิเสธเหมือน sensors.init()
try:
    lcd.print("chip_id =", hex(sensors.bmm350.chip_id()))
except OSError:
    lcd.print("อ่าน chip_id ไม่ได้ - ตรวจว่าบอร์ดมี BMM350 จริงไหม")

ui.screen()
# ผังจอเป็นสามแถบ ขอบนอก 24 ทุกด้าน - หัวเรื่องหนึ่งบรรทัด แล้วสามคอลัมน์ค่าอ่าน
# แล้วแถบล่างที่มีการ์ดข้อบกพร่องอยู่ซ้าย และแถวปุ่มอยู่ขวา
# แถวปุ่มจบที่ y=320 จึงพ้นมุมของปุ่ม Console (x>=690 และ y>=340) ทั้งสองปุ่ม
ui.Label("BMM350: ทิศถูก ขนาดยังไม่มีข้อสรุป", x=24, y=8, value=24,
         color=COL_TEXT)

ui.Label("สนามแม่เหล็ก", x=24, y=44, value=20, color=COL_DIM)
lbl_mx = ui.Label("mx  ----", x=24, y=72, value=20, color=COL_TEXT)
lbl_my = ui.Label("my  ----", x=24, y=100, value=20, color=COL_TEXT)
lbl_mz = ui.Label("mz  ----", x=24, y=128, value=20, color=COL_TEXT)
lbl_mag = ui.Label("|m| ----", x=24, y=156, value=20, color=COL_WARN)

ui.Label("ทิศ (องศา)", x=264, y=44, value=20, color=COL_DIM)
lbl_drv = ui.Label("heading()  ----", x=264, y=72, value=20, color=COL_TEXT)
lbl_dsp = ui.Label("compass()  ----", x=264, y=100, value=20, color=COL_TEXT)
lbl_gap = ui.Label("ต่างกัน ----", x=264, y=128, value=20, color=COL_TEXT)
ui.Label("คนละสูตร จึงคนละคำตอบ", x=264, y=156, value=20, color=COL_DIM)

ui.Label("การสอบเทียบ", x=504, y=44, value=20, color=COL_DIM)
lbl_valid = ui.Label("valid  ----", x=504, y=72, value=20, color=COL_TEXT)
lbl_offx = ui.Label("off_x  ----", x=504, y=100, value=20, color=COL_DIM)
lbl_offy = ui.Label("off_y  ----", x=504, y=128, value=20, color=COL_DIM)

# การ์ดข้อบกพร่อง: บอกสิ่งที่ยังค้าง ไม่ใช่ซ่อนมันไว้ใต้พรม
# สามบรรทัดในการ์ดชิดขอบซ้ายเดียวกันที่ x=40 คือขอบการ์ด 24 บวกระยะใน 16
ui.Panel(x=24, y=192, w=456, h=128)
ui.Label("สนามแม่เหล็กโลกจริงอยู่ที่ 25-65 uT", x=40, y=212, value=20,
         color=COL_WARN)
lbl_claim = ui.Label("บอร์ดรายงาน ---- นอกช่วงนั้นมาก", x=40, y=248,
                     value=20, color=COL_WARN)
ui.Label("ทิศถูก แต่ขนาดกับหน่วยยังไม่มีข้อสรุป", x=40, y=284, value=20,
         color=COL_BAD)

# ปุ่มสองใบเรียงข้างกัน กว้าง 116 สูง 88 เว้นกัน 32 ตาม §S7.13.11-12
ui.Label("cal_reset แล้วหมุน 360", x=504, y=192, value=20, color=COL_DIM)
btn_cal = ui.Button("ล้างค่า", x=504, y=232, w=116, h=88,
                    color=COL_ACCENT, value=20)
btn_exit = ui.Button("ออก", x=652, y=232, w=116, h=88, color=COL_DIM,
                     value=20)
id_cal = btn_cal.id()
id_exit = btn_exit.id()

running = True
while running:
    ok = True
    try:
        # magnetic() คืนสามแกน ป้ายหน่วยในซอร์สเขียนว่า micro-Tesla
        mx, my, mz = sensors.bmm350.magnetic()
        # heading() ของไดรเวอร์ทำงานเยอะกว่าที่คิด: หักค่า offset ของเหล็กติดบอร์ด
        # (เฉพาะตอนสอบเทียบผ่านแล้ว) แล้วเฉลี่ยแบบวงกลมย้อนหลังสิบค่า
        # จึงนิ่งกว่า dsp.compass() ที่คิดสด ๆ ทุกครั้งโดยไม่มีความจำ
        h_drv = sensors.bmm350.heading()
    except OSError:
        ok = False

    if ok:
        # dsp.compass() รับสามค่าแต่ใช้แค่สอง mz ถูกทิ้ง - จึงยังไม่ชดเชยการเอียง
        # ถ้าเอียงบอร์ดแล้วค่าเปลี่ยนทั้งที่ไม่ได้หมุน นั่นคืออาการของเรื่องนี้พอดี
        h_dsp = dsp.compass(mx, my, mz)

        mag = math.sqrt(mx * mx + my * my + mz * mz)

        lbl_mx.text("mx  {:+9.2f}".format(mx))
        lbl_my.text("my  {:+9.2f}".format(my))
        lbl_mz.text("mz  {:+9.2f}".format(mz))
        lbl_mag.text("|m| {:9.2f}".format(mag))

        lbl_drv.text("heading()  {:6.1f}".format(h_drv))
        lbl_dsp.text("compass()  {:6.1f}".format(h_dsp))

        # ต่างกันแบบวงกลม 350 กับ 10 ห่างกัน 20 ไม่ใช่ 340
        d = abs(h_drv - h_dsp)
        if d > 180.0:
            d = 360.0 - d
        lbl_gap.text("ต่างกัน {:.1f} องศา".format(d))

        if mag < EARTH_MIN:
            note = "ต่ำกว่าช่วงของโลก"
        elif mag > EARTH_MAX:
            note = "สูงกว่าช่วงของโลก"
        else:
            note = "อยู่ในช่วงของโลกพอดี"
        lbl_claim.text("รายงาน {:.0f} - {}".format(mag, note))

        # cal_status() คืน dict สามช่อง valid / offset_x / offset_y
        # ค่า valid เปลี่ยนเป็น True ได้เองโดยเราไม่ได้สั่ง เพราะงานเบื้องหลัง
        # ของบอร์ดป้อนตัวอย่างให้ตัวสะสมอยู่ตลอด ต้องเก็บครบ 50 ตัวอย่าง
        # และช่วงกว้างเกิน 15 หน่วยทั้งสองแกน จึงจะถือว่าใช้ได้
        cal = sensors.bmm350.cal_status()
        lbl_valid.text("valid  " + str(cal["valid"]))
        lbl_valid.color(COL_OK if cal["valid"] else COL_WARN)
        lbl_offx.text("off_x  {:+8.2f}".format(cal["offset_x"]))
        lbl_offy.text("off_y  {:+8.2f}".format(cal["offset_y"]))

    for ev in ui.poll():
        h = ev['handle']
        if h == id_cal:
            # cal_reset() ล้างค่า min/max ที่สะสมไว้ทั้งหมด แล้วต้องหมุนบอร์ดครบรอบ
            # ให้ทั้งสองแกนได้เห็นทั้งค่าสูงสุดและต่ำสุดของมัน จึงจะกลับมา valid
            sensors.bmm350.cal_reset()
            lcd.print("ล้างการสอบเทียบแล้ว - หมุนบอร์ดช้า ๆ ครบหนึ่งรอบ")
        elif h == id_exit:
            running = False

    time.sleep_ms(200)

ui.clear()
print("สิ่งที่ไฟล์นี้พิสูจน์ได้: ทิศเชื่อได้ และสองฟังก์ชันทิศคนละสูตรกันจริง")
print("สิ่งที่ไฟล์นี้พิสูจน์ไม่ได้: ตัวเลขขนาดกับหน่วย uT ตัวไหนคือตัวที่ผิด")
