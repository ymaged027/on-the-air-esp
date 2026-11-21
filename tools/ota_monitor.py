#!/usr/bin/env python3
"""
OTA Monitor — Arduino IDE 2.x Pluggable Monitor (protocol: network) & CLI Monitor
=================================================================================
Enables Arduino IDE's built-in Serial Monitor to connect WIRELESSLY to any
ESP32 running OnTheAirESP (e.g. MTR-OTA at 192.168.4.1)!

Features:
  • Instant non-blocking OPEN (<20 ms) — never hits Arduino IDE's 1.5s startup timeout.
  • Dual transport: tries low-latency TCP port 23 first, automatically falls back
    to HTTP port 80 (/api/logs & /api/logs/send) if port 23 is blocked.
  • Seamless auto-reconnect when the ESP32 reboots after wireless OTA upload!

Modes:
  1. Arduino IDE Pluggable Monitor (default, no arguments):
     Speaks the official Arduino Pluggable Monitor Protocol v1 on stdin/stdout.
  2. Standalone Terminal OTA Monitor:
     python tools/ota_monitor.py --connect 192.168.4.1 [--port 23]
"""

from __future__ import annotations

import argparse
import json
import shlex
import socket
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Optional, Tuple

DEFAULT_HOST = "192.168.4.1"
DEFAULT_TCP_PORT = 23


def _emit(payload: dict, lock: threading.Lock) -> None:
    with lock:
        sys.stdout.write(json.dumps(payload) + "\n")
        sys.stdout.flush()


def _http_get_json(url: str, timeout: float = 1.5) -> Tuple[int, dict]:
    req = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            return resp.status, json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        return e.code, {}


def _http_send_cmd(host: str, cmd: str, timeout: float = 2.0) -> None:
    body = urllib.parse.urlencode({"cmd": cmd}).encode("utf-8")
    req = urllib.request.Request(
        f"http://{host}/api/logs/send",
        data=body,
        method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout):
            pass
    except Exception:
        pass


