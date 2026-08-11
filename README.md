# PaxxTouch v0.1.6

Custom firmware for **BIGTREETECH K-Touch** and **PandaTouch** that mirrors the **Snapmaker U1** touchscreen over Wi‑Fi.

Built for printers running [Paxx Extended Firmware](https://github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware).

## What it does

PaxxTouch turns your K-Touch / PandaTouch into a **wireless remote panel** for the U1:

- Full-screen mirror of the U1 UI (480×320) via HTTP snapshot polling
- Touch forwarding to the printer (`POST /screen/touch`)
- Same protocol as the browser client at `http://<printer-ip>/screen/`
- Wi‑Fi + printer IP setup stored on device (NVS)

After Wi‑Fi and printer IP are configured, the device **boots straight into the remote mirror** — no home menu, no Moonraker dashboard.

Tap the **gear icon** (top-right) for WiFi Setup, Printer Connection, and About.

## Requirements

| Component | Requirement |
|-----------|-------------|
| Hardware | BTT K-Touch or PandaTouch (ESP32-S3, 16 MB flash) |
| Printer | Snapmaker U1 with [Paxx Extended Firmware](https://github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware) |
| U1 setting | **Remote Screen** enabled in [firmware-config](http://<printer-ip>/firmware-config/) |
| Network | K-Touch and U1 on the same LAN (reachable IP) |
| Auth | Moonraker username/password or API key if nginx auth is enabled |

## Flash firmware

### Option A: Web flasher (recommended)

1. Open **[PaxxTouch Web Flasher](https://techjeeper.github.io/PaxxTouch/flasher/)** in Chrome or Edge
2. Connect USB, click **Connect USB**, then **Flash PaxxTouch**
3. Firmware downloads from the [latest GitHub Release](https://github.com/TechJeeper/PaxxTouch/releases/latest)

See [docs/flasher/README.md](docs/flasher/README.md) for self-hosting on GitHub Pages.

### Option B: PlatformIO (developers)

```bash
cd PaxxTouch
pio run -e paxxtouch-remote -t upload --upload-port COM3
```

Package bins for a GitHub Release:

```powershell
.\scripts\package-firmware.ps1 -Version "0.1.6"
```

Upload the files from `dist/firmware/` as release assets (see flasher docs).

### Full Moonraker UI (legacy build)

The original multi-screen Moonraker client is still available as a separate build target:

```bash
pio run -e paxxtouch -t upload --upload-port COM3
```

This is **not** what the web flasher ships by default.

## First run

1. Flash PaxxTouch Remote firmware
2. **Gear → WiFi Setup** — connect to your LAN
3. **Gear → Printer Connection** — enter U1 IP, Moonraker port (7125), auth if needed
4. On the U1: enable **Remote Screen** in firmware-config and reboot if prompted
5. The mirror connects automatically (`Connecting to U1 remote screen…` → live panel)

## Performance notes

Remote Screen uses PNG/JPEG snapshots (~10 polls/s), not video. Static menus feel near-instant; animated U1 screens are limited by PNG decode time on the ESP32. See [docs/REMOTE_SCREEN.md](docs/REMOTE_SCREEN.md).

## Project structure

```
src/
├── paxx/          Remote screen, PNG/JPEG decode
├── net/           HTTP, WiFi, OTA
├── moonraker/     REST login (auth token for nginx)
├── storage/       NVS WiFi + printer profiles
├── ui/            LVGL remote mirror + setup screens
└── pt/            BTT display BSP (PandaTouch PlatformIO)
```

## Docs

- [Remote Screen architecture](docs/REMOTE_SCREEN.md)
- [U1 integration](docs/U1_INTEGRATION.md)
- [Architecture overview](docs/ARCHITECTURE.md)
- [Feature roadmap](docs/FEATURES.md)

## License

GPL-3.0
