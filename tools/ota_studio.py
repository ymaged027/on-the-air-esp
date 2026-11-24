#!/usr/bin/env python3
"""
MTR-OTA Local Web Code Studio (Zero-Dependency Python 3)
========================================================
Launched via the "MTR-OTA Web IDE" button inside Arduino IDE (or web-ide.bat).
Features:
  • Auto-discovers ESP32 on BOTH 192.168.4.1 (MTR-OTA SoftAP) AND MTR-OTA.local
    (Dual AP+STA home/lab Wi-Fi so your PC keeps full internet!).
  • Syncs your active Arduino IDE .ino sketch directly into the browser Code Window!
  • Compiles & flashes pasted code or .ino files wirelessly in 1 click!
"""

from __future__ import annotations

import argparse
import http.client
import http.server
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
import webbrowser
from pathlib import Path
from typing import Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parents[1]
WEB_UI_HEADER = REPO_ROOT / "src" / "web_ui.h"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ota_flash  # noqa: E402

RAW_LITERAL_RE = re.compile(r'R"rawliteral\((.*?)\)rawliteral"', re.DOTALL)
SETUP_DEF_RE = re.compile(r"(void\s+setup\s*\([^)]*\)\s*\{)")


def is_studio_running(port: int = 8080) -> bool:
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/local/health", timeout=0.8) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="replace"))
            return bool(data.get("studio"))
    except Exception:
        return False


def push_sketch_to_running_studio(code: str, name: str = "sketch.ino", port: int = 8080) -> bool:
    try:
        raw = json.dumps({"code": code, "name": name}).encode("utf-8")
        req = urllib.request.Request(
            f"http://127.0.0.1:{port}/local/open-sketch",
            data=raw,
            method="POST",
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=1.5) as resp:
            return resp.status == 200
    except Exception:
        return False


def extract_web_ui_html(header_path: Path = WEB_UI_HEADER) -> str:
    """Extract the HTML document directly from src/web_ui.h (single source of truth)."""
    text = header_path.read_text(encoding="utf-8")
    m = RAW_LITERAL_RE.search(text)
    if not m:
        raise ValueError(f"Could not find WEB_UI_HTML rawliteral in {header_path}")
    return m.group(1)


def ensure_ota_included(code: str) -> str:
    """Automatically inject OnTheAirESP if a pasted sketch forgot it so OTA is never lost."""
    if "OnTheAirESP.h" in code or "OnTheAirESP.begin" in code:
        return code

    out = "#include <OnTheAirESP.h>\n\n" + code
    if SETUP_DEF_RE.search(out):
        out = SETUP_DEF_RE.sub(r"\1\n  OnTheAirESP.begin();", out, count=1)
    else:
        out += "\nvoid setup() {\n  OnTheAirESP.begin();\n}\n"
    return out


def compile_code_string(
    code: str,
    auto_keep_ota: bool = True,
    fqbn: str = ota_flash.DEFAULT_FQBN,
) -> Tuple[bool, str, Optional[Path]]:
    """Write code to a persistent temp sketch folder and compile via arduino-cli."""
    cli = ota_flash.find_arduino_cli()
    if not cli:
        return (
            False,
            "Error: arduino-cli not found. Install Arduino IDE 2.x (https://www.arduino.cc/en/software).",
            None,
        )

    final_code = ensure_ota_included(code) if auto_keep_ota else code
    sketch_dir = Path(tempfile.gettempdir()) / "on-the-air-esp-webstudio"
    build_dir = Path(tempfile.gettempdir()) / "on-the-air-esp-webstudio-build"
    sketch_dir.mkdir(parents=True, exist_ok=True)
    build_dir.mkdir(parents=True, exist_ok=True)

    ino_file = sketch_dir / f"{sketch_dir.name}.ino"
    ino_file.write_text(final_code, encoding="utf-8")

    cmd = [
        cli,
        "compile",
        "--fqbn",
        fqbn,
        "--library",
        str(REPO_ROOT),
        "--build-path",
        str(build_dir),
        str(sketch_dir),
    ]
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    res = subprocess.run(cmd, capture_output=True, text=True, creationflags=flags)
    output = (res.stdout or "") + ("\n" + res.stderr if res.stderr else "")
    output = output.strip()

    if res.returncode != 0:
        return False, output or f"Compile failed (exit code {res.returncode})", None

    bin_path = build_dir / f"{sketch_dir.name}.ino.bin"
    if not bin_path.is_file():
        return False, output + f"\nError: compiled binary not found at {bin_path}", None
    return True, output, bin_path


