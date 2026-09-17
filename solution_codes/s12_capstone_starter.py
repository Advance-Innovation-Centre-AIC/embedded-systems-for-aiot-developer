# s12_capstone_starter.py - ตัวอย่างที่ทำเสร็จแล้วหนึ่งชิ้น: Tilt Alarm สำหรับนั่งร้าน/ชั้นวาง
# วิธีรัน: 1) บนจอบอร์ด แตะการ์ด Playground บนหน้า Home แล้วค้างหน้านี้ไว้
#          2) แก้บล็อก CONFIG (device id, WiFi, broker, topic, เกณฑ์) ให้เป็นของทีม
#          3) วางบอร์ดให้นิ่งตอนเริ่มรัน โปรแกรมจะจำท่าตั้งต้นเองในสองวินาทีแรก
#
# นี่คือ "คำตอบหนึ่งที่เป็นไปได้" ไม่ใช่คำตอบเดียวของคาบนี้
# ทีมที่เลือกโจทย์อื่น (มอเตอร์สั่น ประตูถูกเปิด เครื่องจักรเดินเบา) จะได้โครงเดียวกัน
# แต่เปลี่ยนสามอย่าง: ค่าที่วัด เกณฑ์ที่ตัดสิน และสิ่งที่คนหน้างานเห็นบนจอ
#
# สิ่งที่ทำให้ไฟล์นี้เป็น product ไม่ใช่ demo มีห้าอย่าง
#   1) หักค่าตั้งต้น (baseline) - ไม่มีใครติดตั้งอุปกรณ์ได้ระนาบเป๊ะ 0 องศา
#   2) ยืนยันหลายรอบก่อนเปลี่ยนสถานะ - กันสั่นวูบเดียวแล้วโทรตามช่างกลางดึก
#   3) เตือนแล้วค้างไว้จนคนกดรับทราบ - เหตุการณ์ต้องมีคนเห็น ไม่ใช่แวบแล้วหาย
#   4) เน็ตหลุดแล้วจอยังทำงาน และนับไว้ว่าพลาดไปกี่ครั้ง
#   5) จอสั่งของจริงได้ และคำสั่งที่ทำให้ของจริงขยับต้องผ่านกล่องยืนยันก่อน

import time
import json
import gpio
import wifi
import mqtt
import sensors
import dsp
import ui

# ---------- CONFIG ----------
DEVICE_ID = "team01"
WIFI_SSID = "AIoT-Class"
WIFI_PASS = "aiot12345"
BROKER = "test.mosquitto.org"
TOPIC = "bento/team01/telemetry"
UNIT = "deg"
SCALE_MAX = 45                    # ปลายพิสัยของมาตรวัดบนจอ ต้องเป็นค่าที่เป็นไปได้จริง
WARN_LIMIT = 8.0                  # เอียงจากท่าตั้งต้นเกินเท่านี้ = เฝ้าดู
ALERT_LIMIT = 15.0                # เกินเท่านี้ = ผิดปกติ ต้องมีคนไปดู
# CONFIRM_N มาจาก examples/s12/02_confirm_n.py ซึ่งวางป้ายสองใบไว้ข้างกัน
# "เชื่อทันที" กับ "ยืนยัน 3 รอบ" แล้วยิงค่ากระโดดวูบเดียวเข้าไป ป้ายซ้ายแดง ป้ายขวายังเขียว
# ภาพนั้นคือเหตุผลทั้งหมดของบรรทัดนี้ และไฟล์นั้นบังคับให้ตอบราคาด้วยว่าเตือนช้าลงกี่วินาที
# ไม่ใช่ตั้งเลขให้ใหญ่ไว้ก่อน - ที่นี่ 3 x 200 ms = 0.6 วินาที ซึ่งรับได้สำหรับงานนั่งร้าน
CONFIRM_N = 3                     # ต้องเกินเกณฑ์ติดกันกี่รอบจึงเชื่อ (3 x 200ms = 0.6 วิ)
BEACON_LED = "RGB_RED"            # ไฟเตือนหน้างาน ใช้หลอดบนบอร์ดแทนไฟหมุนจริง - ระบุเป็น "ชื่อ"
                                  # ดวงแดงบน Dev Kit (บน Eva จะถูกแทนด้วย LED1 - ดู beacon_lamp ข้างล่าง)
