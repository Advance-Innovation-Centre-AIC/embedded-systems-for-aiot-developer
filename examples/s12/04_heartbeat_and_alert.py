# 04_heartbeat_and_alert.py - ข้อความสองชนิด สองจังหวะ คนละหน้าที่
#
# Why : บอร์ดที่เงียบสนิทมาสามชั่วโมง แปลได้สองอย่างที่ตรงข้ามกัน - ทุกอย่างปกติดี
#       หรือมันตายไปตั้งแต่ชั่วโมงแรก ถ้าอุปกรณ์ส่งเฉพาะตอนมีเรื่อง ฝั่งรับแยกไม่ออก
#       และวันที่มันตายจริง จะไม่มีใครรู้จนกว่าจะมีคนเดินไปดูด้วยตา
# What: heartbeat คือข้อความที่บอกว่า "ยังอยู่" ส่งตามนาฬิกาไม่ว่าจะมีเรื่องหรือไม่
#       alert คือข้อความที่บอกว่า "มีเรื่อง" ส่งตอนสถานะเปลี่ยนเท่านั้น และมีช่วงเว้นกำกับ
#       สองอย่างนี้เดินคนละนาฬิกา เพราะมันตอบคนละคำถาม
#
# ดูที่จอ: Seg7 สามตัวคือจำนวน beat จำนวน alert และจำนวนใบที่ถูกกลั้นไว้
#          แถบล่างของการ์ดคือการนับถอยหลังถึง heartbeat ใบถัดไป มันเต็มแล้ววนใหม่เรื่อย ๆ
#          กราฟล่างคือค่าที่วัดได้ มีเส้นแดงคือ ALERT_LIMIT ตัดขวาง
#          ลิ้นชัก Console เก็บทุกใบที่ส่งออกไป และทุกใบที่ถูกกลั้นไว้
# กับดัก : alert ที่ไม่มีช่วงเว้น จะยิงรัวทุกรอบตอนค่าแกว่งอยู่แถวเส้นแบ่ง
#          ฝั่งรับจะได้ข้อความสามร้อยใบในหนึ่งนาที แล้วปิดการแจ้งเตือนทิ้ง

import lcd
import time
import ui

ALERT_LIMIT = 15.0
HEARTBEAT_MS = 2000      # ของจริงมักตั้ง 30-60 วินาที ย่อลงเพื่อให้เห็นผลในคาบเรียน
ALERT_GAP_MS = 4000      # เตือนซ้ำเรื่องเดิมได้เร็วที่สุดเท่านี้
LOOP_MS = 300
CHART_MAX = 220          # กราฟรับจำนวนเต็ม จึงคูณสิบก่อนใส่ (0.0-22.0 -> 0-220)

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_BAD = 0x30A46C, 0xE5484D

STATE_COLOR = {"OK": COL_OK, "ALERT": COL_BAD}

SERIES = (2.0, 3.0, 16.0, 17.0, 16.5, 18.0, 4.0, 3.0, 2.0,
          19.0, 20.0, 3.0, 2.0, 1.0, 2.0, 3.0, 2.0, 1.0)

ui.screen()
time.sleep_ms(200)

# ผังจอ: การ์ดใบเดียวห้าช่อง เพราะสิ่งที่ต้องอ่านคือความสัมพันธ์ของตัวเลขห้าตัว
# วางคนละการ์ดแล้วตาจะเทียบไม่ติด - แล้วกราฟหนึ่งช่อง และสองบรรทัดล่าง
ui.Label("คาบ 12 - heartbeat กับ alert", x=24, y=8, color=COL_TEXT, value=24)
ui.Panel(x=24, y=56, w=744, h=168, color=COL_CARD, min=COL_CARD, max=12,
         value=1)

ui.Label("สถานะ", x=40, y=72, color=COL_DIM, value=16)
l_state = ui.Label("OK", x=40, y=104, color=COL_OK, value=28)

ui.Label("ค่าตอนนี้", x=184, y=72, color=COL_DIM, value=16)
seg_val = ui.Seg7("0.0", x=184, y=104, w=128, h=56, color=COL_ACCENT)

# ตัวนับสามตัววางเรียงกัน เพราะสิ่งที่ต้องเทียบคืออัตราส่วนของมัน ไม่ใช่ค่าเดี่ยว ๆ
# ทั้งสามใช้สีข้อความปกติ ของเดิมทาเขียว/แดง/ส้มตามชนิดของใบ ซึ่งทำให้จอ
# ประกาศว่า "มีเรื่อง" ตั้งแต่วินาทีแรกทั้งที่ตัวเลขยังเป็นศูนย์ (S7.7.3)
ui.Label("beat", x=328, y=72, color=COL_DIM, value=16)
seg_beat = ui.Seg7("0", x=328, y=104, w=112, h=56, color=COL_TEXT)

