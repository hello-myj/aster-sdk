#!/usr/bin/env python3
"""ASTER app build driver: app.toml -> app.wasm -> (default AOT) -> .ast

Usage (from the sdk/ directory or anywhere):
  python3 tools/build.py apps/gfxshow --target xtensa # wasm + AOT (app.toml aot=true)
  python3 tools/build.py apps/gfxshow --target xtensa --no-aot
  python3 tools/build.py apps/gfxshow --target xtensa # small AOT
  python3 tools/build.py --all --target xtensa
  python3 tools/build.py --clean

The app declares itself in app.toml:

  [app]
  id       = "aster.gfxshow"     # reverse-DNS, becomes the install dir name
  name     = "Hello ASTER"
  version  = "1.0.0"
  abi      = "3.0.0"            # ABI *target* the app was built against
  abi_min  = "3.0"              # lowest device ABI that can run it. Its MAJOR
                                # must equal the firmware's: services/aster_pkg.c
                                # rejects anything else at install (ASTER_EABI).
                                # Both default to this SDK's own ABI, parsed
                                # from abi/include/aster/system.h.
  aot      = true               # default; LVGL-in-wasm apps must set false
  icon     = "icon.bin"         # optional ASTI launcher tile (packed into .ast)
  category = "tool"             # optional: game / tool / system
  orientation = "portrait"      # optional: portrait / landscape. Absent = the
                                # app follows whatever the panel is. That
                                # absence is the ONLY way to ask for "either":
                                # there is no "any" spelling, so a typo cannot
                                # silently become a request -- the device drops
                                # an unknown value with a warning instead
                                # (firmware/services/aster_pkg.c)
  optimize = "O2"               # optional: O0..O3 / Os / Oz
  stack    = 16384              # optional: wasm shadow stack bytes
  initial_memory = 262144       # optional: wasm linear memory bytes
  max_memory     = 524288       # optional
  requires   = ["display"]      # optional: hard capability deps (install fails if absent)
  permissions = ["fs"]          # optional: enforced since ABI 1.2 (fs/kv/appmgr/sd)
  sources = ["src/main.c"]      # optional: defaults to every .c under src/
"""
from __future__ import annotations

import argparse
import json
import os
import platform as _platform
import re
import shutil
import subprocess
import sys
import sysconfig
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aster_board  # noqa: E402
from runtime_paths import sdk_root, output_root

SDK_ROOT = sdk_root()
LIB_DIR = SDK_ROOT / "lib" / "aster"
INCLUDE_DIR = SDK_ROOT / "include"
APPS_DIR = SDK_ROOT / "apps"
BUILD_ROOT = output_root() / "build"
OUT_DIR = output_root() / "dist"

# Toolchain search order: ASTER_WASI_SDK/ASTER_WAMRC env -> host-specific
# bundled copy -> /opt/wasi-sdk -> system PATH.
BUNDLED_TOOLCHAIN = SDK_ROOT / "toolchain"

# Native tools are bundled for x86-64 Linux in the source tree and for
# x86-64 Windows by tools/package_windows.ps1.
BUNDLED_USABLE = ((_platform.system() == "Linux" and _platform.machine().lower() in ("x86_64", "amd64")) or
                  (os.name == "nt" and sysconfig.get_platform().lower() == "win-amd64"))

WAMRC_BUILD_HINT = (
    "wamrc (native AOT compiler) not found. Install the host-specific "
    "toolchain or set ASTER_WAMRC to a compatible WAMR 2.4.3 executable. "
    "See toolchain/README.md."
)

# The wamrc-backend -> package-target map lives in aster_board.AOT_BACKENDS, and
# --aot-target defaults to the selected board preset's backend. Both used to be
# duplicated here; see that map for why a silent fallback was the wrong answer.

LIFECYCLE_EXPORTS = [
    "aster_app_init",
    "aster_app_start",
    "aster_app_pause",
    "aster_app_resume",
    "aster_app_stop",
    "aster_app_destroy",
    "aster_app_event",
]

VALID_CAPS = {
    "display", "touch", "mouse", "keyboard", "camera", "audio-in", "audio-out",
    "wifi", "bluetooth", "accelerometer", "gyroscope", "filesystem", "storage.app",
    "rtc", "psram", "framebuffer", "network",
}
VALID_CATEGORIES = {"game", "tool", "system"}
# The spellings live in aster_board.ORIENTATIONS -- one copy, shared with
# verify_ast.py (which cannot import this file: tomllib needs 3.11, and the
# verifier has to run anywhere). Two values and no "any": an absent key is how
# a package says "follow the panel" (see ASTER_ORIENT_ANY in aster_pkg.h).
VALID_ORIENTATIONS = aster_board.ORIENTATIONS
VALID_PERMS = {"fs", "kv", "appmgr", "sd", "wifi", "net"}


