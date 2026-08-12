# Functional Testing

The completed prototype was tested across keypad authentication, wrong-password lockout, ESP RainMaker phone-based access, and vibration-based tamper detection, plus hardware stability under repeated lock/unlock cycles.

| Test Scenario | Observation |
|---|---|
| Correct PIN entry (`#` to submit) | Vault unlocks, LCD shows "Access Granted", green LED on, buzzer chirps 200 ms, RainMaker "Last Event" updates and a push notification is sent |
| Incorrect PIN entry | Buzzer sounds (800 ms), LCD shows "Wrong Password" and attempts remaining; after 3 wrong attempts the vault sets `locked = true` and enters a 30-second lockout countdown |
| Clearing PIN entry with `*` | Entered digits cleared, LCD prompt resets without submitting |
| RainMaker unlock — while in `ST_PHONE_WAIT` | Vault unlocks immediately on receiving the app command |
| RainMaker unlock — any other state | Command correctly ignored/dropped |
| Manual re-lock with `C` | Works from both `ST_PASS_UNLOCKED` and `ST_PHONE_UNLOCKED`; vault does not auto re-lock on a timer |
| Vibration below threshold (isolated bump) | Hit counter doesn't reach 11 within the 1.5s window; resets, no alarm |
| Sustained hard tampering | 11+ HIGH readings land inside the 1.5s window; alarm triggers — buzzer on, red LED blinking every 150 ms, LCD shows "INTRUDER!", push notification sent |
| Alarm silencing (keypad `D` / app "Intruder Alert" OFF) | Buzzer and red LED turn off immediately; 3-second re-arm cooldown prevents an instant false re-trigger |
| Repeated unlock/lock cycling (pre-capacitor) | Intermittent brownout resets — LCD reinitializing, servo reverting to locked position, buzzer stuck on |
| Repeated unlock/lock cycling (post-capacitor) | No brownout resets; servo, buzzer, and LCD operate reliably across repeated cycles |
| BOOT button held 3–10s | Wi-Fi reset triggered (`RMakerWiFiReset`) |
| BOOT button held >10s | Factory reset triggered (`RMakerFactoryReset`) |

All modules — keypad authentication, menu navigation, remote cloud control, vibration-based intrusion detection, LCD status, servo operation, buzzer/LED indication, and push notifications — functioned correctly under both normal operation and simulated intrusion scenarios.
