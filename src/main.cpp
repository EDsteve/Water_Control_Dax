#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>

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
};

SolenoidSettings solenoid1Settings = {1}; // Default 1 minute
SolenoidSettings solenoid2Settings = {1}; // Default 1 minute
SolenoidSettings solenoid3Settings = {1}; // Default 1 minute

// WiFi and webserver
const char* ssid = "SolenoidController";
const char* password = "12345678";
ESP8266WebServer server(80);
bool apActive = false;
unsigned long wifiStartTime = 0;
const unsigned long WIFI_AUTO_OFF_TIME = 30 * 60 * 1000; // 30 minutes in milliseconds

// EEPROM addresses
const int EEPROM_SIZE = 512; // Size of EEPROM to use
const int EEPROM_MAGIC_NUMBER_ADDR = 0;
const int EEPROM_SOLENOID1_TIME_ADDR = EEPROM_MAGIC_NUMBER_ADDR + sizeof(uint32_t);
const int EEPROM_SOLENOID2_TIME_ADDR = EEPROM_SOLENOID1_TIME_ADDR + sizeof(unsigned long);
const int EEPROM_SOLENOID3_TIME_ADDR = EEPROM_SOLENOID2_TIME_ADDR + sizeof(unsigned long);
const uint32_t EEPROM_MAGIC_NUMBER = 0xA1B2C3D4; // To check if EEPROM is initialized

// Function prototypes
void handleRoot();
void handleGetSettings();
void handleUpdateSettings();
void handleActivateSolenoid1();
void handleActivateSolenoid2();
void handleActivateSolenoid3();
void activateSolenoid(int solenoidNum, unsigned long duration); // Changed pin to solenoidNum for clarity
void deactivateSolenoid(int solenoidNum); // Changed pin to solenoidNum for clarity
void loadSettings();
void saveSettings();
void handleButtons();
void setupAccessPoint();
void log(String message);

void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  Serial.println("\n\nSolenoid Controller starting...");
  
  // Initialize pins
  pinMode(SOLENOID_1_PIN, OUTPUT);
  pinMode(SOLENOID_2_PIN, OUTPUT);
  pinMode(SOLENOID_3_PIN, OUTPUT);
  pinMode(BUTTON_1_PIN, INPUT_PULLUP);
  pinMode(BUTTON_2_PIN, INPUT_PULLUP);
  
  // Ensure solenoids are off at startup
  digitalWrite(SOLENOID_1_PIN, LOW);
  digitalWrite(SOLENOID_2_PIN, LOW);
  digitalWrite(SOLENOID_3_PIN, LOW);
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadSettings();
  
  // Log startup info
  log("Solenoid Controller initialized");
  log("Solenoid 1 (D2), Solenoid 2 (D3), Solenoid 3 (D4)");
  log("Button 1 (D7): Long press (>5s) for WiFi AP, short press for Solenoids 1 & 2");
  log("Button 2 (D6): Short press for Solenoid 3");
  
  // Automatically turn on WiFi at startup
  log("Automatically starting WiFi Access Point...");
  setupAccessPoint();
  
  // Enable the Watchdog Timer
  // ESP.wdtEnable(8000); // 8 seconds timeout - causes issues with AP mode sometimes.
  // Consider enabling if stability issues arise and test thoroughly.
}

