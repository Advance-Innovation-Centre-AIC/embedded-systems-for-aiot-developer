# lvgl_ports — ถอดตัวอย่าง LVGL C เป็น MicroPython (โครงตาม "ตอน" ของคอร์ส)

ต้นทาง: คอร์ส C เทอมก่อน `psoc-e84-e2-lvgl-aic-eec` (47 ตัวอย่างใน
`example_selector.h`; สำเนาอ่านได้: `BiiL_Center/AIC-EEC_psoc-e84-e2-lvgl-bill`)
เป้าหมาย: หน้า UI + การแสดงข้อมูลเหมือน C โดยใช้กลไก firmware ที่มีอยู่จริงเท่านั้น
**ไม่แก้ config บอร์ดใด ๆ** — ตัวอย่างล้อตามเมนู native ของแต่ละบอร์ด
(อนุมัติโครงโดยอาจารย์ 2026-08-20; แผน: `TESAIoT_PLAN/2026-8/LVGL_Ports_Restructure/`)

## โครง

```
boot/main_loader.py        -> /main.py   loader วนโหลด /menu.py
boot/menu_sections.py      -> /menu.py   หน้าเลือก "ตอน" (ตรงโครง gen_roadmap.py)
sec2_ui_to_hw/             ตอนที่ 2 · UI-to-Hardware Interfacing
  menu_sec2.py             -> /s2menu.py
  devkit/ex01..ex11.py     ฉบับ TESAIoT Dev Kit (ล้อเมนู Sensor Dashboard + GPIO & RGB)
  eva/ex01..ex11.py        ฉบับ Eva Kit (ล้อเมนู Sensor Dashboard + Controls)
sec3_sensor_viz/           ตอนที่ 3 · Sensor Visualization on HMI
  menu_sec3.py             -> /s3menu.py (สองหน้า: Part 2 / Part 3)
  {eva,devkit}/ex01-ex11   C Part 2 ×11 (Sensor Visualization)
  {eva,devkit}/ex12-ex18   C Part 3 ×7 (Oscilloscope & DSP; ex3+ex4 ยุบเป็น ex14)
part1/                     ชุด adaptive เดิม (legacy) — คงไว้เป็น regression gate
                           ของ Bento_Engine/IMPLEMENT_PLAN 1.4; เข้าถึงจากหน้า
                           เลือกตอน ("Part 1 legacy") ถ้า /p1*.py ยังอยู่บน FS
screens/                   ภาพคู่ emu_*/hw_* ต่อตัวอย่าง
```

## นโยบาย per-board (คำตัดสินอาจารย์ + red team 2026-08-20)

- **สองสำเนาเต็มต่อบอร์ด** — ผู้เรียนอ่านโค้ดบอร์ดตัวเองล้วน ๆ ไม่มี try/except
  ข้ามบอร์ด ทุกไฟล์ยังคง self-contained (วางลง Playground/IDE ได้ทันที)
- ค่าประจำบอร์ดอยู่ในบล็อก `# ==== BOARD: ... ==== / # ==== END BOARD ====`
  ต้นไฟล์ — ส่วนนอกบล็อกของคู่ eva/devkit ต้อง "ตรงกัน" (gate ตรวจตอน publish)
- **Emulator ใช้ชุด `eva/`** (โปรไฟล์จำลองของ Emulator คือ Eva; ชุด devkit import
  `pots/buttons/rgbmatrix` ซึ่ง Emulator ไม่มี)
- **ex01/ex02 คงไว้ทั้งที่ไม่แตะ GPIO** — เป็น anchor ที่อาจารย์ verify บนจอจริง
  และ ex02 คือ regression ของเส้นทาง touch+IPC; กติกา "ทุกตัวอย่างคุมของจริง"
  บังคับตั้งแต่ ex03 ขึ้นไปและทั้ง sec3

## ความจริงต่อบอร์ด (สกัดจากเมนู native — file:line ในรายงาน ground-truth)

