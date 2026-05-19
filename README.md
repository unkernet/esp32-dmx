# ESP32-DMX: Universal Art-Net & DMX Gateway

A high-performance, flexible DMX-over-WiFi gateway based on the ESP32. This project acts as a bridge between Art-Net, WebSockets, physical DMX512 ports, and addressable LEDs, featuring a powerful embedded Lua scripting engine for custom logic.

## Key Features

### Input & Output Sources

*   **Art-Net:** Full support for Art-Net DMX over WiFi. Includes **ArtPoll Reply** support for automatic discovery by lighting consoles and software.
*   **WebSockets:** Real-time DMX monitoring and control via a web-based dashboard. Ideal for remote debugging or browser-based light shows.
*   **Multi-Channel DMX512:** Full-duplex DMX support across multiple channels (hardware dependent). Each port supports:
    *   Configurable **automatic retransmission**: The system can repeat the last received packet at a specified **interval** for a set **duration** if the source signal is lost.
*   **WS2812 Addressable LEDs:** Multi-channel support for driving LED strips (Neopixels) directly from DMX universes.
*   **Ambitful BLE Control:** Control Ambitful brand Bluetooth LED lamps like Ambitful A2 / A2 Pro via DMX. Supports up to 8 independent control groups.

---

## Flexible Routing & Scenarios

The device acts as a versatile DMX matrix, allowing any input source to be routed to any output destination. Some common configuration scenarios include:

*   **DMX to BLE Bridge:** Receive physical DMX512 data from a lighting console and use it to control Ambitful BLE lamps wirelessly.
*   **Web-to-LED Controller:** Send DMX data from a browser-based dashboard via WebSockets to drive WS2812 LED strips directly.
*   **Art-Net Node with Lua Processing:** Receive Art-Net data from the network, modify it in real-time using a **Lua script** (e.g., for **merging channels**, **scaling values**, or **HSV-to-RGB conversion**), and output the result to a physical DMX port.
*   **Stand-alone FX Generator:** Run a Lua script that generates patterns (rainbows, chases, or sensors) and broadcasts them to Art-Net, physical DMX, or WS2812 LEDs without any external controller.

---

## WiFi Configuration & Behavior

The gateway supports both **Station (Client)** and **Access Point (AP)** modes to ensure connectivity in any environment.

*   **Station Mode:** Connects to your existing WiFi network to receive Art-Net or WebSocket data.
*   **Access Point Mode:** Broadcasts its own SSID (default: `ESP-DMX-XXXX`) for direct connection and configuration.
*   **Auto-Fallback Logic:** 
    *   If the device cannot connect to the configured Station network, it automatically enables **AP Mode**. This allows you to access the web dashboard and update network settings even if the primary network is unavailable.
    *   **Power & Signal Efficiency:** To minimize interference and power usage, the **AP will automatically disable itself** if no clients are connected for **60 seconds**.
    *   **Continuous Reconnection:** After the AP times out, the device will continue attempting to reconnect to the configured Station network in the background.
*   **mDNS Discovery:** The device is discoverable via **mDNS** (compatible with macOS, newer Android, and Windows). You can access the dashboard by navigating to `http://esp-dmx.local` or `http://esp-dmx-XXXX.local` (where XXXX is the unique device ID) if multiple devices are on the same network.

---

## Lua Scripting Engine

The gateway features an integrated **Lua 5.5** interpreter, allowing for complex automation and real-time data manipulation.

*   **Execution Modes:** 
    *   **Persistent:** Scripts can be stored on the SPIFFS file system. An `init.lua` file will run **automatically on startup**.
    *   **Volatile:** Scripts can be executed directly via the HTTP API without being saved to the device for rapid testing and development.
*   **Files:** Supports both plain text `.lua` files and precompiled bytecode `.luac`.
*   **Precompiled Scripts:** The ESP32 Lua interpreter is built with the **`LUA_32BITS`** flag enabled. If you wish to upload precompiled bytecode, you **MUST** build your local Lua 5.5 compiler with the same flag (e.g., `make MYCFLAGS="-DLUA_32BITS"`) to ensure the bytecode is compatible.
*   **Modularity:** Full support for `require()` to organize scripts into multiple files.
*   **Routing:** Scripts can read data from any DMX universe and output to any other universe (physical or virtual).
*   **Examples:** Check the `/lua` folder for templates involving **channel merging**, **value scaling**, and **HSV-to-RGB conversion**.

