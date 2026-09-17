# 03_slots_and_the_dead_half.py - ครึ่งที่ตอบทันที กับ ครึ่งที่ต้องมีชิป OPTIGA
#
# ไฟล์นี้สอน: โมดูล tesaiot มี 28 ชื่อ (นับจากตารางใน modtesaiot.c) ที่ตอบทันทีมีเก้าตัว
#             ส่วนอีกสิบหกตัวส่งคำสั่งข้ามไปคอร์จอ ซึ่งทั้ง Eva และ Dev Kit ประกอบมาด้วย
#             ENABLE_OPTIGA = 0 ฝั่ง CM55 — ตามซอร์ส (ipc_service.c) ปลายทางตอบกลับว่า
#             "ไม่มี OPTIGA" แล้วฝั่ง Python โยน OSError; ถ้าไม่มีคำตอบเลยจึงจะรอจนหมดเวลา
#             เวลาที่เสียจริงบนบอร์ดยังไม่มีใครวัด — ไฟล์นี้จับเวลาให้ดูกับตา
#             อีกสามตัวที่เหลือ (protected_update http_post device_identity) ไม่อยู่ในสองกลุ่มนี้
#             protected_update ทำงานจริงบน Dev Kit (ENABLE_OPTIGA_CLM=1) แต่บน Eva โยน OSError ทันที
#             จะได้ไม่ต้องไปค้นพบเองตอนนั่งดีบัก
# ดูที่จอ   : ครึ่งบน slots() ตอบทันที ครึ่งล่างบอกว่าแต่ละตัวคืนอะไรและใช้เวลากี่ ms
# กับดัก    : อย่าเผลอเรียกกลุ่มสิบหกตัวในลูปก่อนรู้ว่าบนบอร์ดของคุณมันใช้เวลาเท่าไร
#
# ใช้เวลารันราว 25 วินาที เพราะสองในนั้นคือการรอ timeout จริง ๆ ไม่ได้แกล้งหน่วง

import tesaiot
import lcd
import ui
import time

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_BAD, COL_INFO = 0x30A46C, 0xF5A623, 0xE5484D, 0x4A9EFF

ui.screen()
time.sleep_ms(200)
ui.Label("เก้าตัวที่ตอบ กับ สิบหกตัวที่ต้องมีชิป", x=20, y=12, color=COL_TEXT,
         value=24)

