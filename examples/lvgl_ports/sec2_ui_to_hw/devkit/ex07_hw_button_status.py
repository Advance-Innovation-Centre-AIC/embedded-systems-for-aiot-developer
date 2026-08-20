# part1/ex07_hw_button_status.py - port ของ part1_ex7_hw_button_status (part1_hw_examples.c:343)
#
# หน้าจอ C : สองคอลัมน์สมมาตร - LED 70x70 ฟ้า (ซ้าย) / ส้ม (ขวา) พร้อมชื่อปุ่ม
#            สองบรรทัด และสถานะ "PRESSED" (เขียว) / "Released" (แดงอ่อน)
# กลไก C   : timer 50ms อ่านปุ่ม SW2 กับ SW4
# ต่างจาก C : ปุ่มที่สองต่อบอร์ด - Dev Kit ใช้ SW9 (โมดูล buttons ของฐาน QWA309),
#            Eva ใช้ CapSense BTN0 แทน (SW4 ยังเข้าจาก MPY ไม่ได้) - ป้ายบอกตรง ๆ

import time
import ui
import lcd
import gpio

# ==== BOARD: TESAIoT Dev Kit — ปุ่มที่สองใช้ CapSense BTN0 (วัดจริง 2026-08-20:
# SW4 ฐาน = P17.5 คือขา USB VBUS enable ห้ามใช้เป็นปุ่ม; ส่วนการกด SW5 ฐาน
# ไปไม่ถึง P17.7 ที่ SoC — ติดเส้นทางบนฐาน) ====
import sensors as _s
BTN2_NAME = "(CapSense BTN0)"

def read_btn2():
    return _s.capsense.buttons()[0]
# ==== END BOARD ====

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
CYAN, ORANGE = 0x00BCD4, 0xFF9800       # lv_palette CYAN/ORANGE
OK, BAD = 0x00FF00, 0xFF6666

btn1 = gpio.button(0)


def cx(s, fs):
    return CX - (len(s) * fs) // 4


def col(x_off, s):                       # จัดกลางภายในคอลัมน์ที่ CENTER+x_off
    return CX + x_off - (len(s) * 14) // 4


ui.screen()
time.sleep_ms(200)

ui.Panel(x=0, y=0, w=W, h=H, color=0x16213E, min=0x16213E, max=0, value=0)

t = "Part 1 Ex7: Hardware Button Status"
ui.Label(t, x=cx(t, 14), y=17, color=0xFFFFFF, value=14)

# LED 70x70 ที่ CENTER(-90,-30) / (+90,-30) ของ C -> y = 199 - 25 - 35
led1 = ui.Led(x=CX - 90 - 35, y=139, w=70, h=70, color=CYAN, value=0)
led2 = ui.Led(x=CX + 90 - 35, y=139, w=70, h=70, color=ORANGE, value=0)

ui.Label("USER Button 1", x=col(-90, "USER Button 1"), y=219, color=0xFFFFFF,
         value=14)
n1 = "(" + btn1.name() + ")"
ui.Label(n1, x=col(-90, n1), y=237, color=0xFFFFFF, value=14)

ui.Label("USER Button 2", x=col(90, "USER Button 2"), y=219, color=0xFFFFFF,
         value=14)
ui.Label(BTN2_NAME, x=col(90, BTN2_NAME), y=237, color=0xFFFFFF, value=14)

st1 = ui.Label("Released", x=col(-90, "Released"), y=261, color=BAD, value=16)
st2 = ui.Label("Released", x=col(90, "Released"), y=261, color=BAD, value=16)

t = "[Part II] Press the two buttons on the board"
ui.Label(t, x=cx(t, 14), y=336, color=0xAAAAAA, value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=374, color=0x666666, value=14)

lcd.print("ex07: watching button 1 " + n1 + " and button 2 " + BTN2_NAME)

p1 = p2 = None

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
    b1 = btn1.is_pressed()
    b2 = read_btn2()
    if b1 != p1:                        # เขียนจอเฉพาะตอนสถานะเปลี่ยน
        p1 = b1
        led1.value(1 if b1 else 0)
        st1.text("PRESSED" if b1 else "Released")
        st1.color(OK if b1 else BAD)
    if b2 != p2:
        p2 = b2
        led2.value(1 if b2 else 0)
        st2.text("PRESSED" if b2 else "Released")
        st2.color(OK if b2 else BAD)
    time.sleep_ms(50)

print("ex07_hw_button_status: done")
