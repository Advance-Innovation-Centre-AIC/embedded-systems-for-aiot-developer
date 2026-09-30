# 05_value_leaves_the_board.py - ค่าที่วัดได้บนโต๊ะนี้ ไปโผล่บนเครื่องคนอื่น
#
# ของใหม่: mqtt.connect / publish / is_connected
# ไฟล์นี้สอน: บันไดสามขั้น WiFi ได้ IP ก่อน MQTT จึงแนะนำตัวได้ ห้ามสลับ
#             ขั้นที่ยังไม่ผ่านต้องเห็นบนจอ
# ดูที่จอ   : ป้ายสามขั้นเทาเป็นเขียวทีละขั้น ไม่ผ่านเป็นแดงพร้อมเหตุผลแล้วจบ
#             ผ่านครบแล้วเลขใบที่ส่งเดินขึ้น
# ดูอีกฝั่ง : examples/web/my_first_reader.html
# กับดัก    : ชื่ออาร์กิวเมนต์คือ username= ไม่ใช่ user= ผิดได้ TypeError
#             สายหลุดแล้ว publish() โยน OSError ไม่ได้คืน False
#
# พอร์ต 1883 ไม่เข้ารหัส ใครก็อ่านและส่งเข้าหัวข้อเราได้ ห้ามส่งของลับ

import json
import lcd
import sensors
import time
import ui
import wifi
import mqtt

# แก้สามบรรทัดนี้ตามที่ผู้สอนแจก
WIFI_SSID = "bento-teamXX"            # ชื่อ Hotspot มือถือของทีม (WiFi คณะต้อง login บอร์ดใช้ไม่ได้)
WIFI_PASS = "<รหัส Hotspot ของทีม>"     # อย่างน้อย 8 ตัว
TEAM = "teamXX"                   # team01 ถึง team19 ไม่แก้ไม่รัน

# ไม่ต้องแก้ ทั้งห้องใช้ชุดเดียวกัน
BROKER = "broker.hivemq.com"      # สำรอง: "test.mosquitto.org"
ROOT = "bento-aiot"
DEVICE_ID = "bento-aiot-" + TEAM
TOPIC = ROOT + "/" + TEAM + "/telemetry"

# ส่ง retain ไม่ได้ หน้าเว็บที่เปิดทีหลังเห็นแค่ใบถัดไป จึงส่งซ้ำนานสองนาที
N = 60               # ส่งกี่ใบแล้วหยุด
GAP_MS = 2000        # เว้นระหว่างใบกี่ ms

# จานสีของหลักสูตร
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

class Stop(Exception):
    # จบโปรแกรมแบบปกติ (SystemExit ทำให้บอร์ดเริ่มระบบใหม่ และอาจค้างจนต้องถอดสาย)
    pass

ui.screen()
time.sleep_ms(200)

ui.Label("ส่งค่าออกจากบอร์ด", x=24, y=8, color=COL_TEXT, value=24)
ui.Label("พอร์ต 1883 ไม่เข้ารหัส ห้ามส่งของลับ", x=384, y=16,
         color=COL_WARN, value=20)

