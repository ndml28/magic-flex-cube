#include <QMI8658.h>
#include <FastLED.h>
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_bt.h"
#include "driver/rtc_io.h"

// ============================================
// CONFIGURATION
// ============================================
#define MOTD "Magic-Flex-Cube v6"
#define BATTERY_PIN 5
#define BATTERY_READ_INTERVAL 15000
#define POLL_INTERVAL_ACTIVE 12
#define POLL_INTERVAL_IDLE 48
#define IDLE_TIMEOUT 30000
#define IDLE_DEEP_SLEEP_TIMEOUT 310000

#define BATTERY_FULL 4.2f
#define BATTERY_NOMINAL 3.7f
#define BATTERY_LOW 3.4f
#define BATTERY_CRITICAL 3.3f
#define BATTERY_NOT_PRESENT 1.0f

#define ADC_RESOLUTION 4095.0f
#define ADC_REF_VOLTAGE 3.3f
#define VOLTAGE_DIVIDER_RATIO 2.0f

#define BRIGHTNESS_LOW_POWER 1
#define BRIGHTNESS_NORMAL 4
#define BRIGHTNESS_DIM 1
#define BRIGHTNESS_ANIMATION 20
#define BRIGHTNESS_BOOTUP 40
#define BRIGHTNESS_SHAKE 20

#define DEEP_SLEEP_BATTERY 60
#define DEEP_SLEEP_IDLE 20
#define uS_TO_S_FACTOR 1000000ULL

#define LED_PIN 14
#define NUMPIXELS 64
#define BUTTON_PIN 2
#define I2C_SDA 11
#define I2C_SCL 12

// ============================================
// SHAKE DETECTION
// ============================================
#define SHAKE_THRESHOLD 3500.0f
#define SHAKE_GYRO_THRESHOLD 3000.0f
#define SHAKE_MIN_DURATION 350
#define SHAKE_END_DELAY 500
#define SHAKE_PASCH_TIME 2000
#define SHAKE_GAG_TIME 10000
#define SHAKE_COOLDOWN 3000
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
  "Explode", "Glitch", "Wave", "Firework", "Matrix", 
  "Pulse", "Random",
  "PingPong", "Snake", "Nuke", "Fraunhofer", "Rocket",
  "PP", "Skyscraper", "Tetris", "PacMan", "Invaders",
  "Heart", "DNA", "Hourglass", "Smiley",
  "Windows", "Car", "Nikolaus"
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
bool waitingForSecondDigit = false;
uint8_t firstAnimDigit = 0;
unsigned long animInputTime = 0;
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
  uint32_t orange = 0xFFFF00;
  uint32_t darkOrange = 0x804000;
  
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

// Haus vom Nikolaus - Zeichenanimation
void animNikolaus() {
  animStart();
  
  ledClear();
  
  // Haupt-Eckpunkte (auf 8x8 skaliert)
  // A = unten links, B = unten rechts, C = oben rechts, D = oben links, E = Dachspitze
  //
  //        E (3,0)
  //       /\
  //      /  \
  //   D +----+ C
  //     |\  /|
  //     | \/ |
  //     | /\ |
  //     |/  \|
  //   A +----+ B
  
  const int8_t Ax = 1, Ay = 7;  // Unten links
  const int8_t Bx = 6, By = 7;  // Unten rechts
  const int8_t Cx = 6, Cy = 3;  // Oben rechts
  const int8_t Dx = 1, Dy = 3;  // Oben links
  const int8_t Ex = 3, Ey = 0;  // Dachspitze
  
  // Reihenfolge: A -> B -> C -> D -> A -> E -> B -> D -> C
  // "Das ist das Haus vom Ni-ko-laus"
  const int8_t orderX[] = {Ax, Bx, Cx, Dx, Ax, Ex, Bx, Dx, Cx};
  const int8_t orderY[] = {Ay, By, Cy, Dy, Ay, Ey, By, Dy, Cy};
  
  // Farben für jede Linie
  const uint32_t lineColors[] = {
    0xFF0000,  // Rot
    0xFF8800,  // Orange
    0xFFFF00,  // Gelb
    0x00FF00,  // Grün
    0x00FFFF,  // Cyan
    0x0088FF,  // Hellblau
    0x0000FF,  // Blau
    0x8800FF   // Violett
  };
  
  // Bresenham Linien-Algorithmus für jede Linie
  for (int line = 0; line < 8; line++) {
    int x0 = orderX[line];
    int y0 = orderY[line];
    int x1 = orderX[line + 1];
    int y1 = orderY[line + 1];
    
    uint32_t color = lineColors[line];
    
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    int x = x0, y = y0;
    
    while (true) {
      // Pixel setzen
      setPixel(x, y, color);
      
      // Leuchtender Cursor-Effekt
      setPixel(x, y, 0xFFFFFF);
      ledShowSafe();
      delay(40);
      setPixel(x, y, color);  // Zurück zur Linienfarbe
      ledShowSafe();
      
      if (x == x1 && y == y1) break;
      
      int e2 = 2 * err;
      if (e2 > -dy) { err -= dy; x += sx; }
      if (e2 < dx) { err += dx; y += sy; }
    }
    
    delay(80);  // Kurze Pause zwischen Linien
  }
  
  delay(300);
  
  // Fertiges Haus blinken/leuchten lassen
  for (int f = 0; f < 8; f++) {
    // Rainbow-Effekt über alle Pixel
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        if (leds[y * 8 + x].r > 0 || leds[y * 8 + x].g > 0 || leds[y * 8 + x].b > 0) {
          setPixel(x, y, rainbow((x * 20 + y * 20 + f * 30) % 256));
        }
      }
    }
    ledShowSafe();
    delay(100);
  }
  
  // Finales Blinken
  for (int f = 0; f < 3; f++) {
    ledSetBrightness(BRIGHTNESS_ANIMATION * 2);
    ledShowSafe();
    delay(50);
    ledSetBrightness(BRIGHTNESS_ANIMATION);
    ledShowSafe();
    delay(50);
  }
  
  animEnd();
}

// Fahrendes Auto
void animCar() {
  animStart();
  
  for (int frame = 0; frame < 60; frame++) {
    int carX = -5 + frame / 2;  // Auto Position
    
    ledClear();
    
    // Himmel/Hintergrund
    for (int y = 0; y < 5; y++) {
      for (int x = 0; x < 8; x++) {
        setPixel(x, y, ledColor(0, 0, 20 + y * 5));  // Gradient
      }
    }
    
    // Straße
    for (int x = 0; x < 8; x++) {
      setPixel(x, 6, 0x333333);
      setPixel(x, 7, 0x444444);
    }
    
    // Straßenmarkierung (bewegend)
    for (int x = 0; x < 8; x += 3) {
      int markX = (x - (frame / 3) % 3 + 8) % 8;
      setPixel(markX, 6, 0xFFFF00);
    }
    
    // --- AUTO ---
    // Karosserie (rot)
    for (int dx = 0; dx < 5; dx++) {
      int px = carX + dx;
      if (px >= 0 && px < 8) {
        setPixel(px, 4, 0xCC0000);  // Dach
        setPixel(px, 5, 0xFF0000);  // Körper
      }
    }
    
    // Fenster (hellblau)
    for (int dx = 1; dx < 3; dx++) {
      int px = carX + dx;
      if (px >= 0 && px < 8) {
        setPixel(px, 4, 0x66CCFF);
      }
    }
    
    // Motorhaube vorne
    int hoodX = carX + 3;
    if (hoodX >= 0 && hoodX < 8) setPixel(hoodX, 4, 0xAA0000);
    hoodX = carX + 4;
    if (hoodX >= 0 && hoodX < 8) setPixel(hoodX, 4, 0x880000);
    
    // Räder (animiert - drehen)
    int wheelFrame = frame % 4;
    uint32_t wheelColor1 = (wheelFrame < 2) ? 0x222222 : 0x111111;
    uint32_t wheelColor2 = (wheelFrame < 2) ? 0x111111 : 0x222222;
    
    // Hinterrad
    int wheelX = carX + 1;
    if (wheelX >= 0 && wheelX < 8) {
      setPixel(wheelX, 6, wheelColor1);
    }
    
    // Vorderrad
    wheelX = carX + 4;
    if (wheelX >= 0 && wheelX < 8) {
      setPixel(wheelX, 6, wheelColor2);
    }
    
    // Scheinwerfer (gelb, blinkend)
    int lightX = carX + 5;
    if (lightX >= 0 && lightX < 8) {
      setPixel(lightX, 5, (frame % 6 < 3) ? 0xFFFF00 : 0xAAAA00);
    }
    
    // Rücklichter (rot)
    lightX = carX - 1;
    if (lightX >= 0 && lightX < 8) {
      setPixel(lightX, 5, 0xFF0000);
    }
    
    // Abgase / Rauch (hinten)
    if (frame % 3 == 0) {
      int smokeX = carX - 2 - random(2);
      int smokeY = 5 - random(2);
      if (smokeX >= 0 && smokeX < 8 && smokeY >= 0) {
        setPixel(smokeX, smokeY, ledColor(60, 60, 60));
      }
    }
    
    ledShowSafe();
    delay(60);
  }
  
  animEnd();
}

