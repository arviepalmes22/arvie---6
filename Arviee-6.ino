
#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <DHT.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

// =====================================================
// ARVIE ACTIVITY 6 - ESP32 DHT11 + FIREBASE + RELAYS
// =====================================================

// FIREBASE - ARVIE
#define API_KEY "AIzaSyDQLkZt4-fYHwlQhV8KGmJAJ8y2n5JtqOo"
#define USER_EMAIL "arviepalmes22@gmail.com"
#define USER_PASSWORD "arvie22palmes"
#define DATABASE_URL "https://arviee-7474c-default-rtdb.europe-west1.firebasedatabase.app"

// HARDWARE
#define DHT_PIN 4
#define DHT_TYPE DHT11
#define RELAY1_PIN 26
#define RELAY2_PIN 27

// RELAYS: active LOW
const int RELAY_ON = LOW;
const int RELAY_OFF = HIGH;

// TIMING
const unsigned long SAMPLE_INTERVAL = 10000;
const unsigned long WIFI_TIMEOUT = 20000;
const long GMT_OFFSET_SEC = 8L * 60L * 60L;
const int DAYLIGHT_OFFSET_SEC = 0;

// OBJECTS
DHT dht(DHT_PIN, DHT_TYPE);
WebServer server(80);
Preferences preferences;

WiFiClientSecure sslClient;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(sslClient);

UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);
FirebaseApp app;
RealtimeDatabase Database;

// STATES
bool relay1State = false;
bool relay2State = false;
bool firebaseStarted = false;
bool firebaseReadyMessageShown = false;
bool wifiConfigured = false;

unsigned long lastSampleTime = 0;
unsigned long wifiConnectStart = 0;

String savedSSID;
String savedPassword;

// =====================================================
// WIFI MANAGER PAGE FALLBACK
// =====================================================

