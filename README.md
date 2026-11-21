# On The Air ESP (`on-the-air-esp`)

**1-click setup & plug-and-play Over-The-Air (OTA) Wi-Fi flashing** for **ESP32 NodeMCU-32S** using **Arduino IDE** — **0 EGP**, **zero external libraries**, and **automatic anti-brick rollback**.

---

## Default Credentials

| Setting | Value |
| --- | --- |
| **Wi-Fi SSID** | `MTR-OTA` |
| **Wi-Fi Password** | `mtr-ota007` |
| **Arduino IDE Network Port** | `MTR-OTA` (`192.168.4.1`) |
| **Arduino IDE / Web / CLI Password** | `mtr-ota007` |

---

## 1-Click First-Time Setup (Zero Manual Configuration!)

1. Plug your **ESP32 NodeMCU-32S** into your PC via USB.
2. **Double-click `setup-usb.bat`**!
   It automatically:
   - Installs **`OnTheAirESP`** into your Arduino IDE Libraries (`#include <OnTheAirESP.h>`).
   - Verifies/installs the **ESP32 NodeMCU-32S** board core if missing.
   - Auto-detects your USB COM port, compiles, and flashes the board once.
3. Unplug the USB cable — you're wireless from now on!

*(Prefer installing inside Arduino IDE instead? Download this repo ZIP and click **Sketch → Include Library → Add .ZIP Library...**, or double-click `install-library.bat`.)*

---

## Add Wireless OTA to *Any* Arduino Sketch in 1 Line

Once installed, open **File → Examples → OnTheAirESP → BasicOTA** (or add `OnTheAirESP.begin();` to any existing ESP32 sketch):

```cpp
#include <OnTheAirESP.h>

void setup() {
  OnTheAirESP.begin();

  // Your normal setup() code here (Wi-Fi & Bluetooth are 100% OFF!)
}

void loop() {
  // Your normal loop() code here (Watchdog & rollback handled automatically!)
}
```

---

## Flash Wirelessly Anytime — Zero Buttons to Press!

Power on your ESP32 and connect Wi-Fi to **`MTR-OTA`** (password: **`mtr-ota007`**). Because **Always-On Mode** runs `MTR-OTA`, wireless flashing, and **OTA Monitor** in the background on **Core 0** while your sketch runs on **Core 1**, you **never have to press `BOOT` or `EN`** between uploads!

1. **Direct Inside Arduino IDE:** Select **Tools → Port → `MTR-OTA at 192.168.4.1`** → click **Upload (→)** → password **`mtr-ota007`**.
2. **1-Click `flash.bat` (Windows):** Double-click **`flash.bat`** to compile & flash the starter sketch — or **drag & drop ANY `.ino` file onto `flash.bat`**!
3. **Web Portal + Browser Code Window (`web-ide.bat` or `http://192.168.4.1`):**
   - Double-click **`web-ide.bat`** to open the full Web Portal at **`http://127.0.0.1:8080`** (and enable 1-click **Compile & Flash** inside `http://192.168.4.1` too!).
   - Paste or edit Arduino C++ code directly in the browser **Code Window** and click **Compile & Flash to ESP32** (automatically keeps `OnTheAirESP.begin()` included so you never lose OTA!).
   - Also includes live **OTA Monitor**, live NVS parameter tuning, drag-and-drop `.bin` flashing, and 1-click Rollback.

---

## Using the Web Portal Parameters in Your Code

Every value you save in the **Live Parameter Tuning** card is stored in ESP32 NVS flash and updates **immediately** in `OnTheAirESP.params()` — no recompile needed:

```cpp
#include <OnTheAirESP.h>

void setup() {
  OnTheAirESP.begin();
}

void loop() {
  auto &p = OnTheAirESP.params();  // Live values from the Web Portal!

  int baseSpeed   = p.speedNormal;      // 0..255
  int boostSpeed  = p.speedMax;         // 0..255
  int turnSpd     = p.turnSpeed;        // 0..255
  int sensorLimit = p.sensorThreshold;  // 0..4095
  int rangeCm     = p.rangeThreshold;   // 5..150
  int startDelay  = p.startDelayMs;     // 0..10000
  int activeMode  = p.mode;             // 0..3

  if (activeMode == 1) {
    // Run Profile 1 using boostSpeed, sensorLimit, etc.
  }
  delay(10);
}
```

