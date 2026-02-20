#include <QMI8658.h>
#include <FastLED.h>
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_bt.h"
#include "driver/rtc_io.h"

// ============================================
// CONFIGURATION
// ============================================
#define BATTERY_PIN 5
#define BATTERY_READ_INTERVAL 15000
#define POLL_INTERVAL_ACTIVE 12
#define POLL_INTERVAL_IDLE 88
#define IDLE_TIMEOUT 6000
#define IDLE_DEEP_SLEEP_TIMEOUT 30000

#define BATTERY_FULL 4.2f
#define BATTERY_NOMINAL 3.7f
#define BATTERY_LOW 3.4f
#define BATTERY_CRITICAL 3.2f
#define BATTERY_NOT_PRESENT 1.0f

#define ADC_RESOLUTION 4095.0f
#define ADC_REF_VOLTAGE 3.3f
#define VOLTAGE_DIVIDER_RATIO 2.0f

#define BRIGHTNESS_LOW_POWER 1
#define BRIGHTNESS_NORMAL 3
#define BRIGHTNESS_DIM 1
#define BRIGHTNESS_ANIMATION 20
#define BRIGHTNESS_BOOTUP 40
#define BRIGHTNESS_SHAKE 20

#define DEEP_SLEEP_BATTERY 30
#define DEEP_SLEEP_IDLE 10
#define uS_TO_S_FACTOR 1000000ULL

#define LED_PIN 14
#define NUMPIXELS 64
#define BUTTON_PIN 2
#define I2C_SDA 11
#define I2C_SCL 12

// ============================================
// SHAKE DETECTION
// ============================================
#define SHAKE_THRESHOLD 4500.0f
#define SHAKE_GYRO_THRESHOLD 250.0f
#define SHAKE_MIN_DURATION 350
#define SHAKE_END_DELAY 500
#define SHAKE_PASCH_TIME 2000
#define SHAKE_GAG_TIME 10000
#define SHAKE_COOLDOWN 800
#define ORIENTATION_THRESHOLD 400.0f

#define DEFAULT_DISPLAY_INTERVAL 1000
#define DEFAULT_ANIMATION 11

#define DICE1_COLOR 0xFF0000
#define DICE2_COLOR 0x0000FF

// ============================================
// CPU FREQUENZEN
// ============================================
#define CPU_FREQ_IDLE 10
#define CPU_FREQ_ACTIVE 40

// ============================================
// GLOBALE VARIABLEN
// ============================================
CRGB leds[NUMPIXELS];
QMI8658 imu;

uint8_t dice1Result = 1, dice2Result = 1;
bool isDisplayingResult = false;
uint8_t currentDisplayDice = 1;
uint8_t displayPhase = 0;
unsigned long lastDisplaySwitch = 0;
unsigned long displayInterval = DEFAULT_DISPLAY_INTERVAL;

float batteryVoltage = 4.2f;
uint8_t batteryPercent = 100;
unsigned long lastBatteryRead = 0;

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR uint8_t sleepReason = 0;
RTC_DATA_ATTR float sleepAccelX = 0, sleepAccelY = 0, sleepAccelZ = 0;

uint8_t currentAnimation = DEFAULT_ANIMATION;
const char* animationNames[] = {
  "Bounce", "Spiral", "Scatter", "Spin", "Plasma",
  "Explode", "Glitch", "Wave", "Firework", "Matrix", "Pulse", "Random"
};

enum ShakeState { SHAKE_IDLE, SHAKE_DETECTING, SHAKE_ACTIVE, SHAKE_ROLLING };
ShakeState shakeState = SHAKE_IDLE;
unsigned long shakeStartTime = 0, lastShakeTime = 0, lastRollTime = 0;
unsigned long lastActivityTime = 0, shakeAnimTime = 0;
unsigned long shakeActiveTime = 0;

float lastAccelX = 0, lastAccelY = 0, lastAccelZ = 0;
float lastDelta = 0, lastGyro = 0;
bool isDimmed = false;
uint8_t currentBrightness = BRIGHTNESS_NORMAL;
bool waitingForAnimNumber = false;
bool imuOK = false;
uint8_t imuErrorCount = 0;
bool gagModeActive = false;

// ============================================
// STATISTIKEN
// ============================================
struct {
  uint32_t totalRolls = 0;
  uint32_t numberCounts[7] = { 0 };
  uint32_t sumCounts[13] = { 0 };
  uint32_t animationCounts[12] = { 0 };
  unsigned long totalShakeTime = 0;
  unsigned long longestShake = 0;
  unsigned long shortestShake = ULONG_MAX;
  uint32_t shakeAttempts = 0;
} stats;

// ============================================
// 3x5 FONT FÜR LAUFTEXT
// ============================================
const uint8_t font3x5[][3] PROGMEM = {
  {0x00, 0x00, 0x00}, // Space (0)
  {0x1F, 0x05, 0x1F}, // A (1)
  {0x1F, 0x15, 0x0A}, // B
  {0x0E, 0x11, 0x11}, // C
  {0x1F, 0x11, 0x0E}, // D
  {0x1F, 0x15, 0x11}, // E
  {0x1F, 0x05, 0x01}, // F
  {0x0E, 0x11, 0x1D}, // G
  {0x1F, 0x04, 0x1F}, // H
  {0x11, 0x1F, 0x11}, // I
  {0x08, 0x10, 0x0F}, // J
  {0x1F, 0x04, 0x1B}, // K
  {0x1F, 0x10, 0x10}, // L
  {0x1F, 0x02, 0x1F}, // M
  {0x1F, 0x01, 0x1E}, // N
  {0x0E, 0x11, 0x0E}, // O
  {0x1F, 0x05, 0x02}, // P
  {0x0E, 0x19, 0x1E}, // Q
  {0x1F, 0x05, 0x1A}, // R
  {0x12, 0x15, 0x09}, // S
  {0x01, 0x1F, 0x01}, // T
  {0x0F, 0x10, 0x0F}, // U
  {0x07, 0x18, 0x07}, // V
  {0x1F, 0x08, 0x1F}, // W
  {0x1B, 0x04, 0x1B}, // X
  {0x03, 0x1C, 0x03}, // Y
  {0x19, 0x15, 0x13}, // Z (26)
  {0x00, 0x04, 0x00}, // - (27)
  {0x0E, 0x11, 0x0E}, // 0 (28)
  {0x12, 0x1F, 0x10}, // 1
  {0x19, 0x15, 0x12}, // 2
  {0x11, 0x15, 0x0A}, // 3
  {0x07, 0x04, 0x1F}, // 4
  {0x17, 0x15, 0x09}, // 5
  {0x0E, 0x15, 0x08}, // 6
  {0x01, 0x1D, 0x03}, // 7
  {0x0A, 0x15, 0x0A}, // 8
  {0x02, 0x15, 0x0E}, // 9 (37)
};

uint8_t getFontIndex(char c) {
  if (c == ' ') return 0;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
  if (c >= 'a' && c <= 'z') return c - 'a' + 1;
  if (c == '-') return 27;
  if (c >= '0' && c <= '9') return c - '0' + 28;
  return 0;
}

// ============================================
// DEBUG
// ============================================
#define dbg(x) Serial.print(x)
#define dbgln(x) Serial.println(x)
#define dbgf(x, y) Serial.print(x, y)

// ============================================
// SERIAL OUTPUT HELPERS
// ============================================
void printLine(char c = '-', uint8_t len = 50) {
  for (uint8_t i = 0; i < len; i++) Serial.print(c);
  Serial.println();
}

