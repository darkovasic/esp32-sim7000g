# esp32-sim7000g

ESP-IDF firmware for ESP32 talking to a SIM7000-class modem over UART, structured for growth: **transport → AT client (dedicated task) → SIM7000 bring-up → application**.

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

UART defaults: **UART2**, **115200** baud. Change pins/baud in `idf.py menuconfig` under **Modem UART transport**.

## Configuration (`menuconfig`)

- **Modem UART transport** — port, TX/RX GPIO, baud, RX buffer size, optional **PWRKEY** GPIO pulse on boot.
- **AT client** — task stack, queue depth, default timeout, max line/aggregate lengths, retries.
- **SIM7000 modem** — default APN string, whether to run PDP attach in `sim7000_bringup()`, PDP CID.

## NVS: APN

Namespace `modem`, key `apn` (string). If missing, the firmware uses **SIM7000 default APN** from Kconfig (`CONFIG_SIM7000_DEFAULT_APN`).

Use `modem_config_save_apn()` from [components/sim7000/include/modem_config.h](components/sim7000/include/modem_config.h) in your provisioning code to persist a carrier APN.

## Project layout

| Component | Role |
|-----------|------|
| [components/modem_uart](components/modem_uart) | UART install, drain, read/write, optional PWRKEY |
| [components/at_client](components/at_client) | FreeRTOS task + queue + mutex; sync `AT` commands; URC callback |
| [components/sim7000](components/sim7000) | `sim7000_bringup()` (ID, SIM, registration, optional PDP), NVS helpers |
| [main/main.c](main/main.c) | Init, bring-up, periodic `AT` heartbeat |

## IP / PPP / MQTT (next step)

AT-only bring-up does not put TCP/IP on the ESP32. See [components/sim7000/DATA_PATH.md](components/sim7000/DATA_PATH.md) for PPP and **esp-modem** notes.
