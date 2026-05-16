# ESP32-DMX Development Guide

## Project Overview
This project is an ESP32-based DMX-over-WiFi gateway. It supports Art-Net, DMX512 (via UART), and custom logic via an embedded Lua interpreter.

## Project Structure
- `main/src/main.c`: Application entry point and initialization.
- `main/src/router.c`: Central DMX routing engine. Dispatches data between sources (WiFi, UART, Lua).
- `main/src/lua_interpreter.c`: Manages the Lua VM (v5.4), script execution, and graceful task termination.
- `main/src/wifi_manager.c`: Handles STA/AP modes, reconnections, and WiFi scanning.
- `main/src/web_server.c`: REST API and static file server (serving gzipped files from SPIFFS).
- `ui/`: Frontend source code (Preact.js with Vite).

### Frontend Structure (`ui/src/`)
-   `app.jsx`: The root Preact component, orchestrating the main layout and tabs.
-   `main.jsx`: The entry point for the Preact application, mounting the `App` component.
-   `api.js`: Centralized module for all API calls to the ESP32 backend (e.g., fetching/saving config, Lua commands, WiFi scan).
-   `config.js`: Defines the binary configuration `struct` schema and contains `decodeStruct`/`encodeStruct` utilities, along with IP helper functions.
-   `signals.js`: Manages global application state using `@preact/signals`.
-   `index.css`: Global CSS styles for the application (Pico.css based).
-   `components/`: Directory for reusable UI components (e.g., `Card`, `Modal`, `WifiTab`, `ScriptingTab`).

## Web API Specification

### Configuration
| Endpoint | Method | Description | Payload/Response |
| :--- | :--- | :--- | :--- |
| `/config` | `GET` | Retrieve current configuration and metadata. | Binary `app_config_t` followed by binary `device_meta_t` metadata |
| `/config` | `PUT` | Update configuration and restart device. | `app_config_t` binary struct |

### Lua Management
| Endpoint | Method | Description | Payload/Response |
| :--- | :--- | :--- | :--- |
| `/lua/list` | `GET` | List scripts, running status, and last error. | JSON: `{"scripts":[], "running":string, "error":string}` |
| `/lua/run/` | `POST` | Stream and execute Lua code immediately. | Body: Lua code |
| `/lua/run/*` | `POST` | Run a stored Lua script by filename. | None |
| `/lua/kill` | `POST` | Gracefully stop the running Lua script. | None |
| `/lua/scripts/*` | `PUT` | Upload a script or delete it. | Body: Lua code (or empty to delete) |
| `/lua/scripts/*` | `GET` | Download raw script content. | Plaintext Lua code |

### System & WiFi
| Endpoint | Method | Description | Payload/Response |
| :--- | :--- | :--- | :--- |
| `/wifi/scan` | `GET` | Scan for visible WiFi networks. | JSON: `[{"ssid":string, "rssi":int, "auth":int, "bssid":string}]` |
| `/status` | `GET` | Get current system status and memory information. | JSON: `{"uptime": long long int, "heap": {"total": unsigned int, "free": unsigned int, "block": unsigned int, "min": unsigned int}}` |
| `/ws` | `GET` | WebSocket for real-time DMX monitoring/control. | Binary: `[u16 universe][u8 data...]` |

## Lua Scripting API

The system uses Lua version 5.5. The environment is **sandboxed**: the `io` library and sensitive `os` functions (e.g., `execute`, `exit`, `getenv`) are restricted for system stability and security.

Lua scripts usually include an **endless loop** to process or generate DMX data in real-time.

**Precompiled Scripts (.luac):**
You may upload precompiled Lua bytecode. However, the ESP32 interpreter is built with the `LUA_32BITS` flag enabled. To create compatible bytecode, you MUST build your local Lua 5.5 compiler with the same flag: `make MYCFLAGS="-DLUA_32BITS"`.

**Functions available in Lua scripts:**

*   **`dmx.send(universe: number, data: binary string, debug: boolean)`**: Transmit DMX `data` (binary string of 512 bytes max) to the specified `universe`. If `debug` is `true`, the data will also be forwarded via Art-Net and WebSockets for monitoring.
*   **`dmx.read(universe: number, timeout: number)`**: Waits up to `timeout` milliseconds for new DMX data for the specified `universe`. Returns a `binary string` with the received data or `nil` if the `timeout` is reached.
*   **`random(min: number, max: number)`**: Generates a true random integer.
    *   `random()`: Returns a full 32-bit integer.
    *   `random(max)`: Returns a random integer between 1 and `max` (inclusive).
    *   `random(min, max)`: Returns a random integer between `min` and `max` (inclusive).
*   **`sleep(ms: number)`**: Pauses script execution for the specified number of milliseconds.
*   **`print(string)`**: Prints the given `string` to the ESP32's system log (visible via serial monitor).

## Technical Guidelines
1. **Streaming Data:** To prevent heap fragmentation, always stream large datasets (file lists, scan results, file contents) using `httpd_resp_send_chunk` instead of allocating large strings.
2. **Lua Safety:** Never use `vTaskDelete` to kill a Lua task. Use the `should_stop` flag and `lua_sethook` to allow the VM to shut down gracefully and free its own memory via `lua_close`.
3. **Gzip Compression:** Static web files are stored as `.gz` files (Gzip). The server adds the `Content-Encoding: gzip` header automatically. Note: Brotli compression is not supported when serving over plain HTTP, which is our current setup.
4. **Memory Allocation (Heap vs Stack):** For buffers (like the 1024-byte streaming chunk), prefer `malloc` over stack allocation (`char buf[1024]`). The HTTP server tasks have limited stack space (typically 4KB); allocating large arrays on the stack can easily trigger a stack overflow.
5. **Configuration:** The `app_config_t` struct is the source of truth for all modules. Changes usually require a restart.
6. **Routing Performance:** All functions called from `route_dmx_data` (`src/router.c`) MUST be as fast as possible and minimize stack usage. These functions are executed within high-priority timing-critical tasks (like the UART RX task or Art-Net processing). Avoid blocking locks, heavy calculations, or large stack allocations. If a module requires complex processing, offload it to a dedicated task (see the WebSocket implementation in `src/web_server.c` as a reference).
