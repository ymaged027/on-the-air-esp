#!/usr/bin/env python3
"""
OTA Monitor — Arduino IDE 2.x Pluggable Monitor (protocol: network) & CLI Monitor
=================================================================================
Enables Arduino IDE's built-in Serial Monitor to connect WIRELESSLY to any
ESP32 running OnTheAirESP (e.g. MTR-OTA at 192.168.4.1:23)!

Modes:
  1. Arduino IDE Pluggable Monitor (default, no arguments):
     Speaks the official Arduino Pluggable Monitor Protocol v1 on stdin/stdout
     and bridges TCP port 23 on the ESP32 <-> Arduino IDE's Monitor tab.
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
from typing import Optional

DEFAULT_HOST = "192.168.4.1"
DEFAULT_TCP_PORT = 23


def _emit(payload: dict, lock: threading.Lock) -> None:
    with lock:
        sys.stdout.write(json.dumps(payload) + "\n")
        sys.stdout.flush()


class PluggableOtaMonitor:
    def __init__(self) -> None:
        self.tcp_port = DEFAULT_TCP_PORT
        self.out_lock = threading.Lock()
        self.state_lock = threading.Lock()
        self.esp_sock: Optional[socket.socket] = None
        self.ide_sock: Optional[socket.socket] = None
        self.stop_event = threading.Event()
        self.is_open = False

    def _close_sockets(self, notify_closed: bool = False) -> None:
        with self.state_lock:
            was_open = self.is_open
            self.is_open = False
            self.stop_event.set()
            for s in (self.esp_sock, self.ide_sock):
                if s is not None:
                    try:
                        s.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                    try:
                        s.close()
                    except OSError:
                        pass
            self.esp_sock = None
            self.ide_sock = None

        if notify_closed and was_open:
            _emit({"eventType": "port_closed", "message": "OTA Monitor disconnected"}, self.out_lock)

    def _pipe(self, src: socket.socket, dst: socket.socket) -> None:
        try:
            while not self.stop_event.is_set():
                data = src.recv(4096)
                if not data:
                    break
                dst.sendall(data)
        except OSError:
            pass
        finally:
            self._close_sockets(notify_closed=True)

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

            self._close_sockets(notify_closed=False)

            try:
                esp_sock = socket.create_connection((esp_host, esp_port), timeout=5)
                esp_sock.settimeout(None)
                esp_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            except OSError as e:
                _emit(
                    {
                        "eventType": "open",
                        "message": f"Cannot connect to OTA Monitor at {esp_host}:{esp_port} ({e})",
                        "error": True,
                    },
                    self.out_lock,
                )
                return True

            try:
                ide_sock = socket.create_connection((ide_host, ide_port), timeout=5)
                ide_sock.settimeout(None)
                ide_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            except OSError as e:
                esp_sock.close()
                _emit(
                    {"eventType": "open", "message": f"Cannot connect to IDE socket ({e})", "error": True},
                    self.out_lock,
                )
                return True

            with self.state_lock:
                self.esp_sock = esp_sock
                self.ide_sock = ide_sock
                self.stop_event.clear()
                self.is_open = True

            threading.Thread(target=self._pipe, args=(esp_sock, ide_sock), daemon=True).start()
            threading.Thread(target=self._pipe, args=(ide_sock, esp_sock), daemon=True).start()
            _emit({"eventType": "open", "message": "OK"}, self.out_lock)

        elif cmd == "CLOSE":
            if not self.is_open:
                _emit({"eventType": "close", "message": "port already closed", "error": True}, self.out_lock)
            else:
                self._close_sockets(notify_closed=False)
                _emit({"eventType": "close", "message": "OK"}, self.out_lock)

        elif cmd == "QUIT":
            self._close_sockets(notify_closed=False)
            _emit({"eventType": "quit", "message": "OK"}, self.out_lock)
            return False

        else:
            _emit({"eventType": "command_error", "message": f"Unknown command: {cmd}", "error": True}, self.out_lock)

        return True

    def run(self) -> int:
        for line in sys.stdin:
            if not self.handle_command(line):
                break
        self._close_sockets(notify_closed=False)
        return 0


def run_cli_monitor(host: str, port: int) -> int:
    """Interactive standalone terminal OTA Monitor."""
    print(f"Connecting to OTA Monitor at {host}:{port} ... (Ctrl+C to exit)")
    try:
        sock = socket.create_connection((host, port), timeout=6)
    except OSError as e:
        print(f"Error: Could not connect to {host}:{port} ({e})", file=sys.stderr)
        print("Make sure your ESP32 is in Maintenance/Debug Mode and Wi-Fi is connected to MTR-OTA.", file=sys.stderr)
        return 1

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

    t = threading.Thread(target=reader, daemon=True)
    t.start()

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
