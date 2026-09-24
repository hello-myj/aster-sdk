#!/usr/bin/env python3
"""Pack app.wasm (+ optional app.aot, icon, assets) into a .ast package.

ASTP layout (see docs/package-format.md):
  [64 or 72B header][manifest][app.wasm][app.aot?][icon?][ASTR?]
  4-byte aligned. Header is 72 bytes only when an assets blob is present.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

MAGIC = b"ASTP"
HDR_SIZE = 64
HDR_SIZE_ASSETS = 72
FLAG_SIGNED = 1
FLAG_RESERVED_BIT1 = 2  # was HAS_UI (UIB1 ui.bin), dropped in ABI 3.0
FLAG_HAS_ASSETS = 4
ASSETS_MAX = 256 * 1024

AOT_TARGETS = {
    "none": 0,
    "xtensa-esp32s3": 1,
    "riscv": 2,
    "thumb-v7": 3,
    "cortex-m": 4,
    "x86-64": 5,
}


def pad4(n: int) -> int:
    return (n + 3) & ~3


def check_relpath(rel: str) -> str:
    rel = rel.replace("\\", "/").strip()
    if not rel or rel.startswith("/") or rel.endswith("/"):
        raise ValueError(f"bad asset path {rel!r}")
    parts = rel.split("/")
    if any(p in ("", ".", "..") for p in parts):
        raise ValueError(f"bad asset path {rel!r}")
    if len(rel) >= 96:
        raise ValueError(f"asset path too long {rel!r}")
    return rel


def build_mjr1(files: list[tuple[str, bytes]]) -> bytes:
    n = len(files)
    if n > 64:
        raise ValueError(f"too many assets ({n} > 64)")
    table_end = 16 + n * 12
    strtab = bytearray()
    path_offs: list[int] = []
    for rel, _ in files:
        path_offs.append(table_end + len(strtab))
        strtab += rel.encode("utf-8") + b"\0"
    data_start = pad4(table_end + len(strtab))
    payloads = bytearray()
    data_offs: list[int] = []
    data_lens: list[int] = []
    cursor = data_start
    for _, content in files:
        data_offs.append(cursor)
        data_lens.append(len(content))
        payloads += content + b"\0" * (pad4(len(content)) - len(content))
        cursor += pad4(len(content))
    hdr = bytearray(16)
    hdr[0:4] = b"ASTR"
    struct.pack_into("<HH", hdr, 4, 1, n)
    table = bytearray()
    for i in range(n):
        table += struct.pack("<III", path_offs[i], data_offs[i], data_lens[i])
    pad = b"\0" * (data_start - table_end - len(strtab))
    blob = bytes(hdr + table + strtab + pad + payloads)
    if len(blob) > ASSETS_MAX:
        print(f"warning: assets blob {len(blob)} bytes > {ASSETS_MAX} "
              "(device still installs; PSRAM load is capped at 64KB)",
              file=sys.stderr)
    return blob


def build_header(fields: dict, hdr_size: int) -> bytes:
    hdr = bytearray(hdr_size)
    hdr[0:4] = MAGIC
    struct.pack_into("<HHI", hdr, 4, 1, hdr_size, fields.get("flags", 0))
    struct.pack_into("<II", hdr, 12, fields["manifest_off"], fields["manifest_len"])
    struct.pack_into("<II", hdr, 20, fields["wasm_off"], fields["wasm_len"])
    struct.pack_into("<III", hdr, 28, fields["aot_off"], fields["aot_len"], fields["aot_target"])
    struct.pack_into("<II", hdr, 40, fields["icon_off"], fields["icon_len"])
    struct.pack_into("<HH", hdr, 52, fields.get("icon_w", 0), fields.get("icon_h", 0))
    # 56..63 reserved: was ui_off/ui_len (ABI 1.5 UIB1 ui.bin), dropped in 3.0.
    if hdr_size >= HDR_SIZE_ASSETS:
        struct.pack_into("<II", hdr, 64, fields.get("assets_off", 0), fields.get("assets_len", 0))
    return bytes(hdr)


def parse_asset_arg(spec: str) -> tuple[str, Path]:
    if "=" in spec:
        rel, path = spec.split("=", 1)
        return check_relpath(rel), Path(path)
    p = Path(spec)
    return check_relpath(p.as_posix()), p


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest", required=True, type=Path, help="app.toml -> manifest.json input (JSON file)")
    ap.add_argument("--wasm", required=True, type=Path)
    ap.add_argument("--aot", type=Path, default=None)
    ap.add_argument("--aot-target", default="xtensa-esp32s3", choices=sorted(AOT_TARGETS))
    ap.add_argument("--icon", type=Path, default=None)
    ap.add_argument("--asset", action="append", default=[], metavar="REL=PATH",
                    help="extra file bundled into the ASTR assets blob; repeatable. "
                         "REL is the install path under assets/")
    ap.add_argument("--sign", type=Path, default=None, metavar="PEM",
                    help="ECDSA-P256 private key PEM; appends a 64B raw r||s "
                    "signature over the whole file (flags bit0 set). Without "
                    "--sign the package is unsigned (device policy decides).")
    ap.add_argument("-o", "--out", required=True, type=Path)
    args = ap.parse_args(argv)

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    wasm = args.wasm.read_bytes()
    if wasm[:4] != b"\x00asm":
        print("error: wasm file has no \\0asm magic", file=sys.stderr)
        return 1
    manifest["wasm_sha256"] = hashlib.sha256(wasm).hexdigest()

    aot = args.aot.read_bytes() if args.aot else b""
    if aot:
        if aot[:4] != b"\x00aot":
            print("error: aot file has no \\0aot magic", file=sys.stderr)
            return 1
        manifest["aot_sha256"] = hashlib.sha256(aot).hexdigest()
        manifest["aot_target"] = args.aot_target

    icon = args.icon.read_bytes() if args.icon else b""

    assets_files: list[tuple[str, bytes]] = []
    seen: set[str] = set()
    for spec in args.asset:
        rel, path = parse_asset_arg(spec)
        if rel in seen:
            print(f"error: duplicate asset {rel}", file=sys.stderr)
            return 1
        if not path.is_file():
            print(f"error: asset missing {path}", file=sys.stderr)
            return 1
        seen.add(rel)
        assets_files.append((rel, path.read_bytes()))
    assets = build_mjr1(assets_files) if assets_files else b""

    # manifest first so sha fields land inside it
    man_bytes = json.dumps(manifest, separators=(",", ":"), ensure_ascii=False).encode("utf-8")

    hdr_size = HDR_SIZE_ASSETS if assets else HDR_SIZE
    off = hdr_size
    man_off, man_len = off, len(man_bytes)
    off += pad4(man_len)
    wasm_off, wasm_len = off, len(wasm)
    off += pad4(wasm_len)
    aot_off, aot_len = (off, len(aot)) if aot else (0, 0)
    off += pad4(aot_len)
    icon_off, icon_len = (off, len(icon)) if icon else (0, 0)
    off += pad4(icon_len)
    assets_off, assets_len = (off, len(assets)) if assets else (0, 0)

    flags = 0
    if args.sign:
        flags |= FLAG_SIGNED
    if assets:
        flags |= FLAG_HAS_ASSETS

    hdr = build_header({
        "manifest_off": man_off, "manifest_len": man_len,
        "wasm_off": wasm_off, "wasm_len": wasm_len,
        "aot_off": aot_off, "aot_len": aot_len,
        "aot_target": AOT_TARGETS[args.aot_target] if aot else 0,
        "icon_off": icon_off, "icon_len": icon_len,
        "assets_off": assets_off, "assets_len": assets_len,
        "flags": flags,
    }, hdr_size)

    out = bytearray(hdr)
    out += man_bytes + b"\0" * (pad4(man_len) - man_len)
    out += wasm + b"\0" * (pad4(wasm_len) - wasm_len)
    if aot:
        out += aot + b"\0" * (pad4(aot_len) - aot_len)
    if icon:
        out += icon + b"\0" * (pad4(icon_len) - icon_len)
    if assets:
        out += assets + b"\0" * (pad4(assets_len) - assets_len)

    signed_note = "unsigned"
    if args.sign:
        from cryptography.hazmat.primitives import serialization, hashes
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import (
            decode_dss_signature)
        key = serialization.load_pem_private_key(args.sign.read_bytes(), None)
        der = key.sign(bytes(out), ec.ECDSA(hashes.SHA256()))
        r, s = decode_dss_signature(der)
        out += r.to_bytes(32, "big") + s.to_bytes(32, "big")
        signed_note = "signed (ECDSA-P256)"

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(out))
    print(f"packed {args.out}: {len(out)} bytes {signed_note} "
          f"(hdr {hdr_size}, manifest {man_len}, wasm {wasm_len}, aot {aot_len}, "
          f"icon {icon_len}, assets {assets_len})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
