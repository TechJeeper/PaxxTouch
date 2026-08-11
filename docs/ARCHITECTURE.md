# PaxxTouch Architecture

## Overview

**PaxxTouch Remote** (default build) turns the K-Touch / PandaTouch ESP32-S3 into a **wireless mirror** of the Snapmaker U1 touchscreen. Print logic stays on the U1; the handheld device only displays snapshots and forwards touch events.

A **full Moonraker UI** build (`paxxtouch`) remains in the tree for developers but is not the shipped product.

## Software layers (Remote build)

```
┌─────────────────────────────────────────┐
│  LVGL — RemoteScreenView + setup UI     │
├─────────────────────────────────────────┤
│  PaxxApp — WiFi, profiles, gear menu    │
├─────────────────────────────────────────┤
│  RemoteScreenClient                     │
│    poll task → decode task → UI blit    │
│    touch task → POST /screen/touch      │
├─────────────────────────────────────────┤
│  MoonrakerRest — login for nginx auth   │
├─────────────────────────────────────────┤
│  WiFi / HTTP                            │
├─────────────────────────────────────────┤
│  pt_display (BTT PandaTouch BSP)        │
└─────────────────────────────────────────┘
```

## Boot flow (PAXX_REMOTE_ONLY)

1. Load WiFi + printer profile from NVS
2. Connect WiFi if credentials saved
3. If printer IP configured → **show Remote Screen** fullscreen
4. Otherwise → WiFi Setup or Printer Connection wizard
5. Gear menu (⚙) → WiFi, Printer, About, return to mirror

## Remote Screen protocol

Same as the U1 web client at `http://<printer-ip>/screen/`:

| Endpoint | Use |
|----------|-----|
| `GET /screen/snapshot` or `.jpg` | Panel image + `If-None-Match` → 304 when unchanged |
| `POST /screen/touch?a=down\|move\|up&x=&y=` | Inject touch (fire-and-forget) |

nginx on the U1 proxies port 80 → fb-http on localhost.

See [REMOTE_SCREEN.md](REMOTE_SCREEN.md) for performance details.

## Moonraker (auth only)

The remote build uses **Moonraker REST login** to obtain a bearer token for nginx-authenticated snapshot/touch requests. It does not maintain a WebSocket print subscription.

| Call | Purpose |
|------|---------|
| `POST /access/login` | Username/password → token |
| `X-Api-Key` header | Alternative auth |

## Persistence

WiFi and printer settings stored in ESP32 NVS (`Preferences`, `paxxtouch` namespace). Up to 5 printer profiles supported in data structures; remote UI uses the active profile.

## Build targets

| Environment | Product |
|-------------|---------|
| **`paxxtouch-remote`** | **Default** — fullscreen U1 mirror |
| `paxxtouch` | Legacy full Moonraker UI |
| `paxxtouch-arduino-3x` | Experimental Arduino 3.x pin |

## Full UI build (legacy)

The `paxxtouch` environment includes Moonraker WebSocket, Print/Filament/Camera/Files screens, notifications, and OTA. See [FEATURES.md](FEATURES.md).
