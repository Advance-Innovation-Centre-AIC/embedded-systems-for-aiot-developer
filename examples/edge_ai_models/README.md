# ตัวอย่าง AI หนึ่งไฟล์ต่อหนึ่งโมเดล (Edge AI Models)

ในโฟลเดอร์นี้ โมเดล Edge AI บนบอร์ดแต่ละตัวมีตัวอย่างของตัวเองหนึ่งไฟล์ แต่ละไฟล์ออกแบบหน้าจอตามงานจริงที่โมเดลนั้นเหมาะจะทำ เช่น นับเสียงไอในโรงเรือน ตรวจการสั่นของปั๊ม หรือทำปุ่มไร้สัมผัสด้วยเรดาร์

อยากลองโมเดลไหนก็ได้บนหน้าจอเดียว ให้ใช้ AI Model Lab ([`sf3_07_ai_model_lab.py`](https://github.com/Advance-Innovation-Centre-AIC/aiot-development-for-smart-farm/blob/main/s3/sf3_07_ai_model_lab.py) ในรีโพหลักสูตร Smart Farm)

## สถานะการทดสอบ

- **ทดสอบแล้ว:** TESAIoT Dev Kit เฟิร์มแวร์ 2.4.2 รันด้วยปุ่ม Run ของ BENTO IDE ทุกไฟล์ผ่าน 3 ใน 3 รอบ เมื่อวันที่ 29 ก.ย. 2026
- **ยังไม่ได้ทดสอบ:** Eva Kit และ BENTO Emulator
- ทุกไฟล์ต้องใช้บอร์ดจริง

## รายการตัวอย่าง

| ไฟล์ | โมเดล | ใช้ทำอะไร |
|---|---|---|
| [`motion_detection.py`](motion_detection.py) | Motion Detection (ในตัว) | สวิตช์ท่าทาง: วาดวงกลมกลางอากาศเพื่อเปิด/ปิดไฟ |
| [`push_detection.py`](push_detection.py) | Push Detection (ในตัว) | ปุ่มไร้สัมผัส: ดันฝ่ามือเข้าหาเรดาร์ที่ระยะ 30–60 ซม. ใช้ได้เฉพาะ Dev Kit ที่มีเรดาร์ |
| [`baby_cry_detection.py`](baby_cry_detection.py) | Baby Cry Detection (ในตัว) | เตือนเมื่อได้ยินเสียงเด็กร้อง |
| [`cough_detection.py`](cough_detection.py) | Cough Detection (ในตัว) | นับเสียงไอในโรงเรือน |
| [`alarm_detection.py`](alarm_detection.py) | Alarm Detection (ในตัว) | ฟังเสียงสัญญาณเตือนภัย ใช้เพื่อการเรียนเท่านั้น ห้ามใช้แทนอุปกรณ์ความปลอดภัยจริง |
| [`siren_detection.py`](siren_detection.py) | Siren Detection (ในตัว) | เตือนเมื่อได้ยินเสียงไซเรน |
| [`siren_detection_store.py`](siren_detection_store.py) | SirenDetection (Store) เทียบกับ Siren Detection (ในตัว) | เทียบสองโมเดลแบบ A/B: เปิดเสียงไซเรน กด Swap แล้วเปิดเสียงเดิมอีกครั้ง |
| [`anomalous_vibration.py`](anomalous_vibration.py) | AnomalousVibration (Store) | ตรวจสุขภาพปั๊ม: ติดบอร์ดกับพัดลมที่มีตะแกรงครอบ แล้วตั้งเกณฑ์ Alert/Danger ด้วยแถบเลื่อน |
| [`human_activity.py`](human_activity.py) | HumanActivity (Store) | ไทม์ไลน์กิจกรรม: ติดบอร์ดไว้ที่หน้าอก |
| [`surface_mic.py`](surface_mic.py) | SurfaceMic (Store) | สมองหุ่นยนต์ดูดฝุ่น: แยกชนิดพื้นผิวจากเสียงเครื่องดูดฝุ่น |
| [`drill_material_mic.py`](drill_material_mic.py) | DrillMaterialMic (Store) | สว่านอัจฉริยะ: แยกชนิดวัสดุจากเสียงเจาะ **ผู้สอนสาธิตเท่านั้น** ต้องยึดชิ้นงานให้แน่น และสวมแว่นกับที่ครอบหู |
| [`home_sounds.py`](home_sounds.py) | HomeSounds (Store) | กระดานเสียงในบ้าน 3 ชนิด แตะช่องเพื่อปิด/เปิดเสียงเตือนของชนิดนั้น |
| [`environment_sounds.py`](environment_sounds.py) | Environment Sounds (Store, ทดลอง) | ห้องทดลองความสับสนของโมเดล: แตะช่อง แล้วเปิดเสียงนั้น 10 วินาที |
| [`yes_no.py`](yes_no.py) | Yes / No (Store, ทดลอง) | ยืนยันด้วยเสียง: แตะ Ask แล้วพูด yes หรือ no ภายใน 5 วินาที |
| [`voice_commands.py`](voice_commands.py) | Voice Commands (Store, ทดลอง) | บังคับรถเข็นด้วยคำสั่งเสียง |

## ก่อนเริ่ม

- **โมเดลในตัว:** มีมากับเฟิร์มแวร์แล้ว รันได้ทันที
- **โมเดลจาก Store:** ต้อง deploy ลงบอร์ดจาก Edge AI Store ก่อน ซึ่งต้อง sign in ด้วย GitHub ถ้าบนบอร์ดยังไม่มีโมเดลนั้น ตัวอย่างจะขึ้นการ์ดบอกให้ไป deploy
- **ปุ่มบนบอร์ด Dev Kit:**
  - SW6 (ปุ่มบน) = รับทราบการเตือน หรือทำหน้าที่ที่เขียนไว้ในหัวไฟล์
  - SW5 (ปุ่มล่าง) = หยุดชั่วคราว/ทำต่อ
  - แตะบนจอได้เหมือนกัน
- **โมเดลทดลอง 3 ตัว** (Environment Sounds, Yes / No, Voice Commands) ยังตอบผิดได้บ่อย ใช้สอนเรื่องความสับสนของโมเดลและการตั้งเกณฑ์ความมั่นใจ
