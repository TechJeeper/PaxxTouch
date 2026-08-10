# PaxxTouch Web Flasher

Browser-based firmware installer for **BTT K-Touch** and **PandaTouch**, powered by [esptool-js](https://github.com/espressif/esptool-js) and the [Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API).

Works when hosted on **GitHub Pages** (HTTPS required).

## Live URL

After enabling GitHub Pages on this repo:

```
https://<your-user>.github.io/PaxxTouch/flasher/
```

## GitHub Pages setup

1. Push this repository to GitHub
2. Edit `docs/flasher/manifest.json` — set `github.owner` and `github.repo`
3. **Settings → Pages → Build and deployment**
   - Source: **Deploy from a branch**
   - Branch: `main` (or `master`)
   - Folder: **`/docs`**
4. Wait ~1 minute for the site to publish

## Creating a flashable release

Build and package firmware binaries:

```powershell
.\scripts\package-firmware.ps1 -Version "1.0.0"
```

Upload the files from `dist/firmware/` as assets on a GitHub Release tagged `v1.0.0`:

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
3. Pick bins from `.pio/build/paxxtouch/`:
   - `bootloader.bin`
   - `partitions.bin`
   - `firmware.bin`
   - `boot_app0.bin` (from PlatformIO Arduino package, optional)

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

**Flash succeeds but screen is blank**

- Re-flash stock BTT firmware first, then PaxxTouch
- Try including `boot_app0.bin` in the release

## Security note

The flasher only writes data you explicitly select (from your GitHub release or local files). Review release assets before publishing — users trust your repo's releases.
