# s06_digital_level.py - เครื่องวัดระดับดิจิทัลสองแกน (ฉบับฝึกเติมโค้ด)
# วิธีรัน: 1) บนจอบอร์ด แตะการ์ด Playground แล้ววางบอร์ดราบกับโต๊ะ
#          2) เติมช่องว่างทีละขั้น: จุด 1-3 ให้ค่าไหลมาก่อน จุด 4 กรอง จุด 5-6 ตั้งศูนย์กับหน้าปัด
#          3) กด Program to Device แล้วเอียงบอร์ดซ้าย-ขวา (roll) และหน้า-หลัง (pitch)
#
# dsp.tilt() คืนค่าเรียงเป็น (roll, pitch) - roll มาก่อน จำให้แม่น สลับเมื่อไรแถบจะวิ่งผิดแกน
# และตั้งศูนย์ต้องเก็บค่าที่กรองแล้วเสมอ จุดอ้างอิงที่สั่น แย่กว่าไม่มีจุดอ้างอิง
#
# หน้าจอเขียนไว้ให้ครบแล้ว ไม่ต้องแตะ - งานของเราคือทำให้ค่าไหลเข้าไปในนั้น
# ดูที่จอ: สองการ์ดซ้ายคือสองแกน แต่ละแกนมีแถบค่าวางบนไม้บรรทัด -90 ถึง 90 ของมันเอง
#         ขวาบนคือเกณฑ์ยอมรับที่ตั้งเองได้ ขวากลางคือไฟสองดวง ล่างขวาคือคุณภาพของค่า
#
# คาบนี้มีตัวอย่างสองไฟล์ ทั้งคู่กินค่าจาก IMU ตัวเดียวกับที่เราจะใช้
# examples/s06/01_imu_step_counter.py คือท่ามาตรฐาน อ่าน แล้วกรอง แล้วค่อยตัดสิน
# examples/s06/02_imu_fall_detection.py ไปไกลอีกขั้น คือตัดสินจาก "ลำดับของเหตุการณ์" ไม่ใช่ค่าเดียว

import ui
ui.screen()
import time
import sensors
import dsp

ui.clear()
time.sleep_ms(200)

# สีจัดสงวนไว้ให้สถานะผิดปกติ ค่าปกติเป็นตัวหนังสือขาวบนการ์ดสีเข้ม
COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD = 0x00E676, 0xFFC83D, 0xFF5252

# เกณฑ์ยอมรับหน่วยองศา ผู้ใช้ตั้งเองได้ ไม่ใช่ค่าคงที่ที่ฝังอยู่ในโค้ด
TOL_MIN, TOL_MAX, TOL_STEP = 1, 30, 1
tol = 5

# --- ท่าที่ 1: บอร์ดนี้ไม่ต้องปลุกเซนเซอร์ ---
# IMU ของ Eva Kit อยู่บนบัสที่คอร์จอ (CM55) ถือไว้คนเดียว ฝั่ง Python ขอค่า
# ที่คอร์จออ่านเก็บไว้ให้ทุก 200 ms จึงเรียก motion() ได้เลยโดยไม่ต้อง init
# ถ้าเผลอเรียก sensors.init() จะได้ OSError ทันที ไม่ใช่ค้าง
#
# หลังรีเซ็ต คอร์จอเริ่มตอบเรื่องเซนเซอร์ราว 13 วินาที การอ่านครั้งแรก
# จึงนิ่งได้ถึง 16 วินาที ครั้งถัดไปเร็วปกติ อุ่นเครื่องหนึ่งครั้งตรงนี้ก่อนสร้างหน้าปัด
try:
    # เติม: sensors.bmi270.motion()
    pass
except OSError:
    print("คอร์จอยังไม่ตอบรอบแรก จะลองใหม่ในลูป")

# --- ท่าที่ 2: สองแกน สองแถบ และไม้บรรทัดที่บอกพิสัยของมันเอง ---
ui.Label("เครื่องวัดระดับดิจิทัลสองแกน", x=16, y=6, color=COL_TEXT, value=24)