ui.Label("alert", x=472, y=72, color=COL_DIM, value=16)
seg_alert = ui.Seg7("0", x=472, y=104, w=112, h=56, color=COL_TEXT)

ui.Label("กลั้นไว้", x=616, y=72, color=COL_DIM, value=16)
seg_sup = ui.Seg7("0", x=616, y=104, w=112, h=56, color=COL_TEXT)

ui.Label("ถึง heartbeat ใบถัดไป", x=40, y=172, color=COL_DIM, value=16)
bar_beat = ui.Bar(x=288, y=168, w=456, h=32, min=0, max=100, value=0)

ch = ui.Chart(x=24, y=240, w=744, h=88, color=COL_CARD, min=0, max=CHART_MAX)
s_val = ch.add_series(COL_ACCENT)
s_lim = ch.add_series(COL_BAD)

# ข้อสังเกตยืนพื้นสองใบอยู่บรรทัดบน ส่วน l_foot เริ่มด้วยป้ายกำกับเส้นในกราฟ
# แล้วถูกเขียนทับด้วยสรุปตอนจบ - ทั้งคู่กว้างพอ ๆ กัน จึงไม่ยื่นไปทับใคร
ui.Label("beat เดินตามนาฬิกา", x=24, y=336, color=COL_DIM, value=16)
ui.Label("alert เดินตามเหตุการณ์", x=256, y=336, color=COL_DIM, value=16)
l_foot = ui.Label("เส้นแดงคือเกณฑ์ ALERT", x=24, y=368, color=COL_DIM,
                  value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 12 - ทุกใบที่ส่งออกไปจริง</h2>")


def send(kind, value, state):
    # ที่นี่แค่บันทึกลงลิ้นชัก ของจริงคือ mqtt.publish หรือ tesaiot.publish
    # แยกฟังก์ชันไว้ตัวเดียว เปลี่ยนช่องทางทีหลังจึงแก้ที่เดียว
    css = "ok" if kind == "beat" else ("error" if kind == "alert" else "info")
    lcd.print("<span class={}>[ส่ง {}] v={} state={}</span>".format(
        css, kind, value, state))
    return True


state = "OK"
now = time.ticks_ms()
t_beat = now
last_alert = None
beats, alerts, suppressed = 0, 0, 0

for i in range(len(SERIES)):
    now = time.ticks_ms()
    value = SERIES[i]
    level = "ALERT" if value > ALERT_LIMIT else "OK"

    seg_val.text("{:.1f}".format(value))
    ch.set_next(s_val, int(value * 10))
    ch.set_next(s_lim, int(ALERT_LIMIT * 10))

    # ขาที่หนึ่ง: สถานะเปลี่ยนหรือไม่ ถ้าเปลี่ยนเข้าเขต ALERT จึงพิจารณาส่ง
    if level != state:
        state = level
        l_state.text(state)
        l_state.color(STATE_COLOR[state])
        if state == "ALERT":
            # ช่วงเว้นนับจากใบล่าสุดที่ส่งได้จริง ไม่ใช่นับจากครั้งที่พยายามส่ง
            ready = last_alert is None or time.ticks_diff(now, last_alert) >= ALERT_GAP_MS
            if ready:
                send("alert", value, state)
                last_alert = now
                alerts += 1
                seg_alert.text(str(alerts))
            else:
                suppressed += 1
                seg_sup.text(str(suppressed))
                lcd.print("<span class=warn>[กลั้นไว้] ยังเหลืออีก {} ms</span>".format(
                    ALERT_GAP_MS - time.ticks_diff(now, last_alert)))
        else:
            # กลับสู่ปกติต้องบอกด้วย ไม่งั้นฝั่งรับไม่รู้ว่าเรื่องจบแล้ว
            send("clear", value, state)

    # ขาที่สอง: heartbeat เดินตามนาฬิกาของมันเอง ไม่เกี่ยวกับสถานะ
    if time.ticks_diff(now, t_beat) >= HEARTBEAT_MS:
        t_beat = now
        send("beat", value, state)
        beats += 1
        seg_beat.text(str(beats))

    since = time.ticks_diff(time.ticks_ms(), t_beat)
    pct = int(since * 100 / HEARTBEAT_MS)
    bar_beat.value(100 if pct > 100 else pct)

    ui.poll()
    time.sleep_ms(LOOP_MS)

l_foot.text("beat {} ใบ - alert {} ใบ - กลั้นไว้ {} ใบ".format(beats, alerts, suppressed))
l_foot.color(COL_TEXT)
lcd.print("<b>สรุป</b> beat {} - alert {} - กลั้นไว้ {}".format(beats, alerts, suppressed))
lcd.print("<span class=muted>ไม่เห็น beat สองคาบ = อุปกรณ์หาย</span>")

# ค้างจอไว้ให้อ่านตัวนับสามตัวทัน ลูปจบแล้วแต่ผลยังต้องอยู่บนจอ
for _ in range(20):
    ui.poll()
    time.sleep_ms(100)
