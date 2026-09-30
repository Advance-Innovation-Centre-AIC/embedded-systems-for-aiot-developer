# 06_command_comes_back.py - คนอื่นพิมพ์คำสั่งจากที่ไกล แล้วไฟบนโต๊ะเราติด
#
# ของใหม่: mqtt.subscribe / get_message ทางกลับของไฟล์ 05
# ไฟล์นี้สอน: get_message() ไม่บล็อก คืน None ทันทีเมื่อยังไม่มีอะไรมา ต้องถามซ้ำ
# ดูที่จอ   : สถานะสาย ตัวนับ และคำสั่งล่าสุด กดปุ่มบนหน้าเว็บ my_first_reader.html
# กับดัก    : กล่องรับมีช่องเดียว ห้ามหลับยาว ใบใหม่ทับใบเก่า
#             payload เป็น bytes ต้อง .decode() และข้อความมั่วต้องไม่ทำให้โปรแกรมตาย
#
# broker สาธารณะ ใครรู้ชื่อหัวข้อก็ส่งคำสั่งเข้ามาได้ อย่าเชื่อคนส่ง

import gpio
import json
import lcd
import time
import ui
import wifi
import mqtt

# แก้สามบรรทัดนี้ตามที่ผู้สอนแจก
WIFI_SSID = "bento-teamXX"            # ชื่อ Hotspot มือถือของทีม (WiFi คณะต้อง login บอร์ดใช้ไม่ได้)
WIFI_PASS = "<รหัส Hotspot ของทีม>"     # อย่างน้อย 8 ตัว
TEAM = "teamXX"                   # team01 ถึง team19 ไม่แก้ไม่รัน

# ไม่ต้องแก้ ชุดเดียวกับไฟล์ 05
BROKER = "broker.hivemq.com"      # สำรอง: "test.mosquitto.org"
ROOT = "bento-aiot"
DEVICE_ID = "bento-aiot-" + TEAM  # client_id ต้องไม่ซ้ำกับใครบน broker
TOPIC_CMD = ROOT + "/" + TEAM + "/cmd"

# ฟัง 15 นาที พอสำหรับเกมทั้งห้องสองเกม
LISTEN_MS = 900000
POLL_MS = 100        # ยิ่งห่างยิ่งเสี่ยงข้อความทับกัน

# จานสีของหลักสูตร
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

class Stop(Exception):
    # จบโปรแกรมแบบปกติ (SystemExit ทำให้บอร์ดเริ่มระบบใหม่ และอาจค้างจนต้องถอดสาย)
    pass

n_leds = gpio.num_leds()

ui.screen()
time.sleep_ms(200)

ui.Label("รับคำสั่งจากที่ไกล", x=24, y=8, color=COL_TEXT, value=24)
status = ui.Label("กำลังจะเริ่ม", x=344, y=16, color=COL_DIM, value=20)

