# Solenoid Controller – Wemos D1 mini (ESP8266)
WARNING: This code and the readmy was written from AI. So don't trust it (But it works :)

Firmware that turns a Wemos D1 mini into a **stand-alone irrigation / fluid-control brain**:

* Drives **three solenoid valves** (via MOSFETs) on pins **D2, D3, D4**  
* Two on-board **buttons** (D7 & D6) provide instant manual control and configuration access  
* **Wi-Fi Access-Point** starts automatically on device power-up and provides a **responsive web app** where you can:
  * Set individual valve ON-times (ms)
  * Fire a valve for testing
  * See live status
* A long press on **Button 1** can also be used to start the Wi-Fi AP if it has been turned off
* Wi-Fi automatically turns off after 30 minutes if no devices are connected to save power

Settings are saved in EEPROM, so they survive power-cycles.

---

## 1  Hardware

### 1.1 Bill of Materials

| Qty | Item                        | Notes                                   |
|----:|-----------------------------|-----------------------------------------|
| 1   | Wemos D1 mini (ESP8266)     | 5 V USB powered                         |
| 3   | N-Channel MOSFET (e.g. IRLZ44N, AO3400) | Logic-level gate                    |
| 3   | Fly-back diode (1N5819 / 1N4148) | Across each solenoid coil             |
| 2   | Momentary push-button       | Normally-open to GND                    |
| 3   | Solenoid valves             | Match supply voltage (12 V typical)     |
| —   | 12 V PSU (or valve rating)  | Must handle valve current + ESP8266     |
| —   | Wires, resistors (100 Ω gate, optional pull-downs), PCB or perf-board |

### 1.2 Pin Mapping

| Function             | Wemos pin | GPIO | Direction | Comment                        |
|----------------------|-----------|------|-----------|--------------------------------|
| Solenoid 1           | **D2**    | 4    | OUT       | MOSFET gate                    |
| Solenoid 2           | **D3**    | 0    | OUT       | MOSFET gate                    |
| Solenoid 3           | **D4**    | 2    | OUT       | MOSFET gate                    |
| Button 1 (Mode)      | **D7**    | 13   | IN-PULLUP | Short = S1+S2, Long = Config    |
| Button 2 (Manual 3)  | **D6**    | 12   | IN-PULLUP | Short = S3                     |

### 1.3 Wiring Diagram (ASCII)

```

*Connect all grounds together (ESP, MOSFET sources, PSU, solenoids).*

---

## 2  Firmware & Build

### 2.1 Requirements

* VS Code + [PlatformIO](https://platformio.org/)
* Micro-USB cable
* This repository

### 2.2 PlatformIO Project

`platformio.ini` is pre-configured:

```
[env:d1_mini]
platform      = espressif8266
board         = d1_mini
framework     = arduino
lib_deps      = ESP8266WiFi, ESP8266WebServer, ArduinoJson, ESP8266mDNS
upload_speed  = 921600
monitor_speed = 115200
```

### 2.3 Build & Flash

```bash
# clone
git clone https://github.com/yourname/solenoid-controller.git
cd solenoid-controller

# open in VS Code -> PlatformIO

# or CLI:
pio run            # build
pio run -t upload  # flash (auto-detects port)
pio device monitor # view serial logs
```

---

## 3  Operation

### 3.1 Button Actions

| Button | Press type | Action                                         |
|--------|------------|-----------------------------------------------|
| D7     | Short (<5 s) | Turn **Solenoid 1 & 2 ON** for preset time |
|        | Long (>5 s) | Start **Wi-Fi AP** & Web UI                  |
| D6     | Short       | Turn **Solenoid 3 ON** for preset time        |

### 3.2 Wi-Fi Configuration Mode

1. Wi-Fi AP starts automatically when device powers on
2. If Wi-Fi is off, hold **Button 1** for 5 s → blue LED blinks to turn it on
3. Connect phone/PC to Wi-Fi **`SolenoidController`** (pass `12345678`)  
4. Open **http://192.168.4.1** or **http://solenoid.local**  
5. Use the web app to:
   * Change ON-times (100 ms – 65 s+)
   * Press **Test** to fire a valve instantly
   * Save – values stored in EEPROM
6. Wi-Fi will automatically turn off after 30 minutes if no devices are connected

### 3.3 Serial Debug

Open Serial Monitor @ **115 200 baud**.

Example:

```
[0.123s] Solenoid Controller initialized
[4.812s] Short press on Button 1 (D7). Activating Solenoids 1 & 2.
[9.814s] Solenoid 2 (Pin D3) turned OFF
```

---

## 4  Troubleshooting

| Symptom                              | Possible Cause / Fix                                               |
|--------------------------------------|--------------------------------------------------------------------|
| No AP appears after long press       | Check press >5 s, 5 V supply, watch serial log for errors          |
| Valve never switches off             | ON-time too high, wrong MOSFET wiring, diode missing               |
| ESP resets when valve energises      | Add fly-back diode, PSU sag, add 100 µF cap on 5 V                 |
| Upload fails / timeout               | Correct COM port, press RESET, lower `upload_speed` to 115200      |
| “Solenoid already active” message    | Pressed again before timeout; wait or power-cycle                  |

---

## 5  Customisation

* Change default SSID / password in `src/main.cpp`
* Adjust default ON-times (`solenoid*_Settings`)
* Modify WiFi auto-off time (`WIFI_AUTO_OFF_TIME` constant, default 30 minutes)
* Uncomment `ESP.wdtEnable()` to enable watchdog (test stability)

---

## 6  License

MIT © 2024 Your Name
