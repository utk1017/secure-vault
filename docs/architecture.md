# Firmware Architecture

## Overview

The firmware is a **non-blocking finite state machine (FSM)** — there is no `while(true)` anywhere in `loop()`. Every mode of the vault is a `SystemState`, and `loop()` steps through the current state once per pass (with a small `delay(50)` at the very end of the loop for debounce/pacing only). This means the reset button check, RainMaker command handling, the re-arm cooldown, and vibration monitoring are **all evaluated on every single pass**, regardless of which state/screen is currently active.

```
loop() each pass, in order:
  1. WiFi/Factory reset button hold-detection (non-blocking)
  2. handleRemoteCommands()        — RainMaker unlock/lock, gated by state
  3. Re-arm cooldown countdown
  4. Vibration sensor check + intruder alarm handling (highest priority)
  5. Lockout / timed-message states (early-return style, still non-blocking)
  6. Main state machine switch (ST_IDLE, ST_MENU, ST_PASS_ENTRY, etc.)
```

## States (`SystemState` enum)

| State | Meaning |
|---|---|
| `ST_IDLE` | Default state; ultrasonic sensor watches for an approaching user (or shows the lockout message if `locked == true`) |
| `ST_WELCOME` | Brief "Welcome User" message before the menu |
| `ST_MENU` | User picks `A` (Phone mode) or `B` (Keypad mode) |
| `ST_PHONE_WAIT` | Waiting for a RainMaker unlock command; `D` cancels back to idle |
| `ST_PHONE_UNLOCKED` | Vault unlocked via the app; `C` re-locks |
| `ST_PASS_ENTRY` | Collecting PIN digits; `*` clears, `#` submits, `D` cancels |
| `ST_PASS_UNLOCKED` | Vault unlocked via keypad; `C` re-locks |
| `ST_WRONG_PASSWORD_SHOW` | Timed message shown after a wrong PIN, before returning to idle or lockout |
| `ST_LOCKOUT_MSG` | Brief "Try Again After 30 Seconds" message before the countdown starts |
| `ST_LOCKOUT_COUNTDOWN` | Live countdown timer on the LCD; resets `attempts` and unlocks the keypad again at 0 |
| `ST_CANCEL_MSG` | Brief "Cancelled" message after pressing `D` |

## Key Controls

| Key | Action |
|---|---|
| `0–9` | Enter PIN digits |
| `*` | Clear the currently entered PIN |
| `#` | Submit PIN for verification |
| `A` | (Menu) select Phone/RainMaker mode |
| `B` | (Menu) select Keypad mode |
| `C` | Re-lock the vault (from either unlocked state) |
| `D` | Cancel out of a menu/mode; also silences an active intruder alarm |

## Mode-wise Flow

### 1. Idle / Detection (`ST_IDLE`)
If `locked` (the 3-strike lockout flag) is set, the vault jumps straight to the lockout message. Otherwise the ultrasonic sensor is polled; within ~50 cm of a user, the vault moves to `ST_WELCOME` → `ST_MENU`.

### 2. Menu Selection (`ST_MENU`)
- `A` → `ST_PHONE_WAIT`
- `B` → `ST_PASS_ENTRY`

### 3. Phone / RainMaker Mode (`ST_PHONE_WAIT` / `ST_PHONE_UNLOCKED`)
`handleRemoteCommands()` runs on **every** loop pass, but only actually unlocks the vault if `currentState == ST_PHONE_WAIT`. A remote unlock command received while idle, mid-PIN-entry, in lockout, etc. is simply dropped — this is the deliberate security boundary described in the report: the phone can only unlock the vault after the user has explicitly selected "A" on the keypad. Once unlocked, `C` locks it again from `ST_PHONE_UNLOCKED`; a remote lock command also works at any time the vault is currently unlocked.

