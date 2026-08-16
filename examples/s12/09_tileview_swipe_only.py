# 09_tileview_swipe_only.py - จอที่นิ้วพาไปได้ แต่โปรแกรมพาไปไม่ได้
# ชุดตัวอย่างประจำคาบ 12
#
# Why : 04_heartbeat_and_alert.py ตั้งกติกาไว้ว่าเมื่อค่าออกนอกเกณฑ์ สิ่งที่ต้อง
#       เกิดคือ "คนหน้างานต้องเห็น" ไม่ใช่แค่ "ข้อความถูกส่งขึ้น broker" พอทีม
#       เริ่มทำจอหลายหน้าเพื่อยัดข้อมูลของ capstone ให้ครบ คำถามถัดมาคือ ตอน
#       เกิดเหตุ โปรแกรมจะพาคนกลับมาหน้าที่ควรดูได้อย่างไร - widget ตัวนี้ตอบว่า
#       "ไม่ได้" และรู้ตอนวางผังดีกว่ารู้ตอนสาธิตหน้ากรรมการ
# What: ui.Tileview คือผืนใหญ่ที่แบ่งเป็นช่องขนาดเท่าตัวมันเอง ปัดนิ้วแล้วเลื่อน
#       ไปทีละช่อง .add_tile(คอลัมน์, แถว, ทิศที่ปัดออกได้) คืนช่องมาหนึ่งช่อง
#       เอาไปใส่ parent= ของ widget ที่จะอยู่ในช่องนั้น
# How : ช่อง (0,0) คือช่องที่โผล่ตอนเริ่ม ทิศที่อนุญาตกำหนดว่าปัดออกไปไหนได้
#       เช่น ui.DIR_RIGHT หรือ ui.DIR_RIGHT | ui.DIR_BOTTOM
#
# ดูที่จอ: แถบบนสุดอยู่นอก Tileview บอกว่าโปรแกรม "ขอ" ช่องไหน ส่วน Tileview
#          ใต้ลงมายังโชว์ช่องที่ 1 อยู่ตลอด สองอย่างนี้ขัดกันในภาพเดียว - นั่น
#          คือหลักฐานว่าคำสั่งย้ายช่องไม่มีอยู่จริง บรรทัดล่างนับให้ว่ากี่ครั้ง
# กับดัก : Tileview เป็นทางเดียว - นิ้วสั่งได้ โปรแกรมสั่งไม่ได้ และโปรแกรมไม่รู้
#          ด้วยว่าตอนนี้อยู่ช่องไหน สามข้อนี้อ่านจากซอร์สได้ตรง ๆ
#            1. ตอนสร้าง Tileview ไม่มี lv_obj_add_event_cb สักบรรทัด ต่างจาก
#               Tabview ที่ผูก LV_EVENT_VALUE_CHANGED ไว้ - จึงไม่มี event เลย
#            2. ui_widget_mgr_set_value() ไม่มี case ของ Tileview .value(n)
#               จึงเงียบ ส่วน ui.PROP_ACTIVE_TAB ถูกกันไว้เฉพาะชนิด Tabview
#            3. ui_widget_mgr_get_value() ตกไปที่ default คืน 0 เสมอ ค่าที่
#               .value() ตอบกลับมาจึงไม่ใช่หมายเลขช่อง แต่เป็นศูนย์เปล่า ๆ
#          ผลกับงานจริง: ของที่ต้องเห็นตอนเกิดเหตุ ห้ามอยู่ในช่องของ Tileview
#          ต้องอยู่นอกมัน อย่างแถบข้างบนในไฟล์นี้ ซึ่งอยู่บนจอตลอดเวลาไม่ว่าคน
#          จะปัดไปดูช่องไหนค้างไว้ก็ตาม
#
# บน Eva Kit: sensors.snapshot() อ่านได้ตามปกติ อุณหภูมิมาจากชิป IMU จริง

import lcd
import sensors
import time
import ui

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_WARN = 0xF5A623

ROUNDS = 90
PERIOD_MS = 200
SHOW_EVERY = 5      # 1 วินาทีต่อการเปลี่ยนตัวเลขหนึ่งครั้ง ตามเกณฑ์ S7.7.4

ui.screen()
time.sleep_ms(200)

ui.Label("ปัดได้ แต่สั่งไม่ได้", x=24, y=8, color=COL_TEXT, value=28)

# ---- แถบข้อมูล: อยู่นอก Tileview จึงไม่มีวันถูกปัดหาย ---------------------
ui.Panel(x=24, y=56, w=744, h=88, color=COL_CARD, max=8)
ui.Label("แถบนี้อยู่นอก Tileview", x=48, y=72, color=COL_DIM, value=20)
ask_lbl = ui.Label("ยังไม่ได้ขอช่องไหน", x=48, y=104, color=COL_WARN, value=24)
temp_lbl = ui.Label("-- องศา", x=560, y=104, color=COL_TEXT, value=24)

# ---- Tileview: สามช่องไว้ให้คนเดินดู ไม่ใช่ไว้ให้โปรแกรมพาไป --------------
tiles = ui.Tileview(x=24, y=160, w=744, h=160, color=COL_CARD)

