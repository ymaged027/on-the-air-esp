const vscode = require("vscode");
const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");

function launchWebIde() {
  const cfg = vscode.workspace.getConfiguration("mtrOta");
  let batPath = (cfg.get("webIdeBatPath") || "").trim();

  if (!batPath || batPath === "__WEB_IDE_BAT_PATH__" || !fs.existsSync(batPath)) {
    const home = process.env.USERPROFILE || process.env.HOME || "";
    const fallback = path.join(home, "on-the-air-esp", "web-ide.bat");
    if (fs.existsSync(fallback)) {
      batPath = fallback;
    }
  }

  if (!batPath || !fs.existsSync(batPath)) {
    vscode.window.showErrorMessage(
      "MTR-OTA: Could not find web-ide.bat. Set 'mtrOta.webIdeBatPath' in Settings or re-run install-library.bat."
    );
    return;
  }

  try {
    if (process.platform === "win32") {
      const child = spawn("cmd.exe", ["/c", "start", '""', batPath], {
        cwd: path.dirname(batPath),
        detached: true,
        stdio: "ignore",
        windowsHide: true
      });
      child.unref();
    } else {
      const studioPy = path.join(path.dirname(batPath), "tools", "ota_studio.py");
      const child = spawn("python3", [studioPy], {
        cwd: path.dirname(batPath),
        detached: true,
        stdio: "ignore"
      });
      child.unref();
    }
    vscode.window.setStatusBarMessage("$(globe) Launching MTR-OTA Web IDE (http://127.0.0.1:8080)...", 4000);
  } catch (err) {
    vscode.window.showErrorMessage("MTR-OTA: Failed to launch web-ide.bat: " + err.message);
  }
}

function activate(context) {
  const cmd = vscode.commands.registerCommand("mtr-ota.launchWebIde", launchWebIde);
  context.subscriptions.push(cmd);

  const btn = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 1000);
  btn.text = "$(globe) MTR-OTA Web IDE";
  btn.tooltip = "Launch MTR-OTA Web IDE (web-ide.bat) — Ctrl+Alt+W";
  btn.command = "mtr-ota.launchWebIde";
  btn.show();
  context.subscriptions.push(btn);
}

function deactivate() {}

module.exports = { activate, deactivate };