ui.Panel(x=12, y=44, w=468, h=100, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ROLL เอียงซ้าย-ขวา (องศา)", x=26, y=50, color=COL_DIM, value=16)
roll_bar = ui.Bar(x=30, y=76, w=296, h=14, color=COL_OK, min=-90, max=90, value=0)
roll_scale = ui.Scale(x=30, y=90, w=296, h=44, color=COL_TEXT, min=-90, max=90)
roll_scale.ticks(13, 3)         # 13 ขีด ใส่ตัวเลขทุกขีดที่สาม = -90 -45 0 45 90
roll_seg = ui.Seg7("+00.0", x=346, y=76, w=124, h=52, color=COL_TEXT)

ui.Panel(x=12, y=152, w=468, h=100, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("PITCH เอียงหน้า-หลัง (องศา)", x=26, y=158, color=COL_DIM, value=16)
pitch_bar = ui.Bar(x=30, y=184, w=296, h=14, color=COL_OK, min=-90, max=90, value=0)
pitch_scale = ui.Scale(x=30, y=198, w=296, h=44, color=COL_TEXT, min=-90, max=90)
pitch_scale.ticks(13, 3)
pitch_seg = ui.Seg7("+00.0", x=346, y=184, w=124, h=52, color=COL_TEXT)

# เกณฑ์ยอมรับ: ผู้ใช้ตั้งเองด้วยปุ่มสองปุ่ม ไม่ใช่ค่าคงที่ในโค้ด
# spinbox เปล่า ๆ บนจอสัมผัส นิ้วเปลี่ยนค่าไม่ได้ ตัวที่เพิ่มลดจริงคือปุ่มสองปุ่มข้าง ๆ
ui.Panel(x=492, y=44, w=288, h=100, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("เกณฑ์ยอมรับ (องศา)", x=506, y=50, color=COL_DIM, value=16)
sp_tol = ui.Spinbox(x=506, y=76, w=110, h=46, color=COL_TEXT,
                    min=TOL_MIN, max=TOL_MAX, value=tol)
sp_tol.digits(2, 0)             # ไม่บอกจะเห็น 0005 เพราะค่าตั้งต้นคือสี่หลัก
btn_up = ui.Button("เพิ่ม", x=624, y=76, w=68, h=46, color=0x37474F, value=16)
btn_dn = ui.Button("ลด", x=700, y=76, w=64, h=46, color=0x37474F, value=16)

# ไฟสองดวงแทนตัวหนังสือสี: ถ่ายจอเป็นขาวดำแล้วยังแยกออกว่าดวงไหนติด
ui.Panel(x=492, y=152, w=288, h=100, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("สถานะเทียบเกณฑ์", x=506, y=158, color=COL_DIM, value=14)
led_in = ui.Led(x=510, y=184, w=32, h=32, color=COL_OK, value=1)
ui.Label("อยู่ในเกณฑ์", x=548, y=190, color=COL_DIM, value=16)
led_out = ui.Led(x=656, y=184, w=32, h=32, color=COL_BAD, value=0)
ui.Label("เกิน", x=694, y=190, color=COL_DIM, value=16)

# ปุ่มสั่งงานสองปุ่ม แยกหน้าที่กันคนละปุ่ม ไม่มีปุ่มไหนสลับสองความหมายในตัวเดียว
ui.Panel(x=12, y=260, w=468, h=128, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("คำสั่ง", x=26, y=266, color=COL_DIM, value=14)
zero_btn = ui.Button("ตั้งศูนย์", x=26, y=290, w=200, h=52, color=0x1B5E20, value=18)
zero_id = zero_btn.id()
exit_btn = ui.Button("ออกจากโปรแกรม", x=246, y=290, w=210, h=52,
                     color=0x37474F, value=18)
exit_id = exit_btn.id()
ref = ui.Label("อ้างอิง R +0.0  P +0.0", x=26, y=352, color=COL_DIM, value=16)

# คุณภาพของค่า: ค่าที่ค้างอยู่ต้องเขียนให้ชัดว่ามันค้าง ไม่ใช่ปล่อยเลขเดิมไว้เฉย ๆ
ui.Panel(x=492, y=260, w=288, h=128, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("คุณภาพของค่า", x=506, y=266, color=COL_DIM, value=14)
lbl_health = ui.Label("กำลังวัดอยู่", x=506, y=290, color=COL_DIM, value=20)
health = "กำลังวัดอยู่"     # ข้อความล่าสุดของบรรทัดคุณภาพ คิดใหม่วินาทีละครั้ง
led_stale = ui.Led(x=510, y=326, w=28, h=28, color=COL_WARN, value=0)
ui.Label("ค่าค้าง", x=546, y=330, color=COL_DIM, value=16)

# ตัวกรองมีความจำ สองสัญญาณจึงต้องใช้สองตัวเสมอ ใช้ตัวเดียวร่วมกันไม่ได้
ema_roll = dsp.EMA(alpha=0.2)
ema_pitch = dsp.EMA(alpha=0.2)
roll_zero = 0.0
pitch_zero = 0.0

# ค่าเริ่มต้นให้ไฟล์รันได้ก่อนเติมครบ
roll, pitch = 0.0, 0.0
roll_f, pitch_f = 0.0, 0.0
last_sec = -1        # วินาทีที่เพิ่งเขียนตัวเลขลงจอ กันไม่ให้เขียนถี่กว่าวินาทีละครั้ง


def clamp90(v):
    return 90.0 if v > 90.0 else (-90.0 if v < -90.0 else v)


running = True
while running:
    ok_read = True
    try:
        # --- ท่าที่ 3: อ่านหกแกนจาก snapshot ชุดเดียว แล้วแปลงเป็นองศา ---
        # motion() ถามข้ามคอร์รอบเดียวได้ครบหกแกน ต่างจากเรียกทีละฟังก์ชันซึ่งถามหลายรอบ
        # เติม: ax, ay, az, gx, gy, gz = sensors.bmi270.motion()
        # อยากเห็นค่าดิบจาก IMU ก่อนว่าหน้าตาเป็นอย่างไร: examples/s06/01_imu_step_counter.py
        #   วาดขนาดความเร่งดิบไว้เทียบกับค่าที่กรองแล้วบนกราฟเดียวกัน มองสองเส้นนั้นสักพัก
        #   แล้วจะเห็นเองว่าทำไมเราถึงไม่เอาค่าดิบไปขึ้นแถบตรง ๆ
        pass

        # เติม: roll, pitch = dsp.tilt(ax, ay, az)
        pass

    except OSError:
        ok_read = False

    if ok_read:
        # --- ท่าที่ 4: กรองความสั่น แล้วหักค่าอ้างอิงที่ตั้งศูนย์ไว้ ---
        # เติม: roll_f = ema_roll.update(roll)
        # ลำดับ "กรองก่อน แล้วค่อยตัดสิน" ไม่ใช่เรื่องของคาบนี้คาบเดียว
        #   examples/s06/01_imu_step_counter.py ใช้ลำดับเดียวกันเป๊ะ และบอกราคาของการข้ามตัวกรอง
        #   ไว้ด้วย คือค่าดิบมีหนามแหลมที่ทำให้นับเกินได้ง่ายมาก (ส่วนอาการก้าวเดียวถูกนับ
        #   หลายก้าว เป็นราคาของการไม่มีเวลาห้ามนับซ้ำ ซึ่งเป็นคนละกันดักในไฟล์เดียวกัน)
        pass

        pitch_f = ema_pitch.update(pitch)

    # ลำดับสำคัญ: กรองก่อน แล้วค่อยหักค่าศูนย์
    roll_show = roll_f - roll_zero
    pitch_show = pitch_f - pitch_zero

    # --- ท่าที่ 5: รับเหตุการณ์จากจอ ---
    for ev in ui.poll():
        h = ev['handle']
        if h == zero_id:
            # เติม: roll_zero = roll_f  แล้วบรรทัดถัดไป pitch_zero = pitch_f
            # เก็บ roll_f ไม่ใช่ roll เพราะจุดอ้างอิงต้องมาจากค่าที่นิ่งแล้ว
            #   examples/s06/02_imu_fall_detection.py เป็นตัวอย่างของหลักเดียวกันในงานที่แพงกว่า
            #   ที่นั่นเครื่องต้องเห็นครบสามขั้นก่อนจึงจะเชื่อ เพราะระบบที่เตือนผิดบ่อย ผู้ใช้จะถอดทิ้ง
            pass

            ref.text("อ้างอิง R {:+.1f}  P {:+.1f}".format(roll_zero, pitch_zero))
        elif h == exit_id:
            running = False
        elif h == btn_up.id():
            # ปุ่มเพิ่มกับปุ่มลดแยกกันคนละปุ่ม ปุ่มเดียวที่สลับสองทิศ คนกดจะเดาไม่ออก
            tol = min(TOL_MAX, tol + TOL_STEP)
            sp_tol.value(tol)
        elif h == btn_dn.id():
            tol = max(TOL_MIN, tol - TOL_STEP)
            sp_tol.value(tol)

    # --- ท่าที่ 6: ส่งค่าขึ้นหน้าปัด แล้วเว้นจังหวะ ---
    # เติม: roll_bar.value(int(clamp90(roll_show)))
    pass

    pitch_bar.value(int(clamp90(pitch_show)))

    # หนึ่งดวงติดเท่านั้น แผงที่ติดพร้อมกันหลายดวงคือแผงที่อ่านไม่ออก
    in_tol = abs(roll_show) <= tol and abs(pitch_show) <= tol
    led_in.value(1 if in_tol else 0)
    led_out.value(0 if in_tol else 1)
    led_stale.value(0 if ok_read else 1)

    # ตัวเลขที่คนต้องอ่าน เขียนใหม่ไม่เกินวินาทีละครั้ง และอยู่ตำแหน่งเดิมเสมอ
    sec = time.ticks_ms() // 1000
    if sec != last_sec:
        last_sec = sec
        roll_seg.text("{:+.1f}".format(roll_show))
        pitch_seg.text("{:+.1f}".format(pitch_show))
        # ตั้งสีก่อนแล้วค่อยเขียนข้อความ สองบรรทัดนี้เป็นคำสั่งคนละครั้งข้ามคอร์
        if not ok_read:
            lbl_health.color(COL_WARN)
            health = "ค่าค้าง ตัวเลขคือค่าล่าสุด"
        elif in_tol:
            lbl_health.color(COL_DIM)
            health = "อยู่ในเกณฑ์ {} องศา".format(tol)
        else:
            lbl_health.color(COL_BAD)
            health = "เอียงเกินเกณฑ์ {} องศา".format(tol)

    # --- ชีพจรของช่องทางข้ามคอร์: ส่งบรรทัดสถานะซ้ำทุกรอบโดยตั้งใจ ---
    # ฝั่งจอมีตัวจับเวลาสองจังหวะ โหมดเร็ว 5 ms กับโหมดปกติ 200 ms และมันจะอยู่
    # โหมดเร็วต่อไปอีก 500 ms ทุกครั้งที่ได้รับคำสั่ง "เขียนข้อความ" หรือ "ย้าย/ย่อ/เปลี่ยนสี"
    # แต่ .value() ของแถบกับไฟ "ไม่" ปลุกโหมดนั้น ลูปที่อัปเดตเฉพาะแถบกับไฟจึงเงียบเกิน
    # ครึ่งวินาที แล้วจอจะถอยไปโหมดช้า ภาพกระตุกทั้งที่โค้ดไม่ได้เปลี่ยนสักบรรทัด
    # ข้อความที่ส่งไปนี้เท่าเดิมเกือบทุกรอบ และเฟิร์มแวร์ไม่วาดซ้ำถ้าข้อความไม่เปลี่ยน
    # เราจึงจ่ายแค่ค่าส่งข้ามคอร์ ไม่ได้จ่ายค่าวาดใหม่
    lbl_health.text(health)

    time.sleep_ms(200)

ui.clear()
print("ปิดเครื่องวัดระดับแล้ว")
