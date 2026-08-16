# 07_mic_window_stats.py - รูปคลื่นดิบ สามค่าจากหน้าต่างเดียว และคิวที่ค้างอยู่
# ชุดตัวอย่างประจำคาบ 08
#
# ไฟล์นี้สอน: mic.stats() คืนสามค่าจากหน้าต่างเดียวกัน ต่างจากเรียก rms() แล้ว peak()
#             ติดกัน ซึ่งเป็นการอ่านสองหน้าต่างคนละช่วงเวลา และจ่ายค่าอ่านสองรอบ
#             ส่วน mic.lag() บอกว่าคิวเสียงค้างอยู่กี่มิลลิวินาที ตัวเลขนั้นคือ
#             ความหน่วงที่ตาเราเห็นบนจอ ไม่ใช่ตัวประมาณ
# ดูที่จอ   : กราฟบนคือรูปคลื่นดิบจาก read() แถบขวาคือคิวที่ค้าง 0-625 ms
#             กดปุ่มซ้ายสลับ fresh แล้วดูแถบคิว: True แถบเกือบว่าง False แถบเต็ม
#             กดปุ่มขวาถ่วงลูปให้ช้าลง แล้วดูว่าแถบคิวโตขึ้นเร็วแค่ไหนตอน fresh ปิด
# กับดัก    : read() ใช้ทางที่ไม่ทิ้งของค้างเสมอ กราฟรูปคลื่นจึงเป็นของเก่ากว่าตัวเลข
#             ทางขวาได้ ถ้าลูปช้า - เรียก stats() นำหน้าเพื่อล้างคิวก่อนจึงจะตรงกัน
#             และ read(1000) จากบัฟเฟอร์ 256 คืน 256 ไม่ใช่ 1000 มันไม่รอเก็บเพิ่มให้
#
# บน Eva Kit: mic ฝังมากับเฟิร์มแวร์ import ได้เลย ไม่ต้องคัดลอกไฟล์ไปไว้บนบอร์ด

import lcd
import mic
import time
import ui

SENS = 3           # ความไวของไมค์ 1 ถึง 5 นอกช่วงนี้จะตกกลับไปเป็น 3 เงียบ ๆ
SAMPLES = 256      # ขนาดหน้าต่าง ที่ 16 kHz คือ 16 ms ของเวลาจริง ลดไม่ได้
PLOT_N = 50        # หน้าต่างของ ui.Chart กว้าง 50 จุด ป้อนเท่านี้ = แทนที่หมด
RING_MS = 625      # ความจุของคิวเสียง เอามาเป็นขอบบนของแถบ
SLOW_MS = 300      # ถ่วงลูปเท่านี้ตอนกดปุ่มขวา ให้เห็นคิวโตชัด ๆ

ui.screen()
ui.Label("รูปคลื่นดิบ สามค่า และคิวเสียง", x=12, y=6, value=24)

# แกน Y กว้างพอสำหรับเสียงพูด ค่าที่ดังกว่านี้จะถูกหนีบก่อนป้อน ไม่ปล่อยให้ชนขอบเงียบ ๆ
ch = ui.Chart(x=12, y=44, w=470, h=170, min=-8000, max=8000)

ui.Label("rms peak dc จากครั้งเดียว", x=500, y=48, value=16,
         color=0xA0B4CC)
lbl_rms = ui.Label("rms 0", x=500, y=74, value=20, color=0x00BFFF)
lbl_peak = ui.Label("peak 0", x=500, y=106, value=20, color=0xFFC83D)
lbl_dc = ui.Label("dc 0", x=500, y=138, value=20, color=0x9AA0A6)

ui.Label("คิวค้างอยู่กี่ ms (เต็มที่ 625)", x=500, y=172, value=16,
         color=0xA0B4CC)
bar_lag = ui.Bar(x=500, y=196, w=250, h=18, min=0, max=RING_MS)
lbl_lag = ui.Label("lag 0 ms", x=500, y=218, value=20, color=0x55DD55)

btn_fresh = ui.Button("fresh = True", x=12, y=236, w=220, h=52,
                      color=0x2E7D32, value=20)
btn_slow = ui.Button("ลูปปกติ", x=248, y=236, w=220, h=52,
                     color=0x546E7A, value=20)