*(Need Wi-Fi 100% OFF for an RF-strict competition? Call `OnTheAirESP.setAlwaysOn(false);` before `OnTheAirESP.begin();`, or wire a Safe Recovery switch to `GPIO 33 → GND` to boot into isolated OTA-only mode.)*

---

## Wireless OTA Monitor (Web + Arduino IDE + Terminal)

Watch logs and send commands completely wirelessly — no USB cable required:
- **Inside Arduino IDE:** `setup-usb.bat` and `install-library.bat` automatically register **OTA Monitor** as Arduino IDE's pluggable monitor for `network` ports (`platform.local.txt`). Just select **Tools → Port → `MTR-OTA at 192.168.4.1`** and open **Serial Monitor**!
- **In Your Browser / Phone:** Open `http://192.168.4.1` and use the built-in **OTA Monitor** terminal card (type `help`, `status`, `params`, `reboot`, `rollback`).
- **In Windows Terminal:** Double-click **`ota-monitor.bat`**.
- **In Your Own Sketch:** Print with `OtaMonitor.println("sensor = ...")` (mirrors to USB Serial, Web OTA Monitor, and Arduino IDE Monitor simultaneously). Call `OnTheAirESP.enableDebugMode();` before `begin()` and `OnTheAirESP.handle();` in `loop()` to stream live logs while your sketch runs!

---

## Safety & Anti-Brick Features

- **Wi-Fi & Bluetooth 100% OFF in Normal Run Mode:** `WiFi.mode(WIFI_OFF)` + `btStop()` for zero RF interference.
- **Automatic Anti-Brick Rollback:** Overrides Arduino-ESP32's `verifyRollbackLater()` + runs a 10 s Hardware Watchdog + 4 s self-test window. If bad code crashes or hangs, the ESP32 **automatically rolls back** to your previous working slot (`app0` ↔ `app1`).
- **Safety Interlocks:** Blocks OTA if outputs are armed, battery is too low, password is wrong, MD5 fails, or a `.bin` targets a different device ID.

---

## Repository Layout

```text
on-the-air-esp/
├── setup-usb.bat              # 1-Click first-time setup (library + ESP32 core + USB flash)
├── install-library.bat        # 1-Click install OnTheAirESP into Arduino IDE Libraries
├── flash.bat                  # Double-click or drag-and-drop ANY .ino to flash over Wi-Fi!
├── open-console.bat           # Opens Web Console (http://192.168.4.1)
├── library.properties         # Standard Arduino Library manifest (Add .ZIP Library ready)
├── src/                       # OnTheAirESP Arduino library source
│   ├── OnTheAirESP.h/.cpp     # 3-line plug-and-play facade
│   ├── config.h               # Default credentials (MTR-OTA / mtr-ota007) & pins
│   ├── ota_manager.h/.cpp     # Captive Portal, HTTP OTA, ArduinoOTA, auto-rollback
│   ├── app_params.h/.cpp      # Live-tunable NVS parameters
│   └── web_ui.h               # Drag-and-drop Web Console (PROGMEM)
├── examples/BasicOTA/         # Built-in Arduino IDE example sketch
├── firmware/on-the-air-esp/   # Ready-to-use starter sketch with output disarm hook
├── tools/ota_flash.py         # Zero-dependency CLI (setup / install-lib / push / status / params / rollback)
├── tests/test_ota_flash.py    # Unit tests (python -m unittest discover -s tests -v)
└── docs/
    ├── WIRING.md              # NodeMCU-32S pinout & ESP32 ADC2/strapping gotchas
    └── CHECKLIST.md           # Bench tests & field update SOP
```

---

## Terminal CLI Cheat Sheet (Zero `pip install`)

```bash
python tools/ota_flash.py setup                                 # Full 1-step USB setup
python tools/ota_flash.py push                                  # Compile + flash over Wi-Fi
python tools/ota_flash.py push path/to/any_sketch.ino           # Compile + flash ANY sketch over Wi-Fi
python tools/ota_flash.py status                                # Check version, slot & params
python tools/ota_flash.py params --set speedMax=245 mode=1      # Live-tune in 1 second
python tools/ota_flash.py rollback                              # Instant wireless rollback
```

---

## Further Reading
- [Wiring & NodeMCU-32S Pinout Gotchas](docs/WIRING.md)
- [Bench Tests & Field Update Checklist](docs/CHECKLIST.md)
