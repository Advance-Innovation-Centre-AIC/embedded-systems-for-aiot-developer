# 04_compass_and_magnetometer.py - เข็มทิศบนบอร์ด และตัวเลขหนึ่งตัวที่ยังไม่มีใครตอบได้
# ชุดตัวอย่างประจำคาบ 06
#
# ไฟล์นี้สอน: sensors.bmm350 มีห้าคำสั่ง magnetic() heading() chip_id()
#             cal_reset() cal_status() - ทั้งห้าใช้ได้บน Eva Kit
#             และ dsp.compass() คือฟังก์ชันคนละตัวกับ bmm350.heading()
# ดูที่จอ   : ซ้ายคือสามแกนสนามแม่เหล็กและขนาดรวม กลางคือ heading() ของไดรเวอร์
#             เทียบกับ dsp.compass() ซึ่ง "ไม่เท่ากัน" และนั่นถูกต้องแล้ว
#             ขวาคือสถานะการสอบเทียบ กับกล่องเหลืองที่บอกข้อบกพร่องที่ยังค้างอยู่
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

COL_DRV = 0x00BFFF     # ฟ้า = heading() ของไดรเวอร์
COL_DSP = 0xB388FF     # ม่วง = dsp.compass()
COL_GRAY = 0xA0B4CC

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
ui.Label("BMM350: ทิศถูก ขนาดยังไม่มีข้อสรุป", x=12, y=8, value=20)

ui.Label("สนามแม่เหล็ก", x=12, y=42, value=18, color=COL_GRAY)
lbl_mx = ui.Label("mx  ----", x=12, y=68, value=20)
lbl_my = ui.Label("my  ----", x=12, y=94, value=20)
lbl_mz = ui.Label("mz  ----", x=12, y=120, value=20)
lbl_mag = ui.Label("|m| ----", x=12, y=146, value=20, color=0xFFC107)

ui.Label("ทิศ (องศา)", x=250, y=42, value=18, color=COL_GRAY)
lbl_drv = ui.Label("heading()  ----", x=250, y=68, value=20, color=COL_DRV)
lbl_dsp = ui.Label("compass()  ----", x=250, y=94, value=20, color=COL_DSP)
lbl_gap = ui.Label("ต่างกัน ----", x=250, y=120, value=18, color=COL_GRAY)
ui.Label("ต่างกันเพราะคนละสูตร", x=250, y=146, value=16, color=COL_GRAY)

ui.Label("การสอบเทียบ", x=500, y=42, value=18, color=COL_GRAY)
lbl_valid = ui.Label("valid  ----", x=500, y=68, value=20)
lbl_offx = ui.Label("off_x  ----", x=500, y=94, value=18, color=COL_GRAY)
lbl_offy = ui.Label("off_y  ----", x=500, y=120, value=18, color=COL_GRAY)

# กล่องเหลือง: บอกข้อบกพร่องที่ยังค้าง ไม่ใช่ซ่อนมันไว้ใต้พรม
ui.Panel(x=12, y=180, w=670, h=76)
ui.Label("สนามแม่เหล็กโลกจริงอยู่ที่ 25-65 uT", x=24, y=186, value=18,
         color=0xFFC107)
lbl_claim = ui.Label("บอร์ดรายงาน ---- นอกช่วงนั้นมาก", x=24, y=208,
                     value=18, color=0xFFC107)
ui.Label("ทิศถูก แต่ขนาดกับหน่วยยังไม่มีข้อสรุป", x=24, y=230, value=18,
         color=0xFF5252)

ch = ui.Chart(x=12, y=280, w=470, h=104, min=0, max=360, color=COL_DRV)
s_drv = 0
s_dsp = ch.add_series(COL_DSP)

btn_cal = ui.Button("cal_reset แล้วหมุน 360", x=496, y=282, w=186, h=48,
                    color=0x6A1B9A, value=18)
btn_exit = ui.Button("ออก", x=496, y=336, w=186, h=48, color=0x546E7A,
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
        lbl_claim.text("บอร์ดรายงาน {:.0f} - {}".format(mag, note))

        # cal_status() คืน dict สามช่อง valid / offset_x / offset_y
        # ค่า valid เปลี่ยนเป็น True ได้เองโดยเราไม่ได้สั่ง เพราะงานเบื้องหลัง
        # ของบอร์ดป้อนตัวอย่างให้ตัวสะสมอยู่ตลอด ต้องเก็บครบ 50 ตัวอย่าง
        # และช่วงกว้างเกิน 15 หน่วยทั้งสองแกน จึงจะถือว่าใช้ได้
        cal = sensors.bmm350.cal_status()
        lbl_valid.text("valid  " + str(cal["valid"]))
        lbl_valid.color(0x50D890 if cal["valid"] else 0xFF9800)
        lbl_offx.text("off_x  {:+8.2f}".format(cal["offset_x"]))
        lbl_offy.text("off_y  {:+8.2f}".format(cal["offset_y"]))

        ch.set_next(s_drv, int(h_drv))
        ch.set_next(s_dsp, int(h_dsp))

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
