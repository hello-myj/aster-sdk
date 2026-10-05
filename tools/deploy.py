#!/usr/bin/env python3
"""Verify device identity, transfer and install an APP; never builds/flashes firmware."""
import argparse
from contextlib import nullcontext
import re
import subprocess
import sys
import time
from pathlib import Path

import aster_board
from target_checks import validate_device
from verify_ast import check


def shell_command(port, command, timeout=45, connection=None):
    from aster_send import open_serial
    with (nullcontext(connection) if connection is not None else
          open_serial(port, timeout=0.2, write_timeout=5)) as connection:
        for attempt in range(3):
            connection.write(b"\x03\n")
            time.sleep(0.2)
            connection.reset_input_buffer()
            connection.write((command + "\n").encode())
            connection.flush()
            output = bytearray()
            deadline = time.monotonic() + timeout / 3
            while time.monotonic() < deadline:
                output.extend(connection.read(4096))
                reply = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", output.decode(errors="replace"))
                echo = reply.find(command)
                if echo >= 0 and "aster:~$" in reply[echo + len(command):]:
                    return reply
            time.sleep(1)
    raise RuntimeError("device shell timed out waiting for command echo: " + command)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--target", required=True)
    parser.add_argument("--port", default="auto")
    parser.add_argument("--console")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--pubkey", type=Path)
    args = parser.parse_args(argv)
    target = aster_board.get(args.target)
    manifest = check(args.package, args.pubkey, args.target)
    import aster_send
    port, pong = aster_send.find_port(args.port, brd=target)
    if not port:
        raise RuntimeError("no device answered the deployment probe")
    validate_device(pong, target, manifest.get("requires", []))
    remote = target["deployment"]["package_dir"] + "/" + manifest["id"] + ".ast"
    console = args.console or (port if "serial=shared" in pong else target["console"])
    shared = console == port and "serial=shared" in pong
    with (aster_send.open_serial(port, timeout=0.2, write_timeout=10)
          if shared else nullcontext(None)) as connection:
        reply = aster_send.send_file(port, args.package, remote, target=target, connection=connection)
        if reply != "OK":
            raise RuntimeError("package transfer failed: " + str(reply))
        response = shell_command(console, "aster app install " + remote, connection=connection)
        print(response)
        # Background services may report failures during a successful install.
        # Match the shell command's acknowledgement, not unrelated log wording.
        if not re.search(r"(?m)^\s*installed OK\s*$", response):
            raise RuntimeError("device did not confirm installation")
        if args.run:
            response = shell_command(console, "aster app start " + manifest["id"], connection=connection)
            print(response)
            # Recoverable driver diagnostics may contain "Error" even when the
            # shell confirms startup. Require the command result, not log wording.
            if not re.search(r"(?m)^\s*started " + re.escape(manifest["id"]) + r"\s*$", response):
                raise RuntimeError("device could not start the application")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, RuntimeError) as error:
        sys.exit(str(error))
