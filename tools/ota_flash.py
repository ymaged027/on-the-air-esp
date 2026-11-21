#!/usr/bin/env python3
"""
On The Air ESP — Zero-Dependency Setup, Library Installer & OTA CLI
===================================================================
Uses Python 3 standard library only — zero pip packages required (0 EGP).

Commands:
  python tools/ota_flash.py setup                  # 1-step USB setup (library + ESP32 core + USB flash)
  python tools/ota_flash.py install-lib            # Install OnTheAirESP into Arduino IDE Libraries
  python tools/ota_flash.py push [sketch_or_ino]   # Compile + flash over MTR-OTA Wi-Fi
  python tools/ota_flash.py status                 # Check device telemetry & live NVS params
  python tools/ota_flash.py params --set speedMax=245 mode=1
  python tools/ota_flash.py rollback               # Instant wireless rollback
  python tools/ota_flash.py flash file.ino.bin     # Flash an already-exported .bin
  python tools/ota_flash.py inspect file.ino.bin   # Inspect embedded metadata & checksums
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
from typing import Any, Dict, List, Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parents[1]
SKETCH_DIR = REPO_ROOT / "firmware" / "on-the-air-esp"
CONFIG_HEADER = REPO_ROOT / "src" / "config.h"
DEFAULT_FQBN = "esp32:esp32:nodemcu-32s"
ESP32_BOARD_URL = "https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"

META_RE = re.compile(rb"OTA_META:ID=([^;]+);BOARD=([^;]+);VER=([^;]+);")
TOKEN_DEFINE_RE = re.compile(r'^\s*#define\s+OTA_AUTH_TOKEN\s+"([^"]*)"', re.MULTILINE)
DEFAULT_HOST = "192.168.4.1"
DEFAULT_TIMEOUT = 15


def read_token_from_config(config_path: Path = CONFIG_HEADER) -> str:
    """Auto-read OTA_AUTH_TOKEN from src/config.h."""
    if not config_path.is_file():
        return ""
    text = config_path.read_text(encoding="utf-8", errors="replace")
    m = TOKEN_DEFINE_RE.search(text)
    return m.group(1) if m else ""


def resolve_sketch_dir(sketch_arg: str | Path) -> Path:
    """Accept either a sketch directory OR a direct path to a .ino file (great for drag-and-drop!)."""
    p = Path(sketch_arg).expanduser().resolve()
    if p.is_file() and p.suffix.lower() == ".ino":
        return p.parent
    if p.is_dir():
        return p
    raise FileNotFoundError(f"Sketch path not found: {sketch_arg}")


def default_arduino_libraries_dir() -> Path:
    """Return the standard Arduino IDE user libraries folder for the current OS."""
    home = Path.home()
    if os.name == "nt":
        docs = home / "Documents" / "Arduino" / "libraries"
        if (home / "OneDrive" / "Documents" / "Arduino").is_dir() and not (home / "Documents" / "Arduino").is_dir():
            docs = home / "OneDrive" / "Documents" / "Arduino" / "libraries"
        return docs
    return home / "Arduino" / "libraries"


def default_esp32_hardware_dirs() -> List[Path]:
    """Return installed Arduino esp32 hardware version directories."""
    home = Path.home()
    roots = []
    if os.name == "nt":
        local_app = os.environ.get("LOCALAPPDATA", "")
        if local_app:
            roots.append(Path(local_app) / "Arduino15" / "packages" / "esp32" / "hardware" / "esp32")
    else:
        roots.append(home / ".arduino15" / "packages" / "esp32" / "hardware" / "esp32")
        roots.append(home / "Library" / "Arduino15" / "packages" / "esp32" / "hardware" / "esp32")

    found: List[Path] = []
    for root in roots:
        if root.is_dir():
            for child in sorted(root.iterdir()):
                if child.is_dir() and (child / "platform.txt").is_file():
                    found.append(child)
    return found


def install_ide_monitor(
    hardware_dirs: Optional[List[Path]] = None,
    repo_root: Path = REPO_ROOT,
    python_exe: str = sys.executable,
) -> List[Path]:
    """
    Register OTA Monitor as Arduino IDE's official Pluggable Monitor for `network` ports
    by writing `pluggable_monitor.pattern.network` into esp32's platform.local.txt.
    """
    dirs = hardware_dirs if hardware_dirs is not None else default_esp32_hardware_dirs()
    monitor_script = (repo_root / "tools" / "ota_monitor.py").resolve()
    py_path = Path(python_exe).resolve().as_posix()
    mon_path = monitor_script.as_posix()
    prop_line = f'pluggable_monitor.pattern.network="{py_path}" "{mon_path}"'

    updated: List[Path] = []
    for hw_dir in dirs:
        local_txt = hw_dir / "platform.local.txt"
        existing_lines: List[str] = []
        if local_txt.is_file():
            existing_lines = local_txt.read_text(encoding="utf-8", errors="replace").splitlines()

        filtered = [
            ln
            for ln in existing_lines
            if not ln.strip().startswith("pluggable_monitor.pattern.network=")
            and ln.strip() != "# On The Air ESP - Arduino IDE Wireless OTA Monitor"
        ]
        filtered.append("# On The Air ESP - Arduino IDE Wireless OTA Monitor")
        filtered.append(prop_line)
        local_txt.write_text("\n".join(filtered).strip() + "\n", encoding="utf-8")
        updated.append(local_txt)
    return updated


def install_arduino_library(target_libraries_dir: Optional[Path] = None, repo_root: Path = REPO_ROOT) -> Path:
    """Install OnTheAirESP into Arduino IDE's libraries folder so #include <OnTheAirESP.h> works everywhere."""
    libs_dir = target_libraries_dir or default_arduino_libraries_dir()
    dest = libs_dir / "OnTheAirESP"
    libs_dir.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True, exist_ok=True)

    shutil.copy2(repo_root / "library.properties", dest / "library.properties")
    shutil.copytree(repo_root / "src", dest / "src")
    if (repo_root / "examples").is_dir():
        shutil.copytree(repo_root / "examples", dest / "examples")
    return dest


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