# ---------- toolchain discovery (env -> repo-bundled -> /opt -> PATH) ----------

def find_clang() -> Path:
    env = os.environ.get("ASTER_WASI_SDK")
    cands = []
    if env:
        cands.append(Path(env) / "bin" / ("clang.exe" if os.name == "nt" else "clang"))
    if BUNDLED_USABLE:
        host = "windows" if os.name == "nt" else "linux"
        name = "clang.exe" if os.name == "nt" else "clang"
        cands.append(BUNDLED_TOOLCHAIN / f"wasi-sdk-24.0-x86_64-{host}" / "bin" / name)
    cands.append(Path("/opt/wasi-sdk/bin/clang"))
    for c in cands:
        if c.is_file():
            return c
    which = shutil.which("clang")
    if which:
        return Path(which)
    sys.exit("error: wasi-sdk clang not found (set ASTER_WASI_SDK or install "
             "the host-specific Aster toolchain)")


def find_wamrc(backend: str | None = None) -> Path | None:
    """Locate wamrc. An SDK that serves several CPUs keeps one compiler per
    backend under toolchain/backends/<backend>/; --target already names the
    backend, so the right one is picked without any user choice."""
    env = os.environ.get("ASTER_WAMRC")
    cands = []
    if env:
        cands.append(Path(env))
    if BUNDLED_USABLE:
        exe = "wamrc.exe" if os.name == "nt" else "wamrc"
        if backend:
            cands.append(BUNDLED_TOOLCHAIN / "backends" / backend / exe)
        cands.append(BUNDLED_TOOLCHAIN / exe)
    for c in cands:
        if c.is_file():
            return c
    return shutil.which("wamrc")


def bundled_wamrcs() -> dict[str, Path]:
    """backend -> bundled compiler, for doctor output in multi-CPU SDKs."""
    if not BUNDLED_USABLE:
        return {}
    exe = "wamrc.exe" if os.name == "nt" else "wamrc"
    root = BUNDLED_TOOLCHAIN / "backends"
    if not root.is_dir():
        return {}
    return {p.name: p / exe for p in sorted(root.iterdir()) if p.is_dir() and (p / exe).is_file()}


def find_wasm_opt() -> Path | None:
    cands = []
    if BUNDLED_USABLE:
        cands.append(BUNDLED_TOOLCHAIN / "binaryen" / "bin" /
                     ("wasm-opt.exe" if os.name == "nt" else "wasm-opt"))
    for c in cands:
        if c.is_file():
            return c
    return shutil.which("wasm-opt")


def find_builtins(clang: Path) -> list[Path]:
    """libclang_rt.builtins-wasm32.a + libm.a when -lc is used."""
    root = clang.resolve().parent.parent
    out: list[Path] = []
    clang_lib = root / "lib" / "clang"
    if clang_lib.is_dir():
        for ver in sorted(clang_lib.iterdir(), reverse=True):
            for sub in ("wasm32", "wasm32-unknown-wasi", "."):
                p = ver / "lib" / sub / "libclang_rt.builtins-wasm32.a"
                if p.is_file():
                    out.append(p)
                    break
            if out:
                break
    return out


# ---------- ABI version ----------

# The ABI this SDK builds against, parsed from the pinned SDK contract
# (abi/include/aster/system.h -- same header the firmware compiles and
# verify_ast.py checks), using only the SDK's released copy. Never
# hard-code it: services/aster_pkg.c requires an app's abi_min *major* to EQUAL
# the firmware major, so a stale literal here ships packages the device
# refuses with ASTER_EABI(-13).
ABI_HEADERS = (
    INCLUDE_DIR / "aster" / "system.h",
)
_sdk_abi: tuple[str, str] | None = None


