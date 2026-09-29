# s09_network_status.py - จอสถานะเครือข่ายของทีม (ตาราง + ไฟสถานะ + มาตรวัด dBm)
# วิธีรัน: 1) บนจอบอร์ด แตะการ์ด BENTO Playground ค้างหน้านี้ไว้
#          2) แก้ WIFI_SSID / WIFI_PASS ให้ตรงกับเครือข่ายที่ผู้สอนแจก
#          3) เปิดไฟล์นี้ใน BENTO IDE กด "Program to Device" แล้วมองจอบอร์ด
#
# บอร์ดสแกนคลื่นรอบตัว เรียงจากแรงไปอ่อน ลงตารางสี่คอลัมน์ แล้วต่อเครือข่ายของทีม
# เปิดหน้าสถานะที่วัด ping สองปลายทางสดทุก 3 วินาที และมีปุ่มให้สั่งสแกนใหม่ได้เอง
# ระวัง: wifi.connect() บล็อกได้นานถึง ~85 วินาทีถ้ารหัสผ่านผิด ให้รอจนขึ้นผล
#
# ดูที่จอ: แถบบนคือหัวเรื่อง SSID เลข IP ป้ายสถานะ และปุ่มสแกนใหม่
#         ล่างซ้ายคือตารางวงที่สแกนเจอ เรียงแรงไปอ่อน สี่คอลัมน์จัดให้เองไม่ต้องนับพิกเซล
#         ล่างขวาคือไฟสถานะลิงก์สองดวง เลข ping สองปลายทาง และมาตรวัดความแรงพร้อมพิสัย
# กับดัก : ผลสแกนคือ "ตาราง" ตั้งแต่ต้น การเรียง ui.Label เองแล้วนับพิกเซลให้ตรงคอลัมน์
#         เป็นงานที่ ui.Table ทำให้ฟรี และทำได้ถูกกว่าเราตอนข้อความยาวไม่เท่ากัน
#         ส่วนสถานะลิงก์ต้องเป็นไฟ ไม่ใช่ตัวอักษรสี - ภาพขาวดำต้องยังแยกออก

import wifi
import ui
import time

# ---------- ค่าของทีม (แก้ห้าบรรทัดนี้ก่อนรัน) ----------
WIFI_SSID = "AIoT-Class"     # ชื่อเครือข่ายที่ผู้สอนแจกให้ห้องนี้
WIFI_PASS = "changeme"       # รหัสผ่านของเครือข่ายนั้น
NET_TEST_IP = "8.8.8.8"      # ปลายทางฝั่งอินเทอร์เน็ต (Google Public DNS)
PING_TIMEOUT_MS = 1500       # รอคำตอบ ping นานสุดกี่ ms ต่อครั้ง
TOP_N = 3                    # ตารางสูง 288 = หัวตารางบวกสามแถว แถวละ 72 พิกเซล

PING_EVERY_MS = 3000         # วัด ping ทุกกี่ ms
RSSI_FLOOR, RSSI_CEIL = -90, -40   # พิสัยของมาตรวัด: แทบไม่เหลือ ถึง เต็มแท่ง

# ---------- สีการ์ด (ชุดเดียวกับแดชบอร์ดคาบ 8) ----------
COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD, COL_OK, COL_WARN, COL_BAD, COL_RUN = (0x171B22, 0x30A46C, 0xF5A623,
                                                0xE5484D, 0x4A9EFF)

class Stop(Exception):
    # จบโปรแกรมแบบปกติ (SystemExit ทำให้บอร์ดเริ่มระบบใหม่ และอาจค้างจนต้องถอดสาย)
    pass


def signal_of(rssi):
    # RSSI เป็น dBm ติดลบ: -90 dBm = แทบไม่เหลือ, -40 dBm = เต็มแท่ง
    pct = (rssi - RSSI_FLOOR) * 100 // (RSSI_CEIL - RSSI_FLOOR)
    pct = 0 if pct < 0 else (100 if pct > 100 else pct)
    col = COL_RUN if rssi >= -60 else (COL_WARN if rssi >= -75 else COL_BAD)
    return pct, col


def gateway_of(ip):
    # เกตเวย์ของวงแลนบ้านและห้องเรียนเกือบทั้งหมดคือเลข .1 ของวงเดียวกัน
    p = ip.split(".")
    return p[0] + "." + p[1] + "." + p[2] + ".1"


def ms_text(name, ms):
    # ping คืน -1 เมื่อไม่มีคำตอบ อย่าโชว์ -1 ตรง ๆ ให้แปลเป็นคำที่คนอ่านเข้าใจ
    return name + (" ไม่ตอบ" if ms < 0 else " " + str(ms) + " ms")