| | Eva Kit | TESAIoT Dev Kit |
|---|---|---|
| LED | แยก 3 ดวง: idx 0=แดง P16.7, 1=เขียว P16.6, 2=น้ำเงิน P16.5 (ตารางชื่อ firmware เรียก idx2 ว่า "RGB_RED" — อย่าเลือกด้วยชื่อ) | RGB ดวงเดียว: idx 2=แดง/3=น้ำเงิน/4=เขียว (P20.6/5/4); LED1/2 สั่งได้แต่มองไม่เห็น |
| Pot | 1 ตัว P15.1 — `sensors.pot.read()` 0-65535 (>>4 ให้ตรงสเกล C); voltage เป็นค่าคำนวณ | 4 ตัว VR1-4 — `pots.read(0..3)` 0-4095 **ตรงสกรีนแล้ว** (fix 2026-08-20 `ec2b6e8`) |
| ปุ่ม | `gpio.button(0)` (SW2 จริง ชื่อรายงาน "SW1"); SW4 มีบนเมนูแต่ MPY เอื้อมไม่ถึง → ใช้ CapSense BTN0 แทน | `gpio.button(0)` = SW1 + `buttons.pressed(0/1)` = คู่ P17.5/P17.7 (เมนู native เรียก **SW5/SW6**, โมดูลเรียก SW9/SW10 — ขาเดียวกัน) |
| CapSense | `sensors.capsense.*` (ผ่าน CM55 snapshot) | เหมือนกัน (ผ่าน IPC CONTROLS_STATE) |
| เซนเซอร์เพิ่ม | — | DPS368 (0x77), SHT40 (0x44), Radar — `hasattr(sensors,"dps368")` ก่อนใช้ |
| Output เพิ่ม | — | RGB Matrix 16x8 (`rgbmatrix.*`, I2C 0x10) + `ui.Sprite` ภาพจริง |
| กับดักที่รู้ | native Controls โชว์ SW4 ที่ MPY ไม่เห็น | Dashboard native อ่าน accel ~4× สูงเกิน (บั๊ก firmware ฝั่งจอ — ค่า MPY ถูก; รายงานแยกแล้ว) |

## เงื่อนไขเฟิร์มแวร์ขั้นต่ำ (สำคัญ)

ชุดนี้ใช้ `ui.Led / ui.Scale / ui.Line / ui.Tabview / ui._diag` ซึ่ง**ยังไม่อยู่ใน
firmware release template** (`TESAIoT_Firmware_Development_and_Release`) — ต้องใช้
firmware ที่ build จากทรีนี้ (2026-08-20+) จนกว่าจะมี release รอบถัดไป

## วิธีขึ้นบอร์ด

```bash
T=TESAIoT_Dev_Kit_C_and_Micropython_Examples/tools/tacp_program.py
L=AIoT_Cirriculum/examples/lvgl_ports
# เลือกชุดบอร์ด: devkit หรือ eva
i=1; for f in $L/sec2_ui_to_hw/devkit/ex*.py; do
  python3 $T "$f" --dest /s2e$(printf %02d $i).py --reset none; i=$((i+1)); done
python3 $T $L/sec2_ui_to_hw/menu_sec2.py --dest /s2menu.py --reset none
python3 $T $L/boot/menu_sections.py     --dest /menu.py   --reset none
python3 $T $L/boot/main_loader.py                     # -> /main.py + hard reset ปิดชุด
```

กติกา deploy (บทเรียนจ่ายจริง 2026-08-19/20): ชุดส่งไฟล์ปิดท้ายด้วย hard reset เสมอ
(`--reset none` ทิ้งจอค้าง Programming Mode) · ห้าม Ctrl-C โปรแกรมที่ยิง ui ·
grab.sh ใช้ one-shot · จอดำหลัง reset = backlight quirk → power-cycle ·
"·" (middle dot) ไม่มี glyph บนจอ — ใช้ "-" ใน string ที่แสดงผล ·
ก่อน mass-deploy ในห้องเรียนให้ทำ FW-1 ของ `DevKit_Large_Script_Deploy_Freeze` ก่อน

## กติกาถอด (ต่อจากชุดเดิม)

จอ C 800x480 → ui 792x398 (y×0.83) · ฟอนต์ `value=` 14/16/20/24/28 · จานสีตามเลข C ·
timer → ลูป `ticks_ms` · Chart default 50 จุด/4 series — ตั้งได้ถึง 400 ด้วย
`ch.prop(ui.PROP_CHART_POINTS, n)` (R4, firmware 2026-08-20+) ·
`ui.Scale` มีเข็มจริงแล้ว: `sc.prop(ui.PROP_SCALE_NEEDLE, (len<<16)|int(v))`
(+ `PROP_SCALE_NEEDLE_COLOR`; firmware 2026-08-20+ — แบบ ui.Line ลบ/สร้างใหม่กระพริบ) ·
เพดาน 64 widgets

