const vscode = require("vscode");
const { spawn } = require("child_process");
const fs = require("fs");
const http = require("http");
const dns = require("dns");
const path = require("path");

let studioProc = null;
let outChannel = null;
let statusBar = null;
let pollTimer = null;
let lastOnlineHost = null;

function getOutput() {
  if (!outChannel) {
    outChannel = vscode.window.createOutputChannel("MTR-OTA");
  }
  return outChannel;
}

function getRepoRoot() {
  const cfg = vscode.workspace.getConfiguration("mtrOta");
  let repo = (cfg.get("repoPath") || "").trim();
  if (!repo || repo === "__REPO_PATH__" || !fs.existsSync(repo)) {
    const home = process.env.USERPROFILE || process.env.HOME || "";
    const fallback = path.join(home, "on-the-air-esp");
    if (fs.existsSync(fallback)) repo = fallback;
  }
  return repo && fs.existsSync(repo) ? repo : "";
}

function getPythonCmd() {
  return process.platform === "win32" ? "python" : "python3";
}

function httpJson(method, host, port, urlPath, bodyObj, headers, timeoutMs = 2000) {
  return new Promise((resolve, reject) => {
    const payload = bodyObj ? Buffer.from(JSON.stringify(bodyObj), "utf8") : null;
    const reqHeaders = Object.assign({}, headers || {});
    if (payload) {
      reqHeaders["Content-Type"] = "application/json";
      reqHeaders["Content-Length"] = String(payload.length);
    }
    const req = http.request(
      { method, hostname: host, port, path: urlPath, headers: reqHeaders, timeout: timeoutMs },
      res => {
        const chunks = [];
        res.on("data", c => chunks.push(c));
        res.on("end", () => {
          const raw = Buffer.concat(chunks).toString("utf8");
          try {
            resolve({ status: res.statusCode || 0, data: raw ? JSON.parse(raw) : {} });
          } catch {
            resolve({ status: res.statusCode || 0, data: { raw } });
          }
        });
      }
    );
    req.on("timeout", () => { req.destroy(new Error("timeout")); });
    req.on("error", reject);
    if (payload) req.write(payload);
    req.end();
  });
}

function resolveMdnsIp(hostname) {
  return new Promise(resolve => {
    dns.lookup(hostname, { family: 4 }, (err, address) => {
      resolve(err ? null : address);
    });
  });
}

async function probeEspStatus() {
  const candidates = ["192.168.4.1"];
  if (lastOnlineHost && lastOnlineHost !== "192.168.4.1") {
    candidates.unshift(lastOnlineHost);
  } else {
    const mdnsIp = await resolveMdnsIp("MTR-OTA.local");
    if (mdnsIp && mdnsIp !== "127.0.0.1" && !candidates.includes(mdnsIp)) {
      candidates.push(mdnsIp);
    }
  }

  for (const host of candidates) {
    try {
      const r = await httpJson("GET", host, 80, "/api/status", null, null, 900);
      if (r.status === 200 && r.data && r.data.ok) {
        lastOnlineHost = host;
        return { host, status: r.data };
      }
    } catch {}
  }
  return null;
}

async function updateStatusBar() {
  if (!statusBar) return;
  const res = await probeEspStatus();
  if (res) {
    const s = res.status;
    const sig = s.staConnected ? `${s.rssi}dBm` : "AP";
    statusBar.text = `$(radio-tower) ${s.deviceId} v${s.version} [${s.partition} • ${sig}]`;
    statusBar.tooltip =
      `Connected to ${s.deviceId} at http://${res.host}\n` +
      `Slot: ${s.partition} (${s.otaState}) | Heap: ${Math.round((s.freeHeap || 0) / 1024)} KB | Uptime: ${s.uptimeSec}s\n` +
      `Click for MTR-OTA actions (Web IDE, Quick Flash, Rollback, Reboot)`;
  } else {
    statusBar.text = studioProc ? "$(globe) MTR-OTA Web IDE (Running)" : "$(globe) MTR-OTA";
    statusBar.tooltip = "Open MTR-OTA Control Menu (Ctrl+Alt+M) or Launch Web IDE (Ctrl+Alt+W)";
  }
}

function getActiveSketchPayload() {
  const ed = vscode.window.activeTextEditor;
  if (!ed || !ed.document) return null;
  const code = ed.document.getText();
  if (!code || !code.trim()) return null;
  const name = ed.document.fileName ? path.basename(ed.document.fileName) : "sketch.ino";
  return { code, name };
}

