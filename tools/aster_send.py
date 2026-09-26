#!/usr/bin/env python3
"""aster_send — ship a file to the board's SD card over the native USB-CDC.

Protocol (device: firmware/services/aster_file_svc.c):
    ASTERPUT <abs-path> <len> <crc32-hex>\r\n  + raw bytes
    -> "OK" | "ERR <code>\r\n"
Small chunk + inter-chunk pacing because raw CDC has no flow control; the
device verifies length + crc32, so any glitch fails fast (send_file raises
RetryError -> caller resends the whole file).

CLI:
    aster_send.py [--port auto] <local-file> [<sd-path>]
    aster_send.py --ping
"""
import argparse
import glob
import os
import re
import sys
import time
import zlib
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aster_board  # noqa: E402


class RetryError(Exception):
    """integrity failed (crc/length); safe to resend the whole file."""


def _probe(s):
    """Send ASTERPING, retrying when USB CDC drops the first write on reopen."""
    for _ in range(3):
        s.reset_input_buffer()
        s.write(b"ASTERPING\r\n")
        s.flush()
        deadline = time.monotonic() + 1.0
        buf = b""
        while time.monotonic() < deadline:
            buf += s.read(64)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                if line.startswith(b"PONG"):
                    return line.decode(errors="replace").strip()
        time.sleep(0.15)
    return None


def find_port(prefer=None, brd=None):
    """Return (port, pong) for the first candidate answering ASTERPING.

    Which device that is is a MODEL fact -- the reference board serves files
    on its native USB-Serial-JTAG (a ttyACM), but a board that puts the file
    service on a UART bridge or names it differently declares another glob in
    aster_board.MODELS. That glob used to be a literal here while the registry's
    docstring promised an ASTER_CDC override that no code read; now there is one
    copy of it and the override works (both are applied in aster_board.get()).
    Callers pass the model they already resolved (brd), so `--board` selects
    the data channel the same way it selects the console -- resolving the
    console from one model and the data port from another is how a selection
    story ends up half-applied. The name is never trusted: every candidate is
    probed with ASTERPING.
    """
    brd = brd or aster_board.get(None)
    cands = ([prefer] if prefer and prefer != "auto" else [])
    if not cands:
        if os.name == "nt":
            from serial.tools import list_ports
            cands = [port.device for port in list_ports.comports()]
        else:
            cands = sorted(glob.glob(brd["data"]["glob"]))
    for p in dict.fromkeys(cands):
        try:
            with serial.Serial(p, 115200, timeout=0.5, write_timeout=3) as s:
                pong = _probe(s)
                if pong:
                    return p, pong
        except Exception:
            continue
    return None, None


def _token(s, tmo):
    """Read a protocol line without reconfiguring a live Windows CDC port."""
    deadline = time.monotonic() + tmo
    buf = b""
    while time.monotonic() < deadline:
        buf += s.read(64)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            token = raw.decode(errors="replace").strip()
            if token.startswith(("ACK ", "OK", "ERR", "RESUME")):
                return token
    return None


