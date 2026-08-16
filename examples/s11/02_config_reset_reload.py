# 02_config_reset_reload.py - ล้างค่าตั้ง กับ ย้อนค่าตั้ง เป็นคนละเรื่องกัน
#
# ไฟล์นี้สอน: สองฟังก์ชันที่ชื่อคล้ายกันจนสลับกันได้ง่าย แต่ผลต่างกันมาก
#             config_reset()  = ล้างของจริงกลับเป็นค่าโรงงานทั้ง 19 คีย์
#             config_reload() = อ่านไฟล์ตั้งค่าจากแฟลชขึ้นมาทับของที่แก้ค้างไว้
#             ตัวแรกทำลาย ตัวหลังย้อนกลับ - เลือกผิดตัวคือเสียตัวตนของทีมไปทั้งชุด
# ดูที่จอ   : สามจังหวะเรียงลงมา ค่า device_id เปลี่ยนให้เห็นทีละขั้น
# กับดัก    : config_reset() คืน None ไม่ใช่ True เขียน if tesaiot.config_reset():
#             จะไม่มีวันเป็นจริง ส่วน config_reload() คืน bool จริง ๆ - คนละชนิดกัน
#
# หมายเหตุก่อนรัน: ไฟล์นี้ล้างค่าตั้งของบอร์ดจริง จบไฟล์แล้วมันตั้งค่ากลับให้
#   แต่ถ้ากด Ctrl-C ค้างกลางทาง ต้องไปตั้ง device_id / api_key / mqtt_pass ใหม่เอง

import tesaiot
import lcd
import ui
import time

# ตัวตนของทีม - แก้สามค่านี้เป็นของทีมตัวเองก่อนรัน
DEVICE_ID = "eva-team03"
API_KEY = "<api key ของทีม>"
BROKER = "mqtt.tesaiot.dev"

COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD = 0x00E676, 0xFFA726, 0xFF5252

ui.screen()
time.sleep_ms(200)
ui.Label("ล้างค่าตั้ง กับ ย้อนค่าตั้ง", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=650, h=150, color=COL_CARD, min=COL_DIM, max=12, value=1)
l_step = ui.Label("จังหวะที่ 1 - อ่านของเดิม", x=40, y=66, color=COL_WARN,
                  value=20)
l_a = ui.Label("1) ก่อนแตะอะไร      -", x=40, y=100, color=COL_DIM, value=18)
l_b = ui.Label("2) หลัง reset()     -", x=40, y=132, color=COL_DIM, value=18)
l_c = ui.Label("3) หลังตั้งใหม่     -", x=40, y=164, color=COL_DIM, value=18)