HEARTBEAT_MS = 30000
ALERT_GAP_MS = 15000
RETRY_MS = 10000
LOOP_MS = 200

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD, COL_OK, COL_WARN, COL_BAD, COL_RUN = (0x171B22, 0x30A46C, 0xF5A623,
                                                0xE5484D, 0x4A9EFF)

# สถานะที่ส่งขึ้น broker เป็นอังกฤษเพื่อให้ฝั่งรับเขียนเงื่อนไขง่าย ส่วนที่ขึ้นจอเป็นไทย
# และบอก "แล้วต้องทำอะไร" ไม่ใช่บอกแค่ชื่อสถานะ คนหน้างานต้องการประโยคหลัง
STATE_TEXT = {"OK": "ปกติ ไม่ต้องทำอะไร",
              "WARN": "เฝ้าดู ยังไม่ต้องเข้าไป",
              "ALERT": "ผิดปกติ ต้องมีคนไปดู"}

# --- ไฟเตือนหน้างาน: หาตามชื่อ ไม่ใช่ตามเลข ---
# เลขดัชนีของ gpio.led() ต่างกันตามบอร์ดและอาจเปลี่ยนอีก ชื่อในตาราง led_names ไม่เปลี่ยน
#   Eva Kit : LED1=แดง  LED2=เขียว  RGB_RED=ฟ้า   <- ชื่อ RGB_RED บน Eva คือดวงสีฟ้า
#   Dev Kit : LED1 LED2 อยู่บน SoM มองไม่เห็นบนบอร์ดประกอบ  RGB_RED RGB_BLUE RGB_GREEN
# ดวงแดงจึงเป็นข้อยกเว้น: ทั้งสองบอร์ดมีทั้งสองชื่อ ต้องดูก่อนว่ามี RGB ครบสามสีไหม
LED_NAMES = gpio.board_info()["led_names"]


def led_named(*names, fallback=0):
    """หา LED จากชื่อในตารางเฟิร์มแวร์ - เลขดัชนีต่างกันตามบอร์ด ชื่อไม่ต่าง"""
    for n in names:
        if n in LED_NAMES:
            return gpio.led(LED_NAMES.index(n))
    return gpio.led(fallback)


beacon_lamp = led_named(BEACON_LED if "RGB_GREEN" in LED_NAMES else "LED1")   # แดงทั้งสองบอร์ด

# --- ท่าที่ 1: Sense ---
# บน Eva ไม่มี sensors.init() ให้เรียก IMU อยู่บนบัสที่คอร์จอ (CM55) ถือคนเดียว
# ฝั่ง Python ขอค่าที่คอร์จออ่านเก็บไว้ให้ทุก 200 ms เรียก init() จะได้ OSError ทันที
# บน Dev Kit CM33 อ่าน IMU ตรงจาก I2C เอง และเฟิร์มแวร์ปลุกมันไว้ตั้งแต่บูต จึงไม่ต้อง init เช่นกัน
# บน Eva หลังรีเซ็ต คอร์จอเริ่มตอบเรื่องเซนเซอร์ราว 13 วินาที การอ่านครั้งแรกจึงนิ่ง
# ได้ถึง 16 วินาที ยิงหนึ่งครั้งตรงนี้ ให้การรอไปเกิดก่อนวัดท่าตั้งต้น
try:
    sensors.bmi270.motion()
except OSError:
    print("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ตอนวัดท่าตั้งต้น")
smooth = dsp.EMA(alpha=0.2)       # ใช้ EMA ตัวเดียวพอ ค่าที่ตัดสินควรมีที่มาเดียว

last_tilt = (0.0, 0.0)            # ค่าล่าสุดที่อ่านได้ ใช้ต่อเมื่อรอบไหนอ่านพลาด
stale = False                     # รอบนี้อ่านไม่ได้ใช่ไหม - จอต้องบอกความจริงข้อนี้


def raw_tilt():
    # อุปกรณ์ที่ต้องอยู่เป็นเดือนต้องทนการอ่านพลาดหนึ่งรอบได้ ไม่ใช่ตายทั้งเครื่อง
    global last_tilt, stale
    try:
        ax, ay, az, gx, gy, gz = sensors.bmi270.motion()
    except OSError:
        # ค่าค้างยังมีประโยชน์ แต่ต้องไม่ถูกนำเสนอเหมือนค่าที่เพิ่งวัดมา
        stale = True
        return last_tilt
    stale = False
    roll, pitch = dsp.tilt(ax, ay, az)      # ลำดับคือ (roll, pitch) เสมอ
    last_tilt = (roll, pitch)
    return roll, pitch


