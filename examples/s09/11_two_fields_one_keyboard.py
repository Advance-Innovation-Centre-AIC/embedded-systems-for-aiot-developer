# 11_two_fields_one_keyboard.py - ฟอร์มสองช่อง แป้นพิมพ์เดียว และการอ่านค่ากลับ
# ชุดตัวอย่างประจำคาบ 09
#
# Why : ไฟล์ 10 จบด้วยคำถามค้างไว้ว่า "แป้นพิมพ์หนึ่งอันผูกได้ทีละช่องเดียว
#       ถ้าจอมีสองช่อง ต้องมีคนตัดสินใจว่าตอนนี้ผูกกับช่องไหน" ไฟล์นี้ตอบ
#       และคำตอบคือเหตุการณ์ focused ซึ่งบอกได้ว่านิ้วเพิ่งไปแตะช่องไหน
# What: focused / defocused บอกว่าช่องไหนกำลังถูกแก้
#       ready  บอกว่าคนกดตกลงหรือกด Enter ในช่องบรรทัดเดียว - พิมพ์เสร็จแล้ว
#       cancel บอกว่าคนกดปิดแป้นพิมพ์ - เลิกพิมพ์แล้ว
#       .text() ที่ไม่ใส่อาร์กิวเมนต์ อ่านสิ่งที่พิมพ์กลับมาเป็นสตริง
# How : สร้างสองช่อง แล้ว .listen("focused") ทั้งคู่ พอ focused มาถึงก็สั่ง
#       kb.bind() ไปที่ช่องนั้น แป้นพิมพ์อันเดียวจึงเดินตามนิ้ว
#
# ดูที่จอ: แตะช่องซ้าย พิมพ์ชื่อวง แตะช่องขวา พิมพ์รหัส แล้วกดตกลงบนแป้นพิมพ์
#          บรรทัดล่างจะรายงานความยาวของสิ่งที่อ่านกลับมาได้ ไม่ใช่ตัวรหัสเอง
#
# กับดัก : ช่องรหัสผ่านเปิดโหมดดาว จอจึงแสดงดาว แต่ .text() คืน "ตัวจริง"
#          ไม่ใช่ดาว - โหมดดาวเป็นเรื่องของการวาด ไม่ใช่ของข้อมูล
#          อย่าพิมพ์ค่าที่อ่านได้ลงคอนโซล เพราะคอนโซลคือหน้าจอที่ทั้งห้องเห็น

import lcd
import time
import ui
import wifi

RUN_MS = 60000

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_ACCENT = 0x4A9EFF
COL_WARN = 0xF5A623
COL_OK = 0x30A46C

ui.screen()
time.sleep_ms(200)

ui.Label("ฟอร์มสองช่อง แป้นพิมพ์เดียว", x=24, y=8, color=COL_TEXT, value=28)

ui.Label("ชื่อวง", x=24, y=48, color=COL_DIM, value=20)
ta_ssid = ui.Textarea(text="", x=24, y=72, w=328, h=88, color=COL_TEXT)

ui.Label("รหัสผ่าน", x=384, y=48, color=COL_DIM, value=20)
ta_pass = ui.Textarea(text="", x=384, y=72, w=304, h=88, color=COL_TEXT)
ta_pass.prop(ui.PROP_PASSWORD, 1)      # โหมดดาว - คนข้าง ๆ ไม่ควรอ่านออก

# ขอรับ focused ทั้งสองช่อง นี่คือทั้งหมดที่ต้องทำให้แป้นพิมพ์เดินตามนิ้ว
# บนจอสัมผัสไม่ต้องมี lv_group และไม่ต้องมีปุ่ม Tab - การแตะย้ายโฟกัสให้เอง
# ขอ pressed มาด้วย ไม่ใช่เพราะบทเรียนต้องใช้ แต่เพราะมันแยกสองสาเหตุออกจากกัน
# ได้ในบรรทัดเดียว - ถ้าเห็น pressed แต่ไม่เห็น focused แปลว่านิ้วถึง widget แล้ว
# แต่ LVGL ไม่ได้ส่ง focused ส่วนถ้าไม่เห็นอะไรเลย แปลว่านิ้วไปไม่ถึงตั้งแต่ต้น
ta_ssid.listen("focused", "pressed")
ta_pass.listen("focused", "pressed")

kb = ui.Keyboard(x=24, y=176, w=440, h=192, color=COL_TEXT)
kb.bind(ta_ssid)                       # เริ่มที่ช่องซ้าย
# ready มาถึงเมื่อกดปุ่มตกลงบนแป้นพิมพ์ cancel เมื่อกดปิด
kb.listen("ready", "cancel")

lbl_focus = ui.Label("กำลังแก้: ชื่อวง", x=496, y=176, color=COL_ACCENT,
                     value=24)
lbl_read = ui.Label("ยังไม่ได้อ่านค่ากลับ", x=496, y=216, color=COL_DIM,
                    value=20)
lbl_state = ui.Label("กดตกลงเมื่อพิมพ์ครบ", x=496, y=248, color=COL_DIM,
                     value=20)

btn = ui.Button("เชื่อมต่อ", x=496, y=280, w=192, h=88, color=COL_ACCENT,
                value=24)

FIELD_NAME = {ta_ssid.id(): "ชื่อวง", ta_pass.id(): "รหัสผ่าน"}
FIELD_OBJ = {ta_ssid.id(): ta_ssid, ta_pass.id(): ta_pass}

