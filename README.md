# PaxxTouch v0.0.1

Custom firmware for **BIGTREETECH K-Touch** and **PandaTouch**, built for the **Snapmaker U1** running [Paxx Extended Firmware](https://github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware).

## Features

| Screen | Capabilities |
|--------|-------------|
| **Home** | Live print status, progress, temps, connection chip |
| **Print** | Job details, pause/resume/cancel, all 4 toolhead temps |
| **Filament** | U1 `print_task_config` slots, tool select (T0–T3), Paxx RFID save |
| **Remote Screen** | Live PNG mirror of U1 panel (480×320), touch forwarding |
| **Timelapse** | Browse `.mp4` timelapses via Moonraker file API |
| **Camera** | Live `/webcam/snapshot.jpg` feed |
| **Files** | Browse gcode files, start prints |
| **Controls** | G28 home, bed mesh, speed/flow sliders (M220/M221) |
| **Settings** | WiFi, printer auth, firmware-config URL, theme, profiles, OTA |
| **WiFi Setup** | Network scan, connect, save credentials |

### Also included

- Moonraker WebSocket with `print_task_config`, `gcode_move`, all extruders
- Moonraker REST: login, file list, print start, Paxx `filament_detect/set`
- Push notifications (print complete, error, paused)
- Multi-printer profiles (up to 5, cycle in Settings)
- Dark/light theme toggle
- Arduino OTA support (`paxxtouch` hostname)
- NVS persistence for WiFi + printer settings

## Build & Flash

### Option A: Web flasher (recommended)

Host on **GitHub Pages** and flash from Chrome/Edge — no PlatformIO needed on the user's PC.

1. Enable Pages: **Settings → Pages → `/docs` folder**
2. Set `github.owner` / `github.repo` in [`docs/flasher/manifest.json`](docs/flasher/manifest.json)
3. Publish a [GitHub Release](docs/flasher/README.md) with `paxxtouch-*.bin` assets
4. Open `https://<user>.github.io/PaxxTouch/flasher/`

See [docs/flasher/README.md](docs/flasher/README.md) for full setup.

### Option B: PlatformIO (developers)

```bash
cd PaxxTouch
pio run -e paxxtouch
pio run -e paxxtouch -t upload --upload-port COM3
```

Package bins for release:

```powershell
.\scripts\package-firmware.ps1 -Version "1.0.0"
```

## First Run

1. Flash firmware to K-Touch/PandaTouch
2. **WiFi Setup** — scan and connect to your LAN
3. **Printer Connection** — enter U1 IP, Moonraker port (7125), optional auth/API key
4. Ensure U1 has **Advanced Mode** enabled and Paxx Extended Firmware

## U1 Requirements

- [Paxx Extended Firmware](https://github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware)
- Moonraker on port **7125**
- For Remote Screen: enable in `http://<ip>/firmware-config/`
- For Camera/Timelapse: paxx12 internal camera enabled

## Project Structure

```
src/
├── moonraker/     WebSocket + REST clients
├── paxx/          Remote screen, camera, PNG/JPEG decode
├── net/           HTTP, WiFi, OTA
├── storage/       NVS preferences + profiles
├── ui/            LVGL screens
└── pt/            BTT display BSP (from PandaTouch_PlatformIO)
```

## Docs

- [Architecture](docs/ARCHITECTURE.md)
- [U1 Integration](docs/U1_INTEGRATION.md)
- [Feature Roadmap](docs/FEATURES.md)

## License

GPL-3.0
