# 01_config_store.py - คลังค่าตั้งของแพลตฟอร์ม อ่านให้ครบก่อนจะต่ออะไร
#
# ไฟล์นี้สอน: tesaiot.config() คืน dict 19 คีย์ ที่บอกทั้งหมดว่าบอร์ดจะต่อไปที่ไหน
#             ด้วยตัวตนอะไร - ถามมันก่อนเดา
# ดูที่จอ   : เลข 19 ตัวใหญ่ - สี่คีย์ที่ต้องรู้จัก (device_id broker tls_mode port)
#             - บรรทัดสีส้มบอกว่า mqtt_pass ตั้งได้แต่อ่านกลับไม่ได้
# กับดัก    : mqtt_pass ตั้งค่าได้ด้วย config_set แต่ไม่ได้อยู่ใน config()
#             เขียน config()["mqtt_pass"] จะได้ KeyError บนบอร์ดจริง

import tesaiot
import lcd
import time
import ui

COL_TEXT, COL_DIM = 0xE8EAED, 0x9AA3AF
COL_CARD = 0x171B22
COL_OK, COL_WARN, COL_INFO = 0x30A46C, 0xF5A623, 0x4A9EFF

# จัดกลุ่มเพื่อให้อ่านออกว่าแต่ละคีย์ตอบคำถามอะไร ไม่ใช่แค่ไล่พิมพ์ตามตัวอักษร
GROUPS = (
    ("ตัวตนของอุปกรณ์", ("device_id", "factory_uid", "api_key", "tls_mode")),
    ("ปลายทาง MQTT", ("broker", "port", "sni_hostname", "qos", "keepalive")),
    ("ปลายทาง HTTPS", ("api_host", "api_port", "api_endpoint")),
    ("การลองใหม่และเวลา", ("timeout_ms", "max_retries", "retry_interval_ms",
                            "sntp_server", "sntp_timezone")),
    ("อื่น ๆ", ("wifi_ssid", "debug_level")),
)

# สี่คีย์นี้คือคีย์ที่ไฟล์ที่เหลือของคาบนี้จะหยิบไปใช้ จึงเอาขึ้นจอ ไม่ใช่ทั้ง 19
KEY_ROWS = ("device_id", "broker", "tls_mode", "port")

cfg = tesaiot.config()

ui.screen()
time.sleep_ms(200)

ui.Label("คลังค่าตั้ง tesaiot.config()", x=20, y=12, color=COL_TEXT, value=24)
ui.Panel(x=20, y=52, w=652, h=124, color=COL_CARD, min=COL_DIM, max=12, value=1)
ui.Label("คีย์ที่อ่านกลับได้", x=40, y=64, color=COL_DIM, value=16)
ui.Seg7(text=str(len(cfg)), x=40, y=88, w=152, h=72, color=COL_INFO)

y = 82
for k in KEY_ROWS:
    # คีย์ที่ยังว่างต้องอ่านออกว่า "ว่าง" ไม่ใช่หายไปเฉย ๆ เพราะบอร์ดที่ยังไม่ได้
    # provision จะมี device_id เป็นสตริงว่าง ไม่ใช่ไม่มีคีย์
    shown_value = cfg[k] if cfg[k] != "" else "(ยังไม่ตั้ง)"
    ui.Label("{} = {}".format(k, shown_value), x=216, y=y, color=COL_TEXT, value=16)
    y += 22

# คีย์ที่ตั้งค่าได้แต่อ่านกลับไม่ได้ - ความไม่สมมาตรแบบแรกของโมดูลนี้
# เหตุผลคือรหัสผ่านไม่ควรหลุดออกมาทางที่อ่านง่าย ๆ แต่ผลข้างเคียงคือ
# เราตรวจไม่ได้ว่าตั้งไปแล้วถูกหรือเปล่า ต้องพิสูจน์ด้วยการต่อสำเร็จเท่านั้น
has_pass = "mqtt_pass" in cfg
ui.Label("mqtt_pass อยู่ใน config() หรือไม่: {}".format(has_pass),
         x=20, y=188, color=COL_WARN, value=20)
# สองป้ายในบรรทัดเดียว ให้อยู่ในเพดาน 126 ไบต์ของ ui.Label - ไทยตัวละ 3 ไบต์
ui.Label("ตั้งได้ด้วย config_set แต่อ่านตรง ๆ", x=20, y=216, color=COL_WARN,
         value=16)
ui.Label("ได้ KeyError -> ใช้ cfg.get()", x=416, y=216, color=COL_WARN,
         value=16)

ui.Panel(x=20, y=244, w=652, h=76, color=COL_CARD, min=COL_DIM, max=12, value=1)
counts = []
for title, keys in GROUPS:
    counts.append("{} {}".format(title, len(keys)))
ui.Label(" - ".join(counts[:3]), x=40, y=260, color=COL_DIM, value=16)
ui.Label(" - ".join(counts[3:]), x=40, y=288, color=COL_DIM, value=16)

shown = 0
for _title, keys in GROUPS:
    shown += len(keys)
ui.Label("ไล่ครบ {} จาก {} คีย์ - รายละเอียดอยู่ในคอนโซล".format(shown, len(cfg)),
         x=20, y=336, color=COL_OK if shown == len(cfg) else COL_WARN, value=20)
ui.poll()

# คอนโซลรับรายการแบบอ่านเรียง ส่วน print() รับตารางเต็มที่ยาวเกินจอ 4.3 นิ้ว
lcd.clear()
lcd.console("<h2>คาบ 11 - คลังค่าตั้ง</h2>")
lcd.print("config() คืนมา", len(cfg), "คีย์")
for title, keys in GROUPS:
    lcd.print("<b>" + title + "</b>")
    for k in keys:
        lcd.print("  ", k, "=", cfg[k])

print("config() คืนมาทั้งหมด", len(cfg), "คีย์")
print("")
for title, keys in GROUPS:
    print(title)
    for k in keys:
        print("  {:<18} = {}".format(k, cfg[k]))
    print("")

# ตรวจให้แน่ว่าเราไล่ครบ ไม่ได้ลืมคีย์ไหนไว้นอกกลุ่ม
if shown != len(cfg):
    print("มีคีย์ที่ยังไม่ได้จัดกลุ่ม:")
    for k in sorted(cfg.keys()):
        found = False
        for _title, keys in GROUPS:
            if k in keys:
                found = True
        if not found:
            print("  ", k, "=", cfg[k])

# รหัสผ่านเป็นคีย์ที่เขียนลงไปได้ แต่อ่านกลับมาไม่ได้ ซึ่งเป็นสิ่งที่ควรเป็น
# ของที่เป็นความลับไม่ควรมีทางออกจากอุปกรณ์ผ่านคำสั่งอ่านค่าธรรมดา
lcd.print("<span class=warn>mqtt_pass อยู่ใน config():", has_pass, "</span>")
lcd.print("อ่านด้วย .get():", cfg.get("mqtt_pass", "อ่านกลับไม่ได้"))
lcd.print("tls_mode =", cfg["tls_mode"], "| port =", cfg["port"])
# เมื่ออ่านค่าครบแล้ว ขั้นถัดไปคือรอให้ลิงก์ขึ้นจริงก่อนจะส่งอะไร
print("อ่านค่าครบแล้ว ขั้นถัดไปอยู่ที่ examples/s11/05_wait_for_connected.py")
