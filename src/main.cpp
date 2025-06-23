#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <time.h>       // For time functions
#include <sys/time.h>   // For settimeofday
extern "C" {
#include "user_interface.h" // For WiFi sleep functions
}

// Pin definitions
const int SOLENOID_1_PIN = D2;
const int SOLENOID_2_PIN = D3;
const int SOLENOID_3_PIN = D4;
const int BUTTON_1_PIN = D7;
const int BUTTON_2_PIN = D6;

// Button state variables
bool button1LastState = HIGH;     // Confirmed stable state
bool button2LastState = HIGH;     // Confirmed stable state
bool button1PrevReading = HIGH;   // Previous raw reading
bool button2PrevReading = HIGH;   // Previous raw reading
unsigned long button1PressTime = 0;
unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
const unsigned long debounceDelay = 50; // ms
bool button1LongPressDetected = false;

// Solenoid state variables
bool solenoid1Active = false;
bool solenoid2Active = false;
bool solenoid3Active = false;
unsigned long solenoid1StartTime = 0;
unsigned long solenoid2StartTime = 0;
unsigned long solenoid3StartTime = 0;

// Settings
struct SolenoidSettings {
  unsigned long onTime; // in minutes
  uint8_t scheduleHour;   // 0-23
  uint8_t scheduleMinute; // 0-59
  bool scheduleEnabled;
};

SolenoidSettings solenoid1Settings = {1, 12, 0, false}; // Default: 1 min, 12:00, disabled
SolenoidSettings solenoid2Settings = {1, 12, 0, false};
SolenoidSettings solenoid3Settings = {1, 12, 0, false};

// WiFi and webserver
const char* ssid = "SolenoidController";
const char* password = "12345678";
ESP8266WebServer server(80);
bool apActive = false;
unsigned long wifiStartTime = 0;
const unsigned long WIFI_AUTO_OFF_TIME = 20 * 60 * 1000; // 20 minutes in milliseconds

// Timekeeping
time_t now;
struct tm timeinfo;
bool time_synced = false;
int last_run_day[3] = {-1, -1, -1}; // Tracks day of year for last schedule run (0=S1, 1=S2, 2=S3)


// EEPROM addresses
const int EEPROM_SIZE = 512;
const int EEPROM_MAGIC_NUMBER_ADDR = 0; // uint32_t (4 bytes)

const int EEPROM_SOLENOID1_ONTIME_ADDR = EEPROM_MAGIC_NUMBER_ADDR + sizeof(uint32_t); // unsigned long (4 bytes)
const int EEPROM_SOLENOID1_SCHED_HOUR_ADDR = EEPROM_SOLENOID1_ONTIME_ADDR + sizeof(unsigned long); // uint8_t (1 byte)
const int EEPROM_SOLENOID1_SCHED_MIN_ADDR = EEPROM_SOLENOID1_SCHED_HOUR_ADDR + sizeof(uint8_t);   // uint8_t (1 byte)
const int EEPROM_SOLENOID1_SCHED_ENABLED_ADDR = EEPROM_SOLENOID1_SCHED_MIN_ADDR + sizeof(uint8_t); // uint8_t (1 byte, bool)

const int EEPROM_SOLENOID2_ONTIME_ADDR = EEPROM_SOLENOID1_SCHED_ENABLED_ADDR + sizeof(uint8_t);
const int EEPROM_SOLENOID2_SCHED_HOUR_ADDR = EEPROM_SOLENOID2_ONTIME_ADDR + sizeof(unsigned long);
const int EEPROM_SOLENOID2_SCHED_MIN_ADDR = EEPROM_SOLENOID2_SCHED_HOUR_ADDR + sizeof(uint8_t);
const int EEPROM_SOLENOID2_SCHED_ENABLED_ADDR = EEPROM_SOLENOID2_SCHED_MIN_ADDR + sizeof(uint8_t);

const int EEPROM_SOLENOID3_ONTIME_ADDR = EEPROM_SOLENOID2_SCHED_ENABLED_ADDR + sizeof(uint8_t);
const int EEPROM_SOLENOID3_SCHED_HOUR_ADDR = EEPROM_SOLENOID3_ONTIME_ADDR + sizeof(unsigned long);
const int EEPROM_SOLENOID3_SCHED_MIN_ADDR = EEPROM_SOLENOID3_SCHED_HOUR_ADDR + sizeof(uint8_t);
const int EEPROM_SOLENOID3_SCHED_ENABLED_ADDR = EEPROM_SOLENOID3_SCHED_MIN_ADDR + sizeof(uint8_t);

const uint32_t EEPROM_MAGIC_NUMBER = 0xA1B2C3D5; // Updated magic number for new structure


