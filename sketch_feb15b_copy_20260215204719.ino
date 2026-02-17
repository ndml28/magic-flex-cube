#include <Adafruit_NeoPixel.h>
#include <QMI8658.h>


// ============================================
// BATTERY MONITORING
// ============================================
#define BATTERY_PIN 5
#define BATTERY_READ_INTERVAL 5000    // ms - how often to check battery
#define POLL_INTERVALL_LOOP 20 // ms - main loop

// Voltage thresholds
#define BATTERY_FULL 4.2              // USB Powered
#define BATTERY_NOMINAL 3.7           // 100% - required to exit sleep
#define BATTERY_LOW 3.4               // ~50% - warning symbol
#define BATTERY_CRITICAL 3.2          // ~5% - deep sleep mode
#define BATTERY_SHUTDOWN 3.0          // 0% - won't wake up

// ADC configuration (ESP32)
#define ADC_RESOLUTION 4095.0
#define ADC_REF_VOLTAGE 3.3
#define VOLTAGE_DIVIDER_RATIO 2.0

// Low power settings
#define BRIGHTNESS_LOW_POWER 1
#define DEEP_SLEEP_DURATION 10        // seconds between wake checks
#define BATTERY_SYMBOL_BLINK 1000     // ms - blink interval for low battery

// Deep sleep
#define uS_TO_S_FACTOR 1000000ULL     // Conversion factor for micro seconds to seconds
// LED Matrix config
#define PIN 14
#define NUMPIXELS 64
#define BUTTON_PIN 2
#define I2C_SDA       11
#define I2C_SCL       12

// Motion detection thresholds
#define SHAKE_THRESHOLD 1200.0
#define SHAKE_COOLDOWN 500

// ============================================
// CONFIGURABLE SETTINGS
// ============================================
#define DEFAULT_DISPLAY_INTERVAL 1000
#define DEFAULT_ANIMATION 4

// Brightness settings
#define BRIGHTNESS_NORMAL 2
#define BRIGHTNESS_ANIMATION 20
#define BRIGHTNESS_BOOTUP 30

// Dice Colors (RGB values)
#define DICE1_R 255
#define DICE1_G 0
#define DICE1_B 0

#define DICE2_R 0
#define DICE2_G 0
#define DICE2_B 255

#define BG_R 0
#define BG_G 0
#define BG_B 0
// ============================================

Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
QMI8658 imu;

// Colors
uint32_t COLOR_BG;
uint32_t COLOR_DICE1;
uint32_t COLOR_DICE2;

// Dice results
uint8_t dice1Result = 1;
uint8_t dice2Result = 1;

// Display state
bool isDisplayingResult = false;
uint8_t currentDisplayDice = 1;
unsigned long lastDisplaySwitch = 0;
unsigned long displayInterval = DEFAULT_DISPLAY_INTERVAL;

// Battery monitoring
float batteryVoltage = 0.0;
uint8_t batteryPercent = 100;
bool criticalPowerMode = false;
unsigned long lastBatteryRead = 0;
bool lowPowerMode = false;
bool showingBatteryWarning = false;
unsigned long lastBatteryBlink = 0;
bool batteryBlinkState = false;

// Deep sleep tracking
RTC_DATA_ATTR int bootCount = 0;      // Survives deep sleep
RTC_DATA_ATTR bool wasInDeepSleep = false;


// Animation settings
uint8_t currentAnimation = DEFAULT_ANIMATION;
const uint8_t NUM_ANIMATIONS = 5;
const char* animationNames[] = {
  "Bounce",
  "Spiral", 
  "Scatter",
  "Spin",
  "Random"
};

// Motion tracking
float lastAccelMagnitude = 1000.0;
unsigned long lastRollTime = 0;

// CLI state
bool waitingForAnimNumber = false;

// ============================================
// COLOR HELPER FUNCTIONS
// ============================================

// Generate rainbow color from position (0-255)
uint32_t rainbowColor(uint8_t pos) {
  pos = 255 - pos;
  if (pos < 85) {
    return pixels.Color(255 - pos * 3, 0, pos * 3);
  }
  if (pos < 170) {
    pos -= 85;
    return pixels.Color(0, pos * 3, 255 - pos * 3);
  }
  pos -= 170;
  return pixels.Color(pos * 3, 255 - pos * 3, 0);
}

// Get random bright color
uint32_t randomBrightColor() {
  uint8_t colorType = random(0, 7);
  switch (colorType) {
    case 0: return pixels.Color(255, 0, 0);     // Red
    case 1: return pixels.Color(0, 255, 0);     // Green
    case 2: return pixels.Color(0, 0, 255);     // Blue
    case 3: return pixels.Color(255, 255, 0);   // Yellow
    case 4: return pixels.Color(0, 255, 255);   // Cyan
    case 5: return pixels.Color(255, 0, 255);   // Magenta
    case 6: return pixels.Color(255, 128, 0);   // Orange
    default: return pixels.Color(255, 255, 255); // White
  }
}


// ============================================
// BATTERY SYMBOL DISPLAY
// ============================================

// Draw battery outline on 8x8 matrix
void drawBatterySymbol(uint8_t fillLevel, uint32_t outlineColor, uint32_t fillColor) {
  clearDisplay();
  
  // Battery outline (6x4 with nub)
  // Shape:
  //   ██████░░
  //   █    █░█
  //   █    █░█
  //   █    █░█
  //   █    █░█
  //   ██████░░
  
  // Top edge
  for (int x = 0; x < 6; x++) setPixel(x, 1, outlineColor);
  
  // Bottom edge  
  for (int x = 0; x < 6; x++) setPixel(x, 6, outlineColor);
  
  // Left edge
  for (int y = 1; y <= 6; y++) setPixel(0, y, outlineColor);
  
  // Right edge
  for (int y = 1; y <= 6; y++) setPixel(5, y, outlineColor);
  
  // Battery nub (positive terminal)
  setPixel(6, 2, outlineColor);
  setPixel(6, 3, outlineColor);
  setPixel(6, 4, outlineColor);
  setPixel(6, 5, outlineColor);
  setPixel(7, 3, outlineColor);
  setPixel(7, 4, outlineColor);
  
  // Fill level (0-4 bars)
  // Fill area is x: 1-4, y: 2-5 (4 columns)
  for (int i = 0; i < fillLevel && i < 4; i++) {
    int x = 1 + i;
    for (int y = 2; y <= 5; y++) {
      setPixel(x, y, fillColor);
    }
  }
  
  pixels.show();
}



// ============================================
// DEEP SLEEP FUNCTIONS
// ============================================

void printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("   Wakeup: External signal (RTC_IO)");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("   Wakeup: External signal (RTC_CNTL)");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("   Wakeup: Timer");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      Serial.println("   Wakeup: Touchpad");
      break;
    case ESP_SLEEP_WAKEUP_ULP:
      Serial.println("   Wakeup: ULP program");
      break;
    default:
      Serial.println("   Wakeup: Power on / Reset");
      break;
  }
}

void enterDeepSleep() {
  Serial.println();
  Serial.println("😴 ════════════════════════════════════");
  Serial.println("   CRITICAL BATTERY - Entering deep sleep");
  Serial.print("   Will check again in ");
  Serial.print(DEEP_SLEEP_DURATION);
  Serial.println(" seconds");
  Serial.println("   Need >");
  Serial.print(BATTERY_NOMINAL);
  Serial.println("V to resume operation");
  Serial.println("════════════════════════════════════ 😴");
  Serial.println();
  
  // Show empty battery symbol before sleep
  pixels.setBrightness(BRIGHTNESS_LOW_POWER);
  drawEmptyBattery();
  delay(2000);
  
  // Fade out
  for (int b = BRIGHTNESS_LOW_POWER; b >= 0; b--) {
    pixels.setBrightness(b);
    pixels.show();
    delay(100);
  }
  
  // Turn off display completely
  clearDisplay();
  pixels.show();
  
  // Mark that we're going to deep sleep
  wasInDeepSleep = true;
  
  // Configure wakeup timer
  esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION * uS_TO_S_FACTOR);
  
  Serial.println("Going to sleep now...");
  Serial.flush();
  
  // Enter deep sleep
  esp_deep_sleep_start();
}

// Check battery on wakeup and decide what to do
bool checkBatteryOnWakeup() {
  // Quick battery read
  delay(100);  // Let ADC stabilize
  batteryVoltage = readBatteryVoltage();
  batteryPercent = voltageToPercent(batteryVoltage);
  
  Serial.println();
  Serial.println("🔋 Wake-up battery check:");
  Serial.print("   Voltage: ");
  Serial.print(batteryVoltage, 2);
  Serial.println("V");
  Serial.print("   Required: >");
  Serial.print(BATTERY_NOMINAL, 1);
  Serial.println("V");
  
  if (batteryVoltage >= BATTERY_NOMINAL) {
    Serial.println("   ✅ Battery OK - Resuming normal operation!");
    return true;  // Continue with normal boot
  } else if (batteryVoltage <= BATTERY_SHUTDOWN) {
    Serial.println("   💀 Battery too low - Staying in sleep");
    return false;  // Go back to sleep
  } else {
    Serial.println("   ⚠️ Battery still low - Going back to sleep");
    return false;  // Go back to sleep
  }
}

// Handle wakeup from deep sleep
void handleWakeup() {
  bootCount++;
  
  Serial.println();
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.print("   Boot count: ");
  Serial.println(bootCount);
  printWakeupReason();
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  
  if (wasInDeepSleep) {
    Serial.println("   Woke from deep sleep - checking battery...");
    
    // Initialize just enough to read battery and show display
    pixels.begin();
    pixels.setBrightness(BRIGHTNESS_LOW_POWER);
    
    if (!checkBatteryOnWakeup()) {
      // Show brief battery status before sleeping again
      drawEmptyBattery();
      delay(1000);
      
      // Go back to sleep
      enterDeepSleep();
      // Never reaches here
    }
    
    // Battery OK - continue with normal boot
    wasInDeepSleep = false;
    Serial.println("   Continuing to normal boot sequence...");
  }
}


// Draw empty red battery (critical warning)
void drawEmptyBattery() {
  uint32_t red = pixels.Color(255, 0, 0);
  uint32_t darkRed = pixels.Color(60, 0, 0);
  drawBatterySymbol(0, red, darkRed);
}

// Draw low battery with 1 bar blinking
void drawLowBattery(bool blinkOn) {
  uint32_t red = pixels.Color(255, 0, 0);
  uint32_t orange = pixels.Color(255, 80, 0);
  
  if (blinkOn) {
    drawBatterySymbol(1, red, orange);
  } else {
    drawBatterySymbol(0, red, red);
  }
}

// Draw battery based on percentage
void drawBatteryLevel() {
  uint32_t green = pixels.Color(0, 255, 0);
  uint32_t yellow = pixels.Color(255, 255, 0);
  uint32_t orange = pixels.Color(255, 128, 0);
  uint32_t red = pixels.Color(255, 0, 0);
  
  uint8_t bars;
  uint32_t outlineColor;
  uint32_t fillColor;
  
  if (batteryPercent > 75) {
    bars = 4;
    outlineColor = green;
    fillColor = green;
  } else if (batteryPercent > 50) {
    bars = 3;
    outlineColor = green;
    fillColor = green;
  } else if (batteryPercent > 25) {
    bars = 2;
    outlineColor = yellow;
    fillColor = yellow;
  } else if (batteryPercent > 10) {
    bars = 1;
    outlineColor = orange;
    fillColor = orange;
  } else {
    bars = 0;
    outlineColor = red;
    fillColor = red;
  }
  
  drawBatterySymbol(bars, outlineColor, fillColor);
}

// Update battery warning display (called from loop)
void updateBatteryWarningDisplay() {
  if (!showingBatteryWarning) return;
  
  if (millis() - lastBatteryBlink >= BATTERY_SYMBOL_BLINK) {
    lastBatteryBlink = millis();
    batteryBlinkState = !batteryBlinkState;
    
    pixels.setBrightness(BRIGHTNESS_LOW_POWER);
    drawLowBattery(batteryBlinkState);
  }
}


// Interpolate between two colors
uint32_t lerpColor(uint32_t color1, uint32_t color2, float t) {
  uint8_t r1 = (color1 >> 16) & 0xFF;
  uint8_t g1 = (color1 >> 8) & 0xFF;
  uint8_t b1 = color1 & 0xFF;
  
  uint8_t r2 = (color2 >> 16) & 0xFF;
  uint8_t g2 = (color2 >> 8) & 0xFF;
  uint8_t b2 = color2 & 0xFF;
  
  uint8_t r = r1 + (r2 - r1) * t;
  uint8_t g = g1 + (g2 - g1) * t;
  uint8_t b = b1 + (b2 - b1) * t;
  
  return pixels.Color(r, g, b);
}

// ============================================
// BASIC DISPLAY FUNCTIONS
// ============================================

uint16_t xy(uint8_t x, uint8_t y) {
  return y * 8 + x;
}

void setPixel(uint8_t x, uint8_t y, uint32_t color) {
  if (x < 8 && y < 8) {
    pixels.setPixelColor(xy(x, y), color);
  }
}

void setXY(uint8_t x, uint8_t y, uint32_t color) {
  setPixel(x, y, color);
}

