# 01_imu_vibration_monitor.py - เฝ้าการสั่นของเครื่องจักร
# ชุดตัวอย่างประจำคาบ 07
#
# ไฟล์นี้สอน: การสั่นวัดด้วยค่าเฉลี่ยไม่ได้ เพราะค่าบวกลบหักล้างกันจนเหลือศูนย์
#             ต้องใช้ RMS ของส่วนที่เบี่ยงจากแรงโน้มถ่วง แล้วรายงานเป็นกี่เท่า
#             ของเส้นฐาน ไม่ใช่ค่าดิบ จึงเทียบข้ามเครื่องได้
# ดูที่จอ   : กราฟ RMS ของแต่ละหน้าต่าง พร้อมเส้นเฝ้าดู (เหลือง) และเส้นเตือน (แดง)
#             ขวามือบอกว่าตอนนี้กี่เท่าของเส้นฐาน ปุ่มขวาล่างหยุดกราฟไว้ที่ภาพเดิม
# กับดัก    : ต้องเก็บเส้นฐานตอนเครื่องเดินปกติ ไม่ใช่ตอนเครื่องหยุด เพราะเกณฑ์
#             ที่ตั้งจากเครื่องหยุดจะเตือนทันทีที่เปิดเครื่อง แล้วไม่มีใครเชื่ออีกเลย
#             และ "หยุดกราฟ" คือหยุดวาด ไม่ใช่หยุดทำงาน - ยังต้อง poll ปุ่มอยู่
#
# บน Eva Kit: sensors.init() และ sensors.scan() ปฏิเสธด้วย OSError ไม่ต้องเรียก
#   ส่วน sensors.bmi270.* ใช้ได้เลยโดยไม่ต้อง init - มันอ่านจาก snapshot ที่ CM55
#   ส่งมาทาง IPC (modsensors.c) หลังรีเซ็ต การอ่านครั้งแรกช้าได้ถึงราว 16 วินาที
#   และอาจโยน OSError ระหว่างนั้น - rms_window() จึงดักไว้

import gpio
import lcd
import math
import sensors
import time
import ui

WIN = 25           # จำนวนตัวอย่างต่อหนึ่งหน้าต่างวัด
GAP_MS = 2         # ระยะห่างระหว่างตัวอย่าง - ของจริงควรห่างกว่านี้ ที่นี่บีบให้สั้น
                   # เพื่อให้กราฟ 50 หน้าต่างเต็มจอภายในสามวินาที
WARN_K = 2.0       # RMS เกินกี่เท่าของเส้นฐาน ถือว่าเฝ้าดู
ALARM_K = 4.0      # เกินกี่เท่า ถือว่าต้องเข้าตรวจ
GRAV = 9.81


def rms_window():  # หนึ่งหน้าต่าง = ค่า RMS หนึ่งค่า
    # เก็บส่วนที่เบี่ยงจากแรงโน้มถ่วง แล้วหารากที่สองของค่าเฉลี่ยกำลังสอง
    acc = 0.0
    for _ in range(WIN):
        try:
            ax, ay, az, _, _, _ = sensors.bmi270.motion()
        except OSError:
            # หลังรีเซ็ต CM55 ยังไม่พร้อมตอบ นับตัวอย่างนี้เป็นศูนย์ ไม่ใช่ปล่อยให้ตาย
            time.sleep_ms(GAP_MS)
            continue
        d = math.sqrt(ax * ax + ay * ay + az * az) - GRAV
        acc += d * d
        time.sleep_ms(GAP_MS)
    return math.sqrt(acc / WIN)


lcd.clear()
lcd.console("<h2>เฝ้าการสั่นของเครื่องจักร</h2>")
lcd.print("เก็บเส้นฐาน 3 หน้าต่าง ให้เครื่องเดินปกติ...")

ui.screen()
ui.Label("เฝ้าการสั่นของเครื่องจักร", x=12, y=6, value=24)
# วางป้ายนี้ไว้แถบล่าง ไม่ใช่ใต้หัวเรื่อง เพราะพื้นที่บนคือที่ของกราฟที่จะสร้าง
# หลังได้เส้นฐาน ป้ายชั่วคราวที่นั่งทับรอยเท้าของ widget ตัวถัดไปคือนิสัยที่พาไป
# ชนของจริงในวันที่ลืม hide()
busy = ui.Label("กำลังเก็บเส้นฐาน 3 หน้าต่าง...", x=12, y=274, value=16,
                color=0xFFC107)
ui.poll()

# พื้นล่างของเส้นฐาน กันกรณีวางนิ่งสนิทจนเส้นฐานเป็นศูนย์
base = max(0.05, sum(rms_window() for _ in range(3)) / 3.0)
warn_at = base * WARN_K
alarm_at = base * ALARM_K
lcd.print("เส้นฐาน RMS", round(base, 3), "m/s2")
lcd.print("เฝ้าดูที่", round(warn_at, 2), "| เตือนที่", round(alarm_at, 2))
busy.hide()

