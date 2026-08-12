#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <ESP32Servo.h>

#include "RMaker.h"
#include "WiFi.h"
#include "WiFiProv.h"
#include <esp_rmaker_core.h>   // needed for esp_rmaker_raise_alert() - not exposed by the Arduino RMaker wrapper class
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ================================================================
//  SECURE VAULT + ESP RAINMAKER INTEGRATION  (NON-BLOCKING VERSION)
//  Rewritten as a state machine - no while(true) loops anywhere in
//  loop(). Every mode is a "state" that loop() steps through once
//  per pass, so vibration sensor + RainMaker commands are always
//  checked no matter what screen is showing.
// ================================================================

#define TRIG_PIN 19
#define ECHO_PIN 34
#define SERVO_PIN 5

#define GREEN_LED 13
#define RED_LED 12
#define BUZZER 18
#define VIBRATION_PIN 16

#define SDA_PIN 21
#define SCL_PIN 22

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo lockServo;

// ------------------ RAINMAKER CONFIG FLAGS ------------------//
#define ENABLE_WIFI_LED false   // set true only if you wire a spare GPIO for a status LED
static uint8_t wifiLedPin = 0;  // only used if ENABLE_WIFI_LED is true (free GPIO, no conflicts)

const char *service_name = "MEE";        // BLE provisioning service name shown in ESP RainMaker app
const char *pop          = "QWERTYUI";   // Proof of possession, change before deployment

char deviceName_Lock[]     = "Vault Lock";
char deviceName_Intruder[] = "Intruder Alert";

static uint8_t gpio_reset = 0;   // BOOT button on most ESP32 dev boards -> WiFi/Factory reset

// Switch() wants a uint8_t* GPIO reference. SERVO_PIN/VIBRATION_PIN are #define literals,
// so we mirror them into real variables here rather than taking the address of a macro.
static uint8_t lockGpioRef     = 2;
static uint8_t intruderGpioRef = 4;

static Switch my_lock(deviceName_Lock, &lockGpioRef);
static Switch my_intruder(deviceName_Intruder, &intruderGpioRef);

// ------------------ NOTIFICATIONS DEVICE ------------------//
char deviceName_Notify[] = "Vault Notifications";

static Device notifDevice(deviceName_Notify, "esp.device.other", NULL);
static Param  notifParam("Last Event", "esp.param.other", value((char *)"Vault Ready"), PROP_FLAG_READ);

volatile bool remoteUnlockRequested = false;
volatile bool remoteLockRequested   = false;
volatile bool remoteSilenceAlarm    = false;

//---------------- KEYPAD ----------------//

const byte ROWS = 4;
const byte COLS = 4;

char hexakeys[ROWS][COLS] =
{
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {23,17,32,33};
byte colPins[COLS] = {25,26,27,14};

Keypad keypad = Keypad(makeKeymap(hexakeys), rowPins, colPins, ROWS, COLS);

//---------------- VARIABLES ----------------//

String password = "1234";
String input = "";
int attempts = 0;
bool locked = false;
bool vaultUnlocked = false;

//---------------- STATE MACHINE ----------------//

enum SystemState {
  ST_IDLE,
  ST_WELCOME,
  ST_MENU,
  ST_PHONE_WAIT,
  ST_PHONE_UNLOCKED,
  ST_PASS_ENTRY,
  ST_PASS_UNLOCKED,
  ST_WRONG_PASSWORD_SHOW,
  ST_LOCKOUT_MSG,
  ST_LOCKOUT_COUNTDOWN,
  ST_CANCEL_MSG
};

SystemState currentState = ST_IDLE;
unsigned long stateTimer = 0;
int lockoutSecondsLeft = 30;

bool inIntruderAlarm = false;

bool resetButtonHeld = false;
unsigned long resetPressStart = 0;

// Vibration alarm - requires several trigger readings within a short window
// before declaring a real intrusion, so a single light touch doesn't set it off.
int vibrationHitCount = 0;
unsigned long vibrationWindowStart = 0;
const int VIBRATION_HITS_NEEDED = 11;
const unsigned long VIBRATION_WINDOW_MS = 1500;

// Cooldown after silencing an alarm - the sensor is ignored for this long right
// after 'D' (or app) silences it, so it can't instantly re-trigger while you're
// still standing right next to it / while any residual vibration settles.
const unsigned long ALARM_REARM_COOLDOWN_MS = 3000;
unsigned long alarmSilencedAt = 0;
bool inRearmCooldown = false;

// Rapid red LED blink while the intruder alarm is active.
const unsigned long RED_BLINK_INTERVAL_MS = 150;
unsigned long lastRedBlinkTime = 0;
bool redLedBlinkState = false;

//---------------- FORWARD DECLARATIONS ----------------//
void setState(SystemState newState);
void handleWrongPassword();
void handleRemoteCommands();
void unlockVault();
void lockVault();
void notifyEvent(const char *msg);
long distanceCM();
void beep(int time);

//---------------- BUZZER ----------------//

void beep(int time)
{
  digitalWrite(BUZZER, HIGH);
  delay(time);
  digitalWrite(BUZZER, LOW);
}

//---------------- ULTRASONIC ----------------//

long distanceCM()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0)
    return -1;

  return duration * 0.034 / 2;
}

