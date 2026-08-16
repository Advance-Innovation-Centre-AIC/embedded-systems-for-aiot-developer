# s03_led_button.py - ไฟวิ่งสามดวง + ปุ่มนับครั้งแบบกันเด้ง (ฉบับฝึกเติมโค้ด)
# วิธีรัน: 1) บนจอบอร์ด แตะการ์ด Playground ก่อน แล้วมองหลอด RGB กับปุ่ม SW2 บนบอร์ด
#          2) เติมช่องว่างตามคำใบ้ทีละกลุ่ม - จุด 1-2 เรื่องไฟ จุด 3-5 เรื่องปุ่ม จุด 6 ปิดท้าย
#          3) กด Program to Device โปรแกรมรันเอง 30 วินาทีแล้วจบ ไม่ต้องกด RESTART
#
# คาบนี้เราเขียนลูปที่ทำสองงานพร้อมกันโดยไม่ใช้ sleep ยาว ๆ ขวางทาง
# ไฟขยับตามนาฬิกาทุก 150 ms ส่วนปุ่มถูกถามทุก 5 ms - งานคนละจังหวะอยู่ในลูปเดียวได้
#
# ไฟล์นี้เอาสองเรื่องมารวมกันตั้งแต่แรก ถ้าอยากแยกดูทีละเรื่องก่อน ตัวอย่างของคาบนี้
# แยกไว้ให้แล้วที่ examples/s03/ - ฝั่งไฟเริ่มที่ 02_led_blink.py ฝั่งปุ่มเริ่มที่ 04_button_active_low.py
# ส่วน examples/s03/01_board_info.py มีไว้ตอบคำถามว่าบอร์ดตรงหน้ามีหลอดกี่ดวงและปุ่มชื่ออะไร
#
# ดูที่จอ: บนซ้ายคือไฟสามดวงบนจอที่สะท้อนหลอดจริง บนขวาคือสถานะปุ่มกับตัวนับ
#         ล่างซ้ายคือปุ่มสั่งงานสองปุ่มแยกกัน ล่างขวาคือเวลาที่เหลือของรอบนี้
#         แผงทั้งหมดเขียนมาให้แล้ว ช่องว่างหกจุดอยู่ที่ตรรกะ ไม่ได้อยู่ที่การวาด
# กับดัก : ปุ่มเปิดกับปุ่มปิดต้องแยกกันคนละปุ่ม ห้ามใช้ปุ่มเดียวสลับไปมา เพราะปุ่มสลับ
#         จะบอกไม่ได้ว่าตอนนี้อยู่สถานะไหน คนกดจึงต้องเดา และเดาผิดได้เสมอ

import gpio
import lcd
import time
import ui

# ---------- หน้าปัดของโปรแกรม: ปรับสี่ค่านี้ได้โดยไม่ต้องอ่านตรรกะข้างล่าง ----------
STEP_MS = 150          # จังหวะไฟวิ่ง ปรับตรงนี้เพื่อเปลี่ยนความเร็ว
DEBOUNCE_MS = 40       # เวลาที่ปุ่มต้องนิ่งก่อนเราจะเชื่อ
POLL_MS = 5            # ความถี่ที่ลูปถามปุ่ม
RUN_MS = 30000         # อายุของโปรแกรมรอบนี้

NUM_LEDS = gpio.num_leds()
btn = gpio.button(0)   # เก็บไว้ในตัวแปรครั้งเดียว แล้วใช้ซ้ำทั้งโปรแกรม

# --- ท่าที่ 1: ถามบอร์ดก่อนว่ามีอะไรให้เล่นบ้าง ---
info = gpio.board_info()
lcd.clear()
lcd.console("<h2>AIoT in Action - คาบ 3</h2>")
lcd.print("บอร์ด:", info["name"], "| LED:", info["leds"], "| ปุ่ม:", info["buttons"])

# แยกพิมพ์สองครั้ง เพราะรวมกันแล้วเกิน 127 ไบต์ ระบบจะตัดทิ้งเงียบ ๆ
lcd.print("<span class=muted>ปุ่มบนบอร์ดพิมพ์ว่า SW2</span>")
lcd.print("<span class=muted>แต่โค้ดเรียกมันว่า " + btn.name() + "</span>")