void printCentered(const char* text, uint8_t width = 50) {
  uint8_t len = strlen(text);
  uint8_t pad = (width - len) / 2;
  for (uint8_t i = 0; i < pad; i++) Serial.print(' ');
  Serial.println(text);
}

void printKeyValue(const char* key, int value, const char* unit = "") {
  Serial.print("  ");
  Serial.print(key);
  Serial.print(": ");
  Serial.print(value);
  Serial.println(unit);
}

void printKeyValueFloat(const char* key, float value, uint8_t decimals = 2, const char* unit = "") {
  Serial.print("  ");
  Serial.print(key);
  Serial.print(": ");
  Serial.print(value, decimals);
  Serial.println(unit);
}

void printBar(uint32_t value, uint32_t maxValue, uint8_t width = 20) {
  uint8_t filled = maxValue > 0 ? (value * width / maxValue) : 0;
  Serial.print("[");
  for (uint8_t i = 0; i < width; i++) {
    Serial.print(i < filled ? '#' : ' ');
  }
  Serial.print("] ");
}

// ============================================
// CPU FREQUENZ
// ============================================
void cpuFast() {
  if (getCpuFrequencyMhz() != CPU_FREQ_ACTIVE) {
    setCpuFrequencyMhz(CPU_FREQ_ACTIVE);
  }
}

void cpuSlow() {
  if (getCpuFrequencyMhz() != CPU_FREQ_IDLE) {
    setCpuFrequencyMhz(CPU_FREQ_IDLE);
  }
}

// ============================================
// POWER MANAGEMENT
// ============================================
void disableRadios() {
  esp_wifi_stop();
  esp_bt_controller_disable();
}

bool isUSBPower() {
  return batteryVoltage > BATTERY_FULL || batteryVoltage < BATTERY_NOT_PRESENT;
}

// ============================================
// LED FUNCTIONS
// ============================================
void ledShowSafe() {
  bool wasSlowCpu = (getCpuFrequencyMhz() < CPU_FREQ_ACTIVE);
  if (wasSlowCpu) cpuFast();
  FastLED.show();
  if (wasSlowCpu) cpuSlow();
}

inline void ledSetBrightness(uint8_t b) {
  FastLED.setBrightness(b);
}

inline void ledClear() {
  FastLED.clear();
}

void ledSetPixel(uint8_t i, uint32_t c) {
  if (i < NUMPIXELS) leds[i] = CRGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

uint32_t ledColor(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void ledFill(uint32_t c) {
  for (int i = 0; i < NUMPIXELS; i++) ledSetPixel(i, c);
}

void setPixel(uint8_t x, uint8_t y, uint32_t c) {
  if (x < 8 && y < 8) ledSetPixel(y * 8 + x, c);
}

void ledOff() {
  ledClear();
  ledShowSafe();
}

// ============================================
// COLORS
// ============================================
uint32_t rainbow(uint8_t pos) {
  pos = 255 - pos;
  if (pos < 85) return ledColor(255 - pos * 3, 0, pos * 3);
  if (pos < 170) {
    pos -= 85;
    return ledColor(0, pos * 3, 255 - pos * 3);
  }
  pos -= 170;
  return ledColor(pos * 3, 255 - pos * 3, 0);
}

uint32_t randColor() {
  static const uint32_t c[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x00FFFF, 0xFF00FF, 0xFF8000, 0xFFFFFF };
  return c[random(8)];
}

uint32_t hsv(uint8_t h, uint8_t s, uint8_t v) {
  uint8_t region = h / 43, rem = (h - region * 43) * 6;
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * rem) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
  switch (region) {
    case 0: return ledColor(v, t, p);
    case 1: return ledColor(q, v, p);
    case 2: return ledColor(p, v, t);
    case 3: return ledColor(p, q, v);
    case 4: return ledColor(t, p, v);
    default: return ledColor(v, p, q);
  }
}

// ============================================
// DISPLAY - WÜRFELPUNKTE
// ============================================
// 2x2 Pixel Punkt
void dot(uint8_t x, uint8_t y, uint32_t c) {
  setPixel(x, y, c);
  setPixel(x + 1, y, c);
  setPixel(x, y + 1, c);
  setPixel(x + 1, y + 1, c);
}

// Normale Würfelzahlen 1-6
void drawDieRaw(uint8_t v, uint32_t c) {
  ledClear();
  switch (v) {
    case 1: dot(3, 3, c); break;
    case 2:
      dot(1, 1, c);
      dot(5, 5, c);
      break;
    case 3:
      dot(1, 1, c);
      dot(3, 3, c);
      dot(5, 5, c);
      break;
    case 4:
      dot(1, 1, c);
      dot(5, 1, c);
      dot(1, 5, c);
      dot(5, 5, c);
      break;
    case 5:
      dot(1, 1, c);
      dot(5, 1, c);
      dot(3, 3, c);
      dot(1, 5, c);
      dot(5, 5, c);
      break;
    case 6:
      dot(1, 0, c);
      dot(1, 3, c);
      dot(1, 6, c);
      dot(5, 0, c);
      dot(5, 3, c);
      dot(5, 6, c);
      break;
  }
}

void drawDie(uint8_t v, uint32_t c) {
  drawDieRaw(v, c);
  ledShowSafe();
}

// ============================================
// GAG-MODUS: 9 PUNKTE (3x3 Raster)
// ============================================
// 9 Punkte als 3x3 Raster aus 2x2 Pixel Quadraten
// Layout auf 8x8:
//   ##.##.##
//   ##.##.##
//   ........
//   ##.##.##
//   ##.##.##
//   ........
//   ##.##.##
//   ##.##.##
void drawNineRaw(uint32_t c) {
  ledClear();
  // Obere Reihe (y=0)
  dot(0, 0, c);  // Links oben
  dot(3, 0, c);  // Mitte oben
  dot(6, 0, c);  // Rechts oben
  // Mittlere Reihe (y=3)
  dot(0, 3, c);  // Links mitte
  dot(3, 3, c);  // Mitte mitte
  dot(6, 3, c);  // Rechts mitte
  // Untere Reihe (y=6)
  dot(0, 6, c);  // Links unten
  dot(3, 6, c);  // Mitte unten
  dot(6, 6, c);  // Rechts unten
}

void drawNine(uint32_t c) {
  drawNineRaw(c);
  ledShowSafe();
}

// ============================================
// BATTERIE-ANZEIGE
// ============================================
void drawBatt(uint8_t lvl, uint32_t ol, uint32_t fl) {
  ledClear();
  for (int x = 0; x < 6; x++) {
    setPixel(x, 1, ol);
    setPixel(x, 6, ol);
  }
  for (int y = 1; y <= 6; y++) {
    setPixel(0, y, ol);
    setPixel(5, y, ol);
  }
  for (int y = 2; y <= 5; y++) setPixel(6, y, ol);
  setPixel(7, 3, ol);
  setPixel(7, 4, ol);
  for (int i = 0; i < lvl && i < 4; i++)
    for (int y = 2; y <= 5; y++) setPixel(1 + i, y, fl);
  ledShowSafe();
}

void drawBattLow() {
  ledClear();
  uint32_t orange = 0x446600;
  uint32_t darkOrange = 0x182200;
  
  for (int x = 0; x < 6; x++) {
    setPixel(x, 1, orange);
    setPixel(x, 6, orange);
  }
  for (int y = 1; y <= 6; y++) {
    setPixel(0, y, orange);
    setPixel(5, y, orange);
  }
  for (int y = 2; y <= 5; y++) setPixel(6, y, orange);
  setPixel(7, 3, orange);
  setPixel(7, 4, orange);
  
  for (int y = 2; y <= 5; y++) {
    setPixel(1, y, darkOrange);
    setPixel(2, y, darkOrange);
  }
  
  ledShowSafe();
}

// ============================================
// LAUFTEXT ANIMATION
// ============================================
void scrollText(const char* text, uint32_t color, uint16_t speedMs = 50) {
  int textLen = strlen(text);
  int totalWidth = textLen * 4;
  
  for (int offset = 8; offset > -totalWidth; offset--) {
    ledClear();
    
    for (int i = 0; i < textLen; i++) {
      int charX = offset + i * 4;
      if (charX > -4 && charX < 8) {
        uint8_t fontIdx = getFontIndex(text[i]);
        for (int col = 0; col < 3; col++) {
          int screenX = charX + col;
          if (screenX >= 0 && screenX < 8) {
            uint8_t columnData = pgm_read_byte(&font3x5[fontIdx][col]);
            for (int row = 0; row < 5; row++) {
              if (columnData & (1 << row)) {
                setPixel(screenX, row + 1, color);
              }
            }
          }
        }
      }
    }
    
    ledShowSafe();
    delay(speedMs);
  }
}

// ============================================
// ACTIVITY
// ============================================
void registerActivity() {
  lastActivityTime = millis();
  if (isDimmed) {
    isDimmed = false;
    ledSetBrightness(currentBrightness);
    dbgln("💡 Display aufgeweckt");
    if (isDisplayingResult) {
      if (gagModeActive) {
        drawNine(currentDisplayDice == 1 ? DICE1_COLOR : DICE2_COLOR);
      } else {
        drawDie(currentDisplayDice == 1 ? dice1Result : dice2Result,
                currentDisplayDice == 1 ? DICE1_COLOR : DICE2_COLOR);
      }
    }
  }
}

// ============================================
// SHAKE ANIMATION
// ============================================
void updateShakeAnim() {
  ledSetBrightness(BRIGHTNESS_SHAKE);
  unsigned long t = millis() - shakeAnimTime;
  drawDieRaw((t / 70) % 6 + 1, rainbow((t / 4) % 256));
  setPixel(random(8), random(8), 0xFFFFFF);
  setPixel(random(8), random(8), 0xFFFFFF);
  ledShowSafe();
}

// ============================================
// BATTERY
// ============================================
float readBattQuick() {
  return (analogRead(BATTERY_PIN) / ADC_RESOLUTION) * ADC_REF_VOLTAGE * VOLTAGE_DIVIDER_RATIO;
}

float readBatt() {
  uint32_t sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(BATTERY_PIN);
    delay(2);
  }
  return (sum / 10.0f / ADC_RESOLUTION) * ADC_REF_VOLTAGE * VOLTAGE_DIVIDER_RATIO;
}

