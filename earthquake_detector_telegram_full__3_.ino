#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <U8g2_for_Adafruit_GFX.h>

// ===================== WI-FI =====================
const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ==================== TELEGRAM ===================
// Вставь НОВЫЙ токен от BotFather. Старый токен не используй.
const char* BOT_TOKEN = "YOUR_NEW_BOT_TOKEN";
const char* CHAT_ID = "5866442241";

// ===================== DISPLAY ===================
#define TFT_CS    -1
#define TFT_DC    16
#define TFT_RST   17
#define TFT_SCLK  18
#define TFT_MOSI  23

// ===================== MPU6050 ===================
#define MPU_SDA   32
#define MPU_SCL   33

// =================== LED + BUZZER ================
#define GREEN_LED 25
#define RED_LED   26
#define BUZZER    27

Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
Adafruit_MPU6050 mpu;
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

float baseX = 0, baseY = 0, baseZ = 0;
float xValue = 0, yValue = 0, zValue = 0;
float vibration = 0;

// Чувствительность, м/с²
const float WARNING_LEVEL = 0.18;
const float MEDIUM_LEVEL  = 0.35;
const float STRONG_LEVEL  = 0.70;

enum VibrationState { SAFE_STATE, WARNING_STATE, MEDIUM_STATE, STRONG_STATE };
VibrationState currentState = SAFE_STATE;

bool telegramSent = false;
int strongCounter = 0;
const int STRONG_CONFIRM_SAMPLES = 3;
unsigned long lastTelegramTime = 0;
const unsigned long TELEGRAM_COOLDOWN = 30000;

unsigned long lastBuzzerChange = 0;
bool buzzerState = false;

#define GRAPH_POINTS 90
float graphX[GRAPH_POINTS];
float graphY[GRAPH_POINTS];
float graphZ[GRAPH_POINTS];
int graphPosition = 0;
unsigned long lastSample = 0;
const unsigned long SAMPLE_INTERVAL = 70;

#define GRAPH_LEFT   18
#define GRAPH_WIDTH  218
#define GRAPH_HEIGHT 36
#define GRAPH_X_TOP  72
#define GRAPH_Y_TOP  117
#define GRAPH_Z_TOP  162

