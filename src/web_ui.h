#pragma once

#include <Arduino.h>

// Single-page OTA Web Console stored in PROGMEM (auto-opens via Captive Portal).
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
  .wrap{max-width:760px;margin:0 auto}
  h1{margin:0 0 4px;font-size:1.35rem;display:flex;align-items:center;gap:10px;flex-wrap:wrap}
  .sub{color:var(--mut);font-size:.88rem;margin:0 0 16px}
  .badge{font-size:.74rem;padding:3px 8px;border-radius:999px;font-weight:600;border:1px solid var(--bd)}
  .b-ok{background:#12261e;color:var(--ok);border-color:#238636}
  .b-warn{background:#2d220a;color:var(--warn);border-color:#9e6a03}
  .b-err{background:#2d1215;color:var(--err);border-color:#da3633}
  .card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:16px;margin-bottom:14px}
  .card h2{margin:0 0 12px;font-size:1.02rem}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}
  .kv{background:var(--bg);border:1px solid var(--bd);border-radius:8px;padding:10px}
  .kv .k{color:var(--mut);font-size:.73rem;text-transform:uppercase;letter-spacing:.04em}
  .kv .v{font-size:1.02rem;font-weight:600;margin-top:3px;word-break:break-all}
  label{display:block;font-size:.84rem;color:var(--mut);margin-bottom:4px}
  input,select{width:100%;padding:8px 10px;background:var(--bg);color:var(--tx);border:1px solid var(--bd);border-radius:7px;font-size:.92rem}
  .row{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:10px;margin-bottom:10px}
  .btn{background:var(--acc);color:#0d1117;border:none;border-radius:7px;padding:10px 16px;font-weight:700;cursor:pointer;font-size:.92rem}
  .btn:disabled{opacity:.45;cursor:not-allowed}
  .btn-sec{background:#21262d;color:var(--tx);border:1px solid var(--bd)}
  .btn-warn{background:var(--warn);color:#0d1117}
  .actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
  .drop{border:2px dashed var(--bd);border-radius:10px;padding:22px;text-align:center;margin:10px 0;background:var(--bg);cursor:pointer;transition:border-color .15s,background .15s}
  .drop.over{border-color:var(--acc);background:#0c1929}
  .file-name{margin-top:8px;font-size:.88rem;color:var(--acc);font-weight:600}
  .bar-bg{height:12px;background:var(--bg);border:1px solid var(--bd);border-radius:999px;overflow:hidden;margin-top:10px;display:none}
  .bar{height:100%;width:0;background:var(--acc);transition:width .15s}
  .msg{margin-top:10px;font-size:.88rem;padding:9px 11px;border-radius:7px;display:none}
  .msg.ok{display:block;background:#12261e;color:var(--ok);border:1px solid #238636}
  .msg.err{display:block;background:#2d1215;color:var(--err);border:1px solid #da3633}
  .msg.info{display:block;background:#0c2d6b;color:#79c0ff;border:1px solid #1f6feb}
</style>
</head>
<body>
<div class="wrap">
  <h1>
    <span id="titleId">MTR-OTA</span>
    <span class="badge b-ok" id="badgeArmed">DISARMED</span>
    <span class="badge b-warn" id="badgeOta">CHECKING...</span>
  </h1>
  <p class="sub">ESP32 NodeMCU-32S Wireless OTA Console &bull; Wi-Fi turns 100% OFF in Normal Run Mode</p>

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

  <div class="card" id="tokenCard">
    <h2>OTA Password</h2>
    <label for="token">Saved automatically in this browser (default: <code>mtr-ota007</code>)</label>
    <input id="token" type="password" placeholder="Enter OTA password">
  </div>

  <div class="card">
    <h2>Over-The-Air Firmware Flash (.bin)</h2>
    <div class="drop" id="dropZone">
      <div><b>Drag &amp; drop</b> <code>on-the-air-esp.ino.bin</code> here, or <b>click to choose</b></div>
      <div style="font-size:.8rem;color:var(--mut);margin-top:4px">Arduino IDE: Sketch &rarr; Export Compiled Binary (pick <b>.ino.bin</b>)</div>
      <div class="file-name" id="fileLabel"></div>
      <input id="binFile" type="file" accept=".bin" style="display:none">
    </div>
    <div class="actions">
      <button class="btn" id="btnFlash">Flash Firmware</button>
    </div>
    <div class="bar-bg" id="barBg"><div class="bar" id="bar"></div></div>
    <div class="msg" id="otaMsg"></div>
  </div>

  <div class="card">
    <h2>Live Parameter Tuning (NVS Flash &mdash; No Recompile!)</h2>
    <div class="row">
      <div><label>Normal Speed (0-255)</label><input id="pNorm" type="number" min="0" max="255"></div>
      <div><label>Max Speed (0-255)</label><input id="pMax" type="number" min="0" max="255"></div>
      <div><label>Turn Speed (0-255)</label><input id="pTurn" type="number" min="0" max="255"></div>
    </div>
    <div class="row">
      <div><label>Sensor Threshold (0-4095)</label><input id="pSens" type="number" min="0" max="4095"></div>
      <div><label>Range Threshold (5-150 cm)</label><input id="pRng" type="number" min="5" max="150"></div>
      <div><label>Start Delay (0-10000 ms)</label><input id="pDelay" type="number" min="0" max="10000"></div>
    </div>
    <div class="row">
      <div>
        <label>Operating Mode</label>
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

function showMsg(el, cls, txt) {
  el.className = "msg " + cls;
  el.textContent = txt;
}

function getToken() {
  const t = $("token").value.trim();
  localStorage.setItem("ota_auth_token", t);
  return t;
}

function setFile(f) {
  selectedFile = f;
  if (!f) { $("fileLabel").textContent = ""; return; }
  $("fileLabel").textContent = f.name + " (" + Math.round(f.size / 1024) + " KB)";
  if (/bootloader|partitions|boot_app0/i.test(f.name)) {
    showMsg($("otaMsg"), "err", "Wrong file! Pick on-the-air-esp.ino.bin (NOT bootloader or partitions).");
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
      showMsg($("otaMsg"), "ok", "Back online! Running v" + d.version + " on slot " + d.partition + " (" + d.otaState + ").");
    } else if (tries > 15) {
      clearInterval(iv);
    }
  }, 1500);
}

$("btnFlash").onclick = async () => {
  const file = selectedFile || $("binFile").files[0];
  if (!file) return showMsg($("otaMsg"), "err", "Drop or select on-the-air-esp.ino.bin first.");
  if (!file.name.endsWith(".bin")) return showMsg($("otaMsg"), "err", "File must end with .bin");
  if (/bootloader|partitions|boot_app0/i.test(file.name)) {
    return showMsg($("otaMsg"), "err", "Wrong file! Pick on-the-air-esp.ino.bin (NOT bootloader or partitions).");
  }

  const head = new Uint8Array(await file.slice(0, 1).arrayBuffer());
  if (head[0] !== 0xE9) {
    return showMsg($("otaMsg"), "err", "Not a valid ESP32 app image (missing 0xE9 header).");
  }

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
  if (r.ok && d.ok) showMsg($("paramMsg"), "ok", "Parameters saved to NVS flash!");
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
if (savedTok) $("token").value = savedTok;
fetchStatus();
fetchParams();
setInterval(fetchStatus, 2500);
</script>
</body>
</html>)rawliteral";
