# s09_network_status.py - จอสถานะเครือข่ายของทีม (ฉบับฝึกเติมโค้ด)
# วิธีรัน: 1) บนจอบอร์ด แตะการ์ด BENTO Playground ค้างหน้านี้ไว้
#          2) แก้ WIFI_SSID / WIFI_PASS ให้ตรงกับเครือข่ายที่ผู้สอนแจก
#          3) เติมช่องว่างทั้ง 6 จุดตามคำใบ้ แล้วกด "Program to Device"
# สแกนคลื่นรอบตัว เรียงแรงไปอ่อน ลงตารางสี่คอลัมน์ ต่อเครือข่ายของทีม แล้วเปิด
# หน้าสถานะที่วัด ping สองปลายทางสดทุก 3 วินาที พร้อมปุ่มสั่งสแกนใหม่
# ระวัง: wifi.connect() บล็อกได้นานถึง ~85 วินาทีถ้ารหัสผ่านผิด ให้รอจนขึ้นผล
#
# ไฟล์นี้เดินสี่เรื่องต่อกัน สแกน - เรียง - ต่อ - วัด ตัวอย่างของคาบนี้แยกไว้เรื่องละไฟล์
# ที่ examples/s09/ ไล่ตามเลข 01 ถึง 06 ก็จะได้เส้นทางเดียวกับไฟล์นี้พอดี
# ติดตรงไหนให้เปิดเฉพาะไฟล์ของท่านั้น ไม่ต้องอ่านรวด
#
# หน้าจอถูกวางไว้ให้ครบแล้ว ไม่ต้องแก้ - งานของเราคือทำให้ข้อมูลจริงไหลเข้าไปในนั้น

import wifi
import ui
import time

# ---------- ค่าของทีม (แก้ห้าบรรทัดนี้ก่อนรัน) ----------
WIFI_SSID = "AIoT-Class"     # ชื่อเครือข่ายที่ผู้สอนแจกให้ห้องนี้
WIFI_PASS = "changeme"       # รหัสผ่านของเครือข่ายนั้น
NET_TEST_IP = "8.8.8.8"      # ปลายทางฝั่งอินเทอร์เน็ต (Google Public DNS)
PING_TIMEOUT_MS = 1500       # รอคำตอบ ping นานสุดกี่ ms ต่อครั้ง
TOP_N = 4                    # ตารางสูงพอดีกับหัวตารางบวกสี่แถว แถวละ 70 พิกเซล

PING_EVERY_MS = 3000         # วัด ping ทุกกี่ ms
RSSI_FLOOR, RSSI_CEIL = -90, -40   # พิสัยของมาตรวัด: แทบไม่เหลือ ถึง เต็มแท่ง

# ---------- สีการ์ด (ชุดเดียวกับแดชบอร์ดคาบ 8) ----------
COL_TEXT, COL_DIM = 0xFFFFFF, 0xA0B4CC
COL_CARD, COL_OK, COL_WARN, COL_BAD, COL_RUN = (0x142240, 0x00E676, 0xFFC83D,
                                                0xFF5252, 0x4FC3F7)


def signal_of(rssi):
    # RSSI เป็น dBm ติดลบ: -90 dBm = แทบไม่เหลือ, -40 dBm = เต็มแท่ง
    # สีของแท่งใช้ฟ้าตอนปกติ ไม่ใช่เขียว - สถานะปกติต้องเงียบ สีจัดสงวนไว้ให้เรื่องผิดปกติ
    pct = (rssi - RSSI_FLOOR) * 100 // (RSSI_CEIL - RSSI_FLOOR)
    pct = 0 if pct < 0 else (100 if pct > 100 else pct)
    col = COL_RUN if rssi >= -60 else (COL_WARN if rssi >= -75 else COL_BAD)
    return pct, col