const char WIFI_FALLBACK[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Arvie WiFi Manager</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{min-height:100vh;display:flex;align-items:center;justify-content:center;
padding:20px;font-family:Arial;background:#0b1220;color:white}
.container{width:100%;max-width:450px;background:#111827;border:1px solid #1f2937;
border-radius:18px;padding:30px}
.logo{width:60px;height:60px;margin:0 auto 18px;display:flex;align-items:center;
justify-content:center;border-radius:16px;background:#06b6d4;color:#07111f;
font-size:28px;font-weight:bold}
h1{text-align:center;font-size:25px;margin-bottom:8px}
.subtitle{text-align:center;color:#9ca3af;font-size:14px;margin-bottom:28px}
label{display:block;margin-bottom:8px;color:#d1d5db;font-size:14px;font-weight:600}
input{width:100%;padding:13px 14px;margin-bottom:18px;border-radius:10px;
border:1px solid #374151;background:#0b1220;color:white;font-size:15px}
button{width:100%;padding:14px;border:0;border-radius:10px;background:#06b6d4;
color:#07111f;font-weight:bold;font-size:15px;cursor:pointer}
.note{margin-top:20px;padding:13px;border-radius:10px;background:#172033;
color:#94a3b8;font-size:12px;line-height:1.6;text-align:center}
</style>
</head>
<body>
<div class="container">
<div class="logo">A</div>
<h1>Arvie WiFi Manager</h1>
<p class="subtitle">Connect your ESP32 to your WiFi network</p>
<form method="POST" action="/save">
<label for="ssid">WiFi Name (SSID)</label>
<input id="ssid" name="ssid" type="text" placeholder="Enter WiFi name" required>
<label for="pass">WiFi Password</label>
<input id="pass" name="pass" type="password" placeholder="Enter WiFi password">
<button type="submit">SAVE WIFI SETTINGS</button>
</form>
<div class="note">After saving, the ESP32 will restart and connect to your WiFi. Open the IP address shown in Serial Monitor to access the dashboard.</div>
</div>
</body>
</html>
)rawliteral";

// =====================================================
// RELAY FUNCTIONS
// =====================================================

void setRelay(int relay, bool turnOn) {
  if (relay == 1) {
    relay1State = turnOn;
    digitalWrite(RELAY1_PIN, turnOn ? RELAY_ON : RELAY_OFF);
  } else if (relay == 2) {
    relay2State = turnOn;
    digitalWrite(RELAY2_PIN, turnOn ? RELAY_ON : RELAY_OFF);
  }
}

void handleRelayStatus() {
  String json = "{\"LED1\":";
  json += relay1State ? "true" : "false";
  json += ",\"LED2\":";
  json += relay2State ? "true" : "false";
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleRelay() {
  if (!server.hasArg("relay") || !server.hasArg("state")) {
    server.send(400, "application/json",
                "{\"success\":false,\"message\":\"Missing relay or state\"}");
    return;
  }

  int relay = server.arg("relay").toInt();
  String state = server.arg("state");
  state.toLowerCase();

  if ((relay != 1 && relay != 2) ||
      (state != "on" && state != "off")) {
    server.send(400, "application/json",
                "{\"success\":false,\"message\":\"Invalid relay or state\"}");
    return;
  }

  setRelay(relay, state == "on");

  server.send(200, "application/json", "{\"success\":true}");
  Serial.printf("[RELAY] Relay %d: %s\n",
                relay, state == "on" ? "ON" : "OFF");
}

// =====================================================
// FILE SERVER
// =====================================================

String contentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css")) return "text/css";
  if (filename.endsWith(".js")) return "application/javascript";
  if (filename.endsWith(".json")) return "application/json";
  if (filename.endsWith(".png")) return "image/png";
  if (filename.endsWith(".jpg")) return "image/jpeg";
  if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

void serveFile(String path) {
  if (!wifiConfigured || WiFi.status() != WL_CONNECTED) {
    if (LittleFS.exists("/wifi.html")) {
      File file = LittleFS.open("/wifi.html", "r");
      server.streamFile(file, "text/html");
      file.close();
    } else {
      server.send(200, "text/html", WIFI_FALLBACK);
    }
    return;
  }

  if (path == "/") path = "/index.html";

  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "File not found: " + path);
    return;
  }

  File file = LittleFS.open(path, "r");
  server.streamFile(file, contentType(path));
  file.close();
}

void handleRoot() {
  serveFile("/");
}

void handleStaticFile() {
  serveFile(server.uri());
}

// =====================================================
// WIFI SAVE
// =====================================================

void handleSaveWiFi() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "WiFi SSID is required.");
    return;
  }

  String newSSID = server.arg("ssid");
  String newPassword = server.arg("pass");

  newSSID.trim();

  if (newSSID.length() == 0) {
    server.send(400, "text/plain", "WiFi SSID cannot be empty.");
    return;
  }

  preferences.begin("arvie-wifi", false);
  preferences.putString("ssid", newSSID);
  preferences.putString("pass", newPassword);
  preferences.end();

  server.send(200, "text/html",
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:Arial;background:#0b1220;color:white;text-align:center;padding:50px}"
    "h2{color:#22d3ee}</style></head><body><h2>WiFi Settings Saved!</h2>"
    "<p>ESP32 is restarting. Connect your device to the same WiFi network.</p></body></html>");

  Serial.println("[WIFI MANAGER] New WiFi credentials saved.");
  delay(1500);
  ESP.restart();
}

// =====================================================
// WIFI CONNECTION
// =====================================================

void loadWiFiCredentials() {
  preferences.begin("arvie-wifi", true);
  savedSSID = preferences.getString("ssid", "");
  savedPassword = preferences.getString("pass", "");
  preferences.end();

  wifiConfigured = savedSSID.length() > 0;
}

void startWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);

  WiFi.softAP("Arvie_WiFi_");

  Serial.println("[WIFI MANAGER] Access Point started.");
  Serial.println("[WIFI MANAGER] Network: Arvie_WiFi_");
  Serial.print("[WIFI MANAGER] Portal: http://");
  Serial.println(WiFi.softAPIP());

  loadWiFiCredentials();

  if (wifiConfigured) {
    Serial.print("[WIFI] Connecting to: ");
    Serial.println(savedSSID);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    wifiConnectStart = millis();
  } else {
    Serial.println("[WIFI] No saved WiFi. Open http://192.168.4.1");
  }
}