ui.Panel(x=24, y=56, w=744, h=120, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
ui.Label("สถานะสาย", x=40, y=72, color=COL_DIM, value=16)
link_lbl = ui.Label("ยังไม่ได้ต่อ", x=40, y=104, color=COL_WARN, value=24)

ui.Label("ได้รับแล้ว (ใบ)", x=520, y=72, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=520, y=104, w=144, h=56, color=COL_ACCENT)

ui.Label("คำสั่งล่าสุดที่ได้รับ", x=24, y=192, color=COL_DIM, value=16)
last_lbl = ui.Label("ยังไม่มีคำสั่งเข้ามา", x=24, y=224, color=COL_DIM,
                    value=24)

img = ui.Image("smiley", x=560, y=192, w=64, h=64, color=COL_DIM)

ui.Label("หัวข้อที่ฟังอยู่", x=24, y=272, color=COL_DIM, value=16)
topic_lbl = ui.Label("ยังไม่ได้ subscribe", x=224, y=272, color=COL_DIM,
                     value=20)

note = ui.Label("กำลังเริ่ม", x=24, y=312, color=COL_DIM, value=20)
ui.Label("คำสั่งที่รู้จัก beep / led / say", x=24, y=352, color=COL_DIM,
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
    raise Stop


try:
    # ชื่อทีมคนอื่นทำให้ broker เตะบอร์ดทีมนั้นหลุด (team00 = บอร์ดหน้าห้องของผู้สอน)
    if len(TEAM) != 6 or TEAM[:4] != "team" or not TEAM[4:].isdigit():
        stop_here("ยังไม่ได้ตั้งชื่อทีม", "แก้ TEAM เป็นเลขทีมของคุณก่อน เช่น team03")

    # --- ต่อ WiFi ---
    status.color(COL_WARN)
    status.text("กำลังต่อ WiFi จอจะนิ่งสักครู่")
    ui.poll()
    lcd.print("กำลังต่อ WiFi", WIFI_SSID)

    if not wifi.connect(WIFI_SSID, WIFI_PASS):
        heard = False
        try:
            for net in wifi.scan():
                if net[0] == WIFI_SSID:
                    heard = True
        except OSError:
            # scan() ล้มได้ อย่าตายก่อนบอกเหตุผล
            pass
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
        # connect() โยน OSError เฉพาะตอนชั้น WiFi ของบอร์ดเองยังไม่พร้อม
        stop_here("ต่อ broker ไม่ได้", "ชั้น WiFi ของบอร์ดไม่พร้อม ลองรันใหม่")

    if not linked:
        # เน็ตกันพอร์ต 1883 ชื่อผิด หรือ broker ล่ม connect() คืน False
        stop_here("broker ไม่ตอบ",
                  BROKER + " ไม่ตอบ: เน็ตกันพอร์ต 1883 หรือชื่อผิด")

    # --- ขอฟังหัวข้อ ---
    # subscribe() ต้องมาหลัง connect() และต้องทำซ้ำถ้าสายหลุดแล้วต่อใหม่
    if not mqtt.subscribe(TOPIC_CMD):
        stop_here("subscribe ไม่ผ่าน", "broker ไม่ยอมให้ฟังหัวข้อ " + TOPIC_CMD)

    link_lbl.color(COL_OK)
    link_lbl.text("ต่อแล้ว " + BROKER)
    topic_lbl.color(COL_TEXT)
    topic_lbl.text(TOPIC_CMD)
    status.color(COL_OK)
    status.text("พร้อมรับคำสั่งแล้ว")
    note.color(COL_DIM)
    note.text("กดปุ่มบนหน้าเว็บของทีม แล้วดูจอนี้")
    ui.poll()

    lcd.print("<span class=ok>ฟังหัวข้อ", TOPIC_CMD, "อยู่</span>")
    lcd.print('หน้าเว็บส่ง {"cmd":"beep"} หรือ {"cmd":"led","n":0,"on":1}')

    t0 = time.ticks_ms()
    got = 0
    bad = 0

    while True:
        t_work = time.ticks_ms()
        left_ms = LISTEN_MS - time.ticks_diff(t_work, t0)
        if left_ms <= 0:
            break

        # ถามทุกรอบ ห้ามหลับยาว กล่องรับมีช่องเดียว ใบหลังทับใบแรกเงียบ ๆ
        msg = mqtt.get_message()

        if msg is not None:
            got = got + 1
            seg.text(str(got))

            # topic เป็น str ส่วน payload เป็น bytes
            topic, raw = msg

            try:
                cmd = json.loads(raw.decode())
            except ValueError:
                # คนส่งพิมพ์มั่วได้ ข้อความที่ไม่ใช่ JSON ต้องไม่ทำให้ตาย
                bad = bad + 1
                last_lbl.color(COL_BAD)
                last_lbl.text("ไม่ใช่ JSON")
                img.icon("cross")
                img.color(COL_BAD)
                lcd.print("<span class=error>ได้ของที่ไม่ใช่ JSON:", raw, "</span>")
                cmd = {}

            # JSON อาจไม่ใช่ object เช่น 5 null [] ไม่กันไว้ .get() จะโยน AttributeError
            if not isinstance(cmd, dict):
                bad = bad + 1
                last_lbl.color(COL_BAD)
                last_lbl.text("ไม่ใช่ JSON object")
                img.icon("cross")
                img.color(COL_BAD)
                lcd.print("<span class=error>ได้ JSON ที่ไม่ใช่ object:", raw, "</span>")
                cmd = {}

            # ใช้ .get() เพราะ JSON อาจไม่มีคีย์ cmd
            action = cmd.get("cmd", "")

            if action == "beep":
                # ui.tone รับโน้ต MIDI 0-127 ไม่ใช่ความถี่ รับแบบตำแหน่งเท่านั้น
                ui.tone(69, ui.WAVE_SQUARE, 90, 150)
                last_lbl.color(COL_OK)
                last_lbl.text("beep")
                img.icon("star")
                img.color(COL_OK)
                lcd.print("<span class=ok>ใบที่", got, "-> beep</span>")

            elif action == "led":
                # เลขดวงที่บอร์ดไม่มี ต้องกันเอง ไม่เชื่อคนส่ง
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
                # ป้ายพาไปได้ 126 ไบต์ ไทยตัวละ 3 ไบต์ ตัดให้สั้นก่อนเสมอ
                text = str(cmd.get("text", ""))[:24]
                last_lbl.color(COL_ACCENT)
                last_lbl.text(text if text != "" else "say ที่ไม่มีข้อความ")
                img.icon("flag")
                img.color(COL_ACCENT)
                lcd.print("<span class=info>ใบที่", got, "-> say", text, "</span>")

            elif action != "":
                bad = bad + 1
                last_lbl.color(COL_WARN)
                last_lbl.text("ไม่รู้จักคำสั่ง " + str(action)[:16])
                img.icon("cross")
                img.color(COL_WARN)
                lcd.print("<span class=warn>ไม่รู้จักคำสั่ง", action, "</span>")

        # สายหลุดระหว่างฟังได้ ถามทุกรอบ
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

    for i in range(n_leds):
        gpio.led(i).off()

    status.color(COL_DIM)
    status.text("เลิกฟังแล้ว - ได้รับ " + str(got) + " ใบ ใช้ไม่ได้ " + str(bad))
    ui.poll()

    lcd.console("<span class=muted>------------------------</span>")
    lcd.print("<span class=ok>ได้รับ", got, "ใบ | ใช้ไม่ได้", bad, "ใบ</span>")
    print("ได้รับ", got, "ใบ | ใช้ไม่ได้", bad, "ใบ")
except Stop:
    pass

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# ตั้ง POLL_MS = 3000 แล้วกดปุ่มบนหน้าเว็บสามครั้งรวดในวินาทีเดียว
# นับว่าจอขึ้นกี่ใบ เทียบกับที่ส่งจริงสามใบ
# ใบ้: กล่องรับมีช่องเดียว ใบที่มาตอนช่องไม่ว่างไม่ได้ต่อคิว มันทับของเดิม