//---------------- SERVO FUNCTIONS ----------------//

void unlockVault()
{
  lockServo.write(90);   // unlock position

  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);

  beep(200);

  lcd.clear();
  lcd.print("Access Granted");

  vaultUnlocked = true;
  my_lock.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, vaultUnlocked);

  notifyEvent("Vault Unlocked");
}

void lockVault()
{
  bool wasUnlocked = vaultUnlocked;

  lockServo.write(60);   // locked position

  digitalWrite(GREEN_LED, LOW);

  lcd.clear();
  lcd.print("Vault Locked");

  vaultUnlocked = false;
  my_lock.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, vaultUnlocked);

  if (wasUnlocked)
  {
    notifyEvent("Vault Locked");
  }
}

//---------------- NOTIFICATIONS ----------------//

void notifyEvent(const char *msg)
{
  esp_rmaker_raise_alert(msg);
  notifDevice.updateAndReportParam("Last Event", msg);
}

//---------------- RAINMAKER CALLBACKS ----------------//

void write_callback(Device *device, Param *param, const param_val_t val, void *priv_data, write_ctx_t *ctx)
{
  const char *device_name = device->getDeviceName();
  const char *param_name  = param->getParamName();

  if (strcmp(param_name, "Power") != 0) return;

  bool newState = val.val.b;

  if (strcmp(device_name, deviceName_Lock) == 0)
  {
    if (newState) {
      remoteUnlockRequested = true;
    } else {
      remoteLockRequested = true;
    }
    Serial.printf("RainMaker command for %s: requested state = %d\n", device_name, newState);
  }
  else if (strcmp(device_name, deviceName_Intruder) == 0)
  {
    if (!newState) {
      remoteSilenceAlarm = true;
    }
    my_intruder.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, newState);
  }
}

void sysProvEvent(arduino_event_t *sys_event)
{
  switch (sys_event->event_id)
  {
    case ARDUINO_EVENT_PROV_START:
      Serial.printf("Provisioning started: %s\n", service_name);
      printQR(service_name, pop, "ble");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("Connected to Wi-Fi");
      if (ENABLE_WIFI_LED) digitalWrite(wifiLedPin, true);
      break;
  }
}

void handleRemoteCommands()
{
  if (remoteUnlockRequested)
  {
    remoteUnlockRequested = false;

    // Only honor a phone/app unlock command if the user has actually selected
    // option A (Phone mode) from the menu and is currently waiting in that
    // state. In any other state (idle, password entry, etc.) the command is
    // simply ignored - phone unlock only works after choosing "A".
    if (currentState == ST_PHONE_WAIT && !vaultUnlocked)
    {
      unlockVault();
    }
  }

  if (remoteLockRequested)
  {
    remoteLockRequested = false;

    if (vaultUnlocked)
    {
      lockVault();
    }
  }
}

