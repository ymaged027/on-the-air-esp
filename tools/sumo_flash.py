#!/usr/bin/env python3
"""
On The Air ESP — Zero-Dependency CLI & One-Command Builder/Flasher
==================================================================
Compile & flash your ESP32 NodeMCU-32S Sumobot over Wi-Fi in ONE command,
tune live NVS parameters, or roll back instantly.

Uses Python 3 standard library only — no pip packages required (0 EGP).

Easiest command (compiles sketch + flashes wirelessly automatically):
  python tools/sumo_flash.py push

Other commands:
  python tools/sumo_flash.py status
  python tools/sumo_flash.py params --set attackSpeed=245 strategy=1
  python tools/sumo_flash.py rollback
  python tools/sumo_flash.py flash path/to/on-the-air-esp.ino.bin
  python tools/sumo_flash.py inspect path/to/on-the-air-esp.ino.bin
"""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parents[1]
SKETCH_DIR = REPO_ROOT / "firmware" / "on-the-air-esp"
CONFIG_HEADER = SKETCH_DIR / "config.h"
DEFAULT_FQBN = "esp32:esp32:nodemcu-32s"

META_RE = re.compile(rb"SUMO_META:ID=([^;]+);BOARD=([^;]+);VER=([^;]+);")
TOKEN_DEFINE_RE = re.compile(r'^\s*#define\s+OTA_AUTH_TOKEN\s+"([^"]*)"', re.MULTILINE)
DEFAULT_HOST = "192.168.4.1"
DEFAULT_TIMEOUT = 15


def read_token_from_config(config_path: Path = CONFIG_HEADER) -> str:
    """Auto-read OTA_AUTH_TOKEN from firmware/on-the-air-esp/config.h so users don't have to retype it."""
    if not config_path.is_file():
        return ""
    text = config_path.read_text(encoding="utf-8", errors="replace")
    m = TOKEN_DEFINE_RE.search(text)
    return m.group(1) if m else ""


def find_arduino_cli() -> Optional[str]:
    """Locate arduino-cli on PATH or bundled inside Arduino IDE 2.x."""
    on_path = shutil.which("arduino-cli")
    if on_path:
        return on_path

    candidates = []
    local_app = os.environ.get("LOCALAPPDATA", "")
    if local_app:
        candidates.append(
            Path(local_app)
            / "Programs"
            / "Arduino IDE"
            / "resources"
            / "app"
            / "lib"
            / "backend"
            / "resources"
            / "arduino-cli.exe"
        )
    candidates.extend(
        [
            Path(r"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"),
            Path("/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"),
        ]
    )
    for c in candidates:
        if c.is_file():
            return str(c)
    return None


def compile_sketch(sketch_dir: Path = SKETCH_DIR, fqbn: str = DEFAULT_FQBN) -> Path:
    """Compile the Arduino sketch using Arduino IDE's bundled arduino-cli and return the .ino.bin path."""
    cli = find_arduino_cli()
    if not cli:
        raise FileNotFoundError(
            "Could not find arduino-cli. Install Arduino IDE 2.x (or add arduino-cli to PATH), "
            "or export the binary in Arduino IDE (Sketch -> Export Compiled Binary) and use 'flash'."
        )

    build_dir = Path(tempfile.gettempdir()) / "on-the-air-esp-build"
    build_dir.mkdir(parents=True, exist_ok=True)
    print(f"[0/4] Compiling {sketch_dir.name} for {fqbn} ...")
    cmd = [
        cli,
        "compile",
        "--fqbn",
        fqbn,
        "--build-path",
        str(build_dir),
        str(sketch_dir),
    ]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        raise RuntimeError(f"Arduino compile failed (exit code {res.returncode}). Fix code errors and retry.")

    bin_path = build_dir / f"{sketch_dir.name}.ino.bin"
    if not bin_path.is_file():
        raise FileNotFoundError(f"Expected compiled binary not found: {bin_path}")
    return bin_path