// Windows 10 Ladeanimation - kreisende Punkte
void animWindows() {
  animStart();
  
  // Windows 10 style - 5 Punkte die im Kreis rotieren mit variabler Geschwindigkeit
  const int numDots = 5;
  float dotPositions[numDots];
  
  // Gestaffelter Start
  for (int i = 0; i < numDots; i++) {
    dotPositions[i] = -i * 0.6f;
  }
  
  // Windows Blau
  uint32_t winBlue = 0x0078D4;
  
  for (int f = 0; f < 100; f++) {
    ledClear();
    
    // Optional: Dunkler Hintergrund
    // ledFill(0x001020);
    
    for (int i = 0; i < numDots; i++) {
      // Windows-typische Beschleunigung: schnell oben, langsam unten
      float pos = dotPositions[i];
      float normalizedPos = fmod(pos + 10 * PI, 2 * PI);  // 0 bis 2*PI
      
      // Geschwindigkeitsmodifikation: schneller bei 0 und PI, langsamer bei PI/2 und 3PI/2
      float speedMod = 1.0f + 0.8f * cos(normalizedPos * 2);
      
      dotPositions[i] += 0.08f * speedMod;
      
      // Position berechnen
      float x = 3.5f + cos(dotPositions[i]) * 2.8f;
      float y = 3.5f + sin(dotPositions[i]) * 2.8f;
      
      // Helligkeit basierend auf Position im Kreis (vorne heller)
      float brightness = 0.5f + 0.5f * (1.0f - (float)i / numDots);
      
      int px = constrain((int)round(x), 0, 7);
      int py = constrain((int)round(y), 0, 7);
      
      // Windows Blau mit Helligkeit
      setPixel(px, py, ledColor(0, 
                                (uint8_t)(120 * brightness), 
                                (uint8_t)(212 * brightness)));
      
      // Tail-Effekt: vorherige Position leicht
      float prevX = 3.5f + cos(dotPositions[i] - 0.3f) * 2.8f;
      float prevY = 3.5f + sin(dotPositions[i] - 0.3f) * 2.8f;
      int ppx = constrain((int)round(prevX), 0, 7);
      int ppy = constrain((int)round(prevY), 0, 7);
      if (ppx != px || ppy != py) {
        setPixel(ppx, ppy, ledColor(0, 
                                    (uint8_t)(40 * brightness), 
                                    (uint8_t)(70 * brightness)));
      }
    }
    
    ledShowSafe();
    delay(30);
  }
  
  // "Fertig" - kurzer Flash
  ledFill(winBlue);
  ledShowSafe();
  delay(100);
  ledClear();
  ledShowSafe();
  
  animEnd();
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
  // Vollständige IMU-Initialisierung (wie in initIMU)
  if (!imu.begin(I2C_SDA, I2C_SCL)) {
    dbgln("⚠️ IMU Init fehlgeschlagen");
    return true;
  }
  
  Wire.setClock(100000);
  delay(100);  // Längeres Delay nach Begin
  
  // Komplette Konfiguration wie in initIMU()
  imu.setAccelRange(QMI8658_ACCEL_RANGE_4G);
  imu.setAccelODR(QMI8658_ACCEL_ODR_125HZ);
  imu.setAccelUnit_mg(true);
  imu.setGyroRange(QMI8658_GYRO_RANGE_256DPS);
  imu.setGyroODR(QMI8658_GYRO_ODR_125HZ);
  imu.setGyroUnit_dps(true);
  imu.enableSensors(QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO);
  
  delay(150);  // Warten bis Sensor stabil
  
  // WICHTIG: Erste Lesungen verwerfen (Aufwärm-Phase)
  QMI8658_Data dummy;
  for (int i = 0; i < 10; i++) {
    imu.readSensorData(dummy);
    delay(20);
  }
  
  // Jetzt echte Messungen mitteln
  float sumX = 0, sumY = 0, sumZ = 0;
  int validReadings = 0;
  
  for (int i = 0; i < 10; i++) {
    QMI8658_Data d;
    if (imu.readSensorData(d)) {
      // Plausibilitätsprüfung: Werte müssen im normalen Bereich sein
      if (abs(d.accelX) < 3000 && abs(d.accelY) < 3000 && abs(d.accelZ) < 3000) {
        sumX += d.accelX;
        sumY += d.accelY;
        sumZ += d.accelZ;
        validReadings++;
      }
    }
    delay(20);
  }
  
  // Nicht genug gültige Lesungen = Sensor-Problem
  if (validReadings < 5) {
    dbg("⚠️ Nur ");
    dbg(validReadings);
    dbgln(" gültige IMU-Lesungen - nehme Bewegung an");
    disableIMU();
    return true;
  }
  
  float avgX = sumX / validReadings;
  float avgY = sumY / validReadings;
  float avgZ = sumZ / validReadings;
  
  float deltaX = abs(avgX - sleepAccelX);
  float deltaY = abs(avgY - sleepAccelY);
  float deltaZ = abs(avgZ - sleepAccelZ);
  float totalDelta = deltaX + deltaY + deltaZ;
  
  // Debug-Ausgabe
  dbg("   Gespeichert: X=");
  dbgf(sleepAccelX, 0);
  dbg(" Y=");
  dbgf(sleepAccelY, 0);
  dbg(" Z=");
  dbgf(sleepAccelZ, 0);
  dbgln();
  
  dbg("   Aktuell:     X=");
  dbgf(avgX, 0);
  dbg(" Y=");
  dbgf(avgY, 0);
  dbg(" Z=");
  dbgf(avgZ, 0);
  dbgln();
  
  dbg("   Orientierung: delta=");
  dbgf(totalDelta, 0);
  dbg(" (threshold=");
  dbgf(ORIENTATION_THRESHOLD, 2);
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
// ============================================
// DEEP SLEEP - KORRIGIERT
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
  dbg("   Reason: ");
  dbgln(reason);
  printLine('=');
  
  // In RTC Memory speichern
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
  // Prüfen ob es wirklich ein Deep Sleep Wakeup war
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  // Kein Deep Sleep Wakeup -> normaler Boot
  if (wakeup_reason != ESP_SLEEP_WAKEUP_TIMER) {
    dbgln("🔄 Normaler Boot (kein Deep Sleep)");
    sleepReason = 0;  // Reset für sauberen Zustand
    bootCount = 0;
    return;
  }
  
  // Deep Sleep Wakeup!
  bootCount++;
  
  dbgln();
  printLine('=');
  dbg("🔔 DEEP SLEEP WAKEUP #");
  dbgln(bootCount);
  dbg("   Sleep Reason: ");
  dbgln(sleepReason);
  
  delay(50);
  float v = readBattQuick();
  
  dbg("   Spannung: ");
  dbgf(v, 2);
  dbgln("V");
  printLine('-');
  
  // Battery Sleep (reason == 1)
  if (sleepReason == 1) {
    if (v >= BATTERY_NOMINAL) {
      dbgln("   ✅ Batterie OK - Normaler Start");
      sleepReason = 0;
      return;
    }
    if (v < BATTERY_NOT_PRESENT) {
      dbgln("   ✅ USB angeschlossen - Normaler Start");
      sleepReason = 0;
      return;
    }
    dbgln("   ❌ Batterie noch kritisch - weiter schlafen");
    printLine('=');
    
    // Kurz rotes Batterie-Symbol zeigen
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUMPIXELS);
    ledSetBrightness(BRIGHTNESS_LOW_POWER);
    drawBatt(0, 0xFF0000, 0);
    delay(500);
    ledOff();
    
    enterDeepSleep(1, DEEP_SLEEP_BATTERY);
    return;  // Wird nie erreicht
  }
  
  // Idle Sleep (reason == 2)
  if (sleepReason == 2) {
    // Erst Batterie checken
    if (v <= BATTERY_CRITICAL && v >= BATTERY_NOT_PRESENT) {
      dbgln("   ⚠️ Batterie kritisch geworden!");
      enterDeepSleep(1, DEEP_SLEEP_BATTERY);
      return;
    }
    
    // IMU initialisieren für Orientierungsprüfung
    dbgln("   Prüfe Orientierungsänderung...");
    
    if (checkOrientationChanged()) {
      dbgln("   ✅ Bewegung erkannt - Aufwachen!");
      sleepReason = 0;
      return;
    }
    
    dbgln("   ❌ Keine Änderung - weiter schlafen");
    printLine('=');
    enterDeepSleep(2, DEEP_SLEEP_IDLE);
    return;  // Wird nie erreicht
  }
  
  // Unbekannter Reason -> normaler Boot
  dbgln("   ⚠️ Unbekannter Reason - Normaler Start");
  sleepReason = 0;
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