uint8_t battPct(float v) {
  if (v < BATTERY_NOT_PRESENT) return 100;
  if (v >= BATTERY_FULL) return 100;
  if (v <= BATTERY_CRITICAL) return 0;
  return constrain((uint8_t)((v - BATTERY_CRITICAL) / (BATTERY_FULL - BATTERY_CRITICAL) * 100), 0, 100);
}

void updateBatt() {
  if (millis() - lastBatteryRead < BATTERY_READ_INTERVAL) return;
  lastBatteryRead = millis();
  batteryVoltage = readBatt();
  batteryPercent = battPct(batteryVoltage);
}

// ============================================
// IMU
// ============================================
bool initIMU() {
  dbg("IMU initialisieren... ");

  if (!imu.begin(I2C_SDA, I2C_SCL)) {
    dbgln("FEHLER!");
    return false;
  }

  Wire.setClock(100000);
  delay(50);

  imu.setAccelRange(QMI8658_ACCEL_RANGE_4G);
  imu.setAccelODR(QMI8658_ACCEL_ODR_125HZ);
  imu.setAccelUnit_mg(true);
  imu.setGyroRange(QMI8658_GYRO_RANGE_256DPS);
  imu.setGyroODR(QMI8658_GYRO_ODR_125HZ);
  imu.setGyroUnit_dps(true);
  imu.enableSensors(QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO);

  delay(100);

  QMI8658_Data d;
  for (int retry = 0; retry < 5; retry++) {
    if (imu.readSensorData(d)) {
      lastAccelX = d.accelX;
      lastAccelY = d.accelY;
      lastAccelZ = d.accelZ;
      dbgln("OK");
      return true;
    }
    delay(20);
  }

  dbgln("LESEFEHLER!");
  return false;
}

void disableIMU() {
  Wire.end();
}

void saveOrientation() {
  if (imuOK) {
    sleepAccelX = lastAccelX;
    sleepAccelY = lastAccelY;
    sleepAccelZ = lastAccelZ;
    dbg("   Orientierung gespeichert: X=");
    dbgf(sleepAccelX, 0);
    dbg(" Y=");
    dbgf(sleepAccelY, 0);
    dbg(" Z=");
    dbgf(sleepAccelZ, 0);
    dbgln();
  }
}

bool checkOrientationChanged() {
  if (!imu.begin(I2C_SDA, I2C_SCL)) {
    dbgln("⚠️ IMU Init fehlgeschlagen");
    return true;
  }
  
  Wire.setClock(100000);
  delay(50);
  
  imu.setAccelRange(QMI8658_ACCEL_RANGE_4G);
  imu.setAccelODR(QMI8658_ACCEL_ODR_125HZ);
  imu.setAccelUnit_mg(true);
  imu.enableSensors(QMI8658_ENABLE_ACCEL);
  delay(50);

  float sumX = 0, sumY = 0, sumZ = 0;
  int validReadings = 0;
  
  for (int i = 0; i < 5; i++) {
    QMI8658_Data d;
    if (imu.readSensorData(d)) {
      sumX += d.accelX;
      sumY += d.accelY;
      sumZ += d.accelZ;
      validReadings++;
    }
    delay(20);
  }
  
  if (validReadings == 0) {
    dbgln("⚠️ Keine IMU-Daten");
    return true;
  }
  
  float avgX = sumX / validReadings;
  float avgY = sumY / validReadings;
  float avgZ = sumZ / validReadings;
  
  float deltaX = abs(avgX - sleepAccelX);
  float deltaY = abs(avgY - sleepAccelY);
  float deltaZ = abs(avgZ - sleepAccelZ);
  float totalDelta = deltaX + deltaY + deltaZ;
  
  dbg("   Orientierung: delta=");
  dbgf(totalDelta, 0);
  dbg(" (threshold=");
  dbg(ORIENTATION_THRESHOLD);
  dbgln(")");
  
  disableIMU();
  
  return (totalDelta > ORIENTATION_THRESHOLD);
}

bool checkShaking() {
  if (!imuOK) return false;

  QMI8658_Data d;
  if (!imu.readSensorData(d)) {
    imuErrorCount++;
    if (imuErrorCount > 10) {
      imuErrorCount = 0;
      dbgln("⚠️ IMU Fehler - Neuinitialisierung...");
      imuOK = initIMU();
    }
    return false;
  }

  imuErrorCount = 0;

  float dx = abs(d.accelX - lastAccelX);
  float dy = abs(d.accelY - lastAccelY);
  float dz = abs(d.accelZ - lastAccelZ);
  float gyro = abs(d.gyroX) + abs(d.gyroY) + abs(d.gyroZ);

  lastAccelX = d.accelX;
  lastAccelY = d.accelY;
  lastAccelZ = d.accelZ;
  lastDelta = dx + dy + dz;
  lastGyro = gyro;

  return (lastDelta > SHAKE_THRESHOLD) || (gyro > SHAKE_GYRO_THRESHOLD);
}

