# part1/menu_part1.py - เมนูเลือกตัวอย่าง Part 1 บนจอสัมผัส
#
# จิตวิญญาณเดียวกับ example_selector.h ของคอร์ส C แต่เลือกด้วยนิ้วแทน #define:
# flash ครั้งเดียว (ไฟล์ตัวอย่างอยู่บน filesystem เป็น /p1exNN.py) แล้วกดเลือก
# ได้เรื่อย ๆ ไม่ต้อง reset อีก - ตัวอย่างจบ (RUN_MS หมด) จะเด้งกลับเมนูเอง

import time
import gc
import ui

ITEMS = (
    ("Ex1 Hello World", "/p1ex01.py"),
    ("Ex2 Button Counter", "/p1ex02.py"),
    ("Ex3 LED Widget", "/p1ex03.py"),
    ("Ex4 Switch Control", "/p1ex04.py"),
    ("Ex5 GPIO Dashboard", "/p1ex05.py"),
    ("Ex6 HW LED Control", "/p1ex06.py"),
    ("Ex7 HW Buttons", "/p1ex07.py"),
    ("Ex8 HW ADC Display", "/p1ex08.py"),
    ("Ex9 HW GPIO Dash", "/p1ex09.py"),
    ("Ex10 CAPSENSE Mock", "/p1ex10.py"),
    ("Ex11 CAPSENSE HW", "/p1ex11.py"),
)


def run_example(path):
    gc.collect()
    try:
        src = open(path).read()
    except OSError:
        return "missing: " + path
    try:
        # MENU_MODE ทำให้ตัวอย่างโชว์ปุ่ม "< Menu" มุมซ้ายบนสำหรับเด้งกลับ
        exec(src, {"__name__": "__main__", "MENU_MODE": True})
        return None
    except Exception as e:
        return repr(e)
    finally:
        gc.collect()


msg = "Part 1 selector - tap an example"
while True:
    ui.screen()
    time.sleep_ms(300)
    ui.Panel(x=0, y=0, w=792, h=398, color=0x16213E, min=0x16213E, max=0,
             value=0)
    ui.Label("LVGL C -> MicroPython : Part 1", x=250, y=10, color=0xFFFFFF,
             value=20)
    status = ui.Label(msg, x=250, y=40, color=0x00D4FF, value=14)

    ids = {}
    for i, (name, path) in enumerate(ITEMS):
        col, row = i % 3, i // 3
        b = ui.Button(name, x=20 + col * 256, y=70 + row * 66, w=240, h=56,
                      color=0x1F4068, value=14)
        ids[b.id()] = (name, path)

    ui.Label("example returns here when its RUN_MS ends", x=240, y=372,
             color=0x94A3B8, value=14)

    picked = None
    while picked is None:
        for ev in ui.poll():
            if ev["type"] == "clicked" and ev["handle"] in ids:
                picked = ids[ev["handle"]]
                break
        time.sleep_ms(50)

    print("menu: running " + picked[1])
    err = run_example(picked[1])
    msg = (picked[0] + " -> " + err) if err else \
        (picked[0] + " finished - pick the next one")
    if err:
        print("menu: " + msg)