def ensure_esp32_core(cli: str) -> None:
    """Ensure the esp32:esp32 board package is installed in Arduino IDE / arduino-cli."""
    res = subprocess.run([cli, "core", "list", "--format", "json"], capture_output=True, text=True)
    if res.returncode == 0 and "esp32:esp32" in res.stdout:
        print("      ESP32 board package already installed.")
        return

    print("      ESP32 board package not found — installing esp32:esp32 (first time only)...")
    subprocess.run(
        [cli, "config", "add", "board_manager.additional_urls", ESP32_BOARD_URL],
        check=False,
    )
    subprocess.run([cli, "core", "update-index"], check=True)
    subprocess.run([cli, "core", "install", "esp32:esp32"], check=True)


def list_serial_ports(cli: str) -> List[Dict[str, str]]:
    """Query connected serial ports via arduino-cli."""
    res = subprocess.run([cli, "board", "list", "--format", "json"], capture_output=True, text=True)
    if res.returncode != 0 or not res.stdout.strip():
        return []
    data = json.loads(res.stdout)
    raw_ports = data.get("detected_ports", data) if isinstance(data, dict) else data
    ports: List[Dict[str, str]] = []
    for entry in raw_ports or []:
        port_info = entry.get("port", entry) if isinstance(entry, dict) else {}
        if port_info.get("protocol") == "serial" and port_info.get("address"):
            label = port_info.get("protocol_label") or port_info.get("label") or "Serial"
            ports.append({"address": port_info["address"], "label": label})
    return ports


