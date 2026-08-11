# PaxxTouch Web Flasher

Browser-based firmware installer for **BTT K-Touch** and **PandaTouch**, powered by [esptool-js](https://github.com/espressif/esptool-js) and the [Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API).

Flashes **PaxxTouch Remote** — fullscreen U1 panel mirror (default release build).

## Live URL

```
https://techjeeper.github.io/PaxxTouch/flasher/
```

## GitHub Pages setup

1. Push this repository to GitHub
2. Edit `docs/flasher/manifest.json` — set `github.owner` and `github.repo`
3. **Settings → Pages → Build and deployment**
   - Source: **Deploy from a branch**
   - Branch: `main`
   - Folder: **`/docs`**
4. Wait ~1 minute for the site to publish

## Creating a flashable release

Build and package the **remote** firmware (default):

```powershell
.\scripts\package-firmware.ps1 -Version "0.1.2"
```

For the legacy full Moonraker UI build:

```powershell
.\scripts\package-firmware.ps1 -Version "0.1.2" -Env paxxtouch
```

Upload the files from `dist/firmware/` as assets on a GitHub Release tagged `v0.1.2`:

| Asset | Flash offset |
|-------|----------------|
| `paxxtouch-bootloader.bin` | `0x0` |
| `paxxtouch-partitions.bin` | `0x8000` |
| `paxxtouch-boot_app0.bin` | `0xE000` (optional but recommended) |
| `paxxtouch-firmware.bin` | `0x10000` |

The web flasher downloads these automatically from the **latest** release.

## Manual flashing (no release yet)

1. Open the flasher page
2. Select **Manual files**
3. Build remote firmware: `pio run -e paxxtouch-remote`
4. Pick bins from `.pio/build/paxxtouch-remote/`:
   - `bootloader.bin`
   - `partitions.bin`
   - `firmware.bin`
   - `boot_app0.bin` (from PlatformIO Arduino package, optional)

## After flashing

1. Power on the K-Touch and open **gear → WiFi Setup**
2. Connect to your LAN
3. **gear → Printer Connection** — U1 IP, Moonraker port 7125, auth if required
4. Enable **Remote Screen** on the U1 ([firmware-config](http://<printer-ip>/firmware-config/))
5. Device boots into the U1 mirror automatically

## Browser support

| Browser | Supported |
|---------|-----------|
| Chrome (desktop) | Yes |
| Edge (desktop) | Yes |
| Firefox | No (no Web Serial) |
| Safari | No |
| Mobile | No |

## Troubleshooting

**Port not listed / connect fails**

- Use a USB-C cable that supports data (not charge-only)
- Install [CH340 driver](https://www.wch.cn/downloads/CH341SER_EXE.html) on Windows if needed
- Hold BOOT, tap RESET, release BOOT to enter download mode

**GitHub release download fails**

- Publish a release with the exact asset names in `manifest.json`
- Or use Manual files mode
- Unauthenticated GitHub API is rate-limited (60 req/hour)

**Flash completes but screen stays blank / device dead**

- Hard refresh (Ctrl+Shift+R) — v6+ preserves PlatformIO flash headers instead of forcing 16MB
- Click **Reload firmware** so bundled bins match the latest build (stale CDN bins can differ from `pio upload`)
- Recover with CLI: `python -m platformio run -e paxxtouch-remote -t upload --upload-port COMx`
- If still blank after web flash, use Manual files mode with bins from `.pio/build/paxxtouch-remote/`

**Flash fails mid-way (`status 201` or seq failed)**

- Hard refresh the flasher page (Ctrl+Shift+R) to get the latest settings
- Keep **Erase entire flash** checked (required after a partial failed flash)
- Use a short **data-capable USB-C cable**; avoid hubs if possible
- K-Touch / PandaTouch use a **CH340 USB chip** — the flasher uses 115200 baud and uncompressed writes for reliability (~2–3 min)
- Hold BOOT, tap RESET, release BOOT, then Connect USB again

- Re-flash stock BTT firmware first, then PaxxTouch
- Try including `boot_app0.bin` in the release

**Remote screen stuck on Connecting**

- Verify U1 IP is correct and reachable from the same WiFi network
- Enable Remote Screen on the U1 and reboot
- Check Moonraker auth (username/password or API key)

## Security note

The flasher only writes data you explicitly select (from your GitHub release or local files). Review release assets before publishing — users trust your repo's releases.