// ============================================
// DEEP SLEEP
// ============================================
void enterDeepSleep(uint8_t reason, uint32_t seconds) {
  printLine('=');
  if (reason == 1) {
    dbgln("🔴 DEEP SLEEP - Batterie kritisch");
  } else {
    dbgln("💤 DEEP SLEEP - Idle");
  }
  dbg("   Dauer: ");
  dbg(seconds);
  dbgln(" Sekunden");
  printLine('=');
  
  sleepReason = reason;
  
  if (reason == 2) {
    saveOrientation();
  }
  
  ledOff();
  delay(50);
  
  disableIMU();
  
  Serial.flush();
  delay(10);
  
  esp_sleep_enable_timer_wakeup(seconds * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void handleWakeup() {
  if (sleepReason == 0) return;
  
  bootCount++;
  delay(50);
  
  float v = readBattQuick();
  
  dbgln();
  printLine('=');
  dbg("🔔 WAKEUP aus ");
  dbgln(sleepReason == 1 ? "Battery-Sleep" : "Idle-Sleep");
  dbg("   Spannung: ");
  dbgf(v, 2);
  dbgln("V");
  printLine('-');
  
  if (sleepReason == 1) {
    if (v >= BATTERY_NOMINAL) {
      dbgln("   ✅ Batterie OK - Normaler Start");
      sleepReason = 0;
      return;
    }
    dbgln("   ❌ Batterie noch kritisch - weiter schlafen");
    enterDeepSleep(1, DEEP_SLEEP_BATTERY);
  }
  
  if (sleepReason == 2) {
    if (v <= BATTERY_CRITICAL && v >= BATTERY_NOT_PRESENT) {
      dbgln("   ⚠️ Batterie kritisch geworden!");
      enterDeepSleep(1, DEEP_SLEEP_BATTERY);
    }
    
    dbgln("   Prüfe Orientierungsänderung...");
    if (checkOrientationChanged()) {
      dbgln("   ✅ Bewegung erkannt - Aufwachen!");
      sleepReason = 0;
      return;
    }
    
    dbgln("   ❌ Keine Änderung - weiter schlafen");
    enterDeepSleep(2, DEEP_SLEEP_IDLE);
  }
}

// ============================================
// ANIMATIONS
// ============================================
void animStart() {
  cpuFast();
  ledSetBrightness(BRIGHTNESS_ANIMATION);
}

void animEnd() {
  ledSetBrightness(BRIGHTNESS_NORMAL);
}

void animBounce() {
  animStart();
  float x[5], y[5], vx[5], vy[5];
  uint8_t co[5];
  for (int i = 0; i < 5; i++) {
    x[i] = random(1, 7);
    y[i] = random(1, 7);
    vx[i] = (random(2) ? 1 : -1) * (0.8f + random(10) / 10.0f);
    vy[i] = (random(2) ? 1 : -1) * (0.8f + random(10) / 10.0f);
    co[i] = i * 50;
  }
  for (int f = 0; f < 40; f++) {
    ledClear();
    float fr = max(0.3f, 1.0f - f * 0.015f);
    for (int i = 0; i < 5; i++) {
      x[i] += vx[i] * fr;
      y[i] += vy[i] * fr;
      if (x[i] <= 0 || x[i] >= 6) { vx[i] *= -1; x[i] = constrain(x[i], 0.0f, 6.0f); }
      if (y[i] <= 0 || y[i] >= 6) { vy[i] *= -1; y[i] = constrain(y[i], 0.0f, 6.0f); }
      dot((int)x[i], (int)y[i], rainbow((f * 5 + co[i]) % 256));
    }
    ledShowSafe();
    delay(30);
  }
  animEnd();
}

void animSpiral() {
  animStart();
  static const int8_t sx[] = { 0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7, 7, 7, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 5, 4, 3, 2, 1, 1, 1, 1, 1, 2, 3, 4, 5, 5, 5, 5, 4, 3, 2, 2, 2, 3, 4, 4, 3 };
  static const int8_t sy[] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7, 7, 7, 7, 6, 5, 4, 3, 2, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 5, 4, 3, 2, 2, 2, 2, 2, 3, 4, 5, 5, 5, 5, 4, 3, 3, 3, 4, 4 };
  for (int i = 63; i >= 0; i -= 2) {
    for (int j = 63; j >= i; j--) setPixel(sx[j], sy[j], rainbow(((64 - j) * 4) % 256));
    ledShowSafe();
    delay(8);
  }
  delay(80);
  for (int i = 0; i < 64; i += 2) {
    ledClear();
    for (int j = i; j < min(i + 12, 64); j++) {
      float b = 1.0f - (j - i) / 12.0f;
      uint32_t c = rainbow((j * 8) % 256);
      setPixel(sx[j], sy[j], ledColor(((c >> 16) & 0xFF) * b, ((c >> 8) & 0xFF) * b, (c & 0xFF) * b));
    }
    ledShowSafe();
    delay(10);
  }
  animEnd();
}

void animScatter() {
  animStart();
  for (int f = 0; f < 30; f++) {
    ledClear();
    float p = min(1.0f, f / 25.0f);
    for (int i = 0; i < 16; i++) {
      int cx = 4 + (random(8) - 4) * (1 - p) * 2;
      int cy = 4 + (random(8) - 4) * (1 - p) * 2;
      setPixel(constrain(cx, 0, 7), constrain(cy, 0, 7), rainbow((i * 16 + f * 6) % 256));
    }
    ledShowSafe();
    delay(35);
  }
  for (int f = 0; f < 5; f++) {
    ledFill(rainbow(f * 50));
    ledShowSafe();
    delay(50);
  }
  animEnd();
}

void animSpin() {
  animStart();
  float ao = 0;
  for (int f = 0; f < 40; f++) {
    ledClear();
    ao += max(0.1f, 0.5f - f * 0.01f);
    for (int r = 0; r < 3; r++) {
      float rad = (1.0f + r * 1.2f) * (f > 28 ? max(0.2f, 1.0f - (f - 28) / 15.0f) : 1.0f);
      for (int i = 0; i < 4 + r * 2; i++) {
        float a = ao * (1 + r * 0.3f) + i * 2 * PI / (4 + r * 2);
        dot(constrain((int)(3.5f + cos(a) * rad), 0, 6),
            constrain((int)(3.5f + sin(a) * rad), 0, 6),
            rainbow((f * 4 + i * 30 + r * 80) % 256));
      }
    }
    ledShowSafe();
    delay(30);
  }
  animEnd();
}

void animPlasma() {
  animStart();
  for (int t = 0; t < 45; t++) {
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        float v = sin(x * 0.5f + t * 0.12f) + sin(y * 0.5f + t * 0.12f) + sin((x + y + t * 0.1f) * 0.5f);
        setPixel(x, y, hsv((uint8_t)((v + 3) * 40) % 256, 255, 255));
      }
    ledShowSafe();
    delay(35);
  }
  animEnd();
}

void animExplode() {
  animStart();
  for (int r = 4; r >= 0; r--) {
    ledClear();
    for (int x = 3 - r; x <= 4 + r; x++)
      for (int y = 3 - r; y <= 4 + r; y++)
        if (x >= 0 && x < 8 && y >= 0 && y < 8) setPixel(x, y, rainbow(r * 50));
    ledShowSafe();
    delay(50);
  }
  ledFill(0xFFFFFF);
  ledShowSafe();
  delay(40);
  for (int f = 0; f < 22; f++) {
    ledClear();
    for (int i = 0; i < 12; i++) {
      float a = i * PI / 6;
      int px = 3.5f + cos(a) * f * 0.45f, py = 3.5f + sin(a) * f * 0.45f;
      if (px >= 0 && px < 8 && py >= 0 && py < 8)
        setPixel(px, py, ledColor(255 - f * 11, max(0, 150 - f * 7), 0));
    }
    for (int s = 0; s < 3; s++) setPixel(random(8), random(8), rainbow(random(256)));
    ledShowSafe();
    delay(35);
  }
  animEnd();
}