ui.Panel(x=24, y=56, w=744, h=144, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
st_wifi = ui.Label("1) WiFi      ยังไม่ถึงคิว", x=40, y=72, color=COL_DIM,
                   value=20)
st_broker = ui.Label("2) broker    ยังไม่ถึงคิว", x=40, y=112, color=COL_DIM,
                     value=20)
st_pub = ui.Label("3) publish   ยังไม่ถึงคิว", x=40, y=152, color=COL_DIM,
                  value=16)

ui.Panel(x=24, y=216, w=744, h=120, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
ui.Label("ส่งไปแล้ว (ใบ)", x=40, y=232, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=40, y=264, w=160, h=56, color=COL_ACCENT)

ui.Label("ความคืบหน้า", x=248, y=232, color=COL_DIM, value=20)
bar = ui.Bar(x=248, y=264, w=328, h=24, min=0, max=N, value=0)
bar.color(COL_ACCENT)
payload_lbl = ui.Label("ยังไม่ได้ประกอบ payload", x=248, y=296, color=COL_DIM,
                       value=20)

note = ui.Label("กำลังเริ่ม", x=24, y=352, color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>ส่งค่าออกจากบอร์ด</h2>")


def stop_here(label, screen_msg, log_msg):
    """ปิดงานอย่างสุภาพ บอกบนจอว่าไปไม่ถึงไหน แล้วจบ ไม่ค้างรอ

    โปรแกรมที่ล้มเหลวแล้วเงียบ คือโปรแกรมที่คนหน้างานต้องเดาเอง
    ทุกทางออกของไฟล์นี้จึงเขียนบนจอไว้เสมอว่าติดที่ขั้นไหน
    """
    label.color(COL_BAD)
    label.text(screen_msg)
    note.color(COL_BAD)
    note.text(log_msg)
    ui.poll()
    lcd.print("<span class=error>" + log_msg + "</span>")
    print("หยุดที่:", log_msg)
    raise Stop


try:
    # ใช้ชื่อทีมคนอื่น broker จะเตะบอร์ดทีมนั้นหลุด จึงไม่รันจนกว่าจะแก้ teamXX
    if len(TEAM) != 6 or TEAM[:4] != "team" or not TEAM[4:].isdigit() or TEAM == "team00":
        stop_here(st_wifi, "1) WiFi      ยังไม่ได้ตั้งชื่อทีม",
                  "แก้ TEAM เป็นเลขทีมของคุณก่อน เช่น team03")

    # --- ขั้นที่ 1: WiFi ต้องได้ IP ก่อน ---
    # ป้ายต้องขึ้นก่อน connect() เพราะจอนิ่งระหว่างต่อ (ได้ถึงราว 85 วินาที)
    st_wifi.color(COL_WARN)
    st_wifi.text("1) WiFi      กำลังต่อ " + WIFI_SSID)
    note.color(COL_WARN)
    note.text("ครั้งแรกอาจรอนาน จอจะนิ่ง อย่ากดรีเซ็ต")
    ui.poll()
    lcd.print("1) กำลังต่อ WiFi", WIFI_SSID)

    t0 = time.ticks_ms()
    ok = wifi.connect(WIFI_SSID, WIFI_PASS)
    took = time.ticks_diff(time.ticks_ms(), t0)
    lcd.print("connect() ใช้เวลา", took, "ms คืนค่า", ok)

    if not ok:
        # scan() แยก "ไม่ได้ยินวง" ออกจาก "รหัสผิด" แก้คนละทาง
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
        stop_here(st_wifi, "1) WiFi      ต่อไม่ติด", why)

    ip = wifi.ip()
    if ip == "0.0.0.0":
        # รอ DHCP อีกหน่อย "0.0.0.0" เป็นสตริงไม่ว่าง if wifi.ip(): จึงผ่านเสมอ
        for _ in range(15):
            ui.poll()
            time.sleep_ms(200)
            ip = wifi.ip()
            if ip != "0.0.0.0":
                break

    if ip == "0.0.0.0":
        stop_here(st_wifi, "1) WiFi      ลิงก์ขึ้นแต่ไม่มี IP",
                  "ลิงก์ขึ้นแต่ DHCP ไม่ให้เลข ส่งอะไรออกไม่ได้")

    st_wifi.color(COL_OK)
    st_wifi.text("1) WiFi      IP " + ip)
    ui.poll()
    lcd.print("<span class=ok>ได้ IP", ip, "</span>")

    # --- ขั้นที่ 2: แนะนำตัวกับ broker ---
    # client_id ซ้ำ broker เตะตัวเก่าออก สองบอร์ดผลัดกันเตะโดยไม่มีคำเตือน
    # จึงเติม bento-aiot- กันชนคนแปลกหน้า (เฟิร์มแวร์ตัดเงียบ ๆ ที่ 31 ตัว)
    st_broker.color(COL_WARN)
    st_broker.text("2) broker    กำลังต่อ " + BROKER)
    note.text("ถ้าเน็ตของห้องกันพอร์ต 1883 ขั้นนี้จะไม่ผ่าน")
    ui.poll()
    lcd.print("2) กำลังต่อ broker", BROKER, "พอร์ต 1883")

    # broker บังคับตัวเดียว ที่เหลือมีค่าตั้งต้น ชื่อคือ username= ไม่ใช่ user=
    # keepalive=60: เงียบเกิน 60 วินาที broker ตัดเราทิ้งได้
    try:
        linked = mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID, keepalive=60)
    except OSError:
        # connect() โยน OSError เฉพาะตอนชั้น WiFi ของบอร์ดเองยังไม่พร้อม
        stop_here(st_broker, "2) broker    ต่อไม่ได้",
                  "ชั้น WiFi ของบอร์ดไม่พร้อม ลองรันใหม่")

    if not linked:
        # ทางที่เจอบ่อยสุด: เน็ตกันพอร์ต 1883 ชื่อผิด หรือ broker ล่ม
        stop_here(st_broker, "2) broker    ต่อไม่ได้",
                  BROKER + " ไม่ตอบ: เน็ตกันพอร์ต 1883 หรือชื่อผิด")

    st_broker.color(COL_OK)
    st_broker.text("2) broker    ต่อแล้ว " + BROKER)
    st_pub.color(COL_WARN)
    st_pub.text("3) publish   กำลังส่ง")
    note.color(COL_DIM)
    note.text("หัวข้อ " + TOPIC)
    ui.poll()
    lcd.print("<span class=ok>ต่อ broker แล้ว</span>")
    lcd.print("หัวข้อที่ส่ง:", TOPIC)

    # --- ขั้นที่ 3: ส่งของจริง ---
    # ค่าจริงจากบอร์ด ไม่ใช่เลขสุ่ม ถาม in ก่อนหยิบ รอบที่ไม่มีคีย์จะไม่ตายด้วย KeyError
    sent = 0
    for i in range(1, N + 1):
        knob = -1        # -1 = อ่านลูกบิดไม่ได้
        az = -99.0       # -99 = อ่านค่าเอียงไม่ได้
        try:
            s = sensors.snapshot()
            if "pot" in s:
                knob = int(s["pot"]["percent"])
            if "bmi270" in s:
                # m/s^2 วางราบราว 9.8
                az = round(s["bmi270"]["az"], 2)
        except OSError:
            # อ่านไม่ได้รอบนี้ ไม่ใช่เหตุให้หยุดส่ง
            pass

        # หน้าเว็บทั้งห้องอ่านคีย์ชุดนี้ ต้องมี id กับ n เสมอ
        payload = {"id": TEAM,
                   "n": i,
                   "knob": knob,
                   "az": az,
                   "uptime_s": time.ticks_ms() // 1000}
        body = json.dumps(payload)

        # สายหลุดแล้ว publish() ไม่คืน False มันโยน OSError จึงต้องดักทั้งสองทาง
        try:
            ok = mqtt.publish(TOPIC, body)
        except OSError:
            st_pub.color(COL_BAD)
            st_pub.text("3) publish   สายหลุดที่ใบที่ " + str(i))
            note.color(COL_BAD)
            note.text("ส่งไปได้ " + str(sent) + " ใบก่อนสายหลุด")
            ui.poll()
            lcd.print("<span class=error>สายหลุดที่ใบที่", i, "</span>")
            break

        if not ok:
            st_pub.color(COL_BAD)
            st_pub.text("3) publish   ถูกปฏิเสธที่ใบที่ " + str(i))
            ui.poll()
            lcd.print("<span class=error>ใบที่", i, "ถูกปฏิเสธ</span>")
            break

        sent = i
        seg.text(str(sent))            # Seg7 รับข้อความ ไม่ใช่ตัวเลข
        bar.value(sent)
        payload_lbl.color(COL_TEXT)
        payload_lbl.text("ใบที่ " + str(i) + " knob=" + str(knob) + " az=" + str(az))
        ui.poll()
        lcd.print("ใบที่", i, "->", body)
        print("ส่ง:", body)

        time.sleep_ms(GAP_MS)

    # --- สรุป ---
    # is_connected() = ตอนนี้ยังต่ออยู่ไหม ค่าจาก connect() = ตอนนั้นต่อสำเร็จ
    still = mqtt.is_connected()
    if sent == N:
        st_pub.color(COL_OK)
        st_pub.text("3) publish   ส่งครบ " + str(N) + " ใบ")
    note.color(COL_OK if still else COL_WARN)
    note.text("ส่งได้ " + str(sent) + " ใบ | is_connected() = " + str(still))
    ui.poll()

    lcd.console("<span class=muted>------------------------</span>")
    lcd.print("<span class=ok>ส่งได้", sent, "ใบ จาก", N, "</span>")
    print("ส่งได้", sent, "ใบ | ยังต่ออยู่:", still)
except Stop:
    pass

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# เปิดหน้าเว็บอ่านค่า ต่อท้ายลิงก์ด้วยเลขทีม เช่น
# https://advance-innovation-centre-aic.github.io/embedded-systems-for-aiot-developer/examples/web/my_first_reader.html?team=team05
# ระหว่างส่ง หมุนลูกบิดหรือเอียงบอร์ด ดู knob หรือ az บนเว็บ และ n ของใบแรกที่เห็น
# แล้วตกลงกับทีมข้าง ๆ ตั้ง TEAM ชนกัน รันพร้อมกันสองบอร์ด
# ใบ้: client_id ซ้ำกันไม่ได้ ดูว่าใครถูกเตะออก และฝั่งเราเห็นอะไร