class PluggableOtaMonitor:
    def __init__(self) -> None:
        self.tcp_port = DEFAULT_TCP_PORT
        self.out_lock = threading.Lock()
        self.state_lock = threading.Lock()
        self.ide_sock: Optional[socket.socket] = None
        self.esp_sock: Optional[socket.socket] = None
        self.stop_event = threading.Event()
        self.is_open = False
        self.ide_tx_lock = threading.Lock()
        self.http_mode = False
        self.http_host = ""

    def _send_to_ide(self, data: bytes) -> bool:
        with self.ide_tx_lock:
            sock = self.ide_sock
            if sock is None or self.stop_event.is_set():
                return False
            try:
                sock.sendall(data)
                return True
            except OSError:
                return False

    def _send_status_line(self, text: str) -> None:
        self._send_to_ide((text + "\r\n").encode("utf-8", errors="replace"))

    def _close_esp(self) -> None:
        with self.state_lock:
            s = self.esp_sock
            self.esp_sock = None
        if s is not None:
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                s.close()
            except OSError:
                pass

    def _close_all(self, notify_closed: bool = False) -> None:
        with self.state_lock:
            was_open = self.is_open
            self.is_open = False
            self.http_mode = False
            self.stop_event.set()
            ide = self.ide_sock
            esp = self.esp_sock
            self.ide_sock = None
            self.esp_sock = None

        for s in (esp, ide):
            if s is not None:
                try:
                    s.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    s.close()
                except OSError:
                    pass

        if notify_closed and was_open:
            _emit({"eventType": "port_closed", "message": "OTA Monitor closed"}, self.out_lock)

    def _ide_reader_loop(self, ide_sock: socket.socket) -> None:
        """Read input typed by the user in Arduino IDE's Monitor and forward to ESP32."""
        line_buf = bytearray()
        try:
            while not self.stop_event.is_set():
                data = ide_sock.recv(4096)
                if not data:
                    break
                with self.state_lock:
                    esp = self.esp_sock
                    use_http = self.http_mode
                    http_host = self.http_host

                if esp is not None:
                    try:
                        esp.sendall(data)
                    except OSError:
                        pass
                elif use_http and http_host:
                    line_buf.extend(data)
                    while b"\n" in line_buf:
                        raw_line, _, rest = line_buf.partition(b"\n")
                        line_buf = bytearray(rest)
                        cmd = raw_line.decode("utf-8", errors="replace").strip()
                        if cmd:
                            _http_send_cmd(http_host, cmd)
        except OSError:
            pass
        finally:
            self._close_all(notify_closed=True)

    def _run_http_session(self, esp_host: str, initial_cursor: int, initial_data: str) -> None:
        """Stream live logs via HTTP port 80 (/api/logs) when TCP port 23 is unavailable."""
        with self.state_lock:
            self.http_mode = True
            self.http_host = esp_host

        self._send_status_line(f"--- Connected to OTA Monitor at {esp_host} (HTTP) ---")
        if initial_data:
            self._send_to_ide(initial_data.replace("\n", "\r\n").encode("utf-8", errors="replace"))

        cursor = initial_cursor
        failures = 0
        try:
            while not self.stop_event.is_set():
                time.sleep(0.5)
                if self.stop_event.is_set():
                    break
                try:
                    code, payload = _http_get_json(f"http://{esp_host}/api/logs?since={cursor}", timeout=2.0)
                    if code == 200 and payload.get("ok"):
                        failures = 0
                        next_cur = int(payload.get("next", cursor))
                        chunk = payload.get("data", "")
                        if next_cur < cursor:
                            self._send_status_line("\n--- Device rebooted ---")
                            if not chunk:
                                cursor = 0
                                continue
                        if chunk:
                            self._send_to_ide(chunk.replace("\n", "\r\n").encode("utf-8", errors="replace"))
                        cursor = next_cur
                    else:
                        failures += 1
                except Exception:
                    failures += 1
                if failures >= 4:
                    break
        finally:
            with self.state_lock:
                self.http_mode = False

    def _esp_session_manager(self, esp_host: str, esp_port: int) -> None:
        """Connect to ESP32 via TCP port 23 (or HTTP port 80 fallback) and auto-reconnect."""
        announced_waiting = False
        announced_old_fw = False
        self._send_status_line(f"[OTA Monitor] Connecting to {esp_host}:{esp_port} ...")

        while not self.stop_event.is_set():
            # 1. Try direct low-latency TCP OTA Monitor (port 23)
            try:
                sock = socket.create_connection((esp_host, esp_port), timeout=1.0)
                sock.settimeout(1.0)
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                with self.state_lock:
                    self.esp_sock = sock
                    self.http_mode = False
                announced_waiting = False
                announced_old_fw = False

                while not self.stop_event.is_set():
                    try:
                        data = sock.recv(4096)
                        if not data:
                            break
                        if not self._send_to_ide(data):
                            return
                    except socket.timeout:
                        continue
                    except OSError:
                        break

                self._close_esp()
                if not self.stop_event.is_set():
                    self._send_status_line("[OTA Monitor] Connection lost — reconnecting...")
                    time.sleep(1.0)
                continue
            except OSError:
                pass

            if self.stop_event.is_set():
                break

            # 2. Automatic HTTP port 80 fallback (never blocked by Telnet/port-23 firewalls)
            try:
                code, payload = _http_get_json(f"http://{esp_host}/api/logs?since=0", timeout=1.0)
                if code == 200 and payload.get("ok"):
                    announced_waiting = False
                    announced_old_fw = False
                    self._run_http_session(
                        esp_host,
                        int(payload.get("next", 0)),
                        str(payload.get("data", "")),
                    )
                    if not self.stop_event.is_set():
                        self._send_status_line("[OTA Monitor] Connection lost — reconnecting...")
                        time.sleep(1.0)
                    continue
                if code == 404 and not announced_old_fw:
                    announced_old_fw = True
                    announced_waiting = True
                    self._send_status_line(
                        f"[OTA Monitor] Reached {esp_host}, but ESP32 is running older firmware without OTA Monitor."
                    )
                    self._send_status_line(
                        "[OTA Monitor] Click Upload (->) once with #include <OnTheAirESP.h> to enable OTA Monitor!"
                    )
            except Exception:
                pass

            if not announced_waiting and not self.stop_event.is_set():
                announced_waiting = True
                self._send_status_line(
                    f"[OTA Monitor] Waiting for MTR-OTA at {esp_host}:{esp_port} ..."
                )
                self._send_status_line(
                    "[OTA Monitor] Connect Wi-Fi to MTR-OTA (pass: mtr-ota007) and enter Maintenance Mode."
                )

            for _ in range(15):
                if self.stop_event.is_set():
                    break
                time.sleep(0.1)

    def handle_command(self, line: str) -> bool:
        line = line.strip()
        if not line:
            return True
        try:
            parts = shlex.split(line)
        except ValueError:
            parts = line.split()

        cmd = parts[0].upper()

        if cmd == "HELLO":
            _emit({"eventType": "hello", "message": "OK", "protocolVersion": 1}, self.out_lock)

        elif cmd == "DESCRIBE":
            _emit(
                {
                    "eventType": "describe",
                    "message": "OK",
                    "port_description": {
                        "protocol": "network",
                        "configuration_parameters": {
                            "port": {
                                "label": "OTA Monitor Port",
                                "type": "enum",
                                "value": ["23", "2323"],
                                "selected": str(self.tcp_port),
                            }
                        },
                    },
                },
                self.out_lock,
            )

        elif cmd == "CONFIGURE":
            if len(parts) >= 3 and parts[1] == "port":
                try:
                    self.tcp_port = int(parts[2])
                except ValueError:
                    _emit({"eventType": "configure", "message": "Invalid port", "error": True}, self.out_lock)
                    return True
            _emit({"eventType": "configure", "message": "OK"}, self.out_lock)

        elif cmd == "OPEN":
            if len(parts) < 3:
                _emit({"eventType": "open", "message": "Invalid OPEN syntax", "error": True}, self.out_lock)
                return True

            ide_addr = parts[1]
            esp_addr = parts[2]
            if ":" not in ide_addr:
                _emit({"eventType": "open", "message": "Invalid IDE TCP address", "error": True}, self.out_lock)
                return True

            ide_host, ide_port_str = ide_addr.rsplit(":", 1)
            ide_port = int(ide_port_str)

            esp_host = esp_addr
            esp_port = self.tcp_port
            if ":" in esp_addr:
                esp_host, p_str = esp_addr.rsplit(":", 1)
                if p_str.isdigit():
                    esp_port = int(p_str)

            self._close_all(notify_closed=False)

            # Connect to Arduino IDE's localhost TCP socket FIRST and reply OK immediately (<15 ms)
            # so Arduino IDE NEVER hits its internal 1.5s startup timeout!
            try:
                ide_sock = socket.create_connection((ide_host, ide_port), timeout=2.0)
                ide_sock.settimeout(None)
                ide_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            except OSError as e:
                _emit(
                    {"eventType": "open", "message": f"Cannot connect to IDE socket ({e})", "error": True},
                    self.out_lock,
                )
                return True

            with self.state_lock:
                self.ide_sock = ide_sock
                self.stop_event.clear()
                self.is_open = True

            _emit({"eventType": "open", "message": "OK"}, self.out_lock)

            threading.Thread(target=self._ide_reader_loop, args=(ide_sock,), daemon=True).start()
            threading.Thread(
                target=self._esp_session_manager, args=(esp_host, esp_port), daemon=True
            ).start()

        elif cmd == "CLOSE":
            if not self.is_open:
                _emit({"eventType": "close", "message": "port already closed", "error": True}, self.out_lock)
            else:
                self._close_all(notify_closed=False)
                _emit({"eventType": "close", "message": "OK"}, self.out_lock)

        elif cmd == "QUIT":
            self._close_all(notify_closed=False)
            _emit({"eventType": "quit", "message": "OK"}, self.out_lock)
            return False

        else:
            _emit({"eventType": "command_error", "message": f"Unknown command: {cmd}", "error": True}, self.out_lock)

        return True

    def run(self) -> int:
        while True:
            line = sys.stdin.readline()
            if not line:
                break
            if not self.handle_command(line):
                break
        self._close_all(notify_closed=False)
        return 0


