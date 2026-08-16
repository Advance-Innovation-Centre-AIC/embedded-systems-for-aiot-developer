# 03_reconnect_backoff.py - ต่อใหม่แบบถอยห่างขึ้นเรื่อย ๆ ไม่ใช่รัวติดกัน
#
# ไฟล์นี้สอน: ลองต่อใหม่โดยเพิ่มระยะห่างเป็นเท่าตัวทุกครั้งที่ไม่สำเร็จ จนถึงเพดาน
#             แล้วรีเซ็ตกลับค่าเริ่มต้นทันทีที่ต่อกลับได้
# ดูที่จอ   : ป้ายบนซ้ายบอกสถานะสาย เขียวคือ online แดงคือ offline
#             Seg7 กลางคือระยะห่างครั้งถัดไปเป็น ms หลุดจริงจะเห็นมันเดิน 2000 4000 8000
#             แถบล่างคือการนับถอยหลังถึงเวลานัด กราฟล่างคือตารางนัดหมายทั้งชุด
# กับดัก    : ลืมรีเซ็ตระยะห่างเมื่อต่อกลับได้ ครั้งถัดไปที่หลุด มันจะเริ่มจากเพดานเลย
#             ซึ่งแปลว่าหลุดสองวินาทีแล้วรอห้านาทีเพื่อจะรู้ว่ากลับมาแล้ว

import lcd
import mqtt
import time
import ui
import wifi

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
BROKER = "192.168.1.50"
DEVICE_ID = "eva-team03"
BACKOFF_START_MS = 2000     # ครั้งแรกรอสองวินาที
BACKOFF_MAX_MS = 60000      # เพดาน หนึ่งนาที ไม่ปล่อยให้ยาวกว่านี้
SCHEDULE_N = 7              # จำนวนครั้งที่เอามาวาดให้ดูเป็นตัวอย่าง
LOOP_MS = 200

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x00E676, 0xFFA726, 0xFF5252, 0x40C4FF

ui.screen()
time.sleep_ms(200)

ui.Label("คาบ 12 - ถอยห่างเพิ่มขึ้น แล้วรีเซ็ต", x=20, y=10, color=COL_TEXT,
         value=24)
ui.Panel(x=20, y=46, w=650, h=126, color=COL_CARD, min=COL_DIM, max=12, value=1)

ui.Label("สายตอนนี้", x=38, y=54, color=COL_DIM, value=16)
l_link = ui.Label("กำลังต่อ", x=38, y=76, color=COL_WARN, value=28)

ui.Label("ครั้งถัดไปรออีก (ms)", x=250, y=54, color=COL_DIM, value=16)
seg_backoff = ui.Seg7(str(BACKOFF_START_MS), x=250, y=78, w=160, h=44, color=COL_INFO)

ui.Label("ลองมาแล้ว (ครั้ง)", x=470, y=54, color=COL_DIM, value=16)
seg_try = ui.Seg7("0", x=470, y=78, w=110, h=44, color=COL_WARN)

ui.Label("นับถอยหลังถึงเวลานัด", x=38, y=138, color=COL_DIM, value=14)
bar_wait = ui.Bar(x=230, y=140, w=420, h=18, min=0, max=100, value=0)