// ============================================
// NEUE ANIMATIONEN
// ============================================

// Ping-Pong Spiel
void animPingPong() {
  animStart();
  float ballX = 4, ballY = 4;
  float velX = 0.4f, velY = 0.3f;
  int paddle1 = 3, paddle2 = 3;
  int score1 = 0, score2 = 0;
  
  for (int f = 0; f < 80; f++) {
    ledClear();
    
    // Paddles (links und rechts)
    for (int i = 0; i < 3; i++) {
      setPixel(0, constrain(paddle1 + i, 0, 7), 0x00FF00);
      setPixel(7, constrain(paddle2 + i, 0, 7), 0x00FF00);
    }
    
    // Ball bewegen
    ballX += velX;
    ballY += velY;
    
    // Paddle-KI
    if (f % 3 == 0) {
      if (ballY > paddle1 + 1) paddle1 = min(5, paddle1 + 1);
      if (ballY < paddle1 + 1) paddle1 = max(0, paddle1 - 1);
      if (ballY > paddle2 + 1) paddle2 = min(5, paddle2 + 1);
      if (ballY < paddle2 + 1) paddle2 = max(0, paddle2 - 1);
    }
    
    // Kollision oben/unten
    if (ballY <= 0 || ballY >= 7) velY *= -1;
    
    // Kollision Paddle links
    if (ballX <= 1 && ballY >= paddle1 && ballY <= paddle1 + 2) {
      velX = abs(velX) * 1.05f;
      velY += (ballY - paddle1 - 1) * 0.1f;
    }
    // Kollision Paddle rechts
    if (ballX >= 6 && ballY >= paddle2 && ballY <= paddle2 + 2) {
      velX = -abs(velX) * 1.05f;
      velY += (ballY - paddle2 - 1) * 0.1f;
    }
    
    // Tor
    if (ballX < 0 || ballX > 7) {
      // Flash
      ledFill(ballX < 0 ? 0x0000FF : 0xFF0000);
      ledShowSafe();
      delay(100);
      ballX = 4; ballY = 4;
      velX = (ballX < 0) ? 0.4f : -0.4f;
      velY = (random(2) ? 1 : -1) * 0.3f;
    }
    
    // Ball zeichnen
    setPixel(constrain((int)ballX, 0, 7), constrain((int)ballY, 0, 7), 0xFFFFFF);
    
    ledShowSafe();
    delay(40);
  }
  animEnd();
}

// Snake Animation
void animSnake() {
  animStart();
  
  int8_t snakeX[20] = {3, 2, 1};
  int8_t snakeY[20] = {4, 4, 4};
  int snakeLen = 3;
  int8_t dirX = 1, dirY = 0;
  int8_t foodX = 6, foodY = 4;
  
  for (int f = 0; f < 60; f++) {
    ledClear();
    
    // Richtung ändern (pseudo-KI zum Futter)
    if (f % 4 == 0) {
      if (random(3) == 0) {
        // Zufällige Richtungsänderung
        if (dirX != 0) { dirX = 0; dirY = random(2) ? 1 : -1; }
        else { dirY = 0; dirX = random(2) ? 1 : -1; }
      } else {
        // Zum Futter steuern
        if (abs(snakeX[0] - foodX) > abs(snakeY[0] - foodY)) {
          dirX = (foodX > snakeX[0]) ? 1 : -1; dirY = 0;
        } else {
          dirY = (foodY > snakeY[0]) ? 1 : -1; dirX = 0;
        }
      }
    }
    
    // Snake bewegen
    for (int i = snakeLen - 1; i > 0; i--) {
      snakeX[i] = snakeX[i-1];
      snakeY[i] = snakeY[i-1];
    }
    snakeX[0] = (snakeX[0] + dirX + 8) % 8;
    snakeY[0] = (snakeY[0] + dirY + 8) % 8;
    
    // Futter essen
    if (snakeX[0] == foodX && snakeY[0] == foodY) {
      if (snakeLen < 15) snakeLen++;
      foodX = random(8);
      foodY = random(8);
      // Flash
      ledFill(0x00FF00);
      ledShowSafe();
      delay(50);
    }
    
    // Snake zeichnen
    for (int i = 0; i < snakeLen; i++) {
      uint32_t col = hsv((i * 15) % 256, 255, 255 - i * 10);
      setPixel(snakeX[i], snakeY[i], col);
    }
    
    // Kopf heller
    setPixel(snakeX[0], snakeY[0], 0xFFFFFF);
    
    // Futter blinkt
    if (f % 4 < 2) setPixel(foodX, foodY, 0xFF0000);
    
    ledShowSafe();
    delay(80);
  }
  animEnd();
}