# --- ครึ่งบน: slots() ซึ่งตอบทันทีเพราะไม่ได้ถามชิป ---
ui.Panel(x=20, y=52, w=652, h=120, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("slots() คืนกี่ช่อง", x=40, y=64, color=COL_DIM, value=16)
seg = ui.Seg7(text="0", x=248, y=84, w=152, h=60, color=COL_INFO)
l_fast = ui.Label("-", x=40, y=148, color=COL_OK, value=16)
l_s1 = ui.Label("-", x=408, y=84, color=COL_TEXT, value=16)
l_s2 = ui.Label("-", x=216, y=112, color=COL_TEXT, value=16)
l_s3 = ui.Label("-", x=216, y=136, color=COL_TEXT, value=16)

# --- ครึ่งล่าง: สองตัวจากกลุ่มที่ตายแล้ว พร้อมเวลาที่เสียไป ---
ui.Panel(x=20, y=188, w=652, h=140, color=COL_CARD, min=COL_DIM, max=12, value=1)
l_d1 = ui.Label("license_verify()  รอ...", x=40, y=200, color=COL_DIM, value=20)
l_d2 = ui.Label("device_id()       รอ...", x=40, y=232, color=COL_DIM, value=20)
l_d3 = ui.Label("รวมเวลาที่เสียไป  -", x=40, y=264, color=COL_DIM, value=20)
l_verdict = ui.Label("-", x=40, y=296, color=COL_DIM, value=16)
ui.Label("ชิปมีจริง แต่ต้องเข้าทางโมดูล optiga", x=20, y=340,
         color=COL_DIM, value=16)
ui.poll()

lcd.clear()
lcd.console("<h2>คาบ 11 - ครึ่งที่ใช้ได้ กับ ครึ่งที่ตายแล้ว</h2>")

# --- slots(): ไม่ส่ง IPC ไปไหนเลย มันอ่านตารางชื่อในเฟิร์มแวร์ ---
t0 = time.ticks_ms()
slots = tesaiot.slots()
fast_ms = time.ticks_diff(time.ticks_ms(), t0)

seg.text(str(len(slots)))
l_fast.text("ตอบใน {} ms".format(fast_ms))
names = sorted(slots.keys(), key=lambda k: slots[k])
l_s1.text("{} = {}".format(names[0], slots[names[0]]))
l_s2.text("{} = {}".format(names[1], slots[names[1]]))
l_s3.text("อีก {} ช่อง ดูในคอนโซล".format(len(names) - 2))
ui.poll()

lcd.print("slots() คืน", len(slots), "ช่อง ใน", fast_ms, "ms")
print("slots() คืน", len(slots), "ช่อง ใช้เวลา", fast_ms, "ms")
for n in names:
    print("  {:<12} -> ช่อง {}".format(n, slots[n]))
# ช่อง 4 ถูกกันไว้ให้ Protected Update จึงไม่มีชื่อไหนชี้ไปที่ช่องนั้น
print("สังเกตว่าไม่มีชื่อไหนชี้ไปช่อง 4 - ช่องนั้นถูกกันไว้")
time.sleep_ms(1500)

# --- กลุ่มที่ตายแล้ว ตัวที่ 1: license_verify() คืน bool ไม่โยน error ---
lcd.print("<b>กำลังเรียก license_verify() - จับเวลาอยู่</b>")
l_d1.color(COL_WARN)
l_d1.text("license_verify()  กำลังรอ...")
ui.poll()

t1 = time.ticks_ms()
verified = tesaiot.license_verify()
ms1 = time.ticks_diff(time.ticks_ms(), t1)

l_d1.color(COL_BAD if not verified else COL_OK)
l_d1.text("license_verify()  คืน {} ใน {} ms".format(verified, ms1))
ui.poll()
lcd.print("license_verify() คืน", verified, "ใน", ms1, "ms")
print("license_verify() คืน", verified, "ใช้เวลา", ms1, "ms")

# --- กลุ่มที่ตายแล้ว ตัวที่ 2: device_id() โยน OSError แทนที่จะคืนค่า ---
# ตัวในกลุ่มนี้ไม่ได้คืนค่าล้มเหลวเหมือนกันหมด บางตัวคืน False บางตัวโยน error
# จึงต้องครอบ try/except ไว้ ไม่งั้นโปรแกรมตายตรงนี้
lcd.print("<b>กำลังเรียก device_id() - จับเวลาอยู่</b>")
l_d2.color(COL_WARN)
l_d2.text("device_id()       กำลังรอ...")
ui.poll()

t2 = time.ticks_ms()
try:
    uid = tesaiot.device_id()
    ms2 = time.ticks_diff(time.ticks_ms(), t2)
    l_d2.color(COL_OK)
    l_d2.text("device_id()       ได้ {} ไบต์ ใน {} ms".format(len(uid), ms2))
    print("device_id() คืนมา", len(uid), "ไบต์")
except OSError as e:
    ms2 = time.ticks_diff(time.ticks_ms(), t2)
    l_d2.color(COL_BAD)
    l_d2.text("device_id()       OSError ใน {} ms".format(ms2))
    lcd.print("<span class=err>device_id() โยน OSError ใน", ms2, "ms</span>")
    print("device_id() โยน OSError หลังรอ", ms2, "ms :", e)

total = ms1 + ms2
l_d3.color(COL_BAD)
l_d3.text("รวมเวลาที่เสียไป  {} ms จากสองบรรทัด".format(total))
l_verdict.color(COL_BAD)
l_verdict.text("สิบหกตัวในกลุ่มนี้เป็นแบบนี้ทุกตัว Eva และ Dev Kit")   # 124 B (เพดาน 126)
ui.poll()

lcd.print("<b>รวมสองบรรทัดเสียไป", total, "ms</b>")
print("")
print("รวมสองบรรทัดเสียเวลาไป", total, "ms")
print("บทเรียน: ไม่ใช่ทุกชื่อที่ import มาได้ จะเรียกได้จริงบนบอร์ดที่เราถืออยู่")
print("         กลุ่มที่ตายแล้ว: init device_id license_verify health")
print("                          cred_read cred_write cred_erase")
print("                          random hash hmac aes_keygen encrypt decrypt sign")
print("                          counter_read counter_inc")
print("         อยากคุยกับชิป OPTIGA จริง ๆ ให้ใช้โมดูล optiga ซึ่งคุยด้วย I2C")
print("         จาก CM33 ตรง ๆ ไม่ต้องพึ่งคอร์จอ - ดูสไลด์โบนัสท้ายคาบ")
