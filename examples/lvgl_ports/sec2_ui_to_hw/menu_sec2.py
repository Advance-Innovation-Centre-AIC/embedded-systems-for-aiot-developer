# sec2_ui_to_hw/menu_sec2.py -> flash เป็น /s2menu.py
# เมนูตอนที่ 2 · UI-to-Hardware Interfacing — ตัวอย่างอยู่บน FS เป็น /s2eNN.py
# (ไฟล์ต่อบอร์ด: push ชุด eva/ หรือ devkit/ ตามบอร์ดที่ใช้)
import time
import gc
import ui

ITEMS = (
    ("Ex1 Hello World", "/s2e01.py"),
    ("Ex2 Button Counter", "/s2e02.py"),
    ("Ex3 LED Widget+HW", "/s2e03.py"),
    ("Ex4 Switch Control", "/s2e04.py"),
    ("Ex5 GPIO Dashboard", "/s2e05.py"),
    ("Ex6 HW LED Control", "/s2e06.py"),
    ("Ex7 HW Buttons", "/s2e07.py"),
    ("Ex8 HW ADC Display", "/s2e08.py"),
    ("Ex9 HW GPIO Dash", "/s2e09.py"),
    ("Ex10 CAPSENSE Mock", "/s2e10.py"),
    ("Ex11 CAPSENSE HW", "/s2e11.py"),
)


def run_example(path):
    gc.collect()
    try:
        src = open(path).read()
    except OSError:
        return "missing: " + path
    try:
        exec(src, {"__name__": "__main__", "MENU_MODE": True})
        return None
    except Exception as e:
        return repr(e)
    finally:
        gc.collect()


msg = "ตอนที่ 2 - แตะตัวอย่าง (Back = กลับหน้าตอน)"
while True:
    ui.screen()
    time.sleep_ms(300)
    ui.Panel(x=0, y=0, w=792, h=398, color=0x16213E, min=0x16213E, max=0,
             value=0)
    ui.Label("ตอนที่ 2 - UI-to-Hardware", x=250, y=10, color=0xFFFFFF,
             value=20)
    status = ui.Label(msg, x=250, y=40, color=0x00D4FF, value=14)
    back = ui.Button("< Sections", x=8, y=346, w=150, h=44, color=0x3A4150,
                     value=16)
    back_id = back.id()

    ids = {}
    for i, (name, path) in enumerate(ITEMS):
        col, row = i % 3, i // 3
        b = ui.Button(name, x=20 + col * 256, y=70 + row * 66, w=240, h=56,
                      color=0x1F4068, value=14)
        ids[b.id()] = (name, path)

    picked = None
    goback = False
    while picked is None:
        for ev in ui.poll():
            if ev["type"] != "clicked":
                continue
            if ev["handle"] == back_id:
                goback = True
                picked = ("", "")
            elif ev["handle"] in ids:
                picked = ids[ev["handle"]]
        time.sleep_ms(50)

    if goback:
        break
    name, path = picked
    print("s2menu: running " + path)
    err = run_example(path)
    msg = (name + " -> " + err) if err else \
        (name + " finished - pick the next one")
