#!/usr/bin/env python3
"""Offline sanity check of a .ast against the firmware's runtime rules.

Validates, without a device:
  * ASTP header (magic/version/bounds, same checks as services/aster_pkg.c)
  * manifest JSON + required fields + ABI major
  * optional sha256 cross-checks
  * wasm import set == firmware "env" natives (runtime/aster_native.c)
                 + libc-builtin (WAMR, WAMR_BUILD_LIBC_BUILTIN=1, WASI off)
  * required lifecycle exports
Import names are read from the versioned contract bundled with this SDK.
"""
import json
import os
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aster_board  # noqa: E402
import pack_ast  # noqa: E402
from runtime_paths import sdk_root

SDK_ROOT = sdk_root()
CONTRACT = json.loads((SDK_ROOT / "contract" / "api.json").read_text())

REQUIRED_EXPORTS = ["aster_app_init", "aster_app_start", "aster_app_stop",
                    "aster_app_event"]

_cache = {}


def firmware_abi_major() -> str:
    """ASTER_ABI_MAJOR from abi/include/aster/system.h (source of truth)."""
    if "abi_major" not in _cache:
        txt = (SDK_ROOT / "include" / "aster" / "system.h").read_text()
        m = re.search(r"#define\s+ASTER_ABI_MAJOR\s+(\d+)", txt)
        assert m, "ASTER_ABI_MAJOR not found in system.h"
        _cache["abi_major"] = m.group(1)
    return _cache["abi_major"]


def expected_aot_id(board=None) -> int:
    """The AOT target id `board`'s firmware will accept, as a number.

    Derived, not written down: board preset -> wamrc backend -> pack_ast's
    AOT_TARGETS -> the id aster_pkg.c compares against CONFIG_ASTER_AOT_TARGET_ID.
    This used to be a literal `assert at == 1`, which was true only while
    Xtensa was the only port and said nothing useful when it fired.

    Residual gap, closed in M1 by check_models.py: this chains through the
    *preset*, while the device follows CONFIG_ASTER_CPU_ARCH_* in the board conf
    file. If those two ever disagree, this check passes and the device still
    refuses the AOT -- which is now loud on-device (see aster_pkg.c) rather than
    silent, but is not caught here.
    """
    backend = aster_board.get(board)["aot_target"]
    return pack_ast.AOT_TARGETS[aster_board.pack_key_for(backend)]


def uleb(b, i):
    r = 0
    s = 0
    while True:
        x = b[i]
        i += 1
        r |= (x & 0x7F) << s
        if not (x & 0x80):
            return r, i
        s += 7


def wasm_sections(b):
    """yield (section_id, payload bytes)"""
    assert b[:4] == b"\x00asm", "not a wasm module"
    i = 8
    while i < len(b):
        sid = b[i]
        i += 1
        size, i = uleb(b, i)
        yield sid, b[i:i + size]
        i += size


def wasm_io_symbols(wasm):
    """return (imports, exports) as lists of (module, name, kind) / name"""
    imports, exports = [], []
    for sid, payload in wasm_sections(wasm):
        if sid == 2:  # import
            j = 0
            cnt, j = uleb(payload, j)
            for _ in range(cnt):
                ml, j = uleb(payload, j)
                mod = payload[j:j + ml].decode("utf8", "replace")
                j += ml
                nl, j = uleb(payload, j)
                nm = payload[j:j + nl].decode("utf8", "replace")
                j += nl
                kind = payload[j]
                j += 1
                if kind == 0:      # func
                    _, j = uleb(payload, j)
                elif kind == 1:    # table
                    j += 2 if payload[j] & 1 else 1
                    j += 1
                elif kind == 2:    # memory
                    j += 2 if payload[j] & 1 else 1
                elif kind == 3:    # global
                    j += 2
                imports.append((mod, nm, kind))
        elif sid == 7:  # export
            j = 0
            cnt, j = uleb(payload, j)
            for _ in range(cnt):
                nl, j = uleb(payload, j)
                exports.append(payload[j:j + nl].decode("utf8", "replace"))
                j += nl + 2  # kind byte + index
    return imports, exports