// Function prototypes
void handleRoot();
void handleGetSettings();
void handleUpdateSettings();
void handleActivateSolenoid1();
void handleActivateSolenoid2();
void handleActivateSolenoid3();
void activateSolenoid(int solenoidNum, unsigned long duration);
void deactivateSolenoid(int solenoidNum);
void loadSettings();
void saveSettings();
void handleButtons();
void setupAccessPoint();
void shutdownWiFiCompletely(); // Function to properly shut down WiFi for power saving
void log(String message);
void handleSetTime(); // New handler for time synchronization
void checkScheduledEvents(); // New function for schedule logic
String padZero(int number); // Helper function to pad numbers with leading zero

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nSolenoid Controller starting...");
  
  pinMode(SOLENOID_1_PIN, OUTPUT);
  pinMode(SOLENOID_2_PIN, OUTPUT);
  pinMode(SOLENOID_3_PIN, OUTPUT);
  pinMode(BUTTON_1_PIN, INPUT_PULLUP);
  pinMode(BUTTON_2_PIN, INPUT_PULLUP);
  
  digitalWrite(SOLENOID_1_PIN, LOW);
  digitalWrite(SOLENOID_2_PIN, LOW);
  digitalWrite(SOLENOID_3_PIN, LOW);
  
  EEPROM.begin(EEPROM_SIZE);
  loadSettings();

  for (int i = 0; i < 3; ++i) {
    last_run_day[i] = -1; // Initialize last run day to ensure first schedule runs
  }
  
  // Configure NTP with timezone support
  configTime(2 * 3600, 0, "pool.ntp.org", "time.google.com"); // UTC+2 (Berlin timezone)
  
  // Wait for NTP sync
  Serial.println("Waiting for NTP time sync...");
  unsigned long ntpTimeout = millis() + 2000; // 10 second timeout
  while (time(nullptr) < 1000000000 && millis() < ntpTimeout) { // Wait until we have a reasonable timestamp
    delay(100);
  }
  
  if (time(nullptr) >= 1000000000) {
    time_synced = true;
    time(&now);
    localtime_r(&now, &timeinfo);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    log("NTP time synchronized: " + String(timeStr));
  } else {
    log("NTP sync failed, will rely on manual time setting");
  }

  log("Solenoid Controller initialized");
  log("Solenoid 1 (D2), Solenoid 2 (D3), Solenoid 3 (D4)");
  log("Button 1 (D7): Long press (>5s) for WiFi AP (if not auto-started), short press for Solenoids 1 & 2");
  log("Button 2 (D6): Short press for Solenoid 3");
  
  log("Automatically starting WiFi Access Point...");
  setupAccessPoint();
}

void loop() {
  handleButtons();
  unsigned long currentTime = millis();
  
  if (solenoid1Active && (currentTime - solenoid1StartTime >= solenoid1Settings.onTime * 60000UL)) {
    deactivateSolenoid(1);
    solenoid1Active = false;
  }
  if (solenoid2Active && (currentTime - solenoid2StartTime >= solenoid2Settings.onTime * 60000UL)) {
    deactivateSolenoid(2);
    solenoid2Active = false;
  }
  if (solenoid3Active && (currentTime - solenoid3StartTime >= solenoid3Settings.onTime * 60000UL)) {
    deactivateSolenoid(3);
    solenoid3Active = false;
  }
  
  if (apActive) {
    server.handleClient();
    if (MDNS.isRunning()) {
        MDNS.update();
    }
    if (currentTime - wifiStartTime >= WIFI_AUTO_OFF_TIME) {
      if (WiFi.softAPgetStationNum() == 0) {
        log("No active WiFi connections for 20 minutes. Shutting down WiFi completely...");
        shutdownWiFiCompletely();
      } else {
        wifiStartTime = currentTime;
        log("Active WiFi connections detected. Keeping WiFi on.");
      }
    }
  }

  checkScheduledEvents();
}

void checkScheduledEvents() {
  if (!time_synced) {
    return; // Don't run schedules if time is not known
  }

  time(&now); // Get current time_t
  localtime_r(&now, &timeinfo); // Convert to struct tm

  // Create a unique day identifier that works across year boundaries
  int currentDay = timeinfo.tm_year * 1000 + timeinfo.tm_yday;
  
  // Check schedules with improved timing logic
  // Allow trigger within a 2-minute window to avoid missing exact minute
  
  // Solenoid 1 Schedule
  if (solenoid1Settings.scheduleEnabled && currentDay != last_run_day[0]) {
    int scheduleMinutes = solenoid1Settings.scheduleHour * 60 + solenoid1Settings.scheduleMinute;
    int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    
    // Trigger if we're within 2 minutes of scheduled time or past it (but haven't run today)
    if (currentMinutes >= scheduleMinutes && currentMinutes <= scheduleMinutes + 2) {
      log("Solenoid 1 scheduled activation (" + String(solenoid1Settings.scheduleHour) + ":" + padZero(solenoid1Settings.scheduleMinute) + ")");
      if (!solenoid1Active) {
        activateSolenoid(1, solenoid1Settings.onTime * 60000UL);
        solenoid1Active = true;
        solenoid1StartTime = millis();
      } else {
        log("Solenoid 1 was already active, schedule trigger ignored for now.");
      }
      last_run_day[0] = currentDay;
    }
  }

  // Solenoid 2 Schedule
  if (solenoid2Settings.scheduleEnabled && currentDay != last_run_day[1]) {
    int scheduleMinutes = solenoid2Settings.scheduleHour * 60 + solenoid2Settings.scheduleMinute;
    int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    
    if (currentMinutes >= scheduleMinutes && currentMinutes <= scheduleMinutes + 2) {
      log("Solenoid 2 scheduled activation (" + String(solenoid2Settings.scheduleHour) + ":" + padZero(solenoid2Settings.scheduleMinute) + ")");
      if (!solenoid2Active) {
        activateSolenoid(2, solenoid2Settings.onTime * 60000UL);
        solenoid2Active = true;
        solenoid2StartTime = millis();
      } else {
        log("Solenoid 2 was already active, schedule trigger ignored for now.");
      }
      last_run_day[1] = currentDay;
    }
  }

  // Solenoid 3 Schedule
  if (solenoid3Settings.scheduleEnabled && currentDay != last_run_day[2]) {
    int scheduleMinutes = solenoid3Settings.scheduleHour * 60 + solenoid3Settings.scheduleMinute;
    int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    
    if (currentMinutes >= scheduleMinutes && currentMinutes <= scheduleMinutes + 2) {
      log("Solenoid 3 scheduled activation (" + String(solenoid3Settings.scheduleHour) + ":" + padZero(solenoid3Settings.scheduleMinute) + ")");
      if (!solenoid3Active) {
        activateSolenoid(3, solenoid3Settings.onTime * 60000UL);
        solenoid3Active = true;
        solenoid3StartTime = millis();
      } else {
        log("Solenoid 3 was already active, schedule trigger ignored for now.");
      }
      last_run_day[2] = currentDay;
    }
  }
}