def inspect_binary(bin_path: Path) -> Dict[str, Any]:
    """Read a compiled ESP32 .bin file, compute hashes, and extract embedded metadata."""
    if not bin_path.is_file():
        raise FileNotFoundError(f"Firmware binary not found: {bin_path}")

    data = bin_path.read_bytes()
    if len(data) < 4096:
        raise ValueError(f"File too small ({len(data)} bytes) to be a valid ESP32 firmware image.")

    if data[0] != 0xE9:
        raise ValueError(
            f"Invalid ESP32 image magic byte: 0x{data[0]:02X} (expected 0xE9). "
            "Pick on-the-air-esp.ino.bin (NOT .bootloader.bin or .partitions.bin)."
        )

    md5_hex = hashlib.md5(data).hexdigest()
    sha256_hex = hashlib.sha256(data).hexdigest()

    meta: Optional[Dict[str, str]] = None
    m = META_RE.search(data)
    if m:
        meta = {
            "robotId": m.group(1).decode("utf-8", errors="replace"),
            "board": m.group(2).decode("utf-8", errors="replace"),
            "version": m.group(3).decode("utf-8", errors="replace"),
        }

    return {
        "path": str(bin_path),
        "size": len(data),
        "md5": md5_hex,
        "sha256": sha256_hex,
        "meta": meta,
    }


def http_json(
    host: str,
    path: str,
    method: str = "GET",
    headers: Optional[Dict[str, str]] = None,
    body: Optional[bytes] = None,
    timeout: int = DEFAULT_TIMEOUT,
) -> Tuple[int, Dict[str, Any]]:
    url = f"http://{host}{path}"
    req = urllib.request.Request(url, data=body, method=method)
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            return resp.status, json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", errors="replace")
        try:
            parsed = json.loads(raw) if raw else {"ok": False, "error": str(e)}
        except json.JSONDecodeError:
            parsed = {"ok": False, "error": raw or str(e)}
        return e.code, parsed


def upload_firmware(
    host: str,
    bin_path: Path,
    token: str,
    target_robot: str,
    md5_hex: str,
    show_progress: bool = True,
) -> Tuple[int, Dict[str, Any]]:
    boundary = f"----SumoOTA{uuid.uuid4().hex}"
    filename = bin_path.name
    data = bin_path.read_bytes()

    preamble = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="firmware"; filename="{filename}"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode("utf-8")
    epilogue = f"\r\n--{boundary}--\r\n".encode("utf-8")
    total_len = len(preamble) + len(data) + len(epilogue)

    parsed_host = host.split(":")
    hostname = parsed_host[0]
    port = int(parsed_host[1]) if len(parsed_host) > 1 else 80

    conn = http.client.HTTPConnection(hostname, port, timeout=60)
    try:
        conn.putrequest("POST", "/api/ota")
        conn.putheader("Content-Type", f"multipart/form-data; boundary={boundary}")
        conn.putheader("Content-Length", str(total_len))
        conn.putheader("X-OTA-Token", token)
        if target_robot:
            conn.putheader("X-Target-Robot", target_robot)
        conn.putheader("X-Firmware-MD5", md5_hex)
        conn.endheaders()

        conn.send(preamble)
        sent = 0
        chunk_size = 8192
        total = len(data)
        while sent < total:
            chunk = data[sent : sent + chunk_size]
            conn.send(chunk)
            sent += len(chunk)
            if show_progress:
                pct = int((sent / total) * 100)
                bar_fill = pct // 4
                sys.stdout.write(
                    f"\r  Uploading [{'#' * bar_fill}{'-' * (25 - bar_fill)}] "
                    f"{pct:3d}% ({sent // 1024}/{total // 1024} KB)"
                )
                sys.stdout.flush()
        conn.send(epilogue)
        if show_progress:
            sys.stdout.write("\n")

        resp = conn.getresponse()
        raw = resp.read().decode("utf-8", errors="replace")
        try:
            payload = json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            payload = {"ok": False, "error": raw}
        return resp.status, payload
    finally:
        conn.close()


def wait_for_reboot(host: str, wait_sec: int = 20) -> Optional[Dict[str, Any]]:
    print(f"Waiting up to {wait_sec}s for robot to reboot into new partition...")
    time.sleep(2.0)
    deadline = time.time() + wait_sec
    while time.time() < deadline:
        try:
            code, data = http_json(host, "/api/status", timeout=3)
            if code == 200 and data.get("ok"):
                return data
        except Exception:
            pass
        time.sleep(1.0)
    return None


def resolve_token(cli_token: Optional[str]) -> str:
    if cli_token is not None:
        return cli_token
    if "SUMO_OTA_TOKEN" in os.environ:
        return os.environ["SUMO_OTA_TOKEN"]
    return read_token_from_config()


