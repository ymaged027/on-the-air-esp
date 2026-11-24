"""Unit tests for tools/ota_flash.py using a local mock ESP32 OTA HTTP server."""

from __future__ import annotations

import http.server
import io
import json
import socket
import sys
import tempfile
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import ota_flash  # noqa: E402
import ota_monitor  # noqa: E402
import ota_studio  # noqa: E402


class MockDeviceState:
    def __init__(self) -> None:
        self.device_id = "MTR-OTA"
        self.version = "1.0.0"
        self.partition = "app0"
        self.armed = False
        self.token = "mtr-ota007"
        self.params = {"speedMax": 255, "mode": 0}
        self.last_md5 = ""
        self.uploaded_bytes = b""


def make_handler(state: MockDeviceState):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format, *args):  # noqa: A002
            return

        def _send_json(self, code: int, payload: dict) -> None:
            raw = json.dumps(payload).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def do_GET(self):  # noqa: N802
            if self.path == "/api/status":
                self._send_json(
                    200,
                    {
                        "ok": True,
                        "deviceId": state.device_id,
                        "board": "NODEMCU-32S",
                        "version": state.version,
                        "partition": state.partition,
                        "otaState": "VALID",
                        "armed": state.armed,
                        "batteryV": 7.9,
                        "freeHeap": 220000,
                        "uptimeSec": 12,
                    },
                )
            elif self.path == "/api/params":
                self._send_json(200, state.params)
            else:
                self._send_json(404, {"ok": False, "error": "Not found"})

        def do_POST(self):  # noqa: N802
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length)
            if self.headers.get("X-OTA-Token") != state.token:
                self._send_json(401, {"ok": False, "error": "Invalid OTA token"})
                return

            if self.path == "/api/ota":
                target = self.headers.get("X-Target-Device", "")
                if target and target != state.device_id:
                    self._send_json(400, {"ok": False, "error": "Target mismatch"})
                    return
                state.last_md5 = self.headers.get("X-Firmware-MD5", "")
                state.uploaded_bytes = body
                state.partition = "app1"
                state.version = "1.1.0"
                self._send_json(200, {"ok": True, "message": "Flashed OK"})
            elif self.path == "/api/params":
                state.params["speedMax"] = 240
                self._send_json(200, {"ok": True, "params": state.params})
            elif self.path == "/api/rollback":
                self._send_json(200, {"ok": True, "message": "Rolling back"})
            else:
                self._send_json(404, {"ok": False, "error": "Not found"})

    return Handler