## กติกา UX (คำตัดสินอาจารย์ 2026-08-20)

- ปุ่มนำทาง (`< Menu` ในตัวอย่าง / `< Sections` ในเมนู) อยู่**มุมล่างซ้าย**
  (8,344..346) — มุมบนซ้ายชนกับ `< Home` ของหน้า Playground เอง
- ขนาดขั้นต่ำของ touch target: ปุ่ม h≥44, Slider h=22, Dropdown h=44, Switch ≥70x36
- gate กันถอยหลัง: สแกน overlap กับ rect ปุ่ม back ก่อน publish (สแกนแล้ว 2026-08-20 — 0 ชน)

## Part 3 — เส้นทาง DSP ที่พึ่ง firmware ใหม่ (R4, 2026-08-20)

- `ch.prop(ui.PROP_CHART_POINTS, 200)` — สโคป 200 จุดเท่าต้นฉบับ C
- `dsp.fft_mag(samples, n=256)` — FFT จริงใน C (~1ms ที่ N=256); host-harness 14/14
  + emulator parity ผ่าน; `demean=True` ปริยาย กัน DC ไมค์รั่วเป็นยอดปลอมที่ bin 1
- `mic.raw()` + `dsp.s16(buf, step)` — เส้นเสียงจบใน C แทนลูป Python ~280ms
- การวาด 200 จุด = 200 ข้อความ IPC — จังหวะ `draw_trace()` (sleep 6ms ทุก 16 จุด)
  กัน load-shed ที่คิวลึก ≥56

## Fidelity Ledger — sec2 (สถานะ 2026-08-20)

| ตัวอย่าง | devkit | eva |
|---|---|---|
| ex01 hello | ✅ จอจริง (สืบทอดจาก part1) | 🖥️ emulator รอบสอบ | 
| ex02 button counter | ✅ จอจริง | 🖥️ emulator |
| ex03 LED widget + จริง | ✅ จอจริง (UI) / PWM เขียวจริง ✅ | 🖥️ emulator — **รอบอร์ด Eva** |
| ex04-ex11 | 🖥️ สร้างจาก part1 ที่ผ่าน emulator; บนบอร์ด: เมนู sec2 ใช้งานแล้ว รอไล่รายตัว | 🖥️ emulator — **รอบอร์ด Eva** (ไม่มีบอร์ดบนโต๊ะ) |

## Fidelity Ledger — sec3 Part 3 (ex12-ex18, สถานะ 2026-08-20)

| ตัวอย่าง | ต้นฉบับ C | fidelity |
|---|---|---|
| ex12 waveform gen | part3_ex1 | exact (200 จุด, dropdown 5 คลื่น, slider กำลังสอง) |
| ex13 noise gen | part3_ex2 | approximated: C วาด 20Hz → ~2Hz (คอ IPC) |
| ex14 mic waveform | part3_ex3+ex4 ยุบ | จริงกว่า C: ไมค์ PDM จริงแทน sim; autogain แทนสเกล int16 ตายตัว |
| ex15 oscilloscope | part3_ex5 | exact (Vpp/Freq/RMS คำนวณจริง; time/volt div เป็น label ตาม C) |
| ex16 spectrum | part3_ex6 | approximated: BAR chart → LINE trace (ui.Chart เป็น LINE เท่านั้น); FFT จริง |
| ex17 scope panels | part3_scope_example | exact โครง 3 หน้า hide/show; ไม่มี faded-area |
| ex18 hw scope | part3_hw_scope_example | จริง: pot → duty + LED PWM จริง; ยุบ 3 แท็บเป็นจอเดียว |

ทั้งเจ็ด: emulator sweep 7/7 + ภาพใน screens/ (2026-08-20); บนบอร์ด devkit รอไล่รายตัว

หมายเหตุ ledger ละเอียดของ part1 เดิม: ดู git history ของ README นี้ (commit 97c53001..11f7e44e)