String urlEncode(const String &text) {
  String encoded = "";
  const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < text.length(); i++) {
    uint8_t c = (uint8_t)text[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      encoded += (char)c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

bool connectWiFi() {
  Serial.println();
  Serial.println("============================");
  Serial.println("WI-FI ҚОСЫЛУДА...");
  Serial.println("============================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[OK] Wi-Fi қосылды");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("[ҚАТЕ] Wi-Fi қосылмады");
  return false;
}

void checkWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("Wi-Fi жоғалды. Қайта қосылуда...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
}

bool sendTelegramMessage(String message) {
  checkWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ҚАТЕ] Telegram: Wi-Fi жоқ");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://api.telegram.org/bot" + String(BOT_TOKEN) + "/sendMessage";
  if (!https.begin(client, url)) {
    Serial.println("[ҚАТЕ] Telegram HTTPS");
    return false;
  }

  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String postData = "chat_id=" + String(CHAT_ID) + "&text=" + urlEncode(message);

  Serial.println();
  Serial.println("TELEGRAM ХАБАРЛАМА ЖІБЕРІЛУДЕ...");

  int httpCode = https.POST(postData);
  Serial.print("HTTP CODE: ");
  Serial.println(httpCode);

  String response = "";
  if (httpCode > 0) {
    response = https.getString();
    Serial.println(response);
  }
  https.end();

  if (httpCode == 200 && response.indexOf("\"ok\":true") >= 0) {
    Serial.println("[OK] Telegram жіберілді");
    return true;
  }

  Serial.println("[ҚАТЕ] Telegram жіберілмеді");
  return false;
}

void sendAlarmTelegram() {
  if (millis() - lastTelegramTime < TELEGRAM_COOLDOWN && lastTelegramTime != 0) return;

  String message = "ШҰҒЫЛ ДАБЫЛ!\n";
  message += "Қатты діріл анықталды.\n\n";
  message += "Діріл: " + String(vibration, 3) + " м/с²\n";
  message += "X: " + String(xValue, 3);
  message += "\nY: " + String(yValue, 3);
  message += "\nZ: " + String(zValue, 3);
  message += "\n\nҚауіпсіздікті тексеріңіз.";

  if (sendTelegramMessage(message)) {
    telegramSent = true;
    lastTelegramTime = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(BUZZER, LOW);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(240, 240, SPI_MODE3);
  tft.setSPISpeed(1000000);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  u8g2Fonts.begin(tft);
  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setFontDirection(0);
  u8g2Fonts.setFont(u8g2_font_unifont_t_cyrillic);
  u8g2Fonts.setForegroundColor(ST77XX_WHITE);
  u8g2Fonts.setBackgroundColor(ST77XX_BLACK);

  showStartup();

  Wire.begin(MPU_SDA, MPU_SCL);
  Wire.setClock(100000);
  delay(300);

  Serial.println("MPU6050 іске қосылуда...");
  if (!mpu.begin()) {
    Serial.println("MPU6050 ТАБЫЛМАДЫ!");
    showSensorError();
    while (1) {
      digitalWrite(RED_LED, HIGH);
      delay(300);
      digitalWrite(RED_LED, LOW);
      delay(300);
    }
  }

  Serial.println("MPU6050 ТАБЫЛДЫ");
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  calibrateSensor();
  connectWiFi();

  for (int i = 0; i < GRAPH_POINTS; i++) {
    graphX[i] = 0;
    graphY[i] = 0;
    graphZ[i] = 0;
  }

  drawMainScreen();
  digitalWrite(GREEN_LED, HIGH);

  if (WiFi.status() == WL_CONNECTED) {
    sendTelegramMessage("ESP32 жер сілкінісі детекторы іске қосылды.");
  }
}

void loop() {
  updateBuzzer();

  if (millis() - lastSample >= SAMPLE_INTERVAL) {
    lastSample = millis();
    readSensor();
    calculateVibration();
    determineState();
    controlOutputs();
    saveGraph();
    drawValues();
    drawGraphs();
    drawStatus();
    printSerial();
  }
}

void showStartup() {
  tft.fillScreen(ST77XX_BLACK);
  u8g2Fonts.setForegroundColor(ST77XX_WHITE);
  u8g2Fonts.setCursor(38, 70);
  u8g2Fonts.print("ЖЕР СІЛКІНІСІ");
  u8g2Fonts.setCursor(58, 100);
  u8g2Fonts.print("ДЕТЕКТОРЫ");
  u8g2Fonts.setForegroundColor(ST77XX_CYAN);
  u8g2Fonts.setCursor(43, 135);
  u8g2Fonts.print("ESP32 + MPU6050");
  u8g2Fonts.setCursor(46, 160);
  u8g2Fonts.print("Wi-Fi + Telegram");
  delay(1500);
}

void showSensorError() {
  tft.fillScreen(ST77XX_BLACK);
  u8g2Fonts.setForegroundColor(ST77XX_RED);
  u8g2Fonts.setCursor(68, 90);
  u8g2Fonts.print("MPU6050 ҚАТЕ!");
  u8g2Fonts.setForegroundColor(ST77XX_WHITE);
  u8g2Fonts.setCursor(40, 130);
  u8g2Fonts.print("SDA GPIO32");
  u8g2Fonts.setCursor(40, 155);
  u8g2Fonts.print("SCL GPIO33");
}

void calibrateSensor() {
  tft.fillScreen(ST77XX_BLACK);
  u8g2Fonts.setForegroundColor(ST77XX_YELLOW);
  u8g2Fonts.setCursor(55, 75);
  u8g2Fonts.print("КАЛИБРОВКА");
  u8g2Fonts.setForegroundColor(ST77XX_WHITE);
  u8g2Fonts.setCursor(28, 120);
  u8g2Fonts.print("ҚҰРЫЛҒЫНЫ ҚОЗҒАЛТПАҢЫЗ");
  u8g2Fonts.setCursor(78, 155);
  u8g2Fonts.print("КҮТІҢІЗ...");

  baseX = 0;
  baseY = 0;
  baseZ = 0;
  const int samples = 200;

  for (int i = 0; i < samples; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    baseX += a.acceleration.x;
    baseY += a.acceleration.y;
    baseZ += a.acceleration.z;
    delay(10);
  }

  baseX /= samples;
  baseY /= samples;
  baseZ /= samples;

  Serial.println("Калибровка аяқталды");
  Serial.print("Base X: "); Serial.println(baseX);
  Serial.print("Base Y: "); Serial.println(baseY);
  Serial.print("Base Z: "); Serial.println(baseZ);

  tft.fillScreen(ST77XX_BLACK);
  u8g2Fonts.setForegroundColor(ST77XX_GREEN);
  u8g2Fonts.setCursor(48, 110);
  u8g2Fonts.print("КАЛИБРОВКА");
  u8g2Fonts.setCursor(63, 140);
  u8g2Fonts.print("АЯҚТАЛДЫ");
  delay(700);
}

void readSensor() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  xValue = a.acceleration.x - baseX;
  yValue = a.acceleration.y - baseY;
  zValue = a.acceleration.z - baseZ;
}

void calculateVibration() {
  vibration = sqrt(xValue * xValue + yValue * yValue + zValue * zValue);
}

void determineState() {
  if (vibration < WARNING_LEVEL) {
    currentState = SAFE_STATE;
    strongCounter = 0;
    telegramSent = false;
  } else if (vibration < MEDIUM_LEVEL) {
    currentState = WARNING_STATE;
    strongCounter = 0;
  } else if (vibration < STRONG_LEVEL) {
    currentState = MEDIUM_STATE;
    strongCounter = 0;
  } else {
    strongCounter++;
    if (strongCounter < STRONG_CONFIRM_SAMPLES) {
      currentState = MEDIUM_STATE;
      return;
    }
    currentState = STRONG_STATE;
    if (!telegramSent) sendAlarmTelegram();
  }
}

void controlOutputs() {
  switch (currentState) {
    case SAFE_STATE:
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(RED_LED, LOW);
      break;
    case WARNING_STATE:
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(RED_LED, LOW);
      break;
    case MEDIUM_STATE:
    case STRONG_STATE:
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(RED_LED, HIGH);
      break;
  }
}

void updateBuzzer() {
  unsigned long interval = 0;

  if (currentState == SAFE_STATE) {
    digitalWrite(BUZZER, LOW);
    buzzerState = false;
    return;
  }

  if (currentState == WARNING_STATE) interval = 350;
  else if (currentState == MEDIUM_STATE) interval = 180;
  else {
    digitalWrite(BUZZER, HIGH);
    return;
  }

  if (millis() - lastBuzzerChange >= interval) {
    lastBuzzerChange = millis();
    buzzerState = !buzzerState;
    digitalWrite(BUZZER, buzzerState);
  }
}

void saveGraph() {
  graphX[graphPosition] = xValue;
  graphY[graphPosition] = yValue;
  graphZ[graphPosition] = zValue;
  graphPosition++;
  if (graphPosition >= GRAPH_POINTS) graphPosition = 0;
}

void drawMainScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 240, 24, 0x2104);
  u8g2Fonts.setForegroundColor(ST77XX_WHITE);
  u8g2Fonts.setCursor(25, 18);
  u8g2Fonts.print("СЕЙСМИКАЛЫҚ БАҚЫЛАУ");
  drawGraphFrames();
}