# วัดท่าตั้งต้นตอนเริ่มรัน แล้ววัดทุกอย่างเทียบกับท่านี้
# ถ้าไม่ทำ อุปกรณ์ที่ติดเอียง 6 องศาตั้งแต่วันติดตั้งจะเตือนตลอดเวลาโดยไม่มีอะไรผิด
base_roll = 0.0
base_pitch = 0.0
for _ in range(10):
    r, p = raw_tilt()
    base_roll += r / 10.0
    base_pitch += p / 10.0
    time.sleep_ms(100)


def read_value():
    roll, pitch = raw_tilt()
    dr = roll - base_roll
    dp = pitch - base_pitch
    return smooth.update((dr * dr + dp * dp) ** 0.5)   # เอียงไปทางไหนก็นับเป็นการเอียง


# --- ท่าที่ 2: Decide ---
# ลำดับของ if ในฟังก์ชันนี้ต้องไล่จากเข้มที่สุดลงมา สลับเมื่อไรจะไม่มีทางเข้า ALERT เลยสักครั้ง
# และมันจะไม่ error ให้เห็น - examples/s12/01_state_machine.py ชี้กับดักข้อนี้ไว้ตรง ๆ
# พร้อมกับหลักที่ใหญ่กว่านั้น คือแยก "ค่าที่วัดได้" ออกจาก "สถานะที่ตัดสินแล้ว"
# ด้วยฟังก์ชันตัดสินตัวเดียว เกณฑ์จึงอยู่ที่เดียวและทดสอบได้โดยไม่ต้องเขย่าบอร์ด
def level_of(value):
    if value > ALERT_LIMIT:
        return "ALERT"
    if value > WARN_LIMIT:
        return "WARN"
    return "OK"


# --- ท่าที่ 3: Show ---
# วางพิกัดเองทุกตัว ไม่ปล่อยให้ auto-layout จัดให้ เพราะ auto-layout วางทีละ
# ช่องกว้าง 200 px ตายตัว พอข้อความยาวกว่านั้นมันจะล้นไปทับตัวถัดไปทันที
# พื้นที่ใช้งานจริงของจอ 4.3 นิ้วคือกว้าง 792 สูง 398 นับจากใต้แถบหัวเรื่อง
# และมุมขวาล่างเฟิร์มแวร์ถือไว้ให้ปุ่ม Console ห้ามวางของที่ต้องกดไว้ตรงนั้น
ui.screen()
time.sleep_ms(200)
ui.Label("นั่งร้านเอียง - " + DEVICE_ID, x=24, y=8, color=COL_TEXT, value=20)

