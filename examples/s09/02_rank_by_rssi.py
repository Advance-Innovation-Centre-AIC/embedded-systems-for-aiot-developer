# 02_rank_by_rssi.py - เรียงวงจากแรงไปอ่อน แล้วแปลง dBm ให้คนอ่านออก
#
# Why : ช่างติดตั้งถามคำถามเดียวเสมอว่า "ตรงนี้วางบอร์ดได้ไหม" คำว่า -67 dBm
#        ไม่ตอบคำถามนั้นให้ใครเลย ต้องแปลงเป็นแท่งกับคำตัดสินก่อน คนถึงจะใช้ได้
# What: การจัดอันดับด้วย rssi แล้วแปลงเป็นเปอร์เซ็นต์และคำตัดสินสามระดับ
#        RSSI เป็นเลขติดลบ ค่าที่ "มากกว่า" คือแรงกว่า -45 แรงกว่า -80
#
# ดูที่จอ: ห้าอันดับแรก แต่ละอันดับมีชื่อวง dBm คำตัดสิน และแท่งความแรงที่มีสี
# กับดัก : เรียงลืม reverse=True แล้วจะได้วงที่อ่อนที่สุดขึ้นก่อน ซึ่งดูเผิน ๆ
#          เหมือนโค้ดทำงาน จนกว่าจะสังเกตว่าเลขไต่ขึ้นแทนที่จะไต่ลง

import wifi
import lcd
import ui
import time

TOP_N = 5                      # แถวบนจอถูกจำกัดด้วยพื้นที่ ไม่ใช่ด้วยจำนวนวง
COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_OK, COL_WARN, COL_BAD = 0x00E676, 0xFFA726, 0xFF5252


def percent_of(rssi):
    # แผนที่ที่ช่างใช้กันหน้างาน: -90 dBm คือแทบไม่เหลือ -40 dBm คือเต็มขีด
    # สูตรเชิงเส้นนี้ไม่ได้แม่นยำทางฟิสิกส์ แต่เอาไว้เทียบวงต่อวงได้ดีพอ
    pct = (rssi + 90) * 2
    if pct < 0:
        return 0
    if pct > 100:
        return 100
    return pct


def verdict_of(rssi):
    # เกณฑ์นี้มาจากประสบการณ์ติดตั้งจริง ไม่ใช่จากมาตรฐานใด
    # ดีกว่า -60 คือสบาย ระหว่าง -60 ถึง -75 คือใช้ได้แต่จะสะดุดบ้าง
    if rssi >= -60:
        return "ใช้ได้สบาย"
    if rssi >= -75:
        return "พอใช้ได้"
    return "อ่อนเกินไป"


def color_of(rssi):
    # สีต้องมาจากเกณฑ์เดียวกับคำตัดสิน ไม่งั้นจอกับข้อความจะเถียงกันเอง
    if rssi >= -60:
        return COL_OK
    if rssi >= -75:
        return COL_WARN
    return COL_BAD


lcd.clear()
lcd.console("<h2>คาบ 9 - จัดอันดับความแรง</h2>")

ui.screen()
time.sleep_ms(200)
ui.Label("คาบ 9 - เรียงวงตามความแรง", x=20, y=12, color=COL_TEXT, value=24)
ui.Label("อันดับ - ชื่อวง - dBm - คำตัดสิน", x=20, y=48, color=COL_DIM, value=16)

# ป้ายนี้ต้องเกิดก่อน scan() เพราะระหว่างสแกนจอจะไม่ขยับเลย
note = ui.Label("กำลังสแกน... รอสักครู่", x=20, y=316, color=COL_WARN, value=18)
ui.poll()

nets = wifi.scan()

# key=lambda net: net[1] บอก sort ว่าให้ตัดสินด้วยช่องที่สอง ซึ่งคือ rssi
# reverse=True พลิกลำดับเป็นมากไปน้อย จึงได้วงที่แรงที่สุดขึ้นก่อน
nets.sort(key=lambda net: net[1], reverse=True)

print("อันดับ  ชื่อวง                    dBm  ความแรง")

# min() กันกรณีสแกนเจอน้อยกว่าห้าวง ถ้าไม่ใส่จะหลุด IndexError
# และมันคือตัวคุมโควตา widget ด้วย หนึ่งอันดับกิน Label กับ Bar อย่างละตัว
shown = min(TOP_N, len(nets))
for i in range(shown):
    ssid, rssi, security, channel = nets[i]
    pct = percent_of(rssi)
    col = color_of(rssi)
    y = 76 + i * 46

    # ความยาวของแท่งคือความแรง ส่วนสีคือคำตัดสิน ให้สีเดียวกันกับตัวหนังสือด้วย
    # เพราะ .color() ของ Bar ทาที่ "ราง" ไม่ใช่ส่วนที่เติม ถ้าดูแต่แท่งจะอ่านสลับได้
    ui.Label("{}. {}  {} dBm  {}".format(i + 1, ssid[:16], rssi, verdict_of(rssi)),
             x=20, y=y, color=col, value=16)
    ui.Bar(x=20, y=y + 22, w=640, h=14, min=0, max=100, value=pct, color=col)
    ui.poll()

    lcd.print("{}. {} {} dBm {}%".format(i + 1, ssid[:18], rssi, pct))
    print("  {}    {:<24} {:>4}  {:>3}%  {}".format(
        i + 1, ssid[:24], rssi, pct, verdict_of(rssi)))

# วงที่แรงที่สุดไม่ได้แปลว่าเป็นวงที่เราควรต่อ มันอาจเป็นวงของร้านข้าง ๆ
# บทเรียนคือ เลือกวงด้วยชื่อ แล้วใช้ความแรงตัดสินว่าตำแหน่งที่วางบอร์ดใช้ได้ไหม
if len(nets) > 0:
    note.color(color_of(nets[0][1]))
    note.text("แรงที่สุด: {}  {} dBm".format(nets[0][0][:18], nets[0][1]))
    # แยกสองป้ายให้อยู่ในเพดาน 126 ไบต์ของ ui.Label - ไทยหนึ่งตัวกิน 3 ไบต์
    ui.Label("เลือกวงด้วยชื่อ", x=20, y=344, color=COL_DIM, value=16)
    ui.Label("ไม่ใช่เลือกวงที่แรงสุดอัตโนมัติ", x=180, y=344, color=COL_DIM,
             value=16)
    lcd.print("แรงที่สุดคือ", nets[0][0], "ที่", nets[0][1], "dBm")
    # lcd ตัดทิ้งที่ 127 ไบต์ ภาษาไทยตัวละ 3 ไบต์ บรรทัดหนึ่งจึงได้ราว 42 ตัว
    lcd.print("เลือกด้วยชื่อวง ไม่ใช่ด้วยความแรง")
else:
    note.color(COL_BAD)
    note.text("ไม่เจอวงไหนเลย ลองย้ายบอร์ดออกที่โล่ง")

ui.poll()
