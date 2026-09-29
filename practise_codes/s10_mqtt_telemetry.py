# s10_mqtt_telemetry.py - ส่ง telemetry ขึ้น broker และรับคำสั่งกลับ (ฉบับฝึกเติมโค้ด)
# วิธีรัน: 1) แก้ค่าเจ็ดบรรทัดบนหัวไฟล์ให้เป็นของทีม (NN คือหมายเลขทีมเรา)
#          2) เติมท่าที่ 1 แล้วรันจนจอขึ้นว่าต่อแล้ว ห้ามข้ามไปท่าอื่นก่อน
#          3) เปิด MQTT Explorer ค้างไว้ แล้วค่อยเติมท่า 2 และ 3 ทีละจุด
# ก่อนแก้ TOPIC_* อ่าน examples/s10/01_topic_design.py กับ 02_payload_shape.py
# ดูที่จอ: ไฟสี่ดวงบอกสถานะลิงก์ ค่าที่จะส่ง สามใบล่าสุด และคำสั่งที่รับ
#         แผงเขียนมาให้แล้ว ช่องว่างหกจุดอยู่ที่ตรรกะ
# กับดัก : เติมไม่ครบ ไฟ MQTT จะไม่ติด และรายการล่าสุดจะว่าง

import wifi
import mqtt
import sensors
import gpio
import lcd
import json
import time
import ui

WIFI_SSID = "AIoT-Class"
WIFI_PASSWORD = "<รหัสผ่านของห้องเรียน>"
BROKER = "192.168.1.50"                # IP ของเครื่องที่รัน TESAIoT CE ในแลน (ไม่ใช่ localhost)
DEVICE_ID = "team03"                # ตรงกับ device_id บนแพลตฟอร์ม <= 31 ตัวอักษร ไม่ซ้ำกับทีมอื่น
MQTT_PASS = "bento"                    # broker ฝึกไม่ตรวจ แต่ของห้องเรียนจะตรวจ
TOPIC_PUB = "device/team03/telemetry"
TOPIC_CMD = "device/team03/commands"


def led_named(*names, fallback=0):
    """หา LED จากชื่อในตารางเฟิร์มแวร์ - เลขดัชนีต่างกันตามบอร์ด ชื่อไม่ต่าง

    Eva Kit : LED1=แดง LED2=เขียว RGB_RED=ฟ้า (ชื่อ RGB_RED บน Eva คือดวงสีฟ้า)
    Dev Kit : LED1 LED2 อยู่บน SoM มองไม่เห็นบนบอร์ดประกอบ  RGB_RED RGB_BLUE RGB_GREEN
    ส่งชื่อเรียงให้ตัวแรกเป็นของ Dev Kit ตัวถัดไปเป็นของ Eva"""
    table = gpio.board_info()["led_names"]
    for n in names:
        if n in table:
            return gpio.led(table.index(n))
    return gpio.led(fallback)


# หลอดจริงที่คนอีกห้องสั่งได้
lamp = led_named("RGB_GREEN", "LED2")

# --- ท่าที่ 1: ต่อเน็ตให้ได้ก่อน แล้วค่อยแนะนำตัวกับ broker ---
# ไม่ต้องเรียก sensors.init() (บน Eva ได้ OSError) แต่คอร์จอของ Eva เริ่มตอบราว 13 วินาทีหลังรีเซ็ต
# จึงอุ่นเครื่องตรงนี้ก่อนต่อเน็ต
try:
    sensors.bmi270.motion()
except OSError:
    print("อ่านเซนเซอร์รอบแรกยังไม่ได้ - ลองใหม่ตอนส่ง")

lcd.clear()
lcd.console("<h2>MQTT Telemetry - คาบ 10</h2>")

# --- แผงเฝ้าลิงก์: สร้างก่อนต่อเน็ต ---
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD, COL_OK, COL_WARN, COL_RUN = 0x171B22, 0x30A46C, 0xF5A623, 0x4A9EFF
SEND_MS = 5000
STALE_MS = 12000       # เกินสองรอบส่งยังอ่านไม่ได้ = เลขบนจอเป็นของเก่า

ui.screen()
time.sleep_ms(200)
# ไฟสี่ดวงไล่ตามเส้นทางข้อมูล: WiFi -> MQTT -> ค่าค้าง -> ไฟที่คนอื่นสั่ง
ui.Label("MQTT Telemetry - คาบ 10", x=24, y=36, color=COL_TEXT, value=20)
lbl_mqtt = ui.Label("MQTT: ออฟไลน์", x=24, y=72, color=COL_DIM, value=16)

