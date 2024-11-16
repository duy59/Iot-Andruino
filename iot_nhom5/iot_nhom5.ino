#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "DHT.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

// Define WiFi credentials
#define WIFI_SSID "Duybeos"
#define WIFI_PASSWORD "vuquangduy"

// Define sensor and actuator pins
#define LED_PIN 19   // LED
#define FAN_PIN 18   // FAN
#define MIST_PIN 21  // Mist
#define LDR_PIN 34
#define DHT_PIN 4

// Define other constants
#define maxLux 500
#define DHTTYPE DHT11

// Firebase credentials
#define API_KEY "AIzaSyAUmPWMOmENxc3AiwojQvCCMj7HBKWafC4"
#define DATABASE_URL "https://appfirebase1-d1c0a-default-rtdb.asia-southeast1.firebasedatabase.app/"

// Initialize DHT sensor
DHT dht(DHT_PIN, DHTTYPE);

// Initialize Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Firestore Data object
FirebaseData fbdoFirestore;

// Initialize NTP Client với timeOffset = 7 * 3600 (UTC+7)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600, 60000); // UTC+7, cập nhật mỗi 60 giây

// Timing variables
unsigned long sendDataPrevMillis = 0;
bool signupOK = false;

// Sensor data variables
int ldrData = 0;
float temperature = 0.0;
float humidity = 0.0;

// Project ID
String projectID;

// Token status callback function
void tokenStatusCallback(firebase_auth_token_info_t info) {
  if (info.status == token_status_error) {
    Serial.printf("Token error: %s\n", info.error.message.c_str());
  }
}

void setup() {
  // Initialize Serial
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // Extract projectID from DATABASE_URL
  String url = DATABASE_URL;
  int startIndex = url.indexOf("https://") + 8;
  int endIndex = url.indexOf("-default-rtdb");
  projectID = url.substring(startIndex, endIndex);
  Serial.print("Project ID: ");
  Serial.println(projectID);

  // Initialize DHT sensor
  dht.begin();

  // Configure Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // Set token callback
  config.token_status_callback = tokenStatusCallback;

  // Initialize Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Sign up to Firebase
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Sign up OK");
    signupOK = true;
  } else {
    Serial.printf("Sign up failed: %s\n", config.signer.signupError.message.c_str());
  }

  // Initialize device pins
  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(MIST_PIN, OUTPUT);

  // Start NTP Client
  timeClient.begin();
  Serial.println("NTP Client started.");

  // Chờ NTP Client đồng bộ thời gian
  Serial.print("Waiting for NTP time sync");
  while(!timeClient.update()){
    timeClient.forceUpdate();
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nNTP time synchronized.");
}

void loop() {
  // Update NTP Client
  timeClient.update();

  // Check if Firebase is ready and signup is OK
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 5000 || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();
    Serial.println("Reading sensors and updating Firebase...");

    // Read LDR
    ldrData = analogRead(LDR_PIN);
    float voltage = ldrData * (3.3 / 4095.0);
    float lux = 500 - (voltage / 3.3) * maxLux;

    // Read DHT sensors
    humidity = dht.readHumidity();
    temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    // Update Realtime Database
    if (Firebase.RTDB.setFloat(&fbdo, "Sensor/lux", lux)) {
      Serial.println("Lux value updated successfully.");
    } else {
      Serial.println("FAILED: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setFloat(&fbdo, "Sensor/temperature", temperature)) {
      Serial.println("Temperature value updated successfully.");
    } else {
      Serial.println("FAILED: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setFloat(&fbdo, "Sensor/humidity", humidity)) {
      Serial.println("Humidity value updated successfully.");
    } else {
      Serial.println("FAILED: " + fbdo.errorReason());
    }

    // Lấy thời gian hiện tại theo UTC+7
    time_t now = timeClient.getEpochTime();

    struct tm *timeInfo = localtime(&now); // Sử dụng localtime vì đã thiết lập timeOffset
    char timestampStr[30];
    snprintf(timestampStr, sizeof(timestampStr), "%04d-%02d-%02dT%02d:%02d:%02d+07:00",
             timeInfo->tm_year + 1900, timeInfo->tm_mon + 1, timeInfo->tm_mday,
             timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);

    // Đọc trạng thái thiết bị
    bool ledState = false;
    if (Firebase.RTDB.getBool(&fbdo, "devices/LED/state")) {
      ledState = fbdo.boolData();
      Serial.println("LED state updated.");
    } else {
      Serial.println("LED state not found. Creating with default value.");
      if (Firebase.RTDB.setBool(&fbdo, "devices/LED/state", false)) {
        Serial.println("LED state created with default value false.");
      } else {
        Serial.println("Failed to create LED state: " + fbdo.errorReason());
      }
    }
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);

    bool fanState = false;
    if (Firebase.RTDB.getBool(&fbdo, "devices/FAN/state")) {
      fanState = fbdo.boolData();
      Serial.println("FAN state updated.");
    } else {
      Serial.println("FAN state not found. Creating with default value.");
      if (Firebase.RTDB.setBool(&fbdo, "devices/FAN/state", false)) {
        Serial.println("FAN state created with default value false.");
      } else {
        Serial.println("Failed to create FAN state: " + fbdo.errorReason());
      }
    }
    digitalWrite(FAN_PIN, fanState ? HIGH : LOW);

    bool mistState = false;
    if (Firebase.RTDB.getBool(&fbdo, "devices/Mist/state")) {
      mistState = fbdo.boolData();
      Serial.println("Mist state updated.");
    } else {
      Serial.println("Mist state not found. Creating with default value.");
      if (Firebase.RTDB.setBool(&fbdo, "devices/Mist/state", false)) {
        Serial.println("Mist state created with default value false.");
      } else {
        Serial.println("Failed to create Mist state: " + fbdo.errorReason());
      }
    }
    digitalWrite(MIST_PIN, mistState ? HIGH : LOW);

    // Create JSON to send to Firestore
    FirebaseJson json;
    json.set("fields/lux/doubleValue", lux);
    json.set("fields/temperature/doubleValue", temperature);
    json.set("fields/humidity/doubleValue", humidity);
    json.set("fields/timestamp/timestampValue", timestampStr); // Sử dụng timestampValue với UTC+7
    json.set("fields/ledState/booleanValue", ledState);
    json.set("fields/fanState/booleanValue", fanState);
    json.set("fields/mistState/booleanValue", mistState);

    // Write to Firestore
    if (Firebase.Firestore.createDocument(&fbdoFirestore, projectID.c_str(), "", "SensorData", "", json.raw(), "")) {
      Serial.println("Data written to Firestore successfully");
    } else {
      Serial.println("Error writing to Firestore: " + fbdoFirestore.errorReason());
    }

    // Update device states from Firebase
    if (Firebase.RTDB.getBool(&fbdo, "devices/LED/state")) {
      digitalWrite(LED_PIN, fbdo.boolData() ? HIGH : LOW);
      Serial.println("LED state updated.");
    } else {
      Serial.println("Failed to get LED state: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.getBool(&fbdo, "devices/FAN/state")) {
      digitalWrite(FAN_PIN, fbdo.boolData() ? HIGH : LOW);
      Serial.println("FAN state updated.");
    } else {
      Serial.println("Failed to get FAN state: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.getBool(&fbdo, "devices/Mist/state")) {
      digitalWrite(MIST_PIN, fbdo.boolData() ? HIGH : LOW);
      Serial.println("Mist state updated.");
    } else {
      Serial.println("Failed to get Mist state: " + fbdo.errorReason());
    }
  }
}