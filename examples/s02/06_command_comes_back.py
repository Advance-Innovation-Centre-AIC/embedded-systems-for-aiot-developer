# 06_command_comes_back.py - คนอื่นพิมพ์คำสั่งจากที่ไกล แล้วไฟบนโต๊ะเราติด
#
# ไฟล์นี้ใช้ของใหม่จากโมดูล mqtt สองชิ้น คือ subscribe บอก broker ว่าเราขอฟัง
# หัวข้อไหน และ get_message ที่หยิบข้อความที่มาถึงแล้วออกมาอ่าน
# ไฟล์ 05 ส่งออกอย่างเดียว ไฟล์นี้เติมทางกลับให้ครบวง
#
# ไฟล์นี้สอน: get_message() ไม่บล็อก มันคืน None ทันทีเมื่อยังไม่มีอะไรมา
#             ลูปจึงต้องถามซ้ำเรื่อย ๆ และห้ามหลับยาว เพราะกล่องรับมีช่องเดียว
#             ข้อความใบที่สองที่มาถึงก่อนเราหยิบใบแรก จะทับใบแรกทิ้งไปเลย
# ดูที่จอ   : สถานะสายอยู่บนสุด ตัวนับข้อความที่ได้รับ และคำสั่งล่าสุดตัวใหญ่
#             พิมพ์ {"cmd":"beep"} ส่งเข้าหัวข้อที่จอบอก แล้วบอร์ดจะร้องทันที
# กับดัก    : payload ที่ได้มาเป็น bytes ไม่ใช่ str ต้อง .decode() ก่อนเสมอ
#             และคนส่งพิมพ์มั่วได้ ข้อความที่ไม่ใช่ JSON ต้องไม่ทำให้ทั้งโปรแกรมตาย

import gpio
import json
import lcd
import time
import ui
import wifi
import mqtt

# แก้ห้าบรรทัดนี้ให้ตรงกับที่ผู้สอนแจกหน้าห้อง
WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
BROKER = "192.168.1.50"
DEVICE_ID = "eva-team03"
TOPIC_CMD = "bento/eva-team03/cmd"

LISTEN_MS = 60000    # เปิดฟังนานเท่าไร
POLL_MS = 100        # ถามกล่องรับถี่แค่ไหน ยิ่งห่างยิ่งเสี่ยงข้อความทับกัน

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x00E676, 0xFFA726, 0xFF5252, 0x40C4FF

n_leds = gpio.num_leds()

ui.screen()
time.sleep_ms(200)

ui.Label("รับคำสั่งจากที่ไกล", x=20, y=12, color=COL_TEXT, value=24)
status = ui.Label("กำลังจะเริ่ม", x=20, y=48, color=COL_DIM, value=18)

