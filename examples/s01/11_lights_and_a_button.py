# 11_lights_and_a_button.py - หลอดไฟกับปุ่มจริง สั่งได้จาก Python บรรทัดเดียว
#
# ไฟล์นี้ใช้ของใหม่สองกลุ่ม คือ gpio.led(n) ที่มี on off toggle brightness hold
# กับ gpio.button(n) ที่มี is_pressed name ทั้งหมดมาจาก 10_board_knows_itself.py
# ที่บอกไว้แล้วว่าบอร์ดนี้มีหลอดกี่ดวง ปุ่มกี่ปุ่ม
#
# ไฟล์นี้สอน: เขียนโปรแกรมด้วย gpio.num_leds() ไม่ใช่ด้วยเลข 3 ที่จำมา
#             โค้ดชุดเดียวจึงวิ่งครบทุกดวงบนบอร์ดใบไหนก็ได้
# ดูที่จอ   : ท่าที่ 1 หลอดวิ่งไล่ทีละดวง ชื่อของดวงที่กำลังติดขึ้นบนจอพร้อมกัน
#             ท่าที่ 2 หลอดดวงแรกไล่ความสว่างขึ้นลง แถบบนจอเดินตาม
#             ท่าที่ 3 จอรอปุ่ม กดปุ่ม SW2 บนบอร์ดแล้วตัวเลขจะเดินขึ้น
# กับดัก    : brightness(pct) เป็นพัลส์สั้นราว 12 ms แล้วหลอดดับ ตาจึงตามไม่ทัน
#             ท่าที่ค้างให้เห็นคือ hold(pct, ms) ซึ่งย้ำพัลส์เดิมจนครบเวลาที่สั่ง
#             และ duty() คืนเลขครั้งล่าสุดที่เราสั่ง ไม่ได้ไปวัดจากหลอดจริง

import gpio
import lcd
import time
import ui

CHASE_ROUNDS = 2      # หลอดวิ่งไล่กี่รอบ
STEP_MS = 220         # แต่ละดวงติดค้างนานเท่าไร
HOLD_MS = 500         # ระดับความสว่างแต่ละขั้นค้างนานเท่าไร
LADDER = (10, 35, 60, 85, 60, 35, 10)   # ขั้นความสว่างที่ไล่ขึ้นแล้วไล่ลง
WATCH_MS = 12000      # เปิดให้กดปุ่มนานเท่าไร
POLL_MS = 60          # ถามปุ่มถี่แค่ไหน ถี่พอที่การกดเร็ว ๆ จะไม่หลุด

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_INFO = 0x30A46C, 0xF5A623, 0x4A9EFF

n_leds = gpio.num_leds()
n_btns = gpio.num_buttons()

ui.screen()
time.sleep_ms(200)

ui.Label("ไฟจริงกับปุ่มจริง", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=652, h=116, color=COL_CARD, min=COL_DIM, max=12, value=1)

ui.Label("ดวงที่สั่งอยู่", x=40, y=64, color=COL_DIM, value=16)
seg_led = ui.Seg7(text="-", x=40, y=88, w=92, h=56, color=COL_OK)

ui.Label("ชื่อของมัน", x=204, y=64, color=COL_DIM, value=16)
name_lbl = ui.Label("ยังไม่ได้สั่ง", x=180, y=92, color=COL_INFO, value=24)

ui.Label("ความสว่างที่สั่ง", x=400, y=64, color=COL_DIM, value=16)

# Bar ไม่รับ color= ตอนสร้าง ตัวสร้างของ Bar ไม่ได้เอาค่านั้นไปใช้เลย
# ต้องเรียก .color() หลังสร้างถึงจะเปลี่ยนสีได้จริง (ตรวจกับ ui_widget_mgr.c แล้ว)
bar = ui.Bar(x=400, y=96, w=252, h=28, min=0, max=100, value=0)
bar.color(COL_WARN)
duty_lbl = ui.Label("duty() = 0", x=400, y=128, color=COL_DIM, value=16)