# --- ท่าที่ 1: วางหน้าจอสองแผง (ซ้าย = ตารางคลื่นรอบตัว, ขวา = สถานะลิงก์) ---
ui.screen()
time.sleep_ms(200)
ui.Label("สถานะเครือข่ายของทีม", x=24, y=8, color=COL_TEXT, value=24)
l_ssid = ui.Label("SSID: -", x=336, y=12, color=COL_TEXT, value=20)
l_ip = ui.Label("IP: -", x=24, y=48, color=COL_TEXT, value=20)
l_tick = ui.Label("กำลังสแกน", x=336, y=48, color=COL_DIM, value=20)

btn_scan = ui.Button("สแกนใหม่", x=552, y=8, w=216, h=88, color=0x3A4150, value=20)

# ผลการสแกนคือตารางตั้งแต่ต้น จึงใช้ ui.Table ไม่ใช่ ui.Label เรียงกันเอง
tbl = ui.Table(x=24, y=104, w=480, h=288, cols=4)
tbl.col_width(0, 144)        # กว้างพอสำหรับชื่อ 12 ตัวอักษร ซึ่งเป็นเพดานที่เราตัดไว้
tbl.col_width(1, 96)
tbl.col_width(2, 80)
tbl.col_width(3, 128)        # "มีรหัส" คือข้อความที่ยาวที่สุดในคอลัมน์นี้        # "ความปลอดภัย" คือข้อความที่ยาวที่สุดในคอลัมน์นี้

# การ์ดต้องถูกสร้าง "ก่อน" ของที่วางบนมันเสมอ LVGL วาดตามลำดับการสร้าง
ui.Panel(x=520, y=104, w=248, h=288, color=COL_CARD, min=COL_DIM, max=12, value=1)
# ไฟสองดวงติดทีละดวงเสมอ ดวงที่ดับจะ "หรี่" ไม่ใช่ "หาย" - ไฟที่หายไปตอนดับ
# แย่กว่าไฟที่หรี่ลง เพราะคนดูแยกไม่ออกว่าดับหรือจอเสีย
led_up = ui.Led(x=536, y=112, w=48, h=48, color=COL_OK, value=0)
ui.Label("ต่ออยู่", x=592, y=124, color=COL_DIM, value=16)
led_down = ui.Led(x=536, y=168, w=48, h=48, color=COL_BAD, value=1)
ui.Label("ยังไม่ต่อ", x=592, y=180, color=COL_DIM, value=16)

l_gw = ui.Label("เกตเวย์ -", x=536, y=224, color=COL_DIM, value=20)
l_net = ui.Label("อินเทอร์เน็ต -", x=536, y=256, color=COL_DIM, value=20)

# มาตรวัด: ตัวเลข dBm ลอย ๆ ไม่บอกว่าแรงไหม ต้องมีพิสัยอยู่ข้าง ๆ เสมอ
l_rssi = ui.Label("ความแรง - dBm", x=536, y=288, color=COL_TEXT, value=20)
bar_rssi = ui.Bar(x=536, y=320, w=152, h=12, color=COL_RUN,
                  min=RSSI_FLOOR, max=RSSI_CEIL, value=RSSI_FLOOR)
# ไม้บรรทัดจบที่ 688 ไม่ใช่ 752 เพราะมุมขวาล่างตั้งแต่ x=690 y=340 เป็นของปุ่ม Console
sc_rssi = ui.Scale(x=536, y=340, w=152, h=44, color=COL_TEXT,
                   min=RSSI_FLOOR, max=RSSI_CEIL)
# ไม้บรรทัดสั้น ๆ ที่มีตัวเลขหกตัวจะทับกันจนอ่านไม่ออก - เอาแค่สามพอ
sc_rssi.ticks(11, 5)
ui.poll()


# --- ท่าที่ 2 + 3: สแกน เรียงจากแรงไปอ่อน แล้วเทลงตาราง ---
def rescan():
    # scan() บล็อกราว 3-10 วินาที (ย่าน 5 GHz นานกว่า เพราะช่องสัญญาณเยอะกว่ามาก)
    nets = wifi.scan()

    # RSSI ติดลบ ค่าที่ "มากกว่า" คือแรงกว่า จึงเรียงมากไปน้อย ด้วย net[1] ซึ่งเป็น
    # ช่องที่สองของ tuple ไม่ใช่ net['rssi'] เพราะ scan() ไม่ได้คืน dict
    nets.sort(key=lambda net: net[1], reverse=True)
    print("found", len(nets), "networks")

    tbl.clear_items()
    # หัวตารางสั้นเพราะช่องแคบ - หัวที่ยาวกว่าช่องจะถูกตัดบรรทัด แล้วแถวนั้น
    # สูงเป็นสองเท่าทันที ดันแถวล่างสุดตกขอบตารางไปโดยไม่มีอะไรฟ้อง
    tbl.add_row("SSID", "dBm", "ช่อง", "รหัส")

    # ใช้ min() กันกรณีสแกนเจอน้อยกว่า TOP_N วง ไม่งั้นจะหลุด IndexError
    for i in range(min(TOP_N, len(nets))):
        # หนึ่งแถวของ scan() = (ssid, rssi, security, channel) แกะสี่ตัวพร้อมกันได้เลย
        ssid, rssi, security, channel = nets[i]

        # ตัดชื่อที่ 12 ตัวอักษรโดยตั้งใจ ชื่อที่ยาวกว่าคอลัมน์จะถูกตัดบรรทัด
        tbl.add_row(ssid[:12], str(rssi), str(channel),
                    "เปิด" if security == 0 else "มีรหัส")
        ui.poll()

    for ssid, rssi, security, channel in nets:
        if ssid == WIFI_SSID:
            pct, col = signal_of(rssi)
            # ไม่เรียก bar_rssi.color() เพราะ .color() ของ Bar ไปลงที่ "ราง"
            # ไม่ใช่ "แถบที่เต็ม" - รางที่เปลี่ยนสีทำให้ดูเหมือนแถบเต็มทั้งที่ค่ายังน้อย
            bar_rssi.value(rssi)
            l_rssi.color(col)
            l_rssi.text("ความแรง " + str(rssi) + " dBm (" + str(pct) + "%)")
            return nets
    bar_rssi.value(RSSI_FLOOR)
    l_rssi.text("ความแรง ไม่เจอวงของทีม")
    return nets