class StudioState:
    """Shared state for auto-discovering the ESP32 host and syncing sketches from Arduino IDE."""

    def __init__(self, explicit_host: Optional[str], fqbn: str) -> None:
        self.explicit_host = explicit_host
        self.fqbn = fqbn
        self.cached_host = explicit_host or ota_flash.DEFAULT_HOST
        self.last_probe = 0.0
        self.lock = threading.Lock()
        self.pending_code: Optional[str] = None
        self.pending_name: str = ""
        self.server: Optional[http.server.ThreadingHTTPServer] = None

    def set_pending_sketch(self, code: str, name: str = "sketch.ino") -> None:
        with self.lock:
            self.pending_code = code
            self.pending_name = name

    def pop_pending_sketch(self) -> Tuple[Optional[str], str]:
        with self.lock:
            c, n = self.pending_code, self.pending_name
            self.pending_code = None
            self.pending_name = ""
            return c, n

    def get_esp_host(self, force_reprobe: bool = False) -> str:
        if self.explicit_host:
            return self.explicit_host
        now = time.time()
        if not force_reprobe and (now - self.last_probe) < 8.0:
            return self.cached_host
        found = ota_flash.discover_esp_host(None)
        self.cached_host = found
        self.last_probe = now
        return found


def make_studio_handler(state: StudioState):
    class StudioHandler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format, *args):  # noqa: A002
            return

        def _send_cors(self) -> None:
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header(
                "Access-Control-Allow-Headers",
                "Content-Type, X-OTA-Token, X-Target-Device, X-Firmware-MD5",
            )
            self.send_header("Access-Control-Allow-Private-Network", "true")

        def _send_json(self, code: int, payload: dict) -> None:
            raw = json.dumps(payload).encode("utf-8")
            self.send_response(code)
            self._send_cors()
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def _proxy_to_esp(self, method: str) -> None:
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length) if length > 0 else None
            esp_host = state.get_esp_host()

            parsed = esp_host.split(":")
            hostname = parsed[0]
            port = int(parsed[1]) if len(parsed) > 1 else 80

            forward_headers = {}
            for h in ("Content-Type", "X-OTA-Token", "X-Target-Device", "X-Firmware-MD5"):
                val = self.headers.get(h)
                if val is not None:
                    forward_headers[h] = val

            conn = http.client.HTTPConnection(hostname, port, timeout=15)
            try:
                conn.request(method, self.path, body=body, headers=forward_headers)
                resp = conn.getresponse()
                resp_body = resp.read()
                self.send_response(resp.status)
                self._send_cors()
                ct = resp.getheader("Content-Type", "application/json")
                self.send_header("Content-Type", ct)
                self.send_header("Content-Length", str(len(resp_body)))
                self.end_headers()
                self.wfile.write(resp_body)
            except Exception as e:
                # Trigger fresh host discovery next request (in case user switched AP <-> Home Wi-Fi)
                state.last_probe = 0.0
                self._send_json(502, {"ok": False, "error": f"Cannot reach ESP32 at {esp_host}: {e}"})
            finally:
                conn.close()

        def do_OPTIONS(self):  # noqa: N802
            self.send_response(204)
            self._send_cors()
            self.end_headers()

        def do_GET(self):  # noqa: N802
            if self.path == "/" or self.path.startswith("/?"):
                html = extract_web_ui_html().encode("utf-8")
                self.send_response(200)
                self._send_cors()
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(html)))
                self.end_headers()
                self.wfile.write(html)
                return

            if self.path == "/local/health":
                self._send_json(200, {"ok": True, "studio": True, "espHost": state.get_esp_host()})
                return

            if self.path == "/local/pending-sketch":
                code, name = state.pop_pending_sketch()
                self._send_json(200, {"ok": True, "hasSketch": code is not None, "code": code or "", "name": name})
                return

            if self.path.startswith("/api/"):
                self._proxy_to_esp("GET")
                return

            self._send_json(404, {"ok": False, "error": "Not found"})

        def do_POST(self):  # noqa: N802
            if self.path == "/local/shutdown":
                self._send_json(200, {"ok": True, "message": "Stopping MTR-OTA Web IDE server."})
                if state.server:
                    threading.Thread(target=state.server.shutdown, daemon=True).start()
                return

            if self.path == "/local/open-sketch":
                length = int(self.headers.get("Content-Length", "0"))
                raw = self.rfile.read(length).decode("utf-8", errors="replace")
                req_data = json.loads(raw) if raw else {}
                code = str(req_data.get("code", ""))
                name = str(req_data.get("name", "sketch.ino"))
                if not code and req_data.get("path"):
                    p = Path(str(req_data["path"]))
                    if p.is_file():
                        code = p.read_text(encoding="utf-8", errors="replace")
                        name = p.name
                if code:
                    state.set_pending_sketch(code, name)
                    self._send_json(200, {"ok": True})
                else:
                    self._send_json(400, {"ok": False, "error": "Empty sketch"})
                return

            if self.path == "/local/compile":
                length = int(self.headers.get("Content-Length", "0"))
                raw = self.rfile.read(length).decode("utf-8", errors="replace")
                try:
                    req_data = json.loads(raw) if raw else {}
                except json.JSONDecodeError:
                    self._send_json(400, {"ok": False, "error": "Invalid JSON body"})
                    return

                code = str(req_data.get("code", ""))
                do_flash = bool(req_data.get("flash", True))
                auto_keep = bool(req_data.get("autoKeepOta", True))
                token = str(req_data.get("token") or ota_flash.read_token_from_config())

                if not code.strip():
                    self._send_json(400, {"ok": False, "error": "Code window is empty."})
                    return

                ok, build_log, bin_path = compile_code_string(code, auto_keep_ota=auto_keep, fqbn=state.fqbn)
                if not ok or bin_path is None:
                    self._send_json(400, {"ok": False, "error": "Compilation failed.", "output": build_log})
                    return

                if not do_flash:
                    self._send_json(200, {"ok": True, "message": "Verified cleanly (0 errors)! Ready to flash.", "output": build_log})
                    return

                esp_host = state.get_esp_host(force_reprobe=True)
                try:
                    info = ota_flash.inspect_binary(bin_path)
                    up_code, up_res = ota_flash.upload_firmware(
                        host=esp_host,
                        bin_path=bin_path,
                        token=token,
                        target_device="",
                        md5_hex=info["md5"],
                        show_progress=False,
                    )
                    if up_code == 200 and up_res.get("ok"):
                        self._send_json(
                            200,
                            {
                                "ok": True,
                                "message": f"Compiled & flashed to {esp_host} ({info['size'] // 1024} KB)! ESP32 is rebooting...",
                                "output": build_log
                                + f"\n\n[OTA] Flashed {bin_path.name} -> {esp_host} ({info['size']} bytes, MD5={info['md5']}).",
                            },
                        )
                    else:
                        err = up_res.get("error", f"HTTP {up_code}")
                        self._send_json(
                            400,
                            {"ok": False, "error": f"Compiled OK, but flash to {esp_host} failed: {err}", "output": build_log},
                        )
                except Exception as e:
                    self._send_json(
                        502,
                        {
                            "ok": False,
                            "error": f"Compiled OK, but could not reach ESP32 at {esp_host} ({e}). Connect to MTR-OTA or same Home Wi-Fi!",
                            "output": build_log,
                        },
                    )
                return

            if self.path.startswith("/api/"):
                self._proxy_to_esp("POST")
                return

            self._send_json(404, {"ok": False, "error": "Not found"})

    return StudioHandler


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="MTR-OTA Local Web Code Studio")
    parser.add_argument("--port", type=int, default=8080, help="Local HTTP port (default: 8080)")
    parser.add_argument("--host", default=None, help="Explicit ESP32 IP/host (auto-discovers 192.168.4.1 / MTR-OTA.local if omitted)")
    parser.add_argument("--fqbn", default=ota_flash.DEFAULT_FQBN, help=f"Board FQBN (default: {ota_flash.DEFAULT_FQBN})")
    parser.add_argument("--sketch", default=None, help="Optional .ino file to pre-load into the browser Code Window")
    parser.add_argument("--no-browser", action="store_true", help="Do not auto-open browser")
    args = parser.parse_args(argv)

    initial_code: Optional[str] = None
    initial_name = "sketch.ino"
    if args.sketch:
        sp = Path(args.sketch).expanduser()
        if sp.is_file():
            initial_code = sp.read_text(encoding="utf-8", errors="replace")
            initial_name = sp.name

    url = f"http://127.0.0.1:{args.port}"
    if is_studio_running(args.port):
        if initial_code:
            push_sketch_to_running_studio(initial_code, initial_name, port=args.port)
        print(f"MTR-OTA Web IDE is already running at {url} — opening browser tab.", flush=True)
        if not args.no_browser:
            webbrowser.open(url)
        return 0

    state = StudioState(explicit_host=args.host, fqbn=args.fqbn)
    if initial_code:
        state.set_pending_sketch(initial_code, initial_name)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), make_studio_handler(state))
    state.server = server

    print("============================================================", flush=True)
    print(f"  MTR-OTA Web Code Studio running at: {url}", flush=True)
    print(f"  Active ESP32 target: http://{state.get_esp_host()}", flush=True)
    print("  Auto-discovers 192.168.4.1 (SoftAP) and MTR-OTA.local (Home Wi-Fi).", flush=True)
    print("============================================================", flush=True)

    if not args.no_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
