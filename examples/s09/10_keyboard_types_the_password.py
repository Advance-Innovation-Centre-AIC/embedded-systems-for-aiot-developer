# 10_keyboard_types_the_password.py - รหัสผ่านควรพิมพ์บนจอ ไม่ใช่ฝังในโค้ด
# ชุดตัวอย่างประจำคาบ 09
#
# Why : ตัวอย่างทุกไฟล์ของคาบ 9 ถึง 12 ขึ้นต้นด้วย WIFI_SSID กับ WIFI_PASS
#       ที่เขียนตายไว้ในโค้ด แปลว่าเปลี่ยนห้องเรียนทีต้องแก้ไฟล์แล้วแฟลชใหม่
#       ทุกโต๊ะ วันที่ 14 ส.ค. 2026 วงที่ตัวอย่างใช้ไม่มีอยู่ในห้องจริง
#       (HARDWARE_CHECKLIST.md ข้อ 6) ทั้งห้องจึงค้างที่ป้าย "กำลังต่อ" พร้อมกัน
#       ของจริงทุกตัวที่ต่อ WiFi ได้ ถามรหัสผ่านจากคน ไม่ได้ฝังมาจากโรงงาน
# What: ui.Keyboard คือแป้นพิมพ์เต็มบนจอ ผูกกับ ui.Textarea หนึ่งช่องด้วย
#       .bind() แล้วทุกปุ่มที่แตะจะไปโผล่ในช่องนั้นเอง โดยเราไม่ต้องเขียนโค้ดรับ
# How : สร้าง Textarea ก่อน สร้าง Keyboard ทีหลัง แล้ว kb.bind(ta)
#       ช่องรหัสผ่านเปิดโหมดดาวด้วย .prop(ui.PROP_PASSWORD, 1)
#
# ดูที่จอ: ช่องซ้ายบนคือชื่อวงที่ได้จาก wifi.scan() จริง ช่องขวาบนคือรหัสผ่าน
#          ที่เราพิมพ์เอง แป้นพิมพ์อยู่ครึ่งล่างซ้าย ปุ่มเชื่อมต่ออยู่ขวา
#
# กับดักที่ต้องรู้ก่อนวางแผนใช้งาน - อ่านให้จบก่อนเอาไปต่อยอด:
#
#   1) Keyboard ไม่ส่ง event ใด ๆ เลย ui_widget_mgr.c ไม่ได้ลงทะเบียน
#      lv_obj_add_event_cb ให้มัน ต่างจาก Button หรือ Roller ที่ลงทะเบียนไว้
#   2) และ "ยังไม่มีคำสั่งอ่านข้อความกลับ" - ตาราง IPC_CMD_UI_* ใน
#      ipc_ui_protocol.h มี SET_TEXT (0x52) แต่ไม่มี GET_TEXT ส่วน GET_VALUE
#      (0x5B) ตกลง default แล้วคืน 0 เมื่อถูกถามด้วย Textarea
#
#   รวมสองข้อแปลว่า: สิ่งที่พิมพ์ลงจอ "คนอ่านได้ แต่โปรแกรมยังอ่านไม่ได้"
#   ไฟล์นี้จึงยังต้องต่อด้วยค่าคงที่ข้างล่าง และนั่นคือหลักฐานของช่องว่างนี้เอง
#   ไม่ใช่ความผิดพลาดของไฟล์ - กดปุ่มเชื่อมต่อแล้วดูว่ามันใช้รหัสจากตัวแปร
#   ไม่ใช่จากที่เราเพิ่งพิมพ์
#
#   ทางที่ใช้ได้วันนี้ถ้าต้องรับตัวอักษรจากนิ้วจริง ๆ คือ ui.ButtonMatrix
#   ซึ่งส่ง value_changed พร้อม "ลำดับปุ่ม" กลับมา แล้วเราประกอบสตริงเองฝั่ง
#   MicroPython - ช้ากว่าแต่ค่าที่ได้อยู่ในมือเรา
#
# เรื่องขนาดที่ต้องยอมรับ: กติกาเป้าสัมผัสของหลักสูตร (S7.13.11) คือ 88 พิกเซล
#   แป้นพิมพ์มีสี่แถว 4 x 88 = 352 พิกเซล ซึ่งกินพื้นที่วาด 398 ไปเกือบหมด
#   ปุ่มบนแป้นพิมพ์จึงเล็กกว่าเกณฑ์เสมอบนจอ 4.3 นิ้ว นี่เป็นข้อจำกัดของขนาดจอ
#   ไม่ใช่ของ widget และเป็นเหตุผลข้อหนึ่งที่ของจริงหลายตัวเลือกวิธี provision
#   ทางอื่น เช่น SoftAP แล้วกรอกจากมือถือ (ดู 08_softap_fallback.py)

import lcd
import time
import ui
import wifi

WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
RUN_MS = 45000

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_ACCENT = 0x4A9EFF
COL_WARN = 0xF5A623
COL_OK = 0x30A46C

ui.screen()
time.sleep_ms(200)

ui.Label("รหัสผ่านพิมพ์บนจอ", x=24, y=16, color=COL_TEXT, value=28)

ui.Label("ชื่อวง - จาก wifi.scan()", x=24, y=64, color=COL_DIM, value=20)
# ช่องซ้ายไม่สั่ง ONE_LINE จึงสูง 88 ตามที่ขอ - เทียบกับช่องขวาที่สั่ง แล้วดู
# ความสูงบนจอ สองช่องนี้ขอ h=88 เท่ากันแต่ได้ไม่เท่ากัน
ta_ssid = ui.Textarea(text="กำลังสแกน", x=24, y=96, w=328, h=88,
                      color=COL_TEXT)

