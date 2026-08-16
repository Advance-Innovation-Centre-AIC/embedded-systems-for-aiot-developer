# 12_every_sense_at_once.py - คำสั่งเดียว ได้ทุกเซนเซอร์พร้อมกัน
#
# ไฟล์นี้ใช้ของใหม่ชิ้นเดียว คือ sensors.snapshot() ที่คืน dict ก้อนหนึ่ง
# ข้างในมีสามช่อง bmi270 คือเซนเซอร์ความเคลื่อนไหว capsense คือแผ่นสัมผัส
# pot คือลูกบิดหมุน ทั้งสามอ่านมาจากเวลาเดียวกัน จึงเอามาเทียบกันได้
#
# ไฟล์นี้สอน: บนบอร์ดนี้ Python ไม่ได้ไปอ่านเซนเซอร์เอง มันขอจากคอร์ CM55
#             ที่เป็นเจ้าของบัส แล้ว CM55 ส่งค่าของทั้งชุดกลับมาให้ในทีเดียว
# ดูที่จอ   : สามการ์ดขยับพร้อมกัน เอียงบอร์ดแล้วตัวเลข az เปลี่ยน แตะแผ่นสัมผัส
#             แล้วสองบรรทัดบนการ์ดกลางเปลี่ยน หมุนลูกบิดแล้ววงแหวนขวากวาดตาม
#             บรรทัดล่างสุดคือตัวนับรอบ ถ้ามันเดินขึ้น แปลว่าค่าที่เห็นสดจริง
# กับดัก    : sensors.init() กับ sensors.scan() บนบอร์ดนี้ปฏิเสธด้วย OSError
#             ไม่ใช่ของเสีย แต่เป็นการกันไม่ให้เราไปแย่งบัสกับ CM55 จนบอร์ดค้าง
#             หลังรีเซ็ต CM55 ตอบเรื่องเซนเซอร์ช้าได้ถึงราว 13 วินาที ลูปจึงต้องดักไว้

import lcd
import sensors
import time
import ui

RUN_MS = 30000       # เฝ้าดูนานเท่าไร
TICK_MS = 200        # ถามซ้ำทุกกี่ ms - เท่ากับคาบที่ CM55 อ่านเซนเซอร์พอดี

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x00E676, 0xFFA726, 0xFF5252, 0x40C4FF

ui.screen()
time.sleep_ms(200)

ui.Label("อ่านทุกเซนเซอร์ด้วยคำสั่งเดียว", x=20, y=12, color=COL_TEXT,
         value=24)
ui.Label("sensors.snapshot() คืน dict ก้อนเดียว", x=20, y=48, color=COL_DIM,
         value=18)

# --- การ์ดซ้าย: เซนเซอร์ความเคลื่อนไหว ---
ui.Panel(x=20, y=78, w=240, h=180, color=COL_CARD, min=COL_DIM, max=12,
         value=1)
ui.Label("IMU  BMI270", x=36, y=88, color=COL_INFO, value=16)
ax_lbl = ui.Label("ax  รอค่า", x=36, y=116, color=COL_TEXT, value=18)
ay_lbl = ui.Label("ay  รอค่า", x=36, y=144, color=COL_TEXT, value=18)
az_lbl = ui.Label("az  รอค่า", x=36, y=172, color=COL_TEXT, value=18)
ui.Label("เอียงบอร์ดแล้วดู az", x=36, y=206, color=COL_DIM, value=16)

# --- การ์ดกลาง: แผ่นสัมผัส ---
ui.Panel(x=276, y=78, w=240, h=180, color=COL_CARD, min=COL_DIM, max=12,
         value=1)
ui.Label("CapSense", x=292, y=88, color=COL_INFO, value=16)
btn_lbl = ui.Label("ปุ่ม  รอค่า", x=292, y=116, color=COL_TEXT, value=18)
ui.Label("แถบเลื่อน 0-100", x=292, y=150, color=COL_DIM, value=16)