void animGlitch() {
  animStart();
  for (int p = 0; p < 30; p++) {
    ledClear();
    for (int g = 0; g < 3; g++) {
      int y = random(8), off = random(-3, 4);
      uint32_t c = randColor();
      for (int x = 0; x < 8; x++) setPixel((x + off + 8) % 8, y, c);
    }
    for (int i = 0; i < 6 + p / 4; i++) setPixel(random(8), random(8), rainbow(random(256)));
    if (random(5) == 0) ledFill(randColor());
    ledShowSafe();
    delay(35 + random(35));
  }
  for (int i = 0; i < 3; i++) {
    ledFill(0xFFFFFF);
    ledShowSafe();
    delay(25);
    ledClear();
    ledShowSafe();
    delay(25);
  }
  animEnd();
}

void animWave() {
  animStart();
  for (int t = 0; t < 50; t++) {
    ledClear();
    for (int x = 0; x < 8; x++) {
      float wave1 = sin((x + t * 0.3f) * 0.8f) * 3 + 3.5f;
      float wave2 = sin((x - t * 0.25f) * 0.6f + 2) * 2.5f + 3.5f;
      for (int y = 0; y < 8; y++) {
        float d1 = abs(y - wave1), d2 = abs(y - wave2);
        if (d1 < 1.5f) setPixel(x, y, ledColor(0, 255 * (1 - d1 / 1.5f), 255 * (1 - d1 / 1.5f)));
        if (d2 < 1.2f) setPixel(x, y, ledColor(255 * (1 - d2 / 1.2f), 0, 255 * (1 - d2 / 1.2f)));
      }
    }
    ledShowSafe();
    delay(40);
  }
  animEnd();
}

void animFirework() {
  animStart();
  for (int fw = 0; fw < 3; fw++) {
    int cx = 2 + random(4), cy = 2 + random(4);
    uint32_t col = randColor();
    for (int y = 7; y >= cy; y--) {
      ledClear();
      setPixel(cx, y, 0xFFFFFF);
      if (y < 7) setPixel(cx, y + 1, ledColor(100, 100, 100));
      ledShowSafe();
      delay(35);
    }
    for (int r = 0; r < 5; r++) {
      ledClear();
      for (int a = 0; a < 8; a++) {
        float angle = a * PI / 4;
        for (int d = 0; d <= r; d++) {
          int px = cx + cos(angle) * d, py = cy + sin(angle) * d;
          if (px >= 0 && px < 8 && py >= 0 && py < 8) {
            uint8_t br = 255 - (r - d) * 40;
            setPixel(px, py, ledColor(((col >> 16) & 0xFF) * br / 255, ((col >> 8) & 0xFF) * br / 255, (col & 0xFF) * br / 255));
          }
        }
      }
      for (int s = 0; s < 4; s++) setPixel(random(8), random(8), 0xFFFFFF);
      ledShowSafe();
      delay(45);
    }
    for (int f = 0; f < 6; f++) {
      for (int i = 0; i < NUMPIXELS; i++) leds[i].fadeToBlackBy(70);
      ledShowSafe();
      delay(35);
    }
  }
  animEnd();
}

void animMatrix() {
  animStart();
  uint8_t drops[8], speeds[8], lengths[8];
  for (int i = 0; i < 8; i++) {
    drops[i] = random(8);
    speeds[i] = 1 + random(3);
    lengths[i] = 2 + random(4);
  }
  for (int f = 0; f < 50; f++) {
    ledClear();
    for (int x = 0; x < 8; x++) {
      if (f % speeds[x] == 0) {
        drops[x]++;
        if (drops[x] > 10 + lengths[x]) {
          drops[x] = 0;
          speeds[x] = 1 + random(3);
          lengths[x] = 2 + random(4);
        }
      }
      for (int t = 0; t < lengths[x]; t++) {
        int y = drops[x] - t;
        if (y >= 0 && y < 8) setPixel(x, y, ledColor(0, 255 - t * (200 / lengths[x]), 0));
      }
    }
    ledShowSafe();
    delay(45);
  }
  animEnd();
}

void animPulse() {
  animStart();
  for (int p = 0; p < 4; p++) {
    uint32_t color = rainbow(p * 64);
    for (int r = 0; r <= 4; r++) {
      ledClear();
      for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
          float dist = sqrt((x - 3.5f) * (x - 3.5f) + (y - 3.5f) * (y - 3.5f));
          if (abs(dist - r) < 1.2f) {
            float br = 1.0f - abs(dist - r) / 1.2f;
            setPixel(x, y, ledColor(((color >> 16) & 0xFF) * br, ((color >> 8) & 0xFF) * br, (color & 0xFF) * br));
          }
        }
      }
      ledShowSafe();
      delay(55);
    }
  }
  ledFill(0xFFFFFF);
  ledShowSafe();
  delay(50);
  ledClear();
  ledShowSafe();
  animEnd();
}

void animPasch() {
  animStart();
  for (int f = 0; f < 25; f++) {
    ledClear();
    for (int i = 0; i < 15; i++) setPixel(random(8), random(8), ledColor(255, 150 + random(105), 0));
    ledShowSafe();
    delay(35);
  }
  for (int i = 0; i < 3; i++) {
    ledFill(ledColor(255, 200, 0));
    ledShowSafe();
    delay(50);
    ledClear();
    ledShowSafe();
    delay(50);
  }
  animEnd();
}

// GAG Animation - Regenbogen + 9er Pasch
void animGag() {
  animStart();
  
  printLine('*');
  printCentered("🌈 GAG MODUS! 🌈");
  printCentered("9 + 9 = 18");
  printLine('*');
  
  // Regenbogen-Explosion
  for (int i = 0; i < 12; i++) {
    ledFill(rainbow(i * 21));
    ledShowSafe();
    delay(50);
  }
  
  // 9er-Pasch Animation (wie normale Würfel-Animation)
  for (int cycle = 0; cycle < 6; cycle++) {
    // Erste 9 (rot) - blinkend einblenden
    ledSetBrightness(BRIGHTNESS_ANIMATION);
    drawNine(DICE1_COLOR);
    delay(300);
    
    // Zweite 9 (blau)
    drawNine(DICE2_COLOR);
    delay(300);
  }
  
  // Finales Blinken
  for (int i = 0; i < 3; i++) {
    ledFill(0xFFFFFF);
    ledShowSafe();
    delay(50);
    ledClear();
    ledShowSafe();
    delay(50);
  }
  
  animEnd();
  gagModeActive = true;
}

void playAnim(bool silent = false) {
  uint8_t selectedAnim = currentAnimation;
  if (currentAnimation == 11) {
    selectedAnim = random(11);
  }

  stats.animationCounts[selectedAnim]++;

  if (!silent) {
    dbg("🎬 Animation: ");
    dbgln(animationNames[selectedAnim]);
  }

  switch (selectedAnim) {
    case 0: animBounce(); break;
    case 1: animSpiral(); break;
    case 2: animScatter(); break;
    case 3: animSpin(); break;
    case 4: animPlasma(); break;
    case 5: animExplode(); break;
    case 6: animGlitch(); break;
    case 7: animWave(); break;
    case 8: animFirework(); break;
    case 9: animMatrix(); break;
    case 10: animPulse(); break;
    default: animBounce();
  }
}