# --- ท่าที่ 2: ดับไฟให้หมดก่อน แล้วเตรียมตัวแปรสถานะ ---
for i in range(NUM_LEDS):
    # เติม: gpio.led(i).off()
    # ถ้ายังไม่ชินกับ on() off() toggle(): examples/s03/02_led_blink.py แยกให้ดูทีละชั้นว่า
    #   on/off สั่งค่าตรง ๆ ส่วน toggle สั่งกลับด้านจากค่าเดิม และปิดท้ายด้วยเหตุผลที่ว่า
    #   โปรแกรมที่จบแล้วต้องบอกได้ว่าไฟค้างอยู่สถานะไหน จึงต้องมีบรรทัด off() แบบนี้เสมอ
    pass

led_index = 0          # ตอนนี้ไฟดวงไหนกำลังติด (ขาตอบระดับ ไม่ตอบความตั้งใจ ต้องจำเอง)
count = 0              # จำนวนครั้งที่กดปุ่ม
raw = False            # ค่าดิบของปุ่มรอบนี้ ตั้งต้นไว้ให้ไฟล์รันได้ก่อนเติมจุดที่ 3
last_raw = False       # ค่าดิบของปุ่มรอบก่อน
stable = False         # ค่าปุ่มที่ผ่านการกันเด้งแล้ว

# --- แผงควบคุมบนจอ สร้างครั้งเดียวก่อนเข้าลูป ---
# สร้างก่อนลูปเสมอ ไม่ใช่สร้างในลูป เพราะจอมีที่ให้ widget ได้ 64 ตัวเท่านั้น
# และการสร้างซ้ำทุกรอบคือการยิง IPC ทิ้งเปล่า ๆ 200 ครั้งต่อวินาที
COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD, COL_OK, COL_RUN = 0x142240, 0x00E676, 0x4FC3F7
UI_MS = 100            # จอถูกอัปเดตทุก 100 ms ไม่ใช่ทุกรอบลูป

ui.screen()
time.sleep_ms(200)
ui.Label("แผงคุมไฟวิ่ง - คาบ 3", x=16, y=6, color=COL_TEXT, value=20)