def do_flash(
    bin_path: Path,
    host: str,
    token: str,
    force: bool = False,
    no_wait: bool = False,
    wait: int = 20,
    quiet: bool = False,
) -> int:
    info = inspect_binary(bin_path)
    print(f"[1/4] Binary verified: {bin_path.name} ({info['size'] // 1024} KB, MD5={info['md5']})")

    if info["meta"]:
        print(
            f"      Embedded tag: ID={info['meta']['robotId']}, "
            f"BOARD={info['meta']['board']}, VER=v{info['meta']['version']}"
        )
    elif not force:
        print("Error: Binary has no SUMO_META tag. Pass --force to flash anyway.", file=sys.stderr)
        return 2

    print(f"[2/4] Checking live robot at http://{host} ...")
    code, st = http_json(host, "/api/status")
    if code != 200 or not st.get("ok"):
        print(f"Error: Cannot reach robot (HTTP {code}): {st}", file=sys.stderr)
        return 1

    live_id = st.get("robotId", "")
    print(
        f"      Connected to {live_id} v{st.get('version')} "
        f"[slot={st.get('partition')}, state={st.get('otaState')}, armed={st.get('armed')}]"
    )

    if st.get("armed"):
        print("Error: Robot reports ARMED. Disarm motors before flashing!", file=sys.stderr)
        return 3

    if info["meta"] and info["meta"]["robotId"] != live_id and not force:
        print(
            f"Error: Target mismatch! Binary is for '{info['meta']['robotId']}' "
            f"but connected robot is '{live_id}'. Use --force to override.",
            file=sys.stderr,
        )
        return 4

    print("[3/4] Flashing over Wi-Fi...")
    up_code, up_res = upload_firmware(
        host=host,
        bin_path=bin_path,
        token=token,
        target_robot="" if force else live_id,
        md5_hex=info["md5"],
        show_progress=not quiet,
    )
    if up_code != 200 or not up_res.get("ok"):
        print(f"Error: OTA failed (HTTP {up_code}): {up_res}", file=sys.stderr)
        return 5

    print(f"      ESP32 accepted image: {up_res.get('message', 'OK')}")

    if no_wait:
        return 0

    print("[4/4] Verifying post-reboot health...")
    after = wait_for_reboot(host, wait_sec=wait)
    if not after:
        print(
            "Warning: Robot did not reconnect within timeout. "
            "(Normal if it rebooted into Match Mode with Wi-Fi OFF!)"
        )
        return 0

    print(
        f"SUCCESS! Robot is back online: {after.get('robotId')} v{after.get('version')} "
        f"[slot={after.get('partition')}, state={after.get('otaState')}]"
    )
    return 0


def cmd_push(args: argparse.Namespace) -> int:
    bin_path = compile_sketch(sketch_dir=Path(args.sketch), fqbn=args.fqbn)
    return do_flash(
        bin_path=bin_path,
        host=args.host,
        token=resolve_token(args.token),
        force=args.force,
        no_wait=args.no_wait,
        wait=args.wait,
        quiet=args.quiet,
    )


def cmd_inspect(args: argparse.Namespace) -> int:
    info = inspect_binary(Path(args.binary))
    print(f"File   : {info['path']}")
    print(f"Size   : {info['size']:,} bytes ({info['size'] / 1024:.1f} KB)")
    print(f"MD5    : {info['md5']}")
    print(f"SHA256 : {info['sha256']}")
    if info["meta"]:
        m = info["meta"]
        print(f"Target : {m['robotId']} ({m['board']}) — v{m['version']}")
    else:
        print("Target : <no SUMO_META tag found in binary>")
    return 0


def cmd_status(args: argparse.Namespace) -> int:
    code, status = http_json(args.host, "/api/status")
    if code != 200:
        print(f"Error (HTTP {code}): {status}", file=sys.stderr)
        return 1

    print("=== Sumobot Status ===")
    for k in ("robotId", "board", "version", "partition", "otaState", "armed", "batteryV", "freeHeap", "uptimeSec"):
        if k in status:
            print(f"  {k:<12}: {status[k]}")

    pcode, params = http_json(args.host, "/api/params")
    if pcode == 200:
        print("=== Live NVS Parameters ===")
        for k, v in params.items():
            print(f"  {k:<15}: {v}")
    return 0


def cmd_flash(args: argparse.Namespace) -> int:
    return do_flash(
        bin_path=Path(args.binary),
        host=args.host,
        token=resolve_token(args.token),
        force=args.force,
        no_wait=args.no_wait,
        wait=args.wait,
        quiet=args.quiet,
    )