# การ์ดซ้ายบน: ค่าที่วัดได้ พร้อมพิสัยของมัน - ตัวเลขลอย ๆ ไม่บอกว่าสูงไหม
# การ์ดต้องถูกสร้างก่อนของที่วางบนมันเสมอ LVGL วาดตามลำดับการสร้าง การ์ดที่มาทีหลัง
# จะทาทับของที่สร้างไว้ก่อนจนหายไปทั้งใบ โดยไม่มี error สักบรรทัด
#
# ลำดับขนาดตัวอักษรบนหน้านี้มีสามชั้น ค่าหลักที่ต้องอ่านปราดเดียวใช้ 28
# ประโยคที่คนหน้างานต้องอ่านใช้ 20 ส่วนป้ายหัวการ์ด หน่วย และเชิงอรรถใช้ 16
# ถ้าทุกอย่างขนาดเดียวกันหมด จะไม่มีอะไรเด่น ซึ่งอ่านยากพอกับตัวอักษรที่เล็กเกินไป
ui.Panel(x=24, y=48, w=464, h=184, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ค่าที่วัดได้ เทียบกับเกณฑ์", x=40, y=56, color=COL_DIM, value=16)
lbl_value = ui.Label("--", x=40, y=88, color=COL_TEXT, value=28)
lbl_quality = ui.Label("รอค่าแรก", x=248, y=96, color=COL_DIM, value=16)
# แถบค่าวางไว้ "เหนือ" ไม้บรรทัด เพราะ ui.Scale ไม่มีเข็มและไม่รับ .value()
# มันคือไม้บรรทัด ตัวที่ขยับคือ ui.Bar ที่เราวางไว้ชิดขอบบนของมัน
bar_value = ui.Bar(x=40, y=136, w=432, h=16, color=COL_RUN,
                   min=0, max=SCALE_MAX, value=0)
sc_value = ui.Scale(x=40, y=156, w=432, h=48, color=COL_TEXT, min=0, max=SCALE_MAX)
sc_value.ticks(10, 3)
# ป้ายเกณฑ์ประกอบจากค่าคงที่ข้างบน ไม่ใช่พิมพ์เลขซ้ำ - ทีมที่แก้ CONFIG แล้วลืมแก้ป้าย
# จะได้จอที่บอกเกณฑ์ผิด ซึ่งอันตรายกว่าจอที่ไม่บอกเกณฑ์เลย
ui.Label("เฝ้าระวัง " + str(WARN_LIMIT) + " - ผิดปกติ " + str(ALERT_LIMIT) + " " + UNIT,
         x=40, y=208, color=COL_DIM, value=16)

# การ์ดซ้ายล่าง: สถานะที่ตัดสินแล้ว เป็นไฟสามดวง ติดทีละดวงเสมอ
# แผงที่ติดพร้อมกันหลายดวงคือแผงที่อ่านไม่ออก
ui.Panel(x=24, y=240, w=464, h=152, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("สถานะที่ตัดสินแล้ว", x=40, y=248, color=COL_DIM, value=16)
# ป้าย "รอคนรับทราบ" แยกเป็นป้ายของตัวเอง ไม่ต่อท้ายป้ายสถานะ เพราะข้อความไทย
# ที่ยาวขึ้นอีกสิบตัวอักษรจะวิ่งไปใต้ปุ่มข้าง ๆ แล้วอ่านไม่ออก ทั้งที่ตอนออกแบบยังพอดี
# และมันอยู่เหนือปุ่มที่ใช้ปลดล็อกพอดี คนที่เห็นป้ายจึงเห็นทางแก้ในสายตาเดียวกัน
lbl_latch = ui.Label("รอคนรับทราบ", x=336, y=244, color=COL_WARN, value=20)
lbl_latch.hide()
led_ok = ui.Led(x=40, y=276, w=48, h=48, color=COL_OK, value=1)
led_warn = ui.Led(x=144, y=276, w=48, h=48, color=COL_WARN, value=0)
led_bad = ui.Led(x=248, y=276, w=48, h=48, color=COL_BAD, value=0)
# ป้ายอยู่ใต้ไฟของตัวเอง ไม่ใช่ข้าง ๆ - สามดวงเรียงกันแบบมีคำต่อท้ายทีละดวง
# กินความกว้างเกินการ์ด แล้วดวงสุดท้ายจะหลุดออกไปนอกใบ
ui.Label("ปกติ", x=40, y=328, color=COL_DIM, value=16)
ui.Label("เฝ้าระวัง", x=144, y=328, color=COL_DIM, value=16)
ui.Label("ผิดปกติ", x=248, y=328, color=COL_DIM, value=16)
lbl_state = ui.Label("กำลังวัดท่าตั้งต้น", x=40, y=356, color=COL_TEXT, value=20)
btn_ack = ui.Button("รับทราบ", x=368, y=276, w=104, h=88, color=0x3A4150, value=20)

# การ์ดขวา: ของจริงที่เราสั่งได้ กับสถานะการส่งข้อมูล
ui.Panel(x=504, y=48, w=264, h=344, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ไฟเตือนหน้างาน", x=520, y=56, color=COL_DIM, value=16)
# ui.Led สั่ง .value(0) แล้ว "หรี่" ไม่ใช่ "หาย" - ไฟแผงควบคุมที่หายไปตอนดับ
# แย่กว่าไฟที่หรี่ลง เพราะคนดูแยกไม่ออกว่าดับหรือจอเสีย
led_beacon = ui.Led(x=520, y=88, w=48, h=48, color=COL_BAD, value=0)
# ป้ายข้างไฟบอกสถานะเป็นคำด้วย ไม่ใช่ปล่อยให้สีกับความสว่างเล่าคนเดียว
# ภาพขาวดำหรือคนตาบอดสีต้องอ่านหน้าจอนี้ออกเท่ากัน
lbl_beacon_state = ui.Label("ไฟดับอยู่", x=576, y=100, color=COL_DIM, value=20)
lbl_beacon = ui.Label("สั่งปิดต้องยืนยันก่อน", x=520, y=144, color=COL_DIM, value=16)
# ปุ่มเปิดกับปุ่มปิดแยกกันคนละปุ่ม ห้ามใช้ปุ่มเดียวสลับไปมา เพราะปุ่มสลับบอกไม่ได้ว่า
# ตอนนี้อยู่สถานะไหน คนกดจึงต้องเดา และเดาผิดได้เสมอ
# ทั้งคู่จบที่ y=264 เพราะมุมขวาล่างตั้งแต่ x=690 y=340 เป็นของปุ่ม Console
btn_on = ui.Button("เปิดไฟ", x=520, y=176, w=96, h=88, color=0x30A46C, value=20)
btn_off = ui.Button("ปิดไฟ", x=648, y=176, w=96, h=88, color=0x3A4150, value=20)
lbl_net = ui.Label("net: starting", x=520, y=296, color=COL_DIM, value=20)
lbl_sent = ui.Label("ส่งแล้ว 0 ใบ", x=520, y=336, color=COL_DIM, value=16)

# กล่องยืนยันสร้างพร้อมหน้าจอแล้วซ่อนไว้ ไม่ใช่สร้างตอนกด - แฮนเดิลมีจำกัด และการ
# สร้างของตอนคนกำลังรอคำตอบ คือการเพิ่มความหน่วงในจังหวะที่แย่ที่สุด
# ข้อความของ MsgBox เดินทางไปกับ CREATE ซึ่งพาได้ 95 ไบต์ ภาษาไทยตัวละ 3 ไบต์
# แปลว่าหัวเรื่องบวกเนื้อความรวมกันได้ราว 31 ตัวอักษร ยาวกว่านั้นถูกตัดเงียบ ๆ
# และคำยืนยันต้องบอก "สิ่งที่จะเกิด" ไม่ใช่ถามว่า "ยืนยันไหม" ซึ่งไม่ได้ให้ข้อมูลอะไรเลย
# กล่องวางกลางจอ ไม่ใช่มุมใดมุมหนึ่ง เพราะตอนมันโผล่ มันคือสิ่งเดียวที่ต้องอ่าน
box = ui.MsgBox("ปิดไฟเตือน\nไฟหน้างานจะดับทันที", x=112, y=96, w=568, h=136,
                color=COL_CARD)
box.hide()
# ปุ่มสองปุ่มนี้คือคำตอบของกล่อง - ปุ่มในตัว MsgBox เองยังไม่ส่งเหตุการณ์กลับมาให้
# Python เห็น (เฟิร์มแวร์ผูก callback ไว้กับ ui.Button เท่านั้น) ถ้าวางปุ่มตายไว้บนจอ
# คนกดจะสรุปว่าเครื่องแฮงก์ จึงใช้ ui.Button จริงสองปุ่มแทน
btn_yes = ui.Button("ยืนยัน", x=144, y=248, w=200, h=88, color=0x3A4150, value=20)
btn_no = ui.Button("ยกเลิก", x=376, y=248, w=200, h=88, color=0x3A4150, value=20)
btn_yes.hide()
btn_no.hide()
ui.poll()

last_sec = -1                     # วินาทีที่เพิ่งเขียนตัวเลขลงจอ
last_shown = None                 # สถานะที่ป้ายกำลังบอกอยู่ตอนนี้


def show(now, value, state, latched, net_text, sent):
    global last_sec, last_shown
    # ไม่เรียก bar_value.color() ตอนรัน เพราะ .color() ของ Bar ไปลงที่ "ราง"
    # ไม่ใช่ "แถบที่เต็ม" - รางที่เปลี่ยนสีทำให้ดูเหมือนแถบเต็มทั้งที่ค่ายังน้อย
    # แถบกับไฟขยับได้ทุกรอบ ตาอ่านรูปทรงได้เร็วกว่าอ่านตัวเลข
    bar_value.value(int(min(value, SCALE_MAX)))
    led_ok.value(1 if state == "OK" else 0)
    led_warn.value(1 if state == "WARN" else 0)
    led_bad.value(1 if state == "ALERT" else 0)

    # ป้ายสถานะเขียนตอน "เปลี่ยน" ไม่ใช่ตอนถึงรอบวินาที ถ้าปล่อยให้มันรอรอบ
    # ป้ายกับไฟจะไม่ตรงกันได้นานถึงหนึ่งวินาที ซึ่งคนดูจะอ่านว่าจอเพี้ยน
    if (state, latched) != last_shown:
        last_shown = (state, latched)
        lbl_state.text(STATE_TEXT[state])
        if latched:
            lbl_latch.show()
        else:
            lbl_latch.hide()

    # ตัวเลขที่คนต้องอ่าน เขียนใหม่ไม่เกินวินาทีละครั้ง และอยู่ตำแหน่งคงที่เสมอ
    # ตัวเลขที่กระพริบเร็วกว่านั้น คนอ่านไม่ทัน และไม่มีใครได้ประโยชน์จากมัน
    sec = now // 1000
    if sec == last_sec:
        return
    last_sec = sec
    lbl_value.text("{:.1f} {}".format(value, UNIT))
    # ค่าที่แสดงต้องบอกคุณภาพของตัวเองได้ ค่าค้างต้องระบุว่าไม่ใช่ค่าปัจจุบัน
    lbl_quality.text("ค่าค้าง อ่านไม่ได้" if stale else "ค่าปกติ")
    lbl_quality.color(COL_WARN if stale else COL_DIM)
    lbl_net.text(net_text)
    lbl_sent.text("ส่งแล้ว " + str(sent) + " ใบ")


def beacon(on):
    # ของจริงกับจอต้องขยับพร้อมกันเสมอ จอที่บอกว่าไฟติดทั้งที่หลอดดับ คือจอที่โกหก
    if on:
        beacon_lamp.on()
    else:
        beacon_lamp.off()
    led_beacon.value(1 if on else 0)
    lbl_beacon_state.text("ไฟติดอยู่" if on else "ไฟดับอยู่")


# --- ท่าที่ 4: Send ---
def payload(value, state, kind):
    # เล็ก คงที่ มีหน่วย และมีตัวตนของอุปกรณ์ - สามอย่างนี้ทำให้ฝั่งรับเขียนโค้ดครั้งเดียวจบ
    return json.dumps({"id": DEVICE_ID, "v": round(value, 1), "unit": UNIT,
                       "state": state, "kind": kind, "t": time.ticks_ms()})


def send(value, state, kind):
    # ทุกใบผ่านประตูนี้ ทั้ง beat ทั้ง event ทั้ง ack - จึงนับใบที่ส่งไม่ออกได้ที่เดียวจบ
    # examples/s12/04_heartbeat_and_alert.py คือที่มาของการแยกชนิดใบ (kind) ตั้งแต่แรก
    # heartbeat เดินตามนาฬิกา alert เดินตามเหตุการณ์ สองอย่างนี้เดินคนละนาฬิกาเพราะตอบคนละคำถาม
    # และ examples/s12/05_hmi_survives_offline.py คือเหตุผลที่ฟังก์ชันนี้คืน False แทนที่จะโยน error
    # ความล้มเหลวของสายต้องไม่ลามมาเป็นความล้มเหลวของจอ
    if not mqtt.is_connected():
        return False
    mqtt.publish(TOPIC, payload(value, state, kind))
    return True


# --- ท่าที่ 5: กันเน็ตหลุด ---
# ไฟล์นี้ใช้ระยะลองใหม่คงที่ (RETRY_MS) เพราะคาบเรียนสั้นและอยากให้เห็นผลเร็ว
# ของที่ต้องอยู่เป็นเดือนไม่ทำแบบนี้ - examples/s12/03_reconnect_backoff.py ถอยห่างเป็นเท่าตัว
# ทุกครั้งที่ไม่สำเร็จจนถึงเพดาน แล้วรีเซ็ตทันทีที่ต่อกลับได้ นั่นคือรุ่นที่ควรยกไปใช้ต่อ
def go_online():
    if not wifi.is_connected():
        if not wifi.connect(WIFI_SSID, WIFI_PASS):
            return False
    return mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID)


state = "OK"
pending = "OK"                    # ระดับที่กำลังจะเชื่อ ถ้ามันยืนยันตัวเองครบ CONFIRM_N รอบ
streak = 0
latched = False                   # เตือนแล้วค้างไว้ จนกว่าคนจะกดรับทราบ
asking = False                    # กำลังรอคำตอบจากกล่องยืนยันอยู่ไหม
beacon(False)
online = go_online()
missed = 0
sent = 0
last_alert = None
now = time.ticks_ms()
t_beat = now
t_retry = now

while True:
    now = time.ticks_ms()
    value = read_value()
    level = level_of(value)

    # ยืนยันก่อนเปลี่ยนสถานะ: ระดับใหม่ต้องยืนพื้นให้ครบ CONFIRM_N รอบก่อน
    # บล็อก streak ข้างล่างนี้คือหัวใจของ examples/s12/02_confirm_n.py ทั้งไฟล์
    # และถ้าทีมไหนเปลี่ยนแหล่งค่าไปเป็นเสียง ให้ดู examples/s08/02_mic_sound_level_meter.py
    # ชิ้นเดียวกันนี้ครอบค่าจากไมค์ได้เหมือนกัน และเป็นตัวกันข้อความสามสิบใบต่อนาที
    if level == pending:
        streak += 1
    else:
        pending = level
        streak = 1

    if streak >= CONFIRM_N and pending != state:
        state = pending
        if state == "ALERT":
            latched = True
            beacon(True)           # ของจริงขยับเอง ไม่ต้องรอคนกด
            ready = last_alert is None or time.ticks_diff(now, last_alert) >= ALERT_GAP_MS
            if ready:
                if send(value, state, "event"):
                    last_alert = now
                    sent += 1
                else:
                    missed += 1   # ตั้งใจทิ้ง ไม่เก็บย้อนหลัง เพราะค่าที่ค้างนานแล้วไม่มีประโยชน์
        elif state == "OK" and not latched:
            if send(value, state, "clear"):
                sent += 1

    if online and not mqtt.is_connected():
        online = False
        t_retry = now
    if (not online) and time.ticks_diff(now, t_retry) >= RETRY_MS:
        online = go_online()
        t_retry = now
        if online:
            if send(value, state, "back"):     # กลับมาแล้วบอกฝั่งรับทันที อย่าให้เขาเดา
                sent += 1

    if time.ticks_diff(now, t_beat) >= HEARTBEAT_MS:
        t_beat = now
        if send(value, state, "beat"):
            sent += 1
        else:
            missed += 1

    # --- ปุ่มบนจอ: อ่านทุกรอบ แต่คำสั่งที่ทำให้ของจริงขยับต้องผ่านการยืนยันก่อน ---
    for ev in ui.poll():           # ต้องเรียกทุกลูป ไม่งั้นจอจะซ่อน widget ราวสองวินาที
        if ev["type"] != "clicked":
            continue
        if ev["handle"] == btn_on.id():
            # คำสั่งเปิดไม่ต้องยืนยัน เพราะมันย้อนกลับได้ด้วยปุ่มข้าง ๆ ทันที
            beacon(True)
        elif ev["handle"] == btn_off.id() and not asking:
            # คำสั่งที่ทำให้ไฟหน้างานดับ ต้องถามก่อน และคำถามต้องบอกสิ่งที่จะเกิด
            asking = True
            box.show()
            btn_yes.show()
            btn_no.show()
        elif ev["handle"] == btn_yes.id() and asking:
            asking = False
            beacon(False)
            box.hide()
            btn_yes.hide()
            btn_no.hide()
        elif ev["handle"] == btn_no.id() and asking:
            asking = False
            box.hide()
            btn_yes.hide()
            btn_no.hide()
        elif ev["handle"] == btn_ack.id() and latched:
            # คนหน้างานเห็นแล้ว จึงปลดล็อกให้จอกลับเป็นปกติได้
            # ไม่มีอะไรค้างอยู่ก็ไม่ต้องทำอะไร ปุ่มที่กดแล้วส่งใบเปล่าคือปุ่มที่สร้างขยะ
            latched = False
            if send(value, state, "ack"):
                sent += 1

    show(now, value, state, latched,
         "online" if online else "offline - missed " + str(missed), sent)
    time.sleep_ms(LOOP_MS)
