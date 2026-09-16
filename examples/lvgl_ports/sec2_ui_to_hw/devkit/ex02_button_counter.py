# part1/ex02_button_counter.py - port ของ part1_ex2_button_counter (part1_examples.c:169)
#
# หน้าจอ C : พื้น 0x1a1a2e, ปุ่ม "Click Me!" กลางจอ กดแล้วข้อความบนปุ่ม
#            เปลี่ยนเป็น "Clicked: n", คำอธิบายสองบรรทัดใกล้ล่าง, footer
# กลไก     : LV_EVENT_CLICKED -> ที่นี่คือ ui.poll() type 'clicked'
# ต่างจาก C : ปุ่ม MPY เปลี่ยนข้อความด้วย .text() ของตัวปุ่มเอง (ไม่มี child label)

import time
import ui
import lcd

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000


def cx(s, fs):
    return CX - (len(s) * fs) // 4


ui.screen()
time.sleep_ms(200)

ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)

t = "Part 1 - Example 2: Button Counter"
ui.Label(t, x=cx(t, 14), y=17, color=0xFFFFFF, value=14)

# C: ปุ่ม pad_hor 30 / pad_ver 15 รอบข้อความ Montserrat-14 -> ประมาณ 150x48
btn = ui.Button("Click Me!", x=CX - 75, y=175, w=150, h=48, value=14)
btn_id = btn.id()

t = "Learning: lv_button_create, lv_obj_add_event_cb"
ui.Label(t, x=cx(t, 14), y=322, color=0xAAAAAA, value=14)
t = "Pattern: Event callback with LV_EVENT_CLICKED"
ui.Label(t, x=cx(t, 14), y=340, color=0xAAAAAA, value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=374, color=0x666666, value=14)

lcd.print("ex02: tap the button - counter updates on the button itself")

cnt = 0

# ปุ่มย้อนกลับ มุมล่างซ้าย - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE)
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=8, y=344, w=120, h=46, color=0x333333,
                      value=16)
    _back_id = _back.id()
else:
    _back_id = -1

t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
            break
        if ev["type"] == "clicked" and ev["handle"] == btn_id:
            cnt += 1
            btn.text("Clicked: " + str(cnt))
            print("Button clicked " + str(cnt) + " times")
    time.sleep_ms(50)

print("ex02_button_counter: done - " + str(cnt) + " clicks")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