void animNuke() {
  animStart();
  
  for (int y = 0; y < 6; y++) {
    ledClear();
    setPixel(3, y, 0x888888);
    setPixel(4, y, 0x888888);
    if (y > 0) setPixel(3, y-1, 0x444444);
    ledShowSafe();
    delay(60);
  }
  
  // Aufprall - weißer Flash
  ledFill(0xFFFFFF);
  ledShowSafe();
  delay(100);
  
  // Explosion expandiert
  for (int r = 0; r < 5; r++) {
    ledClear();
    for (int x = 0; x < 8; x++) {
      for (int y = 0; y < 8; y++) {
        float dist = sqrt((x - 3.5f) * (x - 3.5f) + (y - 5.5f) * (y - 5.5f));
        if (dist <= r + 1) {
          uint8_t bright = 255 - (r * 30);
          setPixel(x, y, ledColor(bright, bright * 0.6f, 0));
        }
      }
    }
    ledShowSafe();
    delay(50);
  }
  
  // Pilzwolke aufsteigen
  for (int f = 0; f < 25; f++) {
    ledClear();
    
    // Stiel
    int stemTop = max(2, 6 - f / 3);
    for (int y = 7; y >= stemTop; y--) {
      uint8_t br = 150 - (7 - y) * 15;
      setPixel(3, y, ledColor(br, br * 0.4f, 0));
      setPixel(4, y, ledColor(br, br * 0.4f, 0));
    }
    
    // Pilzkopf
    int cloudY = max(0, stemTop - 2);
    int cloudSize = min(3, 1 + f / 6);
    for (int dx = -cloudSize; dx <= cloudSize; dx++) {
      for (int dy = -1; dy <= 1; dy++) {
        int px = 3 + dx, py = cloudY + dy;
        if (px >= 0 && px < 8 && py >= 0 && py < 8) {
          float dist = sqrt(dx * dx + dy * dy * 2);
          if (dist <= cloudSize + 0.5f) {
            uint8_t br = 200 - dist * 30;
            setPixel(px, py, ledColor(br, br * 0.3f + random(30), random(20)));
          }
        }
      }
    }
    
    // Partikel
    for (int p = 0; p < 3; p++) {
      setPixel(random(8), random(4), ledColor(255, random(100), 0));
    }
    
    ledShowSafe();
    delay(70);
  }
  
  // Fade out
  for (int f = 0; f < 10; f++) {
    for (int i = 0; i < NUMPIXELS; i++) leds[i].fadeToBlackBy(50);
    ledShowSafe();
    delay(50);
  }
  
  animEnd();
}
// Fraunhofer Logo - Vertikale gebogene weiße Streifen auf Grün
// Fraunhofer Logo - Weiße geschwungene Linien von links unten nach rechts oben
void animFraunhofer() {
  animStart();
  
  uint32_t fhGreen = 0x179C7D;
  uint32_t white = 0xFFFFFF;
  
  // Grüner Hintergrund fade-in
  for (int b = 0; b <= 10; b++) {
    uint8_t r = (0x17 * b) / 10;
    uint8_t g = (0x9C * b) / 10;
    uint8_t bl = (0x7D * b) / 10;
    ledFill(ledColor(r, g, bl));
    ledShowSafe();
    delay(30);
  }
  
  delay(100);
  
  // Die 4 charakteristischen Kurven des Fraunhofer-Logos
  // Definiert als Pixel-Koordinaten für jede Linie
  // Alle Linien starten links unten und fächern nach rechts oben auf
  
  // Linie 1 (unterste) - startet ganz unten links
  const int8_t line1X[] = {0, 0, 1, 1, 2, 2, 3, 3};
  const int8_t line1Y[] = {7, 6, 5, 4, 4, 3, 3, 2};
  const int line1Len = 8;
  
  // Linie 2
  const int8_t line2X[] = {0, 1, 1, 2, 2, 3, 4, 4, 5};
  const int8_t line2Y[] = {5, 4, 3, 3, 2, 2, 1, 0, 0};
  const int line2Len = 9;
  
  // Linie 3
  const int8_t line3X[] = {0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7};
  const int8_t line3Y[] = {4, 3, 2, 2, 2, 1, 1, 1, 0, 0, 0};
  const int line3Len = 11;
  
  // Linie 4 (oberste) - endet ganz rechts
  const int8_t line4X[] = {0, 1, 2, 3, 4, 5, 6, 7, 7};
  const int8_t line4Y[] = {3, 2, 2, 1, 1, 1, 1, 1, 2};
  const int line4Len = 9;
  
  // Linie 5 (zusätzliche oberste)
  const int8_t line5X[] = {1, 2, 3, 4, 5, 6, 7};
  const int8_t line5Y[] = {1, 1, 0, 0, 0, 0, 0};
  const int line5Len = 7;
  
  // Alle Linien und deren Längen in Arrays
  const int8_t* linesX[] = {line1X, line2X, line3X, line4X, line5X};
  const int8_t* linesY[] = {line1Y, line2Y, line3Y, line4Y, line5Y};
  const int lineLens[] = {line1Len, line2Len, line3Len, line4Len, line5Len};
  
  // Linien nacheinander zeichnen (animiert)
  for (int l = 0; l < 5; l++) {
    for (int p = 0; p < lineLens[l]; p++) {
      setPixel(linesX[l][p], linesY[l][p], white);
      ledShowSafe();
      delay(25);
    }
    delay(50);
  }
  
  delay(400);
  
  // Animation: Linien "fließen" - Wellen-Effekt
  for (int frame = 0; frame < 40; frame++) {
    // Grüner Hintergrund
    ledFill(fhGreen);
    
    // Linien mit Helligkeits-Welle
    for (int l = 0; l < 5; l++) {
      for (int p = 0; p < lineLens[l]; p++) {
        // Helligkeit variiert wellenförmig
        float wave = sin((p * 0.5f) + (frame * 0.3f) - (l * 0.5f));
        uint8_t brightness = 180 + (uint8_t)(wave * 75);
        setPixel(linesX[l][p], linesY[l][p], ledColor(brightness, brightness, brightness));
      }
    }
    
    ledShowSafe();
    delay(40);
  }
  
  // Finales statisches Logo
  ledFill(fhGreen);
  for (int l = 0; l < 5; l++) {
    for (int p = 0; p < lineLens[l]; p++) {
      setPixel(linesX[l][p], linesY[l][p], white);
    }
  }
  ledShowSafe();
  delay(500);
  
  animEnd();
}

// Rakete startet
void animRocket() {
  animStart();
  
  // Rakete (3 Pixel breit, 4 hoch)
  // Countdown
  for (int c = 3; c >= 1; c--) {
    ledClear();
    // Zahl in Mitte
    if (c == 3) { dot(3, 3, 0xFF0000); }
    else if (c == 2) { dot(3, 2, 0xFFFF00); dot(3, 4, 0xFFFF00); }
    else { dot(3, 3, 0x00FF00); }
    ledShowSafe();
    delay(400);
  }
  
  // Start!
  ledFill(0xFFFFFF);
  ledShowSafe();
  delay(50);
  
  // Rakete steigt auf
  for (int rocketY = 6; rocketY >= -5; rocketY--) {
    ledClear();
    
    // Flammen (unter der Rakete)
    for (int fy = rocketY + 4; fy < min(8, rocketY + 7); fy++) {
      if (fy >= 0 && fy < 8) {
        setPixel(3, fy, ledColor(255, random(100, 200), 0));
        if (random(2)) setPixel(2, fy, ledColor(255, random(50, 150), 0));
        if (random(2)) setPixel(4, fy, ledColor(255, random(50, 150), 0));
      }
    }
    
    // Raketenkörper
    for (int ry = 0; ry < 4; ry++) {
      int py = rocketY + ry;
      if (py >= 0 && py < 8) {
        if (ry == 0) {
          // Spitze
          setPixel(3, py, 0xFFFFFF);
        } else {
          // Körper
          setPixel(2, py, 0xCCCCCC);
          setPixel(3, py, 0xFFFFFF);
          setPixel(4, py, 0xCCCCCC);
        }
      }
    }
    
    // Flügel
    if (rocketY + 3 >= 0 && rocketY + 3 < 8) {
      setPixel(1, rocketY + 3, 0xFF0000);
      setPixel(5, rocketY + 3, 0xFF0000);
    }
    
    // Rauch-Trail
    for (int s = 0; s < 3; s++) {
      int sy = rocketY + 5 + s + random(2);
      if (sy >= 0 && sy < 8) {
        setPixel(3 + random(-1, 2), sy, ledColor(80, 80, 80));
      }
    }
    
    ledShowSafe();
    delay(rocketY > 2 ? 100 : 60);  // Beschleunigen
  }
  
  // Sterne nach dem Start
  for (int f = 0; f < 15; f++) {
    ledClear();
    for (int s = 0; s < 8; s++) {
      setPixel(random(8), random(8), random(2) ? 0xFFFFFF : 0x444444);
    }
    ledShowSafe();
    delay(60);
  }
  
  animEnd();
}