ui.Panel(x=20, y=78, w=650, h=96, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("สถานะสาย", x=36, y=88, color=COL_DIM, value=16)
link_lbl = ui.Label("ยังไม่ได้ต่อ", x=36, y=112, color=COL_WARN, value=24)

ui.Label("ได้รับแล้ว (ใบ)", x=380, y=88, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=380, y=110, w=150, h=56, color=COL_INFO)

ui.Label("คำสั่งล่าสุดที่ได้รับ", x=20, y=190, color=COL_DIM, value=16)
last_lbl = ui.Label("ยังไม่มีคำสั่งเข้ามา", x=20, y=216, color=COL_DIM,
                    value=24)

# ไอคอนตอบกลับด้วยภาพ คนที่ยืนไกลจากจอก็ดูออกว่าบอร์ดได้ยินคำสั่งแล้ว
img = ui.Image("smiley", x=560, y=190, w=48, h=48, color=COL_DIM)

ui.Label("หัวข้อที่ฟังอยู่", x=20, y=262, color=COL_DIM, value=16)
topic_lbl = ui.Label("ยังไม่ได้ subscribe", x=20, y=288, color=COL_DIM,
                     value=18)

note = ui.Label("กำลังเริ่ม", x=20, y=330, color=COL_DIM, value=18)
ui.Label("คำสั่งที่รู้จัก beep / led / say", x=20, y=366, color=COL_DIM,
         value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>รับคำสั่งจากที่ไกล</h2>")


def stop_here(screen_msg, log_msg):
    """ปิดงานอย่างสุภาพ บอกบนจอว่าไปไม่ถึงไหน แล้วจบ ไม่ค้างรอ"""
    link_lbl.color(COL_BAD)
    link_lbl.text(screen_msg)
    note.color(COL_BAD)
    note.text(log_msg)
    status.color(COL_BAD)
    status.text("จบตรงนี้ ยังไม่ได้เข้าส่วนรับคำสั่ง")
    ui.poll()
    lcd.print("<span class=error>" + log_msg + "</span>")
    print("หยุดที่:", log_msg)
    raise SystemExit


# --- ต่อ WiFi ---
status.color(COL_WARN)
status.text("กำลังต่อ WiFi จอจะนิ่งสักครู่")
ui.poll()
lcd.print("กำลังต่อ WiFi", WIFI_SSID)

if not wifi.connect(WIFI_SSID, WIFI_PASS):
    heard = False
    for net in wifi.scan():
        if net[0] == WIFI_SSID:
            heard = True
    if heard:
        why = "ได้ยินวง " + WIFI_SSID + " แต่ต่อไม่ผ่าน ตรวจรหัสผ่าน"
    else:
        why = "ไม่ได้ยินวง " + WIFI_SSID + " เลย ตรวจชื่อวงหรือย้ายที่"
    stop_here("ต่อ WiFi ไม่ติด", why)

ip = wifi.ip()
if ip == "0.0.0.0":
    stop_here("ไม่มีเลข IP", "ลิงก์ขึ้นแล้วแต่ DHCP ไม่ให้เลข รับอะไรไม่ได้")

link_lbl.color(COL_WARN)
link_lbl.text("IP " + ip + " กำลังต่อ broker")
ui.poll()
lcd.print("<span class=ok>ได้ IP", ip, "</span>")

# --- ต่อ broker ---
try:
    linked = mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID, keepalive=60)
except OSError:
    stop_here("broker ไม่ตอบ", "broker ที่ " + BROKER + " ไม่ตอบ")

if not linked:
    stop_here("broker ปฏิเสธ", "ตรวจ IP ของ broker และพอร์ต 1883")

# --- ขอฟังหัวข้อ ---
# subscribe() ต้องมาหลัง connect() เสมอ ขอฟังก่อนต่อคือขอกับคนที่ยังไม่ได้คุยด้วย
# และถ้าสายหลุดแล้วต่อใหม่ ต้อง subscribe ซ้ำ broker ไม่ได้จำให้
if not mqtt.subscribe(TOPIC_CMD):
    stop_here("subscribe ไม่ผ่าน", "broker ไม่ยอมให้ฟังหัวข้อ " + TOPIC_CMD)

link_lbl.color(COL_OK)
link_lbl.text("ต่อแล้ว " + BROKER)
topic_lbl.color(COL_TEXT)
topic_lbl.text(TOPIC_CMD)
status.color(COL_OK)
status.text("พร้อมรับคำสั่งแล้ว")
note.color(COL_DIM)
note.text('ลองส่ง {"cmd":"beep"} เข้าหัวข้อข้างบน')
ui.poll()

lcd.print("<span class=ok>ฟังหัวข้อ", TOPIC_CMD, "อยู่</span>")
lcd.print('ลองส่ง {"cmd":"beep"} หรือ {"cmd":"led","n":0}')

t0 = time.ticks_ms()
got = 0
bad = 0

while True:
    t_work = time.ticks_ms()
    left_ms = LISTEN_MS - time.ticks_diff(t_work, t0)
    if left_ms <= 0:
        break

    # ถามทุกรอบ ห้ามหลับยาว กล่องรับมีช่องเดียว ข้อความที่มาติด ๆ กันสองใบ
    # ใบหลังจะทับใบแรกทิ้งไปเลย โดยไม่มีอะไรบอกเราว่ามีใบที่หายไป
    msg = mqtt.get_message()

    if msg is not None:
        got = got + 1
        seg.text(str(got))

        # msg เป็น tuple สองช่อง topic เป็น str ส่วน payload เป็น bytes
        topic, raw = msg

        try:
            cmd = json.loads(raw.decode())
        except ValueError:
            # คนส่งพิมพ์มั่วได้เสมอ ข้อความที่ไม่ใช่ JSON ต้องไม่ทำให้โปรแกรมตาย
            bad = bad + 1
            last_lbl.color(COL_BAD)
            last_lbl.text("ไม่ใช่ JSON")
            img.icon("cross")
            img.color(COL_BAD)
            lcd.print("<span class=error>ได้ของที่ไม่ใช่ JSON:", raw, "</span>")
            cmd = {}

        # .get() แทนการเข้าถึงคีย์ตรง ๆ เพราะ JSON ที่ถูกต้องแต่ไม่มีคีย์ cmd ก็มีได้
        action = cmd.get("cmd", "")

        if action == "beep":
            # ui.tone รับโน้ต MIDI 0-127 ไม่ใช่ความถี่ และรับแบบตำแหน่งเท่านั้น
            ui.tone(69, ui.WAVE_SQUARE, 90, 150)
            last_lbl.color(COL_OK)
            last_lbl.text("beep")
            img.icon("star")
            img.color(COL_OK)
            lcd.print("<span class=ok>ใบที่", got, "-> beep</span>")

        elif action == "led":
            # คนส่งอาจใส่เลขดวงที่บอร์ดนี้ไม่มี ต้องกันไว้เอง ไม่ใช่เชื่อคนส่ง
            n = cmd.get("n", 0)
            if not isinstance(n, int) or n < 0 or n >= n_leds:
                bad = bad + 1
                last_lbl.color(COL_BAD)
                last_lbl.text("led เลขดวงผิด " + str(n))
                img.icon("cross")
                img.color(COL_BAD)
                lcd.print("<span class=error>ไม่มีดวงที่", n, "</span>")
            else:
                on = cmd.get("on", 1)
                led = gpio.led(n)
                if on:
                    led.on()
                else:
                    led.off()
                last_lbl.color(COL_OK)
                last_lbl.text("led " + str(n) + (" ติด" if on else " ดับ"))
                img.icon("check")
                img.color(COL_OK)
                lcd.print("<span class=ok>ใบที่", got, "-> led", n, "</span>")

        elif action == "say":
            # ข้อความจากคนอื่นยาวแค่ไหนก็ได้ ป้ายพาไปได้ 126 ไบต์ ไทยตัวละ 3 ไบต์
            # ตัดให้สั้นก่อนเสมอ ไม่ใช่หวังว่าคนส่งจะพิมพ์สั้น
            text = str(cmd.get("text", ""))[:24]
            last_lbl.color(COL_INFO)
            last_lbl.text(text if text != "" else "say ที่ไม่มีข้อความ")
            img.icon("flag")
            img.color(COL_INFO)
            lcd.print("<span class=info>ใบที่", got, "-> say", text, "</span>")

        elif action != "":
            bad = bad + 1
            last_lbl.color(COL_WARN)
            last_lbl.text("ไม่รู้จักคำสั่ง " + str(action)[:16])
            img.icon("cross")
            img.color(COL_WARN)
            lcd.print("<span class=warn>ไม่รู้จักคำสั่ง", action, "</span>")

    # สายหลุดระหว่างฟังก็เกิดได้ ถามทุกรอบดีกว่าเชื่อคำตอบตอนต้นโปรแกรม
    if not mqtt.is_connected():
        link_lbl.color(COL_BAD)
        link_lbl.text("สายหลุดแล้ว")
        note.color(COL_BAD)
        note.text("หลุดตอนวินาทีที่ " +
                  str(time.ticks_diff(time.ticks_ms(), t0) // 1000))
        ui.poll()
        lcd.print("<span class=error>สายหลุดระหว่างฟัง</span>")
        break

    status.text("ฟังอยู่ - เหลืออีก " + str(left_ms // 1000) + " วินาที")
    ui.poll()

    work = time.ticks_diff(time.ticks_ms(), t_work)
    rest = POLL_MS - work
    if rest > 0:
        time.sleep_ms(rest)

# ปิดไฟทุกดวงก่อนจบ ไม่ทิ้งบอร์ดไว้ในสถานะที่คำสั่งสุดท้ายบังเอิญตั้งไว้
for i in range(n_leds):
    gpio.led(i).off()

status.color(COL_DIM)
status.text("เลิกฟังแล้ว - ได้รับ " + str(got) + " ใบ ใช้ไม่ได้ " + str(bad))
ui.poll()

lcd.console("<span class=muted>------------------------</span>")
lcd.print("<span class=ok>ได้รับ", got, "ใบ | ใช้ไม่ได้", bad, "ใบ</span>")
print("ได้รับ", got, "ใบ | ใช้ไม่ได้", bad, "ใบ")

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# ตั้ง POLL_MS = 3000 แล้วรันใหม่ จากนั้นยิงคำสั่งเข้ามาสามใบรวดภายในวินาทีเดียว
# แล้วนับว่าตัวเลขบนจอขึ้นกี่ใบ เทียบกับที่คุณส่งไปจริงสามใบ
# ใบ้: กล่องรับมีช่องเดียว ใบที่มาถึงตอนที่ช่องยังไม่ว่าง ไม่ได้ไปต่อคิว มันทับของเดิม