void loop() {
  // Reset watchdog timer if enabled
  // ESP.wdtFeed();
  
  // Handle button presses
  handleButtons();
  
  // Check if any solenoid needs to be turned off
  unsigned long currentTime = millis();
  
  if (solenoid1Active && (currentTime - solenoid1StartTime >= solenoid1Settings.onTime * 60000)) {
    deactivateSolenoid(1);
    solenoid1Active = false;
  }
  
  if (solenoid2Active && (currentTime - solenoid2StartTime >= solenoid2Settings.onTime * 60000)) {
    deactivateSolenoid(2);
    solenoid2Active = false;
  }
  
  if (solenoid3Active && (currentTime - solenoid3StartTime >= solenoid3Settings.onTime * 60000)) {
    deactivateSolenoid(3);
    solenoid3Active = false;
  }
  
  // Handle web server if active
  if (apActive) {
    server.handleClient();
    if (MDNS.isRunning()) {
        MDNS.update();
    }
    
    // Check if WiFi should be turned off due to inactivity
    if (currentTime - wifiStartTime >= WIFI_AUTO_OFF_TIME) {
      // Check if there are any active connections
      if (WiFi.softAPgetStationNum() == 0) {
        log("No active WiFi connections for 30 minutes. Turning off WiFi...");
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        apActive = false;
        log("WiFi turned off");
      } else {
        // Reset the timer if there are active connections
        wifiStartTime = currentTime;
        log("Active WiFi connections detected. Keeping WiFi on.");
      }
    }
  }
}

void handleButtons() {
  // Read button states
  bool button1Reading = digitalRead(BUTTON_1_PIN);
  bool button2Reading = digitalRead(BUTTON_2_PIN);
  unsigned long currentTime = millis();
  
  // Button 1 (D7) logic
  // Only reset debounce timer when the raw reading changes from previous reading
  if (button1Reading != button1PrevReading) {
    lastDebounceTime1 = currentTime;
    button1PrevReading = button1Reading;
  }
  
  if ((currentTime - lastDebounceTime1) > debounceDelay) {
    if (button1Reading != button1LastState) { // State has changed and is stable
        button1LastState = button1Reading;
        if (button1Reading == LOW) { // Button pressed
            button1PressTime = currentTime;
            button1LongPressDetected = false;
            log("Button 1 (D7) pressed.");
        } else { // Button released
            if (!button1LongPressDetected && (currentTime - button1PressTime < 5000)) {
                // Short press detected
                log("Short press on Button 1 (D7). Activating Solenoids 1 & 2.");
                if (!solenoid1Active) {
                    activateSolenoid(1, solenoid1Settings.onTime * 60000);
                    solenoid1Active = true;
                    solenoid1StartTime = currentTime;
                }
                if (!solenoid2Active) {
                    activateSolenoid(2, solenoid2Settings.onTime * 60000);
                    solenoid2Active = true;
                    solenoid2StartTime = currentTime;
                }
            }
            // Reset long press detection on release
            button1LongPressDetected = false; 
        }
    } else if (button1Reading == LOW && !button1LongPressDetected) { // Button held down
        if ((currentTime - button1PressTime) > 5000) {
            button1LongPressDetected = true;
            log("Long press on Button 1 (D7). Setting up Access Point.");
            if (!apActive) {
                setupAccessPoint();
            } else {
                log("AP already active.");
            }
        }
    }
  }
  
  // Button 2 (D6) logic
  // Only reset debounce timer when the raw reading changes from previous reading
  if (button2Reading != button2PrevReading) {
    lastDebounceTime2 = currentTime;
    button2PrevReading = button2Reading;
  }
  
  if ((currentTime - lastDebounceTime2) > debounceDelay) {
    if (button2Reading != button2LastState) { // State has changed and is stable
        button2LastState = button2Reading;
        if (button2Reading == LOW) { // Button pressed
            log("Button 2 (D6) pressed. Activating Solenoid 3.");
            if (!solenoid3Active) {
                activateSolenoid(3, solenoid3Settings.onTime * 60000);
                solenoid3Active = true;
                solenoid3StartTime = currentTime;
            }
        }
    }
  }
}

