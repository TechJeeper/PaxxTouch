import { ESPLoader, Transport } from "https://cdn.jsdelivr.net/npm/esptool-js@0.5.4/+esm";

const $ = (id) => document.getElementById(id);

const state = {
  manifest: null,
  release: null,
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

async function loadManifest() {
  const resp = await fetch("./manifest.json", { cache: "no-cache" });
  if (!resp.ok) throw new Error("Could not load manifest.json");
  state.manifest = await resp.json();
  const ver = state.manifest.version ? `v${state.manifest.version}` : "";
  $("versionBadge").textContent = ver ? `${state.manifest.name} ${ver}` : (state.manifest.name || "manifest loaded");
}

async function fetchLatestRelease() {
  if (!manifestGithubReady(state.manifest)) {
    setStatus("GitHub release not configured — use Manual files or edit manifest.json.", "warn");
    $("sourceManual").checked = true;
    toggleSourcePanels();
    return null;
  }

  const { owner, repo } = state.manifest.github;
  setStatus(`Fetching latest release from ${owner}/${repo}…`);
  const resp = await fetch(`https://api.github.com/repos/${owner}/${repo}/releases/latest`);
  if (!resp.ok) {
    if (resp.status === 404) {
      throw new Error(`No GitHub release found for ${owner}/${repo}. Use Manual files or publish a release.`);
    }
    throw new Error(`GitHub API error ${resp.status}. Try Manual files mode.`);
  }
  const release = await resp.json();
  state.release = release;
  const tag = release.tag_name.replace(/^v/i, "");
  $("versionBadge").textContent = `${state.manifest.name} v${tag}`;
  setStatus(`Release ${release.tag_name} ready (${release.assets.length} assets)`, "ok");
  return release;
}

async function downloadAsset(asset, name) {
  log(`Downloading ${name}…`);
  // GitHub API asset URL (browser_download_url is blocked by CORS from GitHub Pages)
  const resp = await fetch(asset.url, {
    headers: { Accept: "application/octet-stream" },
  });
  if (!resp.ok) throw new Error(`Failed to download ${name}: HTTP ${resp.status}`);
  return readFirmwareBlob(await resp.arrayBuffer(), name);
}

async function downloadLocal(name) {
  const base = state.manifest.firmwareLocal?.base || "./firmware/";
  const url = base + name;
  log(`Loading ${name}…`);
  const resp = await fetch(url, { cache: "no-cache" });
  if (!resp.ok) throw new Error(`Failed to load ${name}: HTTP ${resp.status}`);
  return readFirmwareBlob(await resp.arrayBuffer(), name);
}

function readFirmwareBlob(buf, name) {
  if (buf.byteLength < 256) {
    throw new Error(`${name} looks too small (${buf.byteLength} bytes) — wrong file?`);
  }
  log(`  ${name}: ${buf.byteLength} bytes`);
  return new Uint8Array(buf);
}

async function loadFirmwareSpec(spec) {
  const localBase = state.manifest.firmwareLocal?.base;
  if (localBase) {
    try {
      const data = await downloadLocal(spec.asset);
      return { data, address: spec.offset };
    } catch (err) {
      log(`Local ${spec.asset} unavailable (${err.message}), trying GitHub release…`);
    }
  }

  if (!state.release) {
    throw new Error("No GitHub release loaded. Click Refresh or switch to Manual files.");
  }
  const asset = state.release.assets.find((a) => a.name === spec.asset);
  if (!asset) {
    if (spec.optional) {
      log(`Skipping optional ${spec.asset}`);
      return null;
    }
    throw new Error(`Release missing asset: ${spec.asset}`);
  }
  const data = await downloadAsset(asset, spec.asset);
  return { data, address: spec.offset };
}

async function resolveFirmwareFiles() {
  const files = [];
  const manifest = state.manifest;

  if ($("sourceRelease").checked) {
    for (const spec of manifest.files) {
      const entry = await loadFirmwareSpec(spec);
      if (entry) files.push(entry);
    }
    return files;
  }

  if ($("sourceManual").checked) {
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

  throw new Error("Select firmware source (GitHub release or manual files).");
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
    const fileArray = await resolveFirmwareFiles();
    if (!fileArray.length) throw new Error("No firmware files to flash.");

    const flash = state.manifest.flash;
    setStatus("Flashing… do not unplug the USB cable.");

    await state.esploader.writeFlash({
      fileArray,
      eraseAll: false,
      compress: true,
      flashMode: flash.mode,
      flashFreq: flash.freq,
      flashSize: flash.size,
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
    setStatus("Ready — connect your K-Touch via USB-C.");
  }

  try {
    await loadManifest();
    if (state.manifest.firmwareLocal?.base) {
      setStatus(`Firmware v${state.manifest.version} ready (bundled) — connect USB to flash.`, "ok");
    }
    if (manifestGithubReady(state.manifest)) {
      await fetchLatestRelease();
      if (state.manifest.firmwareLocal?.base && state.release) {
        setStatus(`Firmware v${state.manifest.version} ready — connect USB to flash.`, "ok");
      }
    } else {
      $("sourceManual").checked = true;
      toggleSourcePanels();
      setStatus("Manual files mode — build with PlatformIO or use packaged bins.", "warn");
    }
  } catch (err) {
    setStatus(err.message, "warn");
    log(String(err.message));
    $("sourceManual").checked = true;
    toggleSourcePanels();
  }

  $("sourceRelease").addEventListener("change", toggleSourcePanels);
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
    log(`Error: ${e.message}`);
    setProgress(0, false);
    $("btnFlash").disabled = !state.esploader;
    $("btnConnect").disabled = !!state.esploader;
  }));

  $("btnFetchRelease").addEventListener("click", () => fetchLatestRelease().catch((e) => {
    setStatus(e.message, "err");
    log(String(e.message));
  }));
}

init();