void drawValues() {
  tft.fillRect(0, 26, 240, 42, ST77XX_BLACK);
  tft.setTextSize(1);

  tft.setTextColor(ST77XX_RED);
  tft.setCursor(5, 30);
  tft.print("X:");
  tft.print(xValue, 2);

  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(83, 30);
  tft.print("Y:");
  tft.print(yValue, 2);

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(160, 30);
  tft.print("Z:");
  tft.print(zValue, 2);

  u8g2Fonts.setForegroundColor(ST77XX_WHITE);
  u8g2Fonts.setCursor(5, 51);
  u8g2Fonts.print("Діріл: ");
  u8g2Fonts.print(vibration, 3);
  u8g2Fonts.print(" м/с2");

  u8g2Fonts.setCursor(5, 66);
  u8g2Fonts.print("Wi-Fi: ");
  u8g2Fonts.print(WiFi.status() == WL_CONNECTED ? "OK" : "ЖОҚ");
}

void drawGraphFrames() {
  drawFrame(GRAPH_X_TOP, ST77XX_RED, "X");
  drawFrame(GRAPH_Y_TOP, ST77XX_YELLOW, "Y");
  drawFrame(GRAPH_Z_TOP, ST77XX_CYAN, "Z");
}

void drawFrame(int top, uint16_t color, const char *labelText) {
  tft.drawRect(GRAPH_LEFT, top, GRAPH_WIDTH, GRAPH_HEIGHT, 0x4208);
  int center = top + GRAPH_HEIGHT / 2;
  tft.drawFastHLine(GRAPH_LEFT, center, GRAPH_WIDTH, 0x4208);
  tft.setTextColor(color);
  tft.setCursor(5, center - 3);
  tft.print(labelText);
}

