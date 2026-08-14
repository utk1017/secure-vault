# IoT & Embedded System Based Secure Vault

A smart, ESP32-powered vault that upgrades a traditional keypad locker with anti-tamper detection, remote lock/unlock via **ESP RainMaker**, and real-time push notifications , all running on a non-blocking finite-state-machine firmware.



---

## Features

- **4-digit PIN authentication** : via a 4x4 matrix keypad (`*` clears, `#` submits), with a 30-second lockout after 3 wrong attempts (brute-force protection).
- **Remote lock/unlock** : through the ESP RainMaker mobile app  but only when the vault is explicitly waiting in *Phone/RainMaker Mode* (selected with `A` on the keypad menu). Remote commands received in any other state are dropped, so a stray cloud command can never silently unlock the vault.
- **Anti-tamper detection** : using an SW-420 vibration sensor: instead of reacting to a single vibration pulse, the firmware requires **11 HIGH readings inside a rolling 1.5-second window** : before declaring an intrusion , this filters out light knocks/accidental touches while still catching sustained forced-entry attempts.
- **Instant alerts** : on tamper detection: continuous buzzer, red LED blinking every 150 ms, LCD "INTRUDER!" message, and a RainMaker push notification.
- **User presence detection** : via an HC-SR04 ultrasonic sensor ,the vault wakes from idle and shows a welcome/menu screen when someone approaches within ~50 cm.
- **Live LCD status display** (16x2 I2C) : Ready, Welcome User, menu, Password entry, Access Granted, Wrong Password + attempts left, lockout countdown, Vault Open/Locked, Cancelled, INTRUDER!.
- **Non-blocking state-machine firmware** : no blocking `while(true)` loops; the reset button, RainMaker commands, and vibration monitoring are checked on **every** loop pass regardless of which screen is active.
- **Manual re-locking** : the vault does not auto re-lock on a timer; press `C` (or send a remote lock command) to secure it again.
- **Power-stability fix**: a 3300µF/16V decoupling capacitor across the main rail eliminates brownout resets caused by the servo's current spike on unlock, with the ESP32's hardware brownout detector also disabled at boot as a backstop.

---

##  Hardware Components

| Component | Spec | Function |
|---|---|---|
| ESP32 Dev Board | Dual-core, Wi-Fi + BLE | Central controller / RainMaker connection |
| 4x4 Matrix Keypad | 16-key membrane | PIN entry + menu navigation |
| 16x2 I2C LCD | HD44780 + I2C backpack (`0x27`) | Real-time status display |
| Servo Motor | SG90-type, 60°–90° travel | Locking mechanism |
| Ultrasonic Sensor | HC-SR04, ~2–400 cm | User presence detection |
| Vibration Sensor | SW-420, digital out (idle LOW) | Anti-tamper / intrusion detection |
| Buzzer | 5V active | Audible alerts |
| Decoupling Capacitor | 3300µF, 16V electrolytic | Power rail stabilization for servo |

Full details in [`docs/hardware.md`](docs/hardware.md).

## Pin Configuration

| Signal | ESP32 GPIO | Mode |
|---|---|---|
| Ultrasonic Trigger | GPIO 19 | Output |
| Ultrasonic Echo | GPIO 34 | Input |
| Servo Signal | GPIO 5 | PWM Output |
| Green LED | GPIO 13 | Output |
| Red LED | GPIO 12 | Output |
| Buzzer | GPIO 18 | Output |
| Vibration Sensor (SW-420) | GPIO 16 | Input |
| LCD – SDA | GPIO 21 | I2C |
| LCD – SCL | GPIO 22 | I2C |
| Wi-Fi / Factory Reset | GPIO 0 (BOOT) | Input |
| Keypad Rows | GPIO 23, 17, 32, 33 | Scanned output |
| Keypad Columns | GPIO 25, 26, 27, 14 | Scanned input |

## Key Controls

| Key | Action |
|---|---|
| `0–9` | Enter PIN digits |
| `*` | Clear entered PIN |
| `#` | Submit PIN |
| `A` | Select Phone/RainMaker mode (menu) |
| `B` | Select Keypad mode (menu) |
| `C` | Re-lock the vault |
| `D` | Cancel / silence intruder alarm |

---

## How It Works

The vault is idle by default, using the ultrasonic sensor to detect an approaching user. Once detected, it shows a menu to pick **Keypad Mode (`B`)** or **Phone/RainMaker Mode (`A`)**:

