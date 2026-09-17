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
DEVICE_ID = "team03"
BACKOFF_START_MS = 2000     # ครั้งแรกรอสองวินาที
BACKOFF_MAX_MS = 60000      # เพดาน หนึ่งนาที ไม่ปล่อยให้ยาวกว่านี้
SCHEDULE_N = 7              # จำนวนครั้งที่เอามาวาดให้ดูเป็นตัวอย่าง
LOOP_MS = 200

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

ui.screen()
time.sleep_ms(200)

# ผังจอ: การ์ดสถานะสายหนึ่งใบ กราฟตารางนัดหมายหนึ่งช่อง แล้วสองบรรทัดล่าง
# หัวเรื่องย่อจาก "ถอยห่างเพิ่มขึ้น แล้วรีเซ็ต" เพราะของเดิมยาว 94 ไบต์
# ซึ่งเฉียดเพดาน 95 ไบต์ที่ตัวสร้าง ui.Label ตัดทิ้งเงียบ ๆ
ui.Label("คาบ 12 - ถอยห่างแล้วรีเซ็ต", x=24, y=8, color=COL_TEXT, value=28)
ui.Panel(x=24, y=56, w=744, h=168, color=COL_CARD, min=COL_CARD, max=12,
         value=1)

ui.Label("สายตอนนี้", x=40, y=72, color=COL_DIM, value=20)
l_link = ui.Label("กำลังต่อ", x=40, y=104, color=COL_WARN, value=28)

ui.Label("ครั้งถัดไปรออีก (ms)", x=264, y=72, color=COL_DIM, value=20)
seg_backoff = ui.Seg7(str(BACKOFF_START_MS), x=264, y=104, w=192, h=56,
                      color=COL_ACCENT)

# จำนวนครั้งที่ลองคือตัวนับ ไม่ใช่สถานะ จึงไม่ทาสีเฝ้าระวัง - ของเดิมเป็นสีส้ม
# ซึ่งทำให้ตาอ่านว่า "มีเรื่อง" ตลอดเวลา แม้ตอนที่สายดีและตัวเลขเป็นศูนย์
ui.Label("ลองมาแล้ว (ครั้ง)", x=528, y=72, color=COL_DIM, value=20)
seg_try = ui.Seg7("0", x=528, y=104, w=120, h=56, color=COL_TEXT)

ui.Label("นับถอยหลังถึงเวลานัด", x=40, y=172, color=COL_DIM, value=20)
bar_wait = ui.Bar(x=288, y=168, w=456, h=32, min=0, max=100, value=0)

# ตารางนัดหมายวาดเป็นขั้นบันได ให้เห็นด้วยตาว่าคูณสองแล้วชนเพดานตรงไหน
# แต่ละครั้งวาดซ้ำหลายจุดเพื่อให้ขั้นกว้างพออ่านออกบนจอ 4.3 นิ้ว
# เส้นเดียวบนกราฟใช้สีเน้นเสมอ ไม่ใช่สีส้ม - ตารางนัดหมายไม่ใช่การเตือน
ch = ui.Chart(x=24, y=240, w=744, h=88, color=COL_CARD,
              min=0, max=BACKOFF_MAX_MS // 1000 + 10)
s_sched = ch.add_series(COL_ACCENT)

# สองป้ายในบรรทัดเดียว ให้อยู่ในเพดาน 95 ไบต์ของตัวสร้าง - ไทยตัวละ 3 ไบต์
l_foot = ui.Label("กราฟคือสิ่งที่จะเกิดตอนสายหลุด", x=24, y=336,
                  color=COL_DIM, value=20)
ui.Label("ไม่ใช่สิ่งที่กำลังเกิด", x=384, y=336, color=COL_DIM, value=20)
ui.Label("กราฟ = ระยะห่างที่นัดไว้ (วินาที)", x=24, y=368, color=COL_DIM,
         value=20)
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