void handleButtons() {
  bool button1Reading = digitalRead(BUTTON_1_PIN);
  bool button2Reading = digitalRead(BUTTON_2_PIN);
  unsigned long currentTime = millis();
  
  if (button1Reading != button1PrevReading) {
    lastDebounceTime1 = currentTime;
    button1PrevReading = button1Reading;
  }
  
  if ((currentTime - lastDebounceTime1) > debounceDelay) {
    if (button1Reading != button1LastState) {
        button1LastState = button1Reading;
        if (button1Reading == LOW) {
            button1PressTime = currentTime;
            button1LongPressDetected = false;
            log("Button 1 (D7) pressed.");
        } else {
            if (!button1LongPressDetected && (currentTime - button1PressTime < 5000)) {
                log("Short press on Button 1 (D7). Activating Solenoids 1 & 2.");
                if (!solenoid1Active) {
                    activateSolenoid(1, solenoid1Settings.onTime * 60000UL);
                    solenoid1Active = true;
                    solenoid1StartTime = currentTime;
                }
                if (!solenoid2Active) {
                    activateSolenoid(2, solenoid2Settings.onTime * 60000UL);
                    solenoid2Active = true;
                    solenoid2StartTime = currentTime;
                }
            }
            button1LongPressDetected = false; 
        }
    } else if (button1Reading == LOW && !button1LongPressDetected) {
        if ((currentTime - button1PressTime) > 5000) {
            button1LongPressDetected = true;
            log("Long press on Button 1 (D7). Ensuring Access Point is active.");
            if (!apActive) {
                setupAccessPoint(); // Try to start AP if not active
            } else {
                wifiStartTime = currentTime; // Reset AP timeout if button held
                log("AP already active. Activity timer reset.");
            }
        }
    }
  }
  
  if (button2Reading != button2PrevReading) {
    lastDebounceTime2 = currentTime;
    button2PrevReading = button2Reading;
  }
  
  if ((currentTime - lastDebounceTime2) > debounceDelay) {
    if (button2Reading != button2LastState) {
        button2LastState = button2Reading;
        if (button2Reading == LOW) {
            log("Button 2 (D6) pressed. Activating Solenoid 3.");
            if (!solenoid3Active) {
                activateSolenoid(3, solenoid3Settings.onTime * 60000UL);
                solenoid3Active = true;
                solenoid3StartTime = currentTime;
            }
        }
    }
  }
}

void setupAccessPoint() {
  if (apActive) {
    log("WiFi Access Point is already active.");
    wifiStartTime = millis(); // Reset AP timer on explicit call
    return;
  }
  
  log("Setting up WiFi Access Point...");
  
  // Wake up WiFi if it was in forced sleep mode
  WiFi.forceSleepWake();
  delay(100); // Give time for WiFi to wake up
  
  // Ensure WiFi is in the correct mode
  WiFi.mode(WIFI_AP);
  
  // Start the Access Point
  if (!WiFi.softAP(ssid, password)) {
    log("Failed to start Access Point! Retrying...");
    delay(500);
    WiFi.softAP(ssid, password);
  }
  
  IPAddress myIP = WiFi.softAPIP();
  log("AP IP address: " + myIP.toString());
  
  if (MDNS.begin("solenoid")) {
    MDNS.addService("http", "tcp", 80);
    log("MDNS responder started. Access at http://solenoid.local");
  } else {
    log("Error setting up MDNS responder!");
  }
  
  server.on("/", HTTP_GET, handleRoot);
  server.on("/settings", HTTP_GET, handleGetSettings);
  server.on("/settings", HTTP_POST, handleUpdateSettings);
  server.on("/settime", HTTP_POST, handleSetTime); // New endpoint for time sync
  server.on("/activateSolenoid1", HTTP_POST, handleActivateSolenoid1);
  server.on("/activateSolenoid2", HTTP_POST, handleActivateSolenoid2);
  server.on("/activateSolenoid3", HTTP_POST, handleActivateSolenoid3);
  
  server.begin();
  apActive = true;
  wifiStartTime = millis();
  log("HTTP server started");
}

void shutdownWiFiCompletely() {
  log("Initiating complete WiFi shutdown for power saving...");
  
  // Stop all active web server operations
  server.stop();
  log("Web server stopped");
  
  // Stop mDNS responder
  if (MDNS.isRunning()) {
    MDNS.end();
    log("MDNS responder stopped");
  }
  
  // Disconnect all connected stations and stop AP
  WiFi.softAPdisconnect(true);
  log("Access Point disconnected");
  
  // Wait a moment for clean disconnection
  delay(100);
  
  // Ensure WiFi is completely off
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  log("WiFi radio disabled and forced into sleep mode");
  
  // Additional power saving - turn off WiFi modem completely
  delay(100);
  
  // Set flag to indicate WiFi is off
  apActive = false;
  
  // Enable modem sleep mode for maximum power savings when WiFi is off
  // This will significantly reduce power consumption
  wifi_set_sleep_type(MODEM_SLEEP_T);
  
  log("Complete WiFi shutdown successful - significant power reduction achieved");
  log("Use long press on Button 1 (D7) to reactivate WiFi when needed");
}