// ============================================
// BOOTUP
// ============================================
void bootAnim() {
  cpuFast();
  ledSetBrightness(BRIGHTNESS_BOOTUP);

  uint8_t drops[8] = { 0 };
  for (int f = 0; f < 40; f++) {
    ledClear();
    for (int x = 0; x < 8; x++) {
      if (f % 2 == 0) drops[x]++;
      if (drops[x] > 10) drops[x] = 0;
      for (int t = 0; t < 4; t++) {
        int y = drops[x] - t;
        if (y >= 0 && y < 8) setPixel(x, y, rainbow((x * 30 + f * 5) % 256));
      }
    }
    ledShowSafe();
    delay(30);
  }

  for (int w = 0; w < 40; w++) {
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++)
        setPixel(x, y, rainbow((x * 20 + y * 20 + w * 8) % 256));
    ledShowSafe();
    delay(22);
  }

  scrollText("MAGIC-FLEX-CUBE v3", 0xFFFFFF, 50);
  
  delay(200);

  static const uint32_t cols[] = { 0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x0000FF, 0x8000FF };
  for (int d = 1; d <= 6; d++) {
    drawDie(d, cols[d - 1]);
    delay(150);
  }

  for (int f = 0; f < 3; f++) {
    ledFill(0xFFFFFF);
    ledShowSafe();
    delay(60);
    ledClear();
    ledShowSafe();
    delay(60);
  }

  ledSetBrightness(BRIGHTNESS_NORMAL);
  ledClear();
  ledShowSafe();
  cpuSlow();
}

// ============================================
// SCREENSAVER (USB Power)
// ============================================
void screensaverMode();
void roll(unsigned long dur);

void screensaverMode() {
  printLine('=');
  dbgln("🖥️ SCREENSAVER MODUS");
  dbgln("   Automatisches Würfeln aktiv");
  dbgln("   Schütteln oder Taste zum Beenden");
  printLine('=');
  
  while (true) {
    bootAnim();
    roll(0);
    
    for (int i = 0; i < 50; i++) {
      if (Serial.available()) {
        Serial.read();
        registerActivity();
        dbgln("\n✅ Screensaver beendet (Eingabe)");
        return;
      }
      
      if (imuOK) {
        QMI8658_Data d;
        if (imu.readSensorData(d)) {
          float dx = abs(d.accelX - lastAccelX);
          float dy = abs(d.accelY - lastAccelY);
          float dz = abs(d.accelZ - lastAccelZ);
          float delta = dx + dy + dz;
          
          lastAccelX = d.accelX;
          lastAccelY = d.accelY;
          lastAccelZ = d.accelZ;
          
          if (delta > SHAKE_THRESHOLD) {
            registerActivity();
            dbgln("\n✅ Screensaver beendet (Bewegung)");
            return;
          }
        }
      }
      
      delay(100);
    }
    
    float v = readBattQuick();
    if (v <= BATTERY_FULL && v >= BATTERY_NOT_PRESENT) {
      dbgln("\n⚠️ Nicht mehr am USB - Screensaver beendet");
      registerActivity();
      return;
    }
  }
}

// ============================================
// IDLE HANDLING
// ============================================
void checkIdleTimeout() {
  if (shakeState != SHAKE_IDLE) return;

  unsigned long idle = millis() - lastActivityTime;

  if (idle > IDLE_DEEP_SLEEP_TIMEOUT) {
    if (isUSBPower()) {
      screensaverMode();
    } else {
      enterDeepSleep(2, DEEP_SLEEP_IDLE);
    }
    return;
  }

  if (!isDimmed && idle > IDLE_TIMEOUT) {
    isDimmed = true;
    ledSetBrightness(BRIGHTNESS_DIM);
    ledShowSafe();
    dbgln("🔅 Display gedimmt (Idle)");
  }
}

void checkBatteryCritical() {
  if (isUSBPower()) return;
  
  if (batteryVoltage <= BATTERY_CRITICAL) {
    printLine('=');
    dbgln("🪫 BATTERIE KRITISCH!");
    dbg("   Spannung: ");
    dbgf(batteryVoltage, 2);
    dbgln("V");
    printLine('=');
    
    cpuFast();
    ledSetBrightness(BRIGHTNESS_LOW_POWER);
    drawBatt(0, 0xFF0000, 0x3C0000);
    delay(2000);
    
    for (int b = BRIGHTNESS_LOW_POWER; b >= 0; b--) {
      ledSetBrightness(b);
      ledShowSafe();
      delay(80);
    }
    
    enterDeepSleep(1, DEEP_SLEEP_BATTERY);
  }
}

// ============================================
// SHAKE HANDLING
// ============================================
void updateShake() {
  bool shaking = checkShaking();
  unsigned long now = millis();
  unsigned long timeSinceStart = shakeStartTime ? (now - shakeStartTime) : 0;
  unsigned long timeSinceLastShake = lastShakeTime ? (now - lastShakeTime) : 0;

  switch (shakeState) {
    case SHAKE_IDLE:
      if (shaking && (now - lastRollTime > SHAKE_COOLDOWN)) {
        shakeState = SHAKE_DETECTING;
        shakeStartTime = now;
        lastShakeTime = now;
        stats.shakeAttempts++;
        registerActivity();
        dbg("\n🔔 Shake START (dA=");
        dbgf(lastDelta, 0);
        dbg(" G=");
        dbgf(lastGyro, 0);
        dbgln(")");
      }
      break;

    case SHAKE_DETECTING:
      if (shaking) {
        lastShakeTime = now;
      }

      if (timeSinceStart >= SHAKE_MIN_DURATION) {
        shakeState = SHAKE_ACTIVE;
        shakeActiveTime = now;
        shakeAnimTime = now;
        cpuFast();
        printLine('-');
        dbg("🎲 SHAKE AKTIV nach ");
        dbg(timeSinceStart);
        dbgln("ms");
        printLine('-');
      }

      if (timeSinceLastShake > SHAKE_END_DELAY) {
        dbgln("❌ Shake ABGEBROCHEN");
        shakeState = SHAKE_IDLE;
        shakeStartTime = 0;
      }
      break;

    case SHAKE_ACTIVE:
      if (shaking) {
        lastShakeTime = now;
      }

      updateShakeAnim();

      if (timeSinceLastShake > SHAKE_END_DELAY) {
        unsigned long totalDuration = now - shakeStartTime;

        printLine('=');
        dbg("✅ SHAKE BEENDET - ");
        dbg(totalDuration);
        dbgln("ms");
        printLine('=');

        shakeState = SHAKE_ROLLING;
        roll(totalDuration);
        shakeState = SHAKE_IDLE;
        shakeStartTime = 0;
        shakeActiveTime = 0;
      }
      break;

    case SHAKE_ROLLING:
      break;
  }
}