# ---- หน้าจอ: สร้างหลังได้เส้นฐาน เพราะแกน Y ของกราฟตั้งจากเส้นฐานนั้น --------
# กราฟรับได้เฉพาะจำนวนเต็ม จึงคูณพัน แล้วอ่านแกน Y เป็นหน่วยพันเท่าของ m/s^2
ch = ui.Chart(x=12, y=40, w=470, h=232, min=0, max=int(alarm_at * 1000) + 60)
s_rms = 0                          # ซีรีส์ 0 เกิดพร้อมกราฟ สีฟ้าเริ่มต้น
s_warn = ch.add_series(0xFFC107)
s_alarm = ch.add_series(0xFF5555)
ui.Label("ฟ้า = RMS ของหน้าต่าง", x=496, y=44, value=16, color=0x00BFFF)
ui.Label("เหลือง = เฝ้าดู 2 เท่า", x=496, y=68, value=16, color=0xFFC107)
ui.Label("แดง = ต้องเข้าตรวจ 4 เท่า", x=496, y=92, value=16, color=0xFF5555)

ui.Label("ตอนนี้กี่เท่าของเส้นฐาน", x=496, y=128, value=16)
ratio_bar = ui.Bar(x=496, y=152, w=250, h=18, min=0, max=int(ALARM_K * 100),
                   value=0)
seg = ui.Seg7(x=496, y=180, w=180, h=40)
status = ui.Label("ปกติ", x=496, y=232, value=24, color=0x00E676)

ui.Label("เส้นฐานรอบนี้ RMS " + str(round(base, 3)) +
         " m/s2 - เก็บตอนเครื่องเดินปกติ", x=24, y=298, value=20,
         color=0xFFC107)

# ปุ่มหยุดกราฟ - MVP ของคาบนี้บังคับให้กราฟหยุดและเดินต่อได้จริง ไม่ใช่แค่ดูอย่างเดียว
btn_pause = ui.Button("หยุดกราฟ", x=524, y=286, w=150, h=48, color=0x1E88E5,
                      value=20)
ID_PAUSE = btn_pause.id()
# y=264 คือช่องว่างระหว่างป้ายสถานะ (y=232 ฟอนต์ 24) กับปุ่ม (y=286) พอดี
lbl_run = ui.Label("กำลังวัด", x=524, y=264, value=16, color=0x00E676)
ui.poll()

shown_tag = ""
paused = False


def service_ui():
    """ดูดเหตุการณ์จากจอหนึ่งครั้ง แล้วสลับสถานะหยุด/เดิน ถ้าปุ่มถูกกด

    ต้องเรียกทั้งตอนกำลังวัดและตอนหยุด ถ้าหยุดแล้วไม่ poll เลย ปุ่มจะกดไม่ติด
    และเฟิร์มแวร์จะถือว่าโปรแกรมตายแล้วซ่อน widget ทิ้งทั้งจอ
    """
    global paused
    for ev in ui.poll():
        if ev["type"] == "clicked" and ev["handle"] == ID_PAUSE:
            paused = not paused
            btn_pause.text("เดินกราฟต่อ" if paused else "หยุดกราฟ")
            lbl_run.text("หยุดกราฟไว้" if paused else "กำลังวัด")
            lbl_run.color(0xFFC107 if paused else 0x00E676)
            lcd.print("หยุดกราฟ" if paused else "เดินกราฟต่อ")


window_n = 0
while window_n < 200:
    if paused:
        # หยุดคือหยุดวาด ไม่ใช่หยุดโปรแกรม ตัวนับจึงไม่เดิน และไม่อ่านเซนเซอร์
        service_ui()
        time.sleep_ms(30)
        continue

    rms = rms_window()
    ratio = rms / base

    ch.set_next(s_rms, int(rms * 1000))
    ch.set_next(s_warn, int(warn_at * 1000))
    ch.set_next(s_alarm, int(alarm_at * 1000))
    ratio_bar.value(int(ratio * 100))
    seg.text(str(round(ratio, 1)) + "x")

    for n in range(3):
        gpio.led(n).off()

    if ratio >= ALARM_K:
        gpio.led(0).on()
        tag = "ต้องเข้าตรวจ"
        col = 0xFF5555
    elif ratio >= WARN_K:
        gpio.led(2).on()
        tag = "เฝ้าดู"
        col = 0xFFC107
    else:
        gpio.led(1).on()
        tag = "ปกติ"
        col = 0x00E676

    if tag != shown_tag:
        shown_tag = tag
        status.text(tag)
        status.color(col)

    if window_n % 5 == 0:
        lcd.print(str(window_n) + ") RMS " + str(round(rms, 3)) + " = " +
                  str(round(ratio, 1)) + "x -> " + tag)
    service_ui()
    window_n += 1

for n in range(3):
    gpio.led(n).off()
lcd.print("<span class=muted>รายงานเป็นสัดส่วนกับเส้นฐาน จึงเทียบข้ามเครื่องได้</span>")