//---------------- STATE HELPERS ----------------//

void setState(SystemState newState)
{
  currentState = newState;
  stateTimer = millis();

  switch (newState)
  {
    case ST_WELCOME:
      lcd.clear();
      lcd.print("Welcome User");
      break;

    case ST_MENU:
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("A: PHONE");
      lcd.setCursor(0,1);
      lcd.print("B: KEY");
      break;

    case ST_PHONE_WAIT:
      lcd.clear();
      lcd.print("RainMaker Mode");
      lcd.setCursor(0,1);
      lcd.print("Waiting App...");
      remoteUnlockRequested = false;
      break;

    case ST_PHONE_UNLOCKED:
      lcd.clear();
      lcd.print("Phone Access");
      lcd.setCursor(0,1);
      lcd.print("Press C Lock");
      break;

    case ST_PASS_ENTRY:
      input = "";
      lcd.clear();
      lcd.print("Password:");
      break;

    case ST_PASS_UNLOCKED:
      lcd.clear();
      lcd.print("Vault Open");
      lcd.setCursor(0,1);
      lcd.print("Press C Lock");
      break;

    case ST_LOCKOUT_MSG:
      lcd.clear();
      lcd.print("Try Again After");
      lcd.setCursor(0,1);
      lcd.print("30 Seconds");
      break;

    case ST_LOCKOUT_COUNTDOWN:
      lockoutSecondsLeft = 30;
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Try Again After");
      lcd.setCursor(0,1);
      lcd.print(lockoutSecondsLeft);
      lcd.print(" Seconds");
      break;

    case ST_CANCEL_MSG:
      lcd.clear();
      lcd.print("Cancelled");
      break;

    case ST_IDLE:
      lcd.clear();
      lcd.print("Ready");
      break;

    default:
      break;
  }
}

void handleWrongPassword()
{
  attempts++;
  lockVault();
  notifyEvent("Wrong Password Attempt");

  digitalWrite(RED_LED, HIGH);
  beep(800);
  digitalWrite(RED_LED, LOW);

  lcd.clear();
  lcd.print("Wrong Password");
  lcd.setCursor(0,1);

  int left = 3 - attempts;
  if (left > 0) {
    lcd.print("Attempts Left:");
    lcd.print(left);
  } else {
    lcd.print("No Attempts!");
  }

  currentState = ST_WRONG_PASSWORD_SHOW;
  stateTimer = millis();
}

//---------------- SETUP ----------------//

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disables brownout detector
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);

  lcd.init();
  lcd.backlight();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  pinMode(BUZZER, OUTPUT);

  // NOTE: this sensor idles LOW and actively drives HIGH when triggered, so it
  // drives both states itself - no internal pull needed. INPUT_PULLUP was
  // fighting the sensor's idle-LOW output and forcing the pin to read HIGH
  // (looked like "INTRUDER" as soon as power was applied).
  pinMode(VIBRATION_PIN, INPUT);

  pinMode(gpio_reset, INPUT);
  if (ENABLE_WIFI_LED) pinMode(wifiLedPin, OUTPUT);

  lockServo.attach(SERVO_PIN);

  lockServo.write(60);
  vaultUnlocked = false;

  lcd.clear();
  lcd.print("Smart Vault");

  delay(2000);

  lcd.clear();
  lcd.print("Starting Cloud");
  lcd.setCursor(0,1);
  lcd.print("Connection...");

  Node my_node = RMaker.initNode("ESP32_SecureVault");

  my_lock.addCb(write_callback);
  my_intruder.addCb(write_callback);

  notifParam.addUIType(ESP_RMAKER_UI_TEXT);
  notifDevice.addParam(notifParam);

  my_node.addDevice(my_lock);
  my_node.addDevice(my_intruder);
  my_node.addDevice(notifDevice);

  RMaker.enableOTA(OTA_USING_PARAMS);
  RMaker.enableTZService();
  RMaker.enableSchedule();

  RMaker.start();

  WiFi.onEvent(sysProvEvent);
  WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_FREE_BTDM, WIFI_PROV_SECURITY_1, pop, service_name);

  my_lock.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, vaultUnlocked);
  my_intruder.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, false);
  notifDevice.updateAndReportParam("Last Event", "Vault Ready");

  Serial.println("Setup completed - Vault ready, RainMaker started.");

  setState(ST_IDLE);
}