void handleSetTime() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256); // Sufficient for time data
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
      log("JSON Deserialization error for settime: " + String(error.c_str()));
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON for settime\"}");
      return;
    }

    struct tm t_info;
    t_info.tm_year = doc["year"].as<int>() - 1900;
    t_info.tm_mon = doc["month"].as<int>(); // JS month 0-11 -> tm_mon 0-11
    t_info.tm_mday = doc["day"].as<int>();
    t_info.tm_hour = doc["hour"].as<int>();
    t_info.tm_min = doc["minute"].as<int>();
    t_info.tm_sec = doc["second"].as<int>();
    t_info.tm_isdst = -1; // Auto-detect DST based on system rules (if any configured)

    time_t calculated_time = mktime(&t_info);
    
    if (calculated_time == -1) {
        log("Error: mktime failed to convert provided time.");
        server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to interpret time data\"}");
        return;
    }

    struct timeval tv = { .tv_sec = calculated_time, .tv_usec = 0 };
    if (settimeofday(&tv, nullptr) == 0) {
        time_synced = true;
        // Update global timeinfo to reflect the newly set time immediately
        localtime_r(&calculated_time, &timeinfo); 
        log("Time synchronized from browser: " + String(asctime(&timeinfo))); // asctime adds newline
        
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Time updated\",\"time\":\"" + String(buf) + "\"}");
    } else {
        log("Error: settimeofday failed.");
        server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to set system time\"}");
    }
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No data provided for settime\"}");
  }
}


