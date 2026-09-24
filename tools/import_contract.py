#!/usr/bin/env python3
"""Explicit release update from an exported contract ZIP, not firmware sources."""
import argparse
import hashlib
import json
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]


def read_bundle(path):
    with zipfile.ZipFile(path) as archive:
        if len(archive.namelist()) != len(set(archive.namelist())):
            raise ValueError("duplicate bundle entries")
        files = {name: archive.read(name) for name in archive.namelist()}
    lock = json.loads(files["contract/lock.json"])
    if lock.get("product") != "aster" or lock.get("schema") != 1:
        raise ValueError("unsupported contract")
    if set(files) != set(lock["sha256"]) | {"contract/lock.json"}:
        raise ValueError("bundle inventory mismatch")
    for name, digest in lock["sha256"].items():
        rel = PurePosixPath(name)
        if rel.is_absolute() or ".." in rel.parts or "\\" in name:
            raise ValueError("unsafe bundle path")
        if not (name.startswith("include/aster/") or name.startswith("targets/") or name == "contract/api.json"):
            raise ValueError("unexpected bundle member: " + name)
        if hashlib.sha256(files[name]).hexdigest() != digest:
            raise ValueError("contract digest mismatch: " + name)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()
    files = read_bundle(args.bundle)  # validate the entire archive before modifying anything
    for name in files:
        if not (ROOT / name).resolve().is_relative_to(ROOT):
            raise ValueError("bundle destination escapes SDK")
    previous = ROOT / "contract/lock.json"
    old_names = json.loads(previous.read_text())["sha256"] if previous.exists() else {}
    for name in old_names:
        if name not in files and (name.startswith("include/aster/") or name.startswith("targets/")):
            path = ROOT / name
            if path.resolve().is_relative_to(ROOT):
                path.unlink(missing_ok=True)
    for name, data in files.items():
        path = ROOT / name
        if not path.resolve().is_relative_to(ROOT):
            raise ValueError("bundle destination escapes SDK")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    print("Imported contract; review and commit include/, contract/, targets/ together.")


if __name__ == "__main__":
    main()