def sdk_abi() -> tuple[str, str]:
    """(target, min) e.g. ("3.0.0", "3.0") from ASTER_ABI_{MAJOR,MINOR,PATCH}."""
    global _sdk_abi
    if _sdk_abi is not None:
        return _sdk_abi
    for h in ABI_HEADERS:
        if not h.is_file():
            continue
        txt = h.read_text(encoding="utf-8", errors="replace")

        def num(name: str) -> str | None:
            m = re.search(rf"#define\s+{name}\s+(\d+)", txt)
            return m.group(1) if m else None

        maj = num("ASTER_ABI_MAJOR")
        if maj is not None:
            mi, pa = num("ASTER_ABI_MINOR") or "0", num("ASTER_ABI_PATCH") or "0"
            _sdk_abi = (f"{maj}.{mi}.{pa}", f"{maj}.{mi}")
            return _sdk_abi
    sys.exit("error: ASTER_ABI_MAJOR not found in "
             + " or ".join(str(h) for h in ABI_HEADERS))


# ---------- app config ----------

class App:
    def __init__(self, app_dir: Path):
        toml = app_dir / "app.toml"
        if not toml.is_file():
            sys.exit(f"error: {toml} missing")
        cfg = tomllib.loads(toml.read_text(encoding="utf-8"))
        a = cfg.get("app", cfg)
        self.dir = app_dir
        self.id: str = a.get("id") or sys.exit(f"error: {toml}: app.id required")
        self.name: str = a.get("name", self.id)
        self.description = a.get("description", "")
        if (not isinstance(self.description, str) or
                len(self.description.encode("utf-8")) > 127 or
                any(ord(c) < 32 or ord(c) == 127 for c in self.description)):
            sys.exit(f"error: {toml}: description must be single-line text "
                     "of at most 127 UTF-8 bytes")
        self.version: str = str(a.get("version", "0.0.0"))
        target, min_abi = sdk_abi()
        self.abi: str = str(a.get("abi", target))
        # abi_min = lowest runtime that can run this app, defaulting to this
        # SDK's ABI (major.minor). An app that hard-imports a newer symbol
        # (e.g. appmgr/*) must bump it so older devices reject at install, not
        # trap at launch. The install gate compares the *major* for equality,
        # so a min whose major differs from the target's can never install.
        self.abi_min: str = str(a.get("abi_min", min_abi))
        if self.abi_min.split(".")[0] != self.abi.split(".")[0]:
            sys.exit(f"error: {toml}: abi_min {self.abi_min!r} major must equal "
                     f"abi {self.abi!r} major (services/aster_pkg.c compares "
                     f"them for equality at install)")
        self.optimize: str = str(a.get("optimize", "O2"))
        self.stack: int = int(a.get("stack", 16384))
        self.initial_memory: int = int(a.get("initial_memory", 262144))
        self.max_memory: int = int(a.get("max_memory", 0) or 0)
        self.heap: int = int(a.get("heap", 0) or 0)
        self.requires: list[str] = list(a.get("requires", []))
        self.optional: list[str] = list(a.get("optional", []))
        self.permissions: list[str] = list(a.get("permissions", []))
        self.entry: str = a.get("entry", "")
        # AOT is the product default for small business apps (ABI 1.5+).
        # LVGL-in-wasm leftovers must set aot = false (AOT text will not
        # fit the ~96 KB internal IRAM exec window on this class of SoC).
        self.aot: bool = bool(a.get("aot", True))
        self.assets: list[str] = list(a.get("assets", []))
        self.icon: str = str(a.get("icon", "") or "")
        self.category: str = str(a.get("category", "") or "")
        if self.category and self.category not in VALID_CATEGORIES:
            sys.exit(f"error: {toml}: category must be one of "
                     f"{sorted(VALID_CATEGORIES)}")
        self.orientation: str = str(a.get("orientation", "") or "")
        if self.orientation and self.orientation not in VALID_ORIENTATIONS:
            sys.exit(f"error: {toml}: orientation must be one of "
                     f"{sorted(VALID_ORIENTATIONS)} or absent (follow the "
                     f"panel)")
        for p in self.permissions:
            if p not in VALID_PERMS:
                sys.exit(f"error: {toml}: unknown permission {p!r} "
                         f"(want {sorted(VALID_PERMS)})")
        # Optional [build] table: extra inputs for vendored C libraries.
        # Relative entries resolve against the app dir. Apps draw through the
        # gfx engine (abi/include/aster/gfx.h) instead of linking LVGL.
        b = cfg.get("build", {})
        self.source_roots: list[Path] = [(app_dir / p).resolve()
                                         for p in b.get("source_roots", [])]
        self.includes: list[Path] = [(app_dir / p).resolve()
                                     for p in b.get("includes", [])]
        self.defines: list[str] = list(b.get("defines", []))
        self.extra_cflags: list[str] = list(b.get("cflags", []))
        self.extra_ldflags: list[str] = list(b.get("ldflags", []))
        srcs = a.get("sources")
        if srcs:
            self.sources = [app_dir / s for s in srcs]
        else:
            self.sources = sorted((app_dir / "src").rglob("*.c"))
        if not self.sources:
            sys.exit(f"error: {app_dir}: no C sources found")
        bad = [r for r in self.requires if r not in VALID_CAPS]
        if bad:
            sys.exit(f"error: {toml}: unknown requires {bad} (valid: {sorted(VALID_CAPS)})")


