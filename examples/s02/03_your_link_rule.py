# 03_your_link_rule.py - ไฟล์นี้รันได้ แต่มันมองไม่เห็นปัญหาแบบที่สอง
#
# ไฟล์นี้เป็นคู่ฝึกของ 02_link_uptime.py ต่างกันที่ไม่ต้องต่อเน็ตจริงและไม่ต้อง
# ไปปิดเราเตอร์ เพราะมันเล่นเทปผลตรวจลิงก์ชุดหนึ่งที่บันทึกไว้แล้ว รันซ้ำกี่รอบก็ได้
# ผลเหมือนเดิม ซึ่งเป็นสิ่งที่การทดลองกับเน็ตจริงให้ไม่ได้ กฎที่ทดสอบซ้ำไม่ได้
# คือกฎที่ยังไม่รู้ว่าถูก
#
# โจทย์จากทีมหน้างาน
#   ลิงก์ที่ "ใช้งานได้จริง" ต้องครบสองอย่างพร้อมกัน คือต่ออยู่ และมีเลข IP ที่ใช้ได้
#   ต่ออยู่แต่ยังไม่ได้เลข IP ก็คือส่งข้อมูลออกไม่ได้ ซึ่งหน้างานถือว่าเสียเหมือนกัน
#
# ดูที่จอ: ตัวเลขซ้ายคือจำนวนครั้งที่กฎของคุณรายงาน ตัวเลขกลางคือจำนวนที่ควรจะเป็น
#          รันครั้งแรกจะได้ 1 กับ 2 คือกฎที่ให้มาเห็นปัญหาแค่แบบเดียวจากสองแบบในเทป
# กับดัก : "0.0.0.0" เป็นสตริงที่ไม่ว่าง Python จึงถือว่าเป็นจริง เขียน if ip: ผ่านหมด
#          ทั้งที่ยังไม่มีที่อยู่ ต้องเทียบกับ "0.0.0.0" ตรง ๆ เท่านั้น

import lcd
import time
import ui

NEED = 3             # ต้องเสียติดกันกี่รอบถึงจะรายงาน
WANT_REPORTS = 2     # เทปชุดนี้มีปัญหาจริงอยู่สองช่วง
STEP_MS = 120        # เล่นเทปช้า ๆ ให้ตามองทัน

OK_IP = "192.168.1.42"
NO_IP = "0.0.0.0"

# จานสีของหลักสูตร - บทบาทละหนึ่งค่า ตาม SPEC §S7.13
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_ACCENT = 0x4A9EFF
COL_OK, COL_BAD = 0x30A46C, 0xE5484D

# เทปผลตรวจ แต่ละแถวคือคำตอบของ wifi.is_connected() กับ wifi.ip() ในรอบนั้น
# บันทึกมาจากบอร์ดจริงระหว่างที่มีคนเดินถือมันออกนอกห้องแล้วเดินกลับมา
TAPE = (
    (True, OK_IP), (True, OK_IP), (True, OK_IP), (True, OK_IP), (True, OK_IP),
    (False, NO_IP),                                   # สะดุดรอบเดียว ไม่ใช่ปัญหา
    (True, OK_IP), (True, OK_IP), (True, OK_IP),
    (False, NO_IP), (False, NO_IP), (False, NO_IP), (False, NO_IP),
    (True, OK_IP), (True, OK_IP), (True, OK_IP),
    (True, NO_IP), (True, NO_IP), (True, NO_IP), (True, NO_IP), (True, NO_IP),
    (True, OK_IP), (True, OK_IP), (True, OK_IP),
)


# ----- เติมส่วนนี้เอง (งานของคุณ) -----
def is_usable(online, ip):
    """คืน True ถ้าลิงก์รอบนี้ใช้ส่งข้อมูลออกได้จริง

    online = คำตอบของ wifi.is_connected() ในรอบนั้น เป็น True หรือ False
    ip     = คำตอบของ wifi.ip() ในรอบนั้น เป็นสตริงเสมอ ไม่เคยเป็น None

    ตอนนี้มันดูแค่ครึ่งเดียวของโจทย์ จอจึงรายงานแค่ 1 ครั้งจากที่ควรได้ 2
    แก้ข้างในให้ครบทั้งสองเงื่อนไข แล้วรันใหม่จนสองตัวเลขบนจอตรงกัน

    ใบ้: อ่านบรรทัด "กับดัก" ที่หัวไฟล์อีกรอบ แล้วถามว่าจะเทียบสตริงยังไงให้ถูก
    """
    return online
# ----- จบส่วนที่ต้องเติม -----


ui.screen()
time.sleep_ms(200)

# ผังจอ: หัวเรื่องหนึ่งบรรทัด การ์ดตัวเลขสามช่อง กราฟหนึ่งช่อง แล้วสองบรรทัดสรุป
# สามช่องในการ์ดวางห่างกัน 96 px เพราะ Seg7 กว้าง 144 และป้ายหัวช่องยาวสุด 11 ตัว
ui.Label("เขียนกฎว่าลิงก์แบบไหนใช้ได้จริง", x=24, y=8, color=COL_TEXT,
         value=24)
