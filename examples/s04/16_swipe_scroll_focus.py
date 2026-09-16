# 16_swipe_scroll_focus.py - ปัดนิ้ว เลื่อนรายการ และช่องไหนกำลังถูกเลือก
#
# ไฟล์นี้สอน: เหตุการณ์อีกสามกลุ่มที่ไม่ได้เกิดจากการ "กด" อะไรสักอย่าง
#             gesture       - ปัดนิ้วไปทางไหน
#             scroll_begin / scroll_end - เนื้อหาเริ่มเลื่อนและหยุดเลื่อน
#             focused / defocused - ตอนนี้นิ้วอยู่ที่ widget ตัวไหน
# ดูที่จอ   : ปัดนิ้วบนแผ่นพื้นด้านซ้าย แล้วดูทิศที่รายงาน
#             ปัดขึ้นลงบนรายชื่อด้านขวา แล้วดู scroll_begin กับ scroll_end
# กับดัก    : gesture จะไม่เกิดบน widget ที่เลื่อนตัวเองได้ เพราะ LVGL ถือว่า
#             นิ้วนั้นกำลังเลื่อนเนื้อหา ไม่ได้ปัดสั่งงาน จึงต้องปัดบน Panel
#             ที่ไม่เลื่อน
#
# ไฟล์นี้เคยใช้ ui.Roller เป็นตัวอย่างของ scroll แล้วมันเงียบสนิทบนบอร์ดจริง
# (Eva Kit, 16 ส.ค. 2026) เหตุผลอยู่ใน lv_roller.c ตอนสร้าง: มันสั่ง
# lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE) ใส่ตัวเอง แล้วขยับป้ายข้างใน
# ด้วยมือ - ในสายตา LVGL วงล้อ "ไม่ใช่ของที่เลื่อนได้" scroll_begin/scroll_end
# จึงไม่มีวันเกิดกับมัน สิ่งที่วงล้อรายงานคือ value_changed ตอนตัวเลือกเปลี่ยน
# ตัวที่เลื่อนจริงคือของที่มีเนื้อหายาวกว่ากรอบ เช่น ui.List ที่ใช้อยู่ตอนนี้

import lcd
import time
import ui

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC S7.13
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK = 0x30A46C

ui.screen()
time.sleep_ms(200)

ui.Label("ปัดนิ้ว เลื่อนรายการ และช่องที่ถูกเลือก", x=24, y=8,
         color=COL_TEXT, value=28)

# แผ่นพื้นสำหรับปัดนิ้ว - ต้องเป็นของที่ไม่เลื่อนตัวเอง
pad = ui.Panel(x=24, y=56, w=392, h=200, color=COL_CARD)
ui.Label("ปัดนิ้วตรงนี้", x=48, y=72, color=COL_DIM, value=20)
lbl_dir = ui.Label("ยังไม่ได้ปัด", x=48, y=112, color=COL_TEXT, value=28)
lbl_hint = ui.Label("ซ้าย ขวา ขึ้น ลง - ลองให้ครบสี่ทิศ", x=48, y=168,
                    color=COL_DIM, value=20)

# ui.List เลื่อนตัวเองได้ จึงเป็นตัวเดียวในจอที่ส่ง scroll_begin / scroll_end
# (ui.Roller ทำไม่ได้ เหตุผลอยู่ในหัวไฟล์)
lst = ui.List(x=448, y=56, w=320, h=232, color=COL_TEXT, value=20)
for name in ("กรุงเทพ", "เชียงใหม่", "ขอนแก่น", "ภูเก็ต", "สงขลา"):
    lst.add_item(name)

# ---- ขอรับเหตุการณ์ -------------------------------------------------------
#
# ขอเฉพาะที่จะอ่านจริง ไม่ขอเผื่อไว้ ทุกชนิดที่ขอเพิ่มคือช่องในคิวที่ถูกใช้
pad.listen("gesture")
lst.listen("scroll_begin", "scroll_end", "focused")
# ปุ่มล่างขอ focused / defocused เพื่อให้เห็นว่าโฟกัสย้ายเมื่อแตะที่อื่น
# ไม่ต้องมี lv_group และไม่ต้องมีคีย์บอร์ด - การแตะบนจอย้ายโฟกัสให้เอง