# ---- บันทึกเหตุการณ์ลงจอ -------------------------------------------------
#
# ทำไมต้องมี: 16 ส.ค. 2026 บนบอร์ดจริง แตะช่องซ้าย -> ขวา -> กลับมาซ้าย แล้ว
# ตัวอักษรยังลงช่องขวา ตัวจำลองทำซ้ำอาการไม่ได้ - ที่นั่น focused มาครบทั้งสาม
# ครั้งและ bind ย้ายถูกต้อง เมื่อของสองที่ไม่ตรงกัน สิ่งที่ต้องทำคือให้ของจริง
# พูด ไม่ใช่เดาต่อจากของจำลอง บรรทัดนี้จึงพิมพ์ทุกเหตุการณ์ลงจอตามที่มันมาถึง
lbl_log = ui.Label("log: ยังไม่มีเหตุการณ์", x=24, y=368, color=COL_DIM,
                   value=20)
log = []


def log_event(handle, kind):
    """แสดงสามเหตุการณ์ล่าสุด - h คือแฮนเดิล ตามด้วยชนิด"""
    log.append(str(handle) + ":" + kind)
    while len(log) > 3:
        log.pop(0)
    lbl_log.text("log " + " ".join(log))
    print("EV", handle, kind)

lcd.clear()
lcd.console("<h2>ฟอร์มสองช่อง แป้นพิมพ์เดียว</h2>")
lcd.print("focused บอกว่าแตะช่องไหน - แป้นพิมพ์ผูกตามไปเอง")
lcd.print("ready บอกว่าพิมพ์เสร็จ - .text() อ่านค่ากลับมาได้")


def report():
    """รายงานความยาวที่อ่านกลับได้ ไม่ใช่ตัวข้อความ - คอนโซลคือจอสาธารณะ"""
    ssid = ta_ssid.text()
    pw = ta_pass.text()
    lbl_read.text("อ่านกลับได้ " + str(len(ssid)) + " และ " +
                  str(len(pw)) + " ตัวอักษร")
    lbl_read.color(COL_TEXT if ssid else COL_DIM)
    lcd.print("อ่านกลับ: ชื่อวง", len(ssid), "ตัว รหัส", len(pw), "ตัว")
    return ssid, pw


t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
    for ev in ui.poll():
        h, t = ev["handle"], ev["type"]
        log_event(h, t)

        if t == "focused" and h in FIELD_OBJ:
            # แป้นพิมพ์อันเดียวเดินตามนิ้ว - หนึ่งบรรทัดนี้คือทั้งหมดของกลไก
            kb.bind(FIELD_OBJ[h])
            lbl_focus.text("กำลังแก้: " + FIELD_NAME[h])
            lbl_focus.color(COL_ACCENT)

        elif t == "ready":
            # ปุ่มตกลงบนแป้นพิมพ์ หรือ Enter ในช่องบรรทัดเดียว
            lbl_state.text("พิมพ์เสร็จแล้ว")
            lbl_state.color(COL_OK)
            report()

        elif t == "cancel":
            lbl_state.text("ปิดแป้นพิมพ์ - ยังไม่ได้ยืนยัน")
            lbl_state.color(COL_WARN)

        elif h == btn.id() and t == "clicked":
            ssid, pw = report()
            if not ssid:
                lbl_state.text("ยังไม่ได้พิมพ์ชื่อวง")
                lbl_state.color(COL_WARN)
                continue
            lbl_state.text("กำลังต่อ " + ssid)
            lbl_state.color(COL_WARN)
            ui.poll()                  # ให้ป้ายถึงจอก่อนบรรทัดที่บล็อก
            try:
                ok = wifi.connect(ssid, pw)
            except OSError:
                ok = False
            if ok:
                lbl_state.text("ต่อได้ ip " + wifi.ip())
                lbl_state.color(COL_OK)
                lcd.print("<span class=ok>ต่อสำเร็จ</span>", wifi.ip())
            else:
                lbl_state.text("ต่อไม่ได้ - ตรวจชื่อวงและรหัส")
                lbl_state.color(COL_WARN)
                lcd.print("<span class=warn>ต่อไม่สำเร็จ</span>")

    time.sleep_ms(50)

lbl_state.text("หมดเวลา")
print("สองช่อง แป้นพิมพ์เดียว - focused ผูกให้ ready บอกว่าเสร็จ")

# ตาคุณ
# 1) เอา ta_pass.listen("focused") ออก แล้วแตะช่องขวา - พิมพ์ลงช่องไหน
#    นี่คือเหตุผลที่ต้องขอ event ทีละช่อง ไม่ใช่ขอทีเดียวทั้งจอ
# 2) เปลี่ยน report() ให้พิมพ์ตัวรหัสจริงลงคอนโซล แล้วมองรอบห้องหนึ่งรอบ
#    ก่อนตัดสินใจว่าจะเก็บโค้ดบรรทัดนั้นไว้ไหม
# 3) เพิ่มช่องที่สามสำหรับชื่อผู้ใช้ - โค้ดในลูปต้องแก้กี่บรรทัด
#    (ตอบ: ศูนย์ ถ้าใส่ช่องใหม่ลง FIELD_OBJ กับ FIELD_NAME ให้ครบ)