ui.Label("รหัสผ่าน - พิมพ์เอง", x=384, y=64, color=COL_DIM, value=20)
ta_pass = ui.Textarea(text="", x=384, y=96, w=304, h=88, color=COL_TEXT)
# ONE_LINE เรียก lv_textarea_set_one_line() ซึ่ง "เขียนทับความสูง" ที่เราตั้งไว้
# ให้เหลือพอดีหนึ่งบรรทัด เป้าสัมผัสจึงเตี้ยกว่า 88 พิกเซลที่กติกาต้องการ
# อาการเดียวกับ PROP_VISIBLE_ROWS ของ Roller ใน s05/10 - prop ชนะ h= เงียบ ๆ
ta_pass.prop(ui.PROP_ONE_LINE, 1)
ta_pass.prop(ui.PROP_PASSWORD, 1)      # โหมดดาว - คนข้าง ๆ ไม่ควรอ่านออก

# แป้นพิมพ์ต้องสร้างหลังช่องที่มันจะพิมพ์ลง เพราะ .bind() ส่ง "แฮนเดิล" ของช่อง
# ข้ามไปให้ CM55 และ CM55 ปฏิเสธเงียบ ๆ ถ้าแฮนเดิลนั้นยังไม่ใช่ Textarea
kb = ui.Keyboard(x=24, y=200, w=440, h=168, color=COL_TEXT)
kb.bind(ta_pass)

btn = ui.Button("เชื่อมต่อ", x=496, y=200, w=192, h=88, color=COL_ACCENT,
                value=24)
note = ui.Label("แตะเพื่อลองต่อ", x=496, y=304, color=COL_DIM, value=20)

lcd.clear()
lcd.console("<h2>Keyboard - แป้นพิมพ์บนจอ</h2>")
lcd.print("Keyboard ไม่ส่ง event และยังไม่มี GET_TEXT")
lcd.print("สิ่งที่พิมพ์: คนอ่านได้ โปรแกรมยังอ่านไม่ได้")

# --- ของจริงชิ้นเดียวในไฟล์นี้: รายชื่อวงที่บอร์ดสแกนเจอเดี๋ยวนี้ ---------
# scan() คืน list ของ tuple (ssid, rssi, channel, security) - อ่านด้วยลำดับช่อง
# ไม่ใช่ด้วยชื่อคีย์ (ดู 01_scan_tuples.py)
best = ""
best_rssi = -200        # ต่ำกว่าที่วิทยุใด ๆ รายงานได้ จึงแพ้ทุกวงที่เจอจริง
try:
    for row in wifi.scan():
        if row[1] > best_rssi:
            best = row[0]
            best_rssi = row[1]
except OSError:
    best = ""

if best:
    ta_ssid.text(best)
    lcd.print("วงที่แรงที่สุดตอนนี้:", best)
else:
    ta_ssid.text("สแกนไม่เจอวงไหนเลย")
    lcd.print("<span class=warn>สแกนไม่เจอวง - ตรวจสายอากาศ</span>")

t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        # จะไม่มี event ของ kb หรือ ta_pass โผล่มาที่นี่เลย ลองพิมพ์ดูแล้วนับ
        if ev["handle"] == btn.id() and ev["type"] == "clicked":
            note.color(COL_WARN)
            note.text("กำลังต่อด้วยรหัสจากตัวแปร")
            ui.poll()
            ok = wifi.connect(best, WIFI_PASS) if best else False
            if ok:
                note.color(COL_OK)
                note.text("ต่อได้ ip " + wifi.ip())
                lcd.print("ต่อสำเร็จ", wifi.ip())
            else:
                note.color(COL_DIM)
                note.text("ต่อไม่ได้ - แก้ WIFI_PASS")
                lcd.print("ต่อไม่สำเร็จ - รหัสที่พิมพ์บนจอไม่ได้ถูกใช้")
        else:
            lcd.print("event:", ev["type"], "handle", ev["handle"])

    time.sleep_ms(80)

note.text("หมดเวลา")
lcd.print("<span class=ok>จบ - นับ event ของแป้นพิมพ์ได้ศูนย์</span>")
print("Keyboard วาดได้ ผูกช่องได้ แต่ยังส่งค่ากลับ MicroPython ไม่ได้")

# ตาคุณ
# 1) พิมพ์อะไรก็ได้ลงช่องรหัสผ่านสักสิบตัว แล้วนับบรรทัด event ในคอนโซล
#    จะได้ศูนย์ นี่คือวิธีพิสูจน์ข้อ 1 ของกับดักด้วยมือตัวเอง
# 2) เอา ta_pass.prop(ui.PROP_PASSWORD, 1) ออก แล้วพิมพ์ใหม่ - ตัวอักษรจะโผล่
#    ให้ทั้งห้องเห็น ลองคิดว่าถ้าเป็นหน้าจอเครื่องจริงในโรงงาน ใครยืนดูอยู่บ้าง
# 3) สั่ง kb.bind(ta_ssid) แทน แล้วดูว่าพิมพ์ลงช่องไหน - แป้นพิมพ์หนึ่งอันผูกได้
#    ทีละช่องเดียว ถ้าจอมีสองช่อง ต้องมีคนตัดสินใจว่าตอนนี้ผูกกับช่องไหน