void drawDot(uint8_t x, uint8_t y, uint32_t color) {
  setXY(x, y, color);
  setXY(x + 1, y, color);
  setXY(x, y + 1, color);
  setXY(x + 1, y + 1, color);
}

void clearDisplay() {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, COLOR_BG);
  }
}

void fillDisplay(uint32_t color) {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, color);
  }
}

void drawDie(uint8_t value, uint32_t color) {
  clearDisplay();
  
  uint8_t leftX = 1, centerX = 3, rightX = 5;
  uint8_t topY = 1, midY = 3, botY = 5;

  switch (value) {
    case 1:
      drawDot(centerX, midY, color);
      break;
    case 2:
      drawDot(leftX, topY, color);
      drawDot(rightX, botY, color);
      break;
    case 3:
      drawDot(leftX, topY, color);
      drawDot(centerX, midY, color);
      drawDot(rightX, botY, color);
      break;
    case 4:
      drawDot(leftX, topY, color);
      drawDot(rightX, topY, color);
      drawDot(leftX, botY, color);
      drawDot(rightX, botY, color);
      break;
    case 5:
      drawDot(leftX, topY, color);
      drawDot(rightX, topY, color);
      drawDot(centerX, midY, color);
      drawDot(leftX, botY, color);
      drawDot(rightX, botY, color);
      break;
    case 6:
      drawDot(leftX, topY - 1, color);
      drawDot(leftX, midY, color);
      drawDot(leftX, botY + 1, color);
      drawDot(rightX, topY - 1, color);
      drawDot(rightX, midY, color);
      drawDot(rightX, botY + 1, color);
      break;
  }
  
  pixels.show();
}

// ============================================
// BOOTUP ANIMATION (10 seconds)
// ============================================

void bootupAnimation() {
  Serial.println("🚀 Starting bootup sequence...");
  
  pixels.setBrightness(BRIGHTNESS_BOOTUP);
  
  // Phase 1: Matrix Rain Effect (2.5 seconds)
  Serial.println("   Phase 1: Matrix Rain");
  uint8_t drops[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t dropSpeed[8];
  uint32_t dropColor[8];
  
  for (int i = 0; i < 8; i++) {
    dropSpeed[i] = random(1, 4);
    dropColor[i] = rainbowColor(random(0, 256));
  }
  
  for (int frame = 0; frame < 60; frame++) {
    clearDisplay();
    
    for (int x = 0; x < 8; x++) {
      if (frame % dropSpeed[x] == 0) {
        drops[x]++;
        if (drops[x] > 12) {
          drops[x] = 0;
          dropColor[x] = rainbowColor(random(0, 256));
          dropSpeed[x] = random(1, 4);
        }
      }
      
      // Draw drop with trail
      for (int t = 0; t < 5; t++) {
        int y = drops[x] - t;
        if (y >= 0 && y < 8) {
          uint8_t brightness = 255 - (t * 50);
          uint8_t r = ((dropColor[x] >> 16) & 0xFF) * brightness / 255;
          uint8_t g = ((dropColor[x] >> 8) & 0xFF) * brightness / 255;
          uint8_t b = (dropColor[x] & 0xFF) * brightness / 255;
          setPixel(x, y, pixels.Color(r, g, b));
        }
      }
    }
    
    pixels.show();
    delay(40);
  }
  
  // Phase 2: Expanding Rings (2 seconds)
  Serial.println("   Phase 2: Expanding Rings");
  for (int ring = 0; ring < 6; ring++) {
    uint32_t ringColor = rainbowColor(ring * 42);
    
    for (int expand = 0; expand <= 4; expand++) {
      clearDisplay();
      
      int r = expand;
      int cx = 3, cy = 3;
      
      // Draw square ring
      for (int x = cx - r; x <= cx + r + 1; x++) {
        setPixel(x, cy - r, ringColor);
        setPixel(x, cy + r + 1, ringColor);
      }
      for (int y = cy - r; y <= cy + r + 1; y++) {
        setPixel(cx - r, y, ringColor);
        setPixel(cx + r + 1, y, ringColor);
      }
      
      pixels.show();
      delay(50);
    }
  }
  
  // Phase 3: Rainbow Wave (2 seconds)
  Serial.println("   Phase 3: Rainbow Wave");
  for (int wave = 0; wave < 80; wave++) {
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        uint8_t colorPos = (x * 20 + y * 20 + wave * 8) % 256;
        setPixel(x, y, rainbowColor(colorPos));
      }
    }
    pixels.show();
    delay(25);
  }
  
  // Phase 4: Dice Showcase (2 seconds)
  Serial.println("   Phase 4: Dice Showcase");
  uint32_t showcaseColors[] = {
    pixels.Color(255, 0, 0),
    pixels.Color(255, 165, 0),
    pixels.Color(255, 255, 0),
    pixels.Color(0, 255, 0),
    pixels.Color(0, 0, 255),
    pixels.Color(128, 0, 255)
  };
  
  for (int d = 1; d <= 6; d++) {
    drawDie(d, showcaseColors[d - 1]);
    delay(300);
  }
  
  // Phase 5: Fireworks Finale (1.5 seconds)
  Serial.println("   Phase 5: Fireworks Finale");
  for (int fw = 0; fw < 5; fw++) {
    // Launch point
    int cx = random(2, 6);
    int cy = random(2, 6);
    uint32_t fwColor = randomBrightColor();
    
    // Explode outward
    for (int radius = 0; radius < 5; radius++) {
      clearDisplay();
      
      // Draw explosion particles
      for (int angle = 0; angle < 8; angle++) {
        float a = angle * PI / 4;
        int px = cx + cos(a) * radius;
        int py = cy + sin(a) * radius;
        
        if (px >= 0 && px < 8 && py >= 0 && py < 8) {
          setPixel(px, py, fwColor);
        }
      }
      
      // Add sparkles
      for (int s = 0; s < 3; s++) {
        setPixel(random(0, 8), random(0, 8), rainbowColor(random(0, 256)));
      }
      
      pixels.show();
      delay(40);
    }
    
    // Fade out
    for (int fade = 0; fade < 5; fade++) {
      for (int i = 0; i < NUMPIXELS; i++) {
        uint32_t c = pixels.getPixelColor(i);
        uint8_t r = ((c >> 16) & 0xFF) * 0.6;
        uint8_t g = ((c >> 8) & 0xFF) * 0.6;
        uint8_t b = (c & 0xFF) * 0.6;
        pixels.setPixelColor(i, pixels.Color(r, g, b));
      }
      pixels.show();
      delay(30);
    }
  }
  
  // Final Flash
  Serial.println("   Finale: Ready Flash");
  for (int flash = 0; flash < 3; flash++) {
    fillDisplay(pixels.Color(255, 255, 255));
    pixels.show();
    delay(100);
    clearDisplay();
    pixels.show();
    delay(100);
  }
  
  // Restore normal brightness
  pixels.setBrightness(BRIGHTNESS_NORMAL);
  clearDisplay();
  pixels.show();
  
  Serial.println("✅ Bootup complete!");
  Serial.println();
}

