# 08_class_race.py - เกมกดเร็วทั้งห้อง ผู้สอนกดเริ่มรอบ บอร์ดทุกทีมจับเวลาเอง
#
# Why : แข่งกดเร็วผ่านเน็ตดูเหมือนง่าย ใครข้อความถึงจอก่อนคนนั้นชนะ แต่ข้อความแต่ละใบ
#         เดินทางไม่เท่ากัน ทีมที่นั่งใกล้จุดกระจายสัญญาณอาจชนะทั้งที่มือช้ากว่า
#         การแข่งที่ยุติธรรมจึงต้องจับเวลาที่ปลายทาง บนบอร์ดที่นิ้วกด ไม่ใช่ที่ broker
# What: ลูปเดียวทำสี่งานสลับกัน หยิบคำสั่งจากกล่องรับ อ่านปุ่มแบบกันเด้ง
#         นับเวลาของรอบด้วย ticks_ms และส่งผลเป็น event กับสถานะเป็น telemetry
#         รอบหนึ่งมีสามช่วง รอสุ่ม (ห้ามกด) -> ไฟติด (กดเลย) -> จบรอบ
#
# ดูที่จอ: ตัวเลขใหญ่คือเวลาตอบสนองเป็น ms ของรอบล่าสุด ข้างกันคือเลขรอบกับเวลาที่ดีที่สุด
#          ป้ายกลางจอบอกช่วงของรอบด้วยสี ส้ม = รอ อย่าเพิ่งกด  เขียว = กดเลย  แดง = ออกตัวก่อน
#          ผู้สอนฉาย examples/web/class_game.html ผลของทุกทีมขึ้นตารางบนจอหน้าห้อง
# กับดัก : กันเด้ง 40 ms ทำให้เรา "เชื่อ" ว่ากดช้ากว่าที่นิ้วกดจริง 40 ms
#          ถ้าจับเวลาตอนที่เชื่อ ทุกทีมจะช้าเกินจริงเท่ากันหมด เราจึงจดเวลาตั้งแต่ขาเริ่มเปลี่ยน
#          แล้วค่อยใช้เวลานั้นเมื่อกันเด้งยืนยันแล้วว่าเป็นการกดจริง
#          และเราฟังสองหัวข้อ (ของทีมกับของทั้งห้อง) แต่กล่องรับยังมีช่องเดียวเหมือนไฟล์ 06
#
# broker.hivemq.com เป็น broker สาธารณะ ใครก็ส่งคำสั่งเข้าหัวข้อ all ได้ ไม่ใช่แค่ผู้สอน
# คำสั่งทุกใบจึงต้องผ่านการตรวจก่อนใช้ เหมือนไฟล์ 06 ทุกประการ
# บน Emulator โมดูล mqtt เป็นของจำลอง ไม่ได้ยินคำสั่งจากหน้าห้อง
# แต่กดปุ่มตอนว่างเพื่อเล่นรอบซ้อมได้ทั้งบนบอร์ดและบน Emulator
#
# สถานะการทดสอบ (2026-09-24): ตรวจชื่อ API กับซอร์สเฟิร์มแวร์และ compile ผ่านแล้ว
# หน้าเว็บทดสอบกับบอร์ดจำลองที่เขียนด้วย node บน broker.hivemq.com
# ยังไม่มีใครรันไฟล์นี้บนบอร์ดจริง และยังไม่ได้ลองจากเน็ตของมหาวิทยาลัย

import gpio
import json
import lcd
import time
import ui
import wifi
import mqtt

# แก้สี่บรรทัดนี้ให้ตรงกับที่ผู้สอนแจกหน้าห้อง
WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
BROKER = "broker.hivemq.com"      # สำรอง: "test.mosquitto.org"
TEAM = "teamXX"                   # ผู้สอนแจก team01 ถึง team12 ต้องแก้ ไม่งั้นโปรแกรมไม่ยอมรัน