void animPP() {
  animStart();
  
  // Aufbau
  const uint8_t shape[8] = {
    0b00011000,
    0b00111100,
    0b00111100,
    0b00011000,
    0b00011000,
    0b00011000,
    0b00111100,
    0b01100110
  };
  
  // Von unten aufbauen
  uint32_t skinColor = 0xFFAA88;
  for (int row = 7; row >= 0; row--) {
    for (int y = row; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        if (shape[y] & (1 << (7 - x))) {
          setPixel(x, y, skinColor);
        }
      }
    }
    ledShowSafe();
    delay(80);
  }
  
  delay(200);
  
  // Pulsieren
  for (int p = 0; p < 8; p++) {
    uint32_t col = (p % 2 == 0) ? 0xFFAA88 : 0xFF7766;
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        if (shape[y] & (1 << (7 - x))) {
          setPixel(x, y, col);
        }
      }
    }
    ledShowSafe();
    delay(150);
  }
  
  // Regenbogen (Pride!)
  for (int f = 0; f < 20; f++) {
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        if (shape[y] & (1 << (7 - x))) {
          setPixel(x, y, rainbow((y * 30 + f * 10) % 256));
        }
      }
    }
    ledShowSafe();
    delay(50);
  }
  
  animEnd();
}

// Wolkenkratzer wird gebaut
void animSkyscraper() {
  animStart();
  
  // Gebäude von unten aufbauen
  for (int height = 0; height < 8; height++) {
    int buildY = 7 - height;
    
    // Kran oben
    if (buildY > 0) {
      ledClear();
      
      // Bisheriges Gebäude
      for (int y = 7; y > buildY; y--) {
        for (int x = 2; x <= 5; x++) {
          // Fenster-Muster
          bool isWindow = ((x + y) % 2 == 0);
          setPixel(x, y, isWindow ? 0xFFFF00 : 0x444488);
        }
      }
      
      // Kran
      setPixel(3, buildY - 1, 0xFF8800);
      setPixel(4, buildY - 1, 0xFF8800);
      for (int kx = 0; kx < 7; kx++) {
        setPixel(kx, max(0, buildY - 2), 0x888888);
      }
      
      // Bauarbeiter-Blinklicht
      if (height % 2 == 0) setPixel(6, max(0, buildY - 2), 0xFF0000);
      
      ledShowSafe();
      delay(80);
    }
    
    // Etage bauen (von links nach rechts)
    for (int x = 2; x <= 5; x++) {
      ledClear();
      
      // Bisheriges Gebäude
      for (int y = 7; y >= buildY; y--) {
        for (int bx = 2; bx <= 5; bx++) {
          if (y > buildY || bx < x) {
            bool isWindow = ((bx + y) % 2 == 0);
            setPixel(bx, y, isWindow ? 0xFFFF00 : 0x444488);
          }
        }
      }
      
      // Aktueller Block
      setPixel(x, buildY, 0xFFFFFF);
      
      ledShowSafe();
      delay(60);
    }
  }
  
  // Fertiges Gebäude blinkt
  for (int f = 0; f < 10; f++) {
    for (int y = 0; y < 8; y++) {
      for (int x = 2; x <= 5; x++) {
        bool isWindow = ((x + y) % 2 == 0);
        bool lightOn = isWindow && (random(3) > 0);
        setPixel(x, y, lightOn ? 0xFFFF00 : 0x222244);
      }
    }
    ledShowSafe();
    delay(200);
  }
  
  animEnd();
}

// Tetris-Blöcke fallen
void animTetris() {
  animStart();
  
  uint8_t grid[8] = {0};
  
  // Tetris-Formen (I, O, T, L)
  const uint8_t shapes[4][4] = {
    {0b1111, 0b0000, 0b0000, 0b0000},  // I
    {0b0110, 0b0110, 0b0000, 0b0000},  // O
    {0b0010, 0b0110, 0b0010, 0b0000},  // T
    {0b0100, 0b0100, 0b0110, 0b0000}   // L
  };
  const uint32_t colors[4] = {0x00FFFF, 0xFFFF00, 0xFF00FF, 0xFF8800};
  
  for (int block = 0; block < 6; block++) {
    int type = random(4);
    int posX = random(3, 5);
    uint32_t col = colors[type];
    
    // Block fällt
    for (int posY = 0; posY < 8; posY++) {
      // Kollisionsprüfung
      bool canFall = true;
      for (int sy = 0; sy < 4; sy++) {
        for (int sx = 0; sx < 4; sx++) {
          if (shapes[type][sy] & (1 << (3 - sx))) {
            int checkY = posY + sy + 1;
            if (checkY >= 8 || (checkY >= 0 && (grid[checkY] & (1 << (posX + sx))))) {
              canFall = false;
            }
          }
        }
      }
      
      // Zeichnen
      ledClear();
      // Grid
      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
          if (grid[y] & (1 << x)) {
            setPixel(x, y, 0x444444);
          }
        }
      }
      // Aktueller Block
      for (int sy = 0; sy < 4; sy++) {
        for (int sx = 0; sx < 4; sx++) {
          if (shapes[type][sy] & (1 << (3 - sx))) {
            int px = posX + sx, py = posY + sy;
            if (py >= 0 && py < 8 && px >= 0 && px < 8) {
              setPixel(px, py, col);
            }
          }
        }
      }
      ledShowSafe();
      
      if (!canFall) {
        // Block fixieren
        for (int sy = 0; sy < 4; sy++) {
          for (int sx = 0; sx < 4; sx++) {
            if (shapes[type][sy] & (1 << (3 - sx))) {
              int py = posY + sy;
              if (py >= 0 && py < 8) {
                grid[py] |= (1 << (posX + sx));
              }
            }
          }
        }
        break;
      }
      
      delay(80);
    }
  }
  
  // Zeilen blinken lassen
  for (int f = 0; f < 6; f++) {
    ledClear();
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        if (grid[y] & (1 << x)) {
          setPixel(x, y, (f % 2 == 0) ? rainbow(y * 30) : 0x222222);
        }
      }
    }
    ledShowSafe();
    delay(100);
  }
  
  animEnd();
}

