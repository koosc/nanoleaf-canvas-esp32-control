# Nanoleaf Canvas ESP32 Controller & ESPHome Integration

Custom ESP32 firmware and ESPHome component to control **Nanoleaf Canvas (NL29)** modular square light tiles directly over their 1 Mbaud single-wire UART panel bus — replacing the stock controller with complete local control, zero cloud dependencies, native Home Assistant integration, and an embedded interactive 2D placement web UI.

---

## Features

- **Direct Hardware Bus Control**: Replaces the stock Nanoleaf Canvas master controller using an ESP32-C3 via a single GPIO pin running a 1 Mbaud half-duplex UART (open drain, actively driven while transmitting).
- **Cluster Enumeration & 2D Layout Geometry**: Automatic tree discovery and 2D Cartesian coordinate decoding of all attached tiles directly from the bus layout string.
- **Native Home Assistant Integration**: Exposes an ESPHome Light entity with state synchronization, RGB color picker, brightness slider, transitions, and the "Rainbow Wave" / "Static Rainbow" effects. Per-tile colors set from the web UI survive dimming.
- **Embedded Web UI & Placement Visualizer**:
  - Hosted directly on the ESP32 (port 80).
  - Dynamically renders an SVG floorplan of the physical tile arrangement (with a "Rotate view" link, since the layout string does not tell which way the first panel faces).
  - Interactive per-tile color picker and state preview.
  - Global color presets, brightness control, and dynamic effects (e.g., flowing rainbow wave, static rainbow).
- **Canvas Color Protocol**: Global color (`FC 06`) when all tiles match, one addressed frame per tile (`F8 id 06`) otherwise, and global brightness (`FC 04`). The Shapes bulk push (`E0 03`) is not used: Canvas panels ignore it.
- **Robust Bus Health & Hot-Plug Recovery**: 50 ms bulk status polling (`0xC0`), touch reporting (`FC 07`), re-enumeration within 250 ms of a hot-plug marker (`0xCC`) or when the panels stop answering, and a periodic resend of the full state (SET frames have no acknowledgement).
- **Thread-Safe Bus Access**: Web requests are executed on the ESPHome main loop, so they never interleave with polling or Home Assistant commands on the bus.
- **Live Diagnostics API**: Endpoints to inspect enumeration, poll/echo/hot-plug counters, raw poll replies, and to send raw frames for protocol experiments.

---

## Hardware & Wiring

### Requirements
- **Nanoleaf Canvas Tiles (NL29)** with their standard 24V power supply.
- **ESP32-C3** (or standard ESP32).
- **Canvas Linker Board**: Used to connect the ESP32 to an edge connector on any Canvas tile.

### Pinout & Wiring (Looking at Connector)

| ESP32-C3 Pin | Canvas Linker Pad | Function | Notes |
| :--- | :--- | :--- | :--- |
| **GND** | **GND** (Left pad) | Ground Reference | Common ground with power supply |
| *Do not connect* | **+24V** (Middle pad) | Tile Power Supply | **WARNING**: Canvas operates at 24V! Do NOT connect directly to ESP32! |
| **GPIO4** | **DATA** (Right pad) | 1 Mbaud Bidirectional Bus | Open-drain; actively driven while transmitting (`push_pull_tx`). A 1–2.2 kΩ pull-up from DATA to 3.3 V is recommended |

> [!NOTE]
> The internal ~45 kΩ pull-up alone gives rising edges too slow for 1 Mbaud: with pure open-drain,
> panels missed about one frame in five. The firmware therefore drives the line push-pull while it
> sends frames that have no reply, and releases it right after the stop bit. An external 1–2.2 kΩ
> pull-up to 3.3 V further improves the panels' replies and is what the LeafBus reference build uses.

> [!WARNING]
> The Canvas power rail carries **24 V DC** on the **middle pad**. Only connect **DATA (Right pad)** and **GND (Left pad)** between the ESP32 and the tile linker. Power the ESP32 separately via USB (or via a step-down buck converter from the 24V rail).

