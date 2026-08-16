# 14_the_board_hears_you.py - พูดใส่บอร์ด แล้วดูมันขยับตาม
#
# ไฟล์นี้ใช้ของใหม่จากโมดูล mic สี่ชิ้น คือ start เปิดไมค์ level ให้ตัวเลข 0-100
# stats ให้ค่าดิบสามตัวพร้อมกัน และ lag บอกว่าเสียงค้างรออยู่ในคิวกี่มิลลิวินาที
# ของใหม่บนจอมีชิ้นเดียวคือ ui.DotMatrix ซึ่งเป็นตารางจุดที่เราจุดเองทีละบิต
#
# ไฟล์นี้สอน: เสียงหนึ่งวินาทีมีตัวเลขหมื่นหกพันตัว โมดูล mic ยุบมันเหลือตัวเดียว
#             ที่เอาขึ้นจอได้เลย โดยที่เราไม่ต้องรู้เรื่องอัตราสุ่มหรือดีซิเมชัน
# ดูที่จอ   : เลขความดังกับแถบทางซ้าย และมิเตอร์จุด 8x8 ทางขวาที่ไต่ขึ้นจากล่าง
#             พูดใส่บอร์ดแล้วทั้งสามอย่างขยับพร้อมกัน ตบมือแล้วตัวนับจะเดินขึ้น
# กับดัก    : ทุกครั้งที่เรียก level() หรือ stats() คือการอ่านเสียงมาหนึ่งชุดจริง ๆ
#             ถ้าลูปอ่านช้ากว่าที่ไมค์ผลิต คิวจะค้างแล้วทุกอย่างบนจอจะช้ากว่าความจริง
#             ตัวเลข "คิวค้าง" มีไว้ให้เห็นเรื่องนี้ด้วยตา ไม่ใช่ให้เดาเอา

import lcd
import mic
import time
import ui

SENS = 3             # ความไวของไมค์ 1 ถึง 5 ยิ่งมากยิ่งไวต่อเสียงเบา
RUN_MS = 40000       # เดินนานเท่าไร
TICK_MS = 80         # คาบของลูป ถี่กว่านี้ตาคนก็ตามแถบไม่ทันอยู่ดี
CLAP_PEAK = 12000    # ค่ายอดเท่าไรถึงนับว่าเป็นการตบมือ จาก 32768 เต็มสเกล
CLAP_GAP_MS = 400    # ห้ามนับซ้ำภายในกี่ ms กันเสียงก้องถูกนับเป็นครั้งที่สอง

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x00E676, 0xFFA726, 0xFF5252, 0x40C4FF


def meter_bytes(level):
    """แปลงความดัง 0-100 เป็นไบต์ที่ ui.DotMatrix เข้าใจ

    DotMatrix รับ "ไบต์ที่แพ็กแล้ว" หนึ่งบิตต่อหนึ่งดวง เรียงทีละแถวจากบนลงล่าง
    บิตซ้ายสุดของแต่ละไบต์ (0x80) คือดวงซ้ายสุดของแถวนั้น
    ตารางนี้กว้าง 8 ดวง หนึ่งแถวจึงพอดีหนึ่งไบต์ และทั้งตารางคือ 8 ไบต์
    """
    lit = level * 8 // 100
    if lit > 8:
        lit = 8
    out = bytearray(8)
    for row in range(8):
        # แถว 0 คือแถวบนสุด มิเตอร์จึงต้องไต่ขึ้นจากแถวท้าย ไม่ใช่จากแถวแรก
        if row >= 8 - lit:
            out[row] = 0xFF
    return out


ui.screen()
time.sleep_ms(200)

ui.Label("บอร์ดได้ยินเสียงคุณ", x=20, y=12, color=COL_TEXT, value=24)
status = ui.Label("กำลังเปิดไมค์", x=20, y=48, color=COL_WARN, value=18)
ui.Label("มิเตอร์จุด 8x8", x=480, y=52, color=COL_DIM, value=16)

ui.Panel(x=20, y=78, w=430, h=190, color=COL_CARD, min=COL_DIM, max=12,
         value=1)

ui.Label("ความดังตอนนี้", x=36, y=88, color=COL_DIM, value=16)
seg_now = ui.Seg7(text="0", x=36, y=112, w=170, h=60, color=COL_OK)

ui.Label("ดังสุดที่เจอ", x=226, y=88, color=COL_DIM, value=16)
seg_max = ui.Seg7(text="0", x=226, y=112, w=170, h=60, color=COL_WARN)

ui.Label("แถบเดียวกัน", x=36, y=186, color=COL_DIM, value=16)

# Bar ไม่รับ color= ตอนสร้าง ต้องเรียก .color() หลังสร้างถึงจะเปลี่ยนสีได้จริง
bar = ui.Bar(x=36, y=210, w=390, h=26, min=0, max=100, value=0)
bar.color(COL_OK)

# DotMatrix กำหนดจำนวนดวงด้วย cols= กับ rows= ไม่ใช่ด้วย w= กับ h=
# ส่วน w กับ h เป็นตัวบอกระยะห่างของดวง เพดานคือ 16x16
dots = ui.DotMatrix(x=480, y=80, w=180, h=180, cols=8, rows=8)
ui.Label("จ่อปากใกล้ ๆ แล้วพูด", x=480, y=266, color=COL_DIM, value=16)

ui.Label("คิวค้าง (ms)", x=20, y=282, color=COL_DIM, value=16)
seg_lag = ui.Seg7(text="0", x=20, y=306, w=140, h=48, color=COL_INFO)