# บรรทัดเหล่านี้ห้ามแก้ ทั้งห้องต้องใช้ชื่อชุดเดียวกัน หน้าเว็บถึงหาเราเจอ
ROOT = "bento-aiot"
DEVICE_ID = "bento-aiot-" + TEAM  # 17 ตัวอักษร เฟิร์มแวร์ตัด client_id ที่ 31
TOPIC_CMD = ROOT + "/" + TEAM + "/cmd"
TOPIC_ALL = ROOT + "/all/cmd"     # หัวข้อที่ผู้สอนใช้เริ่มรอบให้ทั้งห้องพร้อมกัน
TOPIC_EVENT = ROOT + "/" + TEAM + "/event"
TOPIC_STATE = ROOT + "/" + TEAM + "/telemetry"

DEBOUNCE_MS = 40     # ค่าเดียวกับไฟล์ 05 และ 07
POLL_MS = 5          # ความละเอียดของนาฬิกาเกม เวลาที่ได้คลาดได้ไม่เกินราวค่านี้
STATE_MS = 2000      # ส่ง telemetry ทุกกี่ ms (เฉพาะตอนไม่ได้จับเวลา)
RUN_MS = 2700000     # เปิดเกมนานเท่าไร (45 นาที พอทั้งหกรอบกับตัวนับรวมของทั้งห้อง)

# กติกาของรอบ หน้าเว็บ class_game.html ใช้ตัวเลขชุดเดียวกัน ถ้าแก้ต้องแก้ทั้งสองที่
WAIT_MIN_MS = 1500   # หลังได้คำสั่งเริ่ม บอร์ดรออย่างน้อยเท่านี้ก่อนไฟติด
WAIT_SPAN_MS = 2500  # แล้วสุ่มรอเพิ่มอีก 0 ถึงค่านี้ แต่ละบอร์ดสุ่มไม่เท่ากัน
GO_TIMEOUT_MS = 3000 # ไฟติดแล้วไม่กดภายในเท่านี้ ถือว่าพลาดรอบนี้
HUMAN_MIN_MS = 100   # เร็วกว่านี้ไม่ใช่ปฏิกิริยา คือเดาล่วงหน้า (เกณฑ์ออกตัวของกรีฑาโลก)

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

N = gpio.num_leds()
btn = gpio.button(0)

for i in range(N):
    gpio.led(i).off()

ui.screen()
time.sleep_ms(200)

# ผังจอ: หัวเรื่องกับบรรทัดสถานะแถวบน การ์ดตัวเลขหนึ่งใบ แล้วป้ายใหญ่บอกช่วงของรอบ
# บรรทัดสถานะยาวได้ราว 420 px จึงเริ่มที่ x=344 หลังหัวเรื่องขนาด 24 จบพอดี
ui.Label("เกมกดเร็วทั้งห้อง", x=24, y=8, color=COL_TEXT, value=24)
status = ui.Label("กำลังจะเริ่ม", x=344, y=16, color=COL_DIM, value=20)

# การ์ดบน: ตัวเลขใหญ่ซ้ายเป็น ms สูงสุดสี่หลัก (3000) กว้าง 144 พอ
# สองบรรทัดทางขวาเริ่มที่ x=224 เหมือนไฟล์ 07
ui.Panel(x=24, y=56, w=744, h=120, color=COL_CARD, min=COL_CARD, max=12,
         value=1)
ui.Label("เวลาตอบสนอง (ms)", x=40, y=72, color=COL_DIM, value=16)
seg = ui.Seg7(text="-", x=40, y=104, w=144, h=56, color=COL_DIM)
ui.Label("รอบ | ดีที่สุด", x=224, y=72, color=COL_DIM, value=16)
round_lbl = ui.Label("ยังไม่มีรอบ", x=224, y=104, color=COL_TEXT, value=20)
count_lbl = ui.Label("กด 0 | ส่ง 0 | ส่งไม่ผ่าน 0 | คำสั่งเสีย 0", x=224, y=140,
                     color=COL_DIM, value=16)

# ป้ายใหญ่คือสัญญาณหลักของเกม คนกดมองป้ายนี้กับหลอดไฟ ไม่ได้มองตัวเลข
# ขนาด 28 ยาวสุดคือ "ออกตัวก่อนไฟติด" 15 ตัว ราว 420 px ไม่ชนไอคอนที่ x=640
big = ui.Label("รอผู้สอนเริ่มรอบ", x=24, y=200, color=COL_DIM, value=28)
img = ui.Image("smiley", x=640, y=192, w=64, h=64, color=COL_DIM)

