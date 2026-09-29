# s03_led_button.py - ไฟวิ่งทุกดวง + ปุ่มนับครั้งแบบกันเด้ง
# วิธีรัน: 1) บนจอบอร์ด แตะการ์ด Playground ก่อน
#          2) ลองแก้ STEP_MS เป็นสองค่าแล้วรันใหม่ ดูจังหวะไฟเปลี่ยนตามจริง
#          3) กด Program to Device โปรแกรมรันเอง 30 วินาทีแล้วจบ ไม่ต้องกด RESTART
# ลูปเดียวทำสองงานคนละจังหวะ โดยไม่ใช้ sleep ยาว ๆ ขวางทาง
# ดูที่จอ: บนซ้ายไฟ บนขวาปุ่มกับตัวนับ ล่างซ้ายปุ่มสั่งงาน ล่างขวาเวลาที่เหลือ
# กับดัก : ปุ่มเปิดกับปุ่มปิดต้องแยกกัน ปุ่มสลับบอกไม่ได้ว่าตอนนี้อยู่สถานะไหน

import gpio
import lcd
import time
import ui

# --- หน้าปัด: ปรับสี่ค่านี้ได้โดยไม่ต้องอ่านตรรกะ ---
STEP_MS = 150          # จังหวะไฟวิ่ง ปรับตรงนี้เพื่อเปลี่ยนความเร็ว
DEBOUNCE_MS = 40       # เวลาที่ปุ่มต้องนิ่งก่อนเราจะเชื่อ (น้อยกว่า 10 ยังเด้งหลุด เกิน 200 รู้สึกหน่วง)
POLL_MS = 5            # ความถี่ที่ลูปถามปุ่ม
RUN_MS = 30000         # อายุของโปรแกรมรอบนี้

NUM_LEDS = gpio.num_leds()
# gpio.button(1) จะโยน ValueError เพราะโมดูล gpio เปิดให้แค่ปุ่มเดียว ทั้งสองบอร์ด
btn = gpio.button(0)

# --- ท่าที่ 1: ถามบอร์ดก่อนว่ามีอะไรให้เล่นบ้าง ---
info = gpio.board_info()
lcd.clear()
lcd.console("<h2>AIoT in Action - คาบ 3</h2>")
lcd.print("บอร์ด:", info["name"], "| LED:", info["leds"], "| ปุ่ม:", info["buttons"])

# แยกพิมพ์สองครั้ง เพราะรวมกันแล้วเกิน 127 ไบต์ ระบบจะตัดทิ้งเงียบ ๆ
# ชื่อปุ่มเอาจากบอร์ด ไม่ใช่จากตัวพิมพ์บนแผ่นวงจร
lcd.print("<span class=muted>ปุ่มผู้ใช้มีตัวเดียว ดัชนี 0</span>")
lcd.print("<span class=muted>โค้ดเรียกมันว่า " + btn.name() + "</span>")

# --- ท่าที่ 2: ดับไฟให้หมดก่อน แล้วเตรียมตัวแปรสถานะ ---
# ไฟอาจค้างจากโปรแกรมก่อน ดับให้หมดเพื่อเริ่มจากสถานะที่รู้แน่
for i in range(NUM_LEDS):
    gpio.led(i).off()

# จำ led_index เอง: gpio.led(i).value() ตอบระดับขา อาจได้ 0 ทั้งที่หลอดเพิ่งสว่าง
# กับดัก: บน Eva Kit ดวง "RGB_RED" ส่องเป็นสีน้ำเงิน
led_index = 0          # ตอนนี้ไฟดวงไหนกำลังติด
count = 0              # จำนวนครั้งที่กดปุ่ม
raw = False            # ค่าดิบของปุ่มรอบนี้
last_raw = False       # ค่าดิบของปุ่มรอบก่อน
stable = False         # ค่าปุ่มที่ผ่านการกันเด้งแล้ว

# --- แผงควบคุมบนจอ สร้างครั้งเดียวก่อนเข้าลูป ---
# จอมีที่ให้ widget 64 ตัว และสร้างซ้ำในลูปคือยิง IPC ทิ้งเปล่า ๆ
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD, COL_OK, COL_RUN = 0x171B22, 0x30A46C, 0x4A9EFF
UI_MS = 100            # จอถูกอัปเดตทุก 100 ms ไม่ใช่ทุกรอบลูป

ui.screen()
time.sleep_ms(200)
ui.Label("แผงคุมไฟวิ่ง - คาบ 3", x=24, y=8, color=COL_TEXT, value=28)
lbl_status = ui.Label("ไฟวิ่งกำลังเดิน", x=360, y=12, color=COL_DIM, value=20)