def compile_sketch(sketch_dir: Path = SKETCH_DIR, fqbn: str = DEFAULT_FQBN) -> Path:
    """Compile the Arduino sketch using Arduino IDE's bundled arduino-cli and return the .ino.bin path."""
    cli = find_arduino_cli()
    if not cli:
        raise FileNotFoundError(
            "Could not find arduino-cli. Install Arduino IDE 2.x (https://www.arduino.cc/en/software) "
            "or add arduino-cli to PATH."
        )

    sketch_dir = resolve_sketch_dir(sketch_dir)
    build_dir = Path(tempfile.gettempdir()) / f"on-the-air-esp-build-{sketch_dir.name}"
    build_dir.mkdir(parents=True, exist_ok=True)
    print(f"[0/4] Compiling '{sketch_dir.name}' for {fqbn} ...")
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
    res = subprocess.run(cmd)
    if res.returncode != 0:
        raise RuntimeError(f"Arduino compile failed (exit code {res.returncode}). Fix code errors and retry.")

    bin_path = build_dir / f"{sketch_dir.name}.ino.bin"
    if not bin_path.is_file():
        bins = [b for b in build_dir.glob("*.ino.bin") if "merged" not in b.name]
        if bins:
            bin_path = bins[0]
        else:
            raise FileNotFoundError(f"Expected compiled binary not found in: {build_dir}")
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
            "Pick *.ino.bin (NOT .bootloader.bin or .partitions.bin)."
        )

    md5_hex = hashlib.md5(data).hexdigest()
    sha256_hex = hashlib.sha256(data).hexdigest()

    meta: Optional[Dict[str, str]] = None
    m = META_RE.search(data)
    if m:
        meta = {
            "deviceId": m.group(1).decode("utf-8", errors="replace"),
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
    target_device: str,
    md5_hex: str,
    show_progress: bool = True,
) -> Tuple[int, Dict[str, Any]]:
    boundary = f"----MtrOTA{uuid.uuid4().hex}"
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
        if target_device:
            conn.putheader("X-Target-Device", target_device)
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
    print(f"Waiting up to {wait_sec}s for device to reboot into new partition...")
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
    if "OTA_AUTH_TOKEN" in os.environ:
        return os.environ["OTA_AUTH_TOKEN"]
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
            f"      Embedded tag: ID={info['meta']['deviceId']}, "
            f"BOARD={info['meta']['board']}, VER=v{info['meta']['version']}"
        )
    elif not force:
        print("Error: Binary has no OTA_META tag. Pass --force to flash anyway.", file=sys.stderr)
        return 2

    print(f"[2/4] Checking live device at http://{host} ...")
    code, st = http_json(host, "/api/status")
    if code != 200 or not st.get("ok"):
        print(f"Error: Cannot reach device (HTTP {code}): {st}", file=sys.stderr)
        return 1

    live_id = st.get("deviceId", "")
    print(
        f"      Connected to {live_id} v{st.get('version')} "
        f"[slot={st.get('partition')}, state={st.get('otaState')}, armed={st.get('armed')}]"
    )

    if st.get("armed"):
        print("Error: Device reports ARMED. Disarm outputs before flashing!", file=sys.stderr)
        return 3

    if info["meta"] and info["meta"]["deviceId"] != live_id and not force:
        print(
            f"Error: Target mismatch! Binary is for '{info['meta']['deviceId']}' "
            f"but connected device is '{live_id}'. Use --force to override.",
            file=sys.stderr,
        )
        return 4

    print("[3/4] Flashing over Wi-Fi...")
    up_code, up_res = upload_firmware(
        host=host,
        bin_path=bin_path,
        token=token,
        target_device="" if force else live_id,
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
            "Warning: Device did not reconnect within timeout. "
            "(Normal if it rebooted into Normal Run Mode with Wi-Fi OFF!)"
        )
        return 0

    print(
        f"SUCCESS! Device is back online: {after.get('deviceId')} v{after.get('version')} "
        f"[slot={after.get('partition')}, state={after.get('otaState')}]"
    )
    return 0


def cmd_install_lib(args: argparse.Namespace) -> int:
    target = Path(args.dir) if args.dir else None
    dest = install_arduino_library(target_libraries_dir=target)
    print(f"Installed Arduino library 'OnTheAirESP' -> {dest}")
    updated = install_ide_monitor()
    for p in updated:
        print(f"Registered Arduino IDE OTA Monitor -> {p}")
    print("Restart Arduino IDE (if open) to activate OnTheAirESP + wireless OTA Monitor.")
    return 0


def cmd_install_monitor(args: argparse.Namespace) -> int:
    updated = install_ide_monitor()
    if not updated:
        print("No installed esp32 Arduino core found yet. Run setup-usb.bat first.", file=sys.stderr)
        return 2
    for p in updated:
        print(f"Registered Arduino IDE OTA Monitor -> {p}")
    print("Restart Arduino IDE to use the built-in Monitor on network ports (e.g. 192.168.4.1).")
    return 0


