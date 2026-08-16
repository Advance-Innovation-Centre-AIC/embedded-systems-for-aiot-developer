# 04_scan_the_room.py - ให้บอร์ดฟังคลื่นทั้งห้อง แล้วบอกว่าใครอยู่ตรงไหนบ้าง
#
# ไฟล์นี้ใช้ของใหม่สองชิ้น คือ wifi.scan() ที่คืนรายชื่อทุกวงที่เสาอากาศได้ยิน
# กับ wifi.status() ที่คืน dict สรุปสถานะของวิทยุ ทั้งคู่ไม่ต้องต่อเน็ตก่อน
# สแกนได้แม้ยังไม่รู้รหัสผ่านของวงไหนเลยสักวง
#
# ไฟล์นี้สอน: การสำรวจมาก่อนการเชื่อมต่อเสมอ ก่อนจะถามว่า "ต่อไม่ติดเพราะอะไร"
#             ต้องตอบให้ได้ก่อนว่า "บอร์ดได้ยินวงนั้นไหม" ซึ่ง scan() ตอบให้
# ดูที่จอ   : เลขจำนวนวงที่เจอ เวลาที่ใช้สแกน และห้าแถวที่แรงที่สุดเรียงจากบนลงล่าง
#             แถบขวาบนคือความแรงของวงที่ชนะ ค่า rssi ทุกตัวติดลบ ยิ่งใกล้ศูนย์ยิ่งแรง
# กับดัก    : wifi.status() คืนคีย์ ssid กับ rssi มาด้วยก็จริง แต่สองคีย์นั้นเป็นของปลอม
#             เฟิร์มแวร์ยัด "" กับ 0 ไว้ตายตัว (modwifi.c บรรทัด 337-343) ใครอ่านสองคีย์นี้
#             แล้วเชื่อ จะได้กราฟความแรงที่เป็นเส้นศูนย์ตลอดกาล ความแรงมาจาก scan() เท่านั้น

import lcd
import time
import ui
import wifi

ROWS = 5             # แสดงกี่แถวบนจอ ที่เหลือส่งลงลิ้นชัก Console
STRONG, WEAK = -60, -75    # เกณฑ์แบ่งสีของความแรง

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x00E676, 0xFFA726, 0xFF5252, 0x40C4FF


def percent_of(rssi):
    """แปลง rssi เป็น 0-100 ให้ตาอ่านง่าย -90 คือแทบไม่เหลือ -40 คือเต็มขีด"""
    pct = (rssi + 90) * 2
    if pct < 0:
        return 0
    return 100 if pct > 100 else pct


def color_of(rssi):
    if rssi >= STRONG:
        return COL_OK
    return COL_WARN if rssi >= WEAK else COL_BAD


ui.screen()
time.sleep_ms(200)

ui.Label("หาทุกวงในห้องนี้", x=20, y=12, color=COL_TEXT, value=24)
status = ui.Label("กำลังจะสแกน", x=20, y=48, color=COL_WARN, value=18)

ui.Panel(x=20, y=78, w=650, h=104, color=COL_CARD, min=COL_DIM, max=12,
         value=1)

ui.Label("เจอกี่วง", x=40, y=88, color=COL_DIM, value=16)
seg_n = ui.Seg7(text="--", x=40, y=112, w=130, h=56, color=COL_INFO)

ui.Label("ใช้เวลา (ms)", x=190, y=88, color=COL_DIM, value=16)
seg_ms = ui.Seg7(text="--", x=190, y=112, w=150, h=56, color=COL_INFO)

ui.Label("แรงที่สุด", x=360, y=88, color=COL_DIM, value=16)
best_lbl = ui.Label("รอผลสแกน", x=360, y=116, color=COL_TEXT, value=20)

# Bar ไม่รับ color= ตอนสร้าง ต้องเรียก .color() หลังสร้างถึงจะเปลี่ยนสีได้จริง
bar = ui.Bar(x=360, y=146, w=290, h=18, min=0, max=100, value=0)
bar.color(COL_DIM)