void checkWiFiConnection() {
  if (!wifiConfigured) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (!firebaseStarted) {
      Serial.println("[WIFI] Connected successfully!");
      Serial.print("[WIFI] Dashboard IP: http://");
      Serial.println(WiFi.localIP());

      configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
                 "pool.ntp.org", "time.google.com", "time.nist.gov");

      sslClient.setInsecure();

      Serial.println("[FIREBASE] Initializing...");
      initializeApp(aClient, app, getAuth(user_auth));
      app.getApp<RealtimeDatabase>(Database);
      Database.url(DATABASE_URL);

      firebaseStarted = true;
    }
    return;
  }

  if (millis() - wifiConnectStart >= WIFI_TIMEOUT) {
    Serial.println("[WIFI] Could not connect. WiFi Manager remains available.");
    wifiConfigured = false;
    WiFi.disconnect();
  }
}

// =====================================================
// TIME
// =====================================================

bool getCurrentDateTime(String &date, String &clock) {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo, 1000)) {
    return false;
  }

  char dateBuffer[16];
  char timeBuffer[16];

  strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &timeinfo);
  strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &timeinfo);

  date = String(dateBuffer);
  clock = String(timeBuffer);
  return true;
}

// =====================================================
// FIREBASE SENSOR UPLOAD
// =====================================================

void uploadSensorReading() {
  if (!firebaseStarted || WiFi.status() != WL_CONNECTED) return;

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("[DHT11 ERROR] Failed to read sensor.");
    return;
  }

  String date;
  String clock;

  if (!getCurrentDateTime(date, clock)) {
    Serial.println("[TIME ERROR] No valid time. Reading not uploaded.");
    return;
  }

  String path = "/ESP32_Data/" + date + "/" + clock;

  String json = "{\"temperature\":" + String(temperature, 1) +
                ",\"humidity\":" + String(humidity, 1) + "}";

  Database.set<object_t>(aClient, path, object_t(json));

  Serial.printf("[%s %s] Temperature: %.1f C | Humidity: %.1f %%\n",
                date.c_str(), clock.c_str(), temperature, humidity);
}

// =====================================================
// WEB SERVER ROUTES
// =====================================================

void startWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSaveWiFi);
  server.on("/relay-status", HTTP_GET, handleRelayStatus);
  server.on("/relay", HTTP_GET, handleRelay);

  server.on("/index.html", HTTP_GET, []() { serveFile("/index.html"); });
  server.on("/wifi.html", HTTP_GET, []() { serveFile("/wifi.html"); });
  server.on("/style.css", HTTP_GET, []() { serveFile("/style.css"); });
  server.on("/script.js", HTTP_GET, []() { serveFile("/script.js"); });

  server.onNotFound(handleStaticFile);
  server.begin();

  Serial.println("[WEB SERVER] Started on port 80.");
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("======================================");
  Serial.println(" ARVIE ACTIVITY 6");
  Serial.println(" ESP32 DHT11 FIREBASE MONITOR");
  Serial.println("======================================");

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  setRelay(1, false);
  setRelay(2, false);

  Serial.println("[RELAY] Relay 1 and Relay 2 initialized OFF.");

  dht.begin();
  Serial.println("[DHT11] Sensor initialized.");

  if (!LittleFS.begin(true)) {
    Serial.println("[LittleFS ERROR] Mount failed.");
  } else {
    Serial.println("[LittleFS] Mounted successfully.");
  }

  startWiFi();
  startWebServer();

  Serial.println("[SYSTEM] Setup complete.");
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  server.handleClient();

  checkWiFiConnection();

  if (firebaseStarted) {
    app.loop();

    if (app.ready()) {
      if (!firebaseReadyMessageShown) {
        Serial.println("[FIREBASE] Connected and ready.");
        firebaseReadyMessageShown = true;
      }
    } else {
      firebaseReadyMessageShown = false;
    }
  }

  if (firebaseStarted && app.ready() &&
      millis() - lastSampleTime >= SAMPLE_INTERVAL) {
    lastSampleTime = millis();
    uploadSensorReading();
  }

  delay(2);
}