nets = rescan()

# --- ท่าที่ 4: ต่อเข้าเครือข่ายของทีม ---
l_tick.text("กำลังต่อ 85 วิ")
ui.poll()
# connect() รับสองอาร์กิวเมนต์ตามลำดับเท่านั้น และบล็อกจนกว่าจะรู้ผล
ok = wifi.connect(WIFI_SSID, WIFI_PASS)

try:
    if not ok:
        l_ssid.color(COL_BAD)
        l_ssid.text("ต่อไม่ติด")
        l_tick.text("ตรวจ SSID/รหัสผ่าน")
        ui.poll()
        raise Stop

    ip = wifi.ip()
    gw = gateway_of(ip)
    led_up.value(1)
    led_down.value(0)
    l_ssid.text("SSID: " + WIFI_SSID)
    l_ip.text("IP: " + ip)
    print("connected, ip =", ip, "gateway =", gw)

    # --- ท่าที่ 5: ลูปสถานะสด วัด ping สองปลายทาง และรับคำสั่งจากปุ่ม ---
    t_ping = time.ticks_ms() - PING_EVERY_MS   # ยิงรอบแรกทันที ไม่ต้องรอสามวินาที
    last_sec = -1                              # วินาทีที่เพิ่งเขียนลงจอ กันเขียนซ้ำถี่เกิน
    ms_gw, ms_net = -1, -1

    while True:
        now = time.ticks_ms()
        # ถ้าลืมบรรทัดนี้ เฟิร์มแวร์จะซ่อน widget ทิ้งภายในราวสองวินาที
        events = ui.poll()

        for ev in events:
            if ev["type"] == "clicked" and ev["handle"] == btn_scan.id():
                # สแกนใหม่บล็อกยาว บอกก่อนแล้วค่อยเรียก เหมือนท่าที่ 4 ทุกประการ
                l_tick.text("กำลังสแกน")
                ui.poll()
                nets = rescan()
                t_ping = time.ticks_ms() - PING_EVERY_MS
                last_sec = -1

        if wifi.is_connected():
            led_up.value(1)
            led_down.value(0)
            if time.ticks_diff(now, t_ping) >= PING_EVERY_MS:
                t_ping = now
                try:
                    # ping รับเฉพาะ IP เท่านั้น ใส่ชื่อโฮสต์จะได้ ValueError
                    ms_gw = wifi.ping(gw, PING_TIMEOUT_MS)
                    ms_net = wifi.ping(NET_TEST_IP, PING_TIMEOUT_MS)
                except OSError:
                    # ping จะโยน OSError ถ้าลิงก์หลุดระหว่างทาง ถือว่าไม่มีคำตอบทั้งคู่
                    ms_gw, ms_net = -1, -1
                l_gw.color(COL_TEXT if ms_gw >= 0 else COL_BAD)
                l_net.color(COL_TEXT if ms_net >= 0 else COL_BAD)
                l_gw.text(ms_text("เกตเวย์", ms_gw))
                l_net.text(ms_text("อินเทอร์เน็ต", ms_net))
        else:
            led_up.value(0)
            led_down.value(1)
            l_gw.color(COL_BAD)
            l_net.color(COL_BAD)
            l_gw.text("ลิงก์หลุด")
            l_net.text("เลขล่าสุด ไม่ใช่ตอนนี้")

        left = (PING_EVERY_MS - time.ticks_diff(now, t_ping)) // 1000
        if left != last_sec:
            last_sec = left
            l_tick.text("วัดใหม่ใน " + str(left) + " วิ")

        time.sleep_ms(200)
except Stop:
    pass