// Pac-Man Animation
void animPacMan() {
  animStart();
  
  int pacX = -2;
  int mouthOpen = 0;
  int ghostX = -6;
  
  for (int f = 0; f < 50; f++) {
    ledClear();
    
    // Punkte
    for (int x = 0; x < 8; x += 2) {
      if (x > pacX + 2) {
        setPixel(x, 3, 0xFFFF00);
        setPixel(x, 4, 0xFFFF00);
      }
    }
    
    // Pac-Man
    mouthOpen = (f / 3) % 2;
    for (int dx = 0; dx < 5; dx++) {
      for (int dy = 0; dy < 5; dy++) {
        int px = pacX + dx, py = 2 + dy;
        if (px >= 0 && px < 8) {
          float dist = sqrt((dx - 2) * (dx - 2) + (dy - 2) * (dy - 2));
          bool isMouth = mouthOpen && (dx > 2) && (abs(dy - 2) <= 1);
          if (dist <= 2.3f && !isMouth) {
            setPixel(px, py, 0xFFFF00);
          }
        }
      }
    }
    // Auge
    if (pacX + 1 >= 0 && pacX + 1 < 8) setPixel(pacX + 2, 2, 0x000000);
    
    // Geist
    for (int dx = 0; dx < 4; dx++) {
      for (int dy = 0; dy < 5; dy++) {
        int gx = ghostX + dx, gy = 2 + dy;
        if (gx >= 0 && gx < 8) {
          if (dy < 4 || (dx % 2 == f / 4 % 2)) {
            setPixel(gx, gy, 0xFF0000);
          }
        }
      }
    }
    // Geist-Augen
    if (ghostX + 1 >= 0 && ghostX + 1 < 8) setPixel(ghostX + 1, 3, 0xFFFFFF);
    if (ghostX + 2 >= 0 && ghostX + 2 < 8) setPixel(ghostX + 2, 3, 0xFFFFFF);
    
    pacX++;
    ghostX++;
    if (pacX > 10) { pacX = -4; ghostX = -8; }
    
    ledShowSafe();
    delay(70);
  }
  
  animEnd();
}

// Space Invaders
void animInvaders() {
  animStart();
  
  // Invader-Sprites (5x3)
  const uint8_t invader1[3] = {0b01010, 0b11111, 0b10101};
  const uint8_t invader2[3] = {0b10001, 0b01110, 0b01010};
  
  int invaderX = 1;
  int invaderY = 0;
  int dirX = 1;
  int shipX = 3;
  int bulletY = -1;
  int bulletX = 0;
  int frame = 0;
  
  for (int f = 0; f < 70; f++) {
    ledClear();
    frame = (f / 5) % 2;
    
    // Invaders zeichnen (2 Reihen)
    for (int row = 0; row < 2; row++) {
      const uint8_t* sprite = (row == 0) ? invader1 : invader2;
      for (int sy = 0; sy < 3; sy++) {
        for (int sx = 0; sx < 5; sx++) {
          if (sprite[sy] & (1 << (4 - sx))) {
            int px = invaderX + sx + (frame && sy == 2 ? (sx % 2 ? -1 : 1) : 0);
            int py = invaderY + row * 3 + sy;
            if (px >= 0 && px < 8 && py >= 0 && py < 8) {
              setPixel(px, py, row == 0 ? 0x00FF00 : 0x00FFFF);
            }
          }
        }
      }
    }
    
    // Invader bewegen
    if (f % 8 == 0) {
      invaderX += dirX;
      if (invaderX <= 0 || invaderX >= 3) {
        dirX = -dirX;
        invaderY++;
      }
    }
    
    // Spieler-Schiff
    setPixel(shipX, 7, 0xFFFFFF);
    setPixel(shipX - 1, 7, 0x888888);
    setPixel(shipX + 1, 7, 0x888888);
    
    // Schiff bewegen
    if (f % 4 == 0) {
      if (random(3) == 0) shipX += random(-1, 2);
      shipX = constrain(shipX, 1, 6);
    }
    
    // Schießen
    if (bulletY < 0 && random(5) == 0) {
      bulletY = 6;
      bulletX = shipX;
    }
    if (bulletY >= 0) {
      setPixel(bulletX, bulletY, 0xFFFF00);
      bulletY--;
    }
    
    ledShowSafe();
    delay(60);
  }
  
  animEnd();
}

// Herz pulsierend
void animHeart() {
  animStart();
  
  const uint8_t heart[8] = {
    0b01101100,
    0b11111110,
    0b11111110,
    0b11111110,
    0b01111100,
    0b00111000,
    0b00010000,
    0b00000000
  };
  
  for (int pulse = 0; pulse < 8; pulse++) {
    // Größer werden
    for (int size = 0; size <= 2; size++) {
      ledClear();
      uint8_t br = 150 + size * 50;
      
      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
          if (heart[y] & (1 << (7 - x))) {
            setPixel(x, y, ledColor(br, 0, br * 0.3f));
          }
        }
      }
      // Glow-Effekt
      if (size > 0) {
        for (int y = 0; y < 8; y++) {
          for (int x = 0; x < 8; x++) {
            if (!(heart[y] & (1 << (7 - x)))) {
              // Nachbar-Check für Glow
              bool hasNeighbor = false;
              for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                  int ny = y + dy, nx = x + dx;
                  if (ny >= 0 && ny < 8 && nx >= 0 && nx < 8) {
                    if (heart[ny] & (1 << (7 - nx))) hasNeighbor = true;
                  }
                }
              }
              if (hasNeighbor) {
                setPixel(x, y, ledColor(50 * size, 0, 15 * size));
              }
            }
          }
        }
      }
      ledShowSafe();
      delay(60);
    }
    
    // Kleiner werden
    for (int size = 2; size >= 0; size--) {
      ledClear();
      uint8_t br = 150 + size * 50;
      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
          if (heart[y] & (1 << (7 - x))) {
            setPixel(x, y, ledColor(br, 0, br * 0.3f));
          }
        }
      }
      ledShowSafe();
      delay(60);
    }
  }
  
  animEnd();
}

// DNA-Helix
void animDNA() {
  animStart();
  
  for (int f = 0; f < 60; f++) {
    ledClear();
    
    for (int y = 0; y < 8; y++) {
      float phase = (y + f * 0.3f) * 0.8f;
      int x1 = 3.5f + sin(phase) * 2.5f;
      int x2 = 3.5f - sin(phase) * 2.5f;
      
      // Stränge
      setPixel(constrain(x1, 0, 7), y, 0x00FFFF);
      setPixel(constrain(x2, 0, 7), y, 0xFF00FF);
      
      // Verbindungen (Basenpaare)
      if (y % 2 == 0) {
        int minX = min(x1, x2) + 1;
        int maxX = max(x1, x2) - 1;
        for (int x = minX; x <= maxX; x++) {
          if (x >= 0 && x < 8) {
            setPixel(x, y, ledColor(100, 255, 100));
          }
        }
      }
    }
    
    ledShowSafe();
    delay(50);
  }
  
  animEnd();
}

// Sanduhr
void animHourglass() {
  animStart();
  
  // Rahmen der Sanduhr
  const uint8_t frame[8] = {
    0b11111111,
    0b01000010,
    0b00100100,
    0b00011000,
    0b00011000,
    0b00100100,
    0b01000010,
    0b11111111
  };
  
  // Sand fällt
  int sandTop = 1;
  int sandBottom = 7;
  
  for (int f = 0; f < 40; f++) {
    ledClear();
    
    // Rahmen zeichnen
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        if (frame[y] & (1 << (7 - x))) {
          setPixel(x, y, 0x886622);
        }
      }
    }
    
    // Sand oben (wird weniger)
    int topSandLevel = max(1, 4 - f / 6);
    for (int y = 1; y < topSandLevel; y++) {
      int width = 3 - y;
      for (int x = 3 - width; x <= 4 + width; x++) {
        if (x > 0 && x < 7) {
          setPixel(x, y, 0xFFDD44);
        }
      }
    }
    
    // Fallender Sand in der Mitte
    if (f % 3 == 0 && f < 36) {
      setPixel(3, 3, 0xFFDD44);
      setPixel(4, 4, 0xFFDD44);
    }
    
    // Sand unten (wird mehr)
    int bottomSandLevel = min(3, f / 6);
    for (int y = 7; y > 7 - bottomSandLevel; y--) {
      int dist = 7 - y;
      int width = dist;
      for (int x = 3 - width; x <= 4 + width; x++) {
        if (x > 0 && x < 7) {
          setPixel(x, y, 0xFFDD44);
        }
      }
    }
    
    ledShowSafe();
    delay(80);
  }
  
  // Fertig - umdrehen Animation
  for (int rot = 0; rot < 8; rot++) {
    ledClear();
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        if (frame[y] & (1 << (7 - x))) {
          // Rotation simulieren durch Farbwechsel
          setPixel(x, y, rainbow(rot * 30));
        }
      }
    }
    ledShowSafe();
    delay(80);
  }
  
  animEnd();
}

