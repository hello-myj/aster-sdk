"""Aster device CLI: serial deployment, flash, console and screenshots."""
import argparse
import os
import shutil
import subprocess
import sys
import threading
import time
import tomllib
from pathlib import Path

import aster_board
import aster_send
import deploy
import screenshot
from runtime_paths import output_root, sdk_root
from target_checks import validate_device
from release_identity import version_requested, check_sibling


def console_port(preferred: str | None, data_port: str | None = None) -> str:
    if preferred and preferred != "auto":
        return preferred
    from serial.tools import list_ports
    candidates = ([data_port] if data_port else []) + [p.device for p in list_ports.comports() if p.device != data_port]
    if os.name != "nt":
        candidates = sorted(candidates, key=lambda name: ("ttyUSB" not in name, name))
    for port in candidates:
        try:
            if "aster:~$" in deploy.shell_command(port, "aster sys info", 2):
                return port
        except (OSError, RuntimeError):
            continue
    raise RuntimeError("Aster console not found; specify --console COMx")


def data_port(preferred: str, target: dict) -> tuple[str, str]:
    port, pong = aster_send.find_port(preferred, brd=target)
    if not port:
        raise RuntimeError("Aster file service not found; specify --port COMx")
    validate_device(pong, target)
    return port, pong


def toolchain_path(preferred: Path | None) -> Path:
    if preferred:
        candidate = preferred.resolve()
    else:
        sibling = Path(sys.executable).with_name("aster-toolchain.exe")
        name = 'aster-toolchain.exe' if os.name=='nt' else 'aster-toolchain'
        bundled = sdk_root()/name
        candidate = bundled if bundled.is_file() else (sibling if sibling.is_file() else Path(
            shutil.which(name) or name))
    if not candidate.is_file():
        raise RuntimeError("aster-toolchain.exe not found; set --toolchain PATH")
    if getattr(sys, 'frozen', False) or (sdk_root()/'release.json').is_file():
        check_sibling(candidate)
    return candidate