def ms_text(name, ms):
    # ping คืน -1 เมื่อไม่มีคำตอบ อย่าโชว์ -1 ตรง ๆ ให้แปลเป็นคำที่คนอ่านเข้าใจ
    return name + (" ไม่ตอบ" if ms < 0 else " " + str(ms) + " ms")


# --- ท่าที่ 1: วางหน้าจอสองแผง (ซ้าย = ตารางคลื่นรอบตัว, ขวา = สถานะลิงก์) ---
ui.screen()
time.sleep_ms(200)
ui.Label("สถานะเครือข่ายของทีม", x=16, y=8, color=COL_TEXT, value=20)
# ปุ่มวางบนแถบหัวเรื่อง ไม่ใช่มุมขวาล่าง เพราะมุมนั้นเฟิร์มแวร์ถือไว้ให้ปุ่ม Console
btn_scan = ui.Button("สแกนใหม่", x=596, y=2, w=184, h=40, color=0x37474F, value=18)

# ผลการสแกนคือตารางตั้งแต่ต้น จึงใช้ ui.Table ไม่ใช่ ui.Label เรียงกันเอง
# แถวหนึ่งสูงราว 70 พิกเซล และสูงเป็นสองเท่าทันทีที่ข้อความในช่องยาวเกินคอลัมน์
tbl = ui.Table(x=8, y=44, w=552, h=350, cols=4)
tbl.col_width(0, 175)        # กว้างพอสำหรับชื่อ 12 ตัวอักษร ซึ่งเป็นเพดานที่เราตัดไว้
tbl.col_width(1, 120)
tbl.col_width(2, 80)
tbl.col_width(3, 170)        # "ความปลอดภัย" คือข้อความที่ยาวที่สุดในคอลัมน์นี้

