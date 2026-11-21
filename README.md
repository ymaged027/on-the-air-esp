# On The Air ESP (`on-the-air-esp`)

Competition-grade **Over-The-Air (OTA) Wi-Fi firmware flashing** and **live parameter tuning** for **ESP32 NodeMCU-32S** Sumobots using **Arduino IDE** with **zero external libraries** and **zero extra hardware cost (0 EGP)**.

---

## Why This Instead of Basic `ArduinoOTA`?

Naive `ArduinoOTA` examples break easily at competitions (UDP/mDNS blocked by Windows firewalls, no rollback if bad code crashes, Wi-Fi left on during matches violating RF rules). This infrastructure solves every one of those failure modes:

- **True Anti-Brick Automatic Rollback:** Overrides Arduino-ESP32's weak `verifyRollbackLater()` hook and runs a Hardware Watchdog (`esp_task_wdt`) + 4-second self-test window. If a mid-competition push crashes or hangs, the ESP32 bootloader **automatically rolls back** to the last working firmware slot (`app0` ↔ `app1`).
- **Competition RF Compliant:** Wi-Fi and Bluetooth are **100% powered off (`WIFI_OFF` + `btStop`)** in Match Mode. Wireless flashing only runs in explicit **Maintenance Mode** (via a pit switch on `GPIO 33` or the built-in `BOOT` button during boot).
- **Zero-Router Pit Operation:** Robot hosts its own WPA2 Wi-Fi network (`192.168.4.1`) with a sleek built-in **Web Pit Console** (stored in `PROGMEM`—no SPIFFS/LittleFS plugin needed). Works from any laptop or smartphone browser.
- **Target-Mismatch & Safety Interlocks:** Rejects OTA updates if motors are armed, battery is too low, auth token is wrong, MD5 fails, or a `.bin` was compiled for a different Sumobot ID.
- **2-Second Live Tuning (NVS Flash):** Adjust speeds, line thresholds, opponent range, start delay, and strategy over Wi-Fi without recompiling or flashing!

---

## Repository Layout

```text
on-the-air-esp/
├── firmware/
│   └── sumobot_ota/           # Open sumobot_ota.ino directly in Arduino IDE
│       ├── sumobot_ota.ino    # Main sketch (Match Mode vs Maintenance Mode + WDT)
│       ├── config.h           # Robot ID, version, pins, Wi-Fi creds & auth token
│       ├── ota_manager.h/.cpp # HTTP OTA server, safety interlocks, auto-rollback
│       ├── sumo_params.h/.cpp # Persistent NVS live-tunable Sumobot parameters
│       └── web_ui.h           # Built-in Pit Console HTML/CSS/JS (PROGMEM)
├── tools/
│   └── sumo_flash.py          # Zero-dependency Python 3 CLI flasher & tuner
├── tests/
│   └── test_sumo_flash.py     # Unit tests for sumo_flash.py (mock Sumobot server)
└── docs/
    ├── WIRING.md              # NodeMCU-32S pinout & ESP32 ADC2/strapping gotchas
    └── COMPETITION_CHECKLIST.md
```

---

## Quick Start (Arduino IDE)

### 1. First-Time USB Setup
1. Install the official **esp32** board package by Espressif in Arduino IDE Boards Manager.
2. Open `firmware/sumobot_ota/sumobot_ota.ino` in Arduino IDE.
3. Edit `config.h`: set your `ROBOT_ID`, `OTA_AP_SSID`, `OTA_AP_PASS`, and `OTA_AUTH_TOKEN`.
4. Select **Tools → Board → ESP32 Arduino → NodeMCU-32S** (uses the default 4 MB dual-OTA partition table automatically).
5. Upload once over USB.

### 2. Enter Maintenance Mode (Wireless Pit Mode)
Use either method:
- **Zero-wiring method:** Power on the NodeMCU-32S. While the blue LED (`GPIO 2`) fast-blinks for 2.5 seconds, press and hold the built-in **`BOOT` button** for 0.5 s.
- **Recommended competition method:** Close a toggle switch / jumper between **`GPIO 33` and `GND`** before powering on (and set `BOOT_WINDOW_MS 0` in `config.h` for zero match boot delay).

When the blue LED switches to a **slow heartbeat blink**, Maintenance Mode is active and motors are disarmed.

### 3. Flash Wirelessly (Browser or CLI)
1. Connect your laptop or phone to Wi-Fi **`SUMO-01-OTA`**.
2. In Arduino IDE, click **Sketch → Export Compiled Binary** (creates `sumobot_ota.ino.bin` inside the sketch's `build/` folder).
3. **Option A — Web Console:** Open **`http://192.168.4.1`**, enter your auth token, pick `sumobot_ota.ino.bin`, and click **Flash Firmware**.
4. **Option B — Python CLI (zero pip dependencies):**
   ```bash
   # Check robot telemetry & live params
   python tools/sumo_flash.py status --host 192.168.4.1

   # Inspect binary target tag & hashes offline
   python tools/sumo_flash.py inspect path/to/sumobot_ota.ino.bin

   # Flash with automatic target-ID verification, MD5 check, and post-reboot confirmation
   python tools/sumo_flash.py flash path/to/sumobot_ota.ino.bin --token sumo-pit-2026

   # Live-tune parameters in 1 second without reflashing
   python tools/sumo_flash.py params --token sumo-pit-2026 --set attackSpeed=245 strategy=1

   # Instant wireless rollback to previous partition
   python tools/sumo_flash.py rollback --token sumo-pit-2026
   ```

---

## HTTP API Reference

| Endpoint | Method | Auth Header | Purpose |
| --- | --- | --- | --- |
| `/` | `GET` | — | Built-in Web Pit Console |
| `/api/status` | `GET` | — | JSON telemetry (`robotId`, `version`, `partition`, `otaState`, `armed`, `batteryV`, `freeHeap`, `uptimeSec`) |
| `/api/params` | `GET` | — | Current live NVS parameters |
| `/api/params` | `POST` | `X-OTA-Token` | Update NVS parameters (form-encoded) |
| `/api/params/reset` | `POST` | `X-OTA-Token` | Restore default NVS parameters |
| `/api/ota` | `POST` | `X-OTA-Token` | Multipart `.bin` upload (supports `X-Target-Robot` and `X-Firmware-MD5`) |
| `/api/rollback` | `POST` | `X-OTA-Token` | Roll back to previous OTA slot & reboot |
| `/api/reboot` | `POST` | `X-OTA-Token` | Reboot robot |

---

## Running Tests

```bash
python -m unittest discover -s tests -v
```

---

## Further Reading
- [Wiring & NodeMCU-32S Pinout Gotchas](docs/WIRING.md)
- [Pre-Competition Chaos Tests & Pit SOP](docs/COMPETITION_CHECKLIST.md)
