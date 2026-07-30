# 🔐 IoT & Embedded System Based Secure Vault

An IoT-based smart vault built using the **ESP32** that combines traditional PIN-based authentication with cloud connectivity and real-time intrusion detection. Unlike conventional electronic lockers, this project not only provides secure local and remote access but also continuously monitors the vault for physical tampering and instantly alerts the owner through the ESP RainMaker platform.

Developed as part of a Summer Training project in Embedded Systems and IoT.

---

## 📖 Table of Contents

- Introduction
- Features
- Hardware Components
- Software Stack
- System Workflow
- Hardware Architecture
- Project Structure
- Installation
- Usage
- Testing
- Future Improvements
- Gallery
- License

---

# 📌 Introduction

Traditional mechanical lockers depend entirely on physical keys, which can be lost, duplicated, or stolen. While keypad-based lockers eliminate the need for keys, most commercially available systems lack intelligent monitoring and remote accessibility.

This project bridges that gap by integrating **embedded systems**, **IoT**, and **real-time security monitoring** into a single smart vault. The system supports secure keypad authentication, cloud-based control using **ESP RainMaker**, vibration-based intrusion detection, live status updates, and instant smartphone notifications.

---

# ✨ Features

### 🔑 Secure Authentication

- 4-digit PIN authentication
- Password masking on LCD
- Protection against brute-force attacks
- 30-second lockout after three incorrect attempts

### 📱 IoT Integration

- Remote lock/unlock through ESP RainMaker
- Smartphone push notifications
- Live device status
- Cloud-based control over Wi-Fi

### 🚨 Security Features

- Vibration-based tamper detection
- Audible buzzer alarm
- Flashing red warning LED
- Intrusion notifications
- Alarm can be cleared locally or remotely

### 🤖 Smart Operation

- User detection using ultrasonic sensor
- Automatic welcome screen
- Menu-based authentication selection
- Servo-controlled locking mechanism
- Non-blocking Finite State Machine firmware

---

# 🛠 Hardware Components

| Component | Purpose |
|------------|----------|
| ESP32 | Main controller |
| 4×4 Matrix Keypad | PIN entry |
| SG90 Servo Motor | Locking mechanism |
| HC-SR04 Ultrasonic Sensor | User detection |
| SW-420 Vibration Sensor | Tamper detection |
| 16×2 I2C LCD | Status display |
| Active Buzzer | Alarm |
| Red & Green LEDs | Status indication |
| 3300µF Capacitor | Eliminates servo brownout resets |

---

# 💻 Software Stack

- Arduino IDE
- C++
- ESP RainMaker
- Wi-Fi
- Embedded C
- Finite State Machine (FSM)
- I2C Communication

---

# ⚙️ System Workflow

```
User Approaches Vault
        │
        ▼
Ultrasonic Sensor Detects User
        │
        ▼
Displays Welcome Screen
        │
        ▼
Select Authentication Mode
        │
 ┌──────┴─────────┐
 │                │
 ▼                ▼
Phone Mode     Keypad Mode
 │                │
 │           Enter PIN
 │                │
 ▼                ▼
RainMaker      Verify PIN
Command          │
 │                │
 └──────┬─────────┘
        ▼
 Unlock Vault
        │
        ▼
Auto Lock After Delay
```

Meanwhile, the vibration sensor continuously monitors the vault. If tampering is detected:

```
Tamper Detected
       │
       ▼
Alarm Activated
       │
       ├── Buzzer
       ├── Flashing LED
       ├── LCD Warning
       └── Push Notification
```

---

# 🏗 Hardware Architecture

```
                 Wi-Fi
                   │
           ESP RainMaker Cloud
                   │
                   │
                ESP32
     ┌────────┬────────┬───────────┬─────────┐
     │        │        │           │         │
 Keypad   LCD Display Servo   Ultrasonic  Vibration
                               Sensor      Sensor
     │        │        │           │         │
     └────────┴────────┴───────────┴─────────┘
                      │
                 Buzzer & LEDs
```

---

# 📂 Repository Structure

```
Secure-Vault/
│
├── code/
│   └── secure_vault.ino
│
├── images/
│   ├── front_view.jpg
│   ├── wiring.jpg
│   ├── rainmaker_app.jpg
│   └── demo.gif
│
├── circuit/
│   ├── schematic.png
│   └── block_diagram.png
│
├── report/
│   └── Summer_Training_Report.pdf
│
├── LICENSE
└── README.md
```

---

# 🚀 Installation

1. Clone the repository

```bash
git clone https://github.com/yourusername/secure-vault.git
```

2. Open the project in Arduino IDE.

3. Install required libraries:

- ESP32 Board Package
- ESP RainMaker
- Keypad
- LiquidCrystal_I2C
- ESP32Servo

4. Configure your Wi-Fi credentials.

5. Upload the code to the ESP32.

6. Provision the device using the ESP RainMaker mobile application.

---

# ▶️ Usage

1. Power on the vault.
2. Walk within 50 cm of the vault.
3. Select either:

   - **Phone Mode**
   - **Keypad Mode**

4. Authenticate.
5. Vault unlocks.
6. The servo locks automatically after operation.

If tampering occurs, the alarm activates immediately and the owner receives a push notification.

---

# 🧪 Testing

The prototype was successfully tested for:

- ✅ Correct PIN authentication
- ✅ Incorrect password handling
- ✅ Password lockout
- ✅ Phone-based unlocking
- ✅ Remote command restriction
- ✅ Tamper detection
- ✅ Alarm reset
- ✅ Servo reliability
- ✅ Brownout prevention
- ✅ Push notifications

---

# 📸 Gallery

| Hardware | Wiring | Mobile App |
|----------|--------|------------|
| Add image here | Add image here | Add image here |

---

# 🔮 Future Improvements

- Fingerprint authentication
- Face recognition
- RFID/NFC support
- Event logging
- Battery backup
- Camera integration
- AI-based intrusion detection
- Multi-user authentication
- Cloud activity history

---

# 📚 Documentation

A detailed report explaining the design, implementation, firmware architecture, hardware integration, testing, and future scope is available in the `report/` folder.

---

# 👨‍💻 Author

**Utkarsh Gupta**

B.Tech – Electronics & Communication Engineering

Embedded Systems | IoT | Hardware Design

---

# ⭐ Support

If you found this project helpful or interesting, consider giving it a ⭐ on GitHub!