// Smiley Emotionen
void animSmiley() {
  animStart();
  
  // Verschiedene Gesichtsausdrücke
  // Glücklich
  ledClear();
  setPixel(2, 2, 0xFFFF00); setPixel(5, 2, 0xFFFF00);  // Augen
  setPixel(1, 5, 0xFFFF00); setPixel(2, 6, 0xFFFF00);  // Mund
  setPixel(3, 6, 0xFFFF00); setPixel(4, 6, 0xFFFF00);
  setPixel(5, 6, 0xFFFF00); setPixel(6, 5, 0xFFFF00);
  ledShowSafe();
  delay(500);
  
  // Zwinkern
  ledClear();
  setPixel(2, 2, 0xFFFF00);
  setPixel(4, 2, 0xFFFF00); setPixel(5, 2, 0xFFFF00); setPixel(6, 2, 0xFFFF00);  // Zwinkerndes Auge
  setPixel(1, 5, 0xFFFF00); setPixel(2, 6, 0xFFFF00);
  setPixel(3, 6, 0xFFFF00); setPixel(4, 6, 0xFFFF00);
  setPixel(5, 6, 0xFFFF00); setPixel(6, 5, 0xFFFF00);
  ledShowSafe();
  delay(200);
  
  // Überrascht
  ledClear();
  dot(1, 1, 0xFFFF00); dot(5, 1, 0xFFFF00);  // Große Augen
  setPixel(3, 5, 0xFFFF00); setPixel(4, 5, 0xFFFF00);  // O-Mund
  setPixel(3, 6, 0xFFFF00); setPixel(4, 6, 0xFFFF00);
  setPixel(2, 5, 0xFFFF00); setPixel(5, 5, 0xFFFF00);
  ledShowSafe();
  delay(500);
  
  // Traurig
  ledClear();
  setPixel(2, 2, 0x4444FF); setPixel(5, 2, 0x4444FF);  // Blaue Augen
  setPixel(2, 4, 0x00AAFF);  // Träne
  setPixel(1, 6, 0x4444FF); setPixel(2, 5, 0x4444FF);  // Trauriger Mund
  setPixel(3, 5, 0x4444FF); setPixel(4, 5, 0x4444FF);
  setPixel(5, 5, 0x4444FF); setPixel(6, 6, 0x4444FF);
  ledShowSafe();
  delay(500);
  
  // Herzaugen
  for (int f = 0; f < 10; f++) {
    ledClear();
    uint32_t eyeCol = (f % 2 == 0) ? 0xFF0066 : 0xFF3388;
    // Herz-Augen
    setPixel(1, 2, eyeCol); setPixel(3, 2, eyeCol);
    setPixel(2, 3, eyeCol);
    setPixel(5, 2, eyeCol); setPixel(7, 2, eyeCol);
    setPixel(6, 3, eyeCol);
    // Glücklicher Mund
    setPixel(2, 6, 0xFF6688); setPixel(3, 6, 0xFF6688);
    setPixel(4, 6, 0xFF6688); setPixel(5, 6, 0xFF6688);
    ledShowSafe();
    delay(150);
  }
  
  // Cool (Sonnenbrille)
  ledClear();
  for (int x = 1; x < 7; x++) setPixel(x, 2, 0x222222);  // Brillengestell
  dot(1, 2, 0x222222); dot(4, 2, 0x222222);  // Brillengläser
  setPixel(2, 6, 0xFFFF00); setPixel(3, 5, 0xFFFF00);  // Grinsen
  setPixel(4, 5, 0xFFFF00); setPixel(5, 6, 0xFFFF00);
  ledShowSafe();
  delay(600);
  
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
    selectedAnim = random(25);
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
    case 12: animPingPong(); break;
    case 13: animSnake(); break;
    case 14: animNuke(); break;
    case 15: animFraunhofer(); break;
    case 16: animRocket(); break;
    case 17: animPP(); break;
    case 18: animSkyscraper(); break;
    case 19: animTetris(); break;
    case 20: animPacMan(); break;
    case 21: animInvaders(); break;
    case 22: animHeart(); break;
    case 23: animDNA(); break;
    case 24: animHourglass(); break;
    case 25: animSmiley(); break;
    case 26: animWindows(); break;
    case 27: animCar(); break;
    case 28: animNikolaus(); break;
    default: animBounce();
  }
}

