#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "DFRobot_SHT40.h"
#include <time.h>

// --- WiFi & Firebase config ---
#define WIFI_SSID       "<YOUR_WIFI_SSID>"
#define WIFI_PASSWORD   "<YOUR_WIFI_PASSWORD>"
#define API_KEY         "<YOUR_API_KEY>"
#define USER_EMAIL      "<YOUR_USER_EMAIL>"
#define USER_PASSWORD   "<YOUR_USER_PASSWORD>"
#define DATABASE_URL    "<YOUR_DATABASE_URL>"

// --- Relay pin definitions (5 relays) ---
#define RELAY1_PIN 32  // LIGHT
#define RELAY2_PIN 33  // RIGHT PUMP (In-soil)
#define RELAY3_PIN 25  // BUFFER PUMP
#define RELAY4_PIN 26  // FAN
#define RELAY5_PIN 27  // LEFT PUMP (Sprinkler)

// --- Threshold for buffer pump from reservoir level ---
#define BUFFER_LEVEL_THRESHOLD 30   // % (turn ON when below)

// --- Sensor pin definitions ---
#define A02YYUW_TX       17
#define A02YYUW_RX       16
#define SensorPin        15
#define WATER_LEVEL_PIN  2

const int AirValue   = 300;
const int WaterValue = 0;

FirebaseData fbdo;
FirebaseConfig config;
FirebaseAuth auth;
String uid, basePath, relayBasePath;
unsigned long lastSend = 0;
const unsigned long sendInterval = 180000; // 3 minutes

HardwareSerial modbusSerial(2);
HardwareSerial ultraSerial(1);
DFRobot_SHT40 SHT40(SHT40_AD1B_IIC_ADDR);

float temperature = 0, humidity = 0;
int soilMoisturePercent = 0, waterPercent = 0;
uint16_t nitrogen = 0, phosphorus = 0, potassium = 0;
float waterLevelEma = -1;

// Helper to send relay status
void sendRelayStatus(const String &name, bool isOn) {
  String status = isOn ? "ON" : "OFF";
  Firebase.RTDB.setString(&fbdo, (relayBasePath + name).c_str(), status);
}
bool sendFloat(const String &path, float val) {
  return Firebase.RTDB.setFloat(&fbdo, path, val);
}
bool sendString(const String &path, const String &val) {
  return Firebase.RTDB.setString(&fbdo, path, val);
}

uint16_t read_npk(const byte* cmd) {
  modbusSerial.flush();
  modbusSerial.write(cmd, 8);
  vTaskDelay(pdMS_TO_TICKS(100));
  unsigned long start = millis();
  while (modbusSerial.available() < 7) {
    if (millis() - start > 1000) return 0xFFFF;
  }
  byte r[7];
  for (int i=0; i<7; i++) r[i] = modbusSerial.read();
  return (r[3] << 8) | r[4];
}

