# 13_raw_and_filtered.py - เส้นดิบที่สั่น กับเส้นเดียวกันที่นิ่ง อยู่บนกราฟใบเดียว
#
# ไฟล์นี้ใช้ของใหม่จากโมดูล dsp สามชิ้น คือ dsp.EMA ที่เฉลี่ยแบบถ่วงน้ำหนัก
# dsp.Median ที่หยิบค่ากลางของหน้าต่าง และ dsp.tilt ที่แปลงค่าเร่งสามแกน
# เป็นมุมเอียงสองมุม ทั้งสามกินค่าดิบจากเซนเซอร์ตัวเดียวกันที่เพิ่งเจอในไฟล์ 12
#
# ไฟล์นี้สอน: ค่าจากเซนเซอร์จริงไม่เคยนิ่ง การกรองไม่ได้ทำให้ค่าถูกขึ้น
#             แต่ทำให้ค่าที่รายงานออกไปใช้ตัดสินใจได้ โดยไม่ต้องเขียนสูตรเอง
# ดูที่จอ   : สามเส้นบนกราฟใบเดียว เทาคือค่าดิบ เขียวคือ EMA ส้มคือ Median
#             วางบอร์ดนิ่ง ๆ สามเส้นจะทาบกัน พอเคาะโต๊ะหรือเขย่า เส้นเทาจะพุ่ง
#             ส่วนอีกสองเส้นค่อย ๆ ตามขึ้นไปแล้วกลับลงมาอย่างเรียบ
# กับดัก    : Chart รับเฉพาะจำนวนเต็ม ส่งทศนิยมเข้าไปจะได้ TypeError
#             ค่าเร่งเป็นทศนิยมราว 9.8 จึงต้องคูณสิบแล้วแปลงเป็น int ก่อนส่ง
#             และแกนตั้งของกราฟตั้งตอนสร้างเท่านั้น เปลี่ยนทีหลังไม่ได้

import dsp
import lcd
import math
import sensors
import time
import ui

RUN_MS = 40000       # เดินนานเท่าไร
TICK_MS = 100        # อ่านถี่แค่ไหน
ALPHA = 0.15         # EMA ยิ่งน้อยยิ่งนิ่งแต่ยิ่งตามช้า ค่าตั้งต้นของ dsp คือ 0.1
WINDOW = 7           # หน้าต่างของ Median ต้องเป็นเลขคี่ ถ้าใส่คู่มันบวกหนึ่งให้เอง
CHART_MAX = 300      # เพดานแกนตั้ง เป็นค่าเร่งคูณสิบ ราบนิ่งอยู่แถว 98

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_RAW = 0x4A9EFF
COL_OK, COL_WARN, COL_BAD = 0x30A46C, 0xF5A623, 0xE5484D

# ตัวกรองของ dsp รับอาร์กิวเมนต์แบบคีย์เวิร์ดเท่านั้น เขียน dsp.EMA(0.15)
# จะได้ TypeError ทันที ต้องเขียนชื่อพารามิเตอร์กำกับเสมอ
ema = dsp.EMA(alpha=ALPHA)
med = dsp.Median(window=WINDOW)

ui.screen()
time.sleep_ms(200)

ui.Label("ค่าดิบกับค่าที่กรองแล้ว บนกราฟใบเดียว", x=24, y=24, color=COL_TEXT,
         value=24)
status = ui.Label("กำลังขอค่าชุดแรก", x=24, y=72, color=COL_WARN, value=20)

# สีที่ใส่ตอนสร้าง Chart คือสีของเส้นที่ 0 ซึ่งกราฟมีมาให้ตั้งแต่เกิดแล้ว
# add_series() ตัวแรกจึงคืนเลข 1 ไม่ใช่ 0 เก็บเลขที่มันคืนมาไว้ อย่าเดาเอง
# ใส่ได้ทั้งหมดสี่เส้น รวมเส้นที่ 0 ด้วย
chart = ui.Chart(x=24, y=112, w=448, h=200, color=COL_RAW, min=0, max=CHART_MAX)
s_ema = chart.add_series(COL_OK)
s_med = chart.add_series(COL_WARN)

