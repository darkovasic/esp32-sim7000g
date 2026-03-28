# Cellular data path (PPP / IP)

Bring-up in this project uses **AT commands** only (`sim7000_bringup`). That is enough for SIM check, registration, and activating a PDP context on the module.

To get **TCP/IP on the ESP32** (MQTT, HTTPS, etc.), you typically:

1. **PPP over UART** — The ESP32 runs a PPP client; the modem enters PPP mode (`AT+CGDATA="PPP"` or similar for SIM7000) and frames are carried on the same UART as AT (often after detaching from command mode).

2. **Espressif esp-modem** — Maintained component that wires PPP to `esp_netif`. See the [esp-modem registry page](https://components.espressif.com/components/espressif/esp_modem). Check SIM7000 command compatibility and whether your board shares one UART for AT+PPP or uses a second channel.

3. **Custom stack** — Implement or port PPP + LWIP integration; higher maintenance.

**Recommendation:** When you need IP on the MCU, add `esp_modem` (or IDF’s PPP examples) as a separate phase, keep `at_client` for configuration/diagnostics, and avoid mixing raw AT writes with PPP bytes on the same UART without a clear mode switch.
