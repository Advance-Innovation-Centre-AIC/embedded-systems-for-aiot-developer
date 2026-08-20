# main_loader.py -> flash เป็น /main.py ครั้งเดียว
#
# loader บาง ๆ: โหลดเมนูจริงจาก /p1menu.py ทุกครั้ง - ต่อไปอัปเดตเมนูหรือ
# ตัวอย่างแค่ส่งไฟล์ทับ (TACP --reset none) แล้วกดจากจอ ไม่ต้อง reset บอร์ดอีก
import time
import gc

while True:
    gc.collect()
    try:
        exec(open("/p1menu.py").read(), {"__name__": "__main__"})
    except Exception as e:
        print("loader: menu crashed:", repr(e))
        time.sleep_ms(2000)