void moistureTask(void* pv) {
  while (1) {
    int raw = analogRead(SensorPin);
    soilMoisturePercent =
      constrain(map(raw, AirValue, WaterValue, 0, 100), 0, 100);
    Serial.printf("[Moisture] %d %%\n", soilMoisturePercent);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void shtTask(void* pv) {
  while (1) {
    temperature = SHT40.getTemperature(PRECISION_HIGH);
    humidity    = SHT40.getHumidity(PRECISION_HIGH);
    Serial.printf("[SHT40] Temp: %.2f C, Humidity: %.2f %%\n",
                  temperature, humidity);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void ultrasonicTask(void* pv) {
  const float alpha = 0.2, maxH = 28.0;
  while (1) {
    ultraSerial.write(0x55);
    delay(100);
    if (ultraSerial.available() >= 4) {
      uint8_t h = ultraSerial.read(), l = ultraSerial.read();
      ultraSerial.read(); ultraSerial.read();
      float cm = ((h<<8)|l) / 10.0;
      cm = min(cm, maxH);
      waterLevelEma = (waterLevelEma < 0)
                      ? cm
                      : alpha*cm + (1-alpha)*waterLevelEma;
      Serial.printf("[Ultra] EMA: %.2f cm\n", waterLevelEma);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void npkTask(void* pv) {
  const byte nitroCmd[] = {0x01,0x03,0x00,0x1e,0x00,0x01,0xe4,0x0c};
  const byte phosCmd[]  = {0x01,0x03,0x00,0x1f,0x00,0x01,0xb5,0xcc};
  const byte potaCmd[]  = {0x01,0x03,0x00,0x20,0x00,0x01,0x85,0xc0};
  while (1) {
    nitrogen   = read_npk(nitroCmd);
    phosphorus = read_npk(phosCmd);
    potassium  = read_npk(potaCmd);
    Serial.printf("[NPK] N:%u P:%u K:%u\n",
                  nitrogen, phosphorus, potassium);
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

void waterLevelTask(void* pv) {
  const int maxV = 1450;
  while (1) {
    int raw = analogRead(WATER_LEVEL_PIN);
    waterPercent = constrain((raw>maxV?maxV:raw)*100/maxV, 0, 100);
    Serial.printf("[WaterLvl] %d %%\n", waterPercent);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

String getTimestamp() {
  time_t now = time(nullptr);
  struct tm tmInfo;
  localtime_r(&now, &tmInfo);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M", &tmInfo);
  return String(buf);
}

void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("WiFi connected: " + WiFi.localIP().toString());
}

void setup() {
  Serial.begin(115200);
  ultraSerial.begin(9600, SERIAL_8N1, A02YYUW_RX, A02YYUW_TX);
  Wire.begin();
  SHT40.begin();
  modbusSerial.begin(4800, SERIAL_8N1, 4, 5);

  // Relay pins
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(RELAY3_PIN, OUTPUT);
  pinMode(RELAY4_PIN, OUTPUT);
  pinMode(RELAY5_PIN, OUTPUT);

  initWiFi();
  configTime(7*3600, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 100000) delay(500);

  config.api_key               = API_KEY;
  config.database_url          = DATABASE_URL;
  auth.user.email              = USER_EMAIL;
  auth.user.password           = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);

  Serial.print("Getting UID");
  while (auth.token.uid == "") {
    delay(500);
    Serial.print(".");
  }
  uid           = auth.token.uid.c_str();
  basePath      = "/SensorsData/" + uid + "/";
  relayBasePath = basePath + "relays/";

  // Initial relay states
  sendRelayStatus("Light",      true);
  sendRelayStatus("Right Pump", false);
  sendRelayStatus("Buffer Pump", false);
  sendRelayStatus("Fan",        false);
  sendRelayStatus("Left Pump",  false);

  // Start FreeRTOS tasks
  xTaskCreate(moistureTask,    "Moisture",  2048, NULL, 1, NULL);
  xTaskCreate(shtTask,         "SHT40",     4096, NULL, 1, NULL);
  xTaskCreate(ultrasonicTask,  "Ultra",     4096, NULL, 1, NULL);
  xTaskCreate(npkTask,         "NPK",       4096, NULL, 1, NULL);
  xTaskCreate(waterLevelTask,  "WaterLvl",  2048, NULL, 1, NULL);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost. Reconnecting...");
    initWiFi();
  }

  // Get current time
  time_t now = time(nullptr);
  struct tm timeInfo;
  localtime_r(&now, &timeInfo);
  int currentHour = timeInfo.tm_hour;
  int currentMin  = timeInfo.tm_min;

  // --- Buffer Pump (Relay 3) Control ---
  static bool bufferPumpOn = false;
  if (waterPercent < BUFFER_LEVEL_THRESHOLD && !bufferPumpOn) {
    digitalWrite(RELAY3_PIN, HIGH);
    sendRelayStatus("Buffer Pump", true);
    Serial.println("[Relay] Buffer Pump ON (reservoir low)");
    bufferPumpOn = true;
  }
  else if (waterPercent >= BUFFER_LEVEL_THRESHOLD && bufferPumpOn) {
    digitalWrite(RELAY3_PIN, LOW);
    sendRelayStatus("Buffer Pump", false);
    Serial.println("[Relay] Buffer Pump OFF (reservoir OK)");
    bufferPumpOn = false;
  }

  // --- Timed Watering (Sprinkler & In-soil) ---
  static int lastCheckedMin = -1;
  if (currentMin != lastCheckedMin) {
    lastCheckedMin = currentMin;

    // Sprinkler (Relay 5) at 06:00
    if (currentHour == 6 && currentMin == 0) {
      digitalWrite(RELAY5_PIN, HIGH);
      sendRelayStatus("Left Pump", true);
      Serial.println("[Relay] Sprinkler ON at 06:00");
      delay(5000);  // watering duration
      digitalWrite(RELAY5_PIN, LOW);
      sendRelayStatus("Left Pump", false);
      Serial.println("[Relay] Sprinkler OFF");
    }

    // In-soil (Relay 2) at 06:00 & 16:00
    if ((currentHour == 6 || currentHour == 16) && currentMin == 0) {
      digitalWrite(RELAY2_PIN, HIGH);
      sendRelayStatus("Right Pump", true);
      Serial.println("[Relay] In-soil ON");
      delay(5000);  // watering duration
      digitalWrite(RELAY2_PIN, LOW);
      sendRelayStatus("Right Pump", false);
      Serial.println("[Relay] In-soil OFF");
    }
  }

  if(Serial.available() > 0){
    int data = Serial.read();
    switch(data){
      case '1':
        digitalWrite(RELAY1_PIN, LOW);
        Serial.println("[Relay] Light is ON (Normally ON)");
        break;
      case '2':
        digitalWrite(RELAY1_PIN, HIGH);
        Serial.println("[Relay] Light is OFF (Normally ON)");
        break;
      case '3':
        digitalWrite(RELAY2_PIN, HIGH);
        Serial.println("[Relay] Right Pump is ON");
        break;
      case '4':
        digitalWrite(RELAY2_PIN, LOW);
        Serial.println("[Relay] Right Pump is OFF");
        break;
      case '5':
        digitalWrite(RELAY3_PIN, HIGH);
        Serial.println("[Relay] Buffer Bump is ON");
        break;
      case '6':
        digitalWrite(RELAY3_PIN, LOW);
        Serial.println("[Relay] Buffer Pump is OFF");
        break;
      case '7':
        digitalWrite(RELAY4_PIN, HIGH);
        Serial.println("[Relay] Fan is ON");
        break;
      case '8':
        digitalWrite(RELAY4_PIN, LOW);
        Serial.println("[Relay] Fan is HIGH");
        break;
      case '9':
        digitalWrite(RELAY5_PIN, HIGH);
        Serial.println("[Relay] Left Pump is ON");
        break;
      case 'B':
        digitalWrite(RELAY5_PIN, LOW);
        Serial.println("[Relay] Left Pump is OFF");
        break;
      case 'O':
        digitalWrite(RELAY1_PIN, HIGH);
        digitalWrite(RELAY2_PIN, LOW);
        digitalWrite(RELAY3_PIN, LOW);
        digitalWrite(RELAY4_PIN, LOW);
        digitalWrite(RELAY5_PIN, LOW);
        Serial.println("[Relay] Everything is OFF");
        break;
    }
  }

  // --- Firebase data send ---
  if (Firebase.ready() && millis() - lastSend > sendInterval) {
    lastSend = millis();
    Serial.println("[Firebase] Sending data...");

    // sensor data
    sendFloat(basePath + "temperature",    temperature);
    sendFloat(basePath + "humidity",       humidity);
    sendFloat(basePath + "moisture",       soilMoisturePercent);
    sendFloat(basePath + "waterlevel",     waterLevelEma);
    sendFloat(basePath + "wateranalog",    waterPercent);
    sendFloat(basePath + "natrium",        nitrogen);
    sendFloat(basePath + "phosphorus",     phosphorus);
    sendFloat(basePath + "kalium",         potassium);

    // relay states
    sendRelayStatus("Light",      digitalRead(RELAY1_PIN));
    sendRelayStatus("Right Pump", digitalRead(RELAY2_PIN));
    sendRelayStatus("Buffer Pump",digitalRead(RELAY3_PIN));
    sendRelayStatus("Fan",        digitalRead(RELAY4_PIN));
    sendRelayStatus("Left Pump",  digitalRead(RELAY5_PIN));

    // timestamp
    sendString(basePath + "timestamp", getTimestamp());
  }
}