// ============================================
// DICE ROLL
// ============================================
void roll(unsigned long dur) {
  registerActivity();
  cpuFast();
  
  gagModeActive = false;

  bool pasch = (dur >= SHAKE_PASCH_TIME) && (dur < SHAKE_GAG_TIME);
  bool gag = (dur >= SHAKE_GAG_TIME);

  printLine('=');
  dbg("🎲 WÜRFELN!");
  if (dur > 0) {
    dbg(" (");
    dbg(dur);
    dbg("ms");
    if (gag) dbg(" - GAG!");
    else if (pasch) dbg(" - PASCH!");
    dbgln(")");
  } else {
    dbgln();
  }

  if (dur > 0) {
    stats.totalShakeTime += dur;
    if (dur > stats.longestShake) stats.longestShake = dur;
    if (dur < stats.shortestShake) stats.shortestShake = dur;
  }

  // GAG-MODUS: 9er Pasch
  if (gag) {
    animGag();
    // Zeige 9er Pasch
    drawNine(DICE1_COLOR);
    currentDisplayDice = 1;
    isDisplayingResult = true;
    lastDisplaySwitch = millis();
    lastRollTime = millis();
    cpuSlow();
    
    printLine('-');
    dbgln("   🌈 9 + 9 = 18 (9er Pasch!)");
    printLine('=');
    dbgln();
    return;  // Kein normales Würfelergebnis
  }
  
  // PASCH-MODUS
  if (pasch) {
    animPasch();
    playAnim();
    if (random(100) < 50) {
      dice1Result = random(1, 7);
      dice2Result = dice1Result;
    } else {
      dice1Result = random(1, 7);
      dice2Result = random(1, 7);
    }
  } else {
    // NORMAL
    playAnim();
    dice1Result = random(1, 7);
    dice2Result = random(1, 7);
  }

  stats.totalRolls++;
  stats.numberCounts[dice1Result]++;
  stats.numberCounts[dice2Result]++;
  stats.sumCounts[dice1Result + dice2Result]++;

  currentDisplayDice = random(1, 3);
  displayPhase = (currentDisplayDice == 1) ? 0 : 1;
  currentBrightness = BRIGHTNESS_NORMAL;
  ledSetBrightness(currentBrightness);

  drawDie(currentDisplayDice == 1 ? dice1Result : dice2Result,
          currentDisplayDice == 1 ? DICE1_COLOR : DICE2_COLOR);

  cpuSlow();

  isDisplayingResult = true;
  lastDisplaySwitch = millis();
  lastRollTime = millis();

  printLine('-');
  dbg("   ");
  dbg(dice1Result);
  dbg(" + ");
  dbg(dice2Result);
  dbg(" = ");
  dbgln(dice1Result + dice2Result);
  printLine('=');
  dbgln();
}

// ============================================
// DISPLAY UPDATE
// ============================================
void updateDisplay() {
  if (!isDisplayingResult || shakeState != SHAKE_IDLE) return;
  if (millis() - lastDisplaySwitch < displayInterval) return;
  
  lastDisplaySwitch = millis();
  
  // GAG-Modus: Zwischen zwei 9ern wechseln
  if (gagModeActive) {
    currentDisplayDice = (currentDisplayDice == 1) ? 2 : 1;
    drawNine(currentDisplayDice == 1 ? DICE1_COLOR : DICE2_COLOR);
    return;
  }
  
  // Normale Anzeige
  bool showBatteryWarning = !isUSBPower() && (batteryVoltage < BATTERY_NOMINAL) && (batteryVoltage >= BATTERY_CRITICAL);
  
  if (showBatteryWarning) {
    displayPhase = (displayPhase + 1) % 3;
    
    switch (displayPhase) {
      case 0:
        currentDisplayDice = 1;
        drawDie(dice1Result, DICE1_COLOR);
        break;
      case 1:
        currentDisplayDice = 2;
        drawDie(dice2Result, DICE2_COLOR);
        break;
      case 2:
        drawBattLow();
        dbg("🪫 Batterie: ");
        dbgf(batteryVoltage, 2);
        dbgln("V");
        break;
    }
  } else {
    currentDisplayDice = (currentDisplayDice == 1) ? 2 : 1;
    drawDie(currentDisplayDice == 1 ? dice1Result : dice2Result,
            currentDisplayDice == 1 ? DICE1_COLOR : DICE2_COLOR);
  }
}

// ============================================
// CLI
// ============================================
void showHelp() {
  printLine('=');
  printCentered("🎲 MAGIC-FLEX-CUBE v3 - HILFE 🎲");
  printLine('=');
  dbgln();
  dbgln("  STEUERUNG:");
  dbgln("  ────────────────────────────────────────────");
  dbgln("  r        Würfeln");
  dbgln("  a        Animation wählen (0-11)");
  dbgln("  p        Animation Preview");
  dbgln("  b        Boot-Animation");
  dbgln("  g        Gag-Animation (9er Pasch)");
  dbgln("  1-6      Würfel 1 setzen");
  dbgln("  9        9er Pasch anzeigen");
  dbgln("  + / -    Anzeigeintervall");
  dbgln("  c        Display löschen");
  dbgln();
  dbgln("  INFO:");
  dbgln("  ────────────────────────────────────────────");
  dbgln("  h / ?    Hilfe");
  dbgln("  i        System-Info");
  dbgln("  v        Batterie-Status");
  dbgln("  s        Sensor-Daten (5s)");
  dbgln("  t        Statistiken");
  dbgln("  x        Statistiken Reset");
  dbgln();
  dbgln("  SHAKE-MODI:");
  dbgln("  ────────────────────────────────────────────");
  dbg("  Normal:       < ");
  dbg(SHAKE_PASCH_TIME / 1000);
  dbgln("s");
  dbg("  Pasch:        ");
  dbg(SHAKE_PASCH_TIME / 1000);
  dbg("-");
  dbg(SHAKE_GAG_TIME / 1000);
  dbgln("s (50% Pasch-Chance)");
  dbg("  9er Pasch:    > ");
  dbg(SHAKE_GAG_TIME / 1000);
  dbgln("s");
  printLine('=');
  dbgln();
}

void showInfo() {
  printLine('=');
  printCentered("📊 SYSTEM INFO");
  printLine('=');
  dbgln();
  printKeyValue("CPU aktuell", getCpuFrequencyMhz(), " MHz");
  printKeyValue("Free Heap", ESP.getFreeHeap() / 1024, " KB");
  printKeyValue("Boot Count", bootCount);
  dbg("  IMU: ");
  dbgln(imuOK ? "✅ OK" : "❌ FEHLER");
  dbg("  Power: ");
  dbgln(isUSBPower() ? "🔌 USB" : "🔋 Akku");
  printKeyValueFloat("Spannung", batteryVoltage, 2, " V");
  printKeyValue("Ladung", batteryPercent, " %");
  printLine('=');
  dbgln();
}

void showStats() {
  printLine('=');
  printCentered("📈 STATISTIKEN");
  printLine('=');
  dbgln();

  if (stats.totalRolls == 0) {
    dbgln("  Noch keine Würfe!");
    printLine('=');
    return;
  }

  printKeyValue("Würfe", stats.totalRolls);
  printKeyValue("Shake-Versuche", stats.shakeAttempts);
  dbgln();

  if (stats.shortestShake != ULONG_MAX) {
    dbgln("  SCHÜTTELN:");
    dbgln("  ────────────────────────────────────────────");
    printKeyValue("Kürzester", stats.shortestShake, " ms");
    printKeyValue("Längster", stats.longestShake, " ms");
    if (stats.totalRolls > 0) {
      printKeyValue("Durchschnitt", stats.totalShakeTime / stats.totalRolls, " ms");
    }
    dbgln();
  }

  dbgln("  ZAHLEN 1-6:");
  dbgln("  ────────────────────────────────────────────");
  uint32_t totalNumbers = stats.totalRolls * 2;
  uint32_t maxCount = 1;
  for (int i = 1; i <= 6; i++) {
    if (stats.numberCounts[i] > maxCount) maxCount = stats.numberCounts[i];
  }

  for (int i = 1; i <= 6; i++) {
    dbg("  ");
    dbg(i);
    dbg(": ");
    printBar(stats.numberCounts[i], maxCount, 20);
    dbg(stats.numberCounts[i]);
    if (totalNumbers > 0) {
      dbg(" (");
      dbgf((float)stats.numberCounts[i] / totalNumbers * 100, 1);
      dbgln("%)");
    } else {
      dbgln();
    }
  }

  dbgln();
  dbgln("  SUMMEN 2-12:");
  dbgln("  ────────────────────────────────────────────");
  uint32_t maxSum = 1;
  for (int i = 2; i <= 12; i++) {
    if (stats.sumCounts[i] > maxSum) maxSum = stats.sumCounts[i];
  }
  for (int i = 2; i <= 12; i++) {
    if (i < 10) dbg(" ");
    dbg(i);
    dbg(": ");
    printBar(stats.sumCounts[i], maxSum, 16);
    dbg(stats.sumCounts[i]);
    if (stats.totalRolls > 0) {
      dbg(" (");
      dbgf((float)stats.sumCounts[i] / stats.totalRolls * 100, 1);
      dbgln("%)");
    } else {
      dbgln();
    }
  }

  printLine('=');
  dbgln();
}

