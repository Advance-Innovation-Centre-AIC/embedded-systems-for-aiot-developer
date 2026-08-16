# s10_mqtt_telemetry.py - ส่ง telemetry ขึ้น broker และรับคำสั่งกลับ
# วิธีรัน: 1) แก้ค่าเจ็ดบรรทัดบนหัวไฟล์ให้เป็นของทีม (NN คือหมายเลขทีมเรา)
#          2) เปิด MQTT Explorer ต่อ broker เดียวกัน แล้ว subscribe topic ของทีม
#          3) กด Program to Device ดูค่าไหลขึ้นทุก 5 วินาที แล้วส่ง {"cmd":"toggle"} กลับมา
#
# คาบนี้ข้อมูลเดินสองทางเป็นครั้งแรก: เราส่งขึ้นทุก 5 วินาที และรับคำสั่งลงมาได้ตลอดเวลา
# ปุ่มบนจอคนอื่นสั่งไฟบนบอร์ดเราได้ - นั่นคือความหมายจริง ๆ ของคำว่า IoT
#
# ดูที่จอ: ซ้ายบนคือไฟสี่ดวงบอกสถานะลิงก์ทั้งเส้นทาง กลางบนคือค่าที่กำลังจะถูกส่ง
#         พร้อมพิสัยของมัน ขวาบนคือจำนวนใบที่ส่งไปแล้วกับคำสั่งที่รับกลับมา
#         ล่างซ้ายคือรายการสามใบล่าสุด ล่างขวาคือปุ่มสั่งเริ่ม/หยุดส่ง
# กับดัก : ไฟ "ค่าค้าง" มีไว้เพราะค่าที่อ่านไม่ได้ยังค้างเลขเดิมอยู่บนจอ ไม่ได้หายไป
#         จอที่โชว์เลขเก่าโดยไม่บอกว่ามันเก่า อันตรายกว่าจอที่ไม่โชว์อะไรเลย

import wifi
import mqtt
import sensors
import gpio
import lcd
import json
import time
import ui

# ทุกอย่างที่ต้องแก้อยู่บนสุดที่เดียว ถ้าเปลี่ยนไปใช้ broker ของห้องเรียน
# แก้ BROKER กับ MQTT_PASS สองบรรทัด ที่เหลือไม่ต้องแตะ
WIFI_SSID = "AIoT-Class"
WIFI_PASSWORD = "<รหัสผ่านของห้องเรียน>"
BROKER = "192.168.1.50"                # IP ของเครื่องที่รัน TESAIoT CE ในแลน (ไม่ใช่ localhost)
DEVICE_ID = "eva-team03"                # ต้องตรงกับ device_id ที่ขึ้นทะเบียนบนแพลตฟอร์ม             # <= 31 ตัวอักษร (เกินแล้ว broker ปฏิเสธเงียบ ๆ)
MQTT_PASS = "bento"                    # broker ฝึกไม่ตรวจ แต่ของห้องเรียนจะตรวจ
# สอง topic นี้เป็นโครง ราก/ตัวตน/ชนิด ตามที่ examples/s10/01_topic_design.py วางไว้
# ตัวตนอยู่กลางชื่อ ทุกทีมจึงไม่ชนกันโดยไม่ต้องนัดกัน และฝั่งรับกรองได้ตั้งแต่ชั้น broker
# ไฟล์นั้นยังแยกให้ชัดว่า wildcard (+ กับ #) ใช้ได้เฉพาะตอน subscribe เอาไปใส่ publish เมื่อไร
# จะได้ topic ที่มีตัวอักษรพวกนั้นอยู่จริง ๆ แล้วไม่มีใครได้รับสักคน
TOPIC_PUB = "device/eva-team03/telemetry"
TOPIC_CMD = "device/eva-team03/commands"

