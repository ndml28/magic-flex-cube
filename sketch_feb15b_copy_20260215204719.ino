#include <Adafruit_NeoPixel.h>
#include <QMI8658.h>

// LED Matrix config
#define PIN 14
#define NUMPIXELS 64
#define BUTTON_PIN 2
#define I2C_SDA       11
#define I2C_SCL       12

// Motion detection thresholds
#define SHAKE_THRESHOLD 1200.0   // mg - adjust sensitivity
#define SHAKE_COOLDOWN 500       // ms - prevent rapid re-triggers

// ============================================
// CONFIGURABLE SETTINGS
// ============================================
#define DISPLAY_INTERVAL 1000    // ms - alternating display interval (1s)

// Dice Colors (RGB values) - ADJUST THESE!
#define DICE1_R 230
#define DICE1_G 0
#define DICE1_B 0      // Red

#define DICE2_R 0
#define DICE2_G 0
#define DICE2_B 255    // Blue

// Background color
#define BG_R 0
#define BG_G 0
#define BG_B 0         // Black
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
uint8_t currentDisplayDice = 1;  // 1 or 2
unsigned long lastDisplaySwitch = 0;
unsigned long displayInterval = DISPLAY_INTERVAL;

// Motion tracking
float lastAccelMagnitude = 1000.0;
unsigned long lastRollTime = 0;

// Convert x/y to LED index
uint16_t xy(uint8_t x, uint8_t y) {
  return y * 8 + x;
}

// Set LED at x/y position
void setXY(uint8_t x, uint8_t y, uint32_t color) {
  if (x < 8 && y < 8) {
    pixels.setPixelColor(xy(x, y), color);
  }
}

// Draw a 2x2 dot at position
void drawDot(uint8_t x, uint8_t y, uint32_t color) {
  setXY(x, y, color);
  setXY(x + 1, y, color);
  setXY(x, y + 1, color);
  setXY(x + 1, y + 1, color);
}

// Clear display
void clearDisplay() {
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, COLOR_BG);
  }
}