- **Keypad Mode** → enter a 4-digit PIN, `#` to submit. Correct PIN unlocks the servo and stays unlocked until `C` is pressed. Wrong PIN increments an attempt counter; 3 wrong attempts trigger a 30-second lockout.
- **Phone Mode** → the vault waits for a RainMaker command. Commands received *outside* this exact state are ignored , a deliberate security boundary so the app can never override the lock without the user first selecting this mode on the physical keypad.

In parallel, on **every single loop iteration**, the firmware checks the reset button, any pending RainMaker command, and the vibration sensor  regardless of which of the above screens is active. If 11 vibration hits land inside a 1.5-second window, the vault immediately alarms: continuous buzzer + blinking red LED + LCD warning + push notification, until cleared via `D` or a remote silence command, followed by a 3-second cooldown to prevent re-triggering on residual vibration.

See [`docs/architecture.md`](docs/architecture.md) for the full state diagram, RainMaker device setup, and design evolution (initial blocking implementation → final non-blocking FSM).

---

## Repository Structure

```
secure-vault/
├── firmware/
│   └── SecureVault/
│       └── SecureVault.ino     # Full ESP32 firmware (Arduino-compatible)
├── docs/
│   ├── hardware.md             # Component details & wiring notes
│   ├── architecture.md         # FSM design, mode-wise flow, RainMaker setup
│   ├── testing.md              # Functional test scenarios & results
│   ├── images/                 # Photos of the completed prototype
│   └── circuit/                # Schematic / PCB layout images
├── report/
│   └── Utkarsh_SummerTrainingReport.pdf
├── LICENSE
└── README.md
```

---

##  Getting Started

### Requirements
- [Arduino IDE](https://www.arduino.cc/en/software) (2.x recommended)
- ESP32 board support package installed
- Libraries (via Library Manager):
  - `Keypad`
  - `LiquidCrystal_I2C`
  - `ESP32Servo`
  - ESP RainMaker Arduino core (`RMaker.h`, `WiFiProv.h`) : installed alongside the ESP32 board package or from [Espressif's RainMaker Arduino repo](https://github.com/espressif/arduino-esp-rainmaker)

### Flashing
1. Open `firmware/SecureVault/SecureVault.ino` in the Arduino IDE.
2. Select your ESP32 board and correct COM port.
3. **Before deploying**, change:
   - `password` (default vault PIN, currently `"1234"`)
   - `pop` (BLE provisioning proof-of-possession, currently `"QWERTYUI"`)
   - Consider moving both out of the `.ino` into a git-ignored `secrets.h` (see `.gitignore`) rather than committing them in plaintext.
4. Upload.
5. Provision the device to Wi-Fi using the ESP RainMaker app to scan the QR code printed to Serial on first boot (BLE, service name `MEE`).

### Wiring
Refer to the pin table above and `docs/hardware.md`. **Do not skip the 3300µF/16V capacitor across the servo's power rail** , without it, expect intermittent brownout resets when the servo actuates, even with the software brownout-detector override in place.

---

## Testing Summary

| Scenario | Result |
|---|---|
| Correct PIN + `#` | Unlocks, "Access Granted" on LCD, green LED on, push notification sent |
| Incorrect PIN (x3) | Buzzer sounds, 30s lockout enforced |
| RainMaker unlock inside Phone Mode | Unlocks immediately |
| RainMaker unlock outside Phone Mode | Correctly ignored |
| Isolated vibration bump | No false alarm (11-hit / 1.5s window filter) |
| Sustained hard tampering | Alarm triggers immediately, notification sent |
| Repeated cycling, pre-capacitor | Intermittent brownout resets |
| Repeated cycling, post-capacitor | Stable, no resets |

Full results in [`docs/testing.md`](docs/testing.md).

---

## Future Enhancements

- Time-stamped access history log (synced to RainMaker/cloud)
- ESP32-CAM integration for capturing images on tamper events
- Multi-user PIN support
- Battery backup with low-battery alerts
- Biometric (fingerprint/face) multi-factor authentication
- Sensor fusion (reed switch, accelerometer) for improved tamper accuracy
- Cloud analytics / access pattern reports
- Geofencing & scheduling in the mobile app

---

## License

This project is licensed under the MIT License : see [LICENSE](LICENSE).


