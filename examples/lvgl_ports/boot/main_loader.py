# boot/main_loader.py -> flash เป็น /main.py ครั้งเดียว
# loader บาง ๆ: วนโหลดหน้าเลือก "ตอน" จาก /menu.py — อัปเดตเมนู/ตัวอย่างใด ๆ
# ภายหลังส่งไฟล์ทับด้วย TACP --reset none แล้วปิดชุดด้วย hard reset ตาม SOP
import time
import gc

while True:
    gc.collect()
    try:
        exec(open("/menu.py").read(), {"__name__": "__main__"})
    except Exception as e:
        print("loader: menu crashed:", repr(e))
        time.sleep_ms(2000)