// Draw single die on full 8x8 matrix with specified color
void drawDie(uint8_t value, uint32_t color) {
  clearDisplay();
  
  uint8_t leftX = 1;
  uint8_t centerX = 3;
  uint8_t rightX = 5;

  uint8_t topY = 1;
  uint8_t midY = 3;
  uint8_t botY = 5;

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
// ANIMATION SETTINGS
// ============================================
#define ANIM_TYPE 0  // 0=Bounce, 1=Spiral, 2=Scatter, 3=Sweep, 4=Random

// Bouncing dot structure
struct BouncingDot {
  float x, y;
  float vx, vy;
  uint32_t color;
};

// ============================================
// ANIMATION FUNCTIONS
// ============================================

// Draw a single pixel (not 2x2 dot)
void setPixel(uint8_t x, uint8_t y, uint32_t color) {
  if (x < 8 && y < 8) {
    pixels.setPixelColor(xy(x, y), color);
  }
}

// Animation 1: Bouncing Ball Physics
void animBounce() {
  BouncingDot dots[3];
  
  // Initialize dots with random positions and velocities
  for (int i = 0; i < 3; i++) {
    dots[i].x = random(1, 7);
    dots[i].y = random(1, 7);
    dots[i].vx = (random(0, 2) == 0 ? 1 : -1) * (0.5 + random(0, 10) / 10.0);
    dots[i].vy = (random(0, 2) == 0 ? 1 : -1) * (0.5 + random(0, 10) / 10.0);
    dots[i].color = (i % 2 == 0) ? COLOR_DICE1 : COLOR_DICE2;
  }
  
  // Simulate bouncing with friction
  for (int frame = 0; frame < 40; frame++) {
    clearDisplay();
    
    // Apply friction over time
    float friction = 1.0 - (frame * 0.015);
    if (friction < 0.3) friction = 0.3;
    
    for (int i = 0; i < 3; i++) {
      // Update position
      dots[i].x += dots[i].vx * friction;
      dots[i].y += dots[i].vy * friction;
      
      // Bounce off walls
      if (dots[i].x <= 0 || dots[i].x >= 7) {
        dots[i].vx *= -1;
        dots[i].x = constrain(dots[i].x, 0, 7);
      }
      if (dots[i].y <= 0 || dots[i].y >= 7) {
        dots[i].vy *= -1;
        dots[i].y = constrain(dots[i].y, 0, 7);
      }
      
      // Draw dot as 2x2 block
      int px = (int)dots[i].x;
      int py = (int)dots[i].y;
      setPixel(px, py, dots[i].color);
      setPixel(px + 1, py, dots[i].color);
      setPixel(px, py + 1, dots[i].color);
      setPixel(px + 1, py + 1, dots[i].color);
    }
    
    pixels.show();
    delay(40 - frame / 2);  // Speed up slightly at end
  }
}

// Animation 2: Spiral In/Out
void animSpiral() {
  // Spiral coordinates (from outside to center)
  const int8_t spiralX[] = {0,1,2,3,4,5,6,7, 7,7,7,7,7,7,7, 6,5,4,3,2,1,0, 0,0,0,0,0,0, 1,2,3,4,5,6, 6,6,6,6,6, 5,4,3,2,1, 1,1,1,1, 2,3,4,5, 5,5,5, 4,3,2, 2,2, 3,4, 4, 3};
  const int8_t spiralY[] = {0,0,0,0,0,0,0,0, 1,2,3,4,5,6,7, 7,7,7,7,7,7,7, 6,5,4,3,2,1, 1,1,1,1,1,1, 2,3,4,5,6, 6,6,6,6,6, 5,4,3,2, 2,2,2,2, 3,4,5, 5,5,5, 4,3, 3,3, 4, 4};
  const int spiralLen = 64;
  
  // Spiral outward (fast)
  for (int i = spiralLen - 1; i >= 0; i -= 2) {
    clearDisplay();
    for (int j = spiralLen - 1; j >= i; j--) {
      uint32_t c = ((spiralLen - j) % 4 < 2) ? COLOR_DICE1 : COLOR_DICE2;
      setPixel(spiralX[j], spiralY[j], c);
    }
    pixels.show();
    delay(15);
  }
  
  delay(100);
  
  // Spiral inward (slower, with trail)
  for (int i = 0; i < spiralLen; i += 2) {
    clearDisplay();
    // Draw trailing dots
    for (int j = i; j < min(i + 12, spiralLen); j++) {
      uint32_t c = (j % 4 < 2) ? COLOR_DICE1 : COLOR_DICE2;
      // Fade based on distance from head
      setPixel(spiralX[j], spiralY[j], c);
    }
    pixels.show();
    delay(20);
  }
}

// Animation 3: Scatter and Converge
void animScatter() {
  // Particle structure
  struct Particle {
    float x, y;
    float targetX, targetY;
    float vx, vy;
    uint32_t color;
  };
  
  Particle particles[12];
  
  // Initialize particles scattered around edges
  for (int i = 0; i < 12; i++) {
    // Start at random edge positions
    int edge = random(0, 4);
    switch (edge) {
      case 0: particles[i].x = random(0, 8); particles[i].y = 0; break;
      case 1: particles[i].x = random(0, 8); particles[i].y = 7; break;
      case 2: particles[i].x = 0; particles[i].y = random(0, 8); break;
      case 3: particles[i].x = 7; particles[i].y = random(0, 8); break;
    }
    // Target is center area
    particles[i].targetX = 2 + random(0, 4);
    particles[i].targetY = 2 + random(0, 4);
    particles[i].color = (i % 2 == 0) ? COLOR_DICE1 : COLOR_DICE2;
  }
  
  // Animate convergence
  for (int frame = 0; frame < 30; frame++) {
    clearDisplay();
    
    for (int i = 0; i < 12; i++) {
      // Move toward target with easing
      float progress = frame / 30.0;
      float easing = progress * progress * (3 - 2 * progress);  // Smoothstep
      
      float currentX = particles[i].x + (particles[i].targetX - particles[i].x) * easing;
      float currentY = particles[i].y + (particles[i].targetY - particles[i].y) * easing;
      
      // Add some wobble
      if (frame < 20) {
        currentX += sin(frame * 0.5 + i) * (1 - progress);
        currentY += cos(frame * 0.5 + i) * (1 - progress);
      }
      
      int px = constrain((int)currentX, 0, 7);
      int py = constrain((int)currentY, 0, 7);
      setPixel(px, py, particles[i].color);
    }
    
    pixels.show();
    delay(35);
  }
  
  // Flash at end
  for (int i = 0; i < 3; i++) {
    clearDisplay();
    pixels.show();
    delay(50);
    for (int p = 0; p < 12; p++) {
      setPixel(particles[p].targetX, particles[p].targetY, particles[p].color);
    }
    pixels.show();
    delay(50);
  }
}

// Animation 4: Sweep with Random Faces
void animSweep() {
  // Vertical sweep showing random dice
  for (int sweep = 0; sweep < 3; sweep++) {
    uint32_t sweepColor = (sweep % 2 == 0) ? COLOR_DICE1 : COLOR_DICE2;
    
    // Sweep down
    for (int y = 0; y < 8; y++) {
      // Draw current random dice faintly
      uint8_t tempVal = random(1, 7);
      clearDisplay();
      
      // Draw sweep line
      for (int x = 0; x < 8; x++) {
        setPixel(x, y, sweepColor);
        if (y > 0) setPixel(x, y - 1, pixels.Color(
          (sweepColor >> 16 & 0xFF) / 3,
          (sweepColor >> 8 & 0xFF) / 3,
          (sweepColor & 0xFF) / 3
        ));
      }
      
      // Show random dice value below sweep
      if (y > 2) {
        drawDiePartial(tempVal, sweepColor, 0, y + 1);
      }
      
      pixels.show();
      delay(30 + sweep * 10);
    }
  }
}

// Helper for sweep animation - draw partial dice
void drawDiePartial(uint8_t value, uint32_t color, int minY, int maxY) {
  uint8_t leftX = 1, centerX = 3, rightX = 5;
  uint8_t topY = 1, midY = 3, botY = 5;
  
  auto drawDotIfVisible = [&](uint8_t x, uint8_t y) {
    if (y >= minY && y + 1 < maxY) {
      drawDot(x, y, color);
    }
  };
  
  switch (value) {
    case 1: drawDotIfVisible(centerX, midY); break;
    case 2: drawDotIfVisible(leftX, topY); drawDotIfVisible(rightX, botY); break;
    case 3: drawDotIfVisible(leftX, topY); drawDotIfVisible(centerX, midY); drawDotIfVisible(rightX, botY); break;
    case 4: drawDotIfVisible(leftX, topY); drawDotIfVisible(rightX, topY); drawDotIfVisible(leftX, botY); drawDotIfVisible(rightX, botY); break;
    case 5: drawDotIfVisible(leftX, topY); drawDotIfVisible(rightX, topY); drawDotIfVisible(centerX, midY); drawDotIfVisible(leftX, botY); drawDotIfVisible(rightX, botY); break;
    case 6: drawDotIfVisible(leftX, topY-1); drawDotIfVisible(leftX, midY); drawDotIfVisible(leftX, botY+1); drawDotIfVisible(rightX, topY-1); drawDotIfVisible(rightX, midY); drawDotIfVisible(rightX, botY+1); break;
  }
}

// Animation 5: Spinning Dice (rotating dots)
void animSpin() {
  // Define rotation positions for dots (circular path)
  const float radius = 2.5;
  const float centerX = 3.5;
  const float centerY = 3.5;
  
  int numDots = 6;
  float angleOffset = 0;
  
  for (int frame = 0; frame < 45; frame++) {
    clearDisplay();
    
    // Speed decreases over time
    float speed = 0.4 - (frame * 0.007);
    if (speed < 0.05) speed = 0.05;
    angleOffset += speed;
    
    // Current radius (starts wide, ends in center)
    float currentRadius = radius * (1 - frame / 60.0);
    if (frame > 35) currentRadius = radius * 0.4;
    
    // Draw rotating dots
    int dotsToShow = 2 + (frame / 10);
    if (dotsToShow > numDots) dotsToShow = numDots;
    
    for (int i = 0; i < dotsToShow; i++) {
      float angle = angleOffset + (i * 2 * PI / numDots);
      
      int px = (int)(centerX + cos(angle) * currentRadius);
      int py = (int)(centerY + sin(angle) * currentRadius);
      
      uint32_t color = (i % 2 == 0) ? COLOR_DICE1 : COLOR_DICE2;
      
      // Draw 2x2 dot
      setPixel(constrain(px, 0, 6), constrain(py, 0, 6), color);
      setPixel(constrain(px + 1, 0, 7), constrain(py, 0, 6), color);
      setPixel(constrain(px, 0, 6), constrain(py + 1, 0, 7), color);
      setPixel(constrain(px + 1, 0, 7), constrain(py + 1, 0, 7), color);
    }
    
    pixels.show();
    delay(40);
  }
}

// Master animation function - calls selected animation
void rollAnimation() {
  int animType = ANIM_TYPE;
  
  // If set to random, pick one
  if (animType == 4) {
    animType = random(0, 4);
  }
  
  switch (animType) {
    case 0: animBounce(); break;
    case 1: animSpiral(); break;
    case 2: animScatter(); break;
    case 3: animSpin(); break;
    default: animBounce(); break;
  }
}


// Roll both dice and show results
void rollDice() {
  Serial.println("🎲🎲 Rolling two dice...");
  rollAnimation();
  
  // Generate results for both dice
  dice1Result = random(1, 7);
  dice2Result = random(1, 7);
  
  // Start alternating display with dice 1
  currentDisplayDice = 1;
  drawDie(dice1Result, COLOR_DICE1);
  isDisplayingResult = true;
  lastDisplaySwitch = millis();
  
  // Print results to serial
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

// Show help menu
void showHelp() {
  Serial.println();
  Serial.println("=== DUAL MOTION DICE ===");
  Serial.println("Controls:");
  Serial.println("  SHAKE BOARD    - Roll both dice!");
  Serial.println("  r / R / ENTER  - Roll via serial");
  Serial.println("  1-6            - Show value on dice 1");
  Serial.println("  h / H / ?      - Show this help");
  Serial.println("  c / C          - Clear display");
  Serial.println("  s / S          - Show sensor data");
  Serial.println("  + / -          - Adjust display interval");
  Serial.println("  i / I          - Show current interval");
  Serial.println("========================");
  Serial.println();
}

// Show current sensor values
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

// Show current settings
void showSettings() {
  Serial.println("⚙️ Current Settings:");
  Serial.print("   Display Interval: ");
  Serial.print(displayInterval);
  Serial.println(" ms");
  Serial.print("   Dice 1 Color: RGB(");
  Serial.print(DICE1_R); Serial.print(", ");
  Serial.print(DICE1_G); Serial.print(", ");
  Serial.print(DICE1_B); Serial.println(")");
  Serial.print("   Dice 2 Color: RGB(");
  Serial.print(DICE2_R); Serial.print(", ");
  Serial.print(DICE2_G); Serial.print(", ");
  Serial.print(DICE2_B); Serial.println(")");
  Serial.print("   Current Results: ");
  Serial.print(dice1Result);
  Serial.print(" + ");
  Serial.print(dice2Result);
  Serial.print(" = ");
  Serial.println(dice1Result + dice2Result);
  Serial.println();
}

// Process serial command
void processCommand(char cmd) {
  switch (cmd) {
    case 'r':
    case 'R':
    case '\n':
    case '\r':
      rollDice();
      break;
      
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
      dice1Result = cmd - '0';
      currentDisplayDice = 1;
      drawDie(dice1Result, COLOR_DICE1);
      Serial.print("Dice 1 set to: ");
      Serial.println(dice1Result);
      break;
      
    case 'h':
    case 'H':
    case '?':
      showHelp();
      break;
      
    case 'c':
    case 'C':
      clearDisplay();
      pixels.show();
      isDisplayingResult = false;
      Serial.println("Display cleared.");
      break;
      
    case 's':
    case 'S':
      showSensorData();
      break;
      
    case 'i':
    case 'I':
      showSettings();
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
      
    default:
      break;
  }
}

// Check for shake motion
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

// Update alternating display
void updateAlternatingDisplay() {
  if (!isDisplayingResult) return;
  
  if (millis() - lastDisplaySwitch >= displayInterval) {
    lastDisplaySwitch = millis();
    
    // Switch to other dice
    if (currentDisplayDice == 1) {
      currentDisplayDice = 2;
      drawDie(dice2Result, COLOR_DICE2);
    } else {
      currentDisplayDice = 1;
      drawDie(dice1Result, COLOR_DICE1);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("🎲🎲 Dual Motion-Controlled Digital Dice");
  Serial.println("========================================");
  
  // Initialize LED matrix
  pixels.begin();
  pixels.setBrightness(2);
  
  // Set colors from defines
  COLOR_BG = pixels.Color(BG_R, BG_G, BG_B);
  COLOR_DICE1 = pixels.Color(DICE1_R, DICE1_G, DICE1_B);
  COLOR_DICE2 = pixels.Color(DICE2_R, DICE2_G, DICE2_B);
  
  Serial.println("✅ LED Matrix initialized");
  Serial.print("   Dice 1 Color: RGB(");
  Serial.print(DICE1_R); Serial.print(", ");
  Serial.print(DICE1_G); Serial.print(", ");
  Serial.print(DICE1_B); Serial.println(")");
  Serial.print("   Dice 2 Color: RGB(");
  Serial.print(DICE2_R); Serial.print(", ");
  Serial.print(DICE2_G); Serial.print(", ");
  Serial.print(DICE2_B); Serial.println(")");
  
  // Initialize IMU
  Serial.println("📍 Initializing QMI8658...");
  bool imuSuccess = imu.begin(I2C_SDA, I2C_SCL);
  
  if (!imuSuccess) {
    Serial.println("❌ QMI8658 init failed!");
    Serial.println("   Continuing without motion control...");
  } else {
    Serial.println("✅ QMI8658 initialized");
    Serial.print("   QMI8685 ADR: 0x");
    Serial.println(imu.getWhoAmI(), HEX);
    
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
  
  showHelp();
  rollDice();
}

void loop() {
  // Check for serial commands
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    processCommand(cmd);
  }
  
  // Check for button press
  if (digitalRead(BUTTON_PIN) == LOW) {
    rollDice();
    delay(500);
  }
  
  // Check for shake motion
  if (detectShake()) {
    Serial.println("📳 Shake detected!");
    rollDice();
  }
  
  // Handle alternating display between dice 1 and dice 2
  updateAlternatingDisplay();
  
  delay(10);
}