# --- ท่าที่ 1: ต่อเน็ตให้ได้ก่อน แล้วค่อยแนะนำตัวกับ broker ---
# ลำดับนี้ห้ามสลับ: MQTT วิ่งบน TCP ซึ่งวิ่งบน IP ถ้ายังไม่มี IP ก็ไม่มีอะไรให้ต่อ
#
# บอร์ดนี้ไม่มี sensors.init() ให้เรียก เซนเซอร์อยู่บนบัสที่คอร์จอ (CM55) ถือคนเดียว
# ฝั่ง Python ขอค่าที่คอร์จออ่านเก็บไว้ให้ จึงเรียกอ่านได้เลย เรียก init() จะได้ OSError
# แต่หลังรีเซ็ต คอร์จอเริ่มตอบเรื่องเซนเซอร์ราว 13 วินาที อุ่นเครื่องหนึ่งครั้งตรงนี้
# ให้การรอไปเกิดก่อนต่อเน็ต ไม่ใช่ไปโผล่ตอนถึงรอบส่งข้อมูลรอบแรก
try:
    sensors.bmi270.motion()
except OSError:
    print("คอร์จอยังไม่ตอบรอบแรก จะลองใหม่ตอนส่ง")

lcd.clear()
lcd.console("<h2>MQTT Telemetry - คาบ 10</h2>")

# --- แผงเฝ้าลิงก์: สร้างก่อนต่อเน็ต เพราะการต่อคือสิ่งที่เราอยากเฝ้าดู ---
# ถ้าสร้างจอหลังต่อเสร็จ ช่วงที่น่าดูที่สุดของโปรแกรมจะผ่านไปโดยไม่มีใครเห็น
COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD, COL_OK, COL_WARN, COL_RUN = 0x142240, 0x00E676, 0xFFC83D, 0x4FC3F7
SEND_MS = 5000
STALE_MS = 12000       # เกินสองรอบส่งแล้วยังอ่านค่าไม่ได้ ถือว่าเลขบนจอเป็นของเก่า

ui.screen()
time.sleep_ms(200)
ui.Label("MQTT Telemetry - คาบ 10", x=16, y=6, color=COL_TEXT, value=20)

