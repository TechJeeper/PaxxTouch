# Snapmaker U1 + Paxx Integration Guide

This document maps Paxx Extended Firmware capabilities to PaxxTouch features.

## Prerequisites on the U1

1. Flash [Paxx Extended Firmware](https://github.com/paxx12-snapmaker-u1/SnapmakerU1-Extended-Firmware)
2. Enable **Advanced Mode**: Settings → Maintenance → Advanced Mode
3. Ensure Moonraker is running (port 7125)
4. Connect K-Touch / PandaTouch to the same LAN

## Moonraker Port

Default: **7125**

Verify from SSH or browser:

```bash
curl http://<printer-ip>:7125/server/info
```

## Filament Assignment

The U1 exposes filament data through Klipper’s `print_task_config` object:

- `filament_slots[]` — material, color, remaining length, loaded state
- `extruder_map_table` — maps print file extruders to physical toolheads

PaxxTouch subscribes via `printer.objects.subscribe` and displays slots on the **Filament** screen. Tapping a slot sends `T<n>` to select that toolhead.

### RFID modes

Paxx firmware supports Snapmaker RFID, OpenRFID, and external modes (configured in firmware-config). PaxxTouch displays whatever `print_task_config` reports; it does not write RFID tags directly in v0.1.

## Firmware Config

Web UI: `http://<printer-ip>/firmware-config/`

Requires Advanced Mode and `firmware_config: true` in `extended2.cfg`.

Useful settings for PaxxTouch users:

| Setting | Why |
|---------|-----|
| Remote Screen | Enables `/screen/*` mirroring |
| Internal camera (paxx12) | Required for timelapses |
| Force Timelapse | Bypass Snapmaker app timelapse gate |
| SSH | Debugging and log access |
| Frontend (Fluidd/Mainsail) | Alternative web UI |

PaxxTouch shows the URL in **Settings → Firmware Config URL** (open on a phone/PC browser).

## Remote Screen Setup (required for PaxxTouch Remote)

1. Open [firmware-config](http://<printer-ip>/firmware-config/) → **Web** → enable **Remote Screen**
2. Or edit manually:

```ini
# /home/lava/printer_data/config/extended/extended2.cfg
[web]
remote_screen: true
```

3. Reboot the printer
4. Test in a browser: `http://<printer-ip>/screen/` — you should see the live panel
5. On K-Touch / PandaTouch:
   - **Gear → WiFi Setup** — same LAN as the printer
   - **Gear → Printer Connection** — U1 IP, port 7125, username/password or API key if auth is on
6. The device connects automatically and shows the mirror fullscreen

If auth is enabled on nginx, PaxxTouch logs in via Moonraker REST and passes the bearer token on snapshot/touch requests.

**Troubleshooting mirror**

- Printer and K-Touch must be on the same reachable network (check IP/subnet)
- Serial log lines starting with `[Remote]` show probe, poll, and touch status
- Restart fb-http on U1: `sudo /etc/init.d/S99fb-http restart`

## Timelapses

Storage path on U1: `/userdata/.tmp_timelapse/`

Index file: `timelapse.json`

Slicer G-code macros:

```
TIMELAPSE_START
TIMELAPSE_TAKE_FRAME
TIMELAPSE_STOP
```

Requires paxx12 internal camera. See [camera docs](https://snapmakeru1-extended-firmware.pages.dev/camera_support/).

## U1-Specific Considerations

| Topic | Detail |
|-------|--------|
| Toolheads | 4 independent (SnapSwap); extruders `extruder`–`extruder3` |
| Progress | May come from `toolhead.progress` or `print_stats` depending on firmware version |
| Custom Moonraker | Snapmaker maintains [u1-moonraker](https://github.com/Snapmaker/u1-moonraker) fork |
| Multi-extruder | Not standard Klipper toolchanger; use `T0`–`T3` |

## Troubleshooting

**PaxxTouch shows Offline**

- Confirm printer IP and port 7125
- Ping the printer from the same network
- Check Moonraker logs: `journalctl -u moonraker -f`

**No filament data**

- Enable Advanced Mode
- Start a print or load filament on the U1 first
- Verify object exists: `curl http://<ip>:7125/printer/objects/query?print_task_config`

**PaxxTouch stuck on Connecting**

- Verify printer IP in **Gear → Printer Connection**
- Enable Remote Screen on U1 and reboot
- Check auth credentials match Moonraker login

## Future API Additions

| Feature | API |
|---------|-----|
| Filament write-back | `POST /printer/filament_detect/set` (Paxx) |
| Camera stream | `http://<ip>/webcam/` WebRTC |
| Timelapse list | MQTT `camera.get_timelapse_instance` |
| Prometheus metrics | `:9101/metrics` |
