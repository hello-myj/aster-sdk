#!/usr/bin/env python3
"""Verify device identity, transfer and install an APP; never builds/flashes firmware."""
import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

import aster_board
from target_checks import validate_device
from verify_mja import check


def shell_command(port, command, timeout=30):
    import serial
    with serial.Serial(port, 115200, timeout=0.2, write_timeout=5) as connection:
        connection.write(b"\x03\r\n")
        time.sleep(0.2)
        connection.reset_input_buffer()
        connection.write((command + "\r\n").encode())
        connection.flush()
        output = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            output.extend(connection.read(4096))
            text = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", output.decode(errors="replace"))
            if "aster:~$" in text:
                return text
        raise RuntimeError("device shell timed out: " + output.decode(errors="replace"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--target", required=True)
    parser.add_argument("--port", default="auto")
    parser.add_argument("--console")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--pubkey", type=Path)
    args = parser.parse_args()
    target = aster_board.get(args.target)
    manifest = check(args.package, args.pubkey, args.target)
    import aster_send
    port, pong = aster_send.find_port(args.port, brd=target)
    if not port:
        raise RuntimeError("no device answered the deployment probe")
    validate_device(pong, target, manifest.get("requires", []))
    remote = target["deployment"]["package_dir"] + "/" + manifest["id"] + ".mja"
    reply = aster_send.send_file(port, args.package, remote, target=target)
    if reply != "OK":
        raise RuntimeError("package transfer failed: " + str(reply))
    console = args.console or target["console"]
    response = shell_command(console, "aster app install " + remote)
    print(response)
    if not re.search(r"installed", response, re.IGNORECASE) or re.search(r"failed|error", response, re.IGNORECASE):
        raise RuntimeError("device did not confirm installation")
    if args.run:
        response = shell_command(console, "aster app start " + manifest["id"])
        print(response)
        if re.search(r"failed|error", response, re.IGNORECASE):
            raise RuntimeError("device could not start the application")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, RuntimeError) as error:
        sys.exit(str(error))