t_now = tiles.add_tile(0, 0, ui.DIR_RIGHT)
t_hist = tiles.add_tile(1, 0, ui.DIR_LEFT | ui.DIR_RIGHT)
t_conf = tiles.add_tile(2, 0, ui.DIR_LEFT)

ui.Label("ช่อง 1 - ค่าปัจจุบัน", x=24, y=16, color=COL_ACCENT, value=24,
         parent=t_now)
now_lbl = ui.Label("รออ่านค่าแรก", x=24, y=64, color=COL_TEXT, value=24,
                   parent=t_now)

ui.Label("ช่อง 2 - ค่าสูงสุดที่เจอ", x=24, y=16, color=COL_ACCENT, value=24,
         parent=t_hist)
hist_lbl = ui.Label("ยังไม่มีข้อมูล", x=24, y=64, color=COL_TEXT, value=24,
                    parent=t_hist)

ui.Label("ช่อง 3 - เกณฑ์ที่ตั้งไว้", x=24, y=16, color=COL_ACCENT, value=24,
         parent=t_conf)
ui.Label("รอบส่ง 5 วินาที", x=24, y=64, color=COL_TEXT, value=24, parent=t_conf)

line = ui.Label("ยังไม่ได้สั่งย้ายช่อง", x=24, y=336, color=COL_DIM, value=20)

lcd.clear()
lcd.console("<h2>Tileview - ทางเดียว นิ้วเท่านั้น</h2>")
lcd.print("สามช่อง กิน 4 แฮนเดิล ตั้งแต่ยังไม่มีของอยู่ในนั้น")
lcd.print("ถามว่าอยู่ช่องไหน ได้", tiles.value(), "- ไม่ใช่หมายเลขช่อง")

asked = 0
events = 0
want = 0
hottest = None

for i in range(ROUNDS):
    snap = sensors.snapshot()
    if "bmi270" not in snap:
        lcd.print("<span class=warn>ไม่มีค่าจาก IMU รอบนี้</span>")
        ui.poll()
        time.sleep_ms(PERIOD_MS)
        continue

    temp = snap["bmi270"]["temperature"]
    if hottest is None or temp > hottest:
        hottest = temp

    if i % SHOW_EVERY == 0:
        shown = "{:.1f}".format(temp)
        temp_lbl.text(shown + " องศา")
        now_lbl.text(shown + " องศา")
        hist_lbl.text("สูงสุด {:.1f} องศา".format(hottest))

        # โปรแกรมวนขอช่องถัดไปทุกวินาที เหมือนจอ kiosk ที่ต้องเวียนหน้าเอง
        want = (want + 1) % 3
        ask_lbl.text("โปรแกรมขอช่องที่ " + str(want + 1))

        # สองบรรทัดนี้คือสิ่งที่ทุกคนลองก่อน และทั้งคู่ไม่ทำอะไรเลยสักอย่าง
        tiles.value(want)
        tiles.prop(ui.PROP_ACTIVE_TAB, want)
        asked += 1
        line.text("สั่งย้ายช่องไปแล้ว " + str(asked) + " ครั้ง จอไม่ขยับ")

    # Tileview ไม่ได้ผูก callback ไว้เลย ตัวเลขนี้จึงเป็นศูนย์ ต่อให้ปัดจนเมื่อย
    for ev in ui.poll():
        if ev["handle"] == tiles.id():
            events += 1
    time.sleep_ms(PERIOD_MS)

line.text("สั่งย้าย " + str(asked) + " ครั้ง ได้ event " + str(events) + " ครั้ง")
lcd.print("<span class=warn>สั่งย้ายช่อง", asked, "ครั้ง จอไม่ขยับ</span>")
lcd.print("event จาก Tileview =", events)
print("Tileview: นิ้วสั่งได้ โปรแกรมสั่งไม่ได้ และถามไม่ได้ว่าอยู่ช่องไหน")

# ตาคุณ
# 1) ย้ายแถบข้อมูลข้างบนลงไปไว้ในช่อง 1 (ใส่ parent=t_now) แล้วปัดไปค้างที่
#    ช่อง 3 - ค่าที่ทีมอยากให้คนเห็นจะหายไปจากสายตาทันที นี่คือเหตุผลที่กติกา
#    ตั้งแต่คาบ 4 เขียนไว้ว่าของที่ต้องเห็นตลอดห้ามอยู่ในหน้าย่อย
# 2) เปลี่ยน ui.Tileview เป็น ui.Tabview แล้วใช้ .prop(ui.PROP_ACTIVE_TAB, want)
#    ที่เดิม - คราวนี้จอย้ายให้จริง ตอบให้ได้ว่าจอของทีมเราต้องการอันไหน
#    ระหว่าง "ปัดสวยแต่โปรแกรมสั่งไม่ได้" กับ "มีแถบแท็บแต่สั่งได้"
# 3) เปลี่ยนทิศของช่องแรกเป็น ui.DIR_ALL แล้วลองปัดขึ้นลง - มันเด้งกลับ เพราะ
#    ไม่มีช่องในแนวตั้ง ทิศที่อนุญาตไม่ได้แปลว่ามีช่องอยู่จริงในทิศนั้น
