import { ESPLoader, Transport } from "https://cdn.jsdelivr.net/npm/esptool-js@0.6.1/+esm";

const FLASHER_VERSION = 7;

const $ = (id) => document.getElementById(id);

const state = {
  manifest: null,
  release: null,
  firmwareCache: null,
  transport: null,
  esploader: null,
  chip: null,
};

const terminal = {
  clean() { logClear(); },
  writeLine(data) { log(String(data)); },
  write(data) { log(String(data), false); },
};

function log(msg, newline = true) {
  const el = $("log");
  const div = document.createElement("div");
  if (msg.startsWith("Error") || msg.includes("failed")) div.className = "err";
  if (msg.includes("Success") || msg.includes("Complete")) div.className = "ok";
  div.textContent = newline ? msg : msg;
  el.appendChild(div);
  el.scrollTop = el.scrollHeight;
}

function logClear() {
  $("log").innerHTML = "";
}

function setStatus(text, kind = "") {
  const el = $("status");
  el.textContent = text;
  el.className = "status" + (kind ? ` ${kind}` : "");
}

function setProgress(pct, visible) {
  const wrap = $("progressWrap");
  const bar = $("progress");
  wrap.classList.toggle("active", visible);
  bar.value = pct;
  $("progressLabel").textContent = visible ? `${pct.toFixed(0)}%` : "";
}

function supportsWebSerial() {
  return "serial" in navigator;
}

function manifestGithubReady(manifest) {
  const { owner, repo } = manifest.github || {};
  return owner && repo && owner !== "YOUR_GITHUB_USER";
}

function cdnFirmwareUrl(name) {
  const cdn = state.manifest.firmwareCdn;
  if (!cdn?.owner || !cdn?.repo) return null;
  const ref = cdn.ref || "main";
  const path = cdn.path || "docs/flasher/firmware/";
  return `https://cdn.jsdelivr.net/gh/${cdn.owner}/${cdn.repo}@${ref}/${path}${name}`;
}

async function loadManifest() {
  const resp = await fetch(`./manifest.json?v=${FLASHER_VERSION}`, { cache: "no-cache" });
  if (!resp.ok) throw new Error("Could not load manifest.json");
  state.manifest = await resp.json();
  const ver = state.manifest.version ? `v${state.manifest.version}` : "";
  $("versionBadge").textContent = ver ? `${state.manifest.name} ${ver}` : (state.manifest.name || "manifest loaded");
}

async function fetchLatestRelease() {
  if (!manifestGithubReady(state.manifest)) return null;

  const { owner, repo } = state.manifest.github;
  const resp = await fetch(`https://api.github.com/repos/${owner}/${repo}/releases/latest`);
  if (!resp.ok) return null;

  const release = await resp.json();
  state.release = release;
  const tag = release.tag_name.replace(/^v/i, "");
  if (!state.manifest.firmwareLocal?.base && !state.manifest.firmwareCdn) {
    $("versionBadge").textContent = `${state.manifest.name} v${tag}`;
    setStatus(`Release ${release.tag_name} (metadata only)`, "ok");
  }
  return release;
}

async function fetchBinary(url, name, label) {
  log(`${label} ${name}…`);
  const resp = await fetch(url, { cache: "no-cache" });
  if (!resp.ok) throw new Error(`HTTP ${resp.status} for ${url}`);
  return readFirmwareBlob(await resp.arrayBuffer(), name);
}

function readFirmwareBlob(buf, name) {
  if (buf.byteLength < 256) {
    throw new Error(`${name} looks too small (${buf.byteLength} bytes) — wrong file?`);
  }
  log(`  ${name}: ${buf.byteLength} bytes`);
  return new Uint8Array(buf);
}

/** Pass Uint8Array to esptool-js 0.6.x (native binary support). */
function toFlashFileArray(entries) {
  return entries.map(({ data, address }) => ({
    data: data instanceof Uint8Array ? data : new Uint8Array(data),
    address,
  }));
}