# ตารางนัดหมายวาดเป็นขั้นบันได ให้เห็นด้วยตาว่าคูณสองแล้วชนเพดานตรงไหน
# แต่ละครั้งวาดซ้ำหลายจุดเพื่อให้ขั้นกว้างพออ่านออกบนจอ 4.3 นิ้ว
ch = ui.Chart(x=20, y=184, w=650, h=138, color=COL_CARD,
              min=0, max=BACKOFF_MAX_MS // 1000 + 10)
s_sched = ch.add_series(COL_WARN)

# สองป้ายในบรรทัดเดียว ให้อยู่ในเพดาน 126 ไบต์ของ ui.Label - ไทยตัวละ 3 ไบต์
l_foot = ui.Label("กราฟคือสิ่งที่จะเกิดตอนสายหลุด", x=20, y=334,
                  color=COL_DIM, value=16)
ui.Label("ไม่ใช่สิ่งที่กำลังเกิด", x=310, y=334, color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 12 - ตารางนัดหมายการลองใหม่</h2>")

# แสดงตารางให้เห็นก่อนว่าตัวเลขชุดนี้แปลว่าอะไร ทั้งในลิ้นชักและบนกราฟ
d, total = BACKOFF_START_MS, 0
for i in range(1, SCHEDULE_N + 1):
    total += d
    lcd.print("ครั้งที่ {} รอ {} ms (สะสม {} วินาที)".format(i, d, total // 1000))
    for _ in range(SCHEDULE_N):
        ch.set_next(s_sched, d // 1000)
    d = min(d * 2, BACKOFF_MAX_MS)
ui.poll()


def go_online():
    # ต่อ WiFi ก่อนเสมอ ถ้าไม่มี IP ก็ไม่มีอะไรให้ MQTT ต่อ
    if not wifi.is_connected():
        if not wifi.connect(WIFI_SSID, WIFI_PASS):
            return False
    return mqtt.connect(BROKER, port=1883, client_id=DEVICE_ID)


def show_link(is_online):
    l_link.text("online" if is_online else "offline")
    l_link.color(COL_OK if is_online else COL_BAD)


online = go_online()
backoff = BACKOFF_START_MS
attempts = 0
t_next = time.ticks_ms()
show_link(online)
lcd.print("<b>สถานะเริ่มต้น</b> {}".format("online" if online else "offline"))

while True:
    now = time.ticks_ms()

    # ตรวจว่ายังต่ออยู่จริงไหม สายที่เคยดีไม่ใช่หลักฐานของตอนนี้
    if online and not mqtt.is_connected():
        online = False
        # ticks_add ไม่ใช่ + ธรรมดา เพราะตัวนับ ticks วนกลับเป็นศูนย์ได้
        # บวกตรง ๆ แล้ววันที่มันวน เวลานัดหมายจะกลายเป็นอดีตหรืออนาคตไกลโพ้น
        t_next = time.ticks_add(now, backoff)
        show_link(False)
        seg_backoff.text(str(backoff))
        lcd.print("<span class=error>หลุด จะลองใหม่ในอีก {} ms</span>".format(backoff))

    if (not online) and time.ticks_diff(now, t_next) >= 0:
        attempts += 1
        seg_try.text(str(attempts))
        online = go_online()
        show_link(online)
        if online:
            # รีเซ็ตทันทีที่กลับมาได้ สามบรรทัดล่างคือหัวใจของทั้งไฟล์
            lcd.print("<span class=ok>กลับมาที่ครั้งที่ {} รีเซ็ต {}</span>".format(
                attempts, BACKOFF_START_MS))
            backoff = BACKOFF_START_MS
            attempts = 0
            seg_try.text("0")
            seg_backoff.text(str(backoff))
        else:
            # คูณสองแต่ไม่เกินเพดาน min() คือสิ่งที่กันไม่ให้ระยะห่างวิ่งไปเป็นชั่วโมง
            backoff = min(backoff * 2, BACKOFF_MAX_MS)
            t_next = time.ticks_add(time.ticks_ms(), backoff)
            seg_backoff.text(str(backoff))
            lcd.print("<span class=warn>ยังไม่ได้ ครั้งที่ {} รออีก {} ms</span>".format(
                attempts, backoff))

    # แถบนับถอยหลัง สายดีอยู่ก็ไม่มีอะไรให้รอ แถบจึงว่าง
    if online:
        bar_wait.value(0)
    else:
        left = time.ticks_diff(t_next, now)
        left = 0 if left < 0 else left
        bar_wait.value(100 - int(left * 100 / backoff))

    ui.poll()
    time.sleep_ms(LOOP_MS)
