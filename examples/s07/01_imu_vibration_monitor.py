# 01_imu_vibration_monitor.py - เฝ้าการสั่นของเครื่องจักร
# ชุดตัวอย่างประจำคาบ 07
#
# ไฟล์นี้สอน: การสั่นวัดด้วยค่าเฉลี่ยไม่ได้ เพราะค่าบวกลบหักล้างกันจนเหลือศูนย์
#             ต้องใช้ RMS ของส่วนที่เบี่ยงจากแรงโน้มถ่วง แล้วรายงานเป็นกี่เท่า
#             ของเส้นฐาน ไม่ใช่ค่าดิบ จึงเทียบข้ามเครื่องได้
# ดูที่จอ   : กราฟ RMS ของแต่ละหน้าต่าง พร้อมเส้นแนวนอนสองเส้นที่เป็นเกณฑ์
#             ขวามือบอกว่าตอนนี้กี่เท่าของเส้นฐาน ปุ่มซ้ายล่างหยุดกราฟไว้ที่ภาพเดิม
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

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
# สามตัวล่างใช้กับสถานะเท่านั้น ห้ามหยิบมาลากเส้นกราฟหรือแต่งจอ
COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK = 0x30A46C
COL_WARN = 0xF5A623
COL_BAD = 0xE5484D


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
# ผังจอเดินบนกริด 8 ขอบนอก 24 - หัวเรื่องหนึ่งบรรทัด กราฟซ้าย คอลัมน์ค่าอ่านขวา
# แถวปุ่มกับสถานะอยู่ล่างสุด ทุกอย่างเลี่ยงมุม x>=690 y>=340 ที่เป็นของปุ่ม Console
ui.Label("เฝ้าการสั่นของเครื่องจักร", x=24, y=8, color=COL_TEXT, value=28)
# วางป้ายนี้ไว้แถบล่าง ไม่ใช่ใต้หัวเรื่อง เพราะพื้นที่บนคือที่ของกราฟที่จะสร้าง
# หลังได้เส้นฐาน ป้ายชั่วคราวที่นั่งทับรอยเท้าของ widget ตัวถัดไปคือนิสัยที่พาไป
# ชนของจริงในวันที่ลืม hide()
busy = ui.Label("กำลังเก็บเส้นฐาน 3 หน้าต่าง", x=24, y=288, value=24,
                color=COL_DIM)
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
ch = ui.Chart(x=24, y=56, w=456, h=216, color=COL_CARD, min=0,
              max=int(alarm_at * 1000) + 60)
# สามเส้นนี้เดิมแยกด้วยสี ฟ้า/เหลือง/แดง ซึ่งอ่านไม่ออกเมื่อแปลงเป็นขาวดำ และ
# ยังกินสีสถานะไปเป็นของตกแต่ง ตอนนี้เส้นพระเอกคือ RMS ใช้สีเน้นเพียงเส้นเดียว
# อีกสองเส้นเป็นเกณฑ์คงที่ จึงเป็นเส้นแนวนอนที่แยกกันได้ด้วยตำแหน่งอยู่แล้ว
s_rms = ch.add_series(COL_ACCENT)
s_warn = ch.add_series(COL_DIM)
s_alarm = ch.add_series(COL_TEXT)
ui.Label("เส้นที่ขยับ = RMS ตอนนี้", x=496, y=56, color=COL_ACCENT, value=20)
ui.Label("เส้นล่าง = เฝ้าดู 2 เท่า", x=496, y=88, color=COL_DIM, value=20)
ui.Label("เส้นบน = เข้าตรวจ 4 เท่า", x=496, y=120, color=COL_TEXT, value=20)

# ตัวเลขกับแถบอยู่ติดกัน ตัวเลขบอกค่า แถบบอกว่าค่านั้นอยู่ตรงไหนของพิสัย 0-4
# ตาม §S7.7.8 ที่ห้ามวางตัวเลขลอยโดยไม่มีบริบท
ui.Label("กี่เท่าของเส้นฐาน (0-4)", x=496, y=160, color=COL_DIM, value=20)
seg = ui.Seg7(x=496, y=192, w=192, h=48, color=COL_ACCENT)
ratio_bar = ui.Bar(x=496, y=248, w=272, h=24, min=0, max=int(ALARM_K * 100),
                   value=0, color=COL_CARD)

# แถบล่าง - ปุ่มอยู่ใต้กราฟทางซ้าย สถานะอยู่ตรงกลาง ค่าเส้นฐานอยู่ขวาใต้แถบ
# สองบรรทัดขวาจบที่ y=339 จึงพ้นมุมของปุ่ม Console (x>=690 และ y>=340)
# ก่อนหน้านี้บรรทัดล่างสุดอยู่ที่ y=384 ซึ่งล้นขอบจอออกไป 13 px และหายไปเงียบ ๆ
btn_pause = ui.Button("หยุดกราฟ", x=24, y=288, w=192, h=88, color=COL_ACCENT,
                      value=24)
ID_PAUSE = btn_pause.id()
# ป้ายนี้บอกว่ากราฟกำลังเดินหรือถูกหยุดไว้ ตอนหยุดคือค่าบนจอไม่ใช่ค่าปัจจุบัน
# จึงเป็นสถานะเฝ้าระวังจริง ๆ ตาม §S7.7.5 ไม่ใช่การทาสีให้สวย
lbl_run = ui.Label("กำลังวัด", x=248, y=296, color=COL_DIM, value=20)
status = ui.Label("ปกติ", x=248, y=336, color=COL_OK, value=28)
ui.Label("เส้นฐาน RMS " + str(round(base, 3)), x=496, y=280, color=COL_DIM,
         value=20)
ui.Label("เก็บตอนเครื่องเดินปกติ", x=496, y=312, color=COL_DIM, value=20)
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
            lbl_run.color(COL_WARN if paused else COL_DIM)
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
        col = COL_BAD
    elif ratio >= WARN_K:
        gpio.led(2).on()
        tag = "เฝ้าดู"
        col = COL_WARN
    else:
        gpio.led(1).on()
        tag = "ปกติ"
        col = COL_OK

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
