# Hardware

## Components & Function

| Component | Specification | Function |
|---|---|---|
| ESP32 Development Board | Dual-core, Wi-Fi + BLE | Central controller running the state machine and RainMaker connection |
| 4x4 Matrix Keypad | 16-key membrane keypad | PIN entry and menu navigation |
| 16x2 LCD Display | HD44780 with I2C backpack (addr `0x27`) | Real-time status display |
| Servo Motor | SG90-type, 60°–90° travel | Physical vault locking mechanism |
| Ultrasonic Sensor | HC-SR04, ~2–400 cm range | User presence detection |
| Vibration Sensor | SW-420, digital output (idles LOW, drives HIGH on hit) | Anti-tamper / intrusion detection |
| Buzzer | 5V active buzzer | Audible alerts and alarm |
| Decoupling Capacitor | 3300µF, 16V electrolytic | Power rail stabilisation for the servo |

## Notes per component

**ESP32 Development Board**
Runs the non-blocking state machine that manages the keypad, LCD, servo, ultrasonic sensor, vibration sensor, and the ESP RainMaker cloud connection. BLE is used for initial device provisioning (service name `MEE`), Wi-Fi for the ongoing RainMaker connection.

**4x4 Matrix Keypad**
- Digits `0–9` → PIN entry
- `A` → select Phone/RainMaker mode from the menu
- `B` → select Keypad mode from the menu
- `C` → manually re-lock the vault after it's been unlocked (either via keypad or phone)
- `D` → cancel out of a menu/mode, or silence an active intruder alarm
- `#` → submit the entered PIN for verification
- `*` → clear the currently entered PIN digits

**16x2 I2C LCD**
Displays: Ready, Welcome User, menu (`A: PHONE` / `B: KEY`), Password prompt, Access Granted, Wrong Password + attempts left, lockout countdown, Vault Open/Locked, Cancelled, and INTRUDER!. Communicates over I2C (`SDA`/`SCL`) to keep GPIO usage minimal.

**Servo Motor (locking mechanism)**
Drives the bolt between locked (60°) and unlocked (90°). The vault does **not** auto-relock on a timer — the servo stays unlocked until the user presses `C` (from either keypad or phone-unlocked state) or sends a remote lock command.

**HC-SR04 Ultrasonic Sensor**
Polled continuously while idle; when a user is detected within ~50 cm, the vault shows a welcome message and moves to the access-mode menu.

**SW-420 Vibration Sensor**
Core of the anti-tamper system. The sensor idles LOW and drives HIGH on a hit (no internal pull-up is used, since one would fight the sensor's own idle-LOW output). Detection logic:
- Vibration is only monitored while the vault is **locked** and **not already in an alarm or cooldown**.
- Each HIGH reading within a rolling **1.5-second window** counts as a "hit."
- Once **11 hits** land inside that window, an intruder alarm is triggered.
- If more than 1.5 seconds pass without reaching 11 hits, the counter resets — this filters out isolated bumps while still catching sustained forceful impacts.

**Buzzer**
Audible feedback for wrong-password entry (800 ms tone) and continuous during the intruder alarm.

**3300µF / 16V Electrolytic Capacitor**
Added across the main power rail to fix a recurring brownout reset that happened the instant the servo began moving — the servo's transient current spike caused a voltage dip large enough to trip the ESP32's brownout detector. As a second line of defense, the firmware also disables the hardware brownout detector at boot (`WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)`); the capacitor remains the real fix and should not be removed just because the detector is disabled in software.

## GPIO Pin Configuration

| Component / Signal | ESP32 GPIO Pin | Mode |
|---|---|---|
| Ultrasonic Trigger (TRIG_PIN) | GPIO 19 | Output |
| Ultrasonic Echo (ECHO_PIN) | GPIO 34 | Input |
| Servo Signal (SERVO_PIN) | GPIO 5 | PWM Output |
| Green LED | GPIO 13 | Output |
| Red LED | GPIO 12 | Output |
| Buzzer | GPIO 18 | Output |
| Vibration Sensor (SW-420) | GPIO 16 | Input |
| LCD – SDA | GPIO 21 | I2C |
| LCD – SCL | GPIO 22 | I2C |
| Wi-Fi / Factory Reset Button | GPIO 0 (BOOT) | Input |
| Keypad Rows (R1–R4) | GPIO 23, 17, 32, 33 | Output (scanned) |
| Keypad Columns (C1–C4) | GPIO 25, 26, 27, 14 | Input (scanned) |

## Photos

Add your prototype photos here:
- `images/front-view.jpg` — Completed Prototype, Front View
- `images/internal-wiring.jpg` — Completed Prototype, Internal Wiring
- `circuit/schematic.png` — Circuit schematic
- `circuit/pcb-layout.png` — Final PCB layout