topic_lbl = ui.Label("ยังไม่ได้ต่อ broker", x=24, y=272, color=COL_DIM,
                     value=16)
note = ui.Label("กดปุ่มตอนว่าง = รอบซ้อม", x=24, y=312, color=COL_DIM,
                value=20)
ui.Label("ไฟติดแล้วค่อยกด " + btn.name(), x=24, y=352, color=COL_DIM,
         value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>เกมกดเร็วทั้งห้อง</h2>")


def stop_here(screen_msg, log_msg):
    """ปิดงานอย่างสุภาพ บอกบนจอว่าไปไม่ถึงไหน แล้วจบ ไม่ค้างรอ"""
    status.color(COL_BAD)
    status.text(screen_msg)
    note.color(COL_BAD)
    note.text(log_msg)
    ui.poll()
    lcd.print("<span class=error>" + log_msg + "</span>")
    print("หยุดที่:", log_msg)
    raise SystemExit


# --- ก่อนบันไดสามขั้น: ชื่อทีมต้องเป็นของเราจริง ---
# client_id สร้างจาก TEAM ถ้าใช้ชื่อทีมคนอื่น broker จะเตะบอร์ดของทีมนั้นหลุด
# และเฟิร์มแวร์นี้ไม่ต่อใหม่ให้เอง ทีมนั้นจะหลุดไปทั้งคาบโดยไม่รู้ว่าเพราะอะไร
# team00 เป็นของบอร์ดผู้สอน ห้ามใช้
if len(TEAM) != 6 or TEAM[:4] != "team" or not TEAM[4:].isdigit() or TEAM == "team00":
    stop_here("ยังไม่ได้ตั้งชื่อทีม", "แก้ TEAM เป็นเลขทีมของคุณก่อน เช่น team03")

# --- บันไดสามขั้นของคาบ 2: WiFi -> IP -> broker ---
# ป้าย "กำลังต่อ" ต้องขึ้นก่อน connect() เพราะมันบล็อกได้นานถึงราว 85 วินาที
status.color(COL_WARN)
status.text("กำลังต่อ WiFi จอจะนิ่งสักครู่")
ui.poll()
lcd.print("1) กำลังต่อ WiFi", WIFI_SSID)

if not wifi.connect(WIFI_SSID, WIFI_PASS):
    heard = False
    try:
        for net in wifi.scan():
            if net[0] == WIFI_SSID:
                heard = True
    except OSError:
        pass              # สแกนไม่ได้ก็ยังต้องบอกเหตุบนจอ ไม่ใช่ตายด้วย traceback
    # ข้อความพวกนี้ไปขึ้นป้ายผ่าน .text() ซึ่งรับได้ 126 ไบต์ ไทยตัวละ 3 จึงต้องสั้น
    if heard:
        why = "ได้ยินวง " + WIFI_SSID + " แต่ต่อไม่ผ่าน ตรวจรหัสผ่าน"
    else:
        why = "ไม่ได้ยินวง " + WIFI_SSID + " ตรวจชื่อวง"
    stop_here("ต่อ WiFi ไม่ติด", why)

# "0.0.0.0" เป็นสตริงที่ไม่ว่าง เขียน if wifi.ip(): จึงผ่านทั้งที่ยังไม่มีที่อยู่
ip = wifi.ip()
for _ in range(15):
    if ip != "0.0.0.0":
        break
    ui.poll()
    time.sleep_ms(200)
    ip = wifi.ip()
if ip == "0.0.0.0":
    stop_here("ไม่มีเลข IP", "ได้ลิงก์แต่ DHCP ไม่ให้เลข IP")

status.text("IP " + ip + " กำลังต่อ broker")
ui.poll()
lcd.print("2) ได้ IP", ip, "กำลังต่อ", BROKER)

try:
    linked = mqtt.connect(BROKER, 1883, client_id=DEVICE_ID)
except OSError:
    stop_here("broker ไม่ตอบ", "ต่อ " + BROKER + " ไม่ได้ ลองตัวสำรอง")

if not linked:
    stop_here("broker ปฏิเสธ", "ตรวจชื่อ broker กับพอร์ต 1883")

# ขอฟังสองหัวข้อ ของทีมเรา (หน้าเว็บของทีมสั่งไฟ) กับของทั้งห้อง (ผู้สอนเริ่มรอบ)
# ทั้งสองหัวข้อเทลงกล่องรับช่องเดียวกัน กล่องไม่ได้แยกตามหัวข้อ
for t in (TOPIC_CMD, TOPIC_ALL):
    if not mqtt.subscribe(t):
        stop_here("subscribe ไม่ผ่าน", "broker ไม่ยอมให้ฟัง " + t)

status.color(COL_OK)
status.text("ต่อแล้ว รอผู้สอนเริ่มรอบ")
topic_lbl.color(COL_TEXT)
topic_lbl.text("ฟัง " + TOPIC_CMD + " กับ " + TOPIC_ALL)
ui.poll()
lcd.print("<span class=ok>3) ต่อแล้ว ฟัง", TOPIC_CMD, "และ", TOPIC_ALL, "</span>")

# --- ตัวแปรของเกม ---
IDLE, WAIT, GO = 0, 1, 2          # ช่วงของรอบ ตัวเลขแทนชื่อ อ่านง่ายกว่าเขียน 0 1 2 ลอย ๆ
phase = IDLE
rnd = 0              # เลขรอบที่กำลังเล่น 0 = รอบซ้อม (ไม่ส่งผลขึ้น broker)
last_rnd = -1        # เลขรอบล่าสุดที่รับแล้ว คำสั่งเดิมที่ส่งซ้ำจะไม่เริ่มรอบใหม่
t_arm = 0            # เวลาที่รับคำสั่งเริ่ม
wait_ms = 0          # รอบนี้บอร์ดเราสุ่มได้ต้องรอนานเท่าไร
t_go = 0             # เวลาที่ไฟติด จุดเริ่มนับเวลาตอบสนอง
best = None          # เวลาที่ดีที่สุดของวันนี้ (เฉพาะรอบจริง)
last_ms = None
rounds = 0
presses = 0
n_msg = 0
n_fail = 0
n_bad = 0
lost = False


def show_counts():
    count_lbl.text("กด " + str(presses) + " | ส่ง " + str(n_msg) + " | ส่งไม่ผ่าน " +
                   str(n_fail) + " | คำสั่งเสีย " + str(n_bad))


def show_round():
    b = "-" if best is None else str(best)
    r = "ซ้อม" if rnd == 0 else str(rnd)
    round_lbl.text("รอบ " + r + " | ดีที่สุด " + b + " ms")


def send(topic, payload):
    """ส่งหนึ่งใบ คืน False ถ้าสายหลุด แบบเดียวกับไฟล์ 07

    publish() ตอบ False เมื่อชั้นเครือข่ายไม่รับใบนี้ ใบนั้นหายแต่สายยังอยู่ นับไว้ให้เห็นบนจอ
    ผลรอบที่หายแบบนี้จะไม่ขึ้นตารางหน้าห้อง ทีมต้องรู้จากจอตัวเองว่าเพราะอะไร
    แต่ถ้าสายหลุดไปแล้ว มันไม่คืน False มันโยน OSError
    """
    global n_msg, n_fail
    n_msg = n_msg + 1
    payload["id"] = TEAM
    payload["n"] = n_msg
    try:
        ok = mqtt.publish(topic, json.dumps(payload))
    except OSError:
        return False
    if not ok:
        n_fail = n_fail + 1
        show_counts()
    return True


def lights(on):
    """ไฟทุกดวงติดพร้อมกัน สัญญาณยิ่งใหญ่ยิ่งเห็นเร็ว Eva มี 3 ดวง Dev Kit มี 5"""
    for i in range(N):
        if on:
            gpio.led(i).on()
        else:
            gpio.led(i).off()


def arm(r):
    """เริ่มรอบ r สุ่มเวลารอของบอร์ดนี้เอง แล้วเข้าช่วงห้ามกด"""
    global phase, rnd, t_arm, wait_ms
    rnd = r
    t_arm = time.ticks_ms()
    # สุ่มจากหลักไมโครวินาทีของนาฬิกา ณ วินาทีที่ข้อความมาถึง ซึ่งแต่ละบอร์ดไม่เท่ากัน
    # ไม่ใช้โมดูล random เพราะพอร์ตนี้ไม่ได้ตั้ง seed ให้ ทุกบอร์ดจะสุ่มได้ลำดับเดียวกันหมด
    wait_ms = WAIT_MIN_MS + time.ticks_us() % WAIT_SPAN_MS
    phase = WAIT
    lights(False)
    big.color(COL_WARN)
    big.text("เตรียม... อย่าเพิ่งกด")
    img.icon("flag")
    img.color(COL_WARN)
    seg.text("-")
    seg.color(COL_DIM)
    show_round()
    ui.tone(57, ui.WAVE_SQUARE, 60, 80)   # เสียงต่ำสั้น ๆ บอกว่ารอบเริ่มแล้ว
    lcd.print("รอบ", "ซ้อม" if r == 0 else r, "สุ่มรอ", wait_ms, "ms")


def finish(result):
    """จบรอบ แสดงผลบนจอ และส่งขึ้น broker ถ้าเป็นรอบจริง คืน False ถ้าสายหลุด"""
    global phase, best, last_ms, rounds
    phase = IDLE
    lights(False)
    ms = result.get("ms")
    if result.get("false"):
        big.color(COL_BAD)
        big.text("ออกตัวก่อนไฟติด")
        img.icon("cross")
        img.color(COL_BAD)
        seg.text("-")          # ใช้ขีดเหมือนตอนว่าง ฟอนต์ 7 ส่วนไม่รับประกันตัวพิมพ์ใหญ่
        seg.color(COL_BAD)
    elif result.get("miss"):
        big.color(COL_WARN)
        big.text("ช้าเกิน 3 วินาที")
        img.icon("cross")
        img.color(COL_WARN)
        seg.text("-")
        seg.color(COL_DIM)
    else:
        last_ms = ms
        if rnd != 0 and (best is None or ms < best):
            best = ms
        big.color(COL_OK)
        big.text(str(ms) + " ms")
        img.icon("star")
        img.color(COL_OK)
        seg.text(str(ms))
        seg.color(COL_OK)
    show_round()
    lcd.print("รอบ", "ซ้อม" if rnd == 0 else rnd, "->", result)
    if rnd == 0:
        return True          # รอบซ้อมอยู่บนโต๊ะเรา ไม่ส่งขึ้นตารางของทั้งห้อง
    rounds = rounds + 1
    result["ev"] = "race"
    result["r"] = rnd
    return send(TOPIC_EVENT, result)


def screen_safe(text, n):
    """เก็บเฉพาะอักขระที่จอวาดได้ (ASCII กับไทย) แล้วตัดให้ไม่เกิน n ตัว

    ข้อความจากเว็บเป็นของคนอื่น อิโมจิหนึ่งตัวทำให้ทั้งบรรทัดกลายเป็นกล่องเปล่า
    แบบเดียวกับ examples/s04/17
    """
    out = ""
    for ch in text:
        c = ord(ch)
        if 0x20 <= c <= 0x7E or 0x0E00 <= c <= 0x0E7F:
            out = out + ch
        else:
            out = out + "?"
        if len(out) >= n:
            break
    return out


def handle(raw):
    """แปลคำสั่งหนึ่งใบ คนส่งพิมพ์มั่วได้เสมอ ต้องไม่ทำให้ลูปตาย"""
    global n_bad, last_rnd
    try:
        cmd = json.loads(raw.decode())
        action = cmd.get("cmd", "")
    except (ValueError, AttributeError):
        n_bad = n_bad + 1
        return

    if action == "race":
        r = cmd.get("r")
        # เลขรอบมาจากคนอื่น ต้องเป็นจำนวนเต็มบวกที่ไม่ใหญ่เกินเหตุ
        if not isinstance(r, int) or r < 1 or r > 9999:
            n_bad = n_bad + 1
            return
        # ผู้สอนอาจกดส่งซ้ำเผื่อใบแรกหาย ใบซ้ำต้องไม่เริ่มรอบใหม่ที่กำลังเล่นอยู่
        if r == last_rnd:
            return
        last_rnd = r
        arm(r)
    elif action == "beep":
        # ระหว่างรอบไม่ยอมให้ใครส่งเสียง ไม่งั้นแกล้งทำเสียงเหมือนตอนไฟติดให้ทีมอื่นกดผิดได้
        if phase != IDLE:
            n_bad = n_bad + 1
            return
        ui.tone(69, ui.WAVE_SQUARE, 90, 150)
    elif action == "say":
        # ป้ายพาไปได้ 126 ไบต์ ไทยตัวละ 3 ไบต์ ตัดที่ 24 ตัวเหมือนไฟล์ 06
        # และไม่ทับป้ายใหญ่ระหว่างรอบ ข้อความจากคนอื่นต้องไม่บังสัญญาณเกม
        note.color(COL_ACCENT)
        note.text(screen_safe(str(cmd.get("text", "")), 24))
    elif action == "led":
        i = cmd.get("n", 0)
        if phase != IDLE or not isinstance(i, int) or i < 0 or i >= N:
            n_bad = n_bad + 1     # ระหว่างรอบห้ามใครสั่งไฟ ไม่งั้นแกล้งให้ทีมอื่นกดผิดได้
            return
        if cmd.get("on", 1):
            gpio.led(i).on()
        else:
            gpio.led(i).off()
    else:
        n_bad = n_bad + 1


stable = btn.is_pressed()
last_raw = stable
t0 = time.ticks_ms()
last_change = t0
t_edge = None        # เวลาที่ขาเริ่มเปลี่ยนจากปล่อยเป็นกด ก่อนกันเด้งจะยืนยัน
last_state = time.ticks_add(t0, -STATE_MS)
last_sec = -1

while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    now = time.ticks_ms()
    raw = btn.is_pressed()
    pressed_at = None    # มีค่าเฉพาะรอบลูปที่กันเด้งเพิ่งยืนยันว่ากดจริง

    # --- งานที่ 1: ปุ่ม กันเด้งแบบไฟล์ 05 แต่จดเวลาขอบแรกไว้ด้วย ---
    if raw != last_raw:
        last_raw = raw
        last_change = now
        if raw and not stable and t_edge is None:
            t_edge = now          # ขอบแรกที่ขาเริ่มเป็น "กด" นี่คือเวลาที่นิ้วแตะจริง
        # เด้งกลับไปที่เดิมระหว่างกด ยังไม่ทิ้ง t_edge ไม่งั้นเวลาจะเลื่อนไปขอบหลังของการเด้ง
    elif raw == stable and t_edge is not None and \
            time.ticks_diff(now, last_change) >= DEBOUNCE_MS:
        t_edge = None             # กลับไปนิ่งที่เดิมครบเวลาแล้ว แปลว่าเป็นแค่สัญญาณแว้บ ทิ้งได้
    elif raw != stable and time.ticks_diff(now, last_change) >= DEBOUNCE_MS:
        stable = raw
        if stable:
            presses = presses + 1
            pressed_at = t_edge if t_edge is not None else now
            show_counts()
        t_edge = None

    # --- งานที่ 2: ช่วงของรอบ ---
    if phase == WAIT:
        if pressed_at is not None:
            if not finish({"false": 1}):
                lost = True
                break
        elif time.ticks_diff(now, t_arm) >= wait_ms:
            lights(True)
            t_go = time.ticks_ms()     # จดเวลาหลังสั่งไฟ ไม่ใช่ก่อน
            phase = GO
            big.color(COL_OK)
            big.text("กดเลย")
            img.icon("check")
            img.color(COL_OK)
            ui.tone(81, ui.WAVE_SQUARE, 100, 120)
    elif phase == GO:
        if pressed_at is not None:
            ms = time.ticks_diff(pressed_at, t_go)
            if ms < HUMAN_MIN_MS:
                res = {"false": 1, "ms": ms}   # ไวเกินมนุษย์ แปลว่ากดดักไว้ก่อน
            else:
                res = {"ms": ms}
            if not finish(res):
                lost = True
                break
        elif time.ticks_diff(now, t_go) >= GO_TIMEOUT_MS:
            if not finish({"miss": 1}):
                lost = True
                break
    elif pressed_at is not None:
        arm(0)       # กดตอนว่าง = เริ่มรอบซ้อม ใช้ได้ทั้งบนบอร์ดและบน Emulator

    # --- งานที่ 3: คำสั่งจากหน้าเว็บ ---
    # หยิบทุกรอบลูป กล่องรับมีช่องเดียว ใบใหม่ทับใบเก่า
    msg = mqtt.get_message()
    if msg is not None:
        handle(msg[1])
        show_counts()

    # --- งานที่ 4: telemetry ตามนาฬิกา แต่เฉพาะตอนไม่ได้จับเวลา ---
    # publish ใช้เวลาส่งออกทางเครือข่าย ถ้าไปส่งกลางรอบ ลูปจะอ่านปุ่มช้าลงและเวลาจะเพี้ยน
    # retain ใช้ไม่ได้บนเฟิร์มแวร์นี้ หน้าเว็บที่เปิดทีหลังจึงต้องรอใบถัดไป
    if phase == IDLE and time.ticks_diff(now, last_state) >= STATE_MS:
        last_state = now
        if not send(TOPIC_STATE, {"presses": presses, "best": best,
                                  "last": last_ms, "rounds": rounds}):
            lost = True
            break
        show_counts()

    if not mqtt.is_connected():
        lost = True
        break

    sec = time.ticks_diff(now, t0) // 1000
    if phase == IDLE and sec != last_sec:
        last_sec = sec
        status.text("เล่นได้อีก " + str(RUN_MS // 1000 - sec) + " วินาที")

    ui.poll()
    time.sleep_ms(POLL_MS)

lights(False)

if lost:
    status.color(COL_BAD)
    status.text("สายหลุด หยุดเกมแล้ว")
    note.color(COL_BAD)
    note.text("หลุดวินาทีที่ " + str(time.ticks_diff(time.ticks_ms(), t0) // 1000) +
              " กดรันใหม่ได้เลย")
    ui.poll()
    lcd.print("<span class=error>สายหลุด ต้องรันใหม่</span>")
else:
    status.color(COL_DIM)
    status.text("หมดเวลาเกมแล้ว")
    ui.poll()
    lcd.print("<span class=ok>จบเกม</span>")

b = "-" if best is None else str(best)
lcd.print("<span class=ok>เล่น", rounds, "รอบ | ดีที่สุด", b, "ms | กด", presses, "ครั้ง</span>")
print("เล่น", rounds, "รอบ | ดีที่สุด", b, "ms")

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# 1) ย้าย t_go = time.ticks_ms() ไปไว้ "ก่อน" lights(True) แล้วเล่นรอบซ้อมสิบรอบ
#    เทียบค่าเฉลี่ยกับตอนก่อนย้าย ต่างกันกี่ ms และทำไมถึงน้อยจนแทบมองไม่เห็น
# 2) เปลี่ยน pressed_at = t_edge ... เป็น pressed_at = now แล้วเล่นสิบรอบอีกชุด
#    คาดไว้ก่อนว่าค่าเฉลี่ยจะช้าลงราวเท่าไร (ใบ้: ดู DEBOUNCE_MS) แล้วค่อยวัดจริง
# 3) บนหน้าจอหน้าห้องมีสองคอลัมน์ มาถึงลำดับ กับ เวลาบนบอร์ด
#    หารอบที่สองคอลัมน์ไม่ตรงกัน แล้วบอกให้ได้สองสาเหตุว่าทำไมผลที่มาถึงก่อนไม่ใช่ผลที่เร็วกว่า
#    (ใบ้: ดู wait_ms บน Console ของแต่ละทีม และคิดถึงทางที่ข้อความเดินจากโต๊ะไปถึง broker)