class OtaFlashTests(unittest.TestCase):
    def setUp(self) -> None:
        self.state = MockDeviceState()
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), make_handler(self.state))
        self.port = self.server.server_address[1]
        self.host = f"127.0.0.1:{self.port}"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()

    def _write_fake_bin(self, tmpdir: str, device_id: str = "MTR-OTA", version: str = "1.1.0") -> Path:
        p = Path(tmpdir) / "on-the-air-esp.ino.bin"
        meta = f"OTA_META:ID={device_id};BOARD=NODEMCU-32S;VER={version};".encode("utf-8")
        content = b"\xe9" + (b"\x00" * 4096) + meta + (b"\xff" * 256)
        p.write_bytes(content)
        return p

    def test_inspect_valid_binary(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            bin_path = self._write_fake_bin(td)
            info = ota_flash.inspect_binary(bin_path)
            self.assertEqual(info["meta"]["deviceId"], "MTR-OTA")
            self.assertEqual(info["meta"]["board"], "NODEMCU-32S")
            self.assertEqual(info["meta"]["version"], "1.1.0")
            self.assertEqual(len(info["md5"]), 32)

    def test_inspect_rejects_bad_magic(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "bad.bin"
            p.write_bytes(b"\x00" * 5000)
            with self.assertRaises(ValueError):
                ota_flash.inspect_binary(p)

    def test_flash_success_and_md5_header(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            bin_path = self._write_fake_bin(td, device_id="MTR-OTA", version="1.1.0")
            expected_md5 = ota_flash.inspect_binary(bin_path)["md5"]
            rc = ota_flash.main(
                ["flash", str(bin_path), "--host", self.host, "--token", "mtr-ota007", "--no-wait", "--quiet"]
            )
            self.assertEqual(rc, 0)
            self.assertEqual(self.state.last_md5, expected_md5)
            self.assertEqual(self.state.partition, "app1")

    def test_flash_blocks_target_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            bin_path = self._write_fake_bin(td, device_id="OTHER-BOT", version="1.1.0")
            rc = ota_flash.main(
                ["flash", str(bin_path), "--host", self.host, "--token", "mtr-ota007", "--no-wait", "--quiet"]
            )
            self.assertEqual(rc, 4)

    def test_flash_blocks_when_armed(self) -> None:
        self.state.armed = True
        with tempfile.TemporaryDirectory() as td:
            bin_path = self._write_fake_bin(td)
            rc = ota_flash.main(
                ["flash", str(bin_path), "--host", self.host, "--token", "mtr-ota007", "--no-wait", "--quiet"]
            )
            self.assertEqual(rc, 3)

    def test_params_and_rollback(self) -> None:
        rc1 = ota_flash.main(
            ["params", "--host", self.host, "--token", "mtr-ota007", "--set", "speedMax=240"]
        )
        self.assertEqual(rc1, 0)
        rc2 = ota_flash.main(["rollback", "--host", self.host, "--token", "mtr-ota007"])
        self.assertEqual(rc2, 0)

    def test_read_token_from_config(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            cfg = Path(td) / "config.h"
            cfg.write_text('#define OTA_AUTH_TOKEN "custom-pin"\n', encoding="utf-8")
            self.assertEqual(ota_flash.read_token_from_config(cfg), "custom-pin")
        self.assertEqual(ota_flash.read_token_from_config(), "mtr-ota007")

    def test_install_library_and_resolve_sketch(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            installed = ota_flash.install_arduino_library(target_libraries_dir=Path(td))
            self.assertTrue((installed / "library.properties").is_file())
            self.assertTrue((installed / "src" / "OnTheAirESP.h").is_file())
            self.assertTrue((installed / "examples" / "BasicOTA" / "BasicOTA.ino").is_file())
            ino = installed / "examples" / "BasicOTA" / "BasicOTA.ino"
            self.assertEqual(ota_flash.resolve_sketch_dir(ino), ino.parent)

    def test_install_ide_monitor_writes_platform_local(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            hw = Path(td)
            (hw / "platform.txt").write_text("name=esp32\n", encoding="utf-8")
            updated = ota_flash.install_ide_monitor(hardware_dirs=[hw])
            self.assertEqual(len(updated), 1)
            text = (hw / "platform.local.txt").read_text(encoding="utf-8")
            self.assertIn("pluggable_monitor.pattern.network=", text)
            self.assertIn("ota_monitor.py", text)

    def test_pluggable_ota_monitor_bridge(self) -> None:
        # Start mock ESP32 OTA Monitor server (port 23 equivalent) and mock IDE TCP listener
        esp_srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        esp_srv.bind(("127.0.0.1", 0))
        esp_srv.listen(1)
        esp_port = esp_srv.getsockname()[1]

        ide_srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        ide_srv.bind(("127.0.0.1", 0))
        ide_srv.listen(1)
        ide_port = ide_srv.getsockname()[1]

        esp_received = bytearray()

        def esp_worker() -> None:
            conn, _ = esp_srv.accept()
            conn.sendall(b"HELLO FROM ESP32\n")
            esp_received.extend(conn.recv(64))
            conn.close()

        t_esp = threading.Thread(target=esp_worker, daemon=True)
        t_esp.start()

        mon = ota_monitor.PluggableOtaMonitor()
        buf = io.StringIO()
        orig_stdout = sys.stdout
        sys.stdout = buf
        try:
            mon.handle_command('HELLO 1 "arduino-cli"')
            mon.handle_command("DESCRIBE")
            mon.handle_command(f"CONFIGURE port {esp_port}")
            mon.handle_command(f"OPEN 127.0.0.1:{ide_port} 127.0.0.1")

            ide_conn, _ = ide_srv.accept()
            ide_conn.settimeout(3)
            received = b""
            while b"HELLO FROM ESP32\n" not in received:
                chunk = ide_conn.recv(256)
                if not chunk:
                    break
                received += chunk
            self.assertIn(b"HELLO FROM ESP32\n", received)
            ide_conn.sendall(b"status\n")
            t_esp.join(timeout=3)
            self.assertEqual(bytes(esp_received), b"status\n")

            mon.handle_command("CLOSE")
            mon.handle_command("QUIT")
            ide_conn.close()
        finally:
            sys.stdout = orig_stdout
            esp_srv.close()
            ide_srv.close()

        events = [json.loads(line) for line in buf.getvalue().splitlines() if line.strip()]
        types = [e["eventType"] for e in events]
        self.assertIn("hello", types)
        self.assertIn("describe", types)
        self.assertIn("open", types)
        self.assertIn("quit", types)

    def test_pluggable_ota_monitor_instant_open_when_offline(self) -> None:
        # Even when ESP32 is unreachable, OPEN must reply OK immediately so Arduino IDE never times out
        ide_srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        ide_srv.bind(("127.0.0.1", 0))
        ide_srv.listen(1)
        ide_port = ide_srv.getsockname()[1]

        mon = ota_monitor.PluggableOtaMonitor()
        buf = io.StringIO()
        orig_stdout = sys.stdout
        sys.stdout = buf
        try:
            mon.handle_command('HELLO 1 "arduino-cli"')
            mon.handle_command("CONFIGURE port 1")  # closed local port
            mon.handle_command(f"OPEN 127.0.0.1:{ide_port} 127.0.0.1")
            ide_conn, _ = ide_srv.accept()
            ide_conn.settimeout(3)
            msg = ide_conn.recv(512)
            self.assertIn(b"OTA Monitor", msg)
            mon.handle_command("CLOSE")
            mon.handle_command("QUIT")
            ide_conn.close()
        finally:
            sys.stdout = orig_stdout
            ide_srv.close()

        events = [json.loads(line) for line in buf.getvalue().splitlines() if line.strip()]
        open_ev = next(e for e in events if e["eventType"] == "open")
        self.assertEqual(open_ev["message"], "OK")

    def test_studio_extract_html_and_auto_inject_ota(self) -> None:
        html = ota_studio.extract_web_ui_html()
        self.assertIn("Code Window", html)
        self.assertIn("OTA Monitor", html)
        self.assertIn("Live GPIO", html)
        self.assertIn("Home / Lab Wi-Fi", html)
        self.assertIn('accept=".bin,.ino"', html)

        plain = "void setup() {\n  pinMode(2, OUTPUT);\n}\nvoid loop() {}\n"
        injected = ota_studio.ensure_ota_included(plain)
        self.assertIn("#include <OnTheAirESP.h>", injected)
        self.assertIn("OnTheAirESP.begin();", injected)

        already = "#include <OnTheAirESP.h>\nvoid setup() { OnTheAirESP.begin(); }\nvoid loop() {}\n"
        self.assertEqual(ota_studio.ensure_ota_included(already), already)

        state = ota_studio.StudioState(explicit_host="192.168.4.1", fqbn=ota_flash.DEFAULT_FQBN)
        state.set_pending_sketch("void setup(){}", "my_bot.ino")
        code, name = state.pop_pending_sketch()
        self.assertEqual(code, "void setup(){}")
        self.assertEqual(name, "my_bot.ino")
        self.assertIsNone(state.pop_pending_sketch()[0])

        with tempfile.TemporaryDirectory() as td:
            vsix = ota_flash.install_ide_plugin(plugins_dir=Path(td))
            self.assertTrue(vsix.is_file())
            self.assertEqual(vsix.name, "mtr-ota-web-ide-2.0.0.vsix")
            import zipfile
            with zipfile.ZipFile(vsix) as zf:
                names = set(zf.namelist())
                self.assertIn("[Content_Types].xml", names)
                self.assertIn("extension.vsixmanifest", names)
                self.assertIn("extension/package.json", names)
                self.assertIn("extension/extension.js", names)
                self.assertIn("extension/icon-flash-dark.svg", names)
                pkg = json.loads(zf.read("extension/package.json").decode("utf-8"))
                self.assertTrue(Path(pkg["contributes"]["configuration"]["properties"]["mtrOta.repoPath"]["default"]).is_dir())


if __name__ == "__main__":
    unittest.main()
