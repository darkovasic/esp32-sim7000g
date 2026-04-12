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

- **Application** — **Enable SIM7000 cellular** (modem UART + PPP). **Default is off** for Wi‑Fi-only lab or boards without a modem; turn on for cellular field use. Requires **Component config → LWIP → Enable PPP support** when enabled. Optional **Deep sleep after boot when cellular is off**; **RTC timer wake** interval in seconds (default **60** when deep sleep is on — set to **0** for sleep until EN/reset only).
- **Modem UART transport** — UART port/pins/baud/buffers for **esp_modem**; optional **PWRKEY** (used when cellular is enabled).
- **SIM7000 modem** — default APN, PDP bring-up options, AT timeout/response size, and **wait for network registration** before PPP (helps cold boot when the modem needs tens of seconds to attach).
- **Readings API (HTTP upload)** — one-shot `POST /data` after IP: **PPP** when cellular is on, or **Wi‑Fi STA** when cellular is off (same AP as lab SNTP if that is enabled, otherwise **Readings Wi‑Fi SSID/password** in menuconfig).
- **Lab: Wi-Fi time sync (SNTP)** — optional: connect STA, run NTP, set `TZ`, then disconnect. With cellular off and readings on, the upload runs on that STA before disconnect. With cellular on, runs before PPP. Do not commit `sdkconfig` with real credentials.

**Partition table:** `sdkconfig.defaults` selects **Single factory app, large** (~1500 KiB app) so the image fits after **esp_wifi** is linked from `main` (required for reliable CMake + optional lab SNTP). Use a **2 MiB** (or larger) flash; adjust in menuconfig if your module differs.

**TLS / time:** Correct wall clock helps certificate validation. Lab SNTP (or NTP over PPP) helps HTTPS; Wi‑Fi-only readings without SNTP may still work with the default cert bundle depending on the server.

## NVS: APN

Namespace `modem`, key `apn` (string). If missing, firmware uses **SIM7000 default APN** from Kconfig (`CONFIG_SIM7000_DEFAULT_APN`).

Use `modem_config_save_apn()` from [components/sim7000/include/modem_config.h](components/sim7000/include/modem_config.h) in provisioning code.

## NVS: Readings API

Namespace `readings` (string keys):

| Key | Required | Purpose |
|-----|----------|---------|
| `api_key` | yes for upload | Same secret as server `API_KEY`; sent as `Authorization: Bearer …` |
| `api_base` | no | Overrides Kconfig base URL (e.g. `https://api.bedrocklabs.online`) |
| `device_id` | no | Overrides Kconfig `device_id` in JSON body |

Use `readings_config_save_*()` from [main/readings_config.h](main/readings_config.h) in provisioning code, or generate an NVS image with the partition tool.

**One-shot from menuconfig:** enable **Readings API → One-shot: write API key…**, set **API key to write to NVS**, `idf.py flash` once, confirm the log, then turn provisioning **off** and clear the key string (avoid committing `sdkconfig` with a live key).

## Project layout

| Component | Role |
|-----------|------|
| [components/modem_uart](components/modem_uart) | PWRKEY GPIO only (UART owned by esp_modem) |
| [components/sim7000](components/sim7000) | `sim7000_bringup()` via esp_modem AT API, NVS APN helpers |
| [main/main.c](main/main.c) | Netif/event init, optional Wi‑Fi STA (SNTP and/or readings); if `APP_ENABLE_CELLULAR`, modem + PPP + optional HTTPS POST |
| [main/wifi_lab_sntp.c](main/wifi_lab_sntp.c) | STA session: lab SNTP + optional readings upload when cellular off; or readings-only STA when `CONFIG_READINGS_UPLOAD_ENABLE` without lab SNTP |
| [main/readings_config.c](main/readings_config.c), [main/readings_upload.c](main/readings_upload.c) | NVS + `esp_http_client` upload to `/data` |

Managed dependency: **espressif/esp_modem** (see `main/idf_component.yml`). Do not maintain patches under `managed_components/` (see `.cursor/rules/esp-idf-managed-components.mdc`); vendor or fork if you need upstream changes.

## IP / PPP / applications

See [components/sim7000/DATA_PATH.md](components/sim7000/DATA_PATH.md) for PPP notes, IPv6, and reset behavior.
