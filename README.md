# Solenoid Controller – Wemos D1 mini (ESP8266)

Firmware that turns a Wemos D1 mini into a **stand-alone irrigation / fluid-control brain**:

* Drives **three solenoid valves** (via MOSFETs) on pins **D2, D3, D4**  
* Two on-board **buttons** (D7 & D6) provide instant manual control and configuration access  
* A long press on **Button 1** brings up a **Wi-Fi Access-Point + responsive web app** where you can:
  * Set individual valve **ON-times (minutes)**
  * Configure a **daily HH:MM schedule** for each valve and enable / disable it
  * Fire a valve for testing
  * See live status

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
           +12 V
             │
        .----┴------.               ┌─────────────────────┐
        │ Solenoid  │<───+----------┤ Wemos D1 mini       │
        │   Coil 1  │    │          │                     │
        '-----------'    │          │        D2 ──┐ gate ─┴─ MOSFET Q1
             ^ Fly-back  │          │             │
             | Diode D1  │          │        D3 ──┐ gate ─┴─ MOSFET Q2
GND ◄────────┴───────────┴─…        │             │
                                    │        D4 ──┐ gate ─┴─ MOSFET Q3
Buttons:                             │             │
  BTN1 ▸ D7 ───┐                     │             └───► Solenoid returns to GND
               └───► GND            └─────────────────────┘
  BTN2 ▸ D6 ───┘   (internal pull-ups enabled)
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

### 3.2 Scheduler & Time Sync

Every time a phone / PC opens the web-page the current browser time is sent to the ESP and
stored in its internal RTC (and kept after the browser disconnects).  
In the **Schedule** row for each solenoid you can:

| Field           | Description                                             |
|-----------------|---------------------------------------------------------|
| Schedule (HH:MM)| Daily activation time (24-hour)                         |
| Enable (checkbox)| Turns the daily schedule on or off for that solenoid   |

At the chosen time the valve will switch on for its configured **ON Time (min)**.
The controller ensures the schedule is executed **once per day** (even if Wi-Fi is off).

### 3.2 Wi-Fi Configuration Mode

1. Hold **Button 1** for 5 s → blue LED blinks  
2. Connect phone/PC to Wi-Fi **`SolenoidController`** (pass `12345678`)  
3. Open **http://192.168.4.1** or **http://solenoid.local**  
4. Use the web app to:
   * Change **ON-time (minutes)** for each valve
   * Pick **HH:MM** and tick **Enable** to create a daily schedule
   * Press **Turn ON / OFF** to fire a valve instantly
   * Save – values stored in EEPROM

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
* Uncomment `ESP.wdtEnable()` to enable watchdog (test stability)

---

## 6  License

MIT © 2024 Your Name
