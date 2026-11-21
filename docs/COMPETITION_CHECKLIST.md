# Competition Pit & Safety Checklist (Sumobots)

## Before Leaving the Lab (One-Time Chaos Tests)

Run these 4 tests on your bench before competition day so you trust the system under pressure:

1. **First USB Flash:** Upload `firmware/on-the-air-esp/on-the-air-esp.ino` via USB once with Board = **NodeMCU-32S**.
2. **Normal OTA Flash Test:**
   - Change `FW_VERSION` in `config.h` from `"1.0.0"` to `"1.0.1"`.
   - In Arduino IDE: **Sketch → Export Compiled Binary**.
   - Boot Sumobot into Maintenance Mode → connect laptop/phone to `SUMO-01-OTA` → flash `on-the-air-esp.ino.bin`.
   - Confirm active slot switched (`app0` ↔ `app1`) and after 4 seconds `otaState` becomes `VALID`.
3. **Intentional Brick / Auto-Rollback Test:**
   - Add `while (true) {}` (infinite loop) inside `setup()` right after `initWatchdog()`, export `.bin`, and OTA-flash it.
   - Wait ~10 seconds for the Hardware Watchdog to trigger.
   - Verify the ESP32 **automatically rolls back** to your previous working `v1.0.1` firmware!
4. **Match Mode RF-Off Verification:**
   - Flip the Pit Switch OFF and power-cycle the Sumobot.
   - Scan Wi-Fi on a phone/laptop and confirm `SUMO-01-OTA` does **NOT** appear.

---

## Competition Pit Workflow (30-Second Mid-Match Update)

1. **Remove robot from ring** and turn **Pit Switch ON** (or press `BOOT` during fast-blink window).
2. Confirm onboard LED is doing a **slow heartbeat blink** (motors disarmed, Wi-Fi AP active).
3. Connect laptop or phone to `SUMO-01-OTA`.
4. **Quick tune (2 seconds, zero compile):** Open `http://192.168.4.1`, adjust speeds / thresholds / strategy, click **Save to Flash**.
5. **Full code update (15 seconds):**
   - In Arduino IDE: bump `FW_VERSION`, click **Sketch → Export Compiled Binary**.
   - Upload `on-the-air-esp.ino.bin` via `http://192.168.4.1` OR run:
     ```bash
     python tools/sumo_flash.py flash firmware/on-the-air-esp/build/esp32.esp32.nodemcu-32s/on-the-air-esp.ino.bin --token YOUR_TOKEN
     ```
6. Wait 5 seconds until status shows `VALID`.
7. **Flip Pit Switch OFF**, power-cycle robot, verify solid Match Mode LED, and place on the dohyo!

---

## Competition Security Reminders

- Change `OTA_AP_PASS` and `OTA_AUTH_TOKEN` in `config.h` before competition day.
- Give every robot a unique `ROBOT_ID` and `OTA_AP_SSID` (`SUMO-01-OTA`, `SUMO-02-OTA`, ...).
- For zero boot delay during bouts when using a physical Pit Switch on `GPIO 33`, set `#define BOOT_WINDOW_MS 0` in `config.h`.
