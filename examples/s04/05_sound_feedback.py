# 05_sound_feedback.py - เสียงตอบรับตอนแตะปุ่ม
#
# Why : เสียงเป็นเอาต์พุตชนิดเดียวที่มองไม่เห็น พอกดปุ่มแล้วเงียบ เราแยกไม่ออกเลยว่า
#       ลำโพงเสีย หรือโค้ดไม่ได้เรียก หรือบอร์ดรุ่นนี้ไม่มีชิปเสียง สามอย่างนี้
#       อาการเหมือนกันหมด และทั้งสามอย่างแก้คนละที่
# What: ui.sfx(id) เล่นเสียงสำเร็จรูป ส่วน ui.tone() ให้เราแต่งเสียงเอง ทั้งคู่เป็น
#       แบบยิงแล้วลืม ไม่มีอะไรตอบกลับมาว่าเล่นสำเร็จไหม ทางออกคือให้ "จอ" เป็น
#       พยานแทนหู ทุกครั้งที่โปรแกรมสั่งเสียง ตัวนับบนจอต้องเดินขึ้นให้เห็น
#
# ดูที่จอ: ตัวนับ "เล่นเสียงไปแล้วกี่ครั้ง" ตัวโต และชื่อเสียงหรือโน้ตล่าสุด
#         ปุ่มสามใบข้างล่าง แตะแล้วทั้งสองช่องขยับพร้อมกัน
# กับดัก : ui.tone() รับ "โน้ต MIDI" เป็นเลข 0-127 ไม่ใช่ความถี่เป็นเฮิรตซ์
#         60 คือ C4 - 69 คือ A4 - ส่ง 440 ไปจะถูกตัดเหลือ 184 แล้วเพี้ยน
#         และมันรับแบบตำแหน่งเท่านั้น เขียน ui.tone(note=60) จะ error

import ui
import lcd
import time

RUN_MS = 30000

COL_TEXT = 0xFFFFFF
COL_DIM = 0xA0B4CC
COL_CARD = 0x142240
COL_OK = 0x00E676
COL_INFO = 0x40C4FF
COL_BAD = 0xFF5252

# บอร์ดที่ไม่มีชิปเสียงจะไม่มีสองฟังก์ชันนี้เลย เช็กก่อนใช้ ดีกว่าปล่อยให้
# AttributeError โผล่กลางคาบตอนนักเรียนกดปุ่ม
HAS_SOUND = hasattr(ui, "tone")

# โน้ต MIDI: 60 = โด กลาง แต่ละก้าวคือครึ่งเสียง
# 60 64 67 72 คือ โด มี ซอล โด สูง ซึ่งเข้ากันได้ในทุกลำดับ
MELODY = [60, 64, 67, 72]
NOTE_NAME = {60: "C4", 64: "E4", 67: "G4", 72: "C5"}

ui.screen()
time.sleep_ms(200)

ui.Label("เสียงตอบรับ - ให้จอเป็นพยานแทนหู", x=20, y=12, color=COL_TEXT, value=24)

ui.Panel(x=20, y=50, w=650, h=110, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("เล่นเสียงไปแล้ว (ครั้ง)", x=40, y=60, color=COL_DIM, value=16)

# Seg7 รับข้อความ ไม่ใช่ตัวเลข str() จึงต้องมีทุกครั้ง
seg = ui.Seg7(text="0", x=40, y=90, w=140, h=48, color=COL_OK)

ui.Label("เสียงล่าสุดที่สั่งไป", x=280, y=60, color=COL_DIM, value=16)
last = ui.Label("ยังไม่มี", x=280, y=88, color=COL_INFO, value=24)

b_ok = ui.Button("ยืนยัน", x=20, y=178, w=200, h=80, color=0x2E7D32)
b_no = ui.Button("ปฏิเสธ", x=240, y=178, w=200, h=80, color=0xC62828)
b_note = ui.Button("ไล่โน้ต", x=460, y=178, w=200, h=80, color=0x1565C0)

id_ok = b_ok.id()
id_no = b_no.id()
id_note = b_note.id()

status = ui.Label("แตะปุ่มแล้วฟัง พร้อมกับมองตัวนับ", x=20, y=278,
                  color=COL_DIM, value=20)
ui.Label("ui.tone รับโน้ต MIDI 0-127 - 60=C4 - 69=A4", x=20, y=330,
         color=COL_DIM, value=16)

if not HAS_SOUND:
    status.text("บอร์ดนี้ไม่มีชิปเสียง - ตัวนับจะไม่เดิน")
    status.color(COL_BAD)

# lcd.print ตัดที่ 127 ไบต์ และไทยตัวละ 3 ไบต์ บรรทัดจึงต้องสั้นกว่าที่คิด
lcd.print("แตะปุ่มแล้วฟัง พร้อมมองตัวนับบนจอ")
lcd.print("ตัวนับเดินแต่เงียบ = ปัญหาอยู่ที่ลำโพง")

played = 0
t0 = time.ticks_ms()


def played_one(name):
    """นับหนึ่งครั้ง แล้วเขียนลงจอทันที เสียงมองไม่เห็น ตัวนับจึงต้องเห็น"""
    global played
    played = played + 1
    seg.text(str(played))
    last.text(name)
    lcd.print("สั่งเสียง " + name + " (ครั้งที่ " + str(played) + ")")


while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] != "clicked":
            continue
        h = ev["handle"]

        if h == id_ok:
            status.text("ui.sfx(ui.SFX_UI_SELECT)")
            if HAS_SOUND:
                ui.sfx(ui.SFX_UI_SELECT)
            played_one("SFX_UI_SELECT")

        elif h == id_no:
            status.text("ui.sfx(ui.SFX_UI_DENY)")
            if HAS_SOUND:
                ui.sfx(ui.SFX_UI_DENY)
            played_one("SFX_UI_DENY")

        elif h == id_note:
            status.text("ui.tone(note, wave, velocity, dur_ms)")
            for note in MELODY:
                if HAS_SOUND:
                    # ลำดับพารามิเตอร์: โน้ต, รูปคลื่น, ความแรง, ความยาว ms
                    # ห้ามใส่ชื่อพารามิเตอร์ ต้องเรียงตามตำแหน่งเท่านั้น
                    ui.tone(note, ui.WAVE_TRIANGLE, 90, 120)
                played_one("tone " + str(note) + " = " + NOTE_NAME[note])

                # เสียงเล่นอยู่ฝั่ง CM55 คำสั่งของเราจบทันทีที่ส่งออกไป
                # ถ้าไม่หน่วง โน้ตทั้งสี่จะถูกส่งพร้อมกันจนทับกันเละ
                ui.poll()
                time.sleep_ms(140)

    time.sleep_ms(50)

status.text("หมดเวลา - สั่งเสียงไปทั้งหมด " + str(played) + " ครั้ง")
last.text("จบแล้ว")
lcd.print("สรุป: สั่งเสียง " + str(played) + " ครั้ง")
lcd.print("ยิงแล้วลืม ไม่มีทางรู้ว่าออกจริงไหม")