def allowed_env_imports():
    """Names from the pinned release contract, independent of firmware sources."""
    return set(CONTRACT["imports"]["env"])


def check(path, pub_pem=None, board=None):
    if not __debug__:
        raise RuntimeError("Package verification requires Python without -O")
    target = aster_board.get(board)
    b = Path(path).read_bytes()
    assert b[0:4] == b"ASTP", "magic"
    fmt_ver, hdr_size = struct.unpack_from("<HH", b, 4)
    assert fmt_ver == 1 and hdr_size in (64, 72), f"ver/size {fmt_ver}/{hdr_size}"
    flags = struct.unpack_from("<I", b, 8)[0]
    signed = bool(flags & 1)
    if signed:
        assert len(b) >= hdr_size + 64, "signed but file too small"
        total = len(b) - 64   # sections must fit BEFORE the sig block
    else:
        total = len(b)
    mo, ml = struct.unpack_from("<II", b, 12)
    wo, wl = struct.unpack_from("<II", b, 20)
    ao, al, at = struct.unpack_from("<III", b, 28)
    io, il = struct.unpack_from("<II", b, 40)
    # 56..63 reserved (was ui_off/ui_len, UIB1 dropped in ABI 3.0) -- must be 0
    assert struct.unpack_from("<II", b, 56) == (0, 0), "reserved bytes 56..63 must be 0"
    aso = asl = 0
    if hdr_size >= 72:
        aso, asl = struct.unpack_from("<II", b, 64)

    def bounds(o, l, base):
        return o >= base and o + l <= total

    assert ml and bounds(mo, ml, hdr_size), "manifest bounds"
    assert wl >= 8 and bounds(wo, wl, hdr_size), "wasm bounds"
    assert b[wo:wo + 4] == b"\x00asm", "wasm magic"
    assert not al or (bounds(ao, al, hdr_size) and b[ao:ao + 4] == b"\x00aot"), "aot"
    assert not il or bounds(io, il, hdr_size), "icon bounds"
    n_assets = 0
    if asl:
        assert hdr_size == 72, "assets require hdr_size 72"
        assert bounds(aso, asl, hdr_size) and b[aso:aso + 4] == b"ASTR", "assets bounds/magic"
        n_assets = struct.unpack_from("<H", b, aso + 6)[0]
        assert n_assets <= 64, "too many assets"

    man = json.loads(b[mo:mo + ml].decode("utf-8"))
    for k in ("id", "name", "abi"):
        assert k in man, f"manifest.{k}"
    if "description" in man:
        description = man["description"]
        assert (isinstance(description, str) and
                len(description.encode("utf-8")) <= 127 and
                not any(ord(c) < 32 or ord(c) == 127 for c in description)), (
                    "manifest.description must be single-line text of at most 127 UTF-8 bytes")
    # "orientation" is optional -- absent means "follow the panel", the only
    # way to ask for either one. A value that is neither spelling is DROPPED on
    # the device (aster_pkg_norm_orientation warns, returns ASTER_ORIENT_ANY), and
    # a dropped key looks exactly like a package that never carried one: the
    # app follows the panel instead of failing. So the typo has to be caught
    # here, where it is still visible.
    if "orientation" in man:
        assert man["orientation"] in aster_board.ORIENTATIONS, (
            f"manifest.orientation {man['orientation']!r} is not one of "
            f"{sorted(aster_board.ORIENTATIONS)}; the device would drop it "
            f"(app follows the panel) rather than reject the package")
    # abi major must equal the firmware's, mirroring the install gate in
    # services/aster_pkg.c. Read it from the header single source of truth.
    assert man["abi"]["min"].split(".")[0] == firmware_abi_major(), "abi major"

    import hashlib
    if "wasm_sha256" in man:
        assert hashlib.sha256(b[wo:wo + wl]).hexdigest() == man["wasm_sha256"], "wasm sha"
    if al and "aot_sha256" in man:
        assert hashlib.sha256(b[ao:ao + al]).hexdigest() == man["aot_sha256"], "aot sha"
    if al:
        want = expected_aot_id(board)
        assert at == want, (
            f"aot target {at}, but this board's firmware only loads {want} "
            f"({aster_board.get(board)['aot_target']}) -- install would drop the "
            "aot and run the app interpreted")

    from target_checks import validate_manifest
    validate_manifest(man, target)
    imports, exports = wasm_io_symbols(b[wo:wo + wl])
    missing = [e for e in REQUIRED_EXPORTS if e not in exports]
    assert not missing, f"missing exports: {missing}"

    allow = allowed_env_imports()
    if "supported_imports" in target:
        allow = {n for n in allow if not n.startswith("aster_")} | set(target["supported_imports"])
    bad = []
    for mod, nm, kind in imports:
        if kind != 0:  # only function imports are resolvable here
            bad.append(f"{mod}.{nm}(kind {kind})")
        elif mod != "env":
            bad.append(f"{mod}.{nm}")
        elif nm not in allow:
            bad.append(f"env.{nm}")
    assert not bad, f"imports not provided by firmware: {bad}"

    n_aster = sum(1 for m, n, k in imports if m == "env" and n.startswith("aster_"))
    n_libc = len(imports) - n_aster
    print(f"OK  {path}")
    print(f"    id={man['id']} name={man['name']} ver={man.get('version')} "
          f"abi={man['abi']} requires={man.get('requires', [])}")
    print(f"    manifest={ml}B wasm={wl}B aot={al}B(target {at}) icon={il}B "
          f"assets={asl}B({n_assets} files) hdr={hdr_size} total={len(b)}B")
    print(f"    imports: {n_aster} aster_* + {n_libc} libc-builtin (all resolvable), "
          f"exports: {len(exports)}")
    if signed:
        state = verify_sig(b[:len(b) - 64], b[len(b) - 64:], pub_pem)
        print(f"    signature: {state}")
        assert state.startswith("VALID"), "signature check failed"
    else:
        print("    signature: none")
    return man


