# On The Air ESP (`on-the-air-esp`)

**Plug-and-play Over-The-Air (OTA) Wi-Fi flashing & live tuning** for **ESP32 NodeMCU-32S** Sumobots using **Arduino IDE** — **0 EGP**, **zero external libraries**, and **automatic anti-brick rollback**.

---

## 30-Second Setup

1. Open `firmware/on-the-air-esp/on-the-air-esp.ino` in **Arduino IDE**.
2. Write your Sumobot code right at the top inside `mySumoSetup()` and `mySumoLoop()`.
3. *(Optional)* Change Wi-Fi name/password/pins in the `config.h` tab.
4. Select **Tools → Board → ESP32 Arduino → NodeMCU-32S** and upload **once via USB**.

---

## 3 Effortless Ways to Flash Wirelessly (No Cables!)

First, put your Sumobot into **Maintenance Mode** (close the `GPIO 33` Pit Switch at boot, *or* press the onboard `BOOT` button during the 2.5 s fast-blink window) and connect to **`SUMO-01-OTA`** Wi-Fi.

### Option 1 — 1-Click Double-Click (`flash.bat`, Fastest on Windows)
Save your code in Arduino IDE (`Ctrl+S`), then **double-click `flash.bat`**!
It automatically compiles, checks the target robot ID & MD5, flashes over Wi-Fi, and verifies the reboot—no manual `.bin` exporting or typing commands!

### Option 2 — Direct Inside Arduino IDE
Select **Tools → Port → Network Ports → `sumo-01 at 192.168.4.1`** and hit the normal **Upload (→)** button right inside Arduino IDE!

### Option 3 — Phone or Browser (Auto-Popup Captive Portal)
Connecting to **`SUMO-01-OTA`** automatically pops open the **Pit Console** (`http://192.168.4.1`, or double-click `open-console.bat`):
- **2-second live tuning** (speeds, line threshold, opponent range, start delay, strategy) saved to NVS flash—no recompile!
- **Drag-and-drop `.bin` flashing** with progress bar and auto-reconnect.
- **1-click Rollback** to the previous working firmware slot.

---

## Safety Built In for Competitions

- **Wi-Fi 100% OFF in Match Mode:** Calls `WiFi.mode(WIFI_OFF)` + `btStop()` so you never violate Sumobot RF rules or get Wi-Fi interrupt jitter.
- **Automatic Anti-Brick Rollback:** Overrides Arduino-ESP32's `verifyRollbackLater()` + runs a 10 s Hardware Watchdog + 4 s stability check. If bad code crashes or freezes, the ESP32 **automatically boots back into your last working firmware**.
- **Safety Interlocks:** Blocks OTA if motors are armed, battery is too low, token is wrong, MD5 fails, or a `.bin` was built for a different Sumobot ID.

---

## Repository Layout

```text
on-the-air-esp/
├── flash.bat                  # Double-click: auto-compile + wireless flash!
├── open-console.bat           # Double-click: opens Pit Console in browser
├── firmware/
│   └── on-the-air-esp/        # Open on-the-air-esp.ino in Arduino IDE
│       ├── on-the-air-esp.ino # Put your Sumobot code at the top!
│       ├── config.h           # Robot ID, Wi-Fi creds, token, pins
│       ├── ota_manager.h/.cpp # Captive Portal, HTTP OTA, ArduinoOTA, rollback
│       ├── sumo_params.h/.cpp # Live-tunable NVS parameters
│       └── web_ui.h           # Drag-and-drop Pit Console (PROGMEM)
├── tools/
│   └── sumo_flash.py          # Zero-dependency CLI (push / status / params / rollback)
├── tests/
│   └── test_sumo_flash.py     # Unit tests (python -m unittest discover -s tests -v)
└── docs/
    ├── WIRING.md              # NodeMCU-32S pinout & ESP32 ADC2/strapping gotchas
    └── COMPETITION_CHECKLIST.md
```

---

## Terminal CLI Cheat Sheet (Zero `pip install`)

```bash
python tools/sumo_flash.py push                                  # Compile + flash wirelessly
python tools/sumo_flash.py status                                # Check version, slot & params
python tools/sumo_flash.py params --set attackSpeed=245 strategy=1  # Live-tune in 1 second
python tools/sumo_flash.py rollback                              # Instant wireless rollback
```

*(Token is automatically read from `firmware/on-the-air-esp/config.h` — no need to pass `--token`!)*

---

## Further Reading
- [Wiring & NodeMCU-32S Pinout Gotchas](docs/WIRING.md)
- [Pre-Competition Chaos Tests & Pit SOP](docs/COMPETITION_CHECKLIST.md)