ID_FRESH, ID_SLOW = btn_fresh.id(), btn_slow.id()

lbl_state = ui.Label("กำลังเปิดไมค์", x=12, y=300, value=20)
lbl_hint = ui.Label("fresh ปิดเมื่อไร คิวจะไต่ขึ้นจนเต็ม 625", x=12, y=336,
                    value=16, color=0x90A4AE)
ui.poll()

lcd.clear()
lcd.console("<h2>รูปคลื่นดิบ สามค่า และคิวเสียง</h2>")
lcd.print("หน้าต่าง", SAMPLES, "ตัวอย่างที่ 16 kHz = 16 ms ของเวลาจริง")

mic.start(sens=SENS, samples=SAMPLES)
lbl_state.text("พูดใส่บอร์ดได้เลย")
lcd.print("ไมค์พร้อม กดปุ่มซ้ายสลับ fresh แล้วเทียบแถบคิว")

fresh = True
slow = False
loudest = 0

for _ in range(1500):
    t0 = time.ticks_ms()

    # เรียกครั้งเดียวได้ครบสามค่า และทั้งสามมาจากหน้าต่างเดียวกันจริง ๆ
    # ถ้าแยกเรียก rms() แล้ว peak() จะได้คนละหน้าต่าง และจ่ายค่าอ่านสองรอบ
    rms, peak, dc = mic.stats(fresh=fresh)

    # lag() ต้องอ่านหลัง stats() เพราะ fresh=True เพิ่งทิ้งของค้างไปเมื่อกี้
    # อ่านก่อนจะได้ตัวเลขของรอบที่แล้ว ซึ่งตอบคนละคำถาม
    queued = mic.lag()

    # read() ไม่ทิ้งของค้าง รูปคลื่นจึงเก่ากว่าตัวเลขข้างบนได้ถ้าลูปช้า
    # นั่นไม่ใช่บั๊ก มันคือสิ่งที่หัวไฟล์เตือนไว้ ให้ดูแถบคิวประกอบเสมอ
    wave = mic.read(PLOT_N)
    for v in wave:
        ch.set_next(0, v if -8000 < v < 8000 else (8000 if v > 0 else -8000))

    if peak > loudest:
        loudest = peak

    lbl_rms.text("rms %d" % rms)
    lbl_peak.text("peak %d" % peak)
    lbl_dc.text("dc %d" % dc)
    bar_lag.value(queued if queued < RING_MS else RING_MS)
    lbl_lag.text("lag %d ms" % queued)
    # คิวเกินครึ่งความจุ = สิ่งที่เห็นเป็นห้องเมื่อเสี้ยววินาทีที่แล้ว เตือนด้วยสี
    lbl_lag.color(0x55DD55 if queued < RING_MS // 2 else 0xFF5555)

    took = time.ticks_diff(time.ticks_ms(), t0)
    lbl_state.text("รอบนี้ %d ms | ยอดสูงสุด %d" % (took, loudest))

    for ev in ui.poll():
        if ev["type"] != "clicked":
            continue
        if ev["handle"] == ID_FRESH:
            fresh = not fresh
            btn_fresh.text("fresh = True" if fresh else "fresh = False")
            btn_fresh.color(0x2E7D32 if fresh else 0xC62828)
            lcd.print("fresh = " + str(fresh) + " | คิวตอนนี้ " +
                      str(queued) + " ms")
        elif ev["handle"] == ID_SLOW:
            slow = not slow
            btn_slow.text("ถ่วงลูป 300 ms" if slow else "ลูปปกติ")
            lcd.print("ลูปถ่วง" if slow else "ลูปปกติ")

    if slow:
        time.sleep_ms(SLOW_MS)

mic.stop()
lbl_state.text("ปิดไมค์แล้ว")
lcd.print("ยอด peak สูงสุดที่เจอ", loudest, "จาก 32768")
lcd.print("<span class=muted>ปิด fresh แล้วถ่วงลูป คือวิธีเห็นคิว 625 ms เต็ม ๆ</span>")
print("stats() ครั้งเดียวได้สามค่าจากหน้าต่างเดียวกัน แยกเรียกได้คนละหน้าต่าง")
