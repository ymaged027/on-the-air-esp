#!/usr/bin/env python3
"""
MTR-OTA Local Web Code Studio (Zero-Dependency Python 3)
========================================================
Runs a tiny local bridge at http://127.0.0.1:8080 so you can paste or write
Arduino C++ code directly inside the Web Portal and compile + wirelessly flash
it to your ESP32 NodeMCU-32S in 1 click!

Works in BOTH places:
  1. Open http://127.0.0.1:8080 directly (double-click web-ide.bat)
  2. Or open http://192.168.4.1 (auto-detects this studio via CORS!)
"""

from __future__ import annotations

import argparse
import http.client
import http.server
import json
import re
import subprocess
import sys
import tempfile
import threading
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


def extract_web_ui_html(header_path: Path = WEB_UI_HEADER) -> str:
    """Extract the HTML document directly from src/web_ui.h (single source of truth)."""
    text = header_path.read_text(encoding="utf-8")
    m = RAW_LITERAL_RE.search(text)
    if not m:
        raise ValueError(f"Could not find WEB_UI_HTML rawliteral in {header_path}")
    return m.group(1)


def ensure_ota_included(code: str) -> str:
    """
    Safety guard: if a pasted sketch forgot #include <OnTheAirESP.h>,
    automatically inject it + OnTheAirESP.begin() so wireless OTA is never lost!
    """
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
    """Write code to a persistent temp sketch folder and compile with Arduino IDE's arduino-cli."""
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
    res = subprocess.run(cmd, capture_output=True, text=True)
    output = (res.stdout or "") + ("\n" + res.stderr if res.stderr else "")
    output = output.strip()

    if res.returncode != 0:
        return False, output or f"Compile failed (exit code {res.returncode})", None

    bin_path = build_dir / f"{sketch_dir.name}.ino.bin"
    if not bin_path.is_file():
        return False, output + f"\nError: compiled binary not found at {bin_path}", None
    return True, output, bin_path


def make_studio_handler(esp_host: str, fqbn: str):
    class StudioHandler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format, *args):  # noqa: A002
            return

        def _send_cors(self) -> None:
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type, X-OTA-Token, X-Target-Device, X-Firmware-MD5")
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

            parsed = esp_host.split(":")
            hostname = parsed[0]
            port = int(parsed[1]) if len(parsed) > 1 else 80

            forward_headers = {}
            for h in ("Content-Type", "X-OTA-Token", "X-Target-Device", "X-Firmware-MD5"):
                val = self.headers.get(h)
                if val is not None:
                    forward_headers[h] = val

            conn = http.client.HTTPConnection(hostname, port, timeout=30)
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
                self._send_json(200, {"ok": True, "studio": True, "espHost": esp_host})
                return

            if self.path.startswith("/api/"):
                self._proxy_to_esp("GET")
                return

            self._send_json(404, {"ok": False, "error": "Not found"})

        def do_POST(self):  # noqa: N802
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

                ok, build_log, bin_path = compile_code_string(code, auto_keep_ota=auto_keep, fqbn=fqbn)
                if not ok or bin_path is None:
                    self._send_json(400, {"ok": False, "error": "Compilation failed.", "output": build_log})
                    return

                if not do_flash:
                    self._send_json(200, {"ok": True, "message": "Compiled cleanly! Ready to flash.", "output": build_log})
                    return

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
                                "message": f"Compiled & flashed ({info['size'] // 1024} KB)! ESP32 is rebooting...",
                                "output": build_log + f"\n\n[OTA] Flashed {bin_path.name} ({info['size']} bytes, MD5={info['md5']}).",
                            },
                        )
                    else:
                        err = up_res.get("error", f"HTTP {up_code}")
                        self._send_json(
                            400,
                            {"ok": False, "error": f"Compiled OK, but flash failed: {err}", "output": build_log},
                        )
                except Exception as e:
                    self._send_json(
                        502,
                        {
                            "ok": False,
                            "error": f"Compiled OK, but could not reach ESP32 at {esp_host} ({e}). Connect Wi-Fi to MTR-OTA!",
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
    parser.add_argument("--port", type=int, default=8080, help="Local studio HTTP port (default: 8080)")
    parser.add_argument("--host", default=ota_flash.DEFAULT_HOST, help=f"ESP32 IP (default: {ota_flash.DEFAULT_HOST})")
    parser.add_argument("--fqbn", default=ota_flash.DEFAULT_FQBN, help=f"Board FQBN (default: {ota_flash.DEFAULT_FQBN})")
    parser.add_argument("--no-browser", action="store_true", help="Do not auto-open browser")
    args = parser.parse_args(argv)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), make_studio_handler(args.host, args.fqbn))
    url = f"http://127.0.0.1:{args.port}"
    print("============================================================")
    print(f"  MTR-OTA Web Code Studio running at: {url}")
    print(f"  Target ESP32: http://{args.host}")
    print("  Keep this window open while flashing code from the browser.")
    print("  Press Ctrl+C to stop.")
    print("============================================================")

    if not args.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
