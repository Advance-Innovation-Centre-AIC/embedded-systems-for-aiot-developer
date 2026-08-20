# boot/menu_sections.py -> flash เป็น /menu.py
# หน้าเลือก "ตอน" ตามโครงคอร์ส AIoT in Action (gen_roadmap.py)
import time
import gc
import ui

SECTIONS = (
    ("ตอนที่ 2 - UI-to-Hardware", "/s2menu.py"),
    ("ตอนที่ 3 - Sensor Visualization", "/s3menu.py"),
    ("Part 1 (legacy C-port menu)", "/p1menu.py"),
)


def run(path):
    gc.collect()
    try:
        src = open(path).read()
    except OSError:
        return path + " not on this board yet"
    try:
        exec(src, {"__name__": "__main__"})
        return None
    except Exception as e:
        return repr(e)
    finally:
        gc.collect()


msg = "เลือกตอนที่จะเรียน"
while True:
    ui.screen()
    time.sleep_ms(300)
    ui.Panel(x=0, y=0, w=792, h=398, color=0x16213E, min=0x16213E, max=0,
             value=0)
    ui.Label("LVGL C -> MicroPython", x=280, y=24, color=0xFFFFFF, value=24)
    status = ui.Label(msg, x=280, y=64, color=0x00D4FF, value=16)

    ids = {}
    for i, (name, path) in enumerate(SECTIONS):
        b = ui.Button(name, x=176, y=120 + i * 76, w=440, h=64,
                      color=0x1F4068 if path else 0x2A2F3A, value=16)
        ids[b.id()] = (name, path)

    picked = None
    while picked is None:
        for ev in ui.poll():
            if ev["type"] == "clicked" and ev["handle"] in ids:
                picked = ids[ev["handle"]]
        time.sleep_ms(50)

    name, path = picked
    if path is None:
        msg = name + " — ยังไม่พร้อม"
        continue
    print("sections: running " + path)
    err = run(path)
    msg = (name + " -> " + err) if err else "เลือกตอนที่จะเรียน"