ui.Panel(x=566, y=44, w=218, h=350, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("สถานะลิงก์", x=578, y=48, color=COL_DIM, value=16)
# ไฟสองดวงติดทีละดวงเสมอ ดวงที่ดับจะ "หรี่" ไม่ใช่ "หาย" คนดูจึงยังเห็นว่ามีดวงนั้นอยู่
led_up = ui.Led(x=578, y=74, w=26, h=26, color=COL_OK, value=0)
ui.Label("ต่ออยู่", x=612, y=76, color=COL_DIM, value=16)
led_down = ui.Led(x=578, y=106, w=26, h=26, color=COL_BAD, value=1)
ui.Label("ยังไม่ต่อ", x=612, y=108, color=COL_DIM, value=16)

l_ssid = ui.Label("SSID: -", x=578, y=140, color=COL_TEXT, value=14)
l_ip = ui.Label("IP: -", x=578, y=162, color=COL_TEXT, value=14)
l_gw = ui.Label("เกตเวย์ -", x=578, y=184, color=COL_DIM, value=14)
l_net = ui.Label("อินเทอร์เน็ต -", x=578, y=206, color=COL_DIM, value=14)

# ui.Scale คือไม้บรรทัด ไม่มีเข็มและไม่รับ .value() ตัวที่ขยับคือ ui.Bar ที่วางทับ
ui.Label("ความแรงจากการสแกนล่าสุด", x=578, y=232, color=COL_DIM, value=14)
l_rssi = ui.Label("- dBm", x=578, y=254, color=COL_TEXT, value=18)
bar_rssi = ui.Bar(x=578, y=282, w=170, h=12, color=COL_RUN,
                  min=RSSI_FLOOR, max=RSSI_CEIL, value=RSSI_FLOOR)
sc_rssi = ui.Scale(x=578, y=296, w=170, h=44, color=COL_TEXT,
                   min=RSSI_FLOOR, max=RSSI_CEIL)
sc_rssi.ticks(11, 5)         # ไม้บรรทัดสั้น ๆ ที่มีตัวเลขหกตัวจะทับกันจนอ่านไม่ออก
l_tick = ui.Label("กำลังสแกน", x=578, y=350, color=COL_DIM, value=14)
ui.poll()


# --- ท่าที่ 2 + 3: สแกน เรียงจากแรงไปอ่อน แล้วเทลงตาราง ---
# อยู่ในฟังก์ชันเดียวกัน เพราะปุ่ม "สแกนใหม่" ต้องเรียกซ้ำได้ทั้งชุด ไม่ใช่ครึ่งเดียว
def rescan():
    # ตั้งไว้ว่างก่อน เพื่อให้กด Run ดูโครงหน้าจอได้ตั้งแต่ยังไม่ได้เติมอะไร
    # พอเติมบรรทัดถัดไปเสร็จ บรรทัดนี้จะถูก wifi.scan() เขียนทับทันที
    nets = []

    # scan() บล็อกราว 3-10 วินาที (ย่าน 5 GHz นานกว่า เพราะช่องสัญญาณเยอะกว่ามาก)
    # เติม: nets = wifi.scan()
    # ไม่รู้ว่าของที่ได้กลับมาหน้าตาอย่างไร: examples/s09/01_scan_tuples.py พิมพ์ผลจริงออกมาดู
    #   ทีละแถว และย้ำว่ามันเป็น tuple สี่ช่อง ไม่ใช่ dict เขียน net["ssid"] เมื่อไรได้ TypeError ทันที
    pass

    # RSSI ติดลบ ค่าที่มากกว่าคือแรงกว่า จึงเรียงมากไปน้อยด้วย net[1] ซึ่งเป็นช่องที่สอง
    # ของ tuple ไม่ใช่ net['rssi'] เพราะ scan() ไม่ได้คืน dict
    # เติม: nets.sort(key=lambda net: net[1], reverse=True)
    # ลืม reverse=True แล้วโค้ดจะยัง "ดูเหมือนทำงาน" ทุกอย่าง แค่วงที่อ่อนที่สุดขึ้นก่อน
    #   examples/s09/02_rank_by_rssi.py เจอเรื่องนี้ตรง ๆ และเสนอวิธีจับผิดด้วยตา คือดูว่าเลข
    #   ไต่ขึ้นหรือไต่ลง ไฟล์นั้นยังแปลง dBm เป็นแท่งกับคำตัดสิน แบบเดียวกับ signal_of() ข้างบนนี้
    pass

    # ล้างของเก่าก่อนเสมอ ไม่งั้นแถวของการสแกนรอบก่อนจะค้างอยู่ใต้แถวใหม่
    tbl.clear_items()
    tbl.add_row("SSID", "RSSI", "ช่อง", "ความปลอดภัย")

    # ใช้ min() กันกรณีสแกนเจอน้อยกว่า TOP_N วง ไม่งั้นจะหลุด IndexError
    for i in range(min(TOP_N, len(nets))):
        # หนึ่งแถวของ scan() = (ssid, rssi, security, channel) แกะสี่ตัวพร้อมกันได้เลย
        # เติม: ssid, rssi, security, channel = nets[i]
        # ลำดับสี่ช่องนี้ท่องไม่ได้ ต้องดูของจริง: examples/s09/01_scan_tuples.py วางทั้งสี่ช่อง
        #   ขึ้นจอพร้อมชื่อกำกับ ถ้าแกะแล้วค่าไปคนละที่ ให้กลับไปเทียบกับหน้าจอของไฟล์นั้น
        pass

        # ตัดชื่อที่ 12 ตัวอักษรโดยตั้งใจ ชื่อที่ยาวกว่าคอลัมน์จะถูกตัดบรรทัด แล้วแถวนั้น
        # สูงเป็นสองเท่า ดันแถวสุดท้ายตกขอบจอไปเงียบ ๆ - ชื่อเต็มยังอยู่ที่ Console
        tbl.add_row(ssid[:12], str(rssi) + " dBm", str(channel),
                    "เปิด" if security == 0 else "มีรหัส")
        ui.poll()

    # มาตรวัดฝั่งขวาเล่าเรื่องวงของทีมเอง ไม่ใช่วงที่แรงที่สุด ถ้าสแกนไม่เจอให้บอกตรง ๆ
    for ssid, rssi, security, channel in nets:
        if ssid == WIFI_SSID:
            pct, col = signal_of(rssi)
            # ไม่เรียก bar_rssi.color() เพราะ .color() ของ Bar ไปลงที่ "ราง"
            # ไม่ใช่ "แถบที่เต็ม" - รางที่เปลี่ยนสีทำให้ดูเหมือนแถบเต็มทั้งที่ค่ายังน้อย
            # จึงเอาสีไปไว้ที่ตัวเลขแทน ซึ่ง Label เปลี่ยนสีตัวอักษรได้ตรงตามที่สั่ง
            bar_rssi.value(rssi)
            l_rssi.color(col)
            l_rssi.text(str(rssi) + " dBm (" + str(pct) + "%)")
            return nets
    bar_rssi.value(RSSI_FLOOR)
    l_rssi.text("ไม่เจอวงของทีม")
    return nets


nets = rescan()

# --- ท่าที่ 4: ต่อเข้าเครือข่ายของทีม ---
l_tick.text("กำลังต่อ รอ 85 วิ")
ui.poll()
# ตั้ง False ไว้ก่อน เพื่อให้กด Run ดูโครงหน้าจอได้ตั้งแต่ยังไม่ได้เติมอะไร
# ตอนนี้จะได้ข้อความ "ต่อไม่ติด" ซึ่งเป็นหน้าตาเดียวกับตอนใส่รหัสผ่านผิดพอดี
ok = False

# connect() รับสองอาร์กิวเมนต์ตามลำดับเท่านั้น และบล็อกจนกว่าจะรู้ผล
# เติม: ok = wifi.connect(WIFI_SSID, WIFI_PASS)
# สังเกตว่าบรรทัด l_tick.text("กำลังต่อ...") อยู่ "ก่อน" บรรทัดนี้ ไม่ใช่หลัง
# examples/s09/03_connect_says_first.py สร้างมาเพื่อเรื่องนี้เรื่องเดียว เพราะระหว่างที่ connect()
# บล็อกอยู่ จอจะไม่อัปเดตอะไรเลย ป้ายที่เขียนทีหลังจึงไม่มีวันได้ขึ้น
# ไฟล์นั้นยังเตือนอีกข้อ: wifi.ip() คืนสตริงเสมอ ตอนยังไม่ต่อคืน "0.0.0.0" ซึ่ง if มองว่าจริง
pass

if not ok:
    l_ssid.color(COL_BAD)
    l_ssid.text("ต่อไม่ติด ตรวจ SSID และรหัสผ่าน")
    l_tick.text("จบแล้ว")
    ui.poll()
    raise SystemExit

ip = wifi.ip()
# เกตเวย์ของวงแลนห้องเรียนเกือบทั้งหมดคือเลข .1 ของวงเดียวกัน ถ้าห้องนี้ไม่ใช่ ให้แก้เอง
gw = ".".join(ip.split(".")[:3]) + ".1"
led_up.value(1)
led_down.value(0)
l_ssid.text("SSID: " + WIFI_SSID)
l_ip.text("IP: " + ip)
print("connected, ip =", ip, "gateway =", gw)

# --- ท่าที่ 5: ลูปสถานะสด วัด ping สองปลายทาง และรับคำสั่งจากปุ่ม ---
# ลูปเดินทุก 200 ms เพื่อรับนิ้ว แต่ ping เดินตามนาฬิกาของตัวเองทุก 3 วินาที
t_ping = time.ticks_ms() - PING_EVERY_MS   # ยิงรอบแรกทันที ไม่ต้องรอสามวินาที
last_sec = -1
ms_gw, ms_net = -1, -1

while True:
    now = time.ticks_ms()
    events = []
    # ถ้าลืมบรรทัดนี้ เฟิร์มแวร์จะซ่อน widget ทิ้งภายในราวสองวินาที และปุ่มจะไม่ตอบ
    # เติม: events = ui.poll()
    pass

    for ev in events:
        if ev["type"] == "clicked" and ev["handle"] == btn_scan.id():
            # สแกนใหม่บล็อกยาว บอกก่อนแล้วค่อยเรียก เหมือนท่าที่ 4 ทุกประการ
            l_tick.text("กำลังสแกน")
            ui.poll()
            nets = rescan()
            t_ping = time.ticks_ms() - PING_EVERY_MS
            last_sec = -1

    # ลูปนี้ถามสถานะทีละคำถาม (is_connected แล้วค่อย ping) ซึ่งพอสำหรับจอนี้
    # ถ้าทีมจะเพิ่มบรรทัดที่ถามอย่างอื่นเข้ามาอีก ให้อ่าน examples/s09/05_status_dict.py ก่อน
    # มันชี้ว่า wifi.status() คืนทุกค่าจากจังหวะเวลาเดียวกันในการอ่านครั้งเดียว
    # ส่วนการถามทีละคำถามได้คำตอบจากคนละจังหวะ แล้วเราจะได้ภาพผสมที่ไม่เคยเกิดขึ้นจริง
    if wifi.is_connected():
        led_up.value(1)
        led_down.value(0)
        if time.ticks_diff(now, t_ping) >= PING_EVERY_MS:
            t_ping = now
            try:
                # ping รับเฉพาะ IP เท่านั้น ใส่ชื่อโฮสต์จะได้ ValueError
                # เติม: ms_gw = wifi.ping(gw, PING_TIMEOUT_MS)
                # ทำไมต้องยิงสองปลายทาง ไม่ใช่ปลายทางเดียว: examples/s09/04_ping_two_targets.py
                #   ตอบไว้ครบ เกตเวย์ตอบแต่อินเทอร์เน็ตไม่ตอบ แปลว่าปัญหาอยู่หลังเราเตอร์
                #   และมันเตือนว่า ping คืน -1 เมื่อไม่มีคำตอบ ห้ามเอา -1 ไปเฉลี่ยรวมกับเวลาจริง
                pass

                ms_net = wifi.ping(NET_TEST_IP, PING_TIMEOUT_MS)
            except OSError:
                ms_gw, ms_net = -1, -1
            # ตอบได้คือขาวเงียบ ๆ ตอบไม่ได้คือแดง - สีจัดสงวนไว้ให้เรื่องที่ผิดปกติ
            l_gw.color(COL_TEXT if ms_gw >= 0 else COL_BAD)
            l_net.color(COL_TEXT if ms_net >= 0 else COL_BAD)
            l_gw.text(ms_text("เกตเวย์", ms_gw))
            l_net.text(ms_text("อินเทอร์เน็ต", ms_net))
    else:
        led_up.value(0)
        led_down.value(1)
        # ค่าที่ค้างต้องบอกว่าตัวเองค้าง ไม่ใช่ปล่อยเลขเดิมไว้ให้คนเดินมาอ่านว่ายังสด
        l_gw.color(COL_BAD)
        l_net.color(COL_BAD)
        l_gw.text("ลิงก์หลุด")
        l_net.text("เลขล่าสุดไม่ใช่ค่าปัจจุบัน")

    # ตัวเลขที่คนต้องอ่าน เขียนใหม่ไม่เกินวินาทีละครั้ง และอยู่ตำแหน่งเดิมเสมอ
    left = (PING_EVERY_MS - time.ticks_diff(now, t_ping)) // 1000
    if left != last_sec:
        last_sec = left
        l_tick.text("วัดใหม่ใน " + str(left) + " วิ")

    time.sleep_ms(200)