---

## Quick Start (ESPHome)

### 1. Project Structure
```text
.
├── components/
│   └── nanoleaf_canvas/          # ESPHome custom external component
│       ├── __init__.py
│       ├── canvas_web_page.h     # Embedded responsive HTML/CSS/JS web interface
│       ├── light.py              # ESPHome Light platform registration
│       ├── nanoleaf_canvas.cpp   # Core UART driver, cluster parser & API handler
│       └── nanoleaf_canvas.h
├── nanoleaf-canvas-chris.yaml    # ESPHome configuration file
├── sniffer/                      # Network-enabled 1 Mbaud bus sniffer & panel emulator
│   ├── nanoleaf-sniffer.yaml     # ESPHome sniffer configuration (OTA-enabled)
│   ├── components/nanoleaf_sniffer/ # Sniffer component, web visualizer & REST API
│   ├── scripts/sniff_capture.py  # Live CLI streaming and capture tool
│   └── README.md                 # Sniffer wiring, OTA flashing & usage guide
├── CANVAS_PROTOCOL.md            # Reverse-engineered Canvas protocol documentation
├── PROTOCOL.md                   # LeafBus specification of the Nanoleaf Shapes bus
├── cptmeme-protocol-notes.md     # Cptmeme's additional Shapes bus observations
├── panelbus.py                   # LeafBus MicroPython bus master (Shapes reference)
├── src/main.cpp, platformio.ini  # Early standalone Arduino/PlatformIO prototype (not maintained)
└── README.md
```

### 2. Configuration (`nanoleaf-canvas-chris.yaml`)
```yaml
esphome:
  name: nanoleaf-canvas
  friendly_name: Nanoleaf Canvas

esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: esp-idf

external_components:
  - source:
      type: local
      path: components

wifi:
  ssid: "YourSSID"
  password: "YourPassword"

api:
ota:
  platform: esphome

web_server:
  port: 80

nanoleaf_canvas:
  id: canvas_hub
  data_pin: 4
  # push_pull_tx: true   # drive the line while transmitting (default); see wiring note

light:
  - platform: nanoleaf_canvas
    name: "Nanoleaf Canvas"
    id: canvas_light
    restore_mode: RESTORE_DEFAULT_ON
    default_transition_length: 0s
```

### 3. Compile & Flash
Compile with native ESPHome (e.g. installed in a virtualenv) or Docker:
```bash
esphome compile nanoleaf-canvas-chris.yaml
# or
docker run --rm -v $(pwd):/config esphome/esphome compile /config/nanoleaf-canvas-chris.yaml
```

Flash over serial or OTA:
```bash
esphome upload --device <device-ip> nanoleaf-canvas-chris.yaml
# or
docker run --rm --net=host -v $(pwd):/config ghcr.io/esphome/esphome:latest upload --device <device-ip> /config/nanoleaf-canvas-chris.yaml
```

Check that the compile step succeeded before uploading; `upload` flashes whatever binary was last built.

### 4. Brightness Curve
Brightness is sent through the panels' own brightness command (`FC 04`) after the light's gamma correction (ESPHome default `gamma_correct: 2.8`). The panels respond roughly linearly, so the bottom of the slider is very dim, especially in a lit room. For more usable low levels, set a lower gamma on the light:

```yaml
light:
  - platform: nanoleaf_canvas
    name: "Nanoleaf Canvas"
    gamma_correct: 2.0
```

---

## Embedded Web Interface & REST API

Navigate to `http://<device-ip>/` in any browser to access the interactive controller:

- **Placement Canvas**: Click any individual tile in the SVG diagram to pick and apply a custom color. Use "Rotate view" to match the map to the wall (stored in the browser).
- **All Tiles**: Change master brightness or set a uniform color for all tiles. These go through the ESPHome light, so Home Assistant stays in sync.
- **Effects**: Trigger the Rainbow Wave or Static Rainbow effects (the same effects Home Assistant shows).