def cmd_setup(args: argparse.Namespace) -> int:
    print("[1/4] Installing OnTheAirESP into Arduino IDE libraries...")
    dest = install_arduino_library()
    print(f"      Installed -> {dest}")

    print("[2/4] Checking Arduino CLI & ESP32 NodeMCU-32S board core...")
    cli = find_arduino_cli()
    if not cli:
        print("Error: Arduino IDE 2.x / arduino-cli not found.", file=sys.stderr)
        print("Install Arduino IDE 2.x from https://www.arduino.cc/en/software and re-run setup-usb.bat.")
        return 2
    ensure_esp32_core(cli)

    print("[3/4] Registering wireless OTA Monitor in Arduino IDE...")
    updated = install_ide_monitor()
    for p in updated:
        print(f"      Registered -> {p}")

    if args.no_flash:
        print("[4/4] Skipping USB flash (--no-flash). Setup complete!")
        return 0

    print("[4/4] Looking for plugged-in ESP32 NodeMCU-32S over USB...")
    port = args.port
    if not port:
        ports = list_serial_ports(cli)
        if not ports:
            print("      No USB serial board detected right now.")
            print("      Library & ESP32 core are ready! Plug in your NodeMCU-32S via USB and run setup-usb.bat again,")
            print("      OR open File -> Examples -> OnTheAirESP -> BasicOTA in Arduino IDE.")
            return 0
        if len(ports) == 1:
            port = ports[0]["address"]
            print(f"      Auto-selected USB port: {port} ({ports[0]['label']})")
        else:
            print("      Multiple serial ports detected:")
            for idx, p in enumerate(ports, start=1):
                print(f"        {idx}. {p['address']} ({p['label']})")
            choice = input("      Pick port number (or press Enter for 1): ").strip()
            pick = int(choice) - 1 if choice.isdigit() and 1 <= int(choice) <= len(ports) else 0
            port = ports[pick]["address"]

    sketch_dir = resolve_sketch_dir(args.sketch)
    print(f"      Compiling & uploading '{sketch_dir.name}' to {port} ({args.fqbn}) ...")
    cmd = [
        cli,
        "compile",
        "--upload",
        "--port",
        port,
        "--fqbn",
        args.fqbn,
        "--library",
        str(REPO_ROOT),
        str(sketch_dir),
    ]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print(
            "Error: USB upload failed. If NodeMCU-32S says 'Connecting...', hold the BOOT button for 1s during upload.",
            file=sys.stderr,
        )
        return res.returncode

    print()
    print("============================================================")
    print("  USB SETUP COMPLETE! You can unplug the USB cable now.")
    print("============================================================")
    print("  Next (wireless updates forever):")
    print("  1. Press BOOT right after power-on while blue LED fast-blinks")
    print("     (or close a jumper/switch between GPIO 33 and GND).")
    print("  2. Connect Wi-Fi to:  MTR-OTA   (password: mtr-ota007)")
    print("  3. Double-click flash.bat OR upload via Arduino IDE Port 'MTR-OTA'.")
    return 0


def cmd_push(args: argparse.Namespace) -> int:
    sketch_dir = resolve_sketch_dir(args.sketch)
    bin_path = compile_sketch(sketch_dir=sketch_dir, fqbn=args.fqbn)
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
        print(f"Target : {m['deviceId']} ({m['board']}) — v{m['version']}")
    else:
        print("Target : <no OTA_META tag found in binary>")
    return 0


