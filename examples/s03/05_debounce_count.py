# 05_debounce_count.py - นับการกดให้ตรง ด้วยการรอให้ปุ่มนิ่งก่อน
#
# ไฟล์นี้สอน: การกันเด้งคือกฎข้อเดียวว่า "ค่าต้องนิ่งครบเวลาหนึ่งก่อน เราถึงจะเชื่อ"
# ดูที่จอ   : เลขสองตัววิ่งคู่กัน ส้มคือนับดิบ เขียวคือนับกันเด้ง กับบรรทัดส่วนต่าง
#             กดสิบครั้งช้า ๆ แล้วกดรัว ๆ อีกสิบครั้ง เทียบสองรอบว่าส่วนต่างต่างกันไหม
# กับดัก    : ถ้าลืม if stable: ตอนนับ การปล่อยปุ่มก็จะถูกนับด้วย เลขจะเพิ่มทีละสอง
#             ทุกครั้งที่กด และมันดูเหมือนปุ่มเด้ง ทั้งที่เป็นบั๊กของตรรกะเรา

import gpio
import lcd
import time
import ui

DEBOUNCE_MS = 40     # ต้องนิ่งนานเท่านี้ก่อนเราจะเชื่อ
POLL_MS = 5          # ถามปุ่มถี่แค่ไหน
RUN_MS = 20000

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_OK, COL_WARN, COL_INFO = 0x30A46C, 0xF5A623, 0x4A9EFF

lcd.clear()
lcd.console("<h2>นับดิบ เทียบ นับกันเด้ง</h2>")
lcd.print("<span class=muted>กดปุ่มบนบอร์ดสิบครั้งช้า ๆ</span>")

btn = gpio.button(0)

ui.screen()
time.sleep_ms(200)

ui.Label("นับดิบ เทียบ นับกันเด้ง", x=20, y=12, color=COL_TEXT, value=24)

# สองตัวเลขวางคู่กัน ส้มซ้ายคือนับดิบ เขียวขวาคือนับกันเด้ง
seg_raw = ui.Seg7(text="0", x=40, y=60, w=140, h=56, color=COL_WARN)
seg_clean = ui.Seg7(text="0", x=380, y=60, w=140, h=56, color=COL_OK)

# ป้ายเดียวบอกทั้งว่าเลขไหนเป็นของใคร และส่วนต่างตอนนี้เท่าไร
l_diff = ui.Label("ส้ม = ดิบ | เขียว = กันเด้ง", x=40, y=136, color=COL_INFO,
                  value=20)

# ค่าคงที่เขียนไว้บนจอ ไม่ใช่ในคอมเมนต์ เพราะคนที่กำลังกดปุ่มอยู่ไม่ได้เปิดโค้ดดูไปด้วย
cfg = ui.Label("DEBOUNCE_MS กับ POLL_MS", x=20, y=188, color=COL_DIM,
               value=16)
cfg.text("DEBOUNCE_MS = " + str(DEBOUNCE_MS) + " ms | POLL_MS = " +
         str(POLL_MS) + " ms")

st = ui.Label("กดช้า ๆ สิบครั้ง", x=20, y=228, color=COL_INFO, value=20)
st.text("กดช้า ๆ สิบครั้ง แล้วกดรัว ๆ อีกสิบครั้ง")

raw_count = 0        # นับทุกครั้งที่เห็นขอบขาลง โดยไม่กรองอะไรเลย
clean_count = 0      # นับเฉพาะที่ผ่านการกันเด้ง

last_raw = btn.is_pressed()      # ค่าดิบรอบก่อน
stable = last_raw                # ค่าที่เชื่อแล้ว

t0 = time.ticks_ms()
last_change = t0                 # ครั้งล่าสุดที่ค่าดิบขยับ


def show_counts():
    """เขียนเลขทั้งสองฝั่งและส่วนต่างลงป้ายเดียว ให้เทียบได้ในสายตาเดียว"""
    l_diff.text("ส้ม = ดิบ " + str(raw_count) + " | เขียว = กันเด้ง " +
                str(clean_count) + " | ส่วนต่าง " + str(raw_count - clean_count))


while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    now = time.ticks_ms()        # ขอเวลาครั้งเดียวต่อรอบ ใช้ร่วมกันทั้งสองงาน
    raw = btn.is_pressed()

    if raw != last_raw:
        # ค่าดิบเพิ่งขยับ นับฝั่งดิบทันทีถ้าเป็นขอบ "เริ่มกด"
        # นี่คือวิธีที่โปรแกรมไร้เดียงสาทำ และเป็นที่มาของเลขที่เกินจริง
        if raw:
            raw_count = raw_count + 1
            seg_raw.text(str(raw_count))
            show_counts()

        last_raw = raw
        last_change = now        # เริ่มจับเวลานิ่งใหม่ทุกครั้งที่ขยับ

    elif raw != stable and time.ticks_diff(now, last_change) >= DEBOUNCE_MS:
        # ค่าดิบนิ่งอยู่ที่ค่าใหม่ครบเวลาแล้ว ถึงจะยอมรับเป็นค่าจริง
        stable = raw

        # นับเฉพาะขอบ "เริ่มกด" ถ้าตัดบรรทัด if นี้ออก การปล่อยจะถูกนับด้วย
        if stable:
            clean_count = clean_count + 1
            seg_clean.text(str(clean_count))
            show_counts()
            lcd.print("ดิบ", raw_count, "| กันเด้ง", clean_count)

    # ลูปนี้วิ่งทุก 5 ms จอจึงต้องได้ ui.poll() ทุกรอบเหมือนกัน ไม่งั้นภาพจะค้าง
    ui.poll()
    time.sleep_ms(POLL_MS)

lcd.console("<span class=muted>---</span>")
lcd.print("<span class=ok>สรุป ดิบ", raw_count, "| กันเด้ง", clean_count, "</span>")

# ส่วนต่างคือจำนวนครั้งที่ปุ่มเด้ง ถ้าเป็นศูนย์ ไม่ได้แปลว่าปุ่มไม่เด้ง
# แต่แปลว่ารอบนี้กดช้าพอจนไม่เจอ ลองกดเร็ว ๆ รัว ๆ อีกรอบแล้วเทียบดู
lcd.print("<span class=muted>ส่วนต่าง", raw_count - clean_count, "ครั้ง</span>")

# ค่าที่ต่ำกว่า 10 ยังกันเด้งไม่อยู่ ส่วนค่าที่สูงกว่า 200 คนจะรู้สึกว่าปุ่มหน่วง
cfg.text("ต่ำกว่า 10 ยังเด้งหลุด | สูงกว่า 200 ปุ่มจะหน่วง")
st.text("หมดเวลา - ดิบ " + str(raw_count) + " | กันเด้ง " + str(clean_count))
st.color(COL_OK)
ui.poll()