led_wifi = ui.Led(x=328, y=16, w=48, h=48, color=COL_OK, value=0)
ui.Label("WiFi", x=328, y=72, color=COL_DIM, value=16)
led_mqtt = ui.Led(x=440, y=16, w=48, h=48, color=COL_OK, value=0)
ui.Label("MQTT", x=440, y=72, color=COL_DIM, value=16)
led_stale = ui.Led(x=552, y=16, w=48, h=48, color=COL_WARN, value=0)
ui.Label("ค่าค้าง", x=552, y=72, color=COL_DIM, value=16)
led_remote = ui.Led(x=664, y=16, w=48, h=48, color=COL_RUN, value=0)
ui.Label("ไฟสั่งไกล", x=664, y=72, color=COL_DIM, value=16)

# การ์ด 1: ค่าที่จะส่งกับจำนวนใบที่ส่ง
# สร้างการ์ดก่อนป้ายบนมัน ไม่งั้นการ์ดทับป้ายหายเงียบ ๆ
ui.Panel(x=24, y=112, w=232, h=280, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ลูกบิดที่ส่งจริง", x=40, y=124, color=COL_DIM, value=16)
lbl_pot = ui.Label("- %", x=40, y=160, color=COL_TEXT, value=24)
bar_pot = ui.Bar(x=40, y=204, w=200, h=12, color=0x4A9EFF, min=0, max=100, value=0)
sc_pot = ui.Scale(x=40, y=224, w=200, h=44, color=COL_TEXT, min=0, max=100)
sc_pot.ticks(11, 5)
ui.Label("ส่งไปแล้ว (ใบ)", x=40, y=288, color=COL_DIM, value=16)
seg_sent = ui.Seg7("0", x=40, y=324, w=200, h=48, color=COL_TEXT)

# การ์ด 2: สามใบล่าสุดกับคำสั่งที่รับ
ui.Panel(x=272, y=112, w=240, h=280, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("สามใบล่าสุด", x=288, y=124, color=COL_DIM, value=16)
lst_sent = ui.List(x=288, y=160, w=208, h=168)
ui.Label("รับล่าสุด", x=288, y=344, color=COL_DIM, value=16)
lbl_cmd = ui.Label("ยังไม่มี", x=400, y=344, color=COL_DIM, value=20)

# การ์ด 3: ปุ่มเริ่ม/หยุด ย้อนได้ทันทีจึงไม่ต้องยืนยัน
# มุมขวาล่างตั้งแต่ x=690 y=340 เป็นของปุ่ม Console
ui.Panel(x=528, y=112, w=240, h=280, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("คำสั่งการส่งข้อมูล", x=544, y=124, color=COL_DIM, value=16)
lbl_state = ui.Label("กำลังส่ง", x=544, y=160, color=COL_DIM, value=20)
btn_go = ui.Button("เริ่มส่ง", x=544, y=240, w=88, h=88, color=0x30A46C, value=20)
btn_hold = ui.Button("หยุดส่ง", x=664, y=240, w=88, h=88, color=0x3A4150, value=20)
ui.poll()


def show_link(ok):
    led_mqtt.value(1 if ok else 0)
    lbl_mqtt.text("MQTT: เชื่อมต่อแล้ว" if ok else "MQTT: ออฟไลน์")

wifi.connect(WIFI_SSID, WIFI_PASSWORD)
lcd.print("WiFi:", wifi.ip())
led_wifi.value(1 if wifi.is_connected() else 0)

# try ครอบถึงจบลูป: หยุดกลางทางเมื่อไร finally ยังบอกลา broker
try:
    ok = False                             # ให้ไฟล์รันได้ก่อนเติมครบ
    # เติม: ok = mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID, username=DEVICE_ID, password=MQTT_PASS, keepalive=60)
    # ชื่ออาร์กิวเมนต์ผิดตัวเดียวก็ TypeError (username= ไม่ใช่ user=)
    # ลำดับ: WiFi ได้ IP ก่อน แล้ว TCP จึงต่อได้ แล้ว MQTT จึงแนะนำตัวได้
    # ได้ False ให้ไล่ย้อนทีละขั้น อย่าเดา
    pass

    lcd.print("<span class=ok>MQTT ต่อแล้ว</span>" if ok else "MQTT ต่อไม่ได้")
    show_link(ok)
    ui.poll()

    # --- ท่าที่ 2: อ่านเซนเซอร์ -> ประกอบ JSON -> publish ---
    def publish_telemetry():
        data = {}

        # อ่านพลาดหนึ่งรอบ โปรแกรมไม่ควรตาย ข้ามไปส่งรอบหน้า
        # ทุกบรรทัดที่อ่านค่าต้องอยู่ใน try เดียวกัน
        try:
            ax, ay, az, gx, gy, gz = sensors.bmi270.motion()

            # เติม: data = {"ax": round(ax, 2), "ay": round(ay, 2), "az": round(az, 2), "pot": round(sensors.pot.percent(), 1)}
            # round ให้ payload เล็ก อย่าส่งตัวเลขเป็นสตริง dashboard จะวาดกราฟไม่ได้
            pass
        except OSError:
            return data

        # publish() รับตามตำแหน่งเท่านั้น ไม่มีอาร์กิวเมนต์ retain
        # เติม: mqtt.publish(TOPIC_PUB, json.dumps(data))
        # ส่งแบน ห้ามห่อใต้ {"data": ...} แพลตฟอร์มห่อให้เอง
        # คืน True ไม่ได้แปลว่าถึง broker แล้ว
        pass

        return data

    # --- ท่าที่ 3: subscribe แล้ว poll ถี่ ๆ ในลูปเดียวกับที่ publish ---
    # เติม: mqtt.subscribe(TOPIC_CMD)
    # เรียกครั้งเดียวก่อนเข้าลูปก็พอ
    pass

    led_on = False
    sent = 0
    msg = None
    sending = True
    recent = []
    stale_shown = False             # สถานะไฟค่าค้างที่เขียนลงจอแล้ว
    t_last = time.ticks_ms()
    t_good = time.ticks_ms()        # ครั้งสุดท้ายที่อ่านได้จริง
    t_ui = time.ticks_ms()

    while True:
        if sending and time.ticks_diff(time.ticks_ms(), t_last) >= SEND_MS:
            d = publish_telemetry()
            sent += 1
            lcd.print("ส่งครั้งที่", sent, "| pot", d.get("pot", "-"))
            t_last = time.ticks_ms()

            pot = d.get("pot", 0)
            if pot != "-":
                t_good = time.ticks_ms()
                lbl_pot.text(str(pot) + " %")
                bar_pot.value(int(pot))
            seg_sent.text(str(sent))
            # ข้อความในแถวต้องสั้น ยาวเกิน LVGL จะเลื่อนไปมาจนตัวแรกหาย
            recent.append("ใบ " + str(sent) + " : " + str(pot))
            if len(recent) > 3:
                recent.pop(0)
            lst_sent.clear_items()
            for line in recent:
                lst_sent.add_item(line, ui.ICON_OK)

        # ค่าที่อ่านไม่ได้ไม่ทำให้เลขบนจอหาย มันค้างเลขเดิมไว้
        # เขียนจอเฉพาะตอนเปลี่ยน คิวจอเต็มแล้วคำสั่งเปลี่ยนข้อความถูกทิ้งเงียบ ๆ
        stale = time.ticks_diff(time.ticks_ms(), t_good) >= STALE_MS
        if stale != stale_shown:
            stale_shown = stale
            led_stale.value(1 if stale else 0)

        # get_message() มีบัฟเฟอร์ช่องเดียว ข้อความใหม่ทับของเก่าเงียบ ๆ
        # เติม: msg = mqtt.get_message()
        # ลูปต้องเดินเร็ว (100 ms) ใส่ time.sleep(5) แทน คำสั่งที่มาระหว่างนั้นจะหาย
        pass

        if msg is not None:
            try:
                cmd = json.loads(msg[1].decode())    # payload เป็น bytes ขาเข้าจำกัด 255 ไบต์
            except ValueError:
                cmd = {}
            if cmd.get("cmd") == "toggle":
                led_on = not led_on                  # จำสถานะเอง ขาตอบระดับ ไม่ตอบความตั้งใจ
                # เติม: lamp.value(1 if led_on else 0)
                pass

                led_remote.value(1 if led_on else 0)
            lbl_cmd.text(str(cmd.get("cmd", "อ่านไม่ออก")))

        # --- ปุ่มบนจอ: เริ่มส่งกับหยุดส่ง แยกกันคนละปุ่ม (เขียนมาให้แล้ว) ---
        # ถามปุ่มแค่ห้าครั้งต่อวินาที เพราะทุกครั้งคือ IPC ข้ามคอร์
        if time.ticks_diff(time.ticks_ms(), t_ui) >= 200:
            t_ui = time.ticks_ms()
            for ev in ui.poll():
                if ev["type"] != "clicked":
                    continue
                if ev["handle"] == btn_go.id():
                    sending = True
                    lbl_state.text("กำลังส่ง")
                elif ev["handle"] == btn_hold.id():
                    sending = False
                    lbl_state.text("หยุดส่งชั่วคราว")

        time.sleep_ms(100)
finally:
    try:
        mqtt.disconnect()              # ปิดทุกครั้ง แม้ถูกหยุดกลางทาง
        show_link(False)
    except Exception:
        pass
