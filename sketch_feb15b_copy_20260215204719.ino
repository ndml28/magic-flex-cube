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
#define SHAKE_COOLDOWN 500      // ms - prevent rapid re-triggers

Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
QMI8658 imu;

// Colors
uint32_t COLOR_BG;
uint32_t COLOR_DOT;

// Motion tracking
float lastAccelMagnitude = 1000.0;  // ~1g at rest
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

// Draw single die on full 8x8 matrix
void drawDie(uint8_t value) {
  clearDisplay();
  
  uint8_t leftX = 1;
  uint8_t centerX = 3;
  uint8_t rightX = 5;

  uint8_t topY = 1;
  uint8_t midY = 3;
  uint8_t botY = 5;

  switch (value) {
    case 1:
      drawDot(centerX, midY, COLOR_DOT);
      break;

    case 2:
      drawDot(leftX, topY, COLOR_DOT);
      drawDot(rightX, botY, COLOR_DOT);
      break;

    case 3:
      drawDot(leftX, topY, COLOR_DOT);
      drawDot(centerX, midY, COLOR_DOT);
      drawDot(rightX, botY, COLOR_DOT);
      break;

    case 4:
      drawDot(leftX, topY, COLOR_DOT);
      drawDot(rightX, topY, COLOR_DOT);
      drawDot(leftX, botY, COLOR_DOT);
      drawDot(rightX, botY, COLOR_DOT);
      break;

    case 5:
      drawDot(leftX, topY, COLOR_DOT);
      drawDot(rightX, topY, COLOR_DOT);
      drawDot(centerX, midY, COLOR_DOT);
      drawDot(leftX, botY, COLOR_DOT);
      drawDot(rightX, botY, COLOR_DOT);
      break;

    case 6:
      
      drawDot(leftX, topY-1, COLOR_DOT);
      drawDot(leftX, midY, COLOR_DOT);
      drawDot(leftX, botY+1, COLOR_DOT);
      drawDot(rightX, topY-1, COLOR_DOT);
      drawDot(rightX, midY, COLOR_DOT);
      drawDot(rightX, botY+1, COLOR_DOT);

      break;
  }
  
  pixels.show();
}

// Rolling animation
void rollAnimation() {
  for (int i = 0; i < 12; i++) {
    drawDie(random(1, 7));
    delay(30 + i * 15);
  }
}

// Roll dice and show result
void rollDice() {
  Serial.println("🎲 Rolling...");
  rollAnimation();
  
  uint8_t result = random(1, 7);
  drawDie(result);
  
  Serial.println("┌─────────────┐");
  Serial.println("│             │");
  Serial.print("│      ");
  Serial.print(result);
  Serial.println("      │");
  Serial.println("│             │");
  Serial.println("└─────────────┘");
  Serial.println();
  
  lastRollTime = millis();
}

// Show help menu
void showHelp() {
  Serial.println();
  Serial.println("=== MOTION DICE ===");
  Serial.println("Controls:");
  Serial.println("  SHAKE BOARD    - Roll dice!");
  Serial.println("  r / R / ENTER  - Roll via serial");
  Serial.println("  1-6            - Show specific value");
  Serial.println("  h / H / ?      - Show this help");
  Serial.println("  c / C          - Clear display");
  Serial.println("  s / S          - Show sensor data");
  Serial.println("===================");
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
      drawDie(cmd - '0');
      Serial.print("Showing: ");
      Serial.println(cmd);
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
      Serial.println("Display cleared.");
      break;
      
    case 's':
    case 'S':
      showSensorData();
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
  
  // Calculate acceleration magnitude
  float magnitude = sqrt(data.accelX * data.accelX + 
                        data.accelY * data.accelY + 
                        data.accelZ * data.accelZ);
  
  // Detect sudden change in acceleration
  float delta = abs(magnitude - lastAccelMagnitude);
  lastAccelMagnitude = magnitude;
  
  // Check if shake exceeds threshold and cooldown has passed
  if (delta > SHAKE_THRESHOLD && (millis() - lastRollTime > SHAKE_COOLDOWN)) {
    return true;
  }
  
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("🎲 Motion-Controlled Digital Dice");
  Serial.println("==================================");
  
  // Initialize LED matrix
  pixels.begin();
  pixels.setBrightness(2);
  COLOR_BG = pixels.Color(0, 0, 0);
  COLOR_DOT = pixels.Color(255, 255, 255);
  Serial.println("✅ LED Matrix initialized");
  
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
    
    // Configure IMU
    imu.setAccelRange(QMI8658_ACCEL_RANGE_8G);
    imu.setAccelODR(QMI8658_ACCEL_ODR_1000HZ);
    imu.setGyroRange(QMI8658_GYRO_RANGE_512DPS);
    imu.setGyroODR(QMI8658_GYRO_ODR_1000HZ);
    imu.setAccelUnit_mg(true);
    imu.setGyroUnit_dps(true);
    imu.enableSensors(QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO);
  }
  
  // Initialize random seed and button
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
  
  delay(10);  // Small delay for stability
}