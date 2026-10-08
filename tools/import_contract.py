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
    api=json.loads(files['contract/api.json'])
    if api.get('product')!='aster' or api.get('abi')!=lock.get('abi'):
        raise ValueError('API ABI differs from contract lock')
    for name,data in files.items():
        if name.startswith('targets/'):
            target=json.loads(data)
            if target.get('product')!='aster' or target.get('abi')!=lock.get('abi') or name!='targets/'+target.get('id','')+'.json':
                raise ValueError('Target identity or ABI differs from contract lock')
    return files


def import_bundle(bundle, root=ROOT):
    files = read_bundle(bundle)
    root = Path(root).resolve()
    for name in files:
        if not (root / name).resolve().is_relative_to(root):
            raise ValueError("bundle destination escapes SDK")
    previous = root / "contract/lock.json"
    old_names = json.loads(previous.read_text(encoding='utf-8'))["sha256"] if previous.exists() else {}
    for name in old_names:
        if name not in files and (name.startswith("include/aster/") or name.startswith("targets/")):
            path = root / name
            if path.resolve().is_relative_to(root):
                path.unlink(missing_ok=True)
    for name, data in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()
    import_bundle(args.bundle)
    print("Imported contract; review and commit include/, contract/, targets/ together.")


if __name__ == "__main__":
    main()
