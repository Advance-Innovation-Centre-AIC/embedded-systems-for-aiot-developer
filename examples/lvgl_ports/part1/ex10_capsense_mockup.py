# part1/ex10_capsense_mockup.py - port ของ part1_ex10_capsense_mockup (part1_hw_examples.c:969)
#
# หน้าจอ C : mockup ของแผง CAPSENSE - แผง slider 420x80 บน (label + % +
#            slider + LED ฟ้าเล็ก) และแผงปุ่มสองใบ 140x150 ล่างซ้าย/ขวา
#            (ชื่อ, "(CSB1/2)", LED 50, สถานะ) โหมด Auto Demo เดิน 8 จังหวะ
#            ทุก 500ms จนกว่าผู้ใช้จะแตะเอง -> Manual
# กลไก MPY : แผงปุ่มใช้ .listen("pressed","released") บน Panel
# ต่างจาก C : สีพื้นแผงปุ่มตอนกด เปลี่ยนทั้งใบด้วย .color() เหมือน C

import time
import ui
import lcd
import gpio

W, H, CX = 792, 398, 396
FOOTER = "(C) 2023-2026 AIC-EEC.com and BiiL Centre, Burapha University"
RUN_MS = 120000
PANEL, PANEL_HIT, BORDER = 0x333355, 0x00AA00, 0x666699
GREY, OK, LBLUE = 0x888888, 0x00FF00, 0x00AAFF


def cx(s, fs):
    return CX - (len(s) * fs) // 4


def find_led(name_sub, fallback):
    info = gpio.board_info()
    for i, n in enumerate(info["led_names"]):
        if name_sub in n:
            return gpio.led(i)
    return gpio.led(fallback)


hw_red = find_led("RED", 0)
hw_green = find_led("GREEN", 1)
hw_blue = find_led("BLUE", 2)
cur_pct = 50

ui.screen()
time.sleep_ms(200)

ui.Panel(x=0, y=0, w=W, h=H, color=0x1A1A2E, min=0x1A1A2E, max=0, value=0)

t = "Part 1 Ex10: CAPSENSE UI Mockup"
ui.Label(t, x=cx(t, 14), y=7, color=0xFFFFFF, value=14)
mode_l = ui.Label("Mode: Auto Demo", x=cx("Mode: Auto Demo", 14), y=25,
                  color=0x00FFFF, value=14)

# แผง slider 420x80 -> 420x66 ที่ TOP_MID+85 -> +71
SX, SY = CX - 210, 71
ui.Panel(x=SX, y=SY, w=420, h=66, color=0x0F0F23, min=0x0F0F23, max=0,
         value=0)
ui.Label("SLIDER (CSS1)", x=SX + 10, y=SY + 6, color=0xFFFFFF, value=14)
sld_val = ui.Label("50%", x=SX + 330, y=SY + 4, color=0xFFFFFF, value=16)
sld = ui.Slider(x=SX + 10, y=SY + 34, w=340, h=21, min=0, max=100, value=50,
                color=LBLUE)
out_led = ui.Led(x=SX + 375, y=SY + 30, w=25, h=25, color=LBLUE, value=1)

# แผงปุ่มสองใบ 140x150 -> 140x125 ที่ BOTTOM_MID(+-110,-85) -> y=202
panels, leds, sts, ids = [], [], [], []
for i, bx in enumerate((CX - 110 - 70, CX + 110 - 70)):
    p = ui.Panel(x=bx, y=202, w=140, h=125, color=PANEL, min=BORDER, max=10,
                 value=3)
    p.listen("pressed", "released")
    ui.Label("BTN" + str(i), x=bx + 70 - 14, y=210, color=0xFFFFFF, value=16)
    ui.Label("(CSB" + str(i + 1) + ")", x=bx + 70 - 18, y=230,
             color=0xAAAAAA, value=14)
    led = ui.Led(x=bx + 45, y=250, w=50, h=50, color=(0xF44336, 0x4CAF50)[i],
                 value=0)
    st = ui.Label("Ready", x=bx + 70 - 17, y=306, color=GREY, value=14)
    panels.append(p)
    leds.append(led)
    sts.append(st)
    ids.append(p.id())

ui.Label(FOOTER, x=cx(FOOTER, 14), y=380, color=0x666666, value=14)

sld_id = sld.id()
lcd.print("ex10: auto demo walks the panel - touch anything to go Manual")


def press(i, on):
    global cur_pct
    hw = (hw_red, hw_green)[i]
    if on:
        hw.on()
    else:
        hw.off()
    panels[i].color(PANEL_HIT if on else PANEL)
    leds[i].value(1 if on else 0)
    sts[i].text("TOUCHED" if on else "Ready")
    sts[i].color(OK if on else GREY)


def slide(v):
    global cur_pct
    cur_pct = v
    hw_blue.brightness(100 if v >= 100 else v)
    sld_val.text(str(v) + "%")
    out_led.value(1)
    out_led.prop(ui.PROP_LED_BRIGHTNESS, (v * 255) // 100)


demo, step = True, 0
last = time.ticks_ms()

# ปุ่มย้อนกลับ - โผล่เฉพาะตอนรันผ่านเมนูบนบอร์ด (MENU_MODE) ไม่กระทบ layout เดิม
if globals().get("MENU_MODE"):
    _back = ui.Button("< Menu", x=4, y=4, w=76, h=32, color=0x333333, value=14)
    _back_id = _back.id()
else:
    _back_id = -1

t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == _back_id:
            RUN_MS = 0
            break
        h, k = ev["handle"], ev["type"]
        if h in ids and k in ("pressed", "released"):
            if demo:
                demo = False
                mode_l.text("Mode: Manual")
            press(ids.index(h), k == "pressed")
        elif h == sld_id and k == "value_changed":
            if demo:
                demo = False
                mode_l.text("Mode: Manual")
            slide(ev["value"])

    # จังหวะสาธิตทุก 500ms ตาม C: กด/ปล่อย BTN0, BTN1 แล้วกวาด slider 4 ระดับ
    if demo and time.ticks_diff(time.ticks_ms(), last) >= 500:
        last = time.ticks_ms()
        s = step % 8
        if s < 4:
            press(s // 2, s % 2 == 0)
        else:
            v = min((s - 4) * 33, 100)
            sld.value(v)
            slide(v)
        step += 1
    if 0 < cur_pct < 100:
        hw_blue.brightness(cur_pct)
    time.sleep_ms(50)

print("ex10_capsense_mockup: done")