def run(cmd: list[str], what: str) -> None:
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"[FAIL] {what}\n$ {' '.join(cmd)}\n{r.stderr or r.stdout}", file=sys.stderr)
        sys.exit(1)
    if r.stderr.strip():
        print(f"[{what}] warnings:\n{r.stderr.strip()}")


# ---------- build steps ----------

def _flat_obj(obj_dir: Path, src: Path, roots: list[Path]) -> Path:
    """Deterministic, collision-free object name for a source under a root.

    Vendored libraries (LVGL) have many identical basenames, so `src.stem`
    collides. Map a source to <root-rel-path-with-separators-as-_>.o when it
    sits under a declared source_root; else fall back to the parent-dir tag.
    """
    for r in roots:
        try:
            rel = src.relative_to(r)
        except ValueError:
            continue
        tag = "_".join(rel.parts[:-1] + (rel.stem,))
        return obj_dir / (tag + ".o")
    return obj_dir / (src.stem + ".o")


def build_wasm(app: App, obj_dir: Path, wasm_path: Path) -> None:
    clang = find_clang()
    cflags = [
        str(clang), "--target=wasm32-unknown-unknown",
        f"-{app.optimize}", "-ffunction-sections", "-fdata-sections",
        "-Wall", "-Wextra", "-nostdlib", "-ffreestanding", "-fvisibility=hidden",
        "-isystem", str(INCLUDE_DIR), "-I", str(LIB_DIR),
    ]
    for inc in app.includes:
        cflags += ["-I", str(inc)]
    for d in app.defines:
        cflags += ["-D", d]
    cflags += app.extra_cflags

    # Gather sources: explicit app.sources, plus every .c under source_roots.
    sources = list(app.sources)
    for root in app.source_roots:
        sources += sorted(root.rglob("*.c"))
    # de-dup, preserve order
    seen, uniq = set(), []
    for s in sources:
        if s not in seen:
            seen.add(s)
            uniq.append(s)
    sources = uniq

    objects: list[Path] = []
    obj_dir.mkdir(parents=True, exist_ok=True)
    for src in sources + [LIB_DIR / name for name in ("aster_printf.c", "aster_gfx.c", "aster_scene.c", "aster_layout.c", "aster_widgets.c")]:
        obj = _flat_obj(obj_dir, src, app.source_roots)
        obj.parent.mkdir(parents=True, exist_ok=True)
        run(cflags + ["-c", "-o", str(obj), str(src)], f"cc {src.name}")
        objects.append(obj)
    crt = obj_dir / "crt.o"
    run(cflags + ["-c", "-o", str(crt), str(LIB_DIR / "crt.c")], "cc crt.c")

    # ar + whole-archive keeps the weak crt defaults without pulling in the
    # rest of libc; --allow-undefined lets the aster_* env imports stay imports.
    crt_a = obj_dir / "libcrt.a"
    llvm_ar = clang.parent / ("llvm-ar.exe" if os.name == "nt" else "llvm-ar")
    if not llvm_ar.is_file():
        llvm_ar = Path(shutil.which("llvm-ar") or "llvm-ar")
    run([str(llvm_ar), "rcs", str(crt_a), str(crt)], "ar")

    ldflags = [
        str(clang), "--target=wasm32-unknown-unknown", f"-{app.optimize}",
        "-nostdlib", "-ffreestanding",
        "-Wl,--gc-sections", "-Wl,--strip-all", "-Wl,--no-entry",
        "-Wl,--allow-undefined",
        "-z", f"stack-size={app.stack}",
        f"-Wl,--initial-memory={app.initial_memory}",
        "-Wl,--export=__heap_base", "-Wl,--export=__data_end",
        "-Wl,--whole-archive", str(crt_a), "-Wl,--no-whole-archive",
    ]
    if app.max_memory:
        ldflags.append(f"-Wl,--max-memory={app.max_memory}")
    for e in LIFECYCLE_EXPORTS:
        if e == "aster_app_init":
            ldflags.append(f"-Wl,--export={e}")
        else:
            ldflags.append(f"-Wl,--export-if-defined={e}")
    if app.entry:
        ldflags.append(f"-Wl,--export={app.entry}")
    ldflags += ["-o", str(wasm_path)] + [str(o) for o in objects]
    run(ldflags, "link")

    # verify every required export really landed
    nm = clang.parent / ("llvm-nm.exe" if os.name == "nt" else "llvm-nm")
    if nm.is_file():
        r = subprocess.run([str(nm), str(wasm_path)], capture_output=True, text=True)
        exported = set()
        for line in (r.stdout or "").splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[1] in ("T", "E"):
                exported.add(parts[2])
        missing = [e for e in ("aster_app_init", "aster_app_event") if e not in exported]
        if missing:
            sys.exit(f"error: wasm missing required exports: {missing}")