ui.Label("ตบมือ (ครั้ง)", x=190, y=282, color=COL_DIM, value=16)
seg_clap = ui.Seg7(text="0", x=190, y=306, w=140, h=48, color=COL_OK)

ui.Label("ยอด peak", x=380, y=282, color=COL_DIM, value=16)
seg_peak = ui.Seg7(text="0", x=380, y=306, w=160, h=48, color=COL_WARN)
ui.poll()

lcd.clear()
lcd.console("<h2>บอร์ดได้ยินเสียงคุณ</h2>")
lcd.print("เปิดไมค์ที่ความไว", SENS, "จาก 5")

# start() ทิ้งเสียงสองชุดแรกให้เองแล้ว เพราะชุดแรกหลังเปิดยังไม่นิ่ง
mic.start(sens=SENS)

status.color(COL_OK)
status.text("พูดหรือตบมือใส่บอร์ดได้เลย")
ui.poll()
lcd.print("<span class=ok>ไมค์พร้อมแล้ว</span>")

t0 = time.ticks_ms()
loudest = 0
claps = 0
last_clap = -CLAP_GAP_MS

while True:
    t_work = time.ticks_ms()
    if time.ticks_diff(t_work, t0) >= RUN_MS:
        break

    # เรียกครั้งนี้ = อ่านเสียงมาหนึ่งชุดจริง ๆ แล้วยุบเหลือเลขเดียว 0-100
    # สเกลนี้เป็นสเกลของหู ไม่ใช่สเกลของเลข ห้องเงียบราว 7 พูดปกติราว 54
    lv = mic.level()

    # เรียกครั้งนี้ = อ่านอีกหนึ่งชุด ได้ค่าดิบสามตัวพร้อมกันในครั้งเดียว
    # rms คือความดังเฉลี่ย นิ่งกว่า ส่วน peak คือยอดสูงสุด ใช้จับเสียงสั้น ๆ
    # ทั้งคู่อยู่ในช่วง 0 ถึง 32768 ซึ่งเป็นสเกลเต็มของ 16 บิต
    rms_v, peak_v, dc_v = mic.stats()

    if lv > loudest:
        loudest = lv
        seg_max.text(str(loudest))

    seg_now.text(str(lv))
    seg_peak.text(str(peak_v))
    bar.value(lv)
    bar.color(COL_BAD if lv >= 85 else (COL_WARN if lv >= 45 else COL_OK))
    dots.set_pixels(meter_bytes(lv))

    # lag() ไม่ได้อ่านเสียงเพิ่ม มันแค่ถามว่าตอนนี้มีเสียงค้างรออยู่กี่ ms
    # ถ้าเลขนี้ไต่ขึ้นเรื่อย ๆ แปลว่าลูปเราอ่านช้ากว่าที่ไมค์ผลิต
    # แล้วทุกอย่างบนจอจะช้ากว่าความจริงเท่าเลขนี้ตลอดไป
    lag_ms = mic.lag()
    seg_lag.text(str(lag_ms))
    seg_lag.color(COL_BAD if lag_ms > 200 else COL_INFO)

    # นับตบมือจากค่ายอด ไม่ใช่จากค่าเฉลี่ย เพราะการตบมือคือเสียงสั้นและแรง
    # ซึ่งค่าเฉลี่ยของทั้งชุดจะกลบมันไปเกือบหมด
    now = time.ticks_ms()
    if peak_v >= CLAP_PEAK and time.ticks_diff(now, last_clap) >= CLAP_GAP_MS:
        last_clap = now
        claps = claps + 1
        seg_clap.text(str(claps))
        status.color(COL_BAD)
        status.text("ได้ยินเสียงตบมือ ครั้งที่ " + str(claps))
        lcd.print("<span class=ok>ตบมือครั้งที่", claps, "- peak", peak_v,
                  "</span>")
    elif lv < 20:
        status.color(COL_DIM)
        status.text("ห้องเงียบ - ความดัง " + str(lv))
    else:
        status.color(COL_OK)
        status.text("มีเสียง - ความดัง " + str(lv))

    ui.poll()

    work = time.ticks_diff(time.ticks_ms(), t_work)
    left = TICK_MS - work
    if left > 0:
        time.sleep_ms(left)

mic.stop()

# จบแล้วปล่อยค่าสุดท้ายค้างไว้ ไม่ล้างจอ คนดูจะได้อ่านทัน
dots.set_pixels(meter_bytes(0))
bar.value(0)
status.color(COL_DIM)
status.text("ปิดไมค์แล้ว - ตบมือ " + str(claps) + " ครั้ง")
ui.poll()

lcd.console("<span class=muted>------------------------</span>")
lcd.print("<span class=ok>ดังสุด", loudest, "จาก 100 | ตบมือ", claps,
          "ครั้ง</span>")
print("ระดับสูงสุด", loudest, "| ตบมือ", claps, "ครั้ง")

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# ตั้ง TICK_MS = 500 แล้วรันใหม่ พูดสั้น ๆ หนึ่งคำแล้วหยุด จ้องที่เลขคิวค้าง
# แล้วตอบว่าแถบบนจอขยับตอนที่คุณพูด หรือขยับหลังจากคุณพูดจบไปแล้ว
# ใบ้: ไมค์ผลิตเสียงตลอดเวลาไม่ว่าเราจะอ่านหรือไม่ ของที่อ่านไม่ทันไม่ได้หายไป
#      มันไปต่อคิวรออยู่ แล้วรอบหน้าเราจะได้ของเก่าก่อนของใหม่เสมอ
