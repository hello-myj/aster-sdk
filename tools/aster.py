#!/usr/bin/env python3
"""Aster app SDK: new, targets, build, verify, deploy."""
import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ("build", "verify", "deploy"):
        scripts = {"build": "build.py", "verify": "verify_mja.py", "deploy": "deploy.py"}
        return subprocess.call([sys.executable, str(ROOT / "tools" / scripts[sys.argv[1]]), *sys.argv[2:]])
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("targets")
    new = commands.add_parser("new")
    new.add_argument("directory", type=Path)
    new.add_argument("--id", required=True)
    new.add_argument("--name", default="Hello Aster")
    args = parser.parse_args()
    if args.command == "targets":
        import aster_board
        for name, target in sorted(aster_board.targets().items()):
            print(f"{name:24} {target['cpu']:10} " + ", ".join(target["capabilities"]))
        return 0
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,62}", args.id) or ".." in args.id:
        parser.error("--id must be a safe app identifier, up to 63 characters")
    if args.directory.exists():
        parser.error("destination already exists")
    shutil.copytree(ROOT / "apps/hello", args.directory)
    manifest = args.directory / "app.toml"
    source = manifest.read_text().replace('id = "aster.hello"', "id = " + json.dumps(args.id))
    source = source.replace('name = "Hello Aster"', "name = " + json.dumps(args.name, ensure_ascii=False))
    manifest.write_text(source)
    print(args.directory.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
