# PaxxTouch Architecture

## Overview

PaxxTouch is a **Moonraker client** firmware. The K-Touch / PandaTouch ESP32-S3 acts as a remote display and input device; all print logic remains on the U1 host running Klipper.

This differs from host-side UIs like HelixScreen (runs on the U1 itself) and from BTT’s stock K-Touch firmware (generic Klipper, not U1-aware).

## Software Layers

```
┌─────────────────────────────────────────┐
│  LVGL UI (screens/, Theme)              │
├─────────────────────────────────────────┤
│  PaxxApp orchestrator                   │
├─────────────────────────────────────────┤
│  MoonrakerClient  │  RemoteScreenClient │
├─────────────────────────────────────────┤
│  WiFi / HTTP / WebSocket                │
├─────────────────────────────────────────┤
│  pt_display (BTT PandaTouch BSP)        │
└─────────────────────────────────────────┘
```

## Moonraker Integration

Primary transport: **WebSocket** at `ws://<host>:7125/websocket`

### Subscribed objects

| Object | Purpose |
|--------|---------|
| `print_stats` | Print state, filename, duration |
| `toolhead` | Progress fraction |
| `extruder` | Nozzle temperature |
| `heater_bed` | Bed temperature |
| `print_task_config` | U1 filament slots, extruder map |

### G-code commands

| Action | Command |
|--------|---------|
| Pause | `PAUSE` |
| Resume | `RESUME` |
| Cancel | `CANCEL_PRINT` |
| Select tool | `T0` … `T3` |

## Remote Screen

When enabled in Paxx firmware ([docs](https://snapmakeru1-extended-firmware.pages.dev/remote_screen/)):

| Endpoint | Use |
|----------|-----|
| `GET /screen/snapshot` | PNG of U1’s 480×320 panel |
| `POST /screen/touch?x=&y=` | Inject touch events |

PaxxTouch maps touch on its 480×320 canvas widget to U1 coordinates. PNG decode/render on ESP32 is the next implementation step.

## Timelapse (planned)

U1 timelapses use the Paxx camera MQTT pipeline, not standard `moonraker-timelapse`. Planned approaches:

1. **Moonraker file API** — if timelapse directory is exposed
2. **HTTP proxy** — small endpoint on printer listing `/userdata/.tmp_timelapse/timelapse.json`
3. **MQTT client on ESP32** — `camera.get_timelapse_instance`

## Persistence

Printer connection settings are stored in ESP32 NVS via `Preferences` (`paxxtouch` namespace).

## Build Targets

| Environment | Notes |
|-------------|-------|
| `paxxtouch` | Default; Arduino 2.x via PlatformIO |
| `paxxtouch-arduino-3x` | Pinned Arduino 3.0.7; optional tearing fixes |

For production-quality display timing, consider migrating to [PandaTouch_IDF](https://github.com/bigtreetech/PandaTouch_IDF) (ESP-IDF native).