# Bar ไม่รับ color= ตอนสร้าง ต้องเรียก .color() หลังสร้างถึงจะเปลี่ยนสีได้จริง
slide_bar = ui.Bar(x=292, y=176, w=200, h=22, min=0, max=100, value=0)
slide_bar.color(COL_INFO)
slide_lbl = ui.Label("slider  รอค่า", x=292, y=206, color=COL_TEXT, value=18)

# --- การ์ดขวา: ลูกบิดหมุน ---
ui.Panel(x=532, y=78, w=240, h=180, color=COL_CARD, min=COL_DIM, max=12,
         value=1)
ui.Label("ลูกบิด  pot", x=548, y=88, color=COL_INFO, value=16)

# Arc ก็ไม่รับ color= ตอนสร้างเหมือนกัน และถ้าไม่ใส่ w จะได้ 150x150 มาเลย
arc = ui.Arc(x=556, y=112, w=104, h=104, min=0, max=100, value=0)
arc.color(COL_OK)
pot_lbl = ui.Label("pot  รอค่า", x=548, y=222, color=COL_TEXT, value=18)

# ตัวนับรอบของ CM55 สามตัว ถ้ามันเดินขึ้นแปลว่าค่าที่เห็นสดจริง ไม่ใช่ค่าค้าง
seq_lbl = ui.Label("ตัวนับรอบ  ยังไม่ได้อ่าน", x=20, y=276, color=COL_DIM,
                   value=18)
state = ui.Label("กำลังขอค่าชุดแรกจาก CM55", x=20, y=306, color=COL_WARN,
                 value=18)