ui.Panel(x=20, y=180, w=652, h=104, color=COL_CARD, min=COL_DIM, max=12,
         value=1)
ui.Label("ปุ่มบนบอร์ด", x=40, y=192, color=COL_DIM, value=16)
state_lbl = ui.Label("ยังไม่ถึงคิวปุ่ม", x=40, y=216, color=COL_DIM, value=28)

ui.Label("กดไปแล้ว (ครั้ง)", x=380, y=192, color=COL_DIM, value=16)
seg_cnt = ui.Seg7(text="0", x=380, y=216, w=140, h=56, color=COL_INFO)

phase = ui.Label("ท่าที่ 1 - ไล่ทีละดวง", x=20, y=296, color=COL_WARN,
                 value=20)
note = ui.Label("ยังไม่เริ่ม", x=20, y=332, color=COL_DIM, value=20)
ui.Label("มองที่บอร์ดด้วย ไม่ใช่มองแต่จอ", x=20, y=364, color=COL_DIM,
         value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>ไฟจริงกับปุ่มจริง</h2>")
lcd.print("บอร์ดนี้มีหลอด", n_leds, "ดวง | ปุ่ม", n_btns, "ปุ่ม")

# เก็บวัตถุหลอดไว้ล่วงหน้า จะได้ไม่ต้องเรียก gpio.led(i) ซ้ำในลูปที่วิ่งเร็ว
leds = []
for i in range(n_leds):
    led = gpio.led(i)
    led.off()
    leds.append(led)

# --- ท่าที่ 1: ไล่ทีละดวง ---
# range(n_leds) ไม่ใช่ range(3) ย้ายไปบอร์ดที่มีห้าดวงแล้วโค้ดนี้วิ่งครบเอง
note.text("ไล่ทีละดวง " + str(CHASE_ROUNDS) + " รอบ - มองที่บอร์ด")
ui.poll()

for r in range(CHASE_ROUNDS):
    for i in range(n_leds):
        leds[i].on()
        seg_led.text(str(i))
        name_lbl.text(leds[i].name())
        lcd.print("ติดดวงที่", i, "-", leds[i].name())
        ui.poll()
        time.sleep_ms(STEP_MS)
        leds[i].off()

# --- ท่าที่ 2: ไล่ความสว่าง ---
# ขาของหลอดชุดนี้ไม่มี PWM ของฮาร์ดแวร์ต่ออยู่ ระดับความสว่างจึงค้างเองไม่ได้
# hold(pct, ms) ย้ำพัลส์เดิมซ้ำจนครบเวลา ตาจึงเห็นเป็นระดับที่ค้างอยู่จริง
# และเพราะมันกินเวลาครบ ms พอดี จึงใช้แทน sleep_ms() ไปในตัว
phase.text("ท่าที่ 2 - ไล่ความสว่างดวงที่ 0")
phase.color(COL_INFO)
seg_led.text("0")
name_lbl.text(leds[0].name())
note.text("ไม่มี PWM ก็หรี่ได้ - hold() ย้ำพัลส์ให้เอง")
ui.poll()
lcd.console("<span class=muted>--- ไล่ความสว่าง ---</span>")

for pct in LADDER:
    bar.value(pct)
    bar.color(COL_OK if pct >= 60 else COL_WARN)
    duty_lbl.text("กำลังสั่ง " + str(pct) + " %")
    ui.poll()

    leds[0].hold(pct, HOLD_MS)

    # duty() คืนเปอร์เซ็นต์ครั้งล่าสุดที่เราสั่ง ไม่ได้ไปวัดจากหลอด
    # ตอนนี้ hold จบแล้ว หลอดดับแล้ว แต่ตัวเลขนี้ยังเป็นเลขเดิมอยู่ดี
    duty_lbl.text("duty() = " + str(leds[0].duty()) + " แต่หลอดดับแล้ว")
    lcd.print("hold(", pct, ",", HOLD_MS, ") -> duty()", leds[0].duty())
    ui.poll()

bar.value(0)
leds[0].off()

# --- ท่าที่ 3: รอปุ่ม ---
if n_btns < 1:
    # บอร์ดที่ไม่เปิดปุ่มให้ Python ก็มีอยู่ ต้องพูดออกไปตรง ๆ ไม่ใช่เงียบแล้วจบ
    phase.text("ท่าที่ 3 - บอร์ดนี้ไม่มีปุ่มให้ Python")
    phase.color(COL_DIM)
    state_lbl.text("ข้ามท่านี้")
    ui.poll()
    lcd.print("<span class=warn>ไม่มีปุ่มให้ Python ข้ามท่าที่ 3</span>")
    raise SystemExit

btn = gpio.button(0)

phase.text("ท่าที่ 3 - กดปุ่มบนบอร์ดได้เลย")
phase.color(COL_OK)
state_lbl.text("ปล่อยอยู่")
state_lbl.color(COL_DIM)

# บนแผ่นวงจรพิมพ์ว่า SW2 แต่ .name() คืนอีกชื่อหนึ่ง คนก้มดูบอร์ดกับคนมองโค้ด
# จะเรียกปุ่มเดียวกันคนละชื่อ ถ้าไม่บอกไว้ก็คุยกันไม่รู้เรื่องอยู่ครึ่งชั่วโมง
note.text("บนบอร์ดพิมพ์ว่า SW2 - โค้ดเรียก " + btn.name() + " ดัชนี 0")
ui.poll()
lcd.console("<span class=muted>--- รอปุ่ม ---</span>")
lcd.print("กดปุ่ม SW2 บนบอร์ด - โค้ดเรียกมันว่า", btn.name())

t0 = time.ticks_ms()
presses = 0
was_down = False

while True:
    t_work = time.ticks_ms()
    if time.ticks_diff(t_work, t0) >= WATCH_MS:
        break

    down = btn.is_pressed()

    # นับเฉพาะ "ขอบขาลง" คือรอบที่เพิ่งเปลี่ยนจากปล่อยเป็นกด
    # ถ้านับทุกรอบที่ is_pressed() เป็นจริง การกดค้างหนึ่งครั้งจะถูกนับเป็นร้อยครั้ง
    if down != was_down:
        was_down = down
        if down:
            presses = presses + 1
            seg_cnt.text(str(presses))
            state_lbl.text("กดอยู่")
            state_lbl.color(COL_OK)
            # ไฟตอบกลับทุกครั้งที่กด คนกดจะได้รู้ว่าบอร์ดได้ยินแล้ว
            leds[0].on()
            lcd.print("<span class=ok>กดครั้งที่", presses, "</span>")
        else:
            state_lbl.text("ปล่อยอยู่")
            state_lbl.color(COL_DIM)
            leds[0].off()

    ui.poll()

    work = time.ticks_diff(time.ticks_ms(), t_work)
    left = POLL_MS - work
    if left > 0:
        time.sleep_ms(left)

leds[0].off()
phase.text("จบแล้ว - กดไป " + str(presses) + " ครั้ง")
phase.color(COL_DIM)
state_lbl.text("ปิดรับแล้ว")
state_lbl.color(COL_DIM)
note.text("หลอด " + str(n_leds) + " ดวง ปุ่ม " + str(n_btns) + " ปุ่ม")
ui.poll()

lcd.console("<span class=muted>------------------------</span>")
lcd.print("<span class=ok>กดทั้งหมด", presses, "ครั้ง</span>")

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# ตอนนี้ปุ่มแค่นับกับติดไฟดวงเดิมทุกครั้ง แก้ให้การกดแต่ละครั้งเลื่อนไปติดดวงถัดไป
# แล้ววนกลับดวงแรกเมื่อสุด โดยห้ามพิมพ์เลขจำนวนดวงลงไปตรง ๆ
# ใบ้: ตัวแปร n_leds มีอยู่แล้ว และ % คือเครื่องมือที่ทำให้ตัวเลขวนกลับได้