// ============================================
// ROLL ANIMATIONS (Colorful & Bright)
// ============================================

void setAnimationBrightness() {
  pixels.setBrightness(BRIGHTNESS_ANIMATION);
}

void setNormalBrightness() {
  pixels.setBrightness(BRIGHTNESS_NORMAL);
}

// Animation 0: Colorful Bouncing Balls
void animBounce() {
  setAnimationBrightness();
  
  struct BouncingDot {
    float x, y;
    float vx, vy;
    uint32_t color;
    uint8_t colorOffset;
  };
  
  BouncingDot dots[5];  // More dots!
  
  for (int i = 0; i < 5; i++) {
    dots[i].x = random(1, 7);
    dots[i].y = random(1, 7);
    dots[i].vx = (random(0, 2) == 0 ? 1 : -1) * (0.8 + random(0, 10) / 10.0);
    dots[i].vy = (random(0, 2) == 0 ? 1 : -1) * (0.8 + random(0, 10) / 10.0);
    dots[i].colorOffset = i * 50;
  }
  
  for (int frame = 0; frame < 50; frame++) {
    clearDisplay();
    
    float friction = 1.0 - (frame * 0.012);
    if (friction < 0.3) friction = 0.3;
    
    for (int i = 0; i < 5; i++) {
      // Rainbow color cycling
      dots[i].color = rainbowColor((frame * 5 + dots[i].colorOffset) % 256);
      
      dots[i].x += dots[i].vx * friction;
      dots[i].y += dots[i].vy * friction;
      
      if (dots[i].x <= 0 || dots[i].x >= 6) {
        dots[i].vx *= -1.1;  // Speed boost on bounce!
        dots[i].x = constrain(dots[i].x, 0, 6);
        dots[i].color = randomBrightColor();  // Color change on bounce
      }
      if (dots[i].y <= 0 || dots[i].y >= 6) {
        dots[i].vy *= -1.1;
        dots[i].y = constrain(dots[i].y, 0, 6);
        dots[i].color = randomBrightColor();
      }
      
      int px = (int)dots[i].x;
      int py = (int)dots[i].y;
      setPixel(px, py, dots[i].color);
      setPixel(min(px + 1, 7), py, dots[i].color);
      setPixel(px, min(py + 1, 7), dots[i].color);
      setPixel(min(px + 1, 7), min(py + 1, 7), dots[i].color);
    }
    
    pixels.show();
    delay(35 - frame / 3);
  }
  
  setNormalBrightness();
}

// Animation 1: Rainbow Spiral
void animSpiral() {
  setAnimationBrightness();
  
  const int8_t spiralX[] = {0,1,2,3,4,5,6,7, 7,7,7,7,7,7,7, 6,5,4,3,2,1,0, 0,0,0,0,0,0, 1,2,3,4,5,6, 6,6,6,6,6, 5,4,3,2,1, 1,1,1,1, 2,3,4,5, 5,5,5, 4,3,2, 2,2, 3,4, 4, 3};
  const int8_t spiralY[] = {0,0,0,0,0,0,0,0, 1,2,3,4,5,6,7, 7,7,7,7,7,7,7, 6,5,4,3,2,1, 1,1,1,1,1,1, 2,3,4,5,6, 6,6,6,6,6, 5,4,3,2, 2,2,2,2, 3,4,5, 5,5,5, 4,3, 3,3, 4, 4};
  const int spiralLen = 64;
  
  // Spiral outward with rainbow
  for (int i = spiralLen - 1; i >= 0; i -= 1) {
    for (int j = spiralLen - 1; j >= i; j--) {
      uint32_t c = rainbowColor(((spiralLen - j) * 4) % 256);
      setPixel(spiralX[j], spiralY[j], c);
    }
    pixels.show();
    delay(12);
  }
  
  delay(150);
  
  // Spiral inward with trail
  for (int i = 0; i < spiralLen; i += 1) {
    clearDisplay();
    for (int j = i; j < min(i + 16, spiralLen); j++) {
      float brightness = 1.0 - ((j - i) / 16.0);
      uint32_t c = rainbowColor((j * 8 + i * 2) % 256);
      uint8_t r = ((c >> 16) & 0xFF) * brightness;
      uint8_t g = ((c >> 8) & 0xFF) * brightness;
      uint8_t b = (c & 0xFF) * brightness;
      setPixel(spiralX[j], spiralY[j], pixels.Color(r, g, b));
    }
    pixels.show();
    delay(15);
  }
  
  setNormalBrightness();
}

// Animation 2: Colorful Particle Scatter
void animScatter() {
  setAnimationBrightness();
  
  struct Particle {
    float x, y;
    float targetX, targetY;
    uint32_t color;
    float speed;
  };
  
  Particle particles[16];  // More particles!
  
  for (int i = 0; i < 16; i++) {
    int edge = random(0, 4);
    switch (edge) {
      case 0: particles[i].x = random(0, 8); particles[i].y = -1; break;
      case 1: particles[i].x = random(0, 8); particles[i].y = 8; break;
      case 2: particles[i].x = -1; particles[i].y = random(0, 8); break;
      case 3: particles[i].x = 8; particles[i].y = random(0, 8); break;
    }
    particles[i].targetX = 2 + random(0, 4);
    particles[i].targetY = 2 + random(0, 4);
    particles[i].color = rainbowColor(i * 16);
    particles[i].speed = 0.8 + random(0, 5) / 10.0;
  }
  
  for (int frame = 0; frame < 40; frame++) {
    clearDisplay();
    
    for (int i = 0; i < 16; i++) {
      // Update color over time
      particles[i].color = rainbowColor((i * 16 + frame * 6) % 256);
      
      float progress = (frame * particles[i].speed) / 40.0;
      if (progress > 1.0) progress = 1.0;
      float easing = progress * progress * (3 - 2 * progress);
      
      float currentX = particles[i].x + (particles[i].targetX - particles[i].x) * easing;
      float currentY = particles[i].y + (particles[i].targetY - particles[i].y) * easing;
      
      if (frame < 25) {
        currentX += sin(frame * 0.4 + i) * (1 - progress) * 1.5;
        currentY += cos(frame * 0.4 + i) * (1 - progress) * 1.5;
      }
      
      int px = constrain((int)currentX, 0, 7);
      int py = constrain((int)currentY, 0, 7);
      setPixel(px, py, particles[i].color);
    }
    
    pixels.show();
    delay(30);
  }
  
  // Rainbow flash at end
  for (int flash = 0; flash < 8; flash++) {
    fillDisplay(rainbowColor(flash * 32));
    pixels.show();
    delay(40);
  }
  
  setNormalBrightness();
}

