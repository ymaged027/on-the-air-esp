# Field Testing & Safety Checklist (`on-the-air-esp`)

## Bench Verification Tests (Run Once Before Deployment)

1. **First USB Flash:** Upload `firmware/on-the-air-esp/on-the-air-esp.ino` via USB once with Board = **NodeMCU-32S**.
2. **Normal Wireless OTA Test:**
   - Change `FW_VERSION` in `config.h` from `"1.0.0"` to `"1.0.1"`.
   - Enter Maintenance Mode (hold `BOOT` during 2.5 s fast-blink window or close `GPIO 33` switch).
   - Connect to Wi-Fi **`MTR-OTA`** (password: **`mtr-ota007`**).
   - Flash via **Arduino IDE** (`Tools → Port → MTR-OTA`, password `mtr-ota007`), **double-click `flash.bat`**, or drag-and-drop at `http://192.168.4.1`.
   - Confirm active slot switched (`app0` ↔ `app1`) and after 4 seconds `otaState` becomes `VALID`.
3. **Intentional Crash / Auto-Rollback Test:**
   - Add `while (true) {}` inside `setup()` right after `initWatchdog()`, flash wirelessly, and wait ~10 seconds for the Hardware Watchdog to trigger.
   - Verify the ESP32 **automatically rolls back** to your previous working `v1.0.1` firmware.
4. **Normal Run Mode RF-Off Verification:**
   - Open the `GPIO 33` switch (or power-cycle without pressing `BOOT`).
   - Scan Wi-Fi on a phone/laptop and confirm **`MTR-OTA`** does **NOT** appear.

---

## Quick Field Update Workflow

1. Put the device into **Maintenance Mode** (`GPIO 33` switch ON or press `BOOT` during fast-blink window).
2. Confirm onboard LED is doing a **slow heartbeat blink** (outputs disarmed, Wi-Fi **`MTR-OTA`** active).
3. Connect laptop or phone to **`MTR-OTA`** (password: **`mtr-ota007`**).
4. **Quick parameter tweak (2 seconds):** Use the auto-popup Web Console (`http://192.168.4.1`) and click **Save to Flash**.
5. **Full firmware update:**
   - **Arduino IDE:** Select **Tools → Port → `MTR-OTA`** → click **Upload** (password: `mtr-ota007`).
   - **1-Click Windows:** Save your code and double-click **`flash.bat`**.
6. Wait ~5 seconds until status shows `VALID`, then power-cycle into Normal Run Mode (solid LED, Wi-Fi OFF).