lbl_scroll = ui.Label("รายชื่อ: ยังไม่ได้เลื่อน", x=448, y=304, color=COL_DIM,
                      value=20)
lbl_focus = ui.Label("โฟกัส: ยังไม่มี", x=448, y=344, color=COL_DIM, value=20)

btn_a = ui.Button("ช่อง ก", x=24, y=280, w=200, h=88, color=0x3A4150,
                  value=24)
btn_b = ui.Button("ช่อง ข", x=240, y=280, w=200, h=88, color=0x3A4150,
                  value=24)
btn_a.listen("focused", "defocused")
btn_b.listen("focused", "defocused")

NAME_OF = {
    pad.id(): "แผ่นปัด",
    lst.id(): "รายชื่อ",
    btn_a.id(): "ช่อง ก",
    btn_b.id(): "ช่อง ข",
}

# ทิศที่ ev["value"] พกมา เทียบกับค่าคงที่ของโมดูล ui ไม่ใช่ตัวเลขดิบ
DIR_NAME = {
    ui.DIR_LEFT: "ปัดไปทางซ้าย",
    ui.DIR_RIGHT: "ปัดไปทางขวา",
    ui.DIR_TOP: "ปัดขึ้น",
    ui.DIR_BOTTOM: "ปัดลง",
}

lcd.clear()
lcd.console("<h2>gesture / scroll / focus</h2>")
lcd.print("gesture ไม่เกิดบนของที่เลื่อนได้ - ปัดบน Panel เท่านั้น")
lcd.print("Roller ไม่ส่ง scroll เลย - มันไม่ใช่ของที่เลื่อนได้ในสายตา LVGL")

scrolling = False

while True:
    for ev in ui.poll():
        h, t, v = ev["handle"], ev["type"], ev["value"]
        who = NAME_OF.get(h, "?")

        if t == "gesture":
            lbl_dir.text(DIR_NAME.get(v, "ทิศที่อ่านไม่ออก"))
            lbl_dir.color(COL_ACCENT)
        elif t == "scroll_begin":
            scrolling = True
            lbl_scroll.text("รายชื่อ: กำลังเลื่อน")
            lbl_scroll.color(COL_ACCENT)
        elif t == "scroll_end":
            # จังหวะนี้เท่านั้นที่ค่าที่เลือกนิ่งแล้ว ถ้าจะบันทึกลงเครือข่าย
            # ให้ทำตรงนี้ ไม่ใช่ตอน value_changed ซึ่งมาถี่ระหว่างนิ้วยังลาก
            scrolling = False
            # scroll_end อาจมาถึงมากกว่าหนึ่งครั้งต่อการปัดหนึ่งที เพราะ
            # LVGL รายงานแยกแกน และรายงานอีกครั้งเมื่อภาพเคลื่อนไหวหยุด
            # ให้ถือว่ามันแปลว่า "นิ่งแล้ว" ไม่ใช่ "เกิดขึ้นหนึ่งครั้ง"
            lbl_scroll.text("รายชื่อ: หยุดแล้ว")
            lbl_scroll.color(COL_OK)
        elif t == "focused":
            lbl_focus.text("โฟกัส: " + who)
            lbl_focus.color(COL_TEXT)
        elif t == "defocused":
            lbl_focus.text("โฟกัสออกจาก " + who + " แล้ว")
            lbl_focus.color(COL_DIM)

        lcd.print(who + " " + t + " value=" + str(v))

    time.sleep_ms(30)

# ตาคุณ
# 1) ปัดนิ้วบนรายชื่อแทนที่จะปัดบนแผ่นพื้น - ไม่มี gesture มา แต่มี
#    scroll_begin มาแทน อธิบายว่าทำไม LVGL ตัดสินใจแบบนั้น
# 2) เอา "focused" ออกจาก .listen() ของรายชื่อ แล้วแตะสลับสามที่ - บรรทัด
#    โฟกัสจะพูดถึงรายชื่ออีกไหม
# 3) ให้ scroll_end ส่งค่าที่เลือกขึ้นเครือข่าย แล้วเทียบจำนวนข้อความที่ส่ง
#    กับถ้าใช้ value_changed - นี่คือเหตุผลที่คาบ 10 ต้องรู้จักสองชนิดนี้
