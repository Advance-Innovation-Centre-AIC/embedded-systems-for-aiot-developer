# 07_disconnect_rejoin.py - สร้างสถานะ "เน็ตหลุด" ขึ้นมาดูเองตามสั่ง
#
# ก่อนกด Run แก้ 2 บรรทัดนี้ก่อน:
#   WIFI_SSID = ชื่อวงที่ผู้สอนแจก
#   WIFI_PASS = รหัสผ่านของวงนั้น
#
# Why : คาบ 12 ทุกทีมต้องเขียนโค้ดที่ทนเน็ตหลุด แต่ตอนนั่งเขียนกันในห้อง
#        เน็ตมักจะดีอยู่ตลอด เลยไม่มีใครได้เห็นจริง ๆ ว่า "หลุด" หน้าตาเป็นอย่างไร
#        เดาเอาแล้วเขียนเงื่อนไขผิด กว่าจะรู้ก็ตอนสาธิตหน้าห้อง
#        wifi.disconnect() ทำให้เราสั่งให้หลุดเมื่อไรก็ได้ ไม่ต้องรอให้เราเตอร์ดับ
# What: ต่อ -> อ่านค่าไว้ -> disconnect() -> อ่านค่าซ้ำ -> ต่อกลับ
#        แล้วเทียบสามค่าทั้งสองจังหวะให้เห็นกับตาว่าอะไรเปลี่ยนบ้าง
#
# กับดักที่ไฟล์นี้ตั้งใจให้เห็น: wifi.ip() ตอนไม่ได้ต่อ คืนสตริง "0.0.0.0"
#   สตริงนั้นไม่ใช่สตริงว่าง Python จึงถือว่าเป็นจริง เขียน if wifi.ip(): ไป
#   จะได้ True ทั้งที่เน็ตหลุดสนิท นี่คือบั๊กที่หาเจอยากมากเพราะโค้ดดูถูกทุกบรรทัด
#   ทางที่ถูกคือถามด้วย wifi.is_connected() หรือเทียบสตริงตรง ๆ กับ "0.0.0.0"
#
# ดูที่จอ: สี่บรรทัดบนเปลี่ยนค่าพร้อมกันตอนสั่งตัด แล้วกลับมาตอนต่อใหม่
#          บรรทัด "if wifi.ip():" จะยังขึ้น True ตอนหลุด - นั่นคือประเด็นของไฟล์นี้
# กับดัก : disconnect() คืน None ไม่ใช่ True เขียน if wifi.disconnect(): จะไม่มีวันจริง

import wifi
import lcd
import ui
import time

WIFI_SSID = "AIoT-Class"
WIFI_PASS = "<รหัสผ่านของห้องเรียน>"
DOWN_SECONDS = 6
COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD = 0x142240
COL_OK, COL_WARN, COL_BAD = 0x00E676, 0xFFA726, 0xFF5252

lcd.clear()
lcd.console("<h2>คาบ 9 - สั่งให้เน็ตหลุด แล้วดูว่าอะไรเปลี่ยน</h2>")

ui.screen()
time.sleep_ms(200)
ui.Label("สั่งตัดเน็ตเอง แล้วต่อกลับ", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=650, h=150, color=COL_CARD, min=COL_DIM, max=12, value=1)

l_conn = ui.Label("is_connected() = -", x=40, y=66, color=COL_TEXT, value=20)
l_ip = ui.Label("ip() = -", x=40, y=100, color=COL_TEXT, value=20)
l_mode = ui.Label("status()['mode'] = -", x=40, y=134, color=COL_TEXT, value=20)
# บรรทัดนี้คือหัวใจของไฟล์ ปล่อยให้มันโกหกให้เห็นกับตา
l_trap = ui.Label("if wifi.ip(): -> -", x=40, y=168, color=COL_WARN, value=20)

ui.Panel(x=20, y=216, w=650, h=110, color=COL_CARD, min=COL_DIM, max=12, value=1)
l_phase = ui.Label("กำลังเริ่ม", x=40, y=230, color=COL_DIM, value=20)
l_note = ui.Label("-", x=40, y=264, color=COL_DIM, value=16)
l_note2 = ui.Label("-", x=40, y=292, color=COL_DIM, value=16)
ui.Label("สี่บรรทัดบนอ่านจากจังหวะเดียวกันเสมอ", x=20, y=340,
         color=COL_DIM, value=16)
ui.poll()