// Animation 3: Rainbow Spinning Vortex
void animSpin() {
  setAnimationBrightness();
  
  const float centerX = 3.5;
  const float centerY = 3.5;
  
  int numDots = 8;
  float angleOffset = 0;
  
  for (int frame = 0; frame < 55; frame++) {
    clearDisplay();
    
    float speed = 0.5 - (frame * 0.007);
    if (speed < 0.08) speed = 0.08;
    angleOffset += speed;
    
    // Multiple rings
    for (int ring = 0; ring < 3; ring++) {
      float radius = 1.0 + ring * 1.2;
      
      // Shrink over time
      if (frame > 35) {
        radius *= (1.0 - (frame - 35) / 30.0);
      }
      
      int dotsInRing = 4 + ring * 2;
      
      for (int i = 0; i < dotsInRing; i++) {
        float angle = angleOffset * (1 + ring * 0.3) + (i * 2 * PI / dotsInRing);
        
        int px = (int)(centerX + cos(angle) * radius);
        int py = (int)(centerY + sin(angle) * radius);
        
        uint32_t color = rainbowColor((frame * 4 + i * 30 + ring * 80) % 256);
        
        px = constrain(px, 0, 6);
        py = constrain(py, 0, 6);
        
        setPixel(px, py, color);
        setPixel(min(px + 1, 7), py, color);
        setPixel(px, min(py + 1, 7), color);
        setPixel(min(px + 1, 7), min(py + 1, 7), color);
      }
    }
    
    pixels.show();
    delay(35);
  }
  
  setNormalBrightness();
}

// Animation 4: Random (picks one)
void rollAnimation() {
  uint8_t animToPlay = currentAnimation;
  
  if (animToPlay == 4) {
    animToPlay = random(0, 4);
    Serial.print("   Random selected: ");
    Serial.println(animationNames[animToPlay]);
  }
  
  switch (animToPlay) {
    case 0: animBounce(); break;
    case 1: animSpiral(); break;
    case 2: animScatter(); break;
    case 3: animSpin(); break;
    default: animBounce(); break;
  }
}

// ============================================
// DICE ROLLING
// ============================================

void rollDice() {
  Serial.println("🎲🎲 Rolling two dice...");
  Serial.print("   Animation: ");
  Serial.println(animationNames[currentAnimation]);
  
  rollAnimation();
  
  dice1Result = random(1, 7);
  dice2Result = random(1, 7);
  
  currentDisplayDice = random(1, 3);
  
  if (currentDisplayDice == 1) {
    drawDie(dice1Result, COLOR_DICE1);
  } else {
    drawDie(dice2Result, COLOR_DICE2);
  }
  
  isDisplayingResult = true;
  lastDisplaySwitch = millis();
  
  Serial.println("┌─────────────────────────────┐");
  Serial.println("│        DICE RESULTS         │");
  Serial.println("├──────────────┬──────────────┤");
  Serial.println("│    DICE 1    │    DICE 2    │");
  Serial.print("│      ");
  Serial.print(dice1Result);
  Serial.print("       │      ");
  Serial.print(dice2Result);
  Serial.println("       │");
  Serial.println("├──────────────┴──────────────┤");
  Serial.print("│         TOTAL: ");
  uint8_t total = dice1Result + dice2Result;
  if (total < 10) Serial.print(" ");
  Serial.print(total);
  Serial.println("           │");
  Serial.println("└─────────────────────────────┘");
  Serial.println();
  
  lastRollTime = millis();
}

// ============================================
// CLI FUNCTIONS
// ============================================

void showHelp() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════╗");
  Serial.println("║       DUAL MOTION DICE - HELP         ║");
  Serial.println("╠═══════════════════════════════════════╣");
  Serial.println("║  ROLL COMMANDS                        ║");
  Serial.println("║    SHAKE BOARD  - Roll both dice      ║");
  Serial.println("║    r / R        - Roll via serial     ║");
  Serial.println("║    BUTTON       - Roll dice           ║");
  Serial.println("╠═══════════════════════════════════════╣");
  Serial.println("║  ANIMATION COMMANDS                   ║");
  Serial.println("║    a0 - Bounce (colorful balls)       ║");
  Serial.println("║    a1 - Spiral (rainbow vortex)       ║");
  Serial.println("║    a2 - Scatter (particle storm)      ║");
  Serial.println("║    a3 - Spin (rotating rings)         ║");
  Serial.println("║    a4 - Random (surprise me!)         ║");
  Serial.println("║    a  - Show animation list           ║");
  Serial.println("║    p  - Preview current animation     ║");
  Serial.println("║    b  - Replay bootup animation       ║");
  Serial.println("╠═══════════════════════════════════════╣");
  Serial.println("║  DISPLAY COMMANDS                     ║");
  Serial.println("║    1-6          - Show on dice 1      ║");
  Serial.println("║    +  / -       - Adjust interval     ║");
  Serial.println("║    c / C        - Clear display       ║");
  Serial.println("╠═══════════════════════════════════════╣");
  Serial.println("║  INFO COMMANDS                        ║");
  Serial.println("║    h / ?        - Show this help      ║");
  Serial.println("║    i / I        - Show settings       ║");
  Serial.println("║    s / S        - Show sensor data    ║");
  Serial.println("╚═══════════════════════════════════════╝");
  Serial.println("║  POWER COMMANDS                       ║");
  Serial.println("║    v / V        - Show battery status ║");
  Serial.println();
}

void showAnimationList() {
  Serial.println();
  Serial.println("🎬 Available Animations:");
  Serial.println("┌─────┬────────────┬─────────────────────────────┐");
  Serial.println("│ Cmd │ Name       │ Description                 │");
  Serial.println("├─────┼────────────┼─────────────────────────────┤");
  Serial.println("│ a0  │ Bounce     │ Colorful bouncing balls     │");
  Serial.println("│ a1  │ Spiral     │ Rainbow spiral vortex       │");
  Serial.println("│ a2  │ Scatter    │ Particle storm convergence  │");
  Serial.println("│ a3  │ Spin       │ Multi-ring color rotation   │");
  Serial.println("│ a4  │ Random     │ Random selection each roll  │");
  Serial.println("└─────┴────────────┴─────────────────────────────┘");
  Serial.print("   Current: ");
  Serial.print(currentAnimation);
  Serial.print(" - ");
  Serial.println(animationNames[currentAnimation]);
  Serial.println();
  Serial.println("   Enter animation number (0-4):");
}

