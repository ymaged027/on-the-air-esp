# On The Air ESP (`on-the-air-esp`)

**Plug-and-play Over-The-Air (OTA) Wi-Fi flashing & live tuning** for **ESP32 NodeMCU-32S** using **Arduino IDE** — **0 EGP**, **zero external libraries**, and **automatic anti-brick rollback**.

---

## Default Credentials (`config.h`)

| Setting | Value |
| --- | --- |
| **Wi-Fi SSID** | `MTR-OTA` |
| **Wi-Fi Password** | `mtr-ota007` |
| **Arduino IDE Network Port** | `MTR-OTA` (`192.168.4.1`) |
| **Arduino IDE / Web / CLI Password** | `mtr-ota007` |

---

## 30-Second Setup

1. Open `firmware/on-the-air-esp/on-the-air-esp.ino` in **Arduino IDE**.
2. Put your code right at the top inside `myAppSetup()` and `myAppLoop()`.
3. Select **Tools → Board → ESP32 Arduino → NodeMCU-32S** and upload **once via USB**.

---

## 3 Effortless Ways to Flash Wirelessly

First, enter **Maintenance Mode** (close a switch/jumper on `GPIO 33` → `GND` at boot, *or* press the onboard `BOOT` button during the 2.5 s fast-blink window) and connect to **`MTR-OTA`** Wi-Fi (password: **`mtr-ota007`**).

### Option 1 — Direct Inside Arduino IDE
Select **Tools → Port → Network Ports → `MTR-OTA at 192.168.4.1`** → click **Upload (→)** → enter password **`mtr-ota007`**.

### Option 2 — 1-Click Double-Click (`flash.bat` on Windows)
Save your code in Arduino IDE (`Ctrl+S`), then **double-click `flash.bat`**.
It automatically compiles, verifies target device ID & MD5, flashes over Wi-Fi, and confirms the reboot.

### Option 3 — Phone or Browser (Auto-Popup Captive Portal)
Connecting to **`MTR-OTA`** automatically pops open the **Web Console** (`http://192.168.4.1`, or double-click `open-console.bat`):
- **Live NVS parameter tuning** (speeds, thresholds, start delay, operating mode) — no recompile needed.
- **Drag-and-drop `.bin` flashing** with progress bar and auto-reconnect.
- **1-click Rollback** to the previous working firmware slot.

---

## Safety & Anti-Brick Features

- **Wi-Fi & Bluetooth 100% OFF in Normal Run Mode:** Calls `WiFi.mode(WIFI_OFF)` + `btStop()` for zero RF interference and clean real-time timing.
- **Automatic Anti-Brick Rollback:** Overrides Arduino-ESP32's `verifyRollbackLater()` + runs a 10 s Hardware Watchdog + 4 s self-test window. If bad firmware crashes or hangs, the ESP32 **automatically rolls back** to your previous working slot (`app0` ↔ `app1`).
- **Safety Interlocks:** Blocks OTA if outputs are armed, battery is too low, password is wrong, MD5 fails, or a `.bin` targets a different device ID.

---

## Repository Layout

```text
on-the-air-esp/
├── flash.bat                  # Double-click: auto-compile + wireless flash
├── open-console.bat           # Double-click: opens Web Console in browser
├── firmware/
│   └── on-the-air-esp/        # Open on-the-air-esp.ino in Arduino IDE
│       ├── on-the-air-esp.ino # Put your application code at the top
│       ├── config.h           # Credentials (MTR-OTA / mtr-ota007), pins, WDT
│       ├── ota_manager.h/.cpp # Captive Portal, HTTP OTA, ArduinoOTA, rollback
│       ├── app_params.h/.cpp  # Live-tunable NVS parameters
│       └── web_ui.h           # Drag-and-drop Web Console (PROGMEM)
├── tools/
│   └── ota_flash.py           # Zero-dependency CLI (push / status / params / rollback)
├── tests/
│   └── test_ota_flash.py      # Unit tests (python -m unittest discover -s tests -v)
└── docs/
    ├── WIRING.md              # NodeMCU-32S pinout & ESP32 ADC2/strapping gotchas
    └── CHECKLIST.md           # Bench tests & field update SOP
```

---

## Terminal CLI Cheat Sheet (Zero `pip install`)

```bash
python tools/ota_flash.py push                                    # Compile + flash wirelessly
python tools/ota_flash.py status                                  # Check version, slot & params
python tools/ota_flash.py params --set speedMax=245 mode=1        # Live-tune in 1 second
python tools/ota_flash.py rollback                                # Instant wireless rollback
```

*(Password `mtr-ota007` is automatically read from `firmware/on-the-air-esp/config.h` — no need to pass `--token`.)*

---

## Further Reading
- [Wiring & NodeMCU-32S Pinout Gotchas](docs/WIRING.md)
- [Bench Tests & Field Update Checklist](docs/CHECKLIST.md)