def run_cli_monitor(host: str, port: int) -> int:
    """Interactive standalone terminal OTA Monitor (TCP port 23 with HTTP port 80 fallback)."""
    print(f"Connecting to OTA Monitor at {host}:{port} ... (Ctrl+C to exit)")
    sock = None
    try:
        sock = socket.create_connection((host, port), timeout=3)
    except OSError:
        sock = None

    if sock is not None:
        sock.settimeout(None)
        stop = threading.Event()

        def reader() -> None:
            try:
                while not stop.is_set():
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    sys.stdout.write(chunk.decode("utf-8", errors="replace"))
                    sys.stdout.flush()
            except OSError:
                pass
            finally:
                stop.set()

        threading.Thread(target=reader, daemon=True).start()
        try:
            while not stop.is_set():
                line = sys.stdin.readline()
                if not line:
                    break
                sock.sendall(line.encode("utf-8"))
        except (KeyboardInterrupt, OSError):
            pass
        finally:
            stop.set()
            try:
                sock.close()
            except OSError:
                pass
        print("\nDisconnected from OTA Monitor.")
        return 0

    # Fallback to HTTP port 80
    try:
        code, payload = _http_get_json(f"http://{host}/api/logs?since=0", timeout=3.0)
        if code != 200 or not payload.get("ok"):
            raise OSError(f"HTTP {code}")
    except Exception as e:
        print(f"Error: Could not reach OTA Monitor at {host}:{port} or http://{host}/api/logs ({e})", file=sys.stderr)
        print("Make sure Wi-Fi is connected to MTR-OTA (pass: mtr-ota007) and ESP32 is in Maintenance Mode.", file=sys.stderr)
        return 1

    print(f"--- Connected to OTA Monitor at {host} (HTTP fallback) ---")
    if payload.get("data"):
        sys.stdout.write(payload["data"])
        sys.stdout.flush()
    cursor = int(payload.get("next", 0))
    stop = threading.Event()

    def poller() -> None:
        nonlocal cursor
        while not stop.is_set():
            time.sleep(0.5)
            try:
                c, p = _http_get_json(f"http://{host}/api/logs?since={cursor}", timeout=2.0)
                if c == 200 and p.get("ok"):
                    nxt = int(p.get("next", cursor))
                    chunk = p.get("data", "")
                    if nxt < cursor:
                        sys.stdout.write("\n--- Device rebooted ---\n")
                        sys.stdout.flush()
                        if not chunk:
                            cursor = 0
                            continue
                    if chunk:
                        sys.stdout.write(chunk)
                        sys.stdout.flush()
                    cursor = nxt
            except Exception:
                pass

    threading.Thread(target=poller, daemon=True).start()
    try:
        while not stop.is_set():
            line = sys.stdin.readline()
            if not line:
                break
            cmd = line.strip()
            if cmd:
                _http_send_cmd(host, cmd)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
    print("\nDisconnected from OTA Monitor.")
    return 0


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="OTA Monitor (Arduino IDE Pluggable Monitor & CLI)")
    parser.add_argument("--connect", metavar="HOST", help="Run as standalone terminal OTA Monitor")
    parser.add_argument("--port", type=int, default=DEFAULT_TCP_PORT, help=f"TCP port (default: {DEFAULT_TCP_PORT})")
    args = parser.parse_args(argv)

    if args.connect:
        return run_cli_monitor(args.connect, args.port)
    return PluggableOtaMonitor().run()


if __name__ == "__main__":
    raise SystemExit(main())