# ห้าแถวสร้างไว้ครบตั้งแต่ตอนนี้ แล้วเดี๋ยวเขียนทับด้วยผลจริง
# ถ้าไปสร้างข้างในลูปทีหลัง จำนวน widget จะขึ้นกับจำนวนวงที่บังเอิญเจอในห้องนั้น
# ห้องที่มีวงเยอะกว่าเพดาน 32 ตัวจะทำให้โปรแกรมตายกลางคัน ทั้งที่โค้ดไม่ได้ผิด
rows = []
for i in range(ROWS):
    rows.append(ui.Label("-", x=20, y=196 + i * 28, color=COL_DIM, value=18))

trap = ui.Label("wifi.status() มี ssid กับ rssi แต่เป็นของปลอม", x=20, y=344,
                color=COL_DIM, value=16)
hint = ui.Label("ความแรงจริงมาจาก scan() เท่านั้น", x=20, y=370,
                color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>หาทุกวงในห้องนี้</h2>")

# scan() บล็อกจนกว่าจะกวาดครบทุกช่องสัญญาณ ราว 3 ถึง 10 วินาที
# ย่าน 5 GHz นานกว่าเพราะช่องเยอะกว่ามาก ป้ายบอกสถานะกับ ui.poll()
# จึงต้องมาก่อนบรรทัดนี้ ไม่ใช่หลัง ไม่งั้นคนดูจะเห็นจอว่างตลอดช่วงที่รอ
status.text("กำลังสแกน จอจะนิ่งสักครู่ อย่ากดรีเซ็ต")
ui.poll()
lcd.print("กำลังสแกน...")

t0 = time.ticks_ms()
nets = wifi.scan()
took = time.ticks_diff(time.ticks_ms(), t0)

seg_n.text(str(len(nets)))
seg_ms.text(str(took))
lcd.print("สแกนเสร็จใน", took, "ms เจอ", len(nets), "วง")

if len(nets) == 0:
    # ผลว่างเปล่าไม่ใช่ความผิดพลาดของโค้ด แต่แปลว่าเสาอากาศไม่ได้ยินอะไรเลย
    # เจอบ่อยเมื่อบอร์ดอยู่ในตู้เหล็กหรือห้องใต้ดิน พูดออกไปตรง ๆ ดีกว่าเงียบ
    seg_n.color(COL_BAD)
    status.color(COL_BAD)
    status.text("ไม่เจอวงไหนเลย ลองย้ายบอร์ดออกที่โล่ง")
    ui.poll()
    lcd.print("<span class=error>ไม่เจอวงไหนเลย</span>")
    raise SystemExit

# --- เรียงจากแรงไปอ่อน ---
# scan() คืนตามลำดับที่ชิปเจอ ไม่ได้เรียงให้ ถ้าอยากได้ "วงที่แรงที่สุด"
# ต้องเรียงเอง ช่องที่ 1 ของแต่ละ tuple คือ rssi และมันติดลบ
# ติดลบมากคืออ่อน จึงเรียงจากมากไปน้อยเพื่อให้ตัวแรงที่สุดมาอยู่หัวแถว
# key=lambda net: net[1] บอก sort ว่าให้ตัดสินด้วยช่องที่สอง ซึ่งคือ rssi
# และ .sort() เรียงในตัวมันเอง ไม่ได้คืน list ใหม่ออกมา
nets.sort(key=lambda net: net[1], reverse=True)
ranked = nets

open_count = 0
for i in range(len(ranked)):
    ssid, rssi, security, channel = ranked[i]

    # security เป็นตัวเลข 0 คือเปิดโล่ง ค่าอื่นคือมีการเข้ารหัส
    # ไม่ต้องรู้ว่าเลขไหนคือ WPA2 หรือ WPA3 ก็ตัดสินใจได้ว่าวงไหนเปิดอยู่
    if security == 0:
        open_count = open_count + 1
        lock = "เปิดโล่ง"
    else:
        lock = "มีรหัส"

    # ชื่อวงเป็นข้อความที่คนอื่นตั้ง ยาวแค่ไหนก็ได้ และป้ายบนจอพาไปได้ 126 ไบต์
    # ตัดให้สั้นก่อนเสมอ ไม่ใช่หวังว่าเพื่อนบ้านจะตั้งชื่อสั้น
    short = ssid[:18] if len(ssid) > 0 else "(ไม่ประกาศชื่อ)"

    if i < ROWS:
        rows[i].color(color_of(rssi))
        rows[i].text(str(i + 1) + ".  " + short + "   " + str(rssi) +
                     " dBm   ch" + str(channel) + "   " + lock)

    lcd.print(str(i + 1) + ". " + short + " " + str(rssi) + " dBm ch" +
              str(channel) + " " + lock)

    # ตารางกว้าง ๆ แบบนี้คือสิ่งที่ print() มีไว้ทำ จอ 4.3 นิ้ววางไม่ลง
    print("{:<24} {:>5} dBm  ch{:<4} {}".format(ssid[:24], rssi, channel, lock))

# --- วงที่ชนะ ---
top_ssid, top_rssi = ranked[0][0], ranked[0][1]
pct = percent_of(top_rssi)
bar.value(pct)
bar.color(color_of(top_rssi))
best_lbl.color(color_of(top_rssi))
best_lbl.text(top_ssid[:14] + "  " + str(top_rssi) + " dBm")

seg_n.color(COL_OK)
status.color(COL_OK)
status.text("เจอ " + str(len(nets)) + " วง | เปิดโล่ง " + str(open_count) +
            " วง | แสดง " + str(ROWS) + " แถวแรก")
ui.poll()

# --- กับดักของไฟล์นี้ พิสูจน์ด้วยตัวเลขจริง ไม่ใช่เชื่อคำอธิบาย ---
# status() คืน dict ห้าคีย์ สามคีย์แรกเป็นของจริง สองคีย์หลังเป็นของปลอม
st = wifi.status()
lcd.console("<span class=muted>--- wifi.status() ---</span>")
for k in st:
    lcd.print("  " + k + " =", st[k])

# เทียบกันตรง ๆ ให้เห็นกับตา วงที่แรงที่สุดในห้องแรง top_rssi dBm
# แต่ status()["rssi"] ตอบ 0 เท่าเดิมเสมอ ไม่ว่าจะยืนตรงไหนของตึก
trap.color(COL_BAD)
trap.text("status() rssi = " + str(st["rssi"]) + " แต่ของจริง " +
          str(top_rssi) + " dBm")
hint.color(COL_TEXT)
hint.text("mode = " + st["mode"] + " | ip = " + st["ip"])
ui.poll()

lcd.print("<span class=error>status() rssi =", st["rssi"], "ซึ่งเป็นค่าตายตัว</span>")
lcd.print("<span class=ok>ของจริงคือ", top_rssi, "dBm จาก scan()</span>")
print("status() =", st)
print("แรงที่สุด:", top_ssid, top_rssi, "dBm |", pct, "%")

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# เดินถือบอร์ดไปสุดห้องแล้วรันซ้ำ จดชื่อวงเดิมกับตัวเลข dBm ของมันทั้งสองจุด
# แล้วตอบว่าเลขเปลี่ยนไปกี่ dBm และแถวของมันเลื่อนอันดับไหม
# ใบ้: ถ้าอยากได้ทั้งชื่อวงที่ต่ออยู่และความแรงของมันพร้อมกัน ต้องเอาผลของ scan()
#      มาจับคู่กับ wifi.ip() เอง เพราะ status() ให้ครบทั้งคู่ไม่ได้