# การ์ดซ้ายบน: ไฟบนจอสะท้อนสิ่งที่โปรแกรม "สั่ง" ไม่ใช่สิ่งที่ขา "อ่านได้"
# ระยะต่อดวงคิดจาก NUM_LEDS บอร์ดห้าดวงก็ไม่ล้นการ์ด
ui.Panel(x=24, y=52, w=440, h=144, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ไฟบนบอร์ด " + str(NUM_LEDS) + " ดวง", x=40, y=68, color=COL_DIM, value=16)
PITCH = min(128, 408 // NUM_LEDS)
LED_W = 48 if PITCH >= 128 else 36
led_ui = []
for i in range(NUM_LEDS):
    x = 40 + i * PITCH
    led_ui.append(ui.Led(x=x, y=104, w=LED_W, h=LED_W, color=COL_OK, value=0))
    tag = ("ดวง " if PITCH >= 128 else "") + str(i + 1)
    ui.Label(tag, x=x + LED_W + (16 if PITCH >= 128 else 8), y=112, color=COL_DIM, value=16)
ui.Label("จอสะท้อนคำสั่ง ไม่ใช่ค่าที่ขาอ่าน", x=40, y=160, color=COL_DIM, value=16)

# การ์ดขวาบน: สถานะปุ่มจริง กับตัวนับ
ui.Panel(x=480, y=52, w=288, h=144, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ปุ่ม " + btn.name(), x=496, y=68, color=COL_DIM, value=16)
led_btn = ui.Led(x=496, y=104, w=48, h=48, color=COL_RUN, value=0)
ui.Label("กำลังกด", x=496, y=156, color=COL_DIM, value=16)
ui.Label("นับได้ (ครั้ง)", x=596, y=100, color=COL_DIM, value=16)
seg_count = ui.Seg7("0", x=640, y=132, w=88, h=56, color=COL_TEXT)

# การ์ดซ้ายล่าง: ปุ่มสูง 88 px เว้นห่าง 32 px ตามระยะนิ้วจริง
ui.Panel(x=24, y=212, w=440, h=160, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("คำสั่งไฟวิ่ง", x=40, y=228, color=COL_DIM, value=16)
btn_run = ui.Button("เดินไฟวิ่ง", x=40, y=268, w=176, h=88, color=0x30A46C, value=20)
btn_stop = ui.Button("หยุดไฟวิ่ง", x=248, y=268, w=176, h=88, color=0x3A4150, value=20)

# การ์ดขวาล่าง: เวลาที่เหลือ พร้อมแถบบอกพิสัย
ui.Panel(x=480, y=212, w=288, h=160, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("เวลาที่เหลือของรอบนี้", x=496, y=228, color=COL_DIM, value=16)
lbl_left = ui.Label("30 วิ", x=496, y=264, color=COL_TEXT, value=24)
bar_left = ui.Bar(x=496, y=308, w=192, h=16, color=COL_RUN,
                  min=0, max=RUN_MS // 1000, value=0)
bar_left.value(RUN_MS // 1000)      # สร้างที่ 0 แล้วค่อยตั้งค่า: Bar ที่สร้างพร้อมค่าไม่เป็นศูนย์ทำให้จอว่างทั้งหน้า
sc_left = ui.Scale(x=496, y=324, w=192, h=40, color=COL_TEXT,
                   min=0, max=RUN_MS // 1000)
sc_left.ticks(16, 5)

# กล่องยืนยันสร้างไว้ก่อนแล้วซ่อน ไม่สร้างตอนกด - handle มีจำกัด
# ข้อความ MsgBox รวมได้ 95 ไบต์ (ไทยตัวละ 3) เกินนั้นถูกตัดเงียบ ๆ
box = ui.MsgBox("ยืนยันหยุด\nไฟทุกดวงจะดับทันที",
                x=48, y=96, w=496, h=160, color=COL_CARD)
box.hide()
# ปุ่มใน MsgBox ยังไม่ส่งเหตุการณ์ให้ Python จึงใช้ ui.Button สองปุ่มแทน
btn_yes = ui.Button("ยืนยัน", x=568, y=96, w=152, h=88, color=0x3A4150, value=20)
btn_no = ui.Button("ยกเลิก", x=568, y=216, w=152, h=88, color=0x3A4150, value=20)
btn_yes.hide()
btn_no.hide()
ui.poll()

# เวลาสามตัวตั้งต้นจาก ticks_ms() ค่าเดียวกัน
t0 = time.ticks_ms()
last_step = t0
last_change = t0
last_ui = t0
chase_on = True        # ไฟวิ่งเดินอยู่ไหม - ปุ่มบนจอเป็นคนเปลี่ยนค่านี้
asking = False         # กำลังรอคำตอบจากกล่องยืนยันอยู่ไหม
last_sec = -1

while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    now = time.ticks_ms()      # ขอเวลาครั้งเดียวต่อรอบ แล้วใช้ร่วมกันทั้งสองงาน

    # --- ท่าที่ 3: ไฟวิ่งตามนาฬิกา ไม่ใช่ตาม sleep ---
    if chase_on and time.ticks_diff(now, last_step) >= STEP_MS:
        gpio.led(led_index).off()              # ดับดวงเดิมก่อน
        led_index = (led_index + 1) % NUM_LEDS # เลื่อนไปดวงถัดไป % NUM_LEDS ทำให้วนกลับที่ 0 เอง
        gpio.led(led_index).on()               # จุดดวงใหม่
        last_step = now                        # ต้องอยู่ใน if เท่านั้น ไม่งั้นไฟจะไม่วิ่งเลย
        for k in range(NUM_LEDS):
            led_ui[k].value(1 if k == led_index else 0)

    # --- ท่าที่ 4: อ่านปุ่มทุกรอบ แต่เชื่อเฉพาะค่าที่นิ่งแล้ว ---
    raw = btn.is_pressed()

    if raw != last_raw:
        # ค่าเพิ่งขยับ ยังไม่เชื่อ แค่จับเวลาใหม่
        last_raw = raw
        last_change = now
    elif raw != stable and time.ticks_diff(now, last_change) >= DEBOUNCE_MS:
        # นิ่งครบ DEBOUNCE_MS แล้ว ถึงจะยอมรับเป็นค่าจริง
        stable = raw
        if stable:
            # ถ้าเอา if stable: ออก ตอนปล่อยปุ่มก็ผ่านทางนี้ด้วย เลขจะเพิ่มทีละสอง
            count += 1
            lcd.print("กดครั้งที่", count)

    # --- ท่าที่ 4 ครึ่งหลัง: จอกับปุ่มบนจอ เดินคนละจังหวะ ---
    # ไม่เรียก ui.poll() ทุกรอบลูป เพราะทุกครั้งคือการยิง IPC ข้ามคอร์
    if time.ticks_diff(now, last_ui) >= UI_MS:
        last_ui = now
        led_btn.value(1 if stable else 0)

        # ตัวเลขบนจอเขียนใหม่ไม่เกินวินาทีละครั้ง
        left_s = (RUN_MS - time.ticks_diff(now, t0)) // 1000
        if left_s != last_sec:
            last_sec = left_s
            seg_count.text(str(count))
            lbl_left.text(str(left_s) + " วิ")
            bar_left.value(left_s)

        for ev in ui.poll():
            if ev["type"] != "clicked":
                continue
            if ev["handle"] == btn_run.id():
                # คำสั่งเดินไม่ต้องยืนยัน เพราะย้อนกลับได้ทันที
                chase_on = True
                lbl_status.text("ไฟวิ่งกำลังเดิน")
            elif ev["handle"] == btn_stop.id() and not asking:
                # คำสั่งที่ทำให้ของจริงหยุด ต้องถามก่อน และคำถามต้องบอกสิ่งที่จะเกิด
                asking = True
                box.show()
                btn_yes.show()
                btn_no.show()
                lbl_status.hide()
            elif ev["handle"] == btn_yes.id() and asking:
                asking = False
                chase_on = False
                for k in range(NUM_LEDS):
                    gpio.led(k).off()
                    led_ui[k].value(0)
                lbl_status.text("หยุดแล้ว ไฟดับทุกดวง")
                box.hide()
                btn_yes.hide()
                btn_no.hide()
                lbl_status.show()
            elif ev["handle"] == btn_no.id() and asking:
                asking = False
                box.hide()
                btn_yes.hide()
                btn_no.hide()
                lbl_status.show()

    time.sleep_ms(POLL_MS)                     # จุดเดียวที่โปรแกรมยอมพัก

# --- ท่าที่ 5: ดับไฟ แล้วสรุปผลปิดท้าย ---
# ปิดท้ายด้วย off() เสมอ ไม่ทิ้งไฟติดค้าง
for i in range(NUM_LEDS):
    gpio.led(i).off()
    led_ui[i].value(0)

# จอต้องบอกว่ารอบนี้จบแล้ว ค่าที่ค้างไม่ใช่ค่าปัจจุบัน
seg_count.text(str(count))
bar_left.value(0)
lbl_left.text("0 วิ")
lbl_status.text("รอบนี้จบแล้ว ตัวเลขข้างบนคือค่าสุดท้าย")
ui.poll()

# str(count) จำเป็นเพราะต่อสตริงด้วย +
lcd.print("<span class=ok>จบรอบทดสอบ กดปุ่มทั้งหมด " + str(count) + " ครั้ง</span>")

print("โปรแกรมจบแล้ว - ไฟทุกดวงถูกดับเรียบร้อย")
