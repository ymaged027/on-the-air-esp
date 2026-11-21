#pragma once

#include <Arduino.h>

// Single-page Web Console + .ino/.bin Upload + Code Editor + Live OTA Monitor (PROGMEM).
static const char WEB_UI_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MTR-OTA — On The Air ESP</title>
<style>
  :root{--bg:#0d1117;--card:#161b22;--bd:#30363d;--tx:#e6edf3;--mut:#8b949e;--acc:#58a6ff;--ok:#3fb950;--warn:#d29922;--err:#f85149}
  *{box-sizing:border-box;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
  body{margin:0;background:var(--bg);color:var(--tx);padding:16px}
  .wrap{max-width:820px;margin:0 auto}
  h1{margin:0 0 4px;font-size:1.35rem;display:flex;align-items:center;gap:10px;flex-wrap:wrap}
  .sub{color:var(--mut);font-size:.88rem;margin:0 0 16px}
  .badge{font-size:.74rem;padding:3px 8px;border-radius:999px;font-weight:600;border:1px solid var(--bd)}
  .b-ok{background:#12261e;color:var(--ok);border-color:#238636}
  .b-warn{background:#2d220a;color:var(--warn);border-color:#9e6a03}
  .b-err{background:#2d1215;color:var(--err);border-color:#da3633}
  .card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:16px;margin-bottom:14px}
  .card h2{margin:0 0 12px;font-size:1.02rem;display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(135px,1fr));gap:10px}
  .kv{background:var(--bg);border:1px solid var(--bd);border-radius:8px;padding:10px}
  .kv .k{color:var(--mut);font-size:.73rem;text-transform:uppercase;letter-spacing:.04em}
  .kv .v{font-size:1.02rem;font-weight:600;margin-top:3px;word-break:break-all}
  label{display:block;font-size:.84rem;color:var(--mut);margin-bottom:4px}
  input,select,textarea{width:100%;padding:8px 10px;background:var(--bg);color:var(--tx);border:1px solid var(--bd);border-radius:7px;font-size:.92rem}
  .row{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px;margin-bottom:10px}
  .btn{background:var(--acc);color:#0d1117;border:none;border-radius:7px;padding:9px 15px;font-weight:700;cursor:pointer;font-size:.9rem}
  .btn:disabled{opacity:.45;cursor:not-allowed}
  .btn-sm{padding:5px 10px;font-size:.8rem}
  .btn-ok{background:var(--ok);color:#0d1117}
  .btn-sec{background:#21262d;color:var(--tx);border:1px solid var(--bd)}
  .btn-warn{background:var(--warn);color:#0d1117}
  .actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px;align-items:center}
  .drop{border:2px dashed var(--bd);border-radius:10px;padding:20px;text-align:center;margin:10px 0;background:var(--bg);cursor:pointer;transition:border-color .15s,background .15s}
  .drop.over{border-color:var(--acc);background:#0c1929}
  .file-name{margin-top:8px;font-size:.88rem;color:var(--acc);font-weight:600}
  .bar-bg{height:12px;background:var(--bg);border:1px solid var(--bd);border-radius:999px;overflow:hidden;margin-top:10px;display:none}
  .bar{height:100%;width:0;background:var(--acc);transition:width .15s}
  .msg{margin-top:10px;font-size:.88rem;padding:9px 11px;border-radius:7px;display:none;white-space:pre-wrap}
  .msg.ok{display:block;background:#12261e;color:var(--ok);border:1px solid #238636}
  .msg.err{display:block;background:#2d1215;color:var(--err);border:1px solid #da3633}
  .msg.info{display:block;background:#0c2d6b;color:#79c0ff;border:1px solid #1f6feb}
  .term{background:#05070b;color:#7ee787;border:1px solid var(--bd);border-radius:8px;padding:12px;height:210px;overflow-y:auto;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:.83rem;line-height:1.42;white-space:pre-wrap;word-break:break-word;margin:0}
  .code-ed{background:#05070b;color:#e6edf3;border:1px solid var(--bd);border-radius:8px;padding:12px;min-height:250px;resize:vertical;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:.85rem;line-height:1.45;tab-size:2}
  .code-ed.over{border-color:var(--acc);background:#0c1929}
  .cmd-row{display:flex;gap:8px;margin-top:8px}
  .cmd-row input{flex:1;font-family:ui-monospace,SFMono-Regular,Consolas,monospace}
  .chk{display:flex;align-items:center;gap:6px;font-size:.84rem;color:var(--mut);margin-top:8px;cursor:pointer}
  .chk input{width:auto;margin:0}
  .hint{font-size:.8rem;color:var(--mut);margin-top:6px}
</style>
</head>
<body>
<div class="wrap">
  <h1>
    <span id="titleId">MTR-OTA</span>
    <span class="badge b-ok" id="badgeArmed">DISARMED</span>
    <span class="badge b-warn" id="badgeOta">CHECKING...</span>
  </h1>
  <p class="sub">ESP32 NodeMCU-32S Wireless Code Studio, .ino/.bin Flasher &amp; OTA Monitor</p>

  <div class="card">
    <h2>Device Telemetry</h2>
    <div class="grid">
      <div class="kv"><div class="k">Firmware</div><div class="v" id="stVer">-</div></div>
      <div class="kv"><div class="k">Active Slot</div><div class="v" id="stPart">-</div></div>
      <div class="kv"><div class="k">OTA State</div><div class="v" id="stState">-</div></div>
      <div class="kv"><div class="k">Battery</div><div class="v" id="stBatt">-</div></div>
      <div class="kv"><div class="k">Free Heap</div><div class="v" id="stHeap">-</div></div>
      <div class="kv"><div class="k">Uptime</div><div class="v" id="stUp">-</div></div>
    </div>
  </div>

  <div class="card">
    <h2>
      <span>Code Window (Paste or Drop <code>.ino</code> File)</span>
      <span class="badge b-warn" id="studioBadge">CHECKING COMPILER...</span>
    </h2>
    <textarea class="code-ed" id="codeEd" spellcheck="false"></textarea>
    <input id="inoPicker" type="file" accept=".ino,.cpp,.h,.txt" style="display:none">
    <label class="chk">
      <input type="checkbox" id="chkKeepOta" checked>
      Auto-keep MTR-OTA active (automatically adds <code>OnTheAirESP.begin()</code> if missing)
    </label>
    <div class="actions">
      <button class="btn btn-ok" id="btnCodeFlash">Compile &amp; Flash to ESP32</button>
      <button class="btn btn-sec" id="btnCodeCompile">Verify Only</button>
      <button class="btn btn-sec" id="btnOpenIno">Open .ino File</button>
      <button class="btn btn-sec" id="btnCodeReset">Load Example</button>
      <button class="btn btn-sec" id="btnCodeDl">Download .ino</button>
    </div>
    <div class="hint" id="studioHint">Install the silent background compiler once (<code>install-background-compiler.bat</code>) so compiling works automatically with no terminal window!</div>
    <div class="msg" id="codeMsg"></div>
    <pre class="term" id="buildOut" style="display:none;height:140px;margin-top:8px;color:#c9d1d9"></pre>
  </div>

  <div class="card">
    <h2>
      <span>OTA Monitor <span class="badge b-ok" id="monLive">LIVE</span></span>
      <span style="display:flex;gap:6px">
        <button class="btn btn-sec btn-sm" id="btnMonPause">Pause</button>
        <button class="btn btn-sec btn-sm" id="btnMonClear">Clear</button>
      </span>
    </h2>
    <pre class="term" id="monOut">Connecting to OTA Monitor...</pre>
    <div class="cmd-row">
      <input id="monCmd" type="text" placeholder="Type command (help, status, params, reboot, rollback)...">
      <button class="btn" id="btnMonSend">Send</button>
    </div>
  </div>

  <div class="card" id="tokenCard">
    <h2>OTA Password</h2>
    <label for="token">Saved automatically in this browser (default: <code>mtr-ota007</code>)</label>
    <input id="token" type="password" placeholder="Enter OTA password">
  </div>

  <div class="card">
    <h2>Live Parameter Tuning (NVS Flash &mdash; Read via <code>OnTheAirESP.params()</code>)</h2>
    <div class="row">
      <div><label>speedNormal (0-255)</label><input id="pNorm" type="number" min="0" max="255"></div>
      <div><label>speedMax (0-255)</label><input id="pMax" type="number" min="0" max="255"></div>
      <div><label>turnSpeed (0-255)</label><input id="pTurn" type="number" min="0" max="255"></div>
    </div>
    <div class="row">
      <div><label>sensorThreshold (0-4095)</label><input id="pSens" type="number" min="0" max="4095"></div>
      <div><label>rangeThreshold (5-150 cm)</label><input id="pRng" type="number" min="5" max="150"></div>
      <div><label>startDelayMs (0-10000 ms)</label><input id="pDelay" type="number" min="0" max="10000"></div>
    </div>
    <div class="row">
      <div>
        <label>mode (0-3)</label>
        <select id="pMode">
          <option value="0">0 - Default</option>
          <option value="1">1 - Profile 1</option>
          <option value="2">2 - Profile 2</option>
          <option value="3">3 - Profile 3</option>
        </select>
      </div>
    </div>
    <div class="actions">
      <button class="btn" id="btnSaveParams">Save to Flash</button>
      <button class="btn btn-sec" id="btnResetParams">Reset Defaults</button>
    </div>
    <div class="msg" id="paramMsg"></div>
  </div>

  <div class="card">
    <h2>Upload File (.ino or .bin)</h2>
    <div class="drop" id="dropZone">
      <div><b>Drag &amp; drop</b> any <code>.ino</code> sketch or <code>.ino.bin</code> file here, or <b>click to choose</b></div>
      <div style="font-size:.8rem;color:var(--mut);margin-top:4px"><code>.ino</code> auto-compiles &amp; flashes via background compiler &bull; <code>.bin</code> flashes directly</div>
      <div class="file-name" id="fileLabel"></div>
      <input id="binFile" type="file" accept=".bin,.ino" style="display:none">
    </div>
    <div class="actions">
      <button class="btn" id="btnFlash">Flash File</button>
    </div>
    <div class="bar-bg" id="barBg"><div class="bar" id="bar"></div></div>
    <div class="msg" id="otaMsg"></div>
  </div>

  <div class="card">
    <h2>Recovery &amp; Control</h2>
    <div class="actions">
      <button class="btn btn-warn" id="btnRollback">Rollback to Previous Firmware</button>
      <button class="btn btn-sec" id="btnReboot">Reboot Device</button>
    </div>
    <div class="msg" id="ctlMsg"></div>
  </div>
</div>

<script>
const $ = id => document.getElementById(id);
let deviceId = "";
let selectedFile = null;
let monCursor = 0;
let monPaused = false;
let monFirst = true;
let studioBase = "";

const DEFAULT_CODE = `#include <OnTheAirESP.h>

void setup() {
  OnTheAirESP.begin();  // Zero buttons! Keeps MTR-OTA & OTA Monitor live
  OtaMonitor.println("ESP32 started!");
}

void loop() {
  // Read live-tuned parameters directly from the Web Portal:
  auto &p = OnTheAirESP.params();
  OtaMonitor.printf("speedNormal=%u speedMax=%u sensorThr=%u mode=%u\\n",
                    p.speedNormal, p.speedMax, p.sensorThreshold, p.mode);
  delay(2000);
}
`;

function showMsg(el, cls, txt) {
  el.className = "msg " + cls;
  el.textContent = txt;
}

function getToken() {
  const t = $("token").value.trim();
  localStorage.setItem("ota_auth_token", t);
  return t;
}

// --- Code Window + .ino File Loader + Silent Compiler Detection ---
const ed = $("codeEd");
ed.value = localStorage.getItem("ota_web_code") || DEFAULT_CODE;
ed.addEventListener("input", () => localStorage.setItem("ota_web_code", ed.value));
ed.addEventListener("keydown", e => {
  if (e.key === "Tab") {
    e.preventDefault();
    const s = ed.selectionStart, end = ed.selectionEnd;
    ed.value = ed.value.substring(0, s) + "  " + ed.value.substring(end);
    ed.selectionStart = ed.selectionEnd = s + 2;
  }
});

async function loadInoIntoEditor(file) {
  const txt = await file.text();
  ed.value = txt;
  localStorage.setItem("ota_web_code", txt);
  showMsg($("codeMsg"), "info", "Loaded " + file.name + " (" + txt.length + " chars) into Code Window. Click 'Compile & Flash to ESP32'!");
}

ed.addEventListener("dragover", e => { e.preventDefault(); ed.classList.add("over"); });
ed.addEventListener("dragleave", () => ed.classList.remove("over"));
ed.addEventListener("drop", async e => {
  if (e.dataTransfer.files && e.dataTransfer.files.length) {
    e.preventDefault();
    ed.classList.remove("over");
    await loadInoIntoEditor(e.dataTransfer.files[0]);
  }
});

$("btnOpenIno").onclick = () => $("inoPicker").click();
$("inoPicker").onchange = async e => {
  const f = e.target.files[0];
  if (f) await loadInoIntoEditor(f);
  e.target.value = "";
};

async function detectStudio() {
  const candidates = (location.hostname === "127.0.0.1" || location.hostname === "localhost")
    ? [""] : ["http://127.0.0.1:8080", "http://localhost:8080"];
  for (const base of candidates) {
    try {
      const r = await fetch(base + "/local/health", {cache: "no-store"});
      const d = await r.json();
      if (d && d.studio) {
        studioBase = base;
        $("studioBadge").textContent = "COMPILER READY";
        $("studioBadge").className = "badge b-ok";
        $("studioHint").textContent = "Background compiler ready — paste code or drop any .ino file and click Compile & Flash!";
        return true;
      }
    } catch (e) {}
  }
  studioBase = "";
  $("studioBadge").textContent = "COMPILER OFFLINE";
  $("studioBadge").className = "badge b-warn";
  $("studioHint").innerHTML = "Compiler not detected. Run <code>install-background-compiler.bat</code> once (or in Brave on 192.168.4.1, disable Shields for this page / bookmark <code>http://127.0.0.1:8080</code>).";
  return false;
}

async function runCodeAction(doFlash, codeOverride = null) {
  const ok = await detectStudio();
  if (!ok) {
    return showMsg($("codeMsg"), "err",
      "Background PC compiler is not reachable.\nRun install-background-compiler.bat once (starts silently at login with no terminal window), or bookmark http://127.0.0.1:8080 (Brave Shields blocks 192.168.4.1 -> localhost by default).");
  }

  $("btnCodeFlash").disabled = true;
  $("btnCodeCompile").disabled = true;
  $("btnFlash").disabled = true;
  $("buildOut").style.display = "block";
  $("buildOut").textContent = doFlash ? "Compiling & flashing to ESP32 over Wi-Fi...\n" : "Compiling sketch...\n";
  showMsg($("codeMsg"), "info", doFlash ? "Compiling & flashing (usually 5-15s)..." : "Compiling...");

  try {
    const r = await fetch(studioBase + "/local/compile", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({
        code: codeOverride !== null ? codeOverride : ed.value,
        flash: doFlash,
        autoKeepOta: $("chkKeepOta").checked,
        token: getToken() || "mtr-ota007"
      })
    });
    const d = await r.json();
    $("buildOut").textContent = d.output || "";
    $("buildOut").scrollTop = $("buildOut").scrollHeight;
    if (r.ok && d.ok) {
      showMsg($("codeMsg"), "ok", d.message || (doFlash ? "Compiled & flashed!" : "Compile succeeded!"));
      if (doFlash) setTimeout(waitReconnect, 2500);
    } else {
      showMsg($("codeMsg"), "err", d.error || "Compile/Flash failed (see build log below).");
    }
  } catch (e) {
    showMsg($("codeMsg"), "err", "Lost connection to background compiler: " + e);
  } finally {
    $("btnCodeFlash").disabled = false;
    $("btnCodeCompile").disabled = false;
    $("btnFlash").disabled = false;
  }
}

$("btnCodeFlash").onclick = () => runCodeAction(true);
$("btnCodeCompile").onclick = () => runCodeAction(false);
$("btnCodeReset").onclick = () => {
  ed.value = DEFAULT_CODE;
  localStorage.setItem("ota_web_code", ed.value);
  showMsg($("codeMsg"), "info", "Loaded starter example using OnTheAirESP.params() + OtaMonitor.");
};
$("btnCodeDl").onclick = () => {
  const blob = new Blob([ed.value], {type: "text/plain"});
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = "on-the-air-esp.ino";
  a.click();
};

// --- File Upload (.ino OR .bin) ---
async function setFile(f) {
  selectedFile = f;
  if (!f) { $("fileLabel").textContent = ""; return; }
  $("fileLabel").textContent = f.name + " (" + Math.max(1, Math.round(f.size / 1024)) + " KB)";
  if (f.name.toLowerCase().endsWith(".ino")) {
    await loadInoIntoEditor(f);
    showMsg($("otaMsg"), "info", "Arduino sketch (.ino) selected & loaded into Code Window — click 'Flash File' to compile & flash!");
  } else if (/bootloader|partitions|boot_app0/i.test(f.name)) {
    showMsg($("otaMsg"), "err", "Wrong file! Pick *.ino.bin or *.ino (NOT bootloader or partitions).");
  } else {
    $("otaMsg").className = "msg";
  }
}
const dz = $("dropZone");
dz.onclick = () => $("binFile").click();
$("binFile").onchange = e => setFile(e.target.files[0] || null);
dz.ondragover = e => { e.preventDefault(); dz.classList.add("over"); };
dz.ondragleave = () => dz.classList.remove("over");
dz.ondrop = e => {
  e.preventDefault();
  dz.classList.remove("over");
  if (e.dataTransfer.files.length) setFile(e.dataTransfer.files[0]);
};

// --- OTA Monitor ---
async function pollLogs() {
  if (monPaused) return;
  try {
    const r = await fetch("/api/logs?since=" + monCursor);
    const d = await r.json();
    if (!d.ok) return;
    if (monFirst) { $("monOut").textContent = ""; monFirst = false; }
    if (d.next < monCursor) {
      const el = $("monOut");
      if (el.textContent.length) el.textContent += "\n--- Device rebooted ---\n";
      if (!d.data) { monCursor = 0; return; }
    }
    if (d.data) {
      const el = $("monOut");
      el.textContent += d.data;
      if (el.textContent.length > 20000) el.textContent = el.textContent.slice(-16000);
      el.scrollTop = el.scrollHeight;
    }
    monCursor = d.next;
    $("monLive").textContent = "LIVE";
    $("monLive").className = "badge b-ok";
  } catch (e) {
    $("monLive").textContent = "OFFLINE";
    $("monLive").className = "badge b-err";
  }
}

async function sendMonCmd() {
  const cmd = $("monCmd").value.trim();
  if (!cmd) return;
  $("monCmd").value = "";
  await fetch("/api/logs/send", {
    method: "POST",
    headers: {"Content-Type": "application/x-www-form-urlencoded"},
    body: new URLSearchParams({cmd})
  }).catch(() => {});
  pollLogs();
}

$("btnMonSend").onclick = sendMonCmd;
$("monCmd").onkeydown = e => { if (e.key === "Enter") sendMonCmd(); };
$("btnMonPause").onclick = () => {
  monPaused = !monPaused;
  $("btnMonPause").textContent = monPaused ? "Resume" : "Pause";
  $("monLive").textContent = monPaused ? "PAUSED" : "LIVE";
  $("monLive").className = "badge " + (monPaused ? "b-warn" : "b-ok");
  if (!monPaused) pollLogs();
};
$("btnMonClear").onclick = async () => {
  await fetch("/api/logs/clear", {method: "POST"}).catch(() => {});
  $("monOut").textContent = "";
  monCursor = 0;
};

// --- Telemetry & Parameters ---
async function fetchStatus() {
  try {
    const r = await fetch("/api/status");
    const d = await r.json();
    deviceId = d.deviceId || "";
    $("titleId").textContent = d.deviceId + " (" + d.board + ")";
    $("stVer").textContent = "v" + d.version;
    $("stPart").textContent = d.partition;
    $("stState").textContent = d.otaState;
    $("stBatt").textContent = d.batteryMonitor ? (d.batteryV.toFixed(2) + " V") : "Disabled";
    $("stHeap").textContent = Math.round(d.freeHeap / 1024) + " KB";
    $("stUp").textContent = d.uptimeSec + " s";
    $("tokenCard").style.display = (d.authRequired === false) ? "none" : "block";

    const ba = $("badgeArmed");
    ba.textContent = d.armed ? "ARMED (LOCKED)" : "DISARMED";
    ba.className = "badge " + (d.armed ? "b-err" : "b-ok");

    const bo = $("badgeOta");
    bo.textContent = d.otaState;
    bo.className = "badge " + (d.otaState === "VALID" ? "b-ok" : "b-warn");
    return d;
  } catch (e) { return null; }
}

async function fetchParams() {
  try {
    const r = await fetch("/api/params");
    const p = await r.json();
    $("pNorm").value = p.speedNormal;
    $("pMax").value = p.speedMax;
    $("pTurn").value = p.turnSpeed;
    $("pSens").value = p.sensorThreshold;
    $("pRng").value = p.rangeThreshold;
    $("pDelay").value = p.startDelayMs;
    $("pMode").value = p.mode;
  } catch (e) {}
}

function waitReconnect() {
  let tries = 0;
  const iv = setInterval(async () => {
    tries++;
    const d = await fetchStatus();
    if (d && d.ok) {
      clearInterval(iv);
      showMsg($("codeMsg"), "ok", "ESP32 rebooted & back online! Slot " + d.partition + " (" + d.otaState + ").");
      showMsg($("otaMsg"), "ok", "Back online! Running v" + d.version + " on slot " + d.partition + " (" + d.otaState + ").");
    } else if (tries > 15) {
      clearInterval(iv);
    }
  }, 1500);
}

$("btnFlash").onclick = async () => {
  const file = selectedFile || $("binFile").files[0];
  if (!file) return showMsg($("otaMsg"), "err", "Drop or select a .ino or .bin file first.");

  if (file.name.toLowerCase().endsWith(".ino")) {
    const src = await file.text();
    ed.value = src;
    localStorage.setItem("ota_web_code", src);
    showMsg($("otaMsg"), "info", "Compiling & flashing " + file.name + " (see Code Window log above)...");
    await runCodeAction(true, src);
    return;
  }

  if (!file.name.endsWith(".bin")) return showMsg($("otaMsg"), "err", "File must end with .ino or .bin");
  if (/bootloader|partitions|boot_app0/i.test(file.name)) {
    return showMsg($("otaMsg"), "err", "Wrong file! Pick *.ino.bin (NOT bootloader or partitions).");
  }
  const head = new Uint8Array(await file.slice(0, 1).arrayBuffer());
  if (head[0] !== 0xE9) return showMsg($("otaMsg"), "err", "Not a valid ESP32 app image (missing 0xE9 header).");

  const tok = getToken();
  const fd = new FormData();
  fd.append("firmware", file, file.name);

  $("barBg").style.display = "block";
  $("bar").style.width = "0%";
  $("btnFlash").disabled = true;
  showMsg($("otaMsg"), "info", "Uploading " + file.name + " (" + Math.round(file.size / 1024) + " KB)...");

  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/api/ota", true);
  xhr.setRequestHeader("X-OTA-Token", tok);
  if (deviceId) xhr.setRequestHeader("X-Target-Device", deviceId);
  xhr.upload.onprogress = e => {
    if (e.lengthComputable) $("bar").style.width = Math.round((e.loaded / e.total) * 100) + "%";
  };
  xhr.onload = () => {
    $("btnFlash").disabled = false;
    try {
      const res = JSON.parse(xhr.responseText);
      if (xhr.status === 200 && res.ok) {
        $("bar").style.width = "100%";
        showMsg($("otaMsg"), "ok", "Flash OK! Device is rebooting into the new slot...");
        setTimeout(waitReconnect, 2500);
      } else {
        showMsg($("otaMsg"), "err", "Flash rejected: " + (res.error || xhr.statusText));
      }
    } catch {
      showMsg($("otaMsg"), "err", "Upload error (HTTP " + xhr.status + ")");
    }
  };
  xhr.onerror = () => {
    $("btnFlash").disabled = false;
    showMsg($("otaMsg"), "err", "Connection lost during upload.");
  };
  xhr.send(fd);
};

$("btnSaveParams").onclick = async () => {
  const body = new URLSearchParams({
    speedNormal: $("pNorm").value,
    speedMax: $("pMax").value,
    turnSpeed: $("pTurn").value,
    sensorThreshold: $("pSens").value,
    rangeThreshold: $("pRng").value,
    startDelayMs: $("pDelay").value,
    mode: $("pMode").value
  });
  const r = await fetch("/api/params", {
    method: "POST",
    headers: {"X-OTA-Token": getToken(), "Content-Type": "application/x-www-form-urlencoded"},
    body
  });
  const d = await r.json().catch(() => ({}));
  if (r.ok && d.ok) showMsg($("paramMsg"), "ok", "Parameters saved to NVS flash! Active immediately in OnTheAirESP.params().");
  else showMsg($("paramMsg"), "err", "Failed: " + (d.error || r.status));
};

$("btnResetParams").onclick = async () => {
  const r = await fetch("/api/params/reset", {method: "POST", headers: {"X-OTA-Token": getToken()}});
  const d = await r.json().catch(() => ({}));
  if (r.ok && d.ok) {
    showMsg($("paramMsg"), "ok", "Parameters reset to defaults.");
    fetchParams();
  } else {
    showMsg($("paramMsg"), "err", "Failed: " + (d.error || r.status));
  }
};

$("btnRollback").onclick = async () => {
  if (!confirm("Roll back to the previous OTA partition and reboot?")) return;
  const r = await fetch("/api/rollback", {method: "POST", headers: {"X-OTA-Token": getToken()}});
  const d = await r.json().catch(() => ({}));
  if (r.ok && d.ok) showMsg($("ctlMsg"), "ok", "Rolling back and rebooting...");
  else showMsg($("ctlMsg"), "err", "Rollback failed: " + (d.error || r.status));
};

$("btnReboot").onclick = async () => {
  const r = await fetch("/api/reboot", {method: "POST", headers: {"X-OTA-Token": getToken()}});
  const d = await r.json().catch(() => ({}));
  if (r.ok && d.ok) showMsg($("ctlMsg"), "info", "Rebooting...");
  else showMsg($("ctlMsg"), "err", "Reboot failed: " + (d.error || r.status));
};

const savedTok = localStorage.getItem("ota_auth_token");
$("token").value = savedTok || "mtr-ota007";
detectStudio();
fetchStatus();
fetchParams();
pollLogs();
setInterval(fetchStatus, 2500);
setInterval(pollLogs, 700);
</script>
</body>
</html>)rawliteral";