ui.Label("เอียง แตะ หมุน แล้วดูสามการ์ดขยับพร้อมกัน", x=20, y=340,
         color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>อ่านทุกเซนเซอร์ด้วยคำสั่งเดียว</h2>")

# กับดักของบอร์ดนี้ ประกาศไว้ตั้งแต่บรรทัดแรกของลิ้นชักเลย
# มันไม่ใช่ข้อจำกัด มันคือสิ่งที่แพลตฟอร์มทำแทนเราไปแล้ว
lcd.console("<span class=muted>sensors.init() และ sensors.scan() บอร์ดนี้"
            " ปฏิเสธด้วย OSError</span>")
lcd.print("CM55 เป็นเจ้าของบัส เราขอค่าจากมันแทน ไม่ต้องเปิดบัสเอง")

t0 = time.ticks_ms()
rounds = 0
misses = 0
ready = False
last_seq = -1

while True:
    t_work = time.ticks_ms()
    elapsed = time.ticks_diff(t_work, t0)
    if elapsed >= RUN_MS:
        break

    rounds = rounds + 1

    # หลังรีเซ็ต CM55 ยังไม่พร้อมตอบเรื่องเซนเซอร์อยู่ราว 13 วินาที
    # ช่วงนั้น snapshot() โยน OSError ออกมา ไม่ได้แปลว่าโค้ดผิด
    # โปรแกรมที่ไม่ดักไว้จะตายคาบรรทัดแรกทุกครั้งที่เพิ่งเสียบไฟ
    try:
        s = sensors.snapshot()
    except OSError:
        misses = misses + 1
        state.color(COL_WARN)
        state.text("CM55 ยังไม่ตอบ - พลาดไป " + str(misses) + " รอบ")
        ui.poll()
        time.sleep_ms(TICK_MS)
        continue

    if not ready:
        ready = True
        state.color(COL_OK)
        lcd.print("<span class=ok>ได้ค่าชุดแรกแล้วที่รอบ", rounds, "</span>")
        # คีย์ในก้อนนี้มีเท่าที่ CM55 มีให้ ไม่ได้มีครบเสมอ
        # จึงต้องถามด้วย in ก่อนอ่าน ไม่ใช่อ่านตรง ๆ แล้วหวังว่าจะมี
        for k in s:
            lcd.print("  ได้คีย์", k)

    seq_txt = ""

    # --- ช่องที่ 1: เซนเซอร์ความเคลื่อนไหว ---
    if "bmi270" in s:
        m = s["bmi270"]
        # ค่าเร่งเป็น m/s^2 แกน z จะราว 9.8 ตอนวางราบ และตกลงตอนตะแคง
        ax_lbl.text("ax  " + str(round(m["ax"], 2)))
        ay_lbl.text("ay  " + str(round(m["ay"], 2)))
        az_lbl.text("az  " + str(round(m["az"], 2)))
        az_lbl.color(COL_OK if m["az"] > 8.0 else COL_WARN)
        seq_txt = seq_txt + "imu " + str(m["sequence"])

    # --- ช่องที่ 2: แผ่นสัมผัส ---
    if "capsense" in s:
        c = s["capsense"]
        # btn0 กับ btn1 เป็น True/False อยู่แล้ว ไม่ต้องแปลงอะไรก่อนใช้
        left = "แตะ" if c["btn0"] else "ว่าง"
        right = "แตะ" if c["btn1"] else "ว่าง"
        btn_lbl.text("ปุ่ม  " + left + " / " + right)
        btn_lbl.color(COL_OK if (c["btn0"] or c["btn1"]) else COL_TEXT)

        slide_bar.value(c["slider"])
        slide_lbl.text("slider  " + str(c["slider"]))
        seq_txt = seq_txt + "  touch " + str(c["sequence"])

    # --- ช่องที่ 3: ลูกบิดหมุน ---
    if "pot" in s:
        p = s["pot"]
        # percent เป็นทศนิยม แต่ Arc รับเฉพาะจำนวนเต็ม ต้องแปลงก่อนส่ง
        pct = int(p["percent"])
        arc.value(pct)
        pot_lbl.text("pot  " + str(pct) + " %")
        seq_txt = seq_txt + "  pot " + str(p["sequence"])

        if p["sequence"] != last_seq:
            last_seq = p["sequence"]

    seq_lbl.text("ตัวนับรอบ  " + seq_txt)
    state.text("อ่านไปแล้ว " + str(rounds) + " รอบ - พลาด " + str(misses))

    ui.poll()

    work = time.ticks_diff(time.ticks_ms(), t_work)
    left_ms = TICK_MS - work
    if left_ms > 0:
        time.sleep_ms(left_ms)

# จบแล้วปล่อยค่าสุดท้ายค้างไว้ ไม่ล้างจอ คนดูจะได้อ่านทัน
state.color(COL_DIM)
state.text("จบแล้ว - อ่าน " + str(rounds) + " รอบ พลาด " + str(misses))
ui.poll()

lcd.console("<span class=muted>------------------------</span>")
lcd.print("<span class=ok>อ่าน", rounds, "รอบ | พลาด", misses, "รอบ</span>")

# ก้อน dict ทั้งก้อนยาวเกินกว่าจะอ่านบนจอ 4.3 นิ้ว จึงส่งไปคอนโซลฝั่งคอมแทน
try:
    print("snapshot() ก้อนสุดท้าย =", sensors.snapshot())
except OSError as e:
    print("snapshot() ปฏิเสธ:", e)

# ลองเรียกท่าที่บอร์ดนี้ไม่ให้เรียก แล้วอ่านเหตุผลที่มันบอกกลับมาเอง
# เฟิร์มแวร์ไม่ได้เงียบแล้วปล่อยให้บอร์ดค้าง มันอธิบายและชี้ทางที่ถูกให้ด้วย
try:
    sensors.init()
except OSError as e:
    print("sensors.init() ปฏิเสธว่า:", e)

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# ตั้ง TICK_MS = 1000 แล้วรันใหม่ จ้องที่บรรทัดตัวนับรอบ แล้วตอบว่าตัวเลขกระโดด
# ทีละเท่าไร เทียบกับตอน TICK_MS = 200 ที่มันเดินทีละหนึ่ง
# ใบ้: CM55 อ่านเซนเซอร์ทุก 200 ms ไม่ว่าเราจะถามหรือไม่ถาม ตัวนับจึงเดินของมันเอง