void drawGraphs() {
  clearGraph(GRAPH_X_TOP);
  clearGraph(GRAPH_Y_TOP);
  clearGraph(GRAPH_Z_TOP);
  drawGraphFrames();

  float scale = 15.0;

  for (int i = 0; i < GRAPH_POINTS - 1; i++) {
    int index1 = (graphPosition + i) % GRAPH_POINTS;
    int index2 = (graphPosition + i + 1) % GRAPH_POINTS;

    int x1 = GRAPH_LEFT + 1 + i * (GRAPH_WIDTH - 3) / GRAPH_POINTS;
    int x2 = GRAPH_LEFT + 1 + (i + 1) * (GRAPH_WIDTH - 3) / GRAPH_POINTS;

    drawGraphLine(x1, x2, graphX[index1], graphX[index2], GRAPH_X_TOP, scale, ST77XX_RED);
    drawGraphLine(x1, x2, graphY[index1], graphY[index2], GRAPH_Y_TOP, scale, ST77XX_YELLOW);
    drawGraphLine(x1, x2, graphZ[index1], graphZ[index2], GRAPH_Z_TOP, scale, ST77XX_CYAN);
  }
}

void clearGraph(int top) {
  tft.fillRect(
    GRAPH_LEFT + 1,
    top + 1,
    GRAPH_WIDTH - 2,
    GRAPH_HEIGHT - 2,
    ST77XX_BLACK
  );
}

void drawGraphLine(
  int x1,
  int x2,
  float value1,
  float value2,
  int top,
  float scale,
  uint16_t color
) {
  int center = top + GRAPH_HEIGHT / 2;
  int y1 = center - value1 * scale;
  int y2 = center - value2 * scale;

  y1 = constrain(y1, top + 1, top + GRAPH_HEIGHT - 2);
  y2 = constrain(y2, top + 1, top + GRAPH_HEIGHT - 2);

  tft.drawLine(x1, y1, x2, y2, color);
}

void drawStatus() {
  tft.fillRect(0, 207, 240, 33, ST77XX_BLACK);

  switch (currentState) {
    case SAFE_STATE:
      tft.fillRect(0, 207, 240, 33, ST77XX_GREEN);
      u8g2Fonts.setForegroundColor(ST77XX_BLACK);
      u8g2Fonts.setCursor(78, 228);
      u8g2Fonts.print("ҚАУІПСІЗ");
      break;

    case WARNING_STATE:
      tft.fillRect(0, 207, 240, 33, ST77XX_YELLOW);
      u8g2Fonts.setForegroundColor(ST77XX_BLACK);
      u8g2Fonts.setCursor(78, 228);
      u8g2Fonts.print("ЕСКЕРТУ");
      break;

    case MEDIUM_STATE:
      tft.fillRect(0, 207, 240, 33, ST77XX_RED);
      u8g2Fonts.setForegroundColor(ST77XX_WHITE);
      u8g2Fonts.setCursor(45, 228);
      u8g2Fonts.print("ОРТАША ДІРІЛ");
      break;

    case STRONG_STATE:
      tft.fillRect(0, 207, 240, 33, ST77XX_RED);
      u8g2Fonts.setForegroundColor(ST77XX_WHITE);
      u8g2Fonts.setCursor(45, 220);
      u8g2Fonts.print("ҚАТТЫ ДІРІЛ!");
      u8g2Fonts.setCursor(55, 237);
      u8g2Fonts.print("TELEGRAM");
      break;
  }
}

void printSerial() {
  Serial.print("X: ");
  Serial.print(xValue, 3);
  Serial.print(" | Y: ");
  Serial.print(yValue, 3);
  Serial.print(" | Z: ");
  Serial.print(zValue, 3);
  Serial.print(" | Діріл: ");
  Serial.print(vibration, 3);
  Serial.print(" | ");

  switch (currentState) {
    case SAFE_STATE:    Serial.println("ҚАУІПСІЗ"); break;
    case WARNING_STATE: Serial.println("ЕСКЕРТУ"); break;
    case MEDIUM_STATE:  Serial.println("ОРТАША ДІРІЛ"); break;
    case STRONG_STATE:  Serial.println("ҚАТТЫ ДІРІЛ"); break;
  }
}
