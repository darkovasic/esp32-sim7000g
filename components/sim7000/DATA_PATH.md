# Cellular data path (PPP / IP)

Bring-up in this project uses **AT commands** only (`sim7000_bringup`). That is enough for SIM check, registration, and activating a PDP context on the module.

To get **TCP/IP on the ESP32** (MQTT, HTTPS, etc.), you typically:

1. **PPP over UART** — The ESP32 runs a PPP client; the modem enters PPP mode (`AT+CGDATA="PPP"` or similar for SIM7000) and frames are carried on the same UART as AT (often after detaching from command mode).

2. **Espressif esp-modem** — Maintained component that wires PPP to `esp_netif`. See the [esp-modem registry page](https://components.espressif.com/components/espressif/esp_modem). Check SIM7000 command compatibility and whether your board shares one UART for AT+PPP or uses a second channel.

3. **Custom stack** — Implement or port PPP + LWIP integration; higher maintenance.

**This project:** `main` uses **esp_modem** for the UART DTE, AT (`esp_modem_at` / `esp_modem_sync`), PPP data mode, and `esp_netif`. `sim7000_bringup()` runs identification / registration / optional PDP over that API. Do not drive the modem UART with a second owner while PPP is up.

## `managed_components` and customizing esp_modem

The Component Manager installs registry dependencies under `managed_components/` (often **gitignored**). **Do not maintain project-specific patches there** — they are easy to lose on `idf.py update-dependencies` and are not committed.

To change dial strings or DCE behavior (e.g. SIM7000 `AT+CGDATA="PPP",<cid>` instead of stock `ATD*99#`):

- **Vendor** esp_modem: copy or submodule it under `components/`, then in `main/idf_component.yml` use `override_path` to that directory; or
- **Fork** espressif/esp_modem and pin your git URL/version in the manifest.

For PPP hangs (~60–90 s then “connection lost”), verify **APN**, **RAT/bands**, and try **`CONFIG_LWIP_PPP_ENABLE_IPV6=n`**; enable **`CONFIG_LWIP_PPP_DEBUG_ON`** temporarily for lwIP PPP logs.

If **`esp_modem_sync` / `esp_modem_set_mode(DATA)` fail right after an **ESP32 reset** but worked before: the modem may still be in **PPP** while the ESP rebooted. Firmware calls **`esp_modem_set_mode(COMMAND)`** once after `esp_modem_new_dev()` to return the line to AT (see `main/main.c`).
