"""Unit tests for tools/sumo_flash.py using a local mock Sumobot HTTP server."""

from __future__ import annotations

import http.server
import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import sumo_flash  # noqa: E402


class MockSumobotState:
    def __init__(self) -> None:
        self.robot_id = "SUMO-01"
        self.version = "1.0.0"
        self.partition = "app0"
        self.armed = False
        self.token = "secret-pin"
        self.params = {"attackSpeed": 255, "strategy": 0}
        self.last_md5 = ""
        self.uploaded_bytes = b""


def make_handler(state: MockSumobotState):
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
                        "robotId": state.robot_id,
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
                target = self.headers.get("X-Target-Robot", "")
                if target and target != state.robot_id:
                    self._send_json(400, {"ok": False, "error": "Target mismatch"})
                    return
                state.last_md5 = self.headers.get("X-Firmware-MD5", "")
                state.uploaded_bytes = body
                state.partition = "app1"
                state.version = "1.1.0"
                self._send_json(200, {"ok": True, "message": "Flashed OK"})
            elif self.path == "/api/params":
                state.params["attackSpeed"] = 240
                self._send_json(200, {"ok": True, "params": state.params})
            elif self.path == "/api/rollback":
                self._send_json(200, {"ok": True, "message": "Rolling back"})
            else:
                self._send_json(404, {"ok": False, "error": "Not found"})

    return Handler


class SumoFlashTests(unittest.TestCase):
    def setUp(self) -> None:
        self.state = MockSumobotState()
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), make_handler(self.state))
        self.port = self.server.server_address[1]
        self.host = f"127.0.0.1:{self.port}"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()

    def _write_fake_bin(self, tmpdir: str, robot_id: str = "SUMO-01", version: str = "1.1.0") -> Path:
        p = Path(tmpdir) / "on-the-air-esp.ino.bin"
        meta = f"SUMO_META:ID={robot_id};BOARD=NODEMCU-32S;VER={version};".encode("utf-8")
        # Valid ESP32 magic byte 0xE9 + padding >= 4096 bytes
        content = b"\xe9" + (b"\x00" * 4096) + meta + (b"\xff" * 256)
        p.write_bytes(content)
        return p

    def test_inspect_valid_binary(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            bin_path = self._write_fake_bin(td)
            info = sumo_flash.inspect_binary(bin_path)
            self.assertEqual(info["meta"]["robotId"], "SUMO-01")
            self.assertEqual(info["meta"]["board"], "NODEMCU-32S")
            self.assertEqual(info["meta"]["version"], "1.1.0")
            self.assertEqual(len(info["md5"]), 32)

    def test_inspect_rejects_bad_magic(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "bad.bin"
            p.write_bytes(b"\x00" * 5000)
            with self.assertRaises(ValueError):
                sumo_flash.inspect_binary(p)

    def test_flash_success_and_md5_header(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            bin_path = self._write_fake_bin(td, robot_id="SUMO-01", version="1.1.0")
            expected_md5 = sumo_flash.inspect_binary(bin_path)["md5"]
            rc = sumo_flash.main(
                ["flash", str(bin_path), "--host", self.host, "--token", "secret-pin", "--no-wait", "--quiet"]
            )
            self.assertEqual(rc, 0)
            self.assertEqual(self.state.last_md5, expected_md5)
            self.assertEqual(self.state.partition, "app1")

    def test_flash_blocks_target_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            bin_path = self._write_fake_bin(td, robot_id="SUMO-99", version="1.1.0")
            rc = sumo_flash.main(
                ["flash", str(bin_path), "--host", self.host, "--token", "secret-pin", "--no-wait", "--quiet"]
            )
            self.assertEqual(rc, 4)

    def test_flash_blocks_when_armed(self) -> None:
        self.state.armed = True
        with tempfile.TemporaryDirectory() as td:
            bin_path = self._write_fake_bin(td)
            rc = sumo_flash.main(
                ["flash", str(bin_path), "--host", self.host, "--token", "secret-pin", "--no-wait", "--quiet"]
            )
            self.assertEqual(rc, 3)

    def test_params_and_rollback(self) -> None:
        rc1 = sumo_flash.main(
            ["params", "--host", self.host, "--token", "secret-pin", "--set", "attackSpeed=240"]
        )
        self.assertEqual(rc1, 0)
        rc2 = sumo_flash.main(["rollback", "--host", self.host, "--token", "secret-pin"])
        self.assertEqual(rc2, 0)


if __name__ == "__main__":
    unittest.main()
