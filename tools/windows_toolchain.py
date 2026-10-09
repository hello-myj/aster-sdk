"""Entry point for the standalone Windows Aster APP toolchain executable."""
import argparse
import os
import sys

import build
from runtime_paths import sdk_root
from release_identity import version_requested


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if version_requested(argv):
        return 0
    if not argv or argv[0] in ("-h", "--help"):
        print("usage: aster-toolchain.exe build <app-dir> --target xtensa [--aot|--no-aot]")
        print("       aster-toolchain.exe verify <package.ast> --target xtensa")
        return 0
    command = argv.pop(0)
    if command == "build":
        if os.name == "nt":
            sdk = sdk_root() / "toolchain" / "wasi-sdk-24.0-x86_64-windows" / "bin"
            os.environ["PATH"] = str(sdk) + os.pathsep + os.environ.get("PATH", "")
        return build.main(argv)
    if command == "verify":
        from pathlib import Path
        from verify_ast import check
        parser = argparse.ArgumentParser(prog="aster-toolchain verify")
        parser.add_argument("package", type=Path)
        parser.add_argument("--target", required=True)
        parser.add_argument("--pubkey", type=Path)
        args = parser.parse_args(argv)
        check(args.package, pub_pem=args.pubkey, board=args.target)
        return 0
    raise SystemExit("unknown command: " + command)


if __name__ == "__main__":
    raise SystemExit(main())
