# 04_ping_two_targets.py - เกตเวย์ตอบ แต่อินเทอร์เน็ตไม่ตอบ แปลว่าอะไร
#
# Why : "เน็ตล่ม" เป็นคำที่ซ่อมอะไรไม่ได้เลย เพราะมันไม่บอกว่าขาดตรงไหน
#        ยิงปลายทางเดียวก็ยังตอบไม่ได้ ต้องยิงสองจุดบนเส้นทางเดียวกันแล้วเทียบ
# What: การวัดสองปลายทางพร้อมกัน - เกตเวย์คือปลายในบ้าน 8.8.8.8 คือปลายนอกบ้าน
#        ผลสองเส้นเทียบกันแล้วชี้ได้ว่าปัญหาอยู่ก่อนหรือหลังเราเตอร์
#
# ดูที่จอ: กราฟสองเส้น เขียวคือเกตเวย์ ฟ้าคืออินเทอร์เน็ต กับตัวเลข ms ล่าสุด
#          จำนวนรอบที่หายไปของแต่ละปลายทาง และประโยคสรุปหนึ่งบรรทัด
# กับดัก : ping รับเฉพาะหมายเลข IP ใส่ชื่อโฮสต์อย่าง "google.com" จะได้ ValueError
#          และเมื่อไม่มีคำตอบมันคืน -1 ไม่ได้โยน exception อย่าเอา -1 ไปเฉลี่ย

import wifi
import lcd
import ui
import time

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
INTERNET_IP = "8.8.8.8"      # Google Public DNS ตอบ ping และอยู่นอกบ้านเราแน่นอน
TIMEOUT_MS = 1500
ROUNDS = 20
CHART_MAX_MS = 60            # แกนตั้งของกราฟ ถ้าไม่กำหนดเองมันตั้ง 0-100 ให้
COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x00E676, 0xFFA726, 0xFF5252, 0x40C4FF

lcd.clear()
lcd.console("<h2>คาบ 9 - ยิงสองปลายทาง</h2>")

ui.screen()
time.sleep_ms(200)
ui.Label("คาบ 9 - ping เกตเวย์ กับ อินเทอร์เน็ต", x=20, y=12,
         color=COL_TEXT, value=24)
