# 07_platform_in_one_call.py - บอร์ดจำได้เองว่าจะต่อไปที่ไหน แม้ถอดไฟแล้วเสียบใหม่
#
# อ่านนอกเวลา วันนี้ต่อไม่ได้: tesaiot.connect() ใช้ TLS พอร์ต 8883/8884 เสมอ
# แต่ broker.hivemq.com รับที่ 1883 ไม่เข้ารหัส PLATFORM_BROKER ว่าง = อ่านอย่างเดียว
#
# ของใหม่: tesaiot.config / config_set / connect / is_connected / publish
# ไฟล์นี้สอน: ค่าตั้งอยู่บนแฟลชของบอร์ด ตั้งครั้งเดียว ทุกโปรแกรมอ่านได้
# ดูที่จอ   : หกคีย์จากคลังค่าตั้ง แล้วเวลารอต่อ
# กับดัก    : connect() คืนค่าทันที ต้องวนถาม is_connected() และมีกำหนดเวลาเลิกรอ
#
# ตัวที่ใช้ชิป OPTIGA ปิดอยู่บนบอร์ดนี้ เรียกแล้วรอราวสิบวินาทีแล้วล้ม ไฟล์นี้ไม่แตะ

import json
import lcd
import sensors
import time
import ui
import wifi
import tesaiot

# แก้ให้ตรงกับที่ผู้สอนแจก
WIFI_SSID = "bento-teamXX"            # ชื่อ Hotspot มือถือของทีม (WiFi คณะต้อง login บอร์ดใช้ไม่ได้)
WIFI_PASS = "<รหัส Hotspot ของทีม>"     # อย่างน้อย 8 ตัว
DEVICE_ID = "team03"

# broker TLS ของแพลตฟอร์ม ผู้สอนแจกในคาบ 10 ห้ามใส่ broker.hivemq.com
PLATFORM_BROKER = ""

WAIT_MS = 15000      # รอนานสุดเท่าไร แล้วเลิกรอ
POLL_MS = 250        # ถามซ้ำถี่แค่ไหน
N = 6                # ส่งกี่ใบถ้าต่อติด

# หกคีย์ที่แสดงบนจอ
SHOW = ("device_id", "broker", "port", "tls_mode", "qos", "keepalive")

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF

class Stop(Exception):
    # จบโปรแกรมแบบปกติ (SystemExit ทำให้บอร์ดเริ่มระบบใหม่ และอาจค้างจนต้องถอดสาย)
    pass

ui.screen()
time.sleep_ms(200)

ui.Label("คลังค่าตั้งที่บอร์ดจำเอง", x=20, y=12, color=COL_TEXT, value=24)
status = ui.Label("กำลังอ่านคลังค่าตั้ง", x=20, y=48, color=COL_DIM, value=20)

ui.Panel(x=20, y=80, w=652, h=120, color=COL_CARD, min=COL_DIM, max=12,
         value=1)
ui.Label("tesaiot.config()", x=36, y=88, color=COL_INFO, value=16)

# ห้ามสร้าง Label ด้วยข้อความว่าง LVGL เติมคำว่า "Label" ให้เองแล้วค้าง
cells = []
for i in range(3):
    cells.append(ui.Label("รออ่าน", x=36, y=116 + i * 28, color=COL_TEXT,
                          value=20))
for i in range(3):
    cells.append(ui.Label("รออ่าน", x=360, y=116 + i * 28, color=COL_TEXT,
                          value=20))

ui.Label("สถานะการต่อ", x=20, y=212, color=COL_DIM, value=16)
conn_lbl = ui.Label("ยังไม่ได้สั่งต่อ", x=20, y=236, color=COL_DIM, value=24)

ui.Label("รอมาแล้ว (ms)", x=380, y=212, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=532, y=232, w=152, h=52, color=COL_INFO)

note = ui.Label("กำลังเริ่ม", x=20, y=300, color=COL_DIM, value=20)
warn = ui.Label("ครึ่ง OPTIGA ของโมดูลนี้ปิดอยู่บนบอร์ดชุดนี้", x=20, y=332,
                color=COL_WARN, value=16)