# การ์ด 1: ไฟสี่ดวงไล่ตามเส้นทางจริงของข้อมูล WiFi ก่อน แล้วค่อย MQTT
ui.Panel(x=16, y=34, w=250, h=150, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("สถานะการเชื่อมต่อ", x=30, y=40, color=COL_DIM, value=14)
led_wifi = ui.Led(x=34, y=62, w=28, h=28, color=COL_OK, value=0)
ui.Label("WiFi", x=72, y=66, color=COL_DIM, value=16)
led_mqtt = ui.Led(x=34, y=96, w=28, h=28, color=COL_OK, value=0)
ui.Label("MQTT", x=72, y=100, color=COL_DIM, value=16)
# สีเหลืองใช้กับเรื่องเดียวในหน้านี้คือค่าที่เชื่อไม่ได้ ไม่เอาไปใช้กับอย่างอื่นอีก
led_stale = ui.Led(x=34, y=130, w=28, h=28, color=COL_WARN, value=0)
ui.Label("ค่าค้าง", x=72, y=134, color=COL_DIM, value=16)
led_remote = ui.Led(x=150, y=130, w=28, h=28, color=COL_RUN, value=0)
ui.Label("ไฟสั่งไกล", x=188, y=134, color=COL_DIM, value=16)

# การ์ด 2: ค่าที่กำลังจะถูกส่ง พร้อมพิสัยของมัน
# ตัวเลข 62 ลอย ๆ ไม่บอกว่าสูงไหม ตัวเลข 62 ที่มีไม้บรรทัด 0-100 อยู่ใต้มันบอกทันที
# และค่าที่เห็นบนการ์ดนี้คือค่าเดียวกับที่ออกไปทาง MQTT ไม่ใช่ค่าที่อ่านคนละรอบ
ui.Panel(x=278, y=34, w=250, h=150, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("โพเทนชิโอมิเตอร์ (ที่ส่งจริง)", x=292, y=40, color=COL_DIM, value=14)
lbl_pot = ui.Label("- %", x=292, y=62, color=COL_TEXT, value=24)
bar_pot = ui.Bar(x=292, y=104, w=220, h=12, color=COL_OK, min=0, max=100, value=0)
sc_pot = ui.Scale(x=292, y=118, w=220, h=44, color=COL_TEXT, min=0, max=100)
sc_pot.ticks(11, 5)

# การ์ด 3: จำนวนใบที่ส่ง กับคำสั่งที่เดินทางกลับมา
ui.Panel(x=540, y=34, w=236, h=150, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("ส่งไปแล้ว (ใบ)", x=554, y=40, color=COL_DIM, value=14)
seg_sent = ui.Seg7("0", x=554, y=60, w=200, h=48, color=COL_TEXT)
ui.Label("คำสั่งที่รับล่าสุด", x=554, y=116, color=COL_DIM, value=14)
lbl_cmd = ui.Label("ยังไม่มี", x=554, y=138, color=COL_DIM, value=18)

# การ์ด 4: สามใบล่าสุด - ui.List ไม่ใช่ ui.Label เรียงกัน เพราะรายการที่ต้องล้างแล้ว
# เขียนใหม่ทุกห้าวินาที ถ้าทำด้วย Label ต้องนับพิกเซลใหม่ทุกครั้งที่ข้อความยาวไม่เท่าเดิม
# แถวหนึ่งของ List สูงราว 51 พิกเซล สามแถวจึงขอความสูงราว 165 ไม่ใช่เดาเอา
ui.Panel(x=16, y=192, w=512, h=198, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("สามใบล่าสุดที่ส่งออกไป", x=30, y=198, color=COL_DIM, value=14)
lst_sent = ui.List(x=30, y=220, w=484, h=166)

# การ์ด 5: ปุ่มสั่งเริ่มกับหยุด แยกกันคนละปุ่ม ไม่ใช่ปุ่มเดียวสลับ
# ที่นี่ไม่ต้องมีกล่องยืนยัน เพราะคำสั่งนี้ไม่ได้ทำให้ของจริงขยับ และย้อนกลับได้
# ด้วยปุ่มที่อยู่ข้าง ๆ ทันที - กล่องยืนยันมีไว้สำหรับคำสั่งที่ถอยกลับไม่ได้
ui.Panel(x=540, y=192, w=236, h=198, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("คำสั่งการส่งข้อมูล", x=554, y=198, color=COL_DIM, value=14)
btn_go = ui.Button("เริ่มส่ง", x=554, y=222, w=104, h=44, color=0x1B5E20, value=16)
btn_hold = ui.Button("หยุดส่ง", x=666, y=222, w=104, h=44, color=0x37474F, value=16)
lbl_state = ui.Label("กำลังส่ง", x=554, y=286, color=COL_DIM, value=16)
ui.poll()

wifi.connect(WIFI_SSID, WIFI_PASSWORD)
lcd.print("WiFi:", wifi.ip())
led_wifi.value(1 if wifi.is_connected() else 0)

# broker เป็น positional ที่เหลือเป็น kwargs - ชื่อคือ keepalive ไม่ใช่ keep_alive
# keepalive=60 แปลว่า ถ้าเงียบเกิน 60 วินาที broker มีสิทธิ์ตัดเราทิ้ง
ok = mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID,
                  username=DEVICE_ID, password=MQTT_PASS, keepalive=60)
lcd.print("<span class=ok>MQTT ต่อแล้ว</span>" if ok else "MQTT ต่อไม่ได้")
led_mqtt.value(1 if ok else 0)
ui.poll()

# --- ท่าที่ 2: อ่านเซนเซอร์ -> ประกอบ JSON -> publish ---
def publish_telemetry():
    # round() ไม่ได้ทำเพื่อความสวย แต่เพื่อลดขนาด payload ทศนิยม 14 ตำแหน่ง
    # ไม่ได้บอกอะไรเพิ่มเลย นอกจากกินแบนด์วิดท์และหน่วยความจำของทั้งสองฝั่ง
    # examples/s10/02_payload_shape.py คิดเลขนี้ให้ดูเป็นแท่ง บอร์ดตัวเดียวส่งทุก 5 วินาที
    # คือ 17,280 ใบต่อวัน ทุกไบต์ที่เกินจำเป็นจึงถูกคูณด้วยหมื่นทุกวัน
    # และไฟล์นั้นยังกำหนดคำว่า payload ที่ดีไว้สี่ข้อ เล็ก คงที่ มีหน่วย และมีตัวตนของอุปกรณ์
    #
    # อ่านพลาดหนึ่งรอบไม่ควรทำให้ทั้งโปรแกรมตาย ข้ามรอบนี้แล้วไปส่งรอบหน้า
    # สองบรรทัดที่อ่านค่า = ถามคอร์จอสองรอบ จึงต้องอยู่ใน try เดียวกันทั้งคู่
    try:
        ax, ay, az, gx, gy, gz = sensors.bmi270.motion()
        data = {"ax": round(ax, 2), "ay": round(ay, 2), "az": round(az, 2),
                "pot": round(sensors.pot.percent(), 1)}
    except OSError:
        return None

    # publish() รับตามตำแหน่งเท่านั้น และไม่มีอาร์กิวเมนต์ retain ให้ใช้
    # ห่อใน {"data": ...} เพราะฝั่งแพลตฟอร์มคาดหวังรูปแบบนี้ตั้งแต่คาบ 11 เป็นต้นไป
    # ที่ไฟล์นี้ไม่นับ True จาก publish() เป็น "ส่งสำเร็จ" มีเหตุผลอยู่ที่
    # examples/s10/06_sent_is_not_delivered.py - True แปลว่าส่งต่อให้ชั้นเครือข่ายแล้วเท่านั้น
    # ไม่ได้แปลว่า broker ได้รับ หลักฐานเดียวที่เชื่อได้คือของเดินกลับมาให้เราเห็น
    mqtt.publish(TOPIC_PUB, json.dumps({"data": data}))
    return data

# --- ท่าที่ 3: subscribe แล้ว poll ถี่ ๆ ในลูปเดียวกับที่ publish ---
# subscribe ครั้งเดียวก่อนเข้าลูป ไม่ต้องเรียกซ้ำทุกรอบ
mqtt.subscribe(TOPIC_CMD)

led_on = False
sent = 0
sending = True                  # ปุ่มบนจอเป็นคนเปลี่ยนค่านี้ ไม่ใช่โค้ดที่ไหนอีก
recent = []                     # สามใบล่าสุด เก็บไว้เท่าที่จอแสดงได้ ไม่เก็บทั้งประวัติ
stale_shown = False             # สถานะไฟค่าค้างที่เขียนลงจอไปแล้ว
t_last = time.ticks_ms()
t_good = time.ticks_ms()        # ครั้งสุดท้ายที่อ่านเซนเซอร์ได้จริง
t_ui = time.ticks_ms()          # นาฬิกาของจอ เดินคนละจังหวะกับนาฬิกาของการส่ง

while True:
    # นับเวลาด้วย ticks_diff แทนการ sleep 5 วินาที เพราะถ้า sleep ยาว
    # เราจะไม่ได้ถาม get_message() เลยตลอดห้าวินาทีนั้น
    # นาฬิกาสองเรือนในลูปเดียว (เรือนของการส่ง กับเรือนของการฟัง) คือทั้งเรื่องของ
    # examples/s10/05_send_every_5s_still_listen.py ซึ่งรันได้โดยไม่ต้องต่อ broker เลย
    # ถ้ายังไม่เห็นภาพว่าทำไมโครงนี้ถึงต้องเป็นแบบนี้ ให้รันไฟล์นั้นดูก่อน มันถูกกว่าการเดา
    if sending and time.ticks_diff(time.ticks_ms(), t_last) >= SEND_MS:
        d = publish_telemetry()
        if d is not None:
            sent += 1
            t_good = time.ticks_ms()
            lcd.print("ส่งครั้งที่", sent, "| pot", d.get("pot", "-"))

            # จอถูกเขียนใหม่ทุกห้าวินาที ซึ่งช้ากว่าเพดานหนึ่งครั้งต่อวินาทีอยู่มาก
            # ตัวเลขจึงอยู่นิ่งพอให้คนอ่านทัน และอยู่ตำแหน่งเดิมทุกครั้ง
            pot = d.get("pot", 0)
            lbl_pot.text(str(pot) + " %")
            bar_pot.value(int(pot))
            seg_sent.text(str(sent))

            # รายการล้างแล้วเขียนใหม่ทั้งชุด ง่ายกว่าและถูกกว่าการเลื่อนทีละแถว
            recent.append("ใบ " + str(sent) + " pot " + str(pot))
            if len(recent) > 3:
                recent.pop(0)
            lst_sent.clear_items()
            for line in recent:
                lst_sent.add_item(line, ui.ICON_OK)
        else:
            lcd.print("<span class=muted>อ่านเซนเซอร์ไม่ได้ ข้ามรอบนี้</span>")
        t_last = time.ticks_ms()

    # ค่าที่อ่านไม่ได้ไม่ได้ทำให้เลขบนจอหายไป มันค้างเลขเดิมไว้เฉย ๆ
    # จอที่โชว์เลขเก่าโดยไม่บอกว่ามันเก่า คือจอที่หลอกคนอ่าน ไฟดวงนี้จึงมีไว้พูดแทน
    #
    # เขียนเฉพาะตอน "เปลี่ยน" ไม่ใช่เขียนทุกรอบ และเหตุผลไม่ใช่ความสวยงาม
    # คิวคำสั่งของจอมีก้นถัง พอมันเต็ม เฟิร์มแวร์จะ "ทิ้ง" คำสั่งเปลี่ยนข้อความ
    # ก่อนเป็นอย่างแรก (ipc_ui.c ระบุ SET_TEXT ว่าเป็นคำสั่งที่ทิ้งได้)
    # โปรแกรมที่ยิงคำสั่งจอสิบครั้งต่อวินาที จึงเห็นตัวเลขบนจอค้างเป็นบางครั้ง
    # โดยไม่มี error สักบรรทัด - อาการนี้แก้ที่ "ยิงให้น้อยลง" ไม่ใช่ยิงซ้ำให้มากขึ้น
    stale = time.ticks_diff(time.ticks_ms(), t_good) >= STALE_MS
    if stale != stale_shown:
        stale_shown = stale
        led_stale.value(1 if stale else 0)

    # get_message() มีบัฟเฟอร์ช่องเดียว ข้อความใหม่ทับของเก่าเงียบ ๆ โดยไม่มีคำเตือน
    # ถ้าคนส่งคำสั่งมาสามข้อความรวดเดียว เราจะเห็นแค่ข้อความสุดท้าย นี่ไม่ใช่บั๊ก
    # แต่คือข้อจำกัดที่ต้องออกแบบรอบ ๆ มัน - วิธีเดียวคือถามให้ถี่พอ
    msg = mqtt.get_message()

    if msg is not None:
        try:
            # payload เป็น bytes เสมอ ต้อง decode ก่อน และขาเข้าจำกัด 255 ไบต์
            cmd = json.loads(msg[1].decode())
        except ValueError:
            # ข้อความที่ไม่ใช่ JSON ต้องไม่ทำให้ทั้งโปรแกรมตาย - คนส่งมั่วได้เสมอ
            cmd = {}
        if cmd.get("cmd") == "toggle":
            led_on = not led_on                  # จำสถานะเอง ขาตอบระดับ ไม่ตอบความตั้งใจ
            gpio.led(1).value(1 if led_on else 0)
            # ไฟบนจอสะท้อนหลอดจริง คนที่ยืนอยู่หน้าจอจึงเห็นผลของคำสั่งที่มาจากอีกห้อง
            led_remote.value(1 if led_on else 0)
        lbl_cmd.text(str(cmd.get("cmd", "อ่านไม่ออก")))

    # --- ปุ่มบนจอ: เริ่มส่งกับหยุดส่ง แยกกันคนละปุ่ม ---
    # ถามนิ้วห้าครั้งต่อวินาทีก็พอ นิ้วคนไม่ได้มาถึงเร็วกว่านั้น และทุกครั้งที่ถาม
    # คือการยิง IPC ข้ามคอร์หนึ่งใบ ซึ่งไปเบียดคิวเดียวกับคำสั่งวาดจอ
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
