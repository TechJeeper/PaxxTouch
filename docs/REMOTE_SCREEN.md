# Remote Screen — Performance Research

PaxxTouch mirrors the Snapmaker U1 panel using the same backend as `http://<printer-ip>/screen/`. This document explains why the web UI feels faster and how PaxxTouch closes the gap.

## Architecture (U1 side)

```
Browser or PaxxTouch
        │  GET /screen/snapshot  (+ If-None-Match)
        │  POST /screen/touch
        ▼
     nginx :80
        ▼
  fb-http-server.py :8092
        │  reads /dev/fb0
        │  MD5 hash → ETag
        │  PNG encode on change only
        ▼
   U1 touchscreen (480×320)
```

The web page is **not** a video stream. It is JavaScript that polls `snapshot` every **100 ms** and sends touch via `POST`.

## Why the browser feels “real-time”

| Factor | Browser | PaxxTouch Remote (v0.1.0) |
|--------|---------|------------------------------|
| Poll interval | 100 ms | **100 ms** (dedicated poll task) |
| Unchanged frame | `If-None-Match` → **304** (no body) | **304 → skip decode** |
| Decode | Native, async | **Pipelined** — poll never waits for PNG decode |
| After touch | Keep polling; touch is fire-and-forget | **Same** — touch POST does not block poll |
| Display | `<img>` swap | **Reuse RGB565 buffer** — no alloc/free per frame |
| Touch POST | `fetch()` without awaiting | **`postTouchFireAndForget`** — no response drain |

### Architecture (PaxxTouch)

```
poll task (core 0)     decode task (core 1)     UI onTick
     │                        │                      │
     ├─ GET /screen/snapshot ─┤                      │
     │   keep-alive + ETag    │                      │
     ├─ 304 ──────────────────┼──────────────────────┤ (no work)
     └─ 200 → swap PNG buf ──►│ PNG→RGB565 in place  │
                              └─ frameDirty ────────►│ lv_image invalidate
touch task (core 1): POST /screen/touch fire-and-forget (parallel)
```

## What “real-time” cannot mean on ESP32

- **No embedded browser** — cannot load `/screen/` HTML/JS (needs Chromium-class engine + RAM).
- **Not true video** — U1 API is snapshot-based PNG/JPEG, not MJPEG/WebRTC for the panel.
- **Single HTTP worker on U1** — snapshot GET and touch POST queue on the printer; same as browser.

Best achievable: **match the web client protocol** (~10 polls/s, instant 304 when idle, decode only on change).

## Future upgrades (require U1 or firmware changes)

1. **WebSocket client** — Paxx ships `fb-http-ws` on port **8093** (100 ms, one connection, touch as text frames). Not proxied by default nginx `/screen/` routes; would need U1 config or direct port access.
2. **JPEG snapshots** — smaller than PNG when `/screen/snapshot.jpg` is available (already probed first).
3. **Raw RGB565 stream** — would need new U1 endpoint (not available today).

## References

- [Paxx screen-apps](https://github.com/paxx12/screen-apps) — `fb-http`, `fb-http-ws`
- [U1 Remote Screen docs](https://snapmakeru1-extended-firmware.pages.dev/remote_screen/)
- Web client: `setInterval(updateImage, 100)` + `If-None-Match` in `index.html`