def refresh(phase):
    # อ่าน status() ครั้งเดียวต่อรอบ ตามกติกาเดียวกับ s09/05
    st = wifi.status()
    connected = st["connected"]
    ip = st["ip"]

    l_conn.text("is_connected() = {}".format(wifi.is_connected()))
    l_ip.text("ip() = {}".format(ip))
    l_mode.text("status()['mode'] = {}".format(st["mode"]))

    # bool(ip) คือสิ่งที่ Python จะตอบถ้าเราเขียน if wifi.ip():
    # ตอนหลุดมันตอบ True เพราะ "0.0.0.0" ไม่ใช่สตริงว่าง
    l_trap.text("if wifi.ip(): -> {}   (ต่ออยู่จริง {})".format(
        bool(ip), connected))
    l_trap.color(COL_BAD if (bool(ip) and not connected) else COL_DIM)

    l_conn.color(COL_OK if connected else COL_BAD)
    l_ip.color(COL_OK if connected else COL_BAD)
    l_phase.text(phase)
    ui.poll()

    lcd.print("<b>{}</b> connected={} ip={} mode={}".format(
        phase, connected, ip, st["mode"]))
    print(phase, "| connected =", connected, "| ip =", ip, "| mode =", st["mode"])
    return st


# --- จังหวะที่ 1: ยังไม่ได้ต่อ ---
refresh("1) ก่อนต่อ")
l_note.text("ยังไม่ได้เรียก connect() เลย")
time.sleep_ms(1200)

# --- จังหวะที่ 2: ต่อ ---
l_phase.color(COL_WARN)
l_phase.text("2) กำลังต่อ " + WIFI_SSID + " จอจะนิ่งครู่หนึ่ง")
ui.poll()
ok = wifi.connect(WIFI_SSID, WIFI_PASS)
print("connect() คืนค่า", ok)

if not ok:
    l_phase.color(COL_BAD)
    l_phase.text("ต่อไม่ติด ตรวจ WIFI_SSID กับ WIFI_PASS ที่หัวไฟล์")
    l_note.text("ไฟล์นี้ต้องต่อติดก่อน จึงจะสาธิตการตัดได้")
    ui.poll()
    lcd.print("<span class=err>ต่อไม่ติด จบการทำงาน</span>")
    raise SystemExit

l_phase.color(COL_OK)
up = refresh("2) ต่อแล้ว")
l_note.text("จำเลข IP ก้อนนี้ไว้ เดี๋ยวเทียบกับตอนหลุด")
ip_when_up = up["ip"]
time.sleep_ms(1500)

# --- จังหวะที่ 3: สั่งตัด ---
# disconnect() ไม่คืนอะไรกลับมา มันคืน None เสมอ
# จึงห้ามเอาไปใส่ใน if แบบ if wifi.disconnect(): เพราะจะไม่มีวันเป็นจริง
result = wifi.disconnect()
print("disconnect() คืนค่า", result, "(None เสมอ)")
lcd.print("สั่ง disconnect() แล้ว - ค่าที่คืนกลับมาคือ {}".format(result))
time.sleep_ms(600)

l_phase.color(COL_BAD)
down = refresh("3) สั่งตัดแล้ว")
l_note.color(COL_BAD)
l_note.text("ip() เปลี่ยนจาก {} เป็น {}".format(ip_when_up, down["ip"]))
l_note2.color(COL_BAD)
l_note2.text("แต่ if wifi.ip(): ยังตอบ True อยู่ - อย่าใช้มันตัดสิน")

for i in range(DOWN_SECONDS):
    l_phase.text("3) หลุดอยู่ เหลืออีก {} วินาที".format(DOWN_SECONDS - i))
    ui.poll()
    time.sleep_ms(1000)

# --- จังหวะที่ 4: ต่อกลับ ---
l_phase.color(COL_WARN)
l_phase.text("4) กำลังต่อกลับ")
ui.poll()
back = wifi.connect(WIFI_SSID, WIFI_PASS)
print("ต่อกลับคืนค่า", back)

l_phase.color(COL_OK if back else COL_BAD)
again = refresh("4) ต่อกลับแล้ว" if back else "4) ต่อกลับไม่สำเร็จ")
l_note.color(COL_DIM)
l_note2.color(COL_DIM)

# เลข IP รอบสองอาจไม่เท่ารอบแรก เพราะ DHCP ให้ยืมมาเป็นครั้ง ๆ ไม่ใช่ของเราถาวร
if again["ip"] == ip_when_up:
    l_note.text("ได้ IP เดิม {} - เราเตอร์จำเราได้".format(again["ip"]))
else:
    l_note.text("IP เปลี่ยน {} -> {} - DHCP ให้ยืมเป็นครั้ง ๆ".format(
        ip_when_up, again["ip"]))
l_note2.text("สรุป: ถามสถานะด้วย is_connected() ไม่ใช่ด้วย if wifi.ip():")
ui.poll()

print("บทเรียนของไฟล์นี้:")
print("  1. disconnect() คืน None ไม่ใช่ True")
print('  2. ip() ตอนหลุดคือ "0.0.0.0" ซึ่ง Python ถือว่าเป็นจริง')
print("  3. คำถาม 'ต่ออยู่ไหม' มีคำตอบเดียวที่เชื่อได้คือ is_connected()")