async function ensureStudioServer() {
  const cfg = vscode.workspace.getConfiguration("mtrOta");
  const port = Number(cfg.get("studioPort") || 8080);

  try {
    const h = await httpJson("GET", "127.0.0.1", port, "/local/health", null, null, 700);
    if (h.status === 200 && h.data && h.data.studio) {
      return port;
    }
  } catch {}

  const repo = getRepoRoot();
  if (!repo) {
    vscode.window.showErrorMessage(
      "MTR-OTA: Could not find on-the-air-esp repository. Re-run install-library.bat."
    );
    return null;
  }

  const studioScript = path.join(repo, "tools", "ota_studio.py");
  const out = getOutput();
  out.appendLine(`[MTR-OTA] Starting Web IDE server on http://127.0.0.1:${port} ...`);

  studioProc = spawn(getPythonCmd(), [studioScript, "--no-browser", "--port", String(port)], {
    cwd: repo,
    windowsHide: true
  });
  studioProc.stdout.on("data", d => out.append(d.toString()));
  studioProc.stderr.on("data", d => out.append(d.toString()));
  studioProc.on("exit", code => {
    out.appendLine(`[MTR-OTA] Web IDE server exited (code ${code}).`);
    studioProc = null;
    updateStatusBar();
  });

  for (let i = 0; i < 20; i++) {
    await new Promise(r => setTimeout(r, 150));
    try {
      const h = await httpJson("GET", "127.0.0.1", port, "/local/health", null, null, 500);
      if (h.status === 200 && h.data && h.data.studio) {
        updateStatusBar();
        return port;
      }
    } catch {}
  }
  return port;
}

async function launchWebIde() {
  const port = await ensureStudioServer();
  if (!port) return;

  const sketch = getActiveSketchPayload();
  if (sketch) {
    try {
      await httpJson("POST", "127.0.0.1", port, "/local/open-sketch", sketch, null, 1500);
    } catch {}
  }

  const url = vscode.Uri.parse(`http://127.0.0.1:${port}`);
  await vscode.env.openExternal(url);
  vscode.window.setStatusBarMessage(
    sketch
      ? `$(globe) Opened MTR-OTA Web IDE with '${sketch.name}' synced!`
      : `$(globe) Opened MTR-OTA Web IDE (http://127.0.0.1:${port})`,
    4000
  );
}

async function quickWirelessFlash() {
  const sketch = getActiveSketchPayload();
  if (!sketch) {
    vscode.window.showWarningMessage("MTR-OTA: Open a sketch in the editor first to flash it wirelessly.");
    return;
  }

  const ed = vscode.window.activeTextEditor;
  if (ed && !ed.document.isUntitled && ed.document.isDirty) {
    await ed.document.save();
  }

  const port = await ensureStudioServer();
  if (!port) return;

  const cfg = vscode.workspace.getConfiguration("mtrOta");
  const token = String(cfg.get("otaPassword") || "mtr-ota007");
  const out = getOutput();
  out.show(true);
  out.appendLine(`\n[MTR-OTA] Compiling & wirelessly flashing '${sketch.name}' ...`);

  await vscode.window.withProgress(
    {
      location: vscode.ProgressLocation.Notification,
      title: `MTR-OTA: Compiling & flashing '${sketch.name}' over Wi-Fi...`,
      cancellable: false
    },
    async () => {
      try {
        const r = await httpJson(
          "POST",
          "127.0.0.1",
          port,
          "/local/compile",
          { code: sketch.code, flash: true, autoKeepOta: true, token },
          null,
          120000
        );
        if (r.data && r.data.output) out.appendLine(r.data.output);
        if (r.status === 200 && r.data && r.data.ok) {
          vscode.window.showInformationMessage(`MTR-OTA: ${r.data.message}`);
          setTimeout(updateStatusBar, 3500);
        } else {
          vscode.window.showErrorMessage(
            `MTR-OTA Flash Failed: ${(r.data && r.data.error) || "See MTR-OTA Output panel."}`
          );
        }
      } catch (err) {
        vscode.window.showErrorMessage(`MTR-OTA Flash Error: ${err.message}`);
      }
    }
  );
}