void handleRoot() {
  const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Solenoid Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f0f2f5; color: #333; }
    .container { max-width: 600px; margin: 20px auto; background-color: white; padding: 25px; border-radius: 10px; box-shadow: 0 4px 12px rgba(0, 0, 0, 0.1); }
    h1 { color: #1558b0; text-align: center; margin-bottom: 25px; }
    .solenoid-group { margin-bottom: 25px; padding: 20px; border: 1px solid #dfe1e5; border-radius: 8px; background-color: #f8f9fa; }
    .solenoid-group h2 { margin-top: 0; color: #34495e; font-size: 1.3em; border-bottom: 1px solid #dfe1e5; padding-bottom: 10px; margin-bottom: 15px; display: flex; align-items: center; justify-content: space-between; }
    label { display: inline-block; width: 140px; margin-bottom: 8px; font-weight: 500; vertical-align: middle; }
    input[type="number"], input[type="time"] { padding: 10px; border: 1px solid #ccc; border-radius: 5px; width: 100px; box-sizing: border-box; margin-right:10px; vertical-align: middle;}
    button { background-color: #1a73e8; color: white; border: none; padding: 10px 18px; border-radius: 5px; cursor: pointer; font-size: 0.95em; transition: background-color 0.2s; vertical-align: middle;}
    button:hover { background-color: #1558b0; }
    .save-button { background-color: #28a745; display: block; width: 100%; padding: 12px; font-size: 1.1em; margin-top: 10px;}
    .save-button:hover { background-color: #218838; }
    .status { margin-top: 20px; padding: 12px; border-radius: 5px; display: none; text-align: center; }
    .success { background-color: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
    .error { background-color: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
    .setting-row { margin-bottom: 10px; }
    
    /* Slide Switch Styles */
    .switch { position: relative; display: inline-block; width: 60px; height: 34px; vertical-align: middle; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }
    .slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
    input:checked + .slider { background-color: #1a73e8; }
    input:focus + .slider { box-shadow: 0 0 1px #1a73e8; }
    input:checked + .slider:before { transform: translateX(26px); }
    
    .switch-label { font-weight: normal; width: auto; vertical-align: middle; }
    .title-switch { display: flex; align-items: center; }
    .timer-control { display: flex; justify-content: space-between; align-items: center; width: 100%; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Water Control</h1>
    <div id="currentTime" class="status success" style="display:none; margin-bottom:15px;"></div>
    <form id="settingsForm">
      <div class="solenoid-group">
        <h2>
          <span style="color: #759f2b;">Solenoid 1 (Pin D2)</span>
          <div class="title-switch">
            <label class="switch">
              <input type="checkbox" id="testSolenoid1">
              <span class="slider"></span>
            </label>
          </div>
        </h2>
        <div class="setting-row">
          <label for="solenoid1OnTime">ON Time (min):</label>
          <input type="number" id="solenoid1OnTime" name="solenoid1OnTime" min="1" step="1" value="1">
        </div>
        <div class="setting-row">
          <label for="solenoid1SchedTime">Schedule (HH:MM):</label>
          <input type="time" id="solenoid1SchedTime" name="solenoid1SchedTime">
          <div class="timer-control">
            <label for="solenoid1SchedEnabled" class="switch-label">Enable timer</label>
            <label class="switch">
              <input type="checkbox" id="solenoid1SchedEnabled" name="solenoid1SchedEnabled">
              <span class="slider"></span>
            </label>
          </div>
        </div>
      </div>
      
      <div class="solenoid-group">
        <h2>
          <span style="color: #759f2b;">Solenoid 2 (Pin D3)</span>
          <div class="title-switch">
            <label class="switch">
              <input type="checkbox" id="testSolenoid2">
              <span class="slider"></span>
            </label>
          </div>
        </h2>
        <div class="setting-row">
          <label for="solenoid2OnTime">ON Time (min):</label>
          <input type="number" id="solenoid2OnTime" name="solenoid2OnTime" min="1" step="1" value="1">
        </div>
        <div class="setting-row">
          <label for="solenoid2SchedTime">Schedule (HH:MM):</label>
          <input type="time" id="solenoid2SchedTime" name="solenoid2SchedTime">
          <div class="timer-control">
            <label for="solenoid2SchedEnabled" class="switch-label">Enable timer</label>
            <label class="switch">
              <input type="checkbox" id="solenoid2SchedEnabled" name="solenoid2SchedEnabled">
              <span class="slider"></span>
            </label>
          </div>
        </div>
      </div>
      
      <div class="solenoid-group">
        <h2>
          <span style="color: #759f2b;">Solenoid 3 (Pin D4)</span>
          <div class="title-switch">
            <label class="switch">
              <input type="checkbox" id="testSolenoid3">
              <span class="slider"></span>
            </label>
          </div>
        </h2>
        <div class="setting-row">
          <label for="solenoid3OnTime">ON Time (min):</label>
          <input type="number" id="solenoid3OnTime" name="solenoid3OnTime" min="1" step="1" value="1">
        </div>
        <div class="setting-row">
          <label for="solenoid3SchedTime">Schedule (HH:MM):</label>
          <input type="time" id="solenoid3SchedTime" name="solenoid3SchedTime">
          <div class="timer-control">
            <label for="solenoid3SchedEnabled" class="switch-label">Enable timer</label>
            <label class="switch">
              <input type="checkbox" id="solenoid3SchedEnabled" name="solenoid3SchedEnabled">
              <span class="slider"></span>
            </label>
          </div>
        </div>
      </div>
      
    </form>
    
    <div id="statusMessage" class="status"></div>
  </div>

  <script>
    function showStatus(message, isSuccess, elementId = 'statusMessage') {
      const statusElement = document.getElementById(elementId);
      statusElement.textContent = message;
      statusElement.className = 'status ' + (isSuccess ? 'success' : 'error');
      statusElement.style.display = 'block';
      if (elementId === 'statusMessage') {
        setTimeout(() => { statusElement.style.display = 'none'; }, 3000);
      }
    }

    document.addEventListener('DOMContentLoaded', function() {
      // Sync time with ESP
      const now = new Date();
      const timeData = {
        year: now.getFullYear(),
        month: now.getMonth(), // JS month is 0-11
        day: now.getDate(),
        hour: now.getHours(),
        minute: now.getMinutes(),
        second: now.getSeconds()
      };
      fetch('/settime', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(timeData)
      })
      .then(response => response.json())
      .then(data => {
        if (data.status === 'success') {
          showStatus('Controller Time: ' + data.time, true, 'currentTime');
        } else {
          showStatus('Time sync failed: ' + (data.message || ''), false, 'currentTime');
        }
      })
      .catch(error => {
          console.error('Error syncing time:', error);
          showStatus('Time sync fetch error.', false, 'currentTime');
      });

      // Fetch current settings
      fetch('/settings')
        .then(response => response.json())
        .then(data => {
          document.getElementById('solenoid1OnTime').value = data.solenoid1OnTime;
          document.getElementById('solenoid1SchedTime').value = String(data.solenoid1SchedHour).padStart(2, '0') + ':' + String(data.solenoid1SchedMin).padStart(2, '0');
          document.getElementById('solenoid1SchedEnabled').checked = data.solenoid1SchedEnabled;

          document.getElementById('solenoid2OnTime').value = data.solenoid2OnTime;
          document.getElementById('solenoid2SchedTime').value = String(data.solenoid2SchedHour).padStart(2, '0') + ':' + String(data.solenoid2SchedMin).padStart(2, '0');
          document.getElementById('solenoid2SchedEnabled').checked = data.solenoid2SchedEnabled;

          document.getElementById('solenoid3OnTime').value = data.solenoid3OnTime;
          document.getElementById('solenoid3SchedTime').value = String(data.solenoid3SchedHour).padStart(2, '0') + ':' + String(data.solenoid3SchedMin).padStart(2, '0');
          document.getElementById('solenoid3SchedEnabled').checked = data.solenoid3SchedEnabled;
        })
        .catch(error => {
          console.error('Error fetching settings:', error);
          showStatus('Failed to load settings.', false);
        });
      
      document.getElementById('settingsForm').addEventListener('submit', function(e) {
        e.preventDefault();
        const s1TimeParts = document.getElementById('solenoid1SchedTime').value.split(':');
        const s2TimeParts = document.getElementById('solenoid2SchedTime').value.split(':');
        const s3TimeParts = document.getElementById('solenoid3SchedTime').value.split(':');

        const formData = {
          solenoid1OnTime: parseInt(document.getElementById('solenoid1OnTime').value),
          solenoid1SchedHour: parseInt(s1TimeParts[0]),
          solenoid1SchedMin: parseInt(s1TimeParts[1]),
          solenoid1SchedEnabled: document.getElementById('solenoid1SchedEnabled').checked,

          solenoid2OnTime: parseInt(document.getElementById('solenoid2OnTime').value),
          solenoid2SchedHour: parseInt(s2TimeParts[0]),
          solenoid2SchedMin: parseInt(s2TimeParts[1]),
          solenoid2SchedEnabled: document.getElementById('solenoid2SchedEnabled').checked,

          solenoid3OnTime: parseInt(document.getElementById('solenoid3OnTime').value),
          solenoid3SchedHour: parseInt(s3TimeParts[0]),
          solenoid3SchedMin: parseInt(s3TimeParts[1]),
          solenoid3SchedEnabled: document.getElementById('solenoid3SchedEnabled').checked
        };
        
        fetch('/settings', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(formData)
        })
        .then(response => response.json())
        .then(data => {
          if (data.status === 'success') {
            showStatus('Settings saved successfully!', true);
          } else {
            showStatus('Failed to save settings: ' + (data.message || ''), false);
          }
        })
        .catch(error => {
          console.error('Error saving settings:', error);
          showStatus('Failed to save settings. Please try again.', false);
        });
      });
      
      function createTestSwitchHandler(solenoidNum) {
        return function() {
          const switchElement = document.getElementById('testSolenoid' + solenoidNum);
          fetch('/activateSolenoid' + solenoidNum, { method: 'POST' })
          .then(response => response.json())
          .then(data => {
            if (data.status === 'success') {
              if (data.state === 'on') {
                showStatus(`Solenoid ${solenoidNum} turned ON!`, true);
                switchElement.checked = true;
              } else {
                showStatus(`Solenoid ${solenoidNum} turned OFF!`, true);
                switchElement.checked = false;
              }
            } else {
              showStatus(`Failed to toggle Solenoid ${solenoidNum}: ` + (data.message || ''), false);
              // Revert the switch state on error
              switchElement.checked = !switchElement.checked;
            }
          })
          .catch(error => {
            console.error('Error toggling solenoid:', error);
            showStatus(`Error toggling Solenoid ${solenoidNum}.`, false);
            // Revert the switch state on error
            switchElement.checked = !switchElement.checked;
          });
        };
      }
      
      document.getElementById('testSolenoid1').addEventListener('change', createTestSwitchHandler(1));
      document.getElementById('testSolenoid2').addEventListener('change', createTestSwitchHandler(2));
      document.getElementById('testSolenoid3').addEventListener('change', createTestSwitchHandler(3));
      
      // Auto-save functionality
      function autoSaveSettings() {
        const s1TimeParts = document.getElementById('solenoid1SchedTime').value.split(':');
        const s2TimeParts = document.getElementById('solenoid2SchedTime').value.split(':');
        const s3TimeParts = document.getElementById('solenoid3SchedTime').value.split(':');

        const formData = {
          solenoid1OnTime: parseInt(document.getElementById('solenoid1OnTime').value),
          solenoid1SchedHour: parseInt(s1TimeParts[0]),
          solenoid1SchedMin: parseInt(s1TimeParts[1]),
          solenoid1SchedEnabled: document.getElementById('solenoid1SchedEnabled').checked,

          solenoid2OnTime: parseInt(document.getElementById('solenoid2OnTime').value),
          solenoid2SchedHour: parseInt(s2TimeParts[0]),
          solenoid2SchedMin: parseInt(s2TimeParts[1]),
          solenoid2SchedEnabled: document.getElementById('solenoid2SchedEnabled').checked,

          solenoid3OnTime: parseInt(document.getElementById('solenoid3OnTime').value),
          solenoid3SchedHour: parseInt(s3TimeParts[0]),
          solenoid3SchedMin: parseInt(s3TimeParts[1]),
          solenoid3SchedEnabled: document.getElementById('solenoid3SchedEnabled').checked
        };
        
        fetch('/settings', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(formData)
        })
        .then(response => response.json())
        .then(data => {
          if (data.status === 'success') {
            showStatus('Settings auto-saved!', true);
          } else {
            showStatus('Auto-save failed: ' + (data.message || ''), false);
          }
        })
        .catch(error => {
          console.error('Error auto-saving settings:', error);
          showStatus('Auto-save error. Please check connection.', false);
        });
      }
      
      // Add auto-save event listeners to all input fields
      document.getElementById('solenoid1OnTime').addEventListener('change', autoSaveSettings);
      document.getElementById('solenoid1SchedTime').addEventListener('change', autoSaveSettings);
      document.getElementById('solenoid1SchedEnabled').addEventListener('change', autoSaveSettings);
      
      document.getElementById('solenoid2OnTime').addEventListener('change', autoSaveSettings);
      document.getElementById('solenoid2SchedTime').addEventListener('change', autoSaveSettings);
      document.getElementById('solenoid2SchedEnabled').addEventListener('change', autoSaveSettings);
      
      document.getElementById('solenoid3OnTime').addEventListener('change', autoSaveSettings);
      document.getElementById('solenoid3SchedTime').addEventListener('change', autoSaveSettings);
      document.getElementById('solenoid3SchedEnabled').addEventListener('change', autoSaveSettings);
    });
  </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleGetSettings() {
  DynamicJsonDocument doc(512); // Increased size for more fields
  doc["solenoid1OnTime"] = solenoid1Settings.onTime;
  doc["solenoid1SchedHour"] = solenoid1Settings.scheduleHour;
  doc["solenoid1SchedMin"] = solenoid1Settings.scheduleMinute;
  doc["solenoid1SchedEnabled"] = solenoid1Settings.scheduleEnabled;

  doc["solenoid2OnTime"] = solenoid2Settings.onTime;
  doc["solenoid2SchedHour"] = solenoid2Settings.scheduleHour;
  doc["solenoid2SchedMin"] = solenoid2Settings.scheduleMinute;
  doc["solenoid2SchedEnabled"] = solenoid2Settings.scheduleEnabled;

  doc["solenoid3OnTime"] = solenoid3Settings.onTime;
  doc["solenoid3SchedHour"] = solenoid3Settings.scheduleHour;
  doc["solenoid3SchedMin"] = solenoid3Settings.scheduleMinute;
  doc["solenoid3SchedEnabled"] = solenoid3Settings.scheduleEnabled;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleUpdateSettings() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    DynamicJsonDocument doc(512); // Increased size
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
      log("JSON Deserialization error for settings: " + String(error.c_str()));
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON for settings\"}");
      return;
    }
    
    bool settingsChanged = false;
    // Solenoid 1
    if (doc.containsKey("solenoid1OnTime")) { solenoid1Settings.onTime = doc["solenoid1OnTime"]; settingsChanged = true; }
    if (doc.containsKey("solenoid1SchedHour")) { solenoid1Settings.scheduleHour = doc["solenoid1SchedHour"]; settingsChanged = true; }
    if (doc.containsKey("solenoid1SchedMin")) { solenoid1Settings.scheduleMinute = doc["solenoid1SchedMin"]; settingsChanged = true; }
    if (doc.containsKey("solenoid1SchedEnabled")) { solenoid1Settings.scheduleEnabled = doc["solenoid1SchedEnabled"]; settingsChanged = true; }

    // Solenoid 2
    if (doc.containsKey("solenoid2OnTime")) { solenoid2Settings.onTime = doc["solenoid2OnTime"]; settingsChanged = true; }
    if (doc.containsKey("solenoid2SchedHour")) { solenoid2Settings.scheduleHour = doc["solenoid2SchedHour"]; settingsChanged = true; }
    if (doc.containsKey("solenoid2SchedMin")) { solenoid2Settings.scheduleMinute = doc["solenoid2SchedMin"]; settingsChanged = true; }
    if (doc.containsKey("solenoid2SchedEnabled")) { solenoid2Settings.scheduleEnabled = doc["solenoid2SchedEnabled"]; settingsChanged = true; }

    // Solenoid 3
    if (doc.containsKey("solenoid3OnTime")) { solenoid3Settings.onTime = doc["solenoid3OnTime"]; settingsChanged = true; }
    if (doc.containsKey("solenoid3SchedHour")) { solenoid3Settings.scheduleHour = doc["solenoid3SchedHour"]; settingsChanged = true; }
    if (doc.containsKey("solenoid3SchedMin")) { solenoid3Settings.scheduleMinute = doc["solenoid3SchedMin"]; settingsChanged = true; }
    if (doc.containsKey("solenoid3SchedEnabled")) { solenoid3Settings.scheduleEnabled = doc["solenoid3SchedEnabled"]; settingsChanged = true; }
    
    if (settingsChanged) {
        saveSettings();
        log("Settings updated via web interface.");
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Settings updated\"}");
    } else {
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"No changes detected\"}");
    }
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No data provided for settings\"}");
  }
}

void handleActivateSolenoid(int solenoidNum, bool& activeFlag, unsigned long& startTime, unsigned long onTimeInMinutes) {
    String solenoidName = "Solenoid " + String(solenoidNum);
    if (!activeFlag) {
        activateSolenoid(solenoidNum, onTimeInMinutes * 60000UL); // Duration in ms
        activeFlag = true;
        startTime = millis();
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"" + solenoidName + " activated\",\"state\":\"on\"}");
    } else {
        deactivateSolenoid(solenoidNum);
        activeFlag = false;
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"" + solenoidName + " deactivated\",\"state\":\"off\"}");
    }
}

void handleActivateSolenoid1() { handleActivateSolenoid(1, solenoid1Active, solenoid1StartTime, solenoid1Settings.onTime); }
void handleActivateSolenoid2() { handleActivateSolenoid(2, solenoid2Active, solenoid2StartTime, solenoid2Settings.onTime); }
void handleActivateSolenoid3() { handleActivateSolenoid(3, solenoid3Active, solenoid3StartTime, solenoid3Settings.onTime); }

void activateSolenoid(int solenoidNum, unsigned long durationMs) {
  int pinToActivate = -1;
  switch(solenoidNum) {
    case 1: pinToActivate = SOLENOID_1_PIN; break;
    case 2: pinToActivate = SOLENOID_2_PIN; break;
    case 3: pinToActivate = SOLENOID_3_PIN; break;
    default: log("Invalid solenoid number for activation: " + String(solenoidNum)); return;
  }
  
  digitalWrite(pinToActivate, HIGH);
  String pinName = (pinToActivate == D2) ? "D2" : (pinToActivate == D3) ? "D3" : (pinToActivate == D4) ? "D4" : String(pinToActivate);
  log("Solenoid " + String(solenoidNum) + " (Pin " + pinName + ") turned ON for " + String(durationMs / 60000.0, 2) + " minutes");
}

void deactivateSolenoid(int solenoidNum) {
  int pinToDeactivate = -1;
  switch(solenoidNum) {
    case 1: pinToDeactivate = SOLENOID_1_PIN; break;
    case 2: pinToDeactivate = SOLENOID_2_PIN; break;
    case 3: pinToDeactivate = SOLENOID_3_PIN; break;
    default: log("Invalid solenoid number for deactivation: " + String(solenoidNum)); return;
  }

  digitalWrite(pinToDeactivate, LOW);
  String pinName = (pinToDeactivate == D2) ? "D2" : (pinToDeactivate == D3) ? "D3" : (pinToDeactivate == D4) ? "D4" : String(pinToDeactivate);
  log("Solenoid " + String(solenoidNum) + " (Pin " + pinName + ") turned OFF");
}

void loadSettings() {
  uint32_t magicNumber;
  EEPROM.get(EEPROM_MAGIC_NUMBER_ADDR, magicNumber);
  
  if (magicNumber == EEPROM_MAGIC_NUMBER) {
    EEPROM.get(EEPROM_SOLENOID1_ONTIME_ADDR, solenoid1Settings.onTime);
    EEPROM.get(EEPROM_SOLENOID1_SCHED_HOUR_ADDR, solenoid1Settings.scheduleHour);
    EEPROM.get(EEPROM_SOLENOID1_SCHED_MIN_ADDR, solenoid1Settings.scheduleMinute);
    EEPROM.get(EEPROM_SOLENOID1_SCHED_ENABLED_ADDR, solenoid1Settings.scheduleEnabled);

    EEPROM.get(EEPROM_SOLENOID2_ONTIME_ADDR, solenoid2Settings.onTime);
    EEPROM.get(EEPROM_SOLENOID2_SCHED_HOUR_ADDR, solenoid2Settings.scheduleHour);
    EEPROM.get(EEPROM_SOLENOID2_SCHED_MIN_ADDR, solenoid2Settings.scheduleMinute);
    EEPROM.get(EEPROM_SOLENOID2_SCHED_ENABLED_ADDR, solenoid2Settings.scheduleEnabled);

    EEPROM.get(EEPROM_SOLENOID3_ONTIME_ADDR, solenoid3Settings.onTime);
    EEPROM.get(EEPROM_SOLENOID3_SCHED_HOUR_ADDR, solenoid3Settings.scheduleHour);
    EEPROM.get(EEPROM_SOLENOID3_SCHED_MIN_ADDR, solenoid3Settings.scheduleMinute);
    EEPROM.get(EEPROM_SOLENOID3_SCHED_ENABLED_ADDR, solenoid3Settings.scheduleEnabled);
    
    log("Settings loaded from EEPROM.");
  } else {
    log("EEPROM magic number mismatch or uninitialized. Using default settings and saving.");
    // Default settings are already in structs, so just save them.
    saveSettings();
  }
  // Log current settings after loading or defaulting
  log("S1: OnTime=" + String(solenoid1Settings.onTime) + "m, Sched=" + String(solenoid1Settings.scheduleHour) + ":" + padZero(solenoid1Settings.scheduleMinute) + " En=" + solenoid1Settings.scheduleEnabled);
  log("S2: OnTime=" + String(solenoid2Settings.onTime) + "m, Sched=" + String(solenoid2Settings.scheduleHour) + ":" + padZero(solenoid2Settings.scheduleMinute) + " En=" + solenoid2Settings.scheduleEnabled);
  log("S3: OnTime=" + String(solenoid3Settings.onTime) + "m, Sched=" + String(solenoid3Settings.scheduleHour) + ":" + padZero(solenoid3Settings.scheduleMinute) + " En=" + solenoid3Settings.scheduleEnabled);
}

void saveSettings() {
  EEPROM.put(EEPROM_MAGIC_NUMBER_ADDR, EEPROM_MAGIC_NUMBER);

  EEPROM.put(EEPROM_SOLENOID1_ONTIME_ADDR, solenoid1Settings.onTime);
  EEPROM.put(EEPROM_SOLENOID1_SCHED_HOUR_ADDR, solenoid1Settings.scheduleHour);
  EEPROM.put(EEPROM_SOLENOID1_SCHED_MIN_ADDR, solenoid1Settings.scheduleMinute);
  EEPROM.put(EEPROM_SOLENOID1_SCHED_ENABLED_ADDR, solenoid1Settings.scheduleEnabled);

  EEPROM.put(EEPROM_SOLENOID2_ONTIME_ADDR, solenoid2Settings.onTime);
  EEPROM.put(EEPROM_SOLENOID2_SCHED_HOUR_ADDR, solenoid2Settings.scheduleHour);
  EEPROM.put(EEPROM_SOLENOID2_SCHED_MIN_ADDR, solenoid2Settings.scheduleMinute);
  EEPROM.put(EEPROM_SOLENOID2_SCHED_ENABLED_ADDR, solenoid2Settings.scheduleEnabled);

  EEPROM.put(EEPROM_SOLENOID3_ONTIME_ADDR, solenoid3Settings.onTime);
  EEPROM.put(EEPROM_SOLENOID3_SCHED_HOUR_ADDR, solenoid3Settings.scheduleHour);
  EEPROM.put(EEPROM_SOLENOID3_SCHED_MIN_ADDR, solenoid3Settings.scheduleMinute);
  EEPROM.put(EEPROM_SOLENOID3_SCHED_ENABLED_ADDR, solenoid3Settings.scheduleEnabled);
  
  if (EEPROM.commit()) {
    log("Settings saved to EEPROM.");
  } else {
    log("ERROR: Failed to save settings to EEPROM!");
  }
}

String padZero(int number) {
  if (number < 10) {
    return "0" + String(number);
  }
  return String(number);
}

void log(String message) {
  String timestamp = "[" + String(millis() / 1000.0, 3) + "s] ";
  if (time_synced) {
      char timeStr[20];
      time(&now); // ensure `now` is current
      localtime_r(&now, &timeinfo);
      strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
      timestamp = "[" + String(timeStr) + "] ";
  }
  Serial.println(timestamp + message);
}