# การ์ดตัวเลขอยู่คอลัมน์ขวา หนึ่งแถวคือหนึ่งค่า ป้ายซ้าย ตัวเลขขวา
# Panel ต้องถูกสร้างก่อนของที่จะวางบนมัน LVGL วาดตามลำดับการสร้าง
# ถ้าสร้างการ์ดทีหลัง มันจะทาทับตัวเลขจนหายไปทั้งใบโดยไม่มี error
ui.Panel(x=488, y=112, w=280, h=228, color=COL_CARD, min=COL_DIM, max=12,
         value=1)

ui.Label("ค่าดิบ x10", x=504, y=140, color=COL_DIM, value=16)
seg_raw = ui.Seg7(text="0", x=632, y=128, w=120, h=48, color=COL_RAW)

ui.Label("EMA x10", x=504, y=196, color=COL_DIM, value=16)
seg_ema = ui.Seg7(text="0", x=632, y=184, w=120, h=48, color=COL_OK)

ui.Label("Median x10", x=504, y=252, color=COL_DIM, value=16)
seg_med = ui.Seg7(text="0", x=632, y=240, w=120, h=48, color=COL_WARN)

# ป้ายหัวช่องใช้ 16 ได้ แต่ตัวเลขที่คนต้องอ่านต้องไม่ต่ำกว่า 20
# สามค่าบนใช้ Seg7 ซึ่งตายตัวที่ 28 อยู่แล้ว เหลือมุมเอียงที่เป็น Label ธรรมดา
ui.Label("เอียง องศา", x=504, y=296, color=COL_DIM, value=16)
tilt_lbl = ui.Label("รอค่า", x=632, y=292, color=COL_TEXT, value=20)

