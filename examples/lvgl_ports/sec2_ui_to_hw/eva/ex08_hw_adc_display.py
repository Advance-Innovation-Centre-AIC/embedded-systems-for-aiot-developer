# part1/ex08_hw_adc_display.py - port ของ part1_ex8_hw_adc_display (part1_hw_examples.c:460)
#
# หน้าจอ C : สามแถวสามสี - (เขียว) slider ค่า ADC ดิบ 0-4095 อ่านอย่างเดียว +
#            "Raw: n" ขวามือ, (ฟ้า) bar เปอร์เซ็นต์ + "n%", (เหลือง)
#            "Voltage: n.nnn V" ตัวโต Montserrat-24, timer 100ms อ่าน pot จริง
# กลไก MPY : Dev Kit ใช้ pots.read(0) = VR1 (0-4095 ตรงสเกล C),
#            Eva ใช้ sensors.pot.read()>>4
# ต่างจาก C : slider ของ MPY ปิดการแตะไม่ได้ - ผู้ใช้ลากได้ชั่วครู่ แต่ค่าจริง
#            เขียนทับทุกรอบ; สี track/knob แยกส่วนไม่ได้ ใช้สีเดียวต่อ widget
# โบนัส Dev Kit: rgbmatrix.bar() แสดงระดับบนแถวล่างของ RGB Matrix

import time
import ui
import lcd

# ==== BOARD: Eva Kit — pot เดี่ยว P15.1 (เมนู Controls ใช้ตัวเดียวกัน) ====
import sensors

def read_raw():
    return sensors.pot.read() >> 4  # 0-65535 -> 0-4095 ให้ตรงสเกล C
# ==== END BOARD ====


# ==== BOARD: Eva Kit — ไม่มี RGB Matrix ====
HAS_RGB = False
# ==== END BOARD ====

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
GRN, CYN, YEL = 0x00FF00, 0x00FFFF, 0xFFFF00
ADC_MAX = 4095


def cx(s, fs):
    return CX - (len(s) * fs) // 4


ui.screen()
time.sleep_ms(200)

ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)

t = "Part 1 Ex8: Hardware ADC Display"
ui.Label(t, x=cx(t, 14), y=8, color=0xFFFFFF, value=14)

# แถว 1 (เขียว): slider แสดงค่าดิบ ที่ CENTER(-55,-70/-40) ของ C
LX = CX - 55 - 100                      # จุดเริ่ม slider/bar กว้าง 200
ui.Label("ADC Raw Value (0-4095)", x=LX, y=133, color=GRN, value=14)
sld = ui.Slider(x=LX, y=158, w=200, h=16, min=0, max=ADC_MAX, value=2048,
                color=GRN)
raw_l = ui.Label("Raw: 2048", x=LX + 215, y=158, color=GRN, value=14)

# แถว 2 (ฟ้า): bar เปอร์เซ็นต์ ที่ CENTER(-55,0/+25) ของ C
ui.Label("Percentage", x=LX, y=191, color=CYN, value=14)
bar = ui.Bar(x=LX, y=211, w=200, h=18, min=0, max=100, value=50, color=CYN)
pct_l = ui.Label("50%", x=LX + 215, y=211, color=CYN, value=14)

# แถว 3 (เหลือง): แรงดัน ตัวโต ที่ CENTER(0,+80) ของ C
volt_l = ui.Label("Voltage: 1.650 V", x=cx("Voltage: 1.650 V", 24), y=253,
                  color=YEL, value=24)

t = "[Part II] Turn the potentiometer on the board"
ui.Label(t, x=cx(t, 14), y=352, color=0xAAAAAA, value=14)

ui.Label(FOOTER, x=cx(FOOTER, 14), y=374, color=0x666666, value=14)

lcd.print("ex08: three rows, three colors - raw / percent / volts from the pot")

prev = -1

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
    raw = read_raw()
    if abs(raw - prev) > 8:             # เขียนจอเฉพาะตอนค่าขยับจริง
        prev = raw
        p = (raw * 100) // ADC_MAX
        sld.value(raw)
        bar.value(p)
        raw_l.text("Raw: " + str(raw))
        pct_l.text(str(p) + "%")
        volt_l.text("Voltage: %.3f V" % (raw / ADC_MAX * 3.3))
        if HAS_RGB:
            rgbmatrix.bar((p * 5) // 100 + (1 if p else 0), 5, 2)
    time.sleep_ms(100)

print("ex08_hw_adc_display: done")


# ---- ตัวอย่างจบแล้ว -------------------------------------------------------
# RUN_MS หมดแล้วลูปรับ event ก็จบด้วย ภาพยังค้างบนจอ ถ้าไม่บอก ผู้เรียนจะกด
# ปุ่มแล้วนึกว่าบอร์ดเสีย - แถบทึบนี้วาดทับแถวล่างตอนจบเท่านั้น
ui.Panel(x=0, y=330, w=792, h=36, color=0x1A1A2E, min=0xFF6600, max=0, value=1)
ui.Label("ตัวอย่างจบแล้ว กดปุ่มไม่ได้", x=232, y=338, color=0xFF6600, value=16)