ui.Panel(x=20, y=216, w=650, h=110, color=COL_CARD, min=COL_DIM, max=12, value=1)
l_n1 = ui.Label("-", x=40, y=230, color=COL_DIM, value=18)
l_n2 = ui.Label("-", x=40, y=262, color=COL_DIM, value=16)
l_n3 = ui.Label("-", x=40, y=290, color=COL_DIM, value=16)
ui.Label("reset ล้างของจริง - reload แค่ย้อนกลับ", x=20, y=340, color=COL_DIM,
         value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 11 - reset กับ reload</h2>")


def id_now():
    # อ่าน device_id ปัจจุบัน ถ้าเป็นสตริงว่างต้องอ่านออกว่า "ว่าง"
    # ไม่ใช่เห็นช่องว่างแล้วนึกว่าจอค้าง
    v = tesaiot.config()["device_id"]
    return v if v != "" else "(ว่าง)"


# --- จังหวะที่ 1: ของเดิมเป็นอย่างไร ---
before = id_now()
l_a.text("1) ก่อนแตะอะไร      {}".format(before))
l_a.color(COL_TEXT)
ui.poll()
lcd.print("ก่อนแตะอะไร device_id =", before)
print("ก่อนแตะอะไร device_id =", before)
time.sleep_ms(1200)

# --- จังหวะที่ 2: ล้างทิ้ง ---
# config_reset() คืน None ไม่ใช่ True จึงรับค่ามาพิมพ์ให้ดูตรง ๆ ว่าเป็นอะไร
l_step.text("จังหวะที่ 2 - config_reset() ล้างทั้ง 19 คีย์")
l_step.color(COL_BAD)
ui.poll()
ret = tesaiot.config_reset()
after_reset = id_now()
l_b.text("2) หลัง reset()     {}".format(after_reset))
l_b.color(COL_BAD)
l_n1.color(COL_BAD)
l_n1.text("config_reset() คืน {} (ไม่ใช่ True)".format(ret))
ui.poll()
lcd.print("<span class=err>reset แล้ว device_id =", after_reset, "</span>")
print("config_reset() คืนค่า", ret, "| device_id =", after_reset)

# ล้างแล้วคีย์อื่นก็หายด้วย ไม่ใช่แค่ device_id - นับให้ดูว่าเหลือค่าว่างกี่คีย์
cfg = tesaiot.config()
empty = 0
for k in cfg:
    if cfg[k] == "":
        empty += 1
l_n2.color(COL_BAD)
l_n2.text("จาก {} คีย์ ตอนนี้ว่างไปแล้ว {} คีย์".format(len(cfg), empty))
print("จาก", len(cfg), "คีย์ ว่างไป", empty, "คีย์")
time.sleep_ms(1500)

# --- จังหวะที่ 3: ตั้งกลับเข้าไปใหม่ ---
# ล้างแล้วต้องตั้งใหม่ "ทุกค่า" ไม่ใช่แค่ค่าที่เราเผลอลบ
l_step.text("จังหวะที่ 3 - ตั้งค่ากลับเข้าไปเอง")
l_step.color(COL_WARN)
ui.poll()

# config_set() รับสตริงทั้งสองช่อง และคืน True/False บอกว่าคีย์นั้นมีจริงไหม
ok_id = tesaiot.config_set("device_id", DEVICE_ID)
ok_key = tesaiot.config_set("api_key", API_KEY)
ok_brk = tesaiot.config_set("broker", BROKER)
ok_sni = tesaiot.config_set("sni_hostname", BROKER)
print("config_set คืนค่า:", ok_id, ok_key, ok_brk, ok_sni)

# คีย์ที่สะกดผิดคืน False เงียบ ๆ ไม่โยน error - ต้องรับค่ากลับมาดูเสมอ
typo = tesaiot.config_set("devise_id", "ผิดแน่ ๆ")
l_n3.text("คีย์ที่สะกดผิดคืน {} ไม่โยน error".format(typo))
print("ตั้งคีย์ที่สะกดผิด คืนค่า", typo)

after_set = id_now()
l_c.text("3) หลังตั้งใหม่     {}".format(after_set))
l_c.color(COL_OK)
ui.poll()
lcd.print("<span class=ok>ตั้งกลับแล้ว device_id =", after_set, "</span>")

# --- จังหวะที่ 4: reload ทำอะไร ---
# reload อ่านไฟล์จากแฟลชขึ้นมาทับค่าในหน่วยความจำ
# ค่าที่เพิ่งตั้งแต่ยังไม่ถูกเซฟลงไฟล์ จะถูกทับหายไปตรงนี้
l_step.text("จังหวะที่ 4 - config_reload() ดึงไฟล์จากแฟลชมาทับ")
l_step.color(COL_WARN)
ui.poll()
time.sleep_ms(800)

reloaded = tesaiot.config_reload()
after_reload = id_now()
l_n1.color(COL_DIM)
l_n1.text("config_reload() คืน {} (เป็น bool จริง)".format(reloaded))
l_n2.color(COL_DIM)
l_n2.text("หลัง reload device_id = {}".format(after_reload))
ui.poll()
lcd.print("reload คืนค่า", reloaded, "-> device_id =", after_reload)
print("config_reload() คืนค่า", reloaded, "| device_id =", after_reload)

if after_reload != after_set:
    # ค่าที่เราเพิ่งตั้งหายไป เพราะไฟล์ในแฟลชยังเป็นของเดิม
    l_step.color(COL_BAD)
    l_step.text("reload ทับค่าที่เพิ่งตั้ง - ต้องตั้งใหม่อีกรอบ")
    tesaiot.config_set("device_id", DEVICE_ID)
    tesaiot.config_set("api_key", API_KEY)
    tesaiot.config_set("broker", BROKER)
    tesaiot.config_set("sni_hostname", BROKER)
    l_n3.text("ตั้งกลับให้แล้ว ตอนนี้ device_id = {}".format(id_now()))
else:
    l_step.color(COL_OK)
    l_step.text("reload ได้ค่าเดิม - ไฟล์ในแฟลชตรงกับที่ตั้งไว้")

ui.poll()
print("")
print("สรุปความต่างสองตัวนี้:")
print("  config_reset()  ล้างของจริง คืน None  ใช้ตอนตั้งค่ามั่วจนไม่รู้เหลืออะไร")
print("  config_reload() ย้อนกลับ    คืน bool  ใช้ตอนอยากทิ้งการแก้ที่ยังไม่พอใจ")
