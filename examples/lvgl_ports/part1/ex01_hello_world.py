# part1/ex01_hello_world.py - port ของ part1_ex1_hello_world (part1_examples.c:103)
#
# หน้าจอ C : พื้น 0x003a57, ชื่อเรื่องบนกลาง, "Hello BUU!" (Montserrat-24) กลางจอ,
#            คำอธิบายสองบรรทัดสีเทาใกล้ล่าง, footer ลิขสิทธิ์
# กลไก     : ไม่มี - หน้าจอนิ่งล้วน
# ต่างจาก C : จอ ui สูง 398 (C คือ 480) ตำแหน่งแกน y คูณ 0.83 ทั้งแผ่น

import time
import ui

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"


def cx(s, fs):          # ประมาณจุดเริ่ม x ให้ข้อความอยู่กลางจอ (กว้าง ~ fs/2 ต่อตัว)
    return CX - (len(s) * fs) // 4


ui.screen()
time.sleep_ms(200)

# พื้นหลังทั้งจอ - C ใช้ lv_obj_set_style_bg_color กับ screen ตรง ๆ
ui.Panel(x=0, y=0, w=W, h=H, color=0x003A57, min=0x003A57, max=0, value=0)

t = "Part 1 - Example 1"
ui.Label(t, x=cx(t, 14), y=17, color=0xFFFFFF, value=14)

t = "Hello BUU!"
ui.Label(t, x=cx(t, 24), y=187, color=0xFFFFFF, value=24)

# desc ของ C เป็น label เดียวสองบรรทัด text-align center - แยกเป็นสอง Label แทน
t = "Basic Label Example"
ui.Label(t, x=cx(t, 14), y=314, color=0xAAAAAA, value=14)
t = "Learning: lv_label_create, lv_obj_align"
ui.Label(t, x=cx(t, 14), y=332, color=0xAAAAAA, value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=374, color=0x666666, value=14)

# ปุ่มย้อนกลับ - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE) ไม่กระทบ layout เดิม
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=4, y=4, w=76, h=32, color=0x333333, value=14)
    _back_id = _back.id()
else:
    _back_id = -1

# หน้าจอนิ่ง - รันเดี่ยวคงจอไว้ 2 วิแล้วจบ (widget ค้างบนจอ)
# ผ่านเมนูค้างรอจนกดปุ่ม < Menu (เพดาน 10 นาทีกันลืม)
RUN_MS = 600000 if _back_id >= 0 else 2000
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
    time.sleep_ms(100)
print("ex01_hello_world: done - widgets stay on screen")