# การ์ดซ้ายบน: ไฟบนจอสามดวง สะท้อนสิ่งที่โปรแกรม "สั่ง" ไม่ใช่สิ่งที่ขา "อ่านได้"
ui.Panel(x=16, y=34, w=380, h=140, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ไฟบนบอร์ดสามดวง", x=32, y=42, color=COL_DIM, value=16)
led_ui = []
for i in range(NUM_LEDS):
    led_ui.append(ui.Led(x=40 + i * 72, y=72, w=42, h=42, color=COL_OK, value=0))
ui.Label("ดวง 1", x=40, y=122, color=COL_DIM, value=14)
ui.Label("ดวง 2", x=112, y=122, color=COL_DIM, value=14)
ui.Label("ดวง 3", x=184, y=122, color=COL_DIM, value=14)
ui.Label("จอสะท้อนสิ่งที่สั่ง", x=250, y=76, color=COL_DIM, value=14)
ui.Label("ไม่ใช่สิ่งที่ขาอ่านกลับ", x=250, y=98, color=COL_DIM, value=14)

# การ์ดขวาบน: สถานะปุ่มจริง กับตัวนับที่อ่านง่ายจากอีกฝั่งห้อง
ui.Panel(x=406, y=34, w=370, h=140, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ปุ่ม SW2 บนบอร์ด", x=422, y=42, color=COL_DIM, value=16)
led_btn = ui.Led(x=430, y=72, w=42, h=42, color=COL_RUN, value=0)
ui.Label("กำลังกด", x=482, y=84, color=COL_DIM, value=18)
ui.Label("นับได้ (ครั้ง)", x=600, y=42, color=COL_DIM, value=14)
seg_count = ui.Seg7("0", x=600, y=68, w=150, h=60, color=COL_TEXT)

# การ์ดซ้ายล่าง: ปุ่มเปิดกับปุ่มปิดแยกกันคนละปุ่ม ตามกฎของแผงควบคุมจริง
ui.Panel(x=16, y=186, w=380, h=104, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("คำสั่งไฟวิ่ง", x=32, y=194, color=COL_DIM, value=16)
btn_run = ui.Button("เดินไฟวิ่ง", x=32, y=222, w=168, h=56, color=0x1B5E20, value=18)
btn_stop = ui.Button("หยุดไฟวิ่ง", x=212, y=222, w=168, h=56, color=0x37474F, value=18)

# การ์ดขวาล่าง: เวลาที่เหลือ พร้อมพิสัยของมัน ตัวเลขลอย ๆ ไม่บอกว่าเหลือมากหรือน้อย
ui.Panel(x=406, y=186, w=370, h=104, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("เวลาที่เหลือของรอบนี้", x=422, y=194, color=COL_DIM, value=14)
lbl_left = ui.Label("30 วิ", x=640, y=190, color=COL_TEXT, value=20)
bar_left = ui.Bar(x=422, y=222, w=260, h=12, color=COL_RUN,
                  min=0, max=RUN_MS // 1000, value=RUN_MS // 1000)
sc_left = ui.Scale(x=422, y=236, w=260, h=44, color=COL_TEXT,
                   min=0, max=RUN_MS // 1000)
sc_left.ticks(16, 5)

# แถบล่าง: บรรทัดสถานะตอนปกติ และกล่องยืนยันที่ซ่อนไว้ก่อน
# กล่องยืนยันสร้างพร้อมหน้าจอแล้วซ่อนไว้ ไม่ใช่สร้างตอนกด - handle มีจำกัด
# และการสร้างของตอนคนกำลังรอคำตอบ คือการเพิ่มความหน่วงในจังหวะที่แย่ที่สุด
lbl_status = ui.Label("ไฟวิ่งกำลังเดิน", x=32, y=306, color=COL_DIM, value=18)
# ข้อความของ MsgBox เดินทางไปกับ CREATE ซึ่งพาได้ 95 ไบต์ ภาษาไทยตัวละ 3 ไบต์
# แปลว่าหัวเรื่องบวกเนื้อความรวมกันได้ราว 31 ตัวอักษร ยาวกว่านั้นถูกตัดเงียบ ๆ
box = ui.MsgBox("ยืนยันหยุด\nไฟสามดวงจะดับทันที",
                x=16, y=290, w=548, h=100, color=COL_CARD)
box.hide()
# ปุ่มสองปุ่มนี้คือคำตอบของกล่อง - ปุ่มในตัว MsgBox เองยังไม่ส่งเหตุการณ์กลับมา
# ให้ Python เห็น (เฟิร์มแวร์ผูก callback ไว้กับ ui.Button เท่านั้น) ถ้าวางปุ่มตาย
# ไว้บนจอ คนกดจะสรุปว่าเครื่องแฮงก์ จึงใช้ ui.Button จริงสองปุ่มแทน
btn_yes = ui.Button("ยืนยัน", x=580, y=298, w=104, h=42, color=0x37474F, value=16)
btn_no = ui.Button("ยกเลิก", x=580, y=346, w=104, h=42, color=0x37474F, value=16)
btn_yes.hide()
btn_no.hide()
ui.poll()

# เวลาสามตัวตั้งต้นจาก ticks_ms() ค่าเดียวกัน รอบแรกจะได้ไม่เพี้ยน
t0 = time.ticks_ms()
last_step = t0
last_change = t0
last_ui = t0
chase_on = True        # ไฟวิ่งเดินอยู่ไหม - ปุ่มบนจอเป็นคนเปลี่ยนค่านี้
asking = False         # กำลังรอคำตอบจากกล่องยืนยันอยู่ไหม
last_sec = -1          # วินาทีที่เพิ่งเขียนลงจอ กันไม่ให้เขียนซ้ำเร็วกว่า 1 ครั้งต่อวินาที

while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    now = time.ticks_ms()      # ขอเวลาครั้งเดียวต่อรอบ แล้วใช้ร่วมกันทั้งสองงาน

    # --- ท่าที่ 3: ไฟวิ่งตามนาฬิกา ไม่ใช่ตาม sleep ---
    if chase_on and time.ticks_diff(now, last_step) >= STEP_MS:
        gpio.led(led_index).off()              # ดับดวงเดิมก่อน

        # เติม: led_index = (led_index + 1) % NUM_LEDS
        # ทำไมต้องจำ led_index ไว้เอง แทนที่จะถามหลอด: examples/s03/06_button_picks_led.py
        #   ทำเรื่องนี้ทั้งไฟล์ ตัวแปรเดียวจำว่าดวงไหนติด แล้วเลื่อนไปดวงถัดไปด้วย % จำนวนดวง
        #   พร้อมเหตุผลว่าทำไม gpio.led(i).value() ตอบตรงข้ามกับสิ่งที่ตาเห็น
        pass

        gpio.led(led_index).on()               # จุดดวงใหม่
        last_step = now                        # จดเวลาไว้สำหรับรอบหน้า

        # ไฟบนจอสะท้อนหลอดจริง ดวงที่ดับจะหรี่ ไม่ใช่หายไป (เขียนมาให้แล้ว)
        for k in range(NUM_LEDS):
            led_ui[k].value(1 if k == led_index else 0)

    # --- ท่าที่ 4: อ่านปุ่มทุกรอบ แต่เชื่อเฉพาะค่าที่นิ่งแล้ว ---
    # เติม: raw = btn.is_pressed()
    # ค่าที่ได้กลับด้านกับที่คิด: examples/s03/04_button_active_low.py วางเลขดิบจาก .value()
    #   ไว้ข้างคำตอบของ .is_pressed() บนจอเดียวกัน กดค้างแล้วจะเห็นเลยว่า 0 คือกด ไม่ใช่ 1
    pass

    if raw != last_raw:
        last_raw = raw

        # เติม: last_change = now
        # กันเด้งคือกฎข้อเดียว "ค่าต้องนิ่งครบเวลาหนึ่งก่อนเราถึงจะเชื่อ" และบรรทัดนี้คือ
        #   การเริ่มจับเวลานั้นใหม่ examples/s03/05_debounce_count.py เดินตัวนับสองตัวคู่กัน
        #   ดิบกับกันเด้ง กดรัว ๆ สิบครั้งแล้วดูส่วนต่าง จะเห็นว่ากฎข้อนี้ซื้ออะไรมาให้เรา
        pass

    elif raw != stable and time.ticks_diff(now, last_change) >= DEBOUNCE_MS:
        stable = raw
        if stable:                             # นับเฉพาะตอนกด ไม่นับตอนปล่อย
            # เติม: count += 1
            # ถ้าเลขเพิ่มทีละสองทุกครั้งที่กด อย่าเพิ่งโทษปุ่ม
            #   examples/s03/05_debounce_count.py บอกไว้ว่านั่นคืออาการของการลืม if stable:
            #   คือเรานับตอนปล่อยปุ่มไปด้วย ซึ่งเป็นบั๊กของตรรกะเรา ไม่ใช่ของฮาร์ดแวร์
            pass

            lcd.print("กดครั้งที่", count)

    # --- ท่าที่ 4 ครึ่งหลัง: จอกับปุ่มบนจอ เดินคนละจังหวะกับสองงานข้างบน ---
    # ไม่เรียก ui.poll() ทุกรอบลูป เพราะทุกครั้งคือการยิง IPC ข้ามคอร์ - 200 ครั้ง
    # ต่อวินาทีเพื่อรอนิ้วที่มาถึงวินาทีละครั้ง คือการจ่ายแพงกว่าที่ได้มาก
    if time.ticks_diff(now, last_ui) >= UI_MS:
        last_ui = now
        led_btn.value(1 if stable else 0)

        # ตัวเลขที่คนต้องอ่าน เขียนใหม่ไม่เกินวินาทีละครั้ง และอยู่ตำแหน่งเดิมเสมอ
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
                # คำสั่งเดินไม่ต้องยืนยัน เพราะมันย้อนกลับได้ด้วยปุ่มข้าง ๆ ทันที
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
                lbl_status.text("หยุดแล้ว ไฟดับทั้งสามดวง")
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
for i in range(NUM_LEDS):
    gpio.led(i).off()
    led_ui[i].value(0)

# จอต้องบอกด้วยว่ารอบนี้จบแล้ว ไม่ใช่ค้างเลขเดิมไว้เฉย ๆ ให้คนเดินมาดูเข้าใจผิด
# ว่าโปรแกรมยังเดินอยู่ - ค่าที่ค้างต้องระบุว่ามันไม่ใช่ค่าปัจจุบัน
seg_count.text(str(count))
bar_left.value(0)
lbl_left.text("0 วิ")
lbl_status.text("รอบนี้จบแล้ว ตัวเลขข้างบนคือค่าสุดท้าย")
ui.poll()

# เติม: lcd.print("<span class=ok>จบรอบทดสอบ กดปุ่มทั้งหมด " + str(count) + " ครั้ง</span>")
pass

print("โปรแกรมจบแล้ว - ไฟทุกดวงถูกดับเรียบร้อย")
