#!/usr/bin/env python3
"""
Sumobot OTA CLI Flasher & Pit Tool (Zero-Dependency Python 3)
=============================================================
Flash compiled Arduino IDE .bin files, tune live NVS parameters, and trigger
instant rollback on ESP32 NodeMCU-32S Sumobots over Wi-Fi.

Uses Python 3 standard library only — no pip packages required (0 EGP).

Examples:
  python tools/sumo_flash.py status --host 192.168.4.1
  python tools/sumo_flash.py inspect firmware.bin
  python tools/sumo_flash.py flash firmware.bin --host 192.168.4.1 --token sumo-pit-2026
  python tools/sumo_flash.py params --host 192.168.4.1 --token sumo-pit-2026 --set attackSpeed=245 strategy=1
  python tools/sumo_flash.py rollback --host 192.168.4.1 --token sumo-pit-2026
"""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

META_RE = re.compile(rb"SUMO_META:ID=([^;]+);BOARD=([^;]+);VER=([^;]+);")
DEFAULT_HOST = "192.168.4.1"
DEFAULT_TIMEOUT = 15


def inspect_binary(bin_path: Path) -> Dict[str, Any]:
    """Read a compiled ESP32 .bin file, compute hashes, and extract embedded metadata."""
    if not bin_path.is_file():
        raise FileNotFoundError(f"Firmware binary not found: {bin_path}")

    data = bin_path.read_bytes()
    if len(data) < 4096:
        raise ValueError(f"File too small ({len(data)} bytes) to be a valid ESP32 firmware image.")

    # ESP32 firmware images always start with magic byte 0xE9
    if data[0] != 0xE9:
        raise ValueError(
            f"Invalid ESP32 image magic byte: 0x{data[0]:02X} (expected 0xE9). "
            "Use Sketch -> Export Compiled Binary (.ino.bin, NOT .bootloader.bin or .partitions.bin)."
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
    """Stream multipart/form-data .bin upload with progress bar using stdlib http.client."""
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
    """Poll /api/status after flash reboot to confirm the new image came up healthy."""
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
    bin_path = Path(args.binary)
    info = inspect_binary(bin_path)
    print(f"[1/4] Binary verified: {bin_path.name} ({info['size'] // 1024} KB, MD5={info['md5']})")

    if info["meta"]:
        print(
            f"      Embedded tag: ID={info['meta']['robotId']}, "
            f"BOARD={info['meta']['board']}, VER=v{info['meta']['version']}"
        )
    elif not args.force:
        print(
            "Error: Binary has no SUMO_META tag. Pass --force to flash anyway.",
            file=sys.stderr,
        )
        return 2

    print(f"[2/4] Checking live robot at http://{args.host} ...")
    code, st = http_json(args.host, "/api/status")
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

    if info["meta"] and info["meta"]["robotId"] != live_id and not args.force:
        print(
            f"Error: Target mismatch! Binary is for '{info['meta']['robotId']}' "
            f"but connected robot is '{live_id}'. Use --force to override.",
            file=sys.stderr,
        )
        return 4

    print("[3/4] Flashing over Wi-Fi...")
    up_code, up_res = upload_firmware(
        host=args.host,
        bin_path=bin_path,
        token=args.token,
        target_robot="" if args.force else live_id,
        md5_hex=info["md5"],
        show_progress=not args.quiet,
    )
    if up_code != 200 or not up_res.get("ok"):
        print(f"Error: OTA failed (HTTP {up_code}): {up_res}", file=sys.stderr)
        return 5

    print(f"      ESP32 accepted image: {up_res.get('message', 'OK')}")

    if args.no_wait:
        return 0

    print("[4/4] Verifying post-reboot health...")
    after = wait_for_reboot(args.host, wait_sec=args.wait)
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


def cmd_params(args: argparse.Namespace) -> int:
    if args.reset:
        code, res = http_json(
            args.host,
            "/api/params/reset",
            method="POST",
            headers={"X-OTA-Token": args.token},
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
            "X-OTA-Token": args.token,
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
        headers={"X-OTA-Token": args.token},
    )
    print(json.dumps(res, indent=2))
    return 0 if code == 200 and res.get("ok") else 1


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Sumobot ESP32 NodeMCU-32S OTA CLI Tool")
    sub = p.add_subparsers(dest="command", required=True)

    p_ins = sub.add_parser("inspect", help="Inspect a compiled .bin firmware image offline")
    p_ins.add_argument("binary", help="Path to compiled .bin file")
    p_ins.set_defaults(func=cmd_inspect)

    p_st = sub.add_parser("status", help="Query Sumobot status and live NVS parameters")
    p_st.add_argument("--host", default=DEFAULT_HOST, help=f"Robot IP/host (default: {DEFAULT_HOST})")
    p_st.set_defaults(func=cmd_status)

    p_fl = sub.add_parser("flash", help="Safely flash a .bin firmware image over Wi-Fi")
    p_fl.add_argument("binary", help="Path to compiled .bin file")
    p_fl.add_argument("--host", default=DEFAULT_HOST)
    p_fl.add_argument("--token", default=os.environ.get("SUMO_OTA_TOKEN", "sumo-pit-2026"))
    p_fl.add_argument("--force", action="store_true", help="Ignore missing or mismatched SUMO_META target ID")
    p_fl.add_argument("--no-wait", action="store_true", help="Do not wait for post-reboot status check")
    p_fl.add_argument("--wait", type=int, default=20, help="Seconds to wait for reboot confirmation")
    p_fl.add_argument("--quiet", action="store_true", help="Disable upload progress bar")
    p_fl.set_defaults(func=cmd_flash)

    p_pa = sub.add_parser("params", help="Read or update live NVS Sumobot parameters")
    p_pa.add_argument("--host", default=DEFAULT_HOST)
    p_pa.add_argument("--token", default=os.environ.get("SUMO_OTA_TOKEN", "sumo-pit-2026"))
    p_pa.add_argument("--set", nargs="+", metavar="KEY=VAL", help="Parameters to update")
    p_pa.add_argument("--reset", action="store_true", help="Reset all parameters to defaults")
    p_pa.set_defaults(func=cmd_params)

    p_rb = sub.add_parser("rollback", help="Roll back Sumobot to the previous OTA firmware slot")
    p_rb.add_argument("--host", default=DEFAULT_HOST)
    p_rb.add_argument("--token", default=os.environ.get("SUMO_OTA_TOKEN", "sumo-pit-2026"))
    p_rb.set_defaults(func=cmd_rollback)

    return p


def main(argv: Optional[list[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (FileNotFoundError, ValueError) as e:
        print(f"Error: {e}", file=sys.stderr)
        return 2
    except urllib.error.URLError as e:
        print(f"Network error connecting to {getattr(args, 'host', '')}: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