def opt_wasm(wasm_path: Path) -> None:
    wo = find_wasm_opt()
    if not wo:
        return
    tmp = wasm_path.with_suffix(".opt.wasm")
    r = subprocess.run([str(wo), "-Oz", "--enable-bulk-memory", "--enable-sign-ext",
                        "--enable-mutable-globals", "--strip-debug",
                        "-o", str(tmp), str(wasm_path)],
                       capture_output=True, text=True)
    if r.returncode == 0 and tmp.stat().st_size < wasm_path.stat().st_size:
        tmp.replace(wasm_path)
    else:
        tmp.unlink(missing_ok=True)


def build_aot(app: App, wasm_path: Path, aot_path: Path, target: str = "xtensa") -> bool:
    wamrc = find_wamrc(target)
    if not wamrc:
        print(f"[{app.id}] wamrc not found; skipping AOT")
        print(f"  {WAMRC_BUILD_HINT}")
        raise SystemExit("AOT requested but wamrc unavailable; use --no-aot explicitly")
    run([str(wamrc), f"--target={target}", "--size-level=s",
         "-o", str(aot_path), str(wasm_path)], "wamrc")
    return True


def make_manifest(app: App) -> dict:
    m: dict = {
        "id": app.id,
        "product": "aster",
        "target_id": app.target["id"],
        "name": app.name,
        "version": app.version,
        "abi": {"min": app.abi_min, "target": app.abi},
        "memory": {
            "initial": app.initial_memory,
            "stack": app.stack,
        },
    }
    if app.description:
        m["description"] = app.description
    if app.max_memory:
        m["memory"]["max"] = app.max_memory
    if app.heap:
        m["memory"]["heap"] = app.heap
    if app.entry:
        m["entry"] = app.entry
    if app.requires:
        m["requires"] = app.requires
    if app.optional:
        m["optional"] = app.optional
    if app.permissions:
        m["permissions"] = app.permissions
    if app.category:
        m["category"] = app.category
    if app.orientation:
        m["orientation"] = app.orientation
    return m


def collect_assets(app: App) -> list[tuple[str, Path]]:
    """app.toml `assets = ["icons/a.bin", ...]` -> [(rel, path)] bundled as ASTR.

    Rel paths are install paths under <appdir>/assets/ (see aster_storage.c).
    """
    found: list[tuple[str, Path]] = []
    seen: set[str] = set()
    root = app.dir.resolve()

    for rel in app.assets:
        rel = rel.replace("\\", "/").strip()
        if not rel or rel in seen:
            continue
        seen.add(rel)
        path = (app.dir / rel).resolve()
        try:
            path.relative_to(root)
        except ValueError:
            sys.exit(f"error: {app.id}: asset escapes app dir: {rel}")
        if not path.is_file():
            sys.exit(f"error: {app.id}: asset missing: {rel}")
        found.append((rel, path))
    return found


