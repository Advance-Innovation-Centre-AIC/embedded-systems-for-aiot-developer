# sec3_sensor_viz/menu_sec3.py -> flash เป็น /s3menu.py
# เมนูตอนที่ 3 · Sensor Visualization on HMI — ตัวอย่างอยู่บน FS เป็น /s3eNN.py
# (ไฟล์ต่อบอร์ด: push ชุด eva/ หรือ devkit/ ตามบอร์ดที่ใช้)
# สองหน้า: Part 2 = Sensor Viz (11), Part 3 = Oscilloscope & DSP (7)
# ปุ่มย้อนกลับอยู่มุมล่างซ้าย (คำตัดสิน UX ของอาจารย์ 2026-08-20 — มุมบนซ้าย
# ชนกับ < Home ของหน้า Playground เอง)
import time
import gc
import ui

PART2 = (
    ("Ex1 ADC Visualization", "/s3e01.py"),
    ("Ex2 Arc Gauge", "/s3e02.py"),
    ("Ex3 Accel Chart", "/s3e03.py"),
    ("Ex4 Temperature Gauge", "/s3e04.py"),
    ("Ex5 Sensor Dashboard", "/s3e05.py"),
    ("Ex6 Chart Dashboard", "/s3e06.py"),
    ("Ex7 Real IMU Chart", "/s3e07.py"),
    ("Ex8 Real Dashboard", "/s3e08.py"),
    ("Ex9 Arc (Roll)", "/s3e09.py"),
    ("Ex10 Scale (Pitch)", "/s3e10.py"),
    ("Ex11 Charts + Compass", "/s3e11.py"),
)
PART3 = (
    ("Ex12 Waveform Gen", "/s3e12.py"),
    ("Ex13 Noise Gen", "/s3e13.py"),
    ("Ex14 Mic Waveform", "/s3e14.py"),
    ("Ex15 Oscilloscope", "/s3e15.py"),
    ("Ex16 FFT Spectrum", "/s3e16.py"),
    ("Ex17 Scope Panels", "/s3e17.py"),
    ("Ex18 HW Scope + LED", "/s3e18.py"),
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


msg = "ตอนที่ 3 - แตะตัวอย่าง"
part = 2
while True:
    ui.screen()
    time.sleep_ms(300)
    ui.Panel(x=0, y=0, w=792, h=398, color=0x16213E, min=0x16213E, max=0,
             value=0)
    ui.Label("ตอนที่ 3 - Sensor Visualization", x=20, y=14, color=0xFFFFFF,
             value=20)
    p2_btn = ui.Button("Part 2", x=480, y=8, w=140, h=46,
                       color=0x2196F3 if part == 2 else 0x333333, value=16)
    p3_btn = ui.Button("Part 3", x=632, y=8, w=140, h=46,
                       color=0x2196F3 if part == 3 else 0x333333, value=16)
    status = ui.Label(msg, x=20, y=60, color=0x00D4FF, value=14)

    ids = {}
    for i, (name, path) in enumerate(PART2 if part == 2 else PART3):
        col, row = i % 3, i // 3
        b = ui.Button(name, x=20 + col * 256, y=88 + row * 64, w=240, h=56,
                      color=0x1F4068, value=14)
        ids[b.id()] = (name, path)

    back = ui.Button("< Sections", x=8, y=346, w=150, h=44, color=0x333333,
                     value=16)
    back_id = back.id()

    picked = None
    goback = False
    while picked is None:
        for ev in ui.poll():
            if ev["type"] != "clicked":
                continue
            if ev["handle"] == back_id:
                goback = True
                picked = ("", "")
            elif ev["handle"] == p2_btn.id():
                part = 2
                picked = ("", "")
            elif ev["handle"] == p3_btn.id():
                part = 3
                picked = ("", "")
            elif ev["handle"] in ids:
                picked = ids[ev["handle"]]
        time.sleep_ms(50)

    if goback:
        break
    name, path = picked
    if not path:
        continue  # แค่สลับหน้า Part
    print("s3menu: running " + path)
    err = run_example(path)
    msg = (name + " -> " + err) if err else \
        (name + " finished - pick the next one")