async function stopWebIde() {
  const cfg = vscode.workspace.getConfiguration("mtrOta");
  const port = Number(cfg.get("studioPort") || 8080);
  try {
    await httpJson("POST", "127.0.0.1", port, "/local/shutdown", {}, null, 1200);
  } catch {}
  if (studioProc) {
    try { studioProc.kill(); } catch {}
    studioProc = null;
  }
  updateStatusBar();
  vscode.window.showInformationMessage("MTR-OTA: Stopped Web IDE server.");
}

async function deviceAction(endpoint, label) {
  const res = await probeEspStatus();
  if (!res) {
    vscode.window.showErrorMessage("MTR-OTA: ESP32 not reachable on 192.168.4.1 or MTR-OTA.local.");
    return;
  }
  const cfg = vscode.workspace.getConfiguration("mtrOta");
  const token = String(cfg.get("otaPassword") || "mtr-ota007");
  try {
    const r = await httpJson("POST", res.host, 80, endpoint, null, { "X-OTA-Token": token }, 3000);
    if (r.status === 200 && r.data && r.data.ok) {
      vscode.window.showInformationMessage(`MTR-OTA: ${r.data.message || label + " sent!"}`);
      setTimeout(updateStatusBar, 3500);
    } else {
      vscode.window.showErrorMessage(`MTR-OTA: ${(r.data && r.data.error) || "Request failed"}`);
    }
  } catch (err) {
    vscode.window.showErrorMessage(`MTR-OTA: ${err.message}`);
  }
}

async function openTerminalMonitor() {
  const repo = getRepoRoot();
  if (!repo) return;
  const res = await probeEspStatus();
  const host = res ? res.host : "192.168.4.1";
  const term = vscode.window.createTerminal(`OTA Monitor (${host})`);
  term.show();
  const monPy = path.join(repo, "tools", "ota_monitor.py");
  term.sendText(`${getPythonCmd()} "${monPy}" --connect ${host}`);
}

async function showQuickMenu() {
  const items = [
    { label: "$(globe) Open MTR-OTA Web IDE", description: "Syncs your open sketch to http://127.0.0.1:8080 (Ctrl+Alt+W)", action: launchWebIde },
    { label: "$(zap) Quick Wireless Flash (HTTP OTA)", description: "Compiles & flashes active editor over Wi-Fi (Ctrl+Alt+U)", action: quickWirelessFlash },
    { label: "$(terminal) Open Wireless OTA Monitor (Terminal)", description: "Interactive live log & command console", action: openTerminalMonitor },
    { label: "$(history) Rollback ESP32 Firmware", description: "Switch back to previous OTA slot & reboot", action: () => deviceAction("/api/rollback", "Rollback") },
    { label: "$(refresh) Reboot ESP32", description: "Restart ESP32 wirelessly", action: () => deviceAction("/api/reboot", "Reboot") },
    { label: "$(debug-stop) Stop Web IDE Server", description: "Shut down local port 8080 compiler bridge", action: stopWebIde }
  ];
  const pick = await vscode.window.showQuickPick(items, {
    placeHolder: "MTR-OTA Wireless Control Menu"
  });
  if (pick && pick.action) await pick.action();
}

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand("mtr-ota.launchWebIde", launchWebIde),
    vscode.commands.registerCommand("mtr-ota.quickFlash", quickWirelessFlash),
    vscode.commands.registerCommand("mtr-ota.quickMenu", showQuickMenu),
    vscode.commands.registerCommand("mtr-ota.rollback", () => deviceAction("/api/rollback", "Rollback")),
    vscode.commands.registerCommand("mtr-ota.reboot", () => deviceAction("/api/reboot", "Reboot")),
    vscode.commands.registerCommand("mtr-ota.stopWebIde", stopWebIde)
  );

  statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 1000);
  statusBar.text = "$(globe) MTR-OTA";
  statusBar.command = "mtr-ota.quickMenu";
  statusBar.show();
  context.subscriptions.push(statusBar);

  updateStatusBar();
  pollTimer = setInterval(updateStatusBar, 6000);
  context.subscriptions.push({ dispose: () => clearInterval(pollTimer) });
}

function deactivate() {
  if (pollTimer) clearInterval(pollTimer);
  if (studioProc) {
    try { studioProc.kill(); } catch {}
    studioProc = null;
  }
}

module.exports = { activate, deactivate };
