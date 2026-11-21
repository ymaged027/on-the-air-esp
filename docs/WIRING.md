# ESP32 NodeMCU-32S Wiring Guide (Sumobot OTA)

## Default Pin Mapping (`config.h`)

| Function | GPIO | Notes |
| --- | --- | --- |
| Status LED | `GPIO 2` | Built-in blue LED on NodeMCU-32S (active HIGH) |
| Pit / OTA Switch | `GPIO 33` | Wire SPST toggle switch or 2-pin jumper to `GND` (internal pull-up). **Closed = Maintenance Mode (Wi-Fi ON)**, **Open = Match Mode (Wi-Fi OFF)** |
| Built-in `BOOT` Button | `GPIO 0` | Zero-wiring fallback trigger: press *after* power-on during the 2.5s fast-blink boot window |
| Battery Voltage ADC *(optional)* | `GPIO 34` | **ADC1 input-only pin** (safe with Wi-Fi!). Set `BATTERY_MONITOR_ENABLED` to `true` in `config.h` |
| Motor Driver IN1/IN2/IN3/IN4 | `GPIO 25, 26, 27, 14` | Forced `LOW` immediately at boot & during Maintenance Mode |

---

## Critical ESP32 NodeMCU-32S Pin Gotchas

1. **Do NOT use ADC2 pins while Wi-Fi is active** (`GPIO 0, 2, 4, 12, 13, 14, 15, 25, 26, 27`).
   Always wire analog line sensors or battery dividers to **ADC1** (`GPIO 32, 33, 34, 35, 36, 39`).
2. **Watch ESP32 strapping pins (`GPIO 0, 2, 12, 15`):**
   - Holding `GPIO 0` LOW *while powering on / pressing EN* puts the ESP32 into serial ROM flash mode — press `BOOT` *after* power-on if using the boot window, or wire a dedicated switch to `GPIO 33`.
   - Never let an external pull-up force `GPIO 12` HIGH during boot (changes flash voltage to 1.8V and causes boot loops).
3. **Common Ground:** Tie battery GND, motor driver GND, and NodeMCU-32S GND together.
4. **Brownout Prevention During OTA:** Wi-Fi TX draws brief ~300–400 mA spikes. Power the NodeMCU-32S through a clean 5V buck regulator (≥1.5 A) with a 100–470 µF decoupling capacitor near `VIN`–`GND`.

---

## Optional Battery Voltage Divider (for 2S / 3S LiPo)

```
LiPo (+) ──[ 30 kΩ ]──┬──> GPIO 34 (PIN_BATTERY_ADC)
                      │
                   [ 10 kΩ ]
                      │
                    GND
```

- Ratio = `(30k + 10k) / 10k = 4.0` → matches `BATTERY_DIVIDER_RATIO 4.0f` in `config.h`.
- At 12.6 V (full 3S LiPo), `GPIO 34` sees `3.15 V` (safely under 3.3 V max).
- Set `BATTERY_MONITOR_ENABLED true` and choose `BATTERY_MIN_OTA_VOLTS` (e.g., `6.8f` for 2S, `10.2f` for 3S) to prevent flashing on a dying battery.