async function downloadFirmwareBin(name) {
  const localBase = state.manifest.firmwareLocal?.base;
  if (localBase) {
    try {
      return await fetchBinary(localBase + name, name, "Loading");
    } catch (err) {
      log(`Local ${name} unavailable (${err.message})`);
    }
  }

  const cdnUrl = cdnFirmwareUrl(name);
  if (cdnUrl) {
    return await fetchBinary(cdnUrl, name, "Downloading");
  }

  throw new Error(
    `Could not load ${name}. Use Manual files mode and pick bins from a release zip.`
  );
}

async function prefetchFirmware() {
  const manifest = state.manifest;
  const cache = new Map();

  for (const spec of manifest.files) {
    try {
      const data = await downloadFirmwareBin(spec.asset);
      cache.set(spec.asset, { data, address: spec.offset });
    } catch (err) {
      if (spec.optional) {
        log(`Skipping optional ${spec.asset}: ${err.message}`);
        continue;
      }
      throw err;
    }
  }

  if (cache.size === 0) throw new Error("No firmware files loaded.");
  state.firmwareCache = cache;
  setStatus(`Firmware v${manifest.version} ready — connect USB to flash.`, "ok");
  log(`Firmware v${manifest.version} cached (${cache.size} files).`);
}

async function resolveFirmwareFiles() {
  if ($("sourceRelease").checked) {
    if (!state.firmwareCache?.size) {
      await prefetchFirmware();
    }
    const files = [];
    for (const spec of state.manifest.files) {
      const entry = state.firmwareCache.get(spec.asset);
      if (entry) files.push(entry);
      else if (!spec.optional) throw new Error(`Missing firmware file: ${spec.asset}`);
    }
    return files;
  }

  if ($("sourceManual").checked) {
    const files = [];
    for (const [inputId, spec] of manualInputs()) {
      const input = $(inputId);
      if (!input.files?.length) {
        if (spec.optional) continue;
        throw new Error(`Select file: ${spec.label}`);
      }
      const data = new Uint8Array(await input.files[0].arrayBuffer());
      if (data.byteLength < 1024 && spec.label.includes("firmware")) {
        throw new Error(`${spec.label} looks too small — pick the correct .bin`);
      }
      files.push({ data, address: spec.offset });
      log(`Loaded ${spec.label}: ${data.byteLength} bytes @ 0x${spec.offset.toString(16)}`);
    }
    return files;
  }

  throw new Error("Select firmware source (bundled firmware or manual files).");
}

function manualInputs() {
  return [
    ["fileBootloader", { label: "bootloader.bin", offset: 0, optional: false }],
    ["filePartitions", { label: "partitions.bin", offset: 0x8000, optional: false }],
    ["fileBootApp0", { label: "boot_app0.bin", offset: 0xe000, optional: true }],
    ["fileFirmware", { label: "firmware.bin", offset: 0x10000, optional: false }],
  ];
}

async function connectDevice() {
  if (!supportsWebSerial()) {
    throw new Error("Web Serial not supported. Use Chrome or Edge over HTTPS.");
  }

  await disconnectDevice();

  const filters = state.manifest.usbFilters || [];
  const port = await navigator.serial.requestPort({ filters });

  state.transport = new Transport(port, true);
  state.esploader = new ESPLoader({
    transport: state.transport,
    baudrate: state.manifest.baudRate || 460800,
    terminal,
    debugLogging: false,
  });

  state.chip = await state.esploader.main();
  setStatus(`Connected: ${state.chip}`, "ok");
  log(`Chip detected: ${state.chip}`);

  $("btnConnect").disabled = true;
  $("btnFlash").disabled = false;
  $("btnDisconnect").disabled = false;
}

async function disconnectDevice() {
  if (state.transport) {
    try { await state.transport.disconnect(); } catch (_) { /* ignore */ }
  }
  state.transport = null;
  state.esploader = null;
  state.chip = null;
  $("btnConnect").disabled = false;
  $("btnFlash").disabled = true;
  $("btnDisconnect").disabled = true;
  setStatus("Disconnected");
}