void setupAccessPoint() {
  // If WiFi is already active, don't set it up again
  if (apActive) {
    log("WiFi Access Point is already active.");
    return;
  }
  
  log("Setting up WiFi Access Point...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  log("AP IP address: " + myIP.toString());
  
  // Start mDNS responder
  if (MDNS.begin("solenoid")) { // Hostname for mDNS
    MDNS.addService("http", "tcp", 80);
    log("MDNS responder started. Access at http://solenoid.local");
  } else {
    log("Error setting up MDNS responder!");
  }
  
  // Setup server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/settings", HTTP_GET, handleGetSettings);
  server.on("/settings", HTTP_POST, handleUpdateSettings);
  server.on("/activateSolenoid1", HTTP_POST, handleActivateSolenoid1);
  server.on("/activateSolenoid2", HTTP_POST, handleActivateSolenoid2);
  server.on("/activateSolenoid3", HTTP_POST, handleActivateSolenoid3);
  
  // Start server
  server.begin();
  apActive = true;
  wifiStartTime = millis(); // Start the WiFi timer
  log("HTTP server started");
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
    h1 { color: #1a73e8; text-align: center; margin-bottom: 25px; }
    .solenoid-group { margin-bottom: 25px; padding: 20px; border: 1px solid #dfe1e5; border-radius: 8px; background-color: #f8f9fa; }
    .solenoid-group h2 { margin-top: 0; color: #34495e; font-size: 1.3em; border-bottom: 1px solid #dfe1e5; padding-bottom: 10px; margin-bottom: 15px; }
    label { display: inline-block; width: 140px; margin-bottom: 8px; font-weight: 500; }
    input[type="number"] { padding: 10px; border: 1px solid #ccc; border-radius: 5px; width: 100px; box-sizing: border-box; margin-right:10px; }
    button { background-color: #1a73e8; color: white; border: none; padding: 10px 18px; border-radius: 5px; cursor: pointer; font-size: 0.95em; transition: background-color 0.2s; }
    button:hover { background-color: #1558b0; }
    .save-button { background-color: #28a745; display: block; width: 100%; padding: 12px; font-size: 1.1em; margin-top: 10px;}
    .save-button:hover { background-color: #218838; }
    .test-button { background-color: #ffc107; color: #212529; }
    .test-button:hover { background-color: #e0a800; }
    .test-button.active { background-color: #dc3545; color: white; }
    .test-button.active:hover { background-color: #c82333; }
    .status { margin-top: 20px; padding: 12px; border-radius: 5px; display: none; text-align: center; }
    .success { background-color: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
    .error { background-color: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Solenoid Valve Controller</h1>
    
    <form id="settingsForm">
      <div class="solenoid-group">
        <h2>Solenoid 1 (Pin D2)</h2>
        <div>
          <label for="solenoid1Time">ON Time (min):</label>
          <input type="number" id="solenoid1Time" name="solenoid1Time" min="1" step="1" value="1">
          <button type="button" class="test-button" id="testSolenoid1">Turn ON</button>
        </div>
      </div>
      
      <div class="solenoid-group">
        <h2>Solenoid 2 (Pin D3)</h2>
        <div>
          <label for="solenoid2Time">ON Time (min):</label>
          <input type="number" id="solenoid2Time" name="solenoid2Time" min="1" step="1" value="1">
          <button type="button" class="test-button" id="testSolenoid2">Turn ON</button>
        </div>
      </div>
      
      <div class="solenoid-group">
        <h2>Solenoid 3 (Pin D4)</h2>
        <div>
          <label for="solenoid3Time">ON Time (min):</label>
          <input type="number" id="solenoid3Time" name="solenoid3Time" min="1" step="1" value="1">
          <button type="button" class="test-button" id="testSolenoid3">Turn ON</button>
        </div>
      </div>
      
      <button type="submit" class="save-button">Save All Settings</button>
    </form>
    
    <div id="statusMessage" class="status"></div>
  </div>

  <script>
    function showStatus(message, isSuccess) {
      const statusElement = document.getElementById('statusMessage');
      statusElement.textContent = message;
      statusElement.className = 'status ' + (isSuccess ? 'success' : 'error');
      statusElement.style.display = 'block';
      setTimeout(() => { statusElement.style.display = 'none'; }, 3000);
    }

    document.addEventListener('DOMContentLoaded', function() {
      fetch('/settings')
        .then(response => response.json())
        .then(data => {
          document.getElementById('solenoid1Time').value = data.solenoid1Time;
          document.getElementById('solenoid2Time').value = data.solenoid2Time;
          document.getElementById('solenoid3Time').value = data.solenoid3Time;
        })
        .catch(error => {
          console.error('Error fetching settings:', error);
          showStatus('Failed to load settings.', false);
        });
      
      document.getElementById('settingsForm').addEventListener('submit', function(e) {
        e.preventDefault();
        const formData = {
          solenoid1Time: parseInt(document.getElementById('solenoid1Time').value),
          solenoid2Time: parseInt(document.getElementById('solenoid2Time').value),
          solenoid3Time: parseInt(document.getElementById('solenoid3Time').value)
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
      
      function createTestButtonHandler(solenoidNum) {
        return function() {
          const button = document.getElementById('testSolenoid' + solenoidNum);
          fetch('/activateSolenoid' + solenoidNum, { method: 'POST' })
          .then(response => response.json())
          .then(data => {
            if (data.status === 'success') {
              if (data.state === 'on') {
                showStatus(`Solenoid ${solenoidNum} turned ON!`, true);
                button.textContent = 'Turn OFF';
                button.classList.add('active');
              } else {
                showStatus(`Solenoid ${solenoidNum} turned OFF!`, true);
                button.textContent = 'Turn ON';
                button.classList.remove('active');
              }
            } else {
              showStatus(`Failed to toggle Solenoid ${solenoidNum}: ` + (data.message || ''), false);
            }
          })
          .catch(error => {
            console.error('Error toggling solenoid:', error);
            showStatus(`Error toggling Solenoid ${solenoidNum}.`, false);
          });
        };
      }
      
      document.getElementById('testSolenoid1').addEventListener('click', createTestButtonHandler(1));
      document.getElementById('testSolenoid2').addEventListener('click', createTestButtonHandler(2));
      document.getElementById('testSolenoid3').addEventListener('click', createTestButtonHandler(3));
    });
  </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleGetSettings() {
  DynamicJsonDocument doc(256); // Adjusted size
  doc["solenoid1Time"] = solenoid1Settings.onTime;
  doc["solenoid2Time"] = solenoid2Settings.onTime;
  doc["solenoid3Time"] = solenoid3Settings.onTime;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleUpdateSettings() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    DynamicJsonDocument doc(256); // Adjusted size
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
      log("JSON Deserialization error: " + String(error.c_str()));
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
      return;
    }
    
    bool settingsChanged = false;
    if (doc.containsKey("solenoid1Time")) {
      solenoid1Settings.onTime = doc["solenoid1Time"];
      settingsChanged = true;
    }
    if (doc.containsKey("solenoid2Time")) {
      solenoid2Settings.onTime = doc["solenoid2Time"];
      settingsChanged = true;
    }
    if (doc.containsKey("solenoid3Time")) {
      solenoid3Settings.onTime = doc["solenoid3Time"];
      settingsChanged = true;
    }
    
    if (settingsChanged) {
        saveSettings();
        log("Settings updated via web interface.");
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Settings updated\"}");
    } else {
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"No changes detected\"}");
    }
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"No data provided\"}");
  }
}

void handleActivateSolenoid(int solenoidNum, bool& activeFlag, unsigned long& startTime, unsigned long onTime) {
    String solenoidName = "Solenoid " + String(solenoidNum);
    if (!activeFlag) {
        // Activate the solenoid
        activateSolenoid(solenoidNum, onTime * 60000);
        activeFlag = true;
        startTime = millis();
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"" + solenoidName + " activated\",\"state\":\"on\"}");
    } else {
        // Deactivate the solenoid
        deactivateSolenoid(solenoidNum);
        activeFlag = false;
        server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"" + solenoidName + " deactivated\",\"state\":\"off\"}");
    }
}

void handleActivateSolenoid1() {
    handleActivateSolenoid(1, solenoid1Active, solenoid1StartTime, solenoid1Settings.onTime);
}

void handleActivateSolenoid2() {
    handleActivateSolenoid(2, solenoid2Active, solenoid2StartTime, solenoid2Settings.onTime);
}

void handleActivateSolenoid3() {
    handleActivateSolenoid(3, solenoid3Active, solenoid3StartTime, solenoid3Settings.onTime);
}

void activateSolenoid(int solenoidNum, unsigned long duration) {
  int pinToActivate = -1;
  switch(solenoidNum) {
    case 1: pinToActivate = SOLENOID_1_PIN; break;
    case 2: pinToActivate = SOLENOID_2_PIN; break;
    case 3: pinToActivate = SOLENOID_3_PIN; break;
    default: log("Invalid solenoid number for activation: " + String(solenoidNum)); return;
  }
  
  digitalWrite(pinToActivate, HIGH);
  // The D0 constant is not standard Arduino for ESP8266 pin naming in logs,
  // but it's often used in pinout diagrams. Let's use the actual D-numbers.
  String pinName = "";
  if (pinToActivate == D2) pinName = "D2";
  else if (pinToActivate == D3) pinName = "D3";
  else if (pinToActivate == D4) pinName = "D4";
  else pinName = String(pinToActivate); // Fallback to GPIO number if not D2,D3,D4

  log("Solenoid " + String(solenoidNum) + " (Pin " + pinName + ") turned ON for " + String(duration / 60000.0, 2) + " minutes");
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
  String pinName = "";
  if (pinToDeactivate == D2) pinName = "D2";
  else if (pinToDeactivate == D3) pinName = "D3";
  else if (pinToDeactivate == D4) pinName = "D4";
  else pinName = String(pinToDeactivate);

  log("Solenoid " + String(solenoidNum) + " (Pin " + pinName + ") turned OFF");
}

void loadSettings() {
  uint32_t magicNumber;
  EEPROM.get(EEPROM_MAGIC_NUMBER_ADDR, magicNumber);
  
  if (magicNumber == EEPROM_MAGIC_NUMBER) {
    EEPROM.get(EEPROM_SOLENOID1_TIME_ADDR, solenoid1Settings.onTime);
    EEPROM.get(EEPROM_SOLENOID2_TIME_ADDR, solenoid2Settings.onTime);
    EEPROM.get(EEPROM_SOLENOID3_TIME_ADDR, solenoid3Settings.onTime);
    
    log("Settings loaded from EEPROM:");
    log("  Solenoid 1 ON time: " + String(solenoid1Settings.onTime) + " minutes");
    log("  Solenoid 2 ON time: " + String(solenoid2Settings.onTime) + " minutes");
    log("  Solenoid 3 ON time: " + String(solenoid3Settings.onTime) + " minutes");
  } else {
    log("EEPROM magic number not found. Initializing with default settings.");
    // Default settings are already set, just save them and the magic number
    saveSettings();
  }
}

void saveSettings() {
  EEPROM.put(EEPROM_MAGIC_NUMBER_ADDR, EEPROM_MAGIC_NUMBER);
  EEPROM.put(EEPROM_SOLENOID1_TIME_ADDR, solenoid1Settings.onTime);
  EEPROM.put(EEPROM_SOLENOID2_TIME_ADDR, solenoid2Settings.onTime);
  EEPROM.put(EEPROM_SOLENOID3_TIME_ADDR, solenoid3Settings.onTime);
  
  if (EEPROM.commit()) {
    log("Settings saved to EEPROM.");
  } else {
    log("ERROR: Failed to save settings to EEPROM!");
  }
  log("Current settings:");
  log("  Solenoid 1 ON time: " + String(solenoid1Settings.onTime) + " minutes");
  log("  Solenoid 2 ON time: " + String(solenoid2Settings.onTime) + " minutes");
  log("  Solenoid 3 ON time: " + String(solenoid3Settings.onTime) + " minutes");
}

void log(String message) {
  String timestamp = "[" + String(millis() / 1000.0, 3) + "s] ";
  Serial.println(timestamp + message);
}