// ============================================
// BOOTUP
// ============================================
void bootAnim() {
  cpuFast();
  ledSetBrightness(BRIGHTNESS_BOOTUP);

  // Matrix-Regen (bestehend)
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

  // Regenbogen-Welle (bestehend)
  for (int w = 0; w < 40; w++) {
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++)
        setPixel(x, y, rainbow((x * 20 + y * 20 + w * 8) % 256));
    ledShowSafe();
    delay(22);
  }

  // === NEU: Spiral einwärts ===
  for (int r = 4; r >= 0; r--) {
    for (int i = 0; i < (8 - r * 2); i++) {
      // Oben
      setPixel(r + i, r, rainbow((i * 30) % 256));
      ledShowSafe(); delay(15);
    }
    for (int i = 0; i < (7 - r * 2); i++) {
      // Rechts
      setPixel(7 - r, r + 1 + i, rainbow((i * 30 + 60) % 256));
      ledShowSafe(); delay(15);
    }
    for (int i = 0; i < (7 - r * 2); i++) {
      // Unten
      setPixel(6 - r - i, 7 - r, rainbow((i * 30 + 120) % 256));
      ledShowSafe(); delay(15);
    }
    for (int i = 0; i < (6 - r * 2); i++) {
      // Links
      setPixel(r, 6 - r - i, rainbow((i * 30 + 180) % 256));
      ledShowSafe(); delay(15);
    }
  }

  // === NEU: Explosions-Burst ===
  for (int burst = 0; burst < 3; burst++) {
    ledFill(0xFFFFFF);
    ledSetBrightness(BRIGHTNESS_BOOTUP * 2);  // DOPPELT SO HELL
    ledShowSafe();
    delay(30);
    ledClear();
    ledSetBrightness(BRIGHTNESS_BOOTUP);
    ledShowSafe();
    delay(30);
  }

  // === NEU: Würfel-Symbole morphen ===
  for (int morph = 0; morph < 12; morph++) {
    ledClear();
    uint8_t dieValue = (morph % 6) + 1;
    drawDieRaw(dieValue, rainbow(morph * 20));
    ledShowSafe();
    delay(100);
  }

  // Lauftext - WENIGER HELL (von 0xFFFFFF auf 0x444444)
  ledSetBrightness(BRIGHTNESS_BOOTUP / 3);  // Text dimmer
  scrollText(MOTD, 0x666666, 45);  // Dunklere Farbe
  ledSetBrightness(BRIGHTNESS_BOOTUP);
  
  delay(100);

  // === NEU: Plasma-Übergang ===
  for (int t = 0; t < 25; t++) {
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        float v = sin(x * 0.5f + t * 0.15f) + sin(y * 0.5f + t * 0.15f);
        setPixel(x, y, hsv((uint8_t)((v + 2) * 50) % 256, 255, 200));
      }
    ledShowSafe();
    delay(30);
  }

  // Würfel 1-6 durchlaufen 
  static const uint32_t cols[] = { 0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x0000FF, 0x8000FF };
  for (int d = 1; d <= 6; d++) {
    drawDie(d, cols[d - 1]);
    delay(200);
  }

  // === VERSTÄRKTE FLASHES - DOPPELT SO HELL ===
  ledSetBrightness(BRIGHTNESS_BOOTUP * 2);  // Doppelte Helligkeit
  for (int f = 0; f < 5; f++) {
    ledFill(0xFFFFFF);
    ledShowSafe();
    delay(30);  // Kürzer = intensiver
    ledClear();
    ledShowSafe();
    delay(30);
  }
  
  // Finaler Super-Flash
  ledSetBrightness(min(255, BRIGHTNESS_BOOTUP * 3));
  ledFill(0xFFFFFF);
  ledShowSafe();
  delay(50);

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
  // displayPhase NICHT zurücksetzen! Das stört die 3er-Rotation

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
    return;
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
  
  // Prüfen ob Batterie-Warnung angezeigt werden soll
  bool showBatteryWarning = !isUSBPower() && 
                            (batteryVoltage < BATTERY_LOW) && 
                            (batteryVoltage >= BATTERY_CRITICAL);
  
  if (showBatteryWarning) {
    // 3-Phasen-Rotation: Würfel1 -> Würfel2 -> Batterie
    displayPhase++;
    if (displayPhase > 2) displayPhase = 0;
    
    if (displayPhase == 0) {
      drawDie(dice1Result, DICE1_COLOR);
    } else if (displayPhase == 1) {
      drawDie(dice2Result, DICE2_COLOR);
    } else {
      drawBattLow();
    }
  } else {
    // Normale 2-Phasen-Rotation: Würfel1 <-> Würfel2
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
  printCentered(MOTD);
  printCentered("🎲 HILFE 🎲");
  printLine('=');
  dbgln();
  dbgln("  STEUERUNG:");
  dbgln("  ────────────────────────────────────────────");
  dbgln("  r          Würfeln");
  dbgln("  a + 0-28   Animation wählen (z.B. a15)");
  dbgln("  p          Animation Preview");
  dbgln("  b          Boot-Animation");
  dbgln("  g          Gag-Animation (9er Pasch)");
  dbgln("  1-6        Würfel 1 setzen");
  dbgln("  9          9er Pasch anzeigen");
  dbgln("  + / -      Anzeigeintervall");
  dbgln("  c          Display löschen");
  dbgln();
  dbgln("  INFO:");
  dbgln("  ────────────────────────────────────────────");
  dbgln("  h / ?      Hilfe");
  dbgln("  i          System-Info");
  dbgln("  v          Batterie-Status");
  dbgln("  s          Sensor-Daten (5s)");
  dbgln("  t          Statistiken");
  dbgln("  x          Statistiken Reset");
  dbgln();
  dbgln("  ANIMATIONEN (a + Nummer):");
  dbgln("  ────────────────────────────────────────────");
  dbgln("   0 Bounce      10 Pulse       20 PacMan");
  dbgln("   1 Spiral      11 Random      21 Invaders");
  dbgln("   2 Scatter     12 PingPong    22 Heart");
  dbgln("   3 Spin        13 Snake       23 DNA");
  dbgln("   4 Plasma      14 Nuke        24 Hourglass");
  dbgln("   5 Explode     15 Fraunhofer  25 Smiley");
  dbgln("   6 Glitch      16 Rocket      26 Windows");
  dbgln("   7 Wave        17 PP          27 Car");
  dbgln("   8 Firework    18 Skyscraper  28 Nikolaus");
  dbgln("   9 Matrix      19 Tetris");
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

  // Warten auf zweite Ziffer einer zweistelligen Zahl
  if (waitingForSecondDigit) {
    waitingForSecondDigit = false;
    waitingForAnimNumber = false;
    
    if (c >= '0' && c <= '9') {
      // Zweistellige Zahl: erste*10 + zweite
      currentAnimation = firstAnimDigit * 10 + (c - '0');
    } else {
      // Keine zweite Ziffer -> einstellig verwenden
      currentAnimation = firstAnimDigit;
      // Das aktuelle Zeichen muss auch verarbeitet werden!
      if (c != '\n' && c != '\r' && c != ' ') {
        cmd(c);  // Rekursiv verarbeiten
        return;
      }
    }
    
    // Animation validieren
    if (currentAnimation > 28) {
      dbg("❌ Animation ");
      dbg(currentAnimation);
      dbgln(" existiert nicht! (max 28)");
      currentAnimation = 11;  // Fallback zu Random
      return;
    }
    
    dbg("✅ Animation ");
    dbg(currentAnimation);
    dbg(": ");
    dbgln(animationNames[currentAnimation]);
    return;
  }

  // Warten auf erste Ziffer
  if (waitingForAnimNumber) {
    if (c >= '0' && c <= '9') {
      firstAnimDigit = c - '0';
      waitingForSecondDigit = true;
      animInputTime = millis();
      // Kurz auf zweite Ziffer warten
      delay(100);  // 100ms warten
      if (Serial.available()) {
        char next = Serial.peek();
        if (next >= '0' && next <= '9') {
          // Zweite Ziffer vorhanden, wird im nächsten Loop gelesen
          return;
        }
      }
      // Keine zweite Ziffer -> einstellig
      waitingForSecondDigit = false;
      waitingForAnimNumber = false;
      currentAnimation = firstAnimDigit;
      
      dbg("✅ Animation ");
      dbg(currentAnimation);
      dbg(": ");
      dbgln(animationNames[currentAnimation]);
    } else {
      dbgln("❌ Ungültig! Bitte Zahl 0-28 eingeben");
      waitingForAnimNumber = false;
    }
    return;
  }

  // Normale Kommando-Verarbeitung
  switch (c) {
    case 'r':
    case 'R': roll(0); break;
    
    case 'a':
    case 'A':
      dbgln("📝 Animation wählen (0-28):");
      dbgln("   0=Bounce,   1=Spiral,    2=Scatter");
      dbgln("   3=Spin,     4=Plasma,    5=Explode");
      dbgln("   6=Glitch,   7=Wave,      8=Firework");
      dbgln("   9=Matrix,  10=Pulse,    11=Random");
      dbgln("  12=PingPong,13=Snake,    14=Nuke");
      dbgln("  15=Fraunhofer,16=Rocket, 17=PP");
      dbgln("  18=Skyscraper,19=Tetris, 20=PacMan");
      dbgln("  21=Invaders,22=Heart,    23=DNA");
      dbgln("  24=Hourglass,25=Smiley,  26=Windows");
      dbgln("  27=Car,     28=Nikolaus");
      dbg("Aktuelle Animation: ");
      dbgln(currentAnimation);
      waitingForAnimNumber = true;
      break;
      
    case 'p':
    case 'P':
      dbg("▶️ Preview Animation ");
      dbgln(currentAnimation);
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


  imuOK = initIMU();

  handleWakeup();

  batteryVoltage = readBatt();
  batteryPercent = battPct(batteryVoltage);

  dbgln();
  printLine('=');
  printCentered(MOTD);
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