def build_app(app: App, do_aot: bool, aot_target: str = "xtensa") -> Path:
    out = BUILD_ROOT / app.id
    wasm_path = out / "app.wasm"
    print(f"[build] {app.id} v{app.version} ({len(app.sources)} src)")
    shutil.rmtree(out, ignore_errors=True)
    build_wasm(app, out / "obj", wasm_path)
    opt_wasm(wasm_path)
    print(f"  wasm: {wasm_path.stat().st_size} bytes")

    assets = collect_assets(app)
    if assets:
        print(f"  assets: {len(assets)} file(s)")

    aot_path = None
    if do_aot and build_aot(app, wasm_path, out / "app.aot", aot_target):
        aot_path = out / "app.aot"
        print(f"  aot:  {aot_path.stat().st_size} bytes")

    manifest_path = out / "manifest.json"
    manifest_path.write_text(json.dumps(make_manifest(app), ensure_ascii=False),
                             encoding="utf-8")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    pkg = OUT_DIR / f"{app.id}.ast"
    cmd = ["--manifest", str(manifest_path), "--wasm", str(wasm_path), "-o", str(pkg)]
    sign_key = os.environ.get("ASTER_SIGN_KEY")  # PEM path; unset -> unsigned
    if sign_key:
        cmd += ["--sign", sign_key]
    if aot_path:
        # exits on an unknown backend rather than packing the wrong CPU's AOT
        cmd += ["--aot", str(aot_path),
                "--aot-target", aster_board.pack_key_for(aot_target)]
    for rel, path in assets:
        cmd += ["--asset", f"{rel}={path}"]
    if app.icon:
        icon = (app.dir / app.icon).resolve()
        try:
            icon.relative_to(app.dir.resolve())
        except ValueError:
            sys.exit(f"error: {app.id}: icon escapes app dir: {app.icon}")
        if not icon.is_file():
            sys.exit(f"error: {app.id}: icon missing: {icon}")
        if icon.read_bytes()[:4] != b"ASTI":
            sys.exit(f"error: {app.id}: icon is not ASTI: {icon}")
        cmd += ["--icon", str(icon)]
    import pack_ast
    if pack_ast.main(cmd) != 0:
        raise SystemExit("package creation failed")
    return pkg


def main(argv=None) -> int:
    global BUILD_ROOT, OUT_DIR
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("apps", nargs="*", type=Path, help="app directories (contain app.toml)")
    ap.add_argument("--all", action="store_true", help="build every app under apps/")
    ap.add_argument("--aot", action="store_true",
                    help="force native AOT via wamrc (overrides app.toml aot=false)")
    ap.add_argument("--no-aot", action="store_true",
                    help="skip AOT even if app.toml sets aot=true")
    ap.add_argument("--target", dest="board", default=None,
                    help="platform target (see targets/); supplies the default "
                         "--aot-target")
    ap.add_argument("--aot-target", default=None,
                    help="wamrc backend (" + "|".join(sorted(aster_board.AOT_BACKENDS)) +
                         "; default: the platform aot_target)")
    ap.add_argument("--clean", action="store_true", help="remove build/ and dist/ and exit")
    args = ap.parse_args(argv)

    # An AOT image is built for one CPU and cannot be retargeted afterwards, so
    # the backend has to come from somewhere authoritative. It comes from the
    # board preset -- the same place deploy/flash read theirs -- unless the
    # caller overrides it explicitly for a one-off.

    if args.clean:
        shutil.rmtree(BUILD_ROOT, ignore_errors=True)
        shutil.rmtree(OUT_DIR, ignore_errors=True)
        print("cleaned")
        return 0

    target = aster_board.get(args.board)
    if args.aot_target and args.aot_target != target["aot_target"]:
        ap.error("--aot-target conflicts with the selected target")
    args.aot_target = target["aot_target"]
    BUILD_ROOT = output_root() / "build" / target["id"]
    OUT_DIR = output_root() / "dist" / target["id"]

    targets: list[Path] = list(args.apps)
    if args.all:
        targets += sorted(p.parent for p in APPS_DIR.glob("*/app.toml"))
    if not targets:
        ap.error("no apps given (use paths or --all)")

    results = []
    for t in targets:
        d = t if t.is_dir() else Path.cwd() / t
        app = App(d.resolve())
        from target_checks import validate_app
        validate_app(app, target)
        app.target = target
        want_aot = app.aot
        if args.aot:
            want_aot = True
        if args.no_aot:
            want_aot = False
        package = build_app(app, want_aot, args.aot_target)
        from verify_ast import check
        verification_key = None
        if os.environ.get("ASTER_SIGN_KEY"):
            from cryptography.hazmat.primitives import serialization
            key = serialization.load_pem_private_key(Path(os.environ["ASTER_SIGN_KEY"]).read_bytes(), password=None)
            verification_key = BUILD_ROOT / app.id / "signer-public.pem"
            verification_key.write_bytes(key.public_key().public_bytes(
                serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        check(package, pub_pem=verification_key, board=target["id"])
        results.append(package)
    print("\n".join(f"OK  {m}" for m in results))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
