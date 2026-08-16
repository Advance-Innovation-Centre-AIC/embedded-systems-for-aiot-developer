# 01_scan_tuples.py - ผลของ wifi.scan() หน้าตาเป็นอย่างไร
#
# ไฟล์นี้สอน: wifi.scan() คืน list ของ tuple สี่ช่อง (ssid, rssi, security,
#             channel) เรียงตามลำดับที่ชิปเจอ ไม่ได้เรียงให้
# ดูที่จอ   : เลขจำนวนวงตัวโต แท่งความแรงของวงแรก และรายชื่อวงสี่แถวแรก
#             ค่า rssi ทุกตัวติดลบ ยิ่งใกล้ศูนย์ยิ่งแรง
# กับดัก    : มันไม่ใช่ dict - เขียน net["ssid"] จะได้ TypeError ทันที
#             ต้องอ่านด้วยหมายเลขช่อง หรือแกะสี่ตัวพร้อมกันในบรรทัดเดียว

import wifi
import lcd
import ui
import time

ROWS = 4                       # จำนวนแถวที่จอ 4.3 นิ้ววางได้พอดีโดยไม่เบียดกัน
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF


def percent_of(rssi):
    # -90 dBm คือแทบไม่เหลือ -40 dBm คือเต็มขีด ใช้เทียบวงต่อวงได้ดีพอ
    pct = (rssi + 90) * 2
    return 0 if pct < 0 else (100 if pct > 100 else pct)


lcd.clear()
lcd.console("<h2>คาบ 9 - รูปร่างของผล scan()</h2>")

# สร้าง widget ให้ครบก่อนเรียก scan() พื้นที่วาดจริงคือ 792x398
# มุมขวาล่างราว x>690 y>340 มีปุ่ม Console ทับอยู่ อย่าวางอะไรตรงนั้น
ui.screen()
time.sleep_ms(200)
ui.Label("คาบ 9 - ผลของ wifi.scan()", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=652, h=120, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("จำนวนวงที่เจอ", x=40, y=68, color=COL_DIM, value=16)
seg = ui.Seg7(text="--", x=40, y=92, w=132, h=64, color=COL_INFO)
l_first = ui.Label("วงแรก: -", x=200, y=92, color=COL_TEXT, value=20)
bar = ui.Bar(x=200, y=124, w=440, h=24, min=0, max=100, value=0, color=COL_DIM)
ui.Label("สี่แถวแรก ตามลำดับที่ชิปเจอ", x=20, y=184, color=COL_DIM, value=16)
rows = []
for i in range(ROWS):
    rows.append(ui.Label("-", x=40, y=212 + i * 30, color=COL_DIM, value=20))
status = ui.Label("กำลังสแกน... จอจะนิ่งไปครู่หนึ่ง", x=20, y=336,
                  color=COL_WARN, value=20)
ui.poll()

# scan() บล็อกจนกว่าจะสแกนครบทุกช่องสัญญาณ ราว 3-10 วินาที
# ย่าน 5 GHz นานกว่าเพราะช่องเยอะกว่ามาก ป้ายข้างบนจึงต้องขึ้นก่อนบรรทัดนี้
t0 = time.ticks_ms()
nets = wifi.scan()
took = time.ticks_diff(time.ticks_ms(), t0)

seg.text(str(len(nets)))
status.color(COL_OK)
status.text("สแกนเสร็จใน {} ms - เจอ {} วง".format(took, len(nets)))
lcd.print("สแกนเสร็จใน", took, "ms เจอ", len(nets), "วง")

# แกะสี่ตัวพร้อมกันในบรรทัดเดียว อ่านง่ายกว่าและกันจำสลับช่อง
for i in range(len(nets)):
    ssid, rssi, security, channel = nets[i]

    # security เป็นตัวเลข 0 คือเปิดโล่ง ค่าอื่นคือมีการเข้ารหัส
    # ไม่ต้องรู้ว่าเลขไหนคือ WPA2 หรือ WPA3 ก็ตัดสินใจได้ว่าต่อได้ไหม
    lock = "open" if security == 0 else "lock"

    if i < ROWS:
        rows[i].color(COL_TEXT if rssi >= -75 else COL_DIM)
        rows[i].text("{}. {}  {} dBm  ch{}  {}".format(
            i + 1, ssid[:18], rssi, channel, lock))

    # lcd เก็บรายการเรียงลำดับไว้ให้ย้อนอ่านครบทุกวง ไม่ใช่แค่สี่แถวแรก
    lcd.print("{}. {} {} dBm ch{} {}".format(i + 1, ssid[:20], rssi, channel, lock))

    # ตารางกว้าง ๆ แบบนี้คือสิ่งที่ print() มีไว้ทำ จอ 4.3 นิ้ววางไม่ลง
    print("{:<24} {:>5} dBm  ch{:<4} {}".format(ssid[:24], rssi, channel, lock))

if len(nets) > 0:
    first = nets[0]
    pct = percent_of(first[1])
    bar.color(COL_OK if first[1] >= -60 else
              (COL_WARN if first[1] >= -75 else COL_BAD))
    bar.value(pct)
    l_first.text("วงแรก: {}  {} dBm  ({}%)".format(first[0][:16], first[1], pct))

    # ช่องเดียวกันนี้อ่านด้วยหมายเลขก็ได้ ผลเท่ากันทุกประการ
    # เอามาไว้ให้เห็นว่า net[0] กับ ssid คือของสิ่งเดียวกัน
    print("แถวแรกอ่านด้วยหมายเลขช่อง:")
    print("  net[0] ssid     =", first[0])
    print("  net[1] rssi     =", first[1])
    print("  net[2] security =", first[2])
    print("  net[3] channel  =", first[3])
    lcd.print("net[0]=" + str(first[0]) + " net[1]=" + str(first[1]))
    lcd.print("net[2]=" + str(first[2]) + " net[3]=" + str(first[3]))
else:
    # ผลว่างเปล่าไม่ใช่ความผิดพลาดของโค้ด แต่แปลว่าเสาอากาศไม่ได้ยินอะไรเลย
    # เจอบ่อยเมื่อบอร์ดอยู่ในตู้เหล็กหรือห้องใต้ดิน
    status.color(COL_BAD)
    status.text("ไม่เจอวงไหนเลย ลองย้ายบอร์ดออกที่โล่ง")
    lcd.print("<span class=err>ไม่เจอวงไหนเลย</span>")

ui.poll()