def verify_sig(region, sig, pub_pem):
    try:
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import (
            encode_dss_signature)
        from cryptography.hazmat.primitives import hashes
        from cryptography.exceptions import InvalidSignature
    except ImportError:
        return "UNVERIFIED (pip install cryptography)"
    if pub_pem:
        from cryptography.hazmat.primitives import serialization
        pub = serialization.load_pem_public_key(
            open(pub_pem, "rb").read())
    else:
        pub = ec.EllipticCurvePublicKey.from_encoded_point(
            ec.SECP256R1(), DEFAULT_PUB)
    try:
        pub.verify(encode_dss_signature(int.from_bytes(sig[:32], "big"),
                                        int.from_bytes(sig[32:], "big")),
                   region, ec.ECDSA(hashes.SHA256()))
        return "VALID"
    except InvalidSignature:
        return "INVALID"


# Development trust anchor -- keep in sync with firmware/Kconfig
# CONFIG_ASTER_VENDOR_PUBKEY (sdk/keys/vendor_dev.pem).
DEFAULT_PUB = bytes.fromhex(
    "04eb4e34f79d32219ad5f027d5e6b448f1f5cb44b1857e246867f7f10b980aa6d6"
    "61ddba440f1a9675d8c5832b36cec9bd688890a009170ccf717d393145f0162e")


if __name__ == "__main__":
    args = sys.argv[1:]
    pub = board = None
    while args and args[0].startswith("--"):
        if args[0] == "--pubkey":
            pub, args = args[1], args[2:]
        elif args[0] == "--target":
            board, args = args[1], args[2:]
        else:
            sys.exit(f"unknown option {args[0]}")
    if not args:
        sys.exit(f"usage: {sys.argv[0]} [--pubkey pub.pem] [--target NAME] "
                 "<file.ast> [...]")
    for p in args:
        check(p, pub, board)