ui.Panel(x=20, y=48, w=650, h=92, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("เกตเวย์ ms", x=40, y=58, color=COL_DIM, value=16)
seg_gw = ui.Seg7(text="--", x=40, y=78, w=120, h=52, color=COL_OK)
ui.Label("อินเทอร์เน็ต ms", x=190, y=58, color=COL_DIM, value=16)
seg_net = ui.Seg7(text="--", x=190, y=78, w=120, h=52, color=COL_INFO)
ui.Label("หายไป gw / net", x=350, y=58, color=COL_DIM, value=16)
l_lost = ui.Label("0 / 0", x=350, y=84, color=COL_TEXT, value=24)
ui.Label("รอบ", x=530, y=58, color=COL_DIM, value=16)
l_round = ui.Label("0/" + str(ROUNDS), x=530, y=84, color=COL_TEXT, value=24)
# min/max คือช่วงของแกนตั้ง ping ในบ้านอยู่หลักหน่วยถึงหลักสิบ ถ้าปล่อยเป็น 0-100
# เส้นจะแบนติดขอบล่างจนอ่านความต่างไม่ออก
ch = ui.Chart(x=20, y=152, w=650, h=150, color=COL_CARD, min=0, max=CHART_MAX_MS)
s_gw = ch.add_series(COL_OK)
s_net = ch.add_series(COL_INFO)
# แยกสองป้ายให้อยู่ในเพดาน 126 ไบต์ของ ui.Label - ไทยหนึ่งตัวกิน 3 ไบต์
ui.Label("เขียว = เกตเวย์", x=20, y=308, color=COL_DIM, value=16)
ui.Label("ฟ้า = อินเทอร์เน็ต (ms ต่อรอบ)", x=180, y=308, color=COL_DIM,
         value=16)

# ป้ายสรุปเกิดก่อน connect() เพราะ connect() บล็อก จอต้องพูดก่อนจะเงียบ
l_verdict = ui.Label("กำลังต่อ " + WIFI_SSID + " ... จอจะนิ่งไปครู่หนึ่ง",
                     x=20, y=336, color=COL_WARN, value=18)
ui.poll()

lcd.print("กำลังต่อ", WIFI_SSID, "- อาจรอนาน")
if not wifi.connect(WIFI_SSID, WIFI_PASS):
    l_verdict.color(COL_BAD)
    l_verdict.text("ต่อไม่สำเร็จ หยุดตรงนี้")
    ui.poll()
    lcd.print("<span class=err>ต่อไม่สำเร็จ หยุดตรงนี้</span>")
    raise SystemExit

ip = wifi.ip()

# เกตเวย์ของวงแลนห้องเรียนและบ้านเกือบทั้งหมดคือเลข .1 ของวงเดียวกัน
# ถ้าห้องนี้ไม่ได้ใช้ .1 ให้แก้บรรทัดนี้เป็นเลขจริงที่ผู้สอนบอก
parts = ip.split(".")
gateway = parts[0] + "." + parts[1] + "." + parts[2] + ".1"
l_verdict.color(COL_DIM)
l_verdict.text("IP เรา " + ip + " - ยิงเกตเวย์ " + gateway)
ui.poll()
lcd.print("IP", ip, "เกตเวย์", gateway)
print("รอบ   เกตเวย์      อินเทอร์เน็ต")

gw_lost = 0
net_lost = 0
gw_total = 0
gw_count = 0

for i in range(ROUNDS):
    # ยิงเกตเวย์ก่อนเสมอ ลำดับนี้ทำให้อ่านผลได้ว่าขาดตรงช่วงไหนของเส้นทาง
    ms_gw = wifi.ping(gateway, TIMEOUT_MS)
    ms_net = wifi.ping(INTERNET_IP, TIMEOUT_MS)

    if ms_gw < 0:
        gw_lost += 1
    else:
        # เก็บเฉพาะรอบที่ตอบไว้เฉลี่ย ถ้าเอา -1 ไปรวมค่าเฉลี่ยจะเพี้ยนลงทันที
        gw_total += ms_gw
        gw_count += 1
    if ms_net < 0:
        net_lost += 1

    # กราฟรับได้แต่จำนวนเต็ม รอบที่หายไปป้อน 0 ไม่ใช่ -1 ไม่งั้นเส้นจะดิ่งใต้แกน
    ch.set_next(s_gw, ms_gw if ms_gw > 0 else 0)
    ch.set_next(s_net, ms_net if ms_net > 0 else 0)
    seg_gw.text("--" if ms_gw < 0 else str(ms_gw))
    seg_net.text("--" if ms_net < 0 else str(ms_net))
    seg_gw.color(COL_BAD if ms_gw < 0 else COL_OK)
    seg_net.color(COL_BAD if ms_net < 0 else COL_INFO)
    l_lost.text("{} / {}".format(gw_lost, net_lost))
    l_lost.color(COL_BAD if (gw_lost + net_lost) > 0 else COL_TEXT)
    l_round.text("{}/{}".format(i + 1, ROUNDS))

    lcd.print("รอบ {} gw {} net {}".format(
        i + 1,
        "timeout" if ms_gw < 0 else str(ms_gw) + " ms",
        "timeout" if ms_net < 0 else str(ms_net) + " ms"))
    print("  {:<4} {:<12} {}".format(
        i + 1,
        "timeout" if ms_gw < 0 else str(ms_gw) + " ms",
        "timeout" if ms_net < 0 else str(ms_net) + " ms"))

    # แบ่ง sleep เป็นชิ้นเล็กแล้ว poll ทุกชิ้น จอจึงเดินต่อระหว่างรอ
    for _ in range(2):
        ui.poll()
        time.sleep_ms(150)

lcd.print("เสีย gw {} net {} จาก {} รอบ".format(gw_lost, net_lost, ROUNDS))
print("เกตเวย์เสีย", gw_lost, "จาก", ROUNDS, " อินเทอร์เน็ตเสีย", net_lost)
if gw_count > 0:
    lcd.print("เกตเวย์ตอบเฉลี่ย {} ms จาก {} รอบ".format(gw_total // gw_count, gw_count))
    print("เกตเวย์ตอบเฉลี่ย", gw_total // gw_count, "ms จาก", gw_count, "รอบที่ตอบ")

# นี่คือหัวใจของทั้งไฟล์: อ่านผลสองเส้นแล้วสรุปให้ได้ว่าปัญหาอยู่ที่ไหน
if gw_lost == 0 and net_lost > 0:
    l_verdict.color(COL_WARN)
    l_verdict.text("ในบ้านปกติ ปัญหาอยู่เลยเราเตอร์ออกไป")
elif gw_lost > 0:
    l_verdict.color(COL_BAD)
    l_verdict.text("เราเตอร์ยังไม่รอด ปัญหาที่คลื่นหรือวงแลน")
else:
    l_verdict.color(COL_OK)
    l_verdict.text("เส้นทางโล่งทั้งสองช่วง")
ui.poll()
lcd.print("สรุป: ดูป้ายล่างสุดบนจอ")