void resetStats() {
  memset(&stats, 0, sizeof(stats));
  stats.shortestShake = ULONG_MAX;
  dbgln("🗑️ Statistiken zurückgesetzt");
}

void showBatt() {
  batteryVoltage = readBatt();
  batteryPercent = battPct(batteryVoltage);

  printLine('-');
  dbg("  🔋 ");
  dbgf(batteryVoltage, 2);
  dbg("V ");
  if (isUSBPower()) {
    dbgln("(USB)");
  } else {
    dbg(batteryPercent);
    dbgln("%");
  }
  printLine('-');
}

void showSensor() {
  if (!imuOK) {
    dbgln("❌ IMU nicht verfügbar!");
    return;
  }

  printLine('=');
  dbgln("📡 SENSOR-DATEN (5 Sekunden)");
  printLine('=');

  for (int i = 0; i < 50; i++) {
    QMI8658_Data d;
    if (imu.readSensorData(d)) {
      float dx = abs(d.accelX - lastAccelX);
      float dy = abs(d.accelY - lastAccelY);
      float dz = abs(d.accelZ - lastAccelZ);
      float delta = dx + dy + dz;
      float gyro = abs(d.gyroX) + abs(d.gyroY) + abs(d.gyroZ);

      dbg("  dA=");
      dbgf(delta, 0);
      dbg(" G=");
      dbgf(gyro, 0);

      if (delta > SHAKE_THRESHOLD || gyro > SHAKE_GYRO_THRESHOLD) {
        dbgln(" 🔔 SHAKE!");
      } else {
        dbgln();
      }

      lastAccelX = d.accelX;
      lastAccelY = d.accelY;
      lastAccelZ = d.accelZ;
    }
    delay(100);
  }

  printLine('=');
}

void cmd(char c) {
  registerActivity();

  if (waitingForAnimNumber) {
    waitingForAnimNumber = false;
    if (c >= '0' && c <= '9') {
      currentAnimation = c - '0';
    } else if (c == 'a' || c == 'A') {
      currentAnimation = 10;
    } else if (c == 'b' || c == 'B') {
      currentAnimation = 11;
    } else {
      dbgln("❌ Ungültig!");
      return;
    }
    dbg("✅ Animation: ");
    dbgln(animationNames[currentAnimation]);
    return;
  }

  switch (c) {
    case 'r':
    case 'R': roll(0); break;
    case 'a':
    case 'A':
      dbgln("📝 Animation (0-9, a=Pulse, b=Random):");
      waitingForAnimNumber = true;
      break;
    case 'p':
    case 'P':
      dbgln("▶️ Preview...");
      playAnim(false);
      ledClear();
      ledShowSafe();
      break;
    case 'b':
    case 'B':
      dbgln("▶️ Boot-Animation...");
      bootAnim();
      break;
    case 'g':
    case 'G':
      dbgln("▶️ Gag-Animation...");
      animGag();
      drawNine(DICE1_COLOR);
      break;
    case '1': case '2': case '3': case '4': case '5': case '6':
      gagModeActive = false;
      dice1Result = c - '0';
      currentDisplayDice = 1;
      displayPhase = 0;
      drawDie(dice1Result, DICE1_COLOR);
      isDisplayingResult = true;
      lastDisplaySwitch = millis();
      dbg("🎯 Würfel 1 = ");
      dbgln(dice1Result);
      break;
    case '9':
      // 9er Pasch anzeigen
      gagModeActive = true;
      currentDisplayDice = 1;
      drawNine(DICE1_COLOR);
      isDisplayingResult = true;
      lastDisplaySwitch = millis();
      dbgln("🎯 9er Pasch!");
      break;
    case '+':
      displayInterval = min(5000UL, displayInterval + 100);
      dbg("⏱️ ");
      dbg(displayInterval);
      dbgln("ms");
      break;
    case '-':
      displayInterval = max(200UL, displayInterval - 100);
      dbg("⏱️ ");
      dbg(displayInterval);
      dbgln("ms");
      break;
    case 'c':
    case 'C':
      ledClear();
      ledShowSafe();
      isDisplayingResult = false;
      gagModeActive = false;
      dbgln("🧹 Cleared");
      break;
    case 'h': case 'H': case '?': showHelp(); break;
    case 'i': case 'I': showInfo(); break;
    case 'v': case 'V': showBatt(); break;
    case 's': case 'S': showSensor(); break;
    case 't': case 'T': showStats(); break;
    case 'x': case 'X': resetStats(); break;
    default:
      if (c >= 32) {
        dbg("❓ '");
        dbg(c);
        dbgln("' (h=Hilfe)");
      }
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  setCpuFrequencyMhz(CPU_FREQ_ACTIVE);

  Serial.begin(115200);
  delay(100);

  disableRadios();

  pinMode(BATTERY_PIN, INPUT);
  analogReadResolution(12);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUMPIXELS);
  ledSetBrightness(BRIGHTNESS_NORMAL);

  handleWakeup();

  batteryVoltage = readBatt();
  batteryPercent = battPct(batteryVoltage);

  dbgln();
  printLine('=');
  printCentered("🎲 MAGIC-FLEX-CUBE v3 🎲");
  printLine('=');
  dbgln();

  if (isUSBPower()) {
    dbgln("  🔌 USB-Betrieb");
  } else {
    dbg("  🔋 ");
    dbgf(batteryVoltage, 2);
    dbg("V (");
    dbg(batteryPercent);
    dbgln("%)");
    
    if (batteryVoltage <= BATTERY_CRITICAL) {
      dbgln("  ⚠️ Batterie kritisch!");
      drawBatt(0, 0xFF0000, 0);
      delay(1500);
      enterDeepSleep(1, DEEP_SLEEP_BATTERY);
    }
  }

  imuOK = initIMU();

  randomSeed(analogRead(0) ^ micros() ^ esp_random());
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  lastActivityTime = millis();

  printLine('-');
  dbg("  Boot #");
  dbgln(bootCount);
  printLine('-');
  dbgln();

  dbgln("▶️ Boot-Animation...");
  bootAnim();
  roll(0);

  cpuSlow();
  showHelp();
}

// ============================================
// LOOP
// ============================================
void loop() {
  updateBatt();
  checkBatteryCritical();
  
  checkIdleTimeout();

  if (Serial.available()) {
    cmd(Serial.read());
  }

  updateShake();

  if (shakeState == SHAKE_IDLE) {
    updateDisplay();
    delay(POLL_INTERVAL_IDLE);
  } else {
    delay(POLL_INTERVAL_ACTIVE);
  }
}