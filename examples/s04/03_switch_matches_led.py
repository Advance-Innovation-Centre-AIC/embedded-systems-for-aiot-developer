# 03_switch_matches_led.py - จอกับไฟจริงต้องพูดตรงกันเสมอ
#
# ไฟล์นี้สอน: ให้มีฟังก์ชันเดียวที่มีสิทธิ์เปลี่ยนสถานะไฟ ทุกเส้นทางต้องผ่านประตูนั้น
# ดูที่จอ   : แถวสวิตช์ตามจำนวนไฟบนบอร์ด และ Seg7 ตัวโตบอกว่าตอนนี้ติดอยู่กี่ดวง
#             กด ALL OFF แล้วดูสวิตช์เด้งกลับพร้อมกับเลขลงเป็น 0
# กับดัก    : ปุ่ม ALL OFF ดับไฟจริงได้ แต่ไม่ได้แตะสวิตช์บนจอ ถ้าลืมสั่ง sw.value(0)
#             จอจะโกหกทันทีว่าไฟยังเปิดอยู่ ผู้ใช้เชื่อจอ ไม่ได้เชื่อหลอดไฟ

import ui
import gpio
import lcd
import time

RUN_MS = 30000

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC S7.13
COL_TEXT = 0xE8EAED      # ข้อความหลัก
COL_DIM = 0x9AA3AF       # ข้อความรอง หน่วย เชิงอรรถ
COL_CARD = 0x171B22      # พื้นการ์ด และปุ่มรอง
COL_ACCENT = 0x4A9EFF    # สิ่งที่โต้ตอบได้ และค่าที่กำลังเปลี่ยน

ui.screen()
time.sleep_ms(200)

N = gpio.num_leds()
NAMES = gpio.board_info()["led_names"]

# ความจริงของโปรแกรมอยู่ใน list นี้ที่เดียว
# ห้ามไปอ่านจากฮาร์ดแวร์ เพราะ gpio.led(i).value() ตอบระดับของขา ไม่ใช่สิ่งที่เราสั่ง
# และหลัง brightness() หรือ hold() ขาจะถูกทิ้งไว้ต่ำ ค่าที่ได้จึงเป็น 0
led_on = [False] * N

ui.Label("สวิตช์บนจอ กับ ไฟจริง", x=24, y=8, color=COL_TEXT, value=24)