def send_file(port, local, remote=None, chunk=None, gap=None, retries=3,
              wait=None, ack_tmo=30, target=None):
    """Push one file with stop-and-wait flow control + crc32 + resume.

    ASTERFS 3: the device answers the PUT header with a go-token "ACK <off>",
    then one "ACK <cum>" per 512-byte frame it has written. We never have a
    second frame in flight, so a slow fs_write can no longer overflow the CDC
    RX ring (the old dropped-byte/CRC-mismatch root cause). "RESUME <n>"
    mid-stream keeps <n> durable bytes in <remote>.part; we re-PUT from n.
    "ERR retry" (crc mismatch) means the .part was dropped -> restart at 0.
    """
    data = open(local, "rb").read()
    if remote is None:
        remote = "/SD:/" + os.path.basename(local)
    n = len(data)
    if wait is None:
        wait = 120.0 if n > 1024 * 1024 else 15.0
    crc = zlib.crc32(data) & 0xFFFFFFFF
    FRAME = 512            # must match device BUF_SZ

    attempt = 0
    resume_from = 0
    # A flaky card can wedge then recover: allow many no-progress RESUME rounds
    # (stall) but still bound them. hard_cap is the absolute send budget.
    stall = 0
    hard_cap = 20 + 4 * ((n + 65535) // 65536)
    last = ""
    with serial.Serial(port, 115200, timeout=0.5, write_timeout=10) as s:
        pong = _probe(s)
        if target is not None:
            from target_checks import validate_device
            validate_device(pong or "", target)
        if not pong:
            raise RuntimeError(f"{port} not answering ASTERPING")
        # Give the USB CDC service time to finish its PONG before ASTERPUT.
        time.sleep(0.5)
        for _step in range(hard_cap):
            if _step:
                time.sleep(0.5)
            s.reset_input_buffer()
            s.write(f"ASTERPUT {remote} {n} {crc:08x} {resume_from}\r\n".encode())
            s.flush()

            tok = _token(s, ack_tmo)          # go-token (or early verdict)
            if tok is None:
                raise TimeoutError("no ACK to ASTERPUT header")
            if tok == "OK":                   # .part already complete+valid
                return "OK"
            m = re.match(r"RESUME (\d+)", tok)
            if m:
                # .part complete (or clamped) but the device's verify-read
                # glitched: keep offset, re-PUT now so it retries the crc.
                resume_from = min(int(m.group(1)), n)
                stall += 1
                if stall > 8:
                    raise RetryError(f"stalled at {resume_from}: {tok}")
                continue
            if tok == "ERR retry":
                attempt += 1
                stall = 0
                resume_from = 0
                if attempt >= retries:
                    raise RetryError(f"still failing after {retries} restarts")
                continue
            if not tok.startswith("ACK "):
                return tok                    # hard error: report to caller

            pos = int(tok[4:])                # device-truth start offset
            status = None
            next_report = pos + 64 * 1024
            while pos < n:
                s.write(data[pos:min(pos + FRAME, n)])
                s.flush()
                pos = min(pos + FRAME, n)
                if pos < n:
                    tok = _token(s, ack_tmo)
                    if tok is None:
                        status = "ERR -110"   # device died mid-stream
                        break
                    if tok.startswith("ACK "):
                        pos = int(tok[4:])    # trust device, not our count
                        if pos >= next_report:
                            print(f"  {pos}/{n}", file=sys.stderr, flush=True)
                            next_report = pos + 64 * 1024
                        continue
                    status = tok              # RESUME / ERR after a frame
                    break
                status = _token(s, wait)      # final verdict after last frame
                if status is None:
                    status = "ERR -110"
                break

            print(f"  [{resume_from}->{pos if status != 'OK' else n}/{n}] "
                  f"-> {status[:60]}", file=sys.stderr, flush=True)
            last = status or "ERR -110"
            if last == "OK":
                return "OK"
            m = re.match(r"RESUME (\d+)", last)
            if m:
                new_off = int(m.group(1))
                if new_off > resume_from:     # progress: keep resuming
                    resume_from = new_off
                    stall = 0
                    continue
                stall += 1                    # wedged: stay, let the card
                if stall > 8:                 # recover (flaky FAT bursts)
                    attempt += 1
                    stall = 0
                    resume_from = 0
                continue
            elif last == "ERR retry":         # crc: device dropped .part
                attempt += 1
                stall = 0
                resume_from = 0
            else:
                return last                   # hard error
            if attempt >= retries:
                raise RetryError(f"still failing after {retries} restarts: {last}")
    raise RetryError(f"no progress after {hard_cap} sends: {last}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("local", nargs="?")
    ap.add_argument("remote", nargs="?", default=None)
    ap.add_argument("--port", default="auto")
    ap.add_argument("--target", required=True)
    ap.add_argument("--ping", action="store_true")
    ap.add_argument("--chunk", type=int, default=512,
                    help="(fixed frame size = device BUF_SZ; ignored)")
    args = ap.parse_args()
    target = aster_board.get(args.target)

    if args.ping or not args.local:
        port, pong = find_port(args.port, brd=target)
        print(f"{port}: {pong}" if port
              else f"no ASTER file service on {aster_board.data_glob(args.target)}")
        return 0 if port else 2

    port = args.port
    if port == "auto":
        port, pong = find_port(brd=target)
        if not port:
            print("no file service found", file=sys.stderr)
            return 2
        print(f"port {port}: {pong}")
    reply = send_file(port, args.local, args.remote, chunk=args.chunk, target=target)
    print(f"{args.remote or ''} -> {reply}")
    return 0 if reply == "OK" else 1


if __name__ == "__main__":
    sys.exit(main())