//---------------- LOOP ----------------//

void loop()
{
  unsigned long now = millis();

  //----------- WIFI / FACTORY RESET BUTTON (non-blocking) ------------//
  if (digitalRead(gpio_reset) == LOW)
  {
    if (!resetButtonHeld) {
      resetButtonHeld = true;
      resetPressStart = now;
    }
  }
  else
  {
    if (resetButtonHeld) {
      resetButtonHeld = false;
      unsigned long duration = now - resetPressStart;

      if (duration > 10000) {
        Serial.println("Factory reset triggered.");
        RMakerFactoryReset(2);
      } else if (duration > 3000) {
        Serial.println("WiFi reset triggered.");
        RMakerWiFiReset(2);
      }
    }
  }

  if (ENABLE_WIFI_LED) digitalWrite(wifiLedPin, WiFi.status() == WL_CONNECTED);

  //----------- REMOTE COMMANDS FROM APP (checked every pass) ------------//
  handleRemoteCommands();

  //----------- REARM COOLDOWN (ignore sensor briefly after silencing) ------------//
  if (inRearmCooldown)
  {
    if (now - alarmSilencedAt >= ALARM_REARM_COOLDOWN_MS) {
      inRearmCooldown = false;
      vibrationHitCount = 0;
    }
  }

  //----------- VIBRATION ALARM (highest priority, checked every pass) ------------//
  // Only arms when the vault is actually locked, not during the post-silence
  // cooldown, and requires several trigger readings within a short window
  // before declaring a real intrusion, so a single light tap doesn't set it off.
  int vibrationValue = digitalRead(VIBRATION_PIN);

  if (!inIntruderAlarm && !vaultUnlocked && !inRearmCooldown)
  {
    if (vibrationValue == HIGH)   // this sensor idles LOW, triggers HIGH
    {
      if (vibrationHitCount == 0) {
        vibrationWindowStart = now;
      }
      vibrationHitCount++;

      if (now - vibrationWindowStart > VIBRATION_WINDOW_MS) {
        vibrationHitCount = 1;
        vibrationWindowStart = now;
      }

      if (vibrationHitCount >= VIBRATION_HITS_NEEDED)
      {
        inIntruderAlarm = true;
        vibrationHitCount = 0;
        lastRedBlinkTime = now;
        redLedBlinkState = false;
        lcd.clear();
        lcd.print("INTRUDER!");
        remoteSilenceAlarm = false;
        my_intruder.updateAndReportParam(ESP_RMAKER_DEF_POWER_NAME, true);
        notifyEvent("Intruder Alert Triggered!");
      }
    }
  }
  else if (vaultUnlocked)
  {
    vibrationHitCount = 0;
  }

  if (inIntruderAlarm)
  {
    digitalWrite(BUZZER, HIGH);

    if (now - lastRedBlinkTime >= RED_BLINK_INTERVAL_MS)
    {
      lastRedBlinkTime = now;
      redLedBlinkState = !redLedBlinkState;
      digitalWrite(RED_LED, redLedBlinkState);
    }

    char k = keypad.getKey();
    if (k == 'D' || remoteSilenceAlarm)
    {
      digitalWrite(BUZZER, LOW);
      digitalWrite(RED_LED, LOW);
      redLedBlinkState = false;
      inIntruderAlarm = false;
      remoteSilenceAlarm = false;
      vibrationHitCount = 0;

      // Start a cooldown so the sensor can't instantly re-trigger the moment
      // it's silenced (e.g. while you're still standing right next to it).
      inRearmCooldown = true;
      alarmSilencedAt = now;

      setState(ST_IDLE);
    }

    delay(50);
    return;
  }

  //----------- LOCKOUT AFTER 3 WRONG ATTEMPTS (non-blocking) ------------//
  if (currentState == ST_LOCKOUT_MSG)
  {
    if (now - stateTimer >= 2000) {
      setState(ST_LOCKOUT_COUNTDOWN);
    }
    delay(50);
    return;
  }

  if (currentState == ST_LOCKOUT_COUNTDOWN)
  {
    if (now - stateTimer >= 1000)
    {
      stateTimer = now;
      lockoutSecondsLeft--;

      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Try Again After");
      lcd.setCursor(0,1);
      lcd.print(lockoutSecondsLeft);
      lcd.print(" Seconds");

      if (lockoutSecondsLeft <= 0) {
        attempts = 0;
        locked = false;
        setState(ST_IDLE);
      }
    }
    delay(50);
    return;
  }

  //----------- TEMPORARY MESSAGE STATES (non-blocking timed screens) ------------//
  if (currentState == ST_WRONG_PASSWORD_SHOW)
  {
    if (now - stateTimer >= 3000)
    {
      if (attempts >= 3) {
        locked = true;
        setState(ST_LOCKOUT_MSG);
      } else {
        setState(ST_IDLE);
      }
    }
    delay(50);
    return;
  }

  if (currentState == ST_CANCEL_MSG)
  {
    if (now - stateTimer >= 1000)
    {
      setState(ST_IDLE);
    }
    delay(50);
    return;
  }

  //----------- MAIN STATE MACHINE ------------//
  switch (currentState)
  {
    case ST_IDLE:
    {
      if (locked) {
        setState(ST_LOCKOUT_MSG);
        break;
      }

      long distance = distanceCM();
      Serial.print("Distance: ");
      Serial.println(distance);

      if (distance > 0 && distance <= 50) {
        setState(ST_WELCOME);
      }
      break;
    }

    case ST_WELCOME:
      if (now - stateTimer >= 1500) {
        setState(ST_MENU);
      }
      break;

    case ST_MENU:
    {
      char choice = keypad.getKey();
      if (choice == 'A') {
        setState(ST_PHONE_WAIT);
      } else if (choice == 'B') {
        setState(ST_PASS_ENTRY);
      }
      break;
    }

    case ST_PHONE_WAIT:
    {
      if (vaultUnlocked) {
        setState(ST_PHONE_UNLOCKED);
        break;
      }

      char k = keypad.getKey();
      if (k == 'D') {
        setState(ST_CANCEL_MSG);
      }
      break;
    }

    case ST_PHONE_UNLOCKED:
    {
      if (!vaultUnlocked) {
        setState(ST_IDLE);
        break;
      }

      char k = keypad.getKey();
      if (k == 'C') {
        lockVault();
        setState(ST_IDLE);
      }
      break;
    }

    case ST_PASS_ENTRY:
    {
      char key = keypad.getKey();
      if (key == NO_KEY) break;

      if (key >= '0' && key <= '9')
      {
        if (input.length() < 4) {
          input += key;
          lcd.setCursor(input.length() - 1, 1);
          lcd.print("*");
        }
      }
      else if (key == '*')
      {
        input = "";
        lcd.setCursor(0,1);
        lcd.print("                ");
        lcd.setCursor(0,1);
      }
      else if (key == '#')
      {
        if (input == password) {
          unlockVault();
          setState(ST_PASS_UNLOCKED);
        } else {
          handleWrongPassword();
        }
      }
      else if (key == 'D')
      {
        setState(ST_CANCEL_MSG);
      }
      break;
    }

    case ST_PASS_UNLOCKED:
    {
      char k = keypad.getKey();
      if (k == 'C') {
        lockVault();
        setState(ST_IDLE);
      }
      break;
    }

    default:
      setState(ST_IDLE);
      break;
  }

  delay(50);
}