void setAnimation(uint8_t anim) {
  if (anim < NUM_ANIMATIONS) {
    currentAnimation = anim;
    Serial.print("✅ Animation set to: ");
    Serial.print(anim);
    Serial.print(" - ");
    Serial.println(animationNames[anim]);
  } else {
    Serial.println("❌ Invalid animation number (0-4)");
  }
}

void previewAnimation() {
  Serial.print("👁️ Previewing animation: ");
  Serial.println(animationNames[currentAnimation]);
  rollAnimation();
  clearDisplay();
  pixels.show();
  Serial.println("   Preview complete.");
}

void showSettings() {
  Serial.println();
  Serial.println("⚙️ Current Settings:");
  Serial.println("┌─────────────────────────────────────┐");
  Serial.print("│ Animation:     ");
  Serial.print(currentAnimation);
  Serial.print(" - ");
  Serial.print(animationNames[currentAnimation]);
  for (uint i = 0; i < 12 - strlen(animationNames[currentAnimation]); i++) Serial.print(" ");
  Serial.println("│");
  Serial.print("│ Interval:      ");
  Serial.print(displayInterval);
  Serial.println(" ms             │");
  Serial.print("│ Brightness:    Normal=");
  Serial.print(BRIGHTNESS_NORMAL);
  Serial.print(" Anim=");
  Serial.print(BRIGHTNESS_ANIMATION);
  Serial.println("   │");
  Serial.print("│ Dice 1 Color:  RGB(");
  Serial.print(DICE1_R); Serial.print(",");
  Serial.print(DICE1_G); Serial.print(",");
  Serial.print(DICE1_B); Serial.println(")       │");
  Serial.print("│ Dice 2 Color:  RGB(");
  Serial.print(DICE2_R); Serial.print(",");
  Serial.print(DICE2_G); Serial.print(",");
  Serial.print(DICE2_B); Serial.println(")       │");
  Serial.print("│ Last Roll:     ");
  Serial.print(dice1Result);
  Serial.print(" + ");
  Serial.print(dice2Result);
  Serial.print(" = ");
  uint8_t total = dice1Result + dice2Result;
  if (total < 10) Serial.print(" ");
  Serial.print(total);
  Serial.println("           │");
  Serial.println("└─────────────────────────────────────┘");
  Serial.println();
  Serial.print("│ Battery:       ");
  Serial.print(batteryVoltage, 2);
  Serial.print("V (");
  Serial.print(batteryPercent);
  Serial.println("%)         │");
  Serial.print("│ Power Mode:    ");
  if (criticalPowerMode) Serial.println("CRITICAL           │");
  else if (lowPowerMode) Serial.println("LOW POWER          │");
  else Serial.println("Normal             │");
}

void showSensorData() {
  QMI8658_Data data;
  if (imu.readSensorData(data)) {
    float magnitude = sqrt(data.accelX * data.accelX + 
                          data.accelY * data.accelY + 
                          data.accelZ * data.accelZ);
    
    Serial.println("📊 Sensor Data:");
    Serial.print("   Accel X: "); Serial.print(data.accelX, 2); Serial.println(" mg");
    Serial.print("   Accel Y: "); Serial.print(data.accelY, 2); Serial.println(" mg");
    Serial.print("   Accel Z: "); Serial.print(data.accelZ, 2); Serial.println(" mg");
    Serial.print("   Magnitude: "); Serial.print(magnitude, 2); Serial.println(" mg");
    Serial.print("   Threshold: "); Serial.print(SHAKE_THRESHOLD); Serial.println(" mg");
    Serial.print("   Temp: "); Serial.print(data.temperature, 1); Serial.println(" °C");
    Serial.println();
  }
}

// ============================================
// COMMAND PROCESSING
// ============================================

void processCommand(char cmd) {
  if (waitingForAnimNumber) {
    waitingForAnimNumber = false;
    if (cmd >= '0' && cmd <= '4') {
      setAnimation(cmd - '0');
    } else if (cmd != '\n' && cmd != '\r') {
      Serial.println("❌ Invalid animation number. Use 0-4.");
    }
    return;
  }
  
  switch (cmd) {
    case 'r':
    case 'R':
      rollDice();
      break;
    
    case 'a':
    case 'A':
      showAnimationList();
      waitingForAnimNumber = true;
      break;
      
    case 'p':
    case 'P':
      previewAnimation();
      break;
      
    case 'b':
    case 'B':
      bootupAnimation();
      break;
    
    case '1': case '2': case '3':
    case '4': case '5': case '6':
      dice1Result = cmd - '0';
      currentDisplayDice = 1;
      drawDie(dice1Result, COLOR_DICE1);
      isDisplayingResult = true;
      lastDisplaySwitch = millis();
      Serial.print("Dice 1 set to: ");
      Serial.println(dice1Result);
      break;
    
    case '+':
      displayInterval += 100;
      if (displayInterval > 5000) displayInterval = 5000;
      Serial.print("Display interval: ");
      Serial.print(displayInterval);
      Serial.println(" ms");
      break;
      
    case '-':
      if (displayInterval > 200) displayInterval -= 100;
      Serial.print("Display interval: ");
      Serial.print(displayInterval);
      Serial.println(" ms");
      break;
    
    case 'h': case 'H': case '?':
      showHelp();
      break;
      
    case 'i': case 'I':
      showSettings();
      break;
      
    case 's': case 'S':
      showSensorData();
      break;
    
    case 'c': case 'C':
      clearDisplay();
      pixels.show();
      isDisplayingResult = false;
      Serial.println("Display cleared.");
      break;

    case 'v': case 'V':
      showBatteryStatus();
      break;
    
    case '\n': case '\r':
      break;
      
    default:
      break;
  }
}

// ============================================
// MOTION DETECTION
// ============================================

bool detectShake() {
  QMI8658_Data data;
  
  if (!imu.readSensorData(data)) {
    return false;
  }
  
  float magnitude = sqrt(data.accelX * data.accelX + 
                        data.accelY * data.accelY + 
                        data.accelZ * data.accelZ);
  
  float delta = abs(magnitude - lastAccelMagnitude);
  lastAccelMagnitude = magnitude;
  
  if (delta > SHAKE_THRESHOLD && (millis() - lastRollTime > SHAKE_COOLDOWN)) {
    return true;
  }
  
  return false;
}

// ============================================
// DISPLAY UPDATE
// ============================================