async function flashDevice() {
  if (!state.esploader) throw new Error("Connect to the device first.");

  $("btnFlash").disabled = true;
  $("btnConnect").disabled = true;
  setProgress(0, true);
  setStatus("Preparing firmware…");

  try {
    const fileArray = toFlashFileArray(await resolveFirmwareFiles());
    if (!fileArray.length) throw new Error("No firmware files to flash.");

    const flash = state.manifest.flash;
    const eraseAll = $("optEraseAll")?.checked ?? flash.eraseAll !== false;
    const compress = flash.compress === true;
    if (eraseAll) log("Full chip erase enabled (recommended after a failed flash).");
    if (!compress) log("Uncompressed flash (more reliable over CH340 USB).");
    // Do not patch bootloader flash headers — bins are built by PlatformIO with correct
    // qio_opi settings. Overriding mode/size (e.g. 16MB) breaks boot on K-Touch / PandaTouch.
    log("Preserving flash headers embedded in bootloader (PlatformIO build).");
    setStatus("Flashing… do not unplug the USB cable.");

    await state.esploader.writeFlash({
      fileArray,
      eraseAll,
      compress,
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      reportProgress: (_fileIndex, written, total) => {
        const pct = total ? (written / total) * 100 : 0;
        setProgress(pct, true);
      },
    });

    await state.esploader.after("hard_reset");
    setProgress(100, true);
    setStatus("Flash complete! PaxxTouch is rebooting.", "ok");
    log("Success — firmware written. Device reset.");
    await disconnectDevice();
  } finally {
    $("btnFlash").disabled = !state.esploader;
    $("btnConnect").disabled = !!state.esploader;
  }
}

function toggleSourcePanels() {
  $("panelRelease").classList.toggle("hidden", !$("sourceRelease").checked);
  $("panelManual").classList.toggle("hidden", !$("sourceManual").checked);
}

async function init() {
  if (!supportsWebSerial()) {
    setStatus("Web Serial requires Chrome or Edge (desktop) on HTTPS or localhost.", "err");
    $("btnConnect").disabled = true;
    $("btnFlash").disabled = true;
  } else {
    setStatus("Loading firmware…");
  }

  try {
    await loadManifest();
    await fetchLatestRelease().catch(() => null);

    if ($("sourceRelease").checked) {
      await prefetchFirmware();
    } else {
      setStatus("Manual files mode — select .bin files below.", "warn");
    }
  } catch (err) {
    setStatus(err.message, "err");
    log(String(err.message));
    $("sourceManual").checked = true;
    toggleSourcePanels();
  }

  $("sourceRelease").addEventListener("change", async () => {
    toggleSourcePanels();
    if ($("sourceRelease").checked) {
      state.firmwareCache = null;
      try {
        setStatus("Loading firmware…");
        await prefetchFirmware();
      } catch (err) {
        setStatus(err.message, "err");
        log(String(err.message));
      }
    }
  });
  $("sourceManual").addEventListener("change", toggleSourcePanels);
  toggleSourcePanels();

  $("btnConnect").addEventListener("click", () => connectDevice().catch(async (e) => {
    setStatus(e.message, "err");
    log(`Error: ${e.message}`);
    await disconnectDevice();
  }));

  $("btnDisconnect").addEventListener("click", () => disconnectDevice().catch(() => {}));

  $("btnFlash").addEventListener("click", () => flashDevice().catch((e) => {
    setStatus(e.message, "err");
    log(String(e.message));
    setProgress(0, false);
    $("btnFlash").disabled = !state.esploader;
    $("btnConnect").disabled = !!state.esploader;
  }));

  $("btnFetchRelease").addEventListener("click", async () => {
    state.firmwareCache = null;
    try {
      setStatus("Reloading firmware…");
      await prefetchFirmware();
    } catch (err) {
      setStatus(err.message, "err");
      log(String(err.message));
    }
  });
}

init();