### 4. Keypad Password Mode (`ST_PASS_ENTRY` / `ST_PASS_UNLOCKED`)
Digits accumulate up to 4 characters (each shown as `*` on the LCD). `*` clears the entry, `#` checks it against `password`. Correct → `unlockVault()` and `ST_PASS_UNLOCKED` (stays unlocked until `C` is pressed). Incorrect → `handleWrongPassword()`.

`handleWrongPassword()`:
- increments `attempts`, re-locks the servo (defensive — it should already be locked), notifies via RainMaker, beeps the buzzer for 800 ms, shows "Wrong Password" + attempts remaining, and moves to `ST_WRONG_PASSWORD_SHOW` for 3 seconds.
- After that timed message: if `attempts >= 3`, the vault sets `locked = true` and enters `ST_LOCKOUT_MSG` → `ST_LOCKOUT_COUNTDOWN` (30-second on-screen countdown). At 0, `attempts` resets and the keypad is usable again.

### 5. Intruder / Tamper Alarm (`inIntruderAlarm` flag, handled before the main switch)
This isn't a `SystemState` — it's a flag checked at the top of `loop()` so it can interrupt *any* state instantly. Vibration is only monitored while the vault is locked, not already alarming, and not in the post-silence cooldown.

**Detection logic:**
- The SW-420 idles LOW and drives HIGH on a hit.
- Each HIGH reading inside a rolling 1.5-second window (`VIBRATION_WINDOW_MS`) increments a hit counter.
- If the window elapses without reaching the threshold, the counter resets to 1 on the next hit.
- Once **11 hits** (`VIBRATION_HITS_NEEDED`) land inside that window, `inIntruderAlarm` is set: the LCD shows "INTRUDER!", the buzzer turns on continuously, the red LED blinks every 150 ms, the RainMaker "Intruder Alert" switch is updated, and a push notification is raised.
- Cleared by keypad `D` or a remote silence command (`Intruder Alert` switch turned off in the app) — both clear the buzzer/LED, reset the hit counter, and start a **3-second re-arm cooldown** (`ALARM_REARM_COOLDOWN_MS`) during which vibration monitoring is paused, so residual shaking right after silencing can't instantly re-trigger the alarm.

## RainMaker Integration

Three RainMaker devices are registered on the node `ESP32_SecureVault`:

| Device | Type | Purpose |
|---|---|---|
| `Vault Lock` | Switch | Power ON = unlock request, Power OFF = lock request (only honored per the Phone-Mode rule above) |
| `Intruder Alert` | Switch | Reflects/controls alarm state; turning it OFF from the app silences an active alarm |
| `Vault Notifications` | Custom device, `Last Event` text param (read-only) | Shows the most recent event string; also used with `esp_rmaker_raise_alert()` to push notifications |

Provisioning uses BLE (`WIFI_PROV_SCHEME_BLE`) with service name `MEE` and a hardcoded proof-of-possession `QWERTYUI` — **change the `pop` before any real deployment** and keep it out of version control (see `.gitignore` / `secrets.h` convention).

The BOOT button (GPIO 0) doubles as a Wi-Fi/factory reset: held 3–10s triggers `RMakerWiFiReset()`, held over 10s triggers `RMakerFactoryReset()`. This is checked with non-blocking hold-duration timing, not a blocking wait.

## Design Evolution

The report describes an earlier implementation using blocking `while(true)` loops per mode, where the vibration sensor and RainMaker commands couldn't be checked while the processor was inside another loop. This firmware is the final rewrite: a single `switch` over `SystemState` inside one continuously-running `loop()`, with the highest-priority checks (reset button, remote commands, vibration) always running first, ahead of whatever state-specific logic executes afterward.

## Power Stability Fix

Repeated brownout resets on servo actuation were traced to the servo's current spike causing a momentary voltage dip past the ESP32's brownout threshold. Fixed at the hardware level with a 3300µF/16V decoupling capacitor across the main power rail. The firmware additionally disables the ESP32's brownout detector register at boot as a secondary safeguard — this should be treated as a backstop, not a substitute for the capacitor.
