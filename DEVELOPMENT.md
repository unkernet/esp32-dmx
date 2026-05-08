# ESP32-DMX Development Guide

## Project Overview
This project is an ESP32-based DMX-over-WiFi gateway. It supports Art-Net, DMX512 (via UART), and custom logic via an embedded Lua interpreter.

## Project Structure
- `src/main.c`: Application entry point and initialization.
- `src/router.c`: Central DMX routing engine. Dispatches data between sources (WiFi, UART, Lua).
- `src/lua_interpreter.c`: Manages the Lua VM (v5.4), script execution, and graceful task termination.
- `src/wifi_manager.c`: Handles STA/AP modes, reconnections, and WiFi scanning.
- `src/web_server.c`: REST API and static file server (serving gzipped files from SPIFFS).
- `data-src/`: Frontend source code (Vanilla JS).

## Web API Specification

### Configuration
| Endpoint | Method | Description | Payload/Response |
| :--- | :--- | :--- | :--- |
| `/config` | `GET` | Retrieve current binary configuration. | `app_config_t` binary struct |
| `/config` | `PUT` | Update configuration and restart device. | `app_config_t` binary struct |

### Lua Management
| Endpoint | Method | Description | Payload/Response |
| :--- | :--- | :--- | :--- |
| `/lua/list` | `GET` | List scripts, running status, and last error. | JSON: `{"scripts":[], "running":string, "error":string}` |
| `/lua/run` | `POST` | Start a Lua script. Auto-kills running script. | Body: Plaintext filename |
| `/lua/kill` | `POST` | Gracefully stop the running Lua script. | None |
| `/lua/scripts/*` | `PUT` | Upload a script or delete it. | Body: Lua code (or empty to delete) |
| `/lua/scripts/*` | `GET` | Download raw script content. | Plaintext Lua code |

### System & WiFi
| Endpoint | Method | Description | Payload/Response |
| :--- | :--- | :--- | :--- |
| `/wifi/scan` | `GET` | Scan for visible WiFi networks. | JSON: `[{"ssid":string, "rssi":int, "auth":int}]` |
| `/ws` | `GET` | WebSocket for real-time DMX monitoring/control. | Binary: `[u16 universe][u8 data...]` |

## Technical Guidelines
1. **Streaming Data:** To prevent heap fragmentation, always stream large datasets (file lists, scan results, file contents) using `httpd_resp_send_chunk` instead of allocating large strings.
2. **Lua Safety:** Never use `vTaskDelete` to kill a Lua task. Use the `should_stop` flag and `lua_sethook` to allow the VM to shut down gracefully and free its own memory via `lua_close`.
3. **Gzip Compression:** Static web files are stored as `.gz` files (Gzip). The server adds the `Content-Encoding: gzip` header automatically.
4. **Memory Allocation (Heap vs Stack):** For buffers (like the 1024-byte streaming chunk), prefer `malloc` over stack allocation (`char buf[1024]`). The HTTP server tasks have limited stack space (typically 4KB); allocating large arrays on the stack can easily trigger a stack overflow.
5. **Configuration:** The `app_config_t` struct is the source of truth for all modules. Changes usually require a restart.
6. **Routing Performance:** All functions called from `route_dmx_data` (`src/router.c`) MUST be as fast as possible and minimize stack usage. These functions are executed within high-priority timing-critical tasks (like the UART RX task or Art-Net processing). Avoid blocking locks, heavy calculations, or large stack allocations. If a module requires complex processing, offload it to a dedicated task (see the WebSocket implementation in `src/web_server.c` as a reference).
