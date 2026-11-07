# Nanoleaf Canvas Network Bus Sniffer

Dedicated network-enabled 1 Mbaud half-duplex UART sniffer and panel emulator for **Nanoleaf Canvas (NL29)**.

Designed to reverse engineer the original stock Nanoleaf Canvas master controller (Control Square) over the local network (Wi-Fi), capturing exact brightness, color (RGBW), layout enumeration, and configuration commands to discover why custom firmware colors and brightness differ from the factory controller.

---

## Key Capabilities

1. **100% Network-Driven (OTA & Web/REST)**:
   - Built on ESPHome with native ESP-IDF framework.
   - Flash over the local network via OTA (no USB cable needed).
   - Embedded interactive Dark-Mode Web UI on port 80 (`http://<ip>/` or `http://<ip>/sniffer`).
   - JSON REST API for remote test scripts and automated packet streaming.

2. **Dual-Mode Operation**:
   - **Mode A: Passive Sniffer (Default)**:
     - GPIO4 is configured strictly as High-Z input (`GPIO_MODE_INPUT`).
     - Transmitter is disconnected in hardware: physically cannot interfere with or corrupt bus traffic.
     - Captures all frames exchanged between the stock controller and Canvas tiles at 1,000,000 baud with microsecond timestamps.
   - **Mode B: Virtual Panel Emulator**:
     - When the original controller is connected directly to the ESP32 (without tiles), the ESP32 answers root detect (`0x00`), layout detect (`0x80`), and bulk polls (`0xC0`).
     - The stock controller operates as if real tiles are attached, allowing full capture of bootup sequences, button presses (brightness +/-, power, scenes), and app commands!

3. **Protocol-Aware Real-Time Decoding**:
   - Frames are automatically parsed into human-readable descriptions:
     - `FC 04 <bri>`: Global brightness (0–255 & percentage).
     - `FC 06 <R> <G> <B> <W> <T>`: Global color, revealing the dedicated white LED (`W`) value and fade transitions!
     - `F8 <id> 06 <R> <G> <B> <W> <T>`: Per-panel addressed color frames.
     - `F8 <id> 04 <bri>`: Per-panel brightness frames.
     - `80` & `40`: Layout tree queries and multi-panel responses.
     - `E0 03 ...`: Shapes bulk push format (to detect if stock Canvas firmware uses it).
     - Unknown or vendor-specific opcodes (`0x09`, `0x83`, calibration frames, etc.).

4. **Smart Poll Filtering**:
   - By default, routine 50 ms idle `0xC0` bulk status polls and idle responses are counted in statistics but filtered out of the packet buffer, preserving color and brightness commands indefinitely.
   - Hot-plug events (`0xCC`) and active touch events are always retained.
   - Filter can be toggled on/off at runtime via the web UI or API (`/api/filter?polls=0`).

---

## Wiring & Setup

### Requirements:
- ESP32-C3 (e.g. `esp32-c3-devkitm-1`).
- Nanoleaf Canvas Linker Board (or 3 flying leads to the linker pads).
- Original Nanoleaf Canvas Controller.

### Pinout (Looking at Connector):
| ESP32-C3 Pin | Canvas Linker Pad | Function | Notes |
| :--- | :--- | :--- | :--- |
| **GND** | **GND** (Left pad) | Common Ground | Must be shared between ESP32 and Canvas |
| *Do not connect* | **+24V** (Middle pad) | 24V DC Rail | **DANGER**: Canvas power rail is 24V! Do NOT connect directly to ESP32! |
| **GPIO4** | **DATA** (Right pad) | 1 Mbaud Half-Duplex Bus | High-Z input in passive mode; Open-Drain in emulator mode |

### Connection Scenarios:

#### Scenario 1: Passive In-Cluster Sniffing (T-Tap)
- Connect the original Nanoleaf Canvas controller to the real Canvas tiles.
- Connect the ESP32 GPIO4 (DATA - Right pad) and GND (Left pad) to any free edge linker connector on the cluster (or T-tap the DATA and GND wires between the controller and tile #0).
- Keep the sniffer in **Passive Mode** (default).

```
+------------------+         +------------------+         +------------------+
|  Stock Canvas    | Linker  |   Canvas Tile    | Linker  |  ESP32-C3        |
|  Control Square  |<=======>|     (Tile 0)     |<=======>|  GPIO4 (DATA)    |
|                  | (DATA)  |                  | (DATA)  |  GND             |
+------------------+         +------------------+         +------------------+
```

#### Scenario 2: Standalone Panel Emulator
- Unplug the Canvas tiles from the stock controller.
- Connect the stock controller's linker DATA (Right pad) and GND (Left pad) directly to the ESP32 (GPIO4 & GND).
- Power the stock controller using its normal 24V supply (GND left, 24V middle).
- Switch the sniffer to **Emulator Mode** via the Web UI or `/api/mode?mode=emulator`.
- The stock controller will boot up, see the virtual panels, and send all its initialization and color commands to the ESP32!

---

## Flashing Over the Network (OTA)

If your ESP32 is already connected to Wi-Fi, you do not need a USB cable to flash it.

### Flash Sniffer Firmware:
```bash
docker run --rm --net=host -v $(pwd):/config ghcr.io/esphome/esphome:latest run /config/sniffer/nanoleaf-sniffer.yaml --device <device-ip-or-hostname>
```

### Revert to Original Controller Firmware:
When you are done capturing packets, flash back to the main controller firmware with:
```bash
docker run --rm --net=host -v $(pwd):/config ghcr.io/esphome/esphome:latest run /config/nanoleaf-canvas-chris.yaml --device <device-ip-or-hostname>
```

---

## Live Monitoring & Testing

### Option 1: Embedded Web UI
Open your browser to:
```
http://<device-ip-or-nanoleaf-sniffer.local>/
```
The interface includes:
- Live streaming table of all frames with color swatches.
- Brightness and color parameter breakdowns.
- One-click copy for raw hex packets.
- Toggle between Passive and Emulator modes.
- Export to JSON for offline analysis.

### Option 2: Command-Line Test Script
Run the automated capture script from your computer:
```bash
python3 sniffer/scripts/sniff_capture.py --ip <device-ip-or-nanoleaf-sniffer.local>
```
To capture and save to a JSONL log file:
```bash
python3 sniffer/scripts/sniff_capture.py --ip <device-ip-or-nanoleaf-sniffer.local> -o capture.jsonl
```

### Option 3: REST API Endpoints
| Endpoint | Method | Parameters | Description |
| :--- | :--- | :--- | :--- |
| `/api/packets` | `GET` | `since=<id>`, `limit=<n>` | Returns JSON array of captured frames with decoded fields. |
| `/api/status` | `GET` | — | Returns capture statistics, packet counts, uptime, and mode. |
| `/api/filter` | `GET` | `polls=0\|1` | Enables (`1`) or disables (`0`) routine C0 poll filtering. |
| `/api/mode` | `GET` | `mode=passive\|emulator` | Switches between passive sniffing and panel emulation. |
| `/api/clear` | `GET` | — | Clears the internal circular packet buffer. |
| `/api/reboot` | `GET` | — | Reboots the ESP32. |