ui.Panel(x=24, y=56, w=744, h=128, color=COL_CARD, min=COL_CARD, max=12,
         value=1)

ui.Label("กฎคุณรายงาน", x=40, y=72, color=COL_DIM, value=16)
seg_got = ui.Seg7(text="0", x=40, y=104, w=144, h=64, color=COL_BAD)

# เลขเป้าหมายไม่ใช่สถานะ จึงใช้สีข้อความ ไม่ใช่สีเขียว - เขียวแปลว่า "ยืนยันว่าปกติ"
# ถ้าเลขนี้เขียวค้างไว้ตลอด ตาจะอ่านว่าผ่านแล้วทั้งที่ยังไม่ได้เริ่มตรวจ
ui.Label("ควรรายงาน", x=280, y=72, color=COL_DIM, value=16)
seg_want = ui.Seg7(text=str(WANT_REPORTS), x=280, y=104, w=144, h=64,
                   color=COL_TEXT)

ui.Label("รอบที่", x=520, y=72, color=COL_DIM, value=16)
seg_round = ui.Seg7(text="0", x=520, y=104, w=144, h=64, color=COL_TEXT)

# คำอธิบายเส้นบอกด้วยคำว่า สูง/ต่ำ ไม่ใช่ด้วยชื่อสี จอขาวดำก็ยังอ่านออก (§S7.7.1)
ui.Label("เส้นสูง = ใช้ได้  เส้นต่ำ = ใช้ไม่ได้", x=24, y=192, color=COL_DIM,
         value=16)

# เส้นกราฟค่าเดียวใช้สีเน้นเสมอ เขียวสงวนไว้บอกว่า "ปกติ" อย่างเดียว
chart = ui.Chart(x=24, y=232, w=744, h=72, color=COL_CARD, min=0, max=100)
s_link = chart.add_series(COL_ACCENT)

now_lbl = ui.Label("ยังไม่เริ่มเล่นเทป", x=24, y=312, color=COL_DIM, value=20)
result_lbl = ui.Label("ยังไม่ได้ตรวจ", x=24, y=352, color=COL_DIM, value=20)
ui.poll()

lcd.clear()
lcd.console("<h2>ตรวจกฎว่าลิงก์ใช้ได้จริงไหม</h2>")
lcd.print("<span class=muted>ต้องเสียติดกัน", NEED, "รอบถึงจะรายงาน</span>")

# ตัวนับรอบที่เสียติดกัน กับการรายงานตอนหลักฐานครบพอดี ส่วนนี้เขียนไว้ให้แล้ว
# อ่านมันให้เข้าใจด้วย เพราะท่านี้จะกลับมาอีกหลายครั้งในคาบต่อ ๆ ไป
# ที่ใช้ == ไม่ใช่ >= เพราะเราอยากได้บรรทัดเดียวตอนเริ่มเสีย ไม่ใช่บรรทัดใหม่ทุกรอบ
bad_streak = 0
reports = 0

for i in range(len(TAPE)):
    online, ip = TAPE[i]
    n = i + 1

    usable = is_usable(online, ip)

    if usable:
        bad_streak = 0
    else:
        bad_streak = bad_streak + 1

    chart.set_next(s_link, 100 if usable else 0)
    seg_round.text(str(n))

    if bad_streak == NEED:
        reports = reports + 1
        now_lbl.text("รอบ " + str(n) + " รายงาน - เสียติดกัน " +
                     str(bad_streak))
        now_lbl.color(COL_BAD)
        lcd.print("<span class=error>รอบ", n, "รายงาน | ต่ออยู่", online,
                  "| ip", ip, "</span>")
    else:
        now_lbl.text("รอบ " + str(n) + " เงียบ - เสียติดกัน " + str(bad_streak))
        now_lbl.color(COL_DIM)

    seg_got.text(str(reports))
    seg_got.color(COL_OK if reports == WANT_REPORTS else COL_BAD)

    ui.poll()
    time.sleep_ms(STEP_MS)

if reports == WANT_REPORTS:
    result_lbl.text("ถูกแล้ว - กฎเห็นปัญหาครบทั้งสองแบบ")
    result_lbl.color(COL_OK)
    lcd.print("<span class=ok>ผ่าน - รายงานครบ", reports, "ครั้ง</span>")
else:
    result_lbl.text("รายงาน " + str(reports) + " ครั้ง แต่ควรได้ " +
                 str(WANT_REPORTS) + " ครั้ง")
    result_lbl.color(COL_BAD)
    lcd.print("<span class=warn>รายงาน", reports, "ครั้ง ควรได้",
              WANT_REPORTS, "</span>")

ui.poll()

# ----- ตาคุณ ต่ออีกขั้น -----
# ผ่านแล้วลองเปลี่ยน NEED เป็น 5 แล้วรันใหม่ จำนวนจะลดเหลือ 1 ครั้ง
# ตอบว่าปัญหาช่วงไหนในเทปที่หายไป และการรอหลักฐานนานขึ้นทำให้เราพลาดอะไร
# ใบ้: ดูความยาวของแต่ละช่วงที่เสียในเทป แล้วเทียบกับเลขที่เราไปขอเพิ่ม