### REST Endpoints

| Endpoint | Method | Parameters | Description |
| :--- | :--- | :--- | :--- |
| `/api/tiles` | `GET` | — | Returns JSON array of tiles with Cartesian coordinates (`x`, `y`), rotation, and current RGB color. |
| `/api/set` | `GET` | `id`, `r`, `g`, `b` | Sets the color of a single tile (id = position in the layout, as listed by `/api/tiles`). Turns the light on and stops a running effect if needed. |
| `/api/all` | `GET` | `r`, `g`, `b`, `[bri]` | Sets color and/or brightness (0–255) for all tiles via the light entity. `bri=0` or black turns the light off. |
| `/api/effect` | `GET` | `name=wave\|static_rainbow\|none\|off` | Starts an effect (`none` stops it, `off` turns the light off). The same effects ("Rainbow Wave", "Static Rainbow") are exposed to Home Assistant. |
| `/api/debug` | `GET` | — | Returns enumeration state, poll/echo/hot-plug counters, and raw poll and layout bytes. |
| `/api/raw` | `GET` | `hex`, `[expect]`, `[timeout]` | Debug: sends raw frames (separate several with `-`) and returns the reply. Pauses the firmware's own color frames for 60 s. |
| `/api/buscfg` | `GET` | `[pp]`, `[gap]`, `[nopoll]`, `[autoenum]` | Debug: changes bus settings at runtime. |
| `/api/re-enumerate` | `GET` | — | Forces a full cluster enumeration (`00` + `80` layout walk). |
| `/api/reboot` | `GET` | — | Restarts the ESP32. |

### Troubleshooting

`/api/debug` counters worth watching:

- `poll_short`: poll replies shorter than 2 bytes per panel; almost always lone `CC` hot-plug markers.
- `cc_events` / `enum_count`: hot-plug markers and the re-enumerations they triggered. Several per minute without touching anything suggest a marginal DATA/GND connection; reseat the wires and add the 1–2.2 kΩ pull-up.
- `echo_errors`: frames whose echo did not match what was sent (bus noise or collisions).
- `silent_events`: the panels stopped answering polls entirely and were re-enumerated.

---

## Protocol Documentation

For in-depth documentation on frame structures, packet timings, enumeration grammar, and reverse-engineered command sets, refer to:
- [`CANVAS_PROTOCOL.md`](CANVAS_PROTOCOL.md): Nanoleaf Canvas (NL29) protocol notes and command reference.
- [`PROTOCOL.md`](PROTOCOL.md): LeafBus specification of the Nanoleaf **Shapes** panel bus. Framing, enumeration and the layout grammar apply to Canvas too; color and brightness differ (see `CANVAS_PROTOCOL.md`).

---

## Credits & Prior Work

This project builds upon the foundational reverse-engineering work and documentation shared by the community:

- **Christian Panton**: For the pioneering analysis and deep-dive blog post on reverse engineering the Nanoleaf panel communication bus:
  - [**Nanoleaf Shapes deepdive, part 1: Reverse engineering the protocol**](https://christian.panton.org/posts/nanoleaf-ctrl-pt1/)
- **[LeafBus by MyrikLD](https://github.com/MyrikLD/LeafBus)**: For the comprehensive [`PROTOCOL.md`](PROTOCOL.md) technical specification documenting the serial physical layer, packet framing, and layout tree grammar.
- **[Cptmeme's Nanoleaf-Shapes-controller](https://github.com/Cptmeme/Nanoleaf-Shapes-controller)**: For invaluable observations on layout chunk ordering, hot-plug markers, and bus timing.

---

## License

This project is licensed under the MIT License. Nanoleaf is a trademark of Nanoleaf / NanoGrid Limited. This independent project is not affiliated with or endorsed by Nanoleaf.