def cmd_status(args: argparse.Namespace) -> int:
    code, status = http_json(args.host, "/api/status")
    if code != 200:
        print(f"Error (HTTP {code}): {status}", file=sys.stderr)
        return 1

    print("=== Device Status ===")
    for k in ("deviceId", "board", "version", "partition", "otaState", "armed", "batteryV", "freeHeap", "uptimeSec"):
        if k in status:
            print(f"  {k:<12}: {status[k]}")

    pcode, params = http_json(args.host, "/api/params")
    if pcode == 200:
        print("=== Live NVS Parameters ===")
        for k, v in params.items():
            print(f"  {k:<16}: {v}")
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
    p = argparse.ArgumentParser(description="On The Air ESP — NodeMCU-32S Setup & OTA CLI")
    sub = p.add_subparsers(dest="command", required=True)

    p_setup = sub.add_parser("setup", help="1-step first-time setup: install library + ESP32 core + USB flash")
    p_setup.add_argument("sketch", nargs="?", default=str(SKETCH_DIR), help="Sketch folder or .ino file")
    p_setup.add_argument("--port", default=None, help="USB COM port (auto-detected if omitted)")
    p_setup.add_argument("--fqbn", default=DEFAULT_FQBN, help=f"Board FQBN (default: {DEFAULT_FQBN})")
    p_setup.add_argument("--no-flash", action="store_true", help="Only install library & ESP32 core without USB flashing")
    p_setup.set_defaults(func=cmd_setup)

    p_lib = sub.add_parser("install-lib", help="Install OnTheAirESP library & register Arduino IDE OTA Monitor")
    p_lib.add_argument("--dir", default=None, help="Custom Arduino libraries directory")
    p_lib.set_defaults(func=cmd_install_lib)

    p_mon = sub.add_parser("install-monitor", help="Register wireless OTA Monitor inside Arduino IDE")
    p_mon.set_defaults(func=cmd_install_monitor)

    p_push = sub.add_parser("push", help="ONE-COMMAND compile + wireless OTA flash (supports any .ino or sketch folder)")
    p_push.add_argument("sketch", nargs="?", default=str(SKETCH_DIR), help="Sketch folder or .ino file")
    p_push.add_argument("--fqbn", default=DEFAULT_FQBN, help=f"Board FQBN (default: {DEFAULT_FQBN})")
    p_push.add_argument("--host", default=DEFAULT_HOST)
    p_push.add_argument("--token", default=None, help="OTA password (auto-read from src/config.h if omitted)")
    p_push.add_argument("--force", action="store_true")
    p_push.add_argument("--no-wait", action="store_true")
    p_push.add_argument("--wait", type=int, default=20)
    p_push.add_argument("--quiet", action="store_true")
    p_push.set_defaults(func=cmd_push)

    p_ins = sub.add_parser("inspect", help="Inspect a compiled .bin firmware image offline")
    p_ins.add_argument("binary", help="Path to compiled .bin file")
    p_ins.set_defaults(func=cmd_inspect)

    p_st = sub.add_parser("status", help="Query device status and live NVS parameters")
    p_st.add_argument("--host", default=DEFAULT_HOST, help=f"Device IP/host (default: {DEFAULT_HOST})")
    p_st.set_defaults(func=cmd_status)

    p_fl = sub.add_parser("flash", help="Safely flash an already-exported .bin over Wi-Fi")
    p_fl.add_argument("binary", help="Path to compiled .bin file")
    p_fl.add_argument("--host", default=DEFAULT_HOST)
    p_fl.add_argument("--token", default=None, help="OTA password (auto-read from src/config.h if omitted)")
    p_fl.add_argument("--force", action="store_true")
    p_fl.add_argument("--no-wait", action="store_true")
    p_fl.add_argument("--wait", type=int, default=20)
    p_fl.add_argument("--quiet", action="store_true")
    p_fl.set_defaults(func=cmd_flash)

    p_pa = sub.add_parser("params", help="Read or update live NVS device parameters")
    p_pa.add_argument("--host", default=DEFAULT_HOST)
    p_pa.add_argument("--token", default=None, help="OTA password (auto-read from src/config.h if omitted)")
    p_pa.add_argument("--set", nargs="+", metavar="KEY=VAL", help="Parameters to update")
    p_pa.add_argument("--reset", action="store_true", help="Reset all parameters to defaults")
    p_pa.set_defaults(func=cmd_params)

    p_rb = sub.add_parser("rollback", help="Roll back device to the previous OTA firmware slot")
    p_rb.add_argument("--host", default=DEFAULT_HOST)
    p_rb.add_argument("--token", default=None, help="OTA password (auto-read from src/config.h if omitted)")
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