ui.Label("เทา = ค่าดิบ", x=24, y=324, color=COL_RAW, value=16)
ui.Label("เขียว = EMA", x=176, y=324, color=COL_OK, value=16)
ui.Label("ส้ม = Median", x=328, y=324, color=COL_WARN, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>ค่าดิบกับค่าที่กรองแล้ว</h2>")
lcd.print("EMA alpha =", ALPHA, "| Median window =", WINDOW)
lcd.print("<span class=muted>เขย่าบอร์ดหรือเคาะโต๊ะ แล้วดูสามเส้นแยกกัน</span>")

t0 = time.ticks_ms()
rounds = 0
misses = 0

# ค่าสูงสุดที่แต่ละเส้นเคยไปถึง คือหลักฐานว่าการกรองทำอะไรให้จริง
peak_raw = 0
peak_ema = 0

while True:
    t_work = time.ticks_ms()
    if time.ticks_diff(t_work, t0) >= RUN_MS:
        break

    # หลังรีเซ็ต CM55 ยังไม่พร้อมตอบอยู่พักหนึ่ง ไม่ดักไว้แล้วโปรแกรมตายคาบรรทัดแรก
    try:
        ax, ay, az = sensors.bmi270.acceleration()
    except OSError:
        misses = misses + 1
        status.color(COL_WARN)
        status.text("CM55 ยังไม่ตอบ - พลาดไป " + str(misses) + " รอบ")
        ui.poll()
        time.sleep_ms(TICK_MS)
        continue

    rounds = rounds + 1

    # ขนาดของค่าเร่งรวมสามแกน วางราบนิ่ง ๆ จะได้ราว 9.8 คือแรงโน้มถ่วงล้วน ๆ
    # ใช้ค่านี้เพราะมันไม่สนใจว่าบอร์ดหันทางไหน สนใจแค่ว่าถูกกระแทกแรงแค่ไหน
    mag = math.sqrt(ax * ax + ay * ay + az * az)

    # ป้อนค่าเดียวกันเข้าตัวกรองทั้งสอง แล้วเทียบผลกันบนกราฟใบเดียว
    # นี่คือทั้งบทเรียนของไฟล์นี้ ตัวแปรควบคุมคือค่าดิบที่เข้าไปเหมือนกันเป๊ะ
    v_ema = ema.update(mag)
    v_med = med.update(mag)

    # Chart รับเฉพาะจำนวนเต็ม คูณสิบก่อนแล้วอ่านแกนตั้งเป็นสิบเท่าของ m/s^2
    i_raw = int(mag * 10)
    i_ema = int(v_ema * 10)
    i_med = int(v_med * 10)

    chart.set_next(0, i_raw)          # เส้นที่ 0 มีมาให้ตั้งแต่สร้างกราฟ
    chart.set_next(s_ema, i_ema)
    chart.set_next(s_med, i_med)

    seg_raw.text(str(i_raw))
    seg_ema.text(str(i_ema))
    seg_med.text(str(i_med))

    if i_raw > peak_raw:
        peak_raw = i_raw
    if i_ema > peak_ema:
        peak_ema = i_ema

    # dsp.tilt() แปลงค่าเร่งสามแกนเป็นมุมสองมุม เป็นองศา ไม่ใช่เรเดียน
    # เขียนสูตรเองก็ได้ แต่โมดูลนี้เขียนไว้ให้แล้วและถูกต้องกว่าที่เราจะรีบเขียน
    roll, pitch = dsp.tilt(ax, ay, az)
    tilt_lbl.text("r " + str(int(roll)) + "  p " + str(int(pitch)))

    if i_raw > 150:
        status.color(COL_BAD)
        status.text("โดนกระแทก - ดูว่าเส้นเขียวกับส้มพุ่งตามไหม")
    else:
        status.color(COL_OK)
        status.text("รอบที่ " + str(rounds) + " - สามเส้นทาบกันตอนวางนิ่ง")

    ui.poll()

    work = time.ticks_diff(time.ticks_ms(), t_work)
    left = TICK_MS - work
    if left > 0:
        time.sleep_ms(left)

# จบแล้วปล่อยค่าสุดท้ายค้างไว้ ไม่ล้างจอ คนดูจะได้อ่านทัน
status.color(COL_DIM)
status.text("จบแล้ว - ยอดดิบ " + str(peak_raw) + " ยอด EMA " + str(peak_ema))
ui.poll()

lcd.console("<span class=muted>------------------------</span>")
lcd.print("อ่าน", rounds, "รอบ | พลาด", misses, "รอบ")
lcd.print("<span class=ok>ยอดสูงสุด ดิบ", peak_raw, "| EMA", peak_ema, "</span>")

# ตัวเลขสองตัวนี้คือคำตอบว่าการกรองแลกอะไรกับอะไร
# EMA ตัดยอดแหลมทิ้ง ซึ่งดีถ้ายอดนั้นคือสัญญาณรบกวน และแย่ถ้ายอดนั้นคือเหตุการณ์จริง
print("ยอดดิบ", peak_raw, "| ยอด EMA", peak_ema,
      "| EMA กดยอดลง", peak_raw - peak_ema)

# ----- ตาคุณ แก้แล้วรันใหม่ -----
# ตั้ง ALPHA = 0.9 แล้วรันใหม่ เคาะโต๊ะแรง ๆ เท่าเดิม แล้วตอบว่าเส้นเขียวเปลี่ยนไป
# ทางไหน จากนั้นตั้ง ALPHA = 0.02 แล้วทำแบบเดิมอีกครั้ง
# ใบ้: ตัวเลขนี้คือน้ำหนักที่ให้กับค่าใหม่ ยิ่งมากยิ่งเชื่อค่าล่าสุด ยิ่งน้อยยิ่งเชื่อของเก่า
#      ถามตัวเองว่าเครื่องจักรที่ต้องหยุดทันทีเมื่อสั่นผิดปกติ ควรใช้ค่าไหน