ui.Label("ตั้งครั้งเดียว อยู่บนแฟลช ถอดไฟแล้วยังอยู่", x=20, y=364,
         color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>คลังค่าตั้งที่บอร์ดจำเอง</h2>")

# --- อ่านคลังค่าตั้งที่มีอยู่เดิม ---
# คลังอยู่บนแฟลช อ่านได้ทันทีโดยไม่ต้องมีเน็ต
cfg = tesaiot.config()
lcd.print("คลังค่าตั้งมีทั้งหมด", len(cfg), "คีย์")
for k in cfg:
    lcd.print("  " + k + " =", cfg[k])

try:
    # --- ยังไม่มี broker ของแพลตฟอร์ม: แสดงค่าที่มีอยู่ แล้วจบโดยไม่เขียนอะไร ---
    # port ในคลังไม่ใช่พอร์ตที่ connect() ใช้จริง
    if PLATFORM_BROKER == "":
        for i in range(len(SHOW)):
            key = SHOW[i]
            if key in cfg:
                cells[i].text(key + " = " + str(cfg[key]))
            else:
                cells[i].color(COL_DIM)
                cells[i].text(key + " = ไม่มีคีย์นี้")
        status.color(COL_WARN)
        status.text("อ่านได้ " + str(len(cfg)) + " คีย์ | ไม่ได้เขียนอะไรลงแฟลช")
        conn_lbl.color(COL_DIM)
        conn_lbl.text("ยังไม่สั่งต่อ")
        note.color(COL_WARN)
        note.text("ต่อได้เฉพาะแบบ TLS รอคาบ 10 กับ 11")
        ui.poll()
        lcd.print("<span class=warn>PLATFORM_BROKER ว่าง อ่านอย่างเดียว</span>")
        lcd.print("tesaiot.connect() ใช้ TLS พอร์ต 8883/8884 เสมอ")
        lcd.print("broker.hivemq.com รับบอร์ดที่ 1883 ไม่เข้ารหัส")
        lcd.print("สองทางนี้จึงต่อกันไม่ได้")
        print("อ่านอย่างเดียว | คลังค่าตั้งมี", len(cfg), "คีย์")
        raise Stop

    # --- เขียนทับเฉพาะคีย์ที่ต้องใช้ ---
    # config_set() รับสองอาร์กิวเมนต์ที่ต้องเป็นข้อความทั้งคู่
    # ไม่ตั้ง "port" เพราะ connect() ไม่อ่านคีย์นั้น พอร์ตมาจาก tls_mode
    tesaiot.config_set("device_id", DEVICE_ID)
    tesaiot.config_set("broker", PLATFORM_BROKER)

    # อ่านซ้ำหลังเขียน เพื่อพิสูจน์ว่าค่าลงจริง
    cfg = tesaiot.config()

    for i in range(len(SHOW)):
        key = SHOW[i]
        if key in cfg:
            cells[i].color(COL_TEXT)
            cells[i].text(key + " = " + str(cfg[key]))
        else:
            cells[i].color(COL_DIM)
            cells[i].text(key + " = ไม่มีคีย์นี้")

    status.color(COL_OK)
    status.text("อ่านได้ " + str(len(cfg)) + " คีย์ | เขียนทับไป 2 คีย์")
    ui.poll()
    lcd.print("<span class=ok>เขียนทับ device_id, broker แล้ว</span>")

    # จุดสำคัญ: ถอดไฟแล้วค่ายังอยู่ โปรแกรมถัดไปถาม config() เอาได้เลย
    lcd.console("<span class=muted>ค่าพวกนี้อยู่บนแฟลช ถอดไฟแล้วยังอยู่</span>")

    # --- ต่อ WiFi ก่อน ---
    # tesaiot.connect() ไม่ได้ต่อ WiFi ให้ ต้องมี IP ก่อนเสมอ
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
        conn_lbl.color(COL_BAD)
        conn_lbl.text("ไม่มีเน็ต")
        status.color(COL_BAD)
        status.text("อ่านเขียนคลังค่าตั้งได้ แต่ต่อเน็ตไม่ได้")
        note.color(COL_BAD)
        note.text(why)
        ui.poll()
        lcd.print("<span class=error>" + why + "</span>")
        lcd.print("<span class=ok>ส่วนคลังค่าตั้งทำงานครบแล้ว ไม่ต้องใช้เน็ต</span>")
        print("หยุดที่: " + why)
        raise Stop

    lcd.print("<span class=ok>ได้ IP", wifi.ip(), "</span>")

    # --- สั่งต่อ แล้ววนถามเอง ---
    # connect() คืนค่าทันทีโดยไม่รอให้ต่อเสร็จ ค่าที่ได้แปลว่า "รับคำสั่งแล้ว"
    # if tesaiot.connect(): แล้วส่งทันที จะได้ OSError เพราะสายยังไม่ขึ้น
    conn_lbl.color(COL_WARN)
    conn_lbl.text("สั่งต่อแล้ว กำลังรอ")
    note.color(COL_DIM)
    note.text("รอสูงสุด " + str(WAIT_MS // 1000) + " วินาที แล้วเลิกรอ")
    ui.poll()
    lcd.print("สั่ง tesaiot.connect() แล้ว กำลังรอสาย")

    tesaiot.connect()

    t0 = time.ticks_ms()
    linked = False

    while True:
        waited = time.ticks_diff(time.ticks_ms(), t0)
        seg.text(str(waited))

        if tesaiot.is_connected():
            linked = True
            break

        # กำหนดเวลาเลิกรอ แยกโปรแกรมที่ล้มอย่างสุภาพ ออกจากโปรแกรมที่ค้างจนต้องถอดไฟ
        if waited >= WAIT_MS:
            break

        ui.poll()
        time.sleep_ms(POLL_MS)

    if not linked:
        seg.color(COL_BAD)
        conn_lbl.color(COL_BAD)
        conn_lbl.text("ต่อไม่ติดใน " + str(WAIT_MS // 1000) + " วินาที")
        status.color(COL_BAD)
        status.text("มีเน็ตแล้ว แต่ไปไม่ถึงแพลตฟอร์มที่ตั้งไว้")
        note.color(COL_BAD)
        note.text("ตรวจชื่อ broker ใบรับรอง และพอร์ต TLS")
        ui.poll()
        lcd.print("<span class=error>รอครบ", WAIT_MS, "ms แล้วยังไม่ติด</span>")
        lcd.print("<span class=muted>ค่าที่ตั้งไว้ยังอยู่ ไม่ต้องตั้งใหม่รอบหน้า</span>")
        print("ต่อไม่ติด | broker =", cfg.get("broker", "?"))
        raise Stop

    seg.color(COL_OK)
    conn_lbl.color(COL_OK)
    conn_lbl.text("ต่อติดใน " + str(time.ticks_diff(time.ticks_ms(), t0)) + " ms")
    status.color(COL_OK)
    status.text("พร้อมส่งแล้ว")
    ui.poll()
    lcd.print("<span class=ok>ต่อติดแล้ว</span>")

    # --- ส่งของจริง ---
    # publish() ไม่ต้องใส่หัวข้อ เฟิร์มแวร์ประกอบให้จาก device_id ในคลังค่าตั้ง
    sent = 0
    for i in range(1, N + 1):
        knob = -1
        try:
            s = sensors.snapshot()
            if "pot" in s:
                knob = int(s["pot"]["percent"])
        except OSError:
            knob = -1

        body = json.dumps({"n": i, "knob": knob,
                           "uptime_s": time.ticks_ms() // 1000})

        if not tesaiot.is_connected():
            conn_lbl.color(COL_BAD)
            conn_lbl.text("สายหลุดที่ใบที่ " + str(i))
            ui.poll()
            lcd.print("<span class=error>สายหลุดที่ใบที่", i, "</span>")
            break

        if tesaiot.publish(body):
            sent = i
            note.color(COL_OK)
            note.text("ส่งแล้ว " + str(sent) + " ใบ | knob = " + str(knob))
            lcd.print("ใบที่", i, "->", body)
        else:
            note.color(COL_WARN)
            note.text("ใบที่ " + str(i) + " ถูกปฏิเสธ")
            lcd.print("<span class=warn>ใบที่", i, "ถูกปฏิเสธ</span>")

        ui.poll()
        time.sleep_ms(1500)

    warn.color(COL_DIM)
    status.color(COL_OK)
    status.text("จบแล้ว - ส่งได้ " + str(sent) + " ใบ จาก " + str(N))
    ui.poll()

    lcd.console("<span class=muted>------------------------</span>")
    lcd.print("<span class=ok>ส่งได้", sent, "ใบ จาก", N, "</span>")
    print("ส่งได้", sent, "ใบ | คลังค่าตั้งมี", len(cfg), "คีย์")
except Stop:
    pass

# ----- ตาคุณ แก้แล้วรันใหม่ (คาบ 10 เป็นต้นไป) -----
# วันนี้: รันแบบ PLATFORM_BROKER ว่าง จด port กับ tls_mode ในคลัง
# แล้วตอบว่าทำไม port ที่เห็นอาจไม่ใช่พอร์ตที่ connect() ใช้จริง
# วันที่ได้ชื่อ broker: ใส่ PLATFORM_BROKER รันหนึ่งรอบ ถอดสาย USB เสียบกลับ
# แล้วรัน print(tesaiot.config()["broker"]) ใน Playground ได้ชื่อที่ตั้งไว้ หรือค่าโรงงาน
# ใบ้: tesaiot.config_reset() คืนค่าโรงงาน
