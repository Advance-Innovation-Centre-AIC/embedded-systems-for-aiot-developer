# tabview.py - ui.Tabview ตัวเดียว
#
# ทำอะไร  : คอนเทนเนอร์ที่แบ่งพื้นที่เดิมออกเป็นหลายหน้า สลับด้วยการแตะแถบบนสุด
#           .add_tab("ชื่อ") คืน "หน้า" มาหนึ่งใบ เอาไปใส่ parent= ของลูก
#           value= คือความสูงของแถบแท็บ ไม่ใช่แท็บที่เปิดอยู่
# ดูที่จอ : แถบสามแท็บไทยด้านบน ใต้ลงมาคือของประจำแท็บที่เลือก
#           อีกหกวินาทีต่อมาโปรแกรมสั่งสลับแท็บเอง ให้เห็นว่าทั้งสามหน้ามีของจริง
#
# แบบไหนคือพัง:
#   - ชื่อแท็บไทยเป็นกล่องสี่เหลี่ยม = ฟอนต์ไทยไปไม่ถึงปุ่มบนแถบแท็บ
#   - ของทุกแท็บซ้อนกันอยู่หน้าเดียว = ลืมส่ง parent= ให้ widget ลูก
#   - แถบแท็บบางจนแตะไม่โดน = value= ไม่ทำงาน ค่าปริยายคือ 40 พิกเซล
#     ซึ่งต่ำกว่าเป้าสัมผัส 88 พิกเซลที่กติกากำหนด จึงต้องตั้งเองทุกครั้ง
#
# รันจบเองใน 24 วินาที ไม่ต้องต่อเน็ต ไม่ต้องมีเซนเซอร์

import time
import ui

COL_TEXT = 0xE8EAED
COL_DIM = 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF

NAMES = ("ตอนนี้", "ย้อนหลัง", "ตั้งค่า")

ui.screen()
time.sleep_ms(200)

ui.Label("ui.Tabview", x=24, y=16, color=COL_TEXT, value=28)

tv = ui.Tabview(x=24, y=72, w=744, h=248, color=COL_CARD, value=88)

tab_a = tv.add_tab(NAMES[0])
tab_b = tv.add_tab(NAMES[1])
tab_c = tv.add_tab(NAMES[2])

# พิกัดของลูกนับจากมุมซ้ายบนของหน้า ไม่ใช่ของจอ
ui.Label("อุณหภูมิ 25.4 องศา", x=16, y=16, color=COL_TEXT, value=24,
         parent=tab_a)
ui.Label("อยู่ในพิสัยปกติ", x=16, y=56, color=COL_DIM, value=20, parent=tab_a)

ui.Label("สูงสุดวันนี้ 31.2 องศา", x=16, y=16, color=COL_TEXT, value=24,
         parent=tab_b)
ui.Label("ต่ำสุดวันนี้ 22.8 องศา", x=16, y=56, color=COL_TEXT, value=24,
         parent=tab_b)

ui.Label("แจ้งเตือนที่ 60 องศา", x=16, y=16, color=COL_ACCENT, value=24,
         parent=tab_c)

note = ui.Label("แท็บที่เปิดอยู่: " + NAMES[0], x=24, y=344, color=COL_DIM,
                value=20)

for i in range(60):
    if i % 15 == 0:
        pick = (i // 15) % len(NAMES)
        # ใช้ prop ไม่ใช่ .value(n) - สองทางนี้เลือกแท็บได้เหมือนกัน แต่ .value()
        # สั่งแบบมีอนิเมชันเลื่อน ส่วน prop สั่งแบบเปลี่ยนทันที ภาพที่ถ่ายระหว่าง
        # อนิเมชันยังไม่จบ จะเห็นแท็บเก่าคู่กับป้ายใหม่ ซึ่งอ่านเหมือนของพัง
        tv.prop(ui.PROP_ACTIVE_TAB, pick)
        note.text("แท็บที่เปิดอยู่: " + NAMES[pick])

    for ev in ui.poll():
        if ev["handle"] == tv.id() and ev["type"] == "value_changed":
            print("สลับไปแท็บลำดับที่", ev["value"])
    time.sleep_ms(400)

print("ui.Tabview: .add_tab() คืนหน้า, value=ความสูงแถบ, .value(n) เลือกแท็บ")