def monitor(port: str, logfile: Path | None = None, seconds: float | None = None,
            interactive: bool = True) -> None:
    import serial
    with aster_send.open_serial(port, timeout=0.2) as connection:
        with logfile.open("ab") if logfile else open(os.devnull, "ab") as output:
            deadline = time.monotonic() + seconds if seconds else None
            print(f"monitoring {port}; Ctrl+C to stop", file=sys.stderr)
            if interactive and sys.stdin.isatty():
                def send_lines():
                    try:
                        for line in sys.stdin:
                            connection.write(line.rstrip("\r\n").encode("utf-8") + b"\r\n")
                            connection.flush()
                    except (OSError, ValueError):
                        pass
                threading.Thread(target=send_lines, daemon=True).start()
            try:
                while deadline is None or time.monotonic() < deadline:
                    data = connection.read(4096)
                    if data:
                        sys.stdout.buffer.write(data)
                        sys.stdout.buffer.flush()
                        output.write(data)
            except KeyboardInterrupt:
                pass


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if version_requested(argv):
        return 0
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("ports", help="list serial ports")
    probe = sub.add_parser("probe", help="identify Aster file-service port")
    probe.add_argument("--port", default="auto")
    probe.add_argument("--target", default="xtensa")
    compile_app = sub.add_parser("build", help="compile an APP using the toolchain EXE")
    compile_app.add_argument("app", type=Path)
    compile_app.add_argument("--toolchain", type=Path)
    compile_app.add_argument("--target", default="xtensa")
    compile_app.add_argument("--no-aot", action="store_true")
    push = sub.add_parser("push", help="copy a local file to SD")
    push.add_argument("local", type=Path)
    push.add_argument("remote", nargs="?", help="absolute /SD:/ path")
    push.add_argument("--port", default="auto")
    push.add_argument("--target", default="xtensa")
    install = sub.add_parser("deploy", help="verify, transfer and install an APP")
    install.add_argument("package", type=Path)
    install.add_argument("--port", default="auto")
    install.add_argument("--console", default="auto")
    install.add_argument("--target", default="xtensa")
    install.add_argument("--run", action="store_true")
    build = sub.add_parser("build-deploy", help="compile with toolchain EXE and install")
    build.add_argument("app", type=Path)
    build.add_argument("--toolchain", type=Path)
    build.add_argument("--port", default="auto")
    build.add_argument("--console", default="auto")
    build.add_argument("--target", default="xtensa")
    build.add_argument("--run", action="store_true")
    shell = sub.add_parser("shell", help="run a firmware shell command")
    shell.add_argument("words", nargs="+")
    shell.add_argument("--console", default="auto")
    debug = sub.add_parser("debug", help="stream serial logs and shell output")
    debug.add_argument("--console", default="auto")
    debug.add_argument("--log", type=Path)
    debug.add_argument("--seconds", type=float)
    debug.add_argument("--read-only", action="store_true", help="do not forward typed shell commands")
    shot = sub.add_parser("screenshot", help="capture screen as PNG")
    shot.add_argument("output", type=Path)
    shot.add_argument("--console", default="auto")
    shot.add_argument("--page", choices=("home", "apps"))
    flash = sub.add_parser("flash", help="flash a merged ESP32-S3 firmware image")
    flash.add_argument("image", type=Path)
    flash.add_argument("--port", required=True, help="bootloader UART, e.g. COM3")
    flash.add_argument("--address", default="0x0")
    flash.add_argument("--baud", type=int, default=460800)
    flash.add_argument("--chip", default="esp32s3", choices=("esp32s3",))
    args = parser.parse_args(argv)

    if args.command == "ports":
        from serial.tools import list_ports
        for port in list_ports.comports():
            print(f"{port.device:12} {port.description} [{port.hwid}]")
        return 0
    if args.command == "flash":
        if not args.image.is_file():
            parser.error(f"firmware image missing: {args.image}")
        import esptool
        esptool.main(["--chip", args.chip, "--port", args.port,
                      "--baud", str(args.baud), "--before", "default-reset",
                      "--after", "hard-reset", "write-flash", "--flash-mode", "dio",
                      args.address, str(args.image)])
        return 0
    if args.command == "build":
        command = [str(toolchain_path(args.toolchain)), "build", str(args.app.resolve()),
                   "--target", args.target, "--no-aot" if args.no_aot else "--aot"]
        subprocess.run(command, check=True)
        return 0
    if args.command == "shell":
        print(deploy.shell_command(console_port(args.console), " ".join(args.words)))
        return 0
    if args.command == "debug":
        monitor(console_port(args.console), args.log, args.seconds, not args.read_only)
        return 0
    if args.command == "screenshot":
        port = console_port(args.console)
        width, height = screenshot.save_png(port, args.output, args.page)
        print(f"{args.output}: {width}x{height}")
        return 0

    target = aster_board.get(args.target)
    port, pong = data_port(args.port, target)
    if args.command == "probe":
        print(f"{port}: {pong}")
        return 0
    if args.command == "push":
        if not args.local.is_file():
            parser.error(f"file missing: {args.local}")
        remote = args.remote or "/SD:/" + args.local.name
        if (not remote.startswith("/SD:/") or any(part == ".." for part in remote.split("/"))
                or any(char.isspace() for char in remote)):
            parser.error("remote path must be an absolute /SD:/ path without spaces or '..'")
        response = aster_send.send_file(port, args.local, remote, target=target)
        if response != "OK":
            raise RuntimeError("file transfer failed: " + response)
        print(f"{args.local} -> {remote}: OK")
        return 0
    if args.command == "build-deploy":
        app = args.app.resolve()
        manifest = tomllib.loads((app / "app.toml").read_text(encoding="utf-8"))
        tool = toolchain_path(args.toolchain)
        subprocess.run([str(tool), "build", str(app), "--target", args.target,
                        "--aot"], check=True)
        args.package = output_root() / "dist" / args.target / (manifest["app"]["id"] + ".ast")
    console = (port if args.console == "auto" and "serial=shared" in pong
               else console_port(args.console, port))
    command = [str(args.package), "--port", port, "--console", console,
               "--target", args.target]
    if args.run:
        command.append("--run")
    return deploy.main(command)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