---

## Ambitful BLE Channel Structure

The system can control up to **8 independent lamp groups**. Each group is assigned a block of **8 DMX channels**. The behavior of channels 2-7 within each group depends on the **Mode** set in Channel 1.

| Mode (CH 1) | CH 2 | CH 3 | CH 4 | CH 5 | CH 6 | CH 7 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **0-63: RGBWY** | Red | Green | Blue | White | Yellow | - |
| **64-127: HSL** | Power | Hue | Saturation | - | - | - |
| **128-191: CCT** | Power | Color Temp | Rg (Tint) | - | - | - |
| **192-255: FX** | Power | Scene | Speed | - | - | - |

---

## Building the Project

The project can be built using either the **ESP-IDF** (recommended) or **PlatformIO IDE**.

### Prerequisites
*   **Firmware:** [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html) or [PlatformIO](https://platformio.org/).
*   **Web UI Assets:** [Node.js v22+](https://nodejs.org/) is required to compile the frontend assets.

### Build Instructions
**Firmware & UI:** The Web UI assets are built and upload **automatically** during the firmware compilation and flash stages.

*   **ESP-IDF:** Run `idf.py flash`.
*   **PlatformIO:** Use the Upload button or run `pio run -t upload`.

---

## Customizing the Firmware

The gateway is highly modular. You can easily enable or disable features to fit your specific hardware or memory requirements in two ways:

1.  **Direct Modification:** Modify the `#define` directives in `main/include/hardware_config.h`.
2.  **Hardware Profiles:** Create custom hardware profile files (e.g., `hardware_config.mini.h`) and switch between them using **`menuconfig`**. See **`main/Kconfig.projbuild`** for examples of how to define and switch these profiles.

*   **Memory Efficiency:** Disabling unused modules (e.g., the Lua interpreter or BLE) at build time significantly reduces Flash and RAM usage.
*   **Dynamic UI:** The Web UI automatically adapts to your build. If a module is disabled in the firmware, its corresponding configuration tabs and options will not appear in the dashboard.

---

## Hardware Support & Limitations

While designed to be portable across the ESP32 family, the firmware is primarily tested on the **ESP32-C3**.

### ESP32-C3 Limitations:
*   **Memory:** Typically ~120KB of heap is available for Lua scripting.
*   **UARTs:** Both hardware UARTs are fully available for user configuration, allowing for **two full-duplex DMX channels**.
*   **RMT:** Supports up to 2 TX channels for WS2812 output.

### Patched UART Driver
This project utilizes a **patched version of the ESP-IDF UART driver**. This modification is necessary to ensure reliable DMX512 frame boundary detection. In the standard driver, data bytes and the UART Break signal can arrive in an ambiguous order. The patched driver guarantees that all data preceding a Break is delivered before or within a specialized `UART_DATA_BREAK` event. For technical details on this implementation, see the comments in `main/src/dmx.c`.

---

## Recommended Hardware Configuration

To build a reliable gateway, the following hardware setup is recommended. For a complete example of a build, see **[Reference hardware description](docs/REFERENCE_DEVICE.md)**.

### DMX Interface (RS485)
*   **Duplex Port Mode:** Use **two RS485 transceivers** per channel (one for RX, one for TX) for true simultaneous full-duplex operation.
*   **Simple Mode:** A single transceiver is sufficient if only input or only output is required.
*   **Level Shifting:** Use a voltage divider (**10kΩ / 20kΩ**) on the RX line to shift the 5V signal from the transceiver down to the 3.3V required by the ESP32.
*   **Transceiver Enable (TXEN):** The firmware supports an optional enable pin. If configured, the pin will be pulled **LOW** automatically if both input and output are disabled for that port in the settings, saving power.

### WS2812 LED Output
*   **Protection:** Add a **100Ω resistor** in series with the data pin to protect the ESP32 GPIO from voltage spikes and ensure signal integrity over longer wire runs.

---

## License

This project is licensed under the **ISC License**.