void updateAlternatingDisplay() {
  if (!isDisplayingResult) return;
  
  if (millis() - lastDisplaySwitch >= displayInterval) {
    lastDisplaySwitch = millis();
    
    if (currentDisplayDice == 1) {
      currentDisplayDice = 2;
      drawDie(dice2Result, COLOR_DICE2);
    } else {
      currentDisplayDice = 1;
      drawDie(dice1Result, COLOR_DICE1);
    }
  }
}

// ============================================
// BATTERY MONITORING FUNCTIONS
// ============================================

// ============================================
// BATTERY MONITORING (Updated)
// ============================================

float readBatteryVoltage() {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(BATTERY_PIN);
    delay(2);
  }
  float avgReading = sum / 10.0;
  float voltage = (avgReading / ADC_RESOLUTION) * ADC_REF_VOLTAGE * VOLTAGE_DIVIDER_RATIO;
  return voltage;
}

uint8_t voltageToPercent(float voltage) {
  if (voltage >= BATTERY_FULL) return 100;
  if (voltage <= BATTERY_SHUTDOWN) return 0;
  float percent = ((voltage - BATTERY_SHUTDOWN) / (BATTERY_NOMINAL - BATTERY_SHUTDOWN)) * 100.0;
  return (uint8_t)constrain(percent, 0, 100);
}

void enterLowPowerWarning() {
  if (!lowPowerMode) {
    lowPowerMode = true;
    showingBatteryWarning = true;
    isDisplayingResult = false;  // Stop dice display
    
    Serial.println();
    Serial.println("🪫 ════════════════════════════════════");
    Serial.println("   LOW BATTERY WARNING");
    Serial.println("   Showing battery indicator");
    Serial.println("   Please charge soon!");
    Serial.println("════════════════════════════════════ 🪫");
    Serial.println();
    
    pixels.setBrightness(BRIGHTNESS_LOW_POWER);
    drawLowBattery(true);
  }
}

void exitLowPowerWarning() {
  if (lowPowerMode) {
    lowPowerMode = false;
    showingBatteryWarning = false;
    
    Serial.println("✅ Battery recovered - Resuming normal operation");
    
    pixels.setBrightness(BRIGHTNESS_NORMAL);
    clearDisplay();
    pixels.show();
    
    // Show last dice result
    if (dice1Result > 0) {
      isDisplayingResult = true;
      currentDisplayDice = 1;
      drawDie(dice1Result, COLOR_DICE1);
    }
  }
}

void updateBattery() {
  if (millis() - lastBatteryRead < BATTERY_READ_INTERVAL) {
    return;
  }
  
  lastBatteryRead = millis();
  
  batteryVoltage = readBatteryVoltage();
  batteryPercent = voltageToPercent(batteryVoltage);
  
  // Print status
  String icon;
  if (batteryPercent > 75) icon = "🔋";
  else if (batteryPercent > 50) icon = "🔋";
  else if (batteryPercent > 25) icon = "🪫";
  else if (batteryPercent > 10) icon = "🪫";
  else icon = "⚠️";
  
  Serial.print(icon);
  Serial.print(" Battery: ");
  Serial.print(batteryVoltage, 2);
  Serial.print("V (");
  Serial.print(batteryPercent);
  Serial.print("%) [");
  
  int filled = batteryPercent / 10;
  for (int i = 0; i < 10; i++) {
    Serial.print(i < filled ? "█" : "░");
  }
  Serial.println("]");
  
  // State machine for battery levels
  if (batteryVoltage <= BATTERY_CRITICAL) {
    // CRITICAL - Enter deep sleep
    enterDeepSleep();
    // Never reaches here (deep sleep)
    
  } else if (batteryVoltage <= BATTERY_LOW) {
    // LOW - Show warning symbol
    enterLowPowerWarning();
    
  } else if (batteryVoltage >= BATTERY_NOMINAL) {
    // NOMINAL or above - Normal operation
    if (lowPowerMode) {
      exitLowPowerWarning();
    }
  }
  // Hysteresis: between LOW and NOMINAL, maintain current state
}

void showBatteryStatus() {
  Serial.println();
  Serial.println("🔋 Battery Status:");
  Serial.println("┌─────────────────────────────────────┐");
  Serial.print("│ Voltage:     ");
  Serial.print(batteryVoltage, 2);
  Serial.println(" V                 │");
  Serial.print("│ Percentage:  ");
  if (batteryPercent < 100) Serial.print(" ");
  if (batteryPercent < 10) Serial.print(" ");
  Serial.print(batteryPercent);
  Serial.print("% [");
  int filled = batteryPercent / 10;
  for (int i = 0; i < 10; i++) {
    Serial.print(i < filled ? "█" : "░");
  }
  Serial.println("]  │");
  Serial.print("│ Status:      ");
  if (batteryVoltage <= BATTERY_CRITICAL) {
    Serial.println("🚨 CRITICAL (sleep)    │");
  } else if (batteryVoltage <= BATTERY_LOW) {
    Serial.println("🪫 LOW (warning)       │");
  } else if (batteryVoltage < BATTERY_NOMINAL) {
    Serial.println("🟡 Below nominal       │");
  } else if (batteryPercent > 75) {
    Serial.println("✅ Excellent           │");
  } else if (batteryPercent > 50) {
    Serial.println("✅ Good                │");
  } else {
    Serial.println("🟡 Fair                │");
  }
  Serial.print("│ Mode:        ");
  if (lowPowerMode) {
    Serial.println("⚡ Low Power           │");
  } else {
    Serial.println("⚡ Normal              │");
  }
  Serial.println("├─────────────────────────────────────┤");
  Serial.println("│ Thresholds:                         │");
  Serial.print("│   Full:      "); Serial.print(BATTERY_FULL, 1); Serial.println(" V                  │");
  Serial.print("│   Nominal:   "); Serial.print(BATTERY_NOMINAL, 1); Serial.println(" V (wake target)   │");
  Serial.print("│   Low:       "); Serial.print(BATTERY_LOW, 1); Serial.println(" V (warning)       │");
  Serial.print("│   Critical:  "); Serial.print(BATTERY_CRITICAL, 1); Serial.println(" V (deep sleep)   │");
  Serial.print("│   Shutdown:  "); Serial.print(BATTERY_SHUTDOWN, 1); Serial.println(" V                  │");
  Serial.println("├─────────────────────────────────────┤");
  Serial.print("│ Boot count:  ");
  Serial.print(bootCount);
  Serial.println("                        │");
  Serial.println("└─────────────────────────────────────┘");
  Serial.println();
}


String getBatteryIcon(uint8_t percent) {
  if (percent > 75) return "🔋";       // Full
  if (percent > 50) return "🔋";       // Good
  if (percent > 25) return "🪫";       // Low
  if (percent > 10) return "🪫";       // Very low
  return "⚠️";                         // Critical
}