# ป้ายชื่อกับสวิตช์วางเป็นคู่ ห่างกัน 200 พิกเซล กำหนด x y เองทั้งหมด
# สวิตช์กว้าง 120 สูง 88 ตามขั้นต่ำของเป้าสัมผัส ระยะ 200 จึงเหลือช่องว่าง 80
# ระหว่างสวิตช์สองอัน มากกว่าขั้นต่ำ 32 ที่กันนิ้วแตะพลาดอันข้าง ๆ
ui.Panel(x=24, y=56, w=744, h=152, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
switches = []
for i in range(N):
    x = 40 + i * 200
    if x + 120 > 768:           # กันไว้ ถ้าบอร์ดรุ่นอื่นมีไฟมากกว่าสามดวง
        break
    ui.Label(NAMES[i], x=x, y=72, color=COL_DIM, value=16)
    switches.append(ui.Switch(x=x, y=104, w=120, h=88, color=COL_ACCENT))

sw_ids = [s.id() for s in switches]

# การ์ดสรุป ตัวเลขตัวโตอ่านได้จากระยะไกล บรรทัดข้าง ๆ ขยายความให้
ui.Panel(x=24, y=224, w=360, h=112, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
ui.Label("ไฟที่ติดอยู่ (ดวง)", x=40, y=240, color=COL_DIM, value=16)

# Seg7 รับข้อความเท่านั้น str() จึงไม่ใช่เรื่องความสวยงาม แต่เป็นเรื่องจำเป็น
# ขนาดตัวเลขของ Seg7 มาจาก h= ไม่ได้มาจาก value= อย่าง Label
seg = ui.Seg7(text="0", x=40, y=272, w=104, h=48, color=COL_ACCENT)
# Label ที่สร้างด้วยข้อความว่าง LVGL จะเติมคำว่า "Label" ให้เอง
# แล้วคำนั้นค้างบนจอจนกว่าจะมีการเขียนทับครั้งแรก จึงต้องตั้งข้อความตั้งต้นเสมอ
status = ui.Label("ยังไม่ได้ตั้งค่าไฟ", x=168, y=280, color=COL_DIM, value=20)

# ปุ่มกับคำอธิบายของมันวางซ้อนกันเป็นคอลัมน์ขวา ห่างจากการ์ดสรุป 32
off_btn = ui.Button("ALL OFF", x=416, y=224, w=200, h=88, color=COL_CARD,
                    value=24)
off_id = off_btn.id()
ui.Label("สวิตช์จะเด้งกลับเอง", x=416, y=320, color=COL_DIM, value=16)

# แถบล่างสุดสองช่อง ทั้งคู่ต้องจบก่อน x=690 เพราะมุมขวาล่างเป็นที่ของปุ่ม Console
lesson = ui.Label("ทุกเส้นทางผ่าน set_led ทางเดียว", x=24, y=352,
                  color=COL_DIM, value=16)
# ข้อความตอนสร้าง Label ถูกตัดที่ 95 ไบต์ ไทยตัวละ 3 ไบต์ จึงไม่เกิน 31 ตัว
ui.Label("สั่ง gpio ตรง ๆ จอจะโกหก", x=400, y=352, color=COL_DIM, value=16)


def set_led(i, on):
    """ประตูเดียวที่เปลี่ยนสถานะไฟได้ ทุกเส้นทางต้องผ่านฟังก์ชันนี้"""
    led_on[i] = on                      # 1) จำไว้ในตัวแปรของเราก่อน
    if on:
        gpio.led(i).on()                # 2) สั่งของจริง
    else:
        gpio.led(i).off()
    if i < len(switches):
        switches[i].value(1 if on else 0)   # 3) ดึงสวิตช์บนจอให้ตรง

    # 4) รายงานทุกครั้ง ไม่มีข้อยกเว้น และนับจาก led_on อย่างเดียว ไม่ถามฮาร์ดแวร์
    lit = led_on.count(True)
    seg.text(str(lit))
    # "ติดกี่ดวง" ไม่ใช่สถานะดีหรือร้าย จึงไม่ทาเขียว/ส้ม - สีสถานะสงวนไว้บอก
    # ความผิดปกติเท่านั้น ตัวเลขที่กำลังเปลี่ยนใช้สีเน้น ศูนย์ใช้สีข้อความรอง
    seg.color(COL_ACCENT if lit else COL_DIM)
    status.text("จาก " + str(N) + " ดวง")


# เริ่มจากสถานะที่เรารู้แน่ ไม่ "สมมติ" ว่าไฟดับอยู่ แต่สั่งให้ดับ
for i in range(N):
    set_led(i, False)

lcd.print("สลับสวิตช์บนจอ แล้วมองหลอดไฟจริงด้วย")
lcd.print("กด ALL OFF แล้วดูว่าสวิตช์บนจอเด้งกลับด้วยไหม")

t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        h = ev["handle"]
        if ev["type"] == "toggled" and h in sw_ids:
            i = sw_ids.index(h)
            set_led(i, ev["value"] == 1)
            lcd.print(NAMES[i] + " -> " + ("ON" if led_on[i] else "OFF"))
        elif ev["type"] == "clicked" and h == off_id:
            # เรียกผ่านประตูเดิม สวิตช์บนจอจึงเด้งกลับให้เองโดยไม่ต้องสั่งซ้ำ
            for i in range(N):
                set_led(i, False)
            lcd.print("ALL OFF -> ดับทุกดวงผ่านประตูเดิม")
    time.sleep_ms(50)

for i in range(N):
    set_led(i, False)

lesson.text("จบแล้ว - ดับทุกดวงผ่าน set_led")
lcd.print("สรุป: จอกับไฟตรงกันได้ เพราะมีประตูเดียว")