def cmd_params(args: argparse.Namespace) -> int:
    token = resolve_token(args.token)
    if args.reset:
        code, res = http_json(
            args.host,
            "/api/params/reset",
            method="POST",
            headers={"X-OTA-Token": token},
        )
        print(json.dumps(res, indent=2))
        return 0 if code == 200 and res.get("ok") else 1

    if not args.set:
        code, res = http_json(args.host, "/api/params")
        print(json.dumps(res, indent=2))
        return 0 if code == 200 else 1

    kv: Dict[str, str] = {}
    for item in args.set:
        if "=" not in item:
            print(f"Invalid --set format '{item}' (expected key=value)", file=sys.stderr)
            return 2
        k, v = item.split("=", 1)
        kv[k.strip()] = v.strip()

    body = urllib.parse.urlencode(kv).encode("utf-8")
    code, res = http_json(
        args.host,
        "/api/params",
        method="POST",
        headers={
            "X-OTA-Token": token,
            "Content-Type": "application/x-www-form-urlencoded",
        },
        body=body,
    )
    print(json.dumps(res, indent=2))
    return 0 if code == 200 and res.get("ok") else 1


def cmd_rollback(args: argparse.Namespace) -> int:
    code, res = http_json(
        args.host,
        "/api/rollback",
        method="POST",
        headers={"X-OTA-Token": resolve_token(args.token)},
    )
    print(json.dumps(res, indent=2))
    return 0 if code == 200 and res.get("ok") else 1


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="On The Air ESP — Sumobot OTA CLI Tool")
    sub = p.add_subparsers(dest="command", required=True)

    p_push = sub.add_parser("push", help="ONE-COMMAND compile + wireless OTA flash (auto-reads token from config.h)")
    p_push.add_argument("--sketch", default=str(SKETCH_DIR), help="Sketch folder path")
    p_push.add_argument("--fqbn", default=DEFAULT_FQBN, help=f"Board FQBN (default: {DEFAULT_FQBN})")
    p_push.add_argument("--host", default=DEFAULT_HOST)
    p_push.add_argument("--token", default=None, help="OTA token (auto-read from config.h if omitted)")
    p_push.add_argument("--force", action="store_true")
    p_push.add_argument("--no-wait", action="store_true")
    p_push.add_argument("--wait", type=int, default=20)
    p_push.add_argument("--quiet", action="store_true")
    p_push.set_defaults(func=cmd_push)

    p_ins = sub.add_parser("inspect", help="Inspect a compiled .bin firmware image offline")
    p_ins.add_argument("binary", help="Path to compiled .bin file")
    p_ins.set_defaults(func=cmd_inspect)

    p_st = sub.add_parser("status", help="Query Sumobot status and live NVS parameters")
    p_st.add_argument("--host", default=DEFAULT_HOST, help=f"Robot IP/host (default: {DEFAULT_HOST})")
    p_st.set_defaults(func=cmd_status)

    p_fl = sub.add_parser("flash", help="Safely flash an already-exported .bin over Wi-Fi")
    p_fl.add_argument("binary", help="Path to compiled .bin file")
    p_fl.add_argument("--host", default=DEFAULT_HOST)
    p_fl.add_argument("--token", default=None, help="OTA token (auto-read from config.h if omitted)")
    p_fl.add_argument("--force", action="store_true")
    p_fl.add_argument("--no-wait", action="store_true")
    p_fl.add_argument("--wait", type=int, default=20)
    p_fl.add_argument("--quiet", action="store_true")
    p_fl.set_defaults(func=cmd_flash)

    p_pa = sub.add_parser("params", help="Read or update live NVS Sumobot parameters")
    p_pa.add_argument("--host", default=DEFAULT_HOST)
    p_pa.add_argument("--token", default=None, help="OTA token (auto-read from config.h if omitted)")
    p_pa.add_argument("--set", nargs="+", metavar="KEY=VAL", help="Parameters to update")
    p_pa.add_argument("--reset", action="store_true", help="Reset all parameters to defaults")
    p_pa.set_defaults(func=cmd_params)

    p_rb = sub.add_parser("rollback", help="Roll back Sumobot to the previous OTA firmware slot")
    p_rb.add_argument("--host", default=DEFAULT_HOST)
    p_rb.add_argument("--token", default=None, help="OTA token (auto-read from config.h if omitted)")
    p_rb.set_defaults(func=cmd_rollback)

    return p


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (FileNotFoundError, ValueError, RuntimeError) as e:
        print(f"Error: {e}", file=sys.stderr)
        return 2
    except urllib.error.URLError as e:
        print(f"Network error connecting to {getattr(args, 'host', '')}: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
