# esp32-sim7000g

ESP-IDF firmware for ESP32 + **SIM7000G** using **esp_modem** (UART DTE, AT, PPP) and **esp_netif** for cellular IP.

## Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) **6.x** (tested with 6.1)
- Target: **ESP32** (`idf.py set-target esp32`)

## Build and flash

```bash
idf.py set-target esp32
idf.py build
idf.py -p PORT flash monitor
```

Firmware version is `PROJECT_VER` in the root [CMakeLists.txt](CMakeLists.txt) and is logged at boot via `esp_app_get_description()`.

## Wiring (default)

| ESP32 | Modem |
|-------|--------|
| GPIO17 (TX) | modem RX |
| GPIO16 (RX) | modem TX |
| GND | GND |

UART for the modem is configured in **`main/main.c`** from **Modem UART transport** in `menuconfig` (port, pins, baud, buffers). Optional **PWRKEY** GPIO lives in the same menu and is driven by [components/modem_uart](components/modem_uart).

## Configuration (`menuconfig`)

- **Modem UART transport** — UART port/pins/baud/buffers for **esp_modem**; optional **PWRKEY**.
- **SIM7000 modem** — default APN, PDP bring-up options, AT timeout/response size, and **wait for network registration** before PPP (helps cold boot when the modem needs tens of seconds to attach).

## NVS: APN

Namespace `modem`, key `apn` (string). If missing, firmware uses **SIM7000 default APN** from Kconfig (`CONFIG_SIM7000_DEFAULT_APN`).

Use `modem_config_save_apn()` from [components/sim7000/include/modem_config.h](components/sim7000/include/modem_config.h) in provisioning code.

## Project layout

| Component | Role |
|-----------|------|
| [components/modem_uart](components/modem_uart) | PWRKEY GPIO only (UART owned by esp_modem) |
| [components/sim7000](components/sim7000) | `sim7000_bringup()` via esp_modem AT API, NVS APN helpers |
| [main/main.c](main/main.c) | Netif/event init, esp_modem DCE, PPP data mode, IP wait, fallbacks |

Managed dependency: **espressif/esp_modem** (see `main/idf_component.yml`). Do not maintain patches under `managed_components/` (see `.cursor/rules/esp-idf-managed-components.mdc`); vendor or fork if you need upstream changes.

## IP / PPP / applications

See [components/sim7000/DATA_PATH.md](components/sim7000/DATA_PATH.md) for PPP notes, IPv6, and reset behavior.