String getBatteryBar(uint8_t percent) {
  String bar = "[";
  int filled = percent / 10;
  for (int i = 0; i < 10; i++) {
    if (i < filled) bar += "█";
    else bar += "░";
  }
  bar += "]";
  return bar;
}

void enterLowPowerMode() {
  if (!lowPowerMode) {
    lowPowerMode = true;
    Serial.println();
    Serial.println("⚠️ ════════════════════════════════════");
    Serial.println("   LOW BATTERY - Entering power save");
    Serial.println("   Reducing brightness to conserve power");
    Serial.println("════════════════════════════════════ ⚠️");
    Serial.println();
    
    pixels.setBrightness(BRIGHTNESS_LOW_POWER);
    displayInterval = 2000;  // Slower display switching
  }
}

void exitLowPowerMode() {
  if (lowPowerMode && !criticalPowerMode) {
    lowPowerMode = false;
    Serial.println("✅ Battery OK - Exiting power save mode");
    pixels.setBrightness(BRIGHTNESS_NORMAL);
    displayInterval = DEFAULT_DISPLAY_INTERVAL;
  }
}

void enterCriticalPowerMode() {
  if (!criticalPowerMode) {
    criticalPowerMode = true;
    lowPowerMode = true;
    
    Serial.println();
    Serial.println("🚨 ════════════════════════════════════");
    Serial.println("   CRITICAL BATTERY - Minimal operation");
    Serial.println("   Please charge immediately!");
    Serial.println("════════════════════════════════════ 🚨");
    Serial.println();
    
    // Minimal power - just show static display
    pixels.setBrightness(BRIGHTNESS_LOW_POWER);
    isDisplayingResult = false;
    clearDisplay();
    
    // Show warning pattern (low battery indicator)
    setPixel(3, 3, pixels.Color(255, 0, 0));
    setPixel(4, 3, pixels.Color(255, 0, 0));
    setPixel(3, 4, pixels.Color(255, 0, 0));
    setPixel(4, 4, pixels.Color(255, 0, 0));
    pixels.show();
  }
}

void shutdownDisplay() {
  Serial.println();
  Serial.println("💀 ════════════════════════════════════");
  Serial.println("   BATTERY EMPTY - Shutting down display");
  Serial.println("   Please charge to continue");
  Serial.println("════════════════════════════════════ 💀");
  Serial.println();
  
  // Fade out animation
  for (int b = pixels.getBrightness(); b >= 0; b--) {
    pixels.setBrightness(b);
    pixels.show();
    delay(50);
  }
  
  clearDisplay();
  pixels.show();
  isDisplayingResult = false;
}




// ============================================
// SETUP & LOOP
// ============================================

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(500);
  
  Serial.println();
  Serial.println("╔═══════════════════════════════════════╗");
  Serial.println("║   🎲🎲 DUAL MOTION DIGITAL DICE 🎲🎲   ║");
  Serial.println("║         v2.1 - Power Management       ║");
  Serial.println("╚═══════════════════════════════════════╝");
  Serial.println();
  
  // Initialize battery monitoring FIRST
  pinMode(BATTERY_PIN, INPUT);
  analogReadResolution(12);
  
  // Handle wakeup from deep sleep (may not return if going back to sleep)
  handleWakeup();
  
  // Initialize LED matrix
  pixels.begin();
  pixels.setBrightness(BRIGHTNESS_NORMAL);
  
  COLOR_BG = pixels.Color(BG_R, BG_G, BG_B);
  COLOR_DICE1 = pixels.Color(DICE1_R, DICE1_G, DICE1_B);
  COLOR_DICE2 = pixels.Color(DICE2_R, DICE2_G, DICE2_B);
  
  Serial.println("✅ LED Matrix initialized");
  
  // Initial battery check
  batteryVoltage = readBatteryVoltage();
  batteryPercent = voltageToPercent(batteryVoltage);
  
  Serial.print("🔋 Battery: ");
  Serial.print(batteryVoltage, 2);
  Serial.print("V (");
  Serial.print(batteryPercent);
  Serial.println("%)");
  
  // Check if battery is too low to operate
  if (batteryVoltage <= BATTERY_CRITICAL) {
    Serial.println("⚠️ Battery too low for operation!");
    enterDeepSleep();
    // Never reaches here
  }
  
  // Initialize IMU
  Serial.println("📍 Initializing QMI8658...");
  bool imuSuccess = imu.begin(I2C_SDA, I2C_SCL);
  
  if (!imuSuccess) {
    Serial.println("❌ QMI8658 init failed!");
    Serial.println("   Continuing without motion control...");
  } else {
    Serial.println("✅ QMI8658 initialized");
    
    imu.setAccelRange(QMI8658_ACCEL_RANGE_8G);
    imu.setAccelODR(QMI8658_ACCEL_ODR_1000HZ);
    imu.setGyroRange(QMI8658_GYRO_RANGE_512DPS);
    imu.setGyroODR(QMI8658_GYRO_ODR_1000HZ);
    imu.setAccelUnit_mg(true);
    imu.setGyroUnit_dps(true);
    imu.enableSensors(QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO);
  }
  
  randomSeed(analogRead(0));
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Check if battery is low (but not critical) - show warning
  if (batteryVoltage <= BATTERY_LOW) {
    enterLowPowerWarning();
  } else {
    // Run epic bootup animation only if battery is OK!
    bootupAnimation();
  }
  
  Serial.print("🎬 Default animation: ");
  Serial.println(animationNames[currentAnimation]);
  Serial.print("⏱️ Display interval: ");
  Serial.print(displayInterval);
  Serial.println(" ms");
  
  showHelp();
  
  // Only auto-roll if not in low power mode
  if (!lowPowerMode) {
    rollDice();
  }
}

void loop() {
  // Always check battery first
  updateBattery();
  
  // Update battery warning display if active
  if (showingBatteryWarning) {
    updateBatteryWarningDisplay();
    
    // Still allow serial commands in low power mode
    if (Serial.available() > 0) {
      char cmd = Serial.read();
      // Only allow limited commands in low power
      if (cmd == 'v' || cmd == 'V') {
        showBatteryStatus();
      } else if (cmd == 'h' || cmd == 'H' || cmd == '?') {
        showHelp();
      } else if (cmd == 'i' || cmd == 'I') {
        showSettings();
      }
    }
    
    delay(50);
    return;  // Skip normal operations
  }
  
  // Normal operation below this point
  
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    processCommand(cmd);
  }
  
  
  if (detectShake()) {
    Serial.println("📳 Shake detected!");
    rollDice();
  }
  
  updateAlternatingDisplay();
  
  delay(POLL_INTERVALL_LOOP);